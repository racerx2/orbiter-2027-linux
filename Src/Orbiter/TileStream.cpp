// not upstream: the tile stream service: queue, fetch, build, write and size cap (TileStream.h)

#include "TileStream.h"
#include "TileStreamNet.h"
#include <QColor>
#include <QFile>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <iterator>
#include <deque>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <unistd.h>

namespace fs = std::filesystem;
using Clock = TileStream::Clock;

namespace {

enum { EOX, GIBS, TERR, WC, BLDG, NHOST };
const char *HostName[NHOST] = {"EOX", "GIBS", "Terrain Tiles", "WorldCover", "OpenFreeMap"};

uint64_t Key (int L, int ilat, int ilng) { return (uint64_t)L << 48 | (uint64_t)ilat << 24 | (uint64_t)ilng; }
int KL (uint64_t k) { return (int)(k >> 48 & 0xFF); }
int KLat (uint64_t k) { return (int)(k >> 24 & 0xFFFFFF); }
int KLng (uint64_t k) { return (int)(k & 0xFFFFFF); }
uint64_t Id (int body, uint64_t k) { return (uint64_t)body << 56 | k; }

std::string Tile (const std::string &dir, const char *layer, uint64_t k)
{
	char b[48];
	snprintf (b, sizeof b, "/%s/%02d/%06d/%06d.%s", layer, KL (k), KLat (k), KLng (k), strncmp (layer, "Elev", 4) ? "dds" : "elv");
	return dir + b;
}

int64_t Today ()
{
	return std::chrono::duration_cast<std::chrono::hours> (std::chrono::system_clock::now().time_since_epoch()).count() / 24;
}

int HostOf (const TsSource &s) { return s.kind == "eox" ? EOX : s.kind == "terrarium" ? TERR : GIBS; }

std::string BldgFile (const std::string &dir, uint64_t k)
{
	return dir + "/Bldg/14/" + std::to_string (KLat (k)) + "/" + std::to_string (KLng (k)) + ".bld";
}

// the file behind an entry of bldg.idx, mod.idx or cloud.idx
std::string StreamFile (const std::string &dir, const std::string &idx, uint64_t k)
{
	return idx == "bldg" ? BldgFile (dir, k) : Tile (dir, idx == "mod" ? "Elev_mod" : "Cloud", k);
}

}

struct TileStream::Impl {
	struct Body {
		TsBodyCfg cfg;
		std::string dir;
		std::unordered_set<uint64_t> written, ocean, refetch;
		std::unordered_map<uint64_t, int64_t> none; // key -> day
		std::deque<uint64_t> done;
		std::vector<uint64_t> hits;
		std::deque<uint64_t> refetchQueue; // fed in slowly when nothing else waits
		std::unordered_set<uint64_t> elevSeen, noElev; // this session: checked for an Elev file, Elev failed
		std::unordered_set<uint64_t> cloud, mod;        // earth-sky: Cloud and Elev_mod files this stream wrote
		std::vector<TsBase> bases;
		std::vector<double> baseH;                      // flat height per base, NaN until known
		double radius = 6.37101e6;
		std::unordered_set<uint64_t> bldg; // building tiles written, Key (14, x, y)
		std::string bldgUrl;               // from the TileJSON
		int bldgState = 0;                 // 0 TileJSON not asked, 1 asking, 2 known
	};
	struct BJob { int body = 0, stage = 0, tries = 0; uint64_t key = 0; Clock::time_point want, retry; };
	struct Src {
		std::string url;
		int host = EOX;
		int status = -1;
		std::shared_ptr<const QByteArray> data;
	};
	struct Job {
		uint64_t id = 0, key = 0;
		int body = 0, stage = 0, tries = 0, waiting = 0; // stage 0 queued, 1 fetching, 2 building
		bool required = false, low = false, fallback = false, elevOnly = false;
		bool pre = false;            // earth-sky: ground-track prefetch
		uint64_t seq = 0;
		std::vector<int> baseIdx;    // earth-sky: bases whose centre tiles follow the grid in elev
		Clock::time_point want, retry;
		std::vector<Src> surf, lights, water, elev;
	};
	struct Host {
		int cap = 4, inflight = 0, fails = 0;
		double backoff = 60.0;
		Clock::time_point paused;
		std::deque<std::string> queue;
	};
	struct Result {
		std::string url;
		int host, status, retryAfter;
		QByteArray body;
	};
	using Lru = std::list<std::pair<std::string, std::shared_ptr<const QByteArray>>>;

	Options opt;
	mutable std::mutex mx;
	std::condition_variable cv, wcv;
	bool stop = false, kick = false;
	std::atomic<bool> quit {false}; // read by long scans without the lock
	std::vector<std::unique_ptr<Body>> bodies;
	std::unordered_map<uint64_t, Job> jobs;
	std::unordered_map<uint64_t, Clock::time_point> hold;
	std::unordered_map<std::string, std::vector<uint64_t>> inflight;
	Lru lru;
	std::unordered_map<std::string, Lru::iterator> lruIdx;
	size_t lruBytes = 0;
	Host host[NHOST];
	std::deque<Result> results;
	std::deque<std::function<void ()>> work;
	std::unique_ptr<TsNet> net;
	std::thread ctl;
	std::vector<std::thread> workers;
	Clock::time_point nextEvict, nextHits;
	std::mutex imx;
	std::list<std::pair<uint64_t, QImage>> imgs;
	int tmpSeq = 0;
	std::condition_variable scv; // a worker's WorldCover range reads
	std::unordered_map<std::string, int> syncWait;
	std::unordered_map<std::string, std::pair<int, std::shared_ptr<const QByteArray>>> syncRes;
	int coverFails = 0;
	void FetchAll (std::vector<TsCog::Req> &rq);
	std::unordered_map<uint64_t, BJob> bjobs;               // building tiles, by Id (body, Key (14, x, y))
	std::unordered_map<std::string, uint64_t> binflight;    // url -> bjob id
	std::unordered_map<std::string, int> bjson;             // TileJSON url -> body
	std::unordered_map<uint64_t, Clock::time_point> bhold;

	explicit Impl (const Options &o);
	~Impl ();

	void Log (const std::string &s) { if (opt.log) opt.log (s); }

	int FindBody (const std::string &name) const
	{
		for (int i = 0; i < (int)bodies.size(); i++)
			if (bodies[i]->cfg.name == name) return i;
		return -1;
	}

	Job &NewJob (int b, uint64_t k, Clock::time_point now)
	{
		uint64_t id = Id (b, k);
		Job &J = jobs[id];
		J.id = id;
		J.key = k;
		J.body = b;
		J.want = now;
		return J;
	}

	bool Ocean (const Body &B, uint64_t k) const
	{
		int L = KL (k), la = KLat (k), ln = KLng (k);
		for (int d = 0; L - d >= 4; d++)
			if (B.ocean.count (Key (L - d, la >> d, ln >> d))) return true;
		return false;
	}

	int Want (const std::string &name, int L, int ilat, int ilng, int flags);
	void Trim ();
	void Run ();
	void Work ();
	void Queue (std::function<void ()> fn) { work.push_back (std::move (fn)); wcv.notify_one(); }
	void OnResult (Result &r, Clock::time_point now);
	void Deliver (const std::string &url, int status, const std::shared_ptr<const QByteArray> &data, Clock::time_point now);
	void Pause (int h, Clock::time_point until, Clock::time_point now);
	void Fetched (Job &J, Clock::time_point now);
	void Retry (Job &J, Clock::time_point now, int sec);
	bool Paused (int h, Clock::time_point now) const { return now < host[h].paused; }
	bool CanFallback (const Body &B, int L) const
	{
		const TsSource &fb = B.cfg.fallback;
		return !fb.kind.empty() && L >= fb.lmin && L <= fb.lmax;
	}
	int Startable (const Job &J, Clock::time_point now) const;
	bool AddLow (Clock::time_point now);
	void StartJobs (Clock::time_point now);
	void Start (Job &J, Clock::time_point now);
	std::vector<Src> Srcs (const TsSource &s, int L, int la, int ln);
	void Dispatch (Clock::time_point now);
	void DropStale (Clock::time_point now);
	void Hits ();
	std::shared_ptr<const QByteArray> CacheGet (const std::string &url);
	void CachePut (const std::string &url, const std::shared_ptr<const QByteArray> &d);
	void CacheDrop (const std::string &url);
	void Build (Job J);
	void Failed (const Job &J);
	QImage Ancestor (int body, int L, int la, int ln);
	void Remember (uint64_t id, const QImage &img);
	QImage Recall (uint64_t id);
	bool Write (const Body &B, const std::string &path, const std::string &bytes);
	std::string Idx (const Body &B, const char *name) const { return B.dir + "/TileStream/" + name + ".idx"; }
	void LoadIdx (Body &B);
	void Append (const Body &B, const char *name, uint64_t k, int64_t t);
	void Rewrite (const Body &B, const char *name, const std::unordered_set<uint64_t> &s);
	void MarkNone (Body &B, uint64_t k);
	void CleanTmp ();
	void Evict ();
	// earth-sky: prefetch, clouds, base flattening (end of this file)
	uint64_t preSeq = 0;
	int Prefetch (const std::string &name, int L, int ilat, int ilng);
	QByteArray Fetch (const std::string &url, int sec);
	QByteArray Source (Body &B, const std::string &url, const char *file, int sec);
	void Clouds (Body &B);
	void BakeClouds (Body &B, const QImage &src, int l0, int l1);
	void LoadBases (Body &B);
	void BaseSrcs (Body &B, Job &J);
	void BaseHeights (Body &B, const Job &J, const std::vector<QImage> &et);
	bool Mods (Body &B, uint64_t k, const std::string &elv);
	int WantBldg (int b, int x, int y);       // TileStreamMvt.cpp's tiles, below imagery
	void StartBldg (Clock::time_point now);
	void BldgDeliver (const std::string &url, int status, const std::shared_ptr<const QByteArray> &data, Clock::time_point now);
	void BuildBldg (int b, uint64_t k, std::shared_ptr<const QByteArray> data);
};

static bool Better (const TileStream::Impl::Job &a, const TileStream::Impl::Job &b)
{
	if (a.low != b.low) return !a.low;
	if (a.pre != b.pre) return !a.pre; // earth-sky: prefetch below wants, above refetches
	if (a.pre) return a.seq < b.seq;  // along the track, nearest first
	if (a.required != b.required) return a.required;
	if (a.elevOnly != b.elevOnly) return !a.elevOnly;
	if (KL (a.key) != KL (b.key)) return KL (a.key) < KL (b.key);
	return a.want > b.want;
}

TileStream::Impl::Impl (const Options &o)
: opt(o)
{
	if (!opt.now) opt.now = [] { return Clock::now(); };
	host[EOX].cap = 6;
	host[GIBS].cap = 4;
	host[TERR].cap = 8;
	host[WC].cap = 6;
	host[BLDG].cap = 4;
	std::vector<TsBodyCfg> cfg;
	std::string err;
	if (!TsReadCfg (opt.cfgfile, cfg, err)) {
		Log ("TileStream: " + err);
		return;
	}
	for (const TsBodyCfg &c : cfg) {
		if (std::find (opt.bodies.begin(), opt.bodies.end(), c.name) == opt.bodies.end()) continue;
		auto b = std::make_unique<Body>();
		b->cfg = c;
		b->dir = opt.bodyDir ? opt.bodyDir (c.name) : std::string();
		std::replace (b->dir.begin(), b->dir.end(), '\\', '/');
		while (b->dir.size() > 1 && b->dir.back() == '/') b->dir.pop_back();
		std::error_code ec;
		bool ok = !b->dir.empty();
		for (const char *s : {"/Surf", "/Mask", "/Elev", "/Class", "/TileStream/tmp", "/Cloud", "/Elev_mod"})
			if (ok) { fs::create_directories (b->dir + s, ec); ok = !ec; }
		if (!ok) {
			Log ("TileStream: cannot create the tile folders in " + b->dir);
			continue;
		}
		LoadIdx (*b);
		Log ("TileStream: " + c.name + " tiles go to " + b->dir + (opt.offline ? " (offline)" : ""));
		bodies.push_back (std::move (b));
	}
	if (bodies.empty()) return;
	if (!opt.offline) net = std::make_unique<TsNet> (opt.userAgent);
	if (opt.sky) for (auto &b : bodies) { LoadBases (*b); Clouds (*b); } // earth-sky: before the planet visuals load their tiles
	Clock::time_point now = opt.now();
	nextHits = now + std::chrono::seconds (5);
	nextEvict = now + std::chrono::seconds (300);
	work.push_back ([this] { CleanTmp (); Evict (); });
	if (net) {
		for (int b = 0; b < (int)bodies.size(); b++) {
			Body &B = *bodies[b];
			for (uint64_t k : {Key (1, 0, 0), Key (2, 0, 0), Key (3, 0, 0), Key (4, 0, 0), Key (4, 0, 1)}) {
				std::error_code ec;
				if (!fs::exists (Tile (B.dir, "Surf", k), ec)) NewJob (b, k, now).required = true;
			}
			for (uint64_t k : B.refetch)
				if (B.written.count (k)) B.refetchQueue.push_back (k);
		}
	}
	for (int i = 0; i < 4; i++) workers.emplace_back ([this] { Work (); }); // WorldCover reads and the cloud bake wait in a worker
	ctl = std::thread ([this] { Run (); });
}

TileStream::Impl::~Impl ()
{
	{
		std::lock_guard<std::mutex> lk (mx);
		stop = true;
	}
	quit = true;
	cv.notify_all();
	wcv.notify_all();
	scv.notify_all();
	if (ctl.joinable()) ctl.join();
	for (std::thread &t : workers) t.join();
	net.reset();
}

int TileStream::Impl::Want (const std::string &name, int L, int ilat, int ilng, int flags)
{
	int b = FindBody (name);
	if (b < 0) return -1;
	Body &B = *bodies[b];
	if (L < 1 || L > B.cfg.surf.lmax || ilat < 0 || ilng < 0) return -1;
	if (L < 4 ? (ilat || ilng) : (ilat >= 1 << (L - 4) || ilng >= 1 << (L - 3))) return -1;
	uint64_t k = Key (L, ilat, ilng), id = Id (b, k);
	std::unique_lock<std::mutex> lk (mx);
	if (stop) return -1;
	if (flags & 2) {
		if (!B.written.count (k)) return 1;
		B.hits.push_back (k);
		const TsSource &es = B.cfg.elev;
		if (!net || es.kind.empty() || L < 4 || L > es.lmax || !B.elevSeen.insert (k).second || B.noElev.count (k) || jobs.count (id)) return 1;
		lk.unlock();
		std::error_code ec;
		bool have = fs::exists (Tile (B.dir, "Elev", k), ec);
		lk.lock();
		if (stop || have || jobs.count (id)) return 1;
		NewJob (b, k, opt.now()).elevOnly = true; // a tile from before the stream made Elev
		Trim ();
		kick = true;
		cv.notify_all();
		return 1;
	}
	if (flags & 1) {
		if (!B.written.erase (k)) return -1; // not ours: never replaced
		lk.unlock();
		std::error_code ec;
		fs::remove (Tile (B.dir, "Surf", k), ec);
		fs::remove (Tile (B.dir, "Mask", k), ec);
		fs::remove (Tile (B.dir, "Class", k), ec);
		lk.lock();
		if (stop) return -1;
	}
	if (!net || Ocean (B, k)) return -1;
	auto n = B.none.find (k);
	if (n != B.none.end()) {
		if (Today() - n->second < 30) return -1;
		B.none.erase (n);
	}
	Clock::time_point now = opt.now();
	auto h = hold.find (id);
	if (h != hold.end()) {
		if (now < h->second) return 0; // still wanted, asked again once the hold ends
		hold.erase (h);
	}
	auto j = jobs.find (id);
	if (j != jobs.end() && !j->second.elevOnly) {
		j->second.want = now;
		j->second.low = j->second.pre = false; // earth-sky: a want takes over a prefetch
		return 0;
	}
	if (!(flags & 1)) {
		lk.unlock();
		std::error_code ec;
		bool have = fs::exists (Tile (B.dir, "Surf", k), ec);
		lk.lock();
		if (stop) return -1;
		if (have) return 1;
		j = jobs.find (id);
		if (j != jobs.end()) {
			j->second.want = now;
			j->second.low = j->second.pre = false;
			return 0; // an Elev job in the way: asked again when it is done
		}
	}
	if (jobs.count (id)) return 0;
	NewJob (b, k, now);
	Trim ();
	kick = true;
	cv.notify_all();
	return 0;
}

// over the cap: queued refetches go first, then the stalest want
void TileStream::Impl::Trim ()
{
	while (jobs.size() > 256) {
		auto worst = jobs.end();
		for (auto it = jobs.begin(); it != jobs.end(); ++it) {
			const Job &J = it->second;
			if (J.stage || J.required) continue;
			auto rank = [] (const Job &x) { return x.low || x.elevOnly ? 2 : x.pre ? 1 : 0; }; // earth-sky: prefetch goes before wants
			int jl = rank (J), wl = worst != jobs.end() ? rank (worst->second) : -1;
			if (worst == jobs.end() || jl > wl || (jl == wl && J.want < worst->second.want)) worst = it;
		}
		if (worst == jobs.end()) return;
		if (worst->second.low) bodies[worst->second.body]->refetchQueue.push_back (worst->second.key);
		if (worst->second.elevOnly) bodies[worst->second.body]->elevSeen.erase (worst->second.key);
		jobs.erase (worst);
	}
}

void TileStream::Impl::Run ()
{
	std::unique_lock<std::mutex> lk (mx);
	while (!stop) {
		cv.wait_for (lk, std::chrono::milliseconds (50), [this] { return stop || kick; });
		kick = false;
		if (stop) break;
		Clock::time_point now = opt.now();
		while (!results.empty()) {
			Result r = std::move (results.front());
			results.pop_front();
			OnResult (r, now);
		}
		DropStale (now);
		StartJobs (now);
		StartBldg (now);
		Dispatch (now);
		if (now >= nextHits) {
			nextHits = now + std::chrono::seconds (5);
			Hits ();
		}
		if (now >= nextEvict) {
			nextEvict = now + std::chrono::seconds (300);
			Queue ([this] { Evict (); });
		}
	}
}

void TileStream::Impl::Work ()
{
	std::unique_lock<std::mutex> lk (mx);
	for (;;) {
		wcv.wait (lk, [this] { return stop || !work.empty(); });
		if (stop) return;
		std::function<void ()> fn = std::move (work.front());
		work.pop_front();
		lk.unlock();
		fn ();
		lk.lock();
	}
}

void TileStream::Impl::OnResult (Result &r, Clock::time_point now)
{
	Host &h = host[r.host];
	h.inflight = std::max (0, h.inflight - 1);
	std::shared_ptr<const QByteArray> data;
	if (r.status == 200) {
		data = std::make_shared<const QByteArray> (std::move (r.body));
		if (r.host != BLDG) CachePut (r.url, data);
	}
	if (r.status == 200 || r.status == 204 || r.status == 404 || r.status == 400) {
		h.fails = 0;
		h.backoff = 60.0;
	} else if (r.status == 429 || r.status == 503) {
		Pause (r.host, now + std::chrono::seconds (r.retryAfter > 0 ? std::min (r.retryAfter, 3600) : 30), now);
	} else if (++h.fails >= 5) {
		Log (std::string ("TileStream: ") + HostName[r.host] + " is not answering, paused for " + std::to_string ((int)h.backoff) + " s");
		Pause (r.host, now + std::chrono::milliseconds ((int64_t)(h.backoff * 1000.0)), now);
		h.backoff = std::min (h.backoff * 2.0, 600.0);
		h.fails = 0;
	}
	if (r.host == BLDG) BldgDeliver (r.url, r.status, data, now);
	else Deliver (r.url, r.status, data, now);
}

// a host that pauses gives its queued requests back at once, so their jobs fall back or wait in the queue, not in a slot
void TileStream::Impl::Pause (int h, Clock::time_point until, Clock::time_point now)
{
	host[h].paused = until;
	std::deque<std::string> q;
	q.swap (host[h].queue);
	for (const std::string &url : q) {
		if (h == BLDG) BldgDeliver (url, 0, nullptr, now);
		else Deliver (url, 0, nullptr, now);
	}
}

void TileStream::Impl::Deliver (const std::string &url, int status, const std::shared_ptr<const QByteArray> &data, Clock::time_point now)
{
	auto it = inflight.find (url);
	if (it == inflight.end()) return;
	std::vector<uint64_t> ids = std::move (it->second);
	inflight.erase (it);
	if (syncWait.count (url)) {
		syncRes[url] = {status, data};
		scv.notify_all();
	}
	for (uint64_t id : ids) {
		auto j = jobs.find (id);
		if (j == jobs.end() || j->second.stage != 1) continue;
		Job &J = j->second;
		for (std::vector<Src> *v : {&J.surf, &J.lights, &J.water, &J.elev})
			for (Src &s : *v)
				if (s.status < 0 && s.url == url) {
					s.status = status;
					s.data = data;
					J.waiting--;
				}
		if (J.waiting <= 0) Fetched (J, now);
	}
}

void TileStream::Impl::Retry (Job &J, Clock::time_point now, int sec)
{
	J.stage = 0;
	J.waiting = 0;
	J.retry = now + std::chrono::seconds (sec);
	J.surf.clear();
	J.lights.clear();
	J.water.clear();
	J.elev.clear();
}

// all sources answered: build, fall back, retry or give up
void TileStream::Impl::Fetched (Job &J, Clock::time_point now)
{
	if (J.elevOnly) {
		for (const Src &s : J.elev)
			if (s.status != 200) {
				bodies[J.body]->noElev.insert (J.key);
				jobs.erase (J.id);
				return;
			}
		J.stage = 2;
		Job copy = J;
		Queue ([this, copy] { Build (copy); });
		return;
	}
	bool miss[3] = {false, false, false}, fail[3] = {false, false, false};
	int i = 0;
	for (std::vector<Src> *v : {&J.surf, &J.lights, &J.water}) {
		for (const Src &s : *v) {
			if (s.status == 404 || s.status == 400) miss[i] = true;
			else if (s.status != 200) fail[i] = true;
		}
		i++;
	}
	Body &B = *bodies[J.body];
	int L = KL (J.key);
	bool surfHost = !J.fallback && HostOf (B.cfg.surf) == EOX && CanFallback (B, L);
	if ((miss[0] || (fail[0] && Paused (EOX, now))) && surfHost) {
		J.fallback = true;
		Retry (J, now, 0);
		return;
	}
	if (fail[0] || fail[1] || fail[2]) {
		if (++J.tries > 5 && !J.required) jobs.erase (J.id);
		else Retry (J, now, 10);
		return;
	}
	if (miss[0] || miss[1] || miss[2]) {
		MarkNone (B, J.key);
		jobs.erase (J.id);
		return;
	}
	J.stage = 2;
	Job copy = J;
	Queue ([this, copy] { Build (copy); });
}

// 0: not now, 1: from its own source, 2: from the fallback while EOX is paused
int TileStream::Impl::Startable (const Job &J, Clock::time_point now) const
{
	const Body &B = *bodies[J.body];
	if (J.elevOnly) return Paused (TERR, now) ? 0 : 1;
	if (Paused (GIBS, now)) return 0; // the mask needs GIBS
	if (J.fallback || HostOf (B.cfg.surf) != EOX || !Paused (EOX, now)) return 1;
	return CanFallback (B, KL (J.key)) ? 2 : 0;
}

// one refetch of a fallback tile at a time, when nothing else is queued
bool TileStream::Impl::AddLow (Clock::time_point now)
{
	for (auto &p : jobs) if (p.second.low) return false;
	for (int b = 0; b < (int)bodies.size(); b++) {
		Body &B = *bodies[b];
		while (!B.refetchQueue.empty()) {
			uint64_t k = B.refetchQueue.front();
			B.refetchQueue.pop_front();
			if (!B.refetch.count (k) || !B.written.count (k) || jobs.count (Id (b, k))) continue;
			NewJob (b, k, now).low = true;
			return true;
		}
	}
	return false;
}

void TileStream::Impl::StartJobs (Clock::time_point now)
{
	int active = 0, preActive = 0, preCap = std::max (1, opt.maxActive / 3); // earth-sky: prefetch never takes every slot
	for (auto &p : jobs) if (p.second.stage) { active++; preActive += p.second.pre; }
	while (active < opt.maxActive) {
		Job *best = nullptr;
		int how = 0;
		for (auto &p : jobs) {
			Job &J = p.second;
			if (J.stage || J.retry > now || (J.pre && preActive >= preCap)) continue;
			int s = Startable (J, now);
			if (s && (!best || Better (J, *best))) { best = &J; how = s; }
		}
		if (!best) {
			if (!Paused (EOX, now) && !Paused (GIBS, now) && AddLow (now)) continue;
			return;
		}
		if (how == 2) best->fallback = true;
		preActive += best->pre;
		Start (*best, now);
		active++;
	}
}

std::vector<TileStream::Impl::Src> TileStream::Impl::Srcs (const TsSource &s, int L, int la, int ln)
{
	TsImg::Grid g = TsImg::SrcGrid (s, L, la, ln);
	std::vector<Src> v;
	for (int r = 0; r < g.nrow; r++)
		for (int c = 0; c < g.ncol; c++) {
			Src x;
			x.url = TsImg::Url (s.url, g.z, g.row0 + r, g.col0 + c);
			x.host = HostOf (s);
			v.push_back (x);
		}
	return v;
}

void TileStream::Impl::Start (Job &J, Clock::time_point now)
{
	Body &B = *bodies[J.body];
	int L = KL (J.key), la = KLat (J.key), ln = KLng (J.key);
	if (!J.elevOnly) {
		J.surf = Srcs (J.fallback ? B.cfg.fallback : B.cfg.surf, L, la, ln);
		J.lights = Srcs (B.cfg.lights, L, la, ln);
		J.water = Srcs (B.cfg.water, L, la, ln);
	}
	if (J.elevOnly) J.elev = Srcs (B.cfg.elev, L, la, ln); // a Surf job never waits on Terrarium
	if (J.elevOnly) BaseSrcs (B, J); // earth-sky: base centre heights still unknown
	J.stage = 1;
	J.waiting = 0;
	for (std::vector<Src> *v : {&J.surf, &J.lights, &J.water, &J.elev})
		for (Src &s : *v) {
			if ((s.data = CacheGet (s.url))) {
				s.status = 200;
				continue;
			}
			J.waiting++;
			std::vector<uint64_t> &w = inflight[s.url];
			if (w.empty()) host[s.host].queue.push_back (s.url);
			if (std::find (w.begin(), w.end(), J.id) == w.end()) w.push_back (J.id);
		}
	if (!J.waiting) Fetched (J, now);
}

void TileStream::Impl::Dispatch (Clock::time_point now)
{
	if (!net) return;
	for (int h = 0; h < NHOST; h++) {
		Host &H = host[h];
		while (H.inflight < H.cap && !H.queue.empty() && !Paused (h, now)) {
			std::string url = std::move (H.queue.front());
			H.queue.pop_front();
			H.inflight++;
			auto back = [this, url, h] (int st, QByteArray body, int ra) {
				std::lock_guard<std::mutex> lk (mx);
				results.push_back ({url, h, st, ra, std::move (body)});
				kick = true;
				cv.notify_all();
			};
			size_t r = url.rfind ('#');
			long long off = -1, len = 0;
			if (h == WC && r != std::string::npos && sscanf (url.c_str() + r, "#%lld+%lld", &off, &len) == 2)
				net->Get (url.substr (0, r), back, off, len); // url#off+len: a byte range
			else net->Get (url, back);
		}
	}
}

void TileStream::Impl::DropStale (Clock::time_point now)
{
	std::chrono::duration<double> stale (opt.staleSec);
	for (auto it = jobs.begin(); it != jobs.end(); ) {
		const Job &J = it->second;
		bool old = now - J.want > (J.pre ? std::chrono::duration<double> (10.0) : stale); // earth-sky: a prefetch the track left behind
		if (!J.stage && !J.required && !J.low && !J.elevOnly && old) it = jobs.erase (it);
		else ++it;
	}
	for (auto it = bjobs.begin(); it != bjobs.end(); ) {
		if (!it->second.stage && now - it->second.want > 3 * stale) it = bjobs.erase (it);
		else ++it;
	}
}

// the client loaded these from the cache: newer mtime, so the size cap takes them last
void TileStream::Impl::Hits ()
{
	std::vector<std::string> paths;
	for (auto &b : bodies) {
		std::sort (b->hits.begin(), b->hits.end());
		b->hits.erase (std::unique (b->hits.begin(), b->hits.end()), b->hits.end());
		for (uint64_t k : b->hits) paths.push_back (Tile (b->dir, "Surf", k));
		b->hits.clear();
	}
	if (paths.empty()) return;
	Queue ([paths] {
		std::error_code ec;
		auto t = fs::file_time_type::clock::now();
		for (const std::string &p : paths) fs::last_write_time (p, t, ec);
	});
}

std::shared_ptr<const QByteArray> TileStream::Impl::CacheGet (const std::string &url)
{
	auto it = lruIdx.find (url);
	if (it == lruIdx.end()) return nullptr;
	lru.splice (lru.begin(), lru, it->second);
	return it->second->second;
}

void TileStream::Impl::CacheDrop (const std::string &url)
{
	auto it = lruIdx.find (url);
	if (it == lruIdx.end()) return;
	lruBytes -= (size_t)it->second->second->size();
	lru.erase (it->second);
	lruIdx.erase (it);
}

void TileStream::Impl::CachePut (const std::string &url, const std::shared_ptr<const QByteArray> &d)
{
	CacheDrop (url);
	lru.emplace_front (url, d);
	lruIdx[url] = lru.begin();
	lruBytes += (size_t)d->size();
	while (lruBytes > ((size_t)64 << 20) && lru.size() > 1) {
		lruBytes -= (size_t)lru.back().second->size();
		lruIdx.erase (lru.back().first);
		lru.pop_back();
	}
}

void TileStream::Impl::Build (Job J)
{
	Body &B = *bodies[J.body];
	const TsBodyCfg &C = B.cfg;
	int L = KL (J.key), la = KLat (J.key), ln = KLng (J.key);
	const TsSource &sv = J.fallback ? C.fallback : C.surf;
	auto decode = [] (const std::vector<Src> &v, std::vector<QImage> &out) {
		for (const Src &s : v) {
			QImage im;
			if (!s.data || !im.loadFromData (*s.data) || im.isNull()) return false;
			out.push_back (im);
		}
		return true;
	};
	std::string elv; // optional: every Terrarium tile in hand, else none
	bool elevOk = !J.elev.empty();
	for (const Src &s : J.elev) elevOk = elevOk && s.status == 200 && s.data;
	if (elevOk) {
		std::vector<QImage> et;
		if (decode (J.elev, et)) elv = TsImg::Elev (L, la, ln, TsImg::SrcGrid (C.elev, L, la, ln), et, C.elevScale, C.floor);
		if (!elv.empty()) BaseHeights (B, J, et); // earth-sky
	}
	if (J.elevOnly) {
		bool ok = !elv.empty() && Mods (B, J.key, elv) && Write (B, Tile (B.dir, "Elev", J.key), elv); // earth-sky: its Elev_mod first
		std::lock_guard<std::mutex> lk (mx);
		if (!ok) {
			B.noElev.insert (J.key);
			for (const Src &s : J.elev) CacheDrop (s.url);
		}
		jobs.erase (J.id);
		kick = true;
		cv.notify_all();
		return;
	}
	std::vector<QImage> st, lt, wt;
	if (!decode (J.surf, st) || !decode (J.lights, lt) || !decode (J.water, wt)) {
		Failed (J);
		return;
	}
	bool ocean = false, ok = true;
	if (sv.kind == "eox" && L >= 4) {
		std::vector<bool> fill (st.size());
		size_t nfill = 0;
		for (size_t i = 0; i < st.size(); i++)
			if ((fill[i] = TsImg::IsFill (st[i], C.fill))) nfill++;
		if (nfill == st.size()) ocean = true;
		else if (nfill) {
			QImage anc = Ancestor (J.body, L, la, ln);
			for (size_t i = 0; i < st.size(); i++) {
				if (!fill[i]) continue;
				if (anc.isNull()) {
					st[i] = QImage (256, 256, QImage::Format_RGB888);
					st[i].fill (QColor (C.fill[0], C.fill[1], C.fill[2]));
				} else st[i] = anc.copy ((int)(i % 2) * 256, (int)(i / 2) * 256, 256, 256);
			}
		}
	}
	if (!ocean) {
		QImage surf = TsImg::Build (sv, L, la, ln, TsImg::SrcGrid (sv, L, la, ln), st);
		QImage lights = TsImg::Build (C.lights, L, la, ln, TsImg::SrcGrid (C.lights, L, la, ln), lt);
		QImage water = TsImg::Build (C.water, L, la, ln, TsImg::SrcGrid (C.water, L, la, ln), wt);
		QImage cls; // WorldCover: the water bit and the class tile, else GIBS water as before
		if (!C.cover.kind.empty() && L >= C.cover.lmin && L <= C.cover.lmax && net) {
			std::vector<uint8_t> cv;
			int n = water.width();
			if (TsCog::Classes (C.cover.url, L, la, ln, n, [this] (std::vector<TsCog::Req> &r) { FetchAll (r); }, cv)) {
				water = TsCog::Water (cv, n, water);
				cls = TsCog::ClassImage (cv, n, 256);
			} else {
				std::lock_guard<std::mutex> lk (mx);
				if (coverFails++ % 50 == 0) Log ("TileStream: WorldCover read failed, GIBS water for " + Tile (B.dir, "Mask", J.key));
			}
		}
		QImage mask = TsImg::Mask (lights, water, C.black, C.waterGrey);
		std::string mpath = Tile (B.dir, "Mask", J.key), cpath = Tile (B.dir, "Class", J.key);
		std::error_code ec;
		if (cls.isNull() || !Write (B, cpath, TsImg::Dds (cls, false))) fs::remove (cpath, ec); // before Surf: the client reads them together
		ok = Write (B, mpath, TsImg::Dds (mask, true));
		if (ok && !(ok = Write (B, Tile (B.dir, "Surf", J.key), TsImg::Dds (surf, false)))) {
			fs::remove (mpath, ec); // no Mask without its Surf
			fs::remove (cpath, ec);
		}
		if (ok && L >= 4) Remember (Id (J.body, J.key), surf);
		if (ok && !elv.empty()) Write (B, Tile (B.dir, "Elev", J.key), elv);
	}
	std::lock_guard<std::mutex> lk (mx);
	if (ocean) {
		B.ocean.insert (J.key);
		Append (B, "ocean", J.key, -1);
	} else if (ok) {
		B.written.insert (J.key);
		Append (B, "written", J.key, -1);
		B.done.push_back (J.key);
		if (B.done.size() > 1024) B.done.pop_front();
		if (J.fallback) {
			if (B.refetch.insert (J.key).second) Append (B, "refetch", J.key, -1);
		} else if (B.refetch.erase (J.key)) Rewrite (B, "refetch", B.refetch);
	} else {
		Log ("TileStream: cannot write " + Tile (B.dir, "Surf", J.key));
		hold[J.id] = opt.now() + std::chrono::seconds (60);
	}
	jobs.erase (J.id);
	const TsSource &es = C.elev;
	if (ok && !ocean && net && !es.kind.empty() && L >= 4 && L <= es.lmax && B.elevSeen.insert (J.key).second)
		NewJob (J.body, J.key, opt.now()).elevOnly = true; // its Elev, a job of its own
	kick = true;
	cv.notify_all();
}

// a worker's range reads on the WorldCover host, through its cap and pause; waits for all of them
void TileStream::Impl::FetchAll (std::vector<TsCog::Req> &rq)
{
	std::unique_lock<std::mutex> lk (mx);
	std::vector<std::string> keys;
	for (TsCog::Req &r : rq) {
		keys.push_back (r.url + "#" + std::to_string (r.off) + "+" + std::to_string (r.len));
		const std::string &k = keys.back();
		if ((r.data = CacheGet (k))) { r.status = 200; continue; }
		if (!net || stop || Paused (WC, opt.now())) { r.status = 0; continue; }
		r.status = -1;
		syncWait[k]++;
		if (!inflight.count (k)) {
			inflight[k];
			host[WC].queue.push_back (k);
		}
	}
	kick = true;
	cv.notify_all();
	auto ready = [&] {
		if (stop) return true;
		for (size_t i = 0; i < rq.size(); i++)
			if (rq[i].status < 0 && !syncRes.count (keys[i])) return false;
		return true;
	};
	scv.wait_for (lk, std::chrono::seconds (90), ready);
	for (size_t i = 0; i < rq.size(); i++) {
		if (rq[i].status >= 0) continue;
		auto it = syncRes.find (keys[i]);
		rq[i].status = it != syncRes.end() ? it->second.first : 0;
		if (it != syncRes.end()) rq[i].data = it->second.second;
		if (--syncWait[keys[i]] <= 0) {
			syncWait.erase (keys[i]);
			syncRes.erase (keys[i]);
		}
	}
}

// a source didn't decode: drop it from memory and fetch again, a few times
void TileStream::Impl::Failed (const Job &J)
{
	std::lock_guard<std::mutex> lk (mx);
	for (const std::vector<Src> *v : {&J.surf, &J.lights, &J.water, &J.elev})
		for (const Src &s : *v) CacheDrop (s.url);
	auto it = jobs.find (J.id);
	if (it == jobs.end()) return;
	Job &K = it->second;
	Clock::time_point now = opt.now();
	if (++K.tries >= 3) {
		hold[J.id] = now + std::chrono::seconds (600);
		jobs.erase (it);
	} else Retry (K, now, 30);
	kick = true;
	cv.notify_all();
}

// the nearest ancestor's pixels over this tile, 512 px
QImage TileStream::Impl::Ancestor (int body, int L, int la, int ln)
{
	const Body &B = *bodies[body];
	for (int d = 1; L - d >= 4; d++) {
		uint64_t ak = Key (L - d, la >> d, ln >> d);
		QImage a = Recall (Id (body, ak));
		if (a.isNull()) {
			QFile f (QString::fromStdString (Tile (B.dir, "Surf", ak)));
			if (f.open (QIODevice::ReadOnly)) a = TsImg::DdsImage (f.readAll());
		}
		if (!a.isNull()) return TsImg::Region (a, d, la, ln);
	}
	return QImage();
}

void TileStream::Impl::Remember (uint64_t id, const QImage &img)
{
	std::lock_guard<std::mutex> lk (imx);
	imgs.emplace_front (id, img);
	if (imgs.size() > 40) imgs.pop_back();
}

QImage TileStream::Impl::Recall (uint64_t id)
{
	std::lock_guard<std::mutex> lk (imx);
	for (auto &p : imgs)
		if (p.first == id) return p.second;
	return QImage();
}

// to a unique file in TileStream/tmp, then renamed into place: the client never reads half a file
bool TileStream::Impl::Write (const Body &B, const std::string &path, const std::string &bytes)
{
	std::error_code ec;
	fs::create_directories (fs::path (path).parent_path(), ec);
	int seq;
	{
		std::lock_guard<std::mutex> lk (imx);
		seq = ++tmpSeq;
	}
	std::string tmp = B.dir + "/TileStream/tmp/" + std::to_string (getpid()) + "." + std::to_string (seq) + ".tmp";
	{
		std::ofstream o (tmp, std::ios::binary);
		o.write (bytes.data(), (std::streamsize)bytes.size());
		o.close();
		if (!o) {
			fs::remove (tmp, ec);
			return false;
		}
	}
	fs::rename (tmp, path, ec);
	if (ec) {
		std::error_code rc;
		fs::remove (tmp, rc);
		return false;
	}
	return true;
}

void TileStream::Impl::LoadIdx (Body &B)
{
	auto load = [&] (const char *name, auto add) {
		std::ifstream f (Idx (B, name));
		std::string line;
		while (std::getline (f, line)) {
			std::istringstream is (line);
			int L, a, b;
			long long t = 0;
			if (!(is >> L >> a >> b) || L < 1 || L > 21 || a < 0 || b < 0) continue;
			is >> t;
			add (Key (L, a, b), (int64_t)t);
		}
	};
	load ("written", [&] (uint64_t k, int64_t) { B.written.insert (k); });
	load ("ocean", [&] (uint64_t k, int64_t) { B.ocean.insert (k); });
	load ("refetch", [&] (uint64_t k, int64_t) { B.refetch.insert (k); });
	load ("none", [&] (uint64_t k, int64_t t) { B.none[k] = t; });
	load ("bldg", [&] (uint64_t k, int64_t) { if (KL (k) == 14) B.bldg.insert (k); });
}

void TileStream::Impl::Append (const Body &B, const char *name, uint64_t k, int64_t t)
{
	std::ofstream f (Idx (B, name), std::ios::app);
	f << KL (k) << ' ' << KLat (k) << ' ' << KLng (k);
	if (t >= 0) f << ' ' << t;
	f << '\n';
}

// the old index stays when the new one can't be written whole
void TileStream::Impl::Rewrite (const Body &B, const char *name, const std::unordered_set<uint64_t> &s)
{
	std::string p = Idx (B, name), tmp = p + ".new";
	bool ok;
	{
		std::ofstream f (tmp, std::ios::trunc);
		for (uint64_t k : s) f << KL (k) << ' ' << KLat (k) << ' ' << KLng (k) << '\n';
		f.close();
		ok = !f.fail();
	}
	std::error_code ec;
	if (ok) fs::rename (tmp, p, ec);
	if (!ok || ec) {
		std::error_code rc;
		fs::remove (tmp, rc);
		Log ("TileStream: cannot rewrite " + p);
	}
}

void TileStream::Impl::MarkNone (Body &B, uint64_t k)
{
	int64_t t = Today();
	B.none[k] = t;
	Append (B, "none", k, t);
}

// .tmp files of an earlier run that was killed mid-write
void TileStream::Impl::CleanTmp ()
{
	std::string mine = std::to_string (getpid()) + ".";
	for (auto &b : bodies) {
		std::error_code ec;
		for (fs::directory_iterator it (b->dir + "/TileStream/tmp", ec), end; !ec && it != end && !quit; it.increment (ec)) {
			std::string n = it->path().filename().string();
			if (n.compare (0, mine.size(), mine)) {
				std::error_code rc;
				fs::remove (it->path(), rc);
			}
		}
	}
}

// the size cap: oldest written tiles first, never L <= 8, never a file this stream didn't write
void TileStream::Impl::Evict ()
{
	uint64_t cap = opt.capMB << 20;
	if (!cap) return;
	for (auto &bp : bodies) {
		Body &B = *bp;
		std::vector<uint64_t> keys, bkeys;
		{
			std::lock_guard<std::mutex> lk (mx);
			keys.assign (B.written.begin(), B.written.end());
			bkeys.assign (B.bldg.begin(), B.bldg.end());
		}
		struct E { fs::file_time_type t; uint64_t k, size; bool bldg; };
		std::vector<E> v;
		std::vector<uint64_t> gone, bgone;
		uint64_t total = 0;
		for (uint64_t k : bkeys) {
			if (quit) return;
			std::error_code e1;
			std::string s = BldgFile (B.dir, k);
			uint64_t size = fs::file_size (s, e1);
			if (e1) { bgone.push_back (k); continue; }
			total += size;
			v.push_back ({fs::last_write_time (s, e1), k, size, true});
		}
		for (uint64_t k : keys) {
			if (quit) return;
			std::error_code e1, e2;
			std::string s = Tile (B.dir, "Surf", k), m = Tile (B.dir, "Mask", k);
			uint64_t ss = fs::file_size (s, e1);
			if (e1) { gone.push_back (k); continue; }
			uint64_t ms = fs::file_size (m, e2);
			uint64_t size = ss + (e2 ? 0 : ms);
			uint64_t es = fs::file_size (Tile (B.dir, "Elev", k), e2);
			if (!e2) size += es;
			es = fs::file_size (Tile (B.dir, "Class", k), e2);
			if (!e2) size += es;
			total += size;
			if (KL (k) > 8) v.push_back ({fs::last_write_time (s, e1), k, size, false});
		}
		if (cap && total > cap) {
			std::sort (v.begin(), v.end(), [] (const E &a, const E &b) { return a.t < b.t; });
			for (const E &e : v) {
				if (total <= cap / 10 * 9) break;
				std::error_code ec;
				total -= e.size;
				if (e.bldg) {
					fs::remove (BldgFile (B.dir, e.k), ec);
					bgone.push_back (e.k);
					continue;
				}
				fs::remove (Tile (B.dir, "Surf", e.k), ec);
				fs::remove (Tile (B.dir, "Mask", e.k), ec);
				fs::remove (Tile (B.dir, "Elev", e.k), ec);
				fs::remove (Tile (B.dir, "Class", e.k), ec);
				gone.push_back (e.k);
			}
		}
		if (gone.empty() && bgone.empty()) continue;
		std::lock_guard<std::mutex> lk (mx);
		if (!bgone.empty()) {
			for (uint64_t k : bgone) B.bldg.erase (k);
			Rewrite (B, "bldg", B.bldg);
		}
		if (gone.empty()) continue;
		bool refetch = false;
		for (uint64_t k : gone) {
			B.written.erase (k);
			refetch |= B.refetch.erase (k) > 0;
		}
		Rewrite (B, "written", B.written);
		if (refetch) Rewrite (B, "refetch", B.refetch);
	}
}

// building tiles: their own jobs on the OpenFreeMap host, started when no imagery job waits
int TileStream::Impl::WantBldg (int b, int x, int y)
{
	Body &B = *bodies[b];
	if (!opt.buildings || B.cfg.bldgUrl.empty() || x < 0 || y < 0 || x >= 1 << 14 || y >= 1 << 14) return -1;
	uint64_t k = Key (14, x, y), id = Id (b, k);
	std::unique_lock<std::mutex> lk (mx);
	if (stop) return -1;
	if (B.bldg.count (k)) return 1;
	Clock::time_point now = opt.now();
	auto j = bjobs.find (id);
	if (j != bjobs.end()) {
		j->second.want = now;
		return 0;
	}
	lk.unlock();
	std::error_code ec;
	bool have = fs::exists (BldgFile (B.dir, k), ec);
	lk.lock();
	if (stop) return -1;
	if (have) return 1;
	if (!net) return -1;
	auto h = bhold.find (id);
	if (h != bhold.end()) {
		if (now < h->second) return 0;
		bhold.erase (h);
	}
	if (bjobs.count (id)) return 0;
	if (!B.bldgState) {
		B.bldgState = 1;
		bjson[B.cfg.bldgJson] = b;
		host[BLDG].queue.push_front (B.cfg.bldgJson);
	}
	BJob &J = bjobs[id];
	J.body = b;
	J.key = k;
	J.want = now;
	while (bjobs.size() > 64) {
		auto worst = bjobs.end();
		for (auto it = bjobs.begin(); it != bjobs.end(); ++it)
			if (!it->second.stage && (worst == bjobs.end() || it->second.want < worst->second.want)) worst = it;
		if (worst == bjobs.end()) break;
		bjobs.erase (worst);
	}
	kick = true;
	cv.notify_all();
	return 0;
}

void TileStream::Impl::StartBldg (Clock::time_point now)
{
	if (bjobs.empty() || Paused (BLDG, now)) return;
	int cap = host[BLDG].cap;
	for (auto &p : jobs)
		if (!p.second.stage && !p.second.low && p.second.retry <= now && Startable (p.second, now)) { cap = 1; break; } // imagery waits: one building tile at a time
	int active = 0;
	for (auto &p : bjobs) if (p.second.stage) active++;
	while (active < cap) {
		BJob *best = nullptr;
		for (auto &p : bjobs) {
			BJob &J = p.second;
			if (J.stage || J.retry > now || bodies[J.body]->bldgState != 2) continue;
			if (!best || J.want > best->want) best = &J;
		}
		if (!best) return;
		Body &B = *bodies[best->body];
		std::string url = B.bldgUrl;
		for (auto r : {std::make_pair ("{z}", 14), std::make_pair ("{x}", KLat (best->key)), std::make_pair ("{y}", KLng (best->key))}) {
			size_t at = url.find (r.first);
			if (at != std::string::npos) url.replace (at, 3, std::to_string (r.second));
		}
		best->stage = 1;
		binflight[url] = Id (best->body, best->key);
		host[BLDG].queue.push_back (url);
		active++;
	}
}

void TileStream::Impl::BldgDeliver (const std::string &url, int status, const std::shared_ptr<const QByteArray> &data, Clock::time_point now)
{
	auto js = bjson.find (url);
	if (js != bjson.end()) {
		Body &B = *bodies[js->second];
		bjson.erase (js);
		B.bldgUrl = B.cfg.bldgUrl;
		if (status == 200 && data) {
			std::string s = data->toStdString();
			size_t a = s.find ("\"tiles\""), q0 = a == std::string::npos ? a : s.find ('"', s.find ('[', a) + 1);
			size_t q1 = q0 == std::string::npos ? q0 : s.find ('"', q0 + 1);
			std::string t = q1 == std::string::npos ? std::string() : s.substr (q0 + 1, q1 - q0 - 1);
			if (t.find ("://") != std::string::npos && t.find ("{x}") != std::string::npos && t.find ("{y}") != std::string::npos) B.bldgUrl = t;
		}
		Log ("TileStream: " + B.cfg.name + " buildings from " + B.bldgUrl);
		B.bldgState = 2;
		kick = true;
		return;
	}
	auto it = binflight.find (url);
	if (it == binflight.end()) return;
	uint64_t id = it->second;
	binflight.erase (it);
	auto j = bjobs.find (id);
	if (j == bjobs.end() || j->second.stage != 1) return;
	BJob &J = j->second;
	if (status == 200 || status == 204 || status == 404) { // 204 and 404: no tile, written empty
		J.stage = 2;
		int b = J.body;
		uint64_t k = J.key;
		std::shared_ptr<const QByteArray> d = status == 200 ? data : nullptr;
		Queue ([this, b, k, d] { BuildBldg (b, k, d); });
		return;
	}
	if (++J.tries >= 3) {
		bhold[id] = now + std::chrono::seconds (60);
		bjobs.erase (j);
	} else {
		J.stage = 0;
		J.retry = now + std::chrono::seconds (10);
	}
}

void TileStream::Impl::BuildBldg (int b, uint64_t k, std::shared_ptr<const QByteArray> data)
{
	Body &B = *bodies[b];
	std::string raw = data ? data->toStdString() : std::string(), pbf;
	bool ok = true;
	if (raw.size() >= 2 && (uint8_t)raw[0] == 0x1f && (uint8_t)raw[1] == 0x8b) ok = TsBldg::Inflate (raw, pbf);
	else pbf.swap (raw);
	std::vector<TsBldg::Feature> f;
	uint32_t ext = 4096;
	ok = ok && TsBldg::Decode (pbf, f, ext);
	size_t n = 0;
	if (ok) {
		TsBldg::Mesh m = TsBldg::Build (14, KLat (k), KLng (k), f, ext);
		n = m.b.size();
		ok = Write (B, BldgFile (B.dir, k), TsBldg::Encode (m));
	}
	std::lock_guard<std::mutex> lk (mx);
	if (ok) {
		B.bldg.insert (k);
		Append (B, "bldg", k, -1);
		Log ("TileStream: buildings 14/" + std::to_string (KLat (k)) + "/" + std::to_string (KLng (k)) + ": " + std::to_string (n));
	} else {
		Log ("TileStream: cannot make buildings 14/" + std::to_string (KLat (k)) + "/" + std::to_string (KLng (k)));
		bhold[Id (b, k)] = opt.now() + std::chrono::seconds (600);
	}
	bjobs.erase (Id (b, k));
	kick = true;
	cv.notify_all();
}

// -----------------------------------------------------------------------

TileStream::TileStream (const Options &opt)
: d(std::make_unique<Impl> (opt))
{
}

TileStream::~TileStream () = default;

void TileStream::PrepareDirs (const std::string &cfgfile, const std::function<std::string (const std::string &)> &bodyDir)
{
	std::vector<TsBodyCfg> cfg;
	std::string err;
	if (!bodyDir || !TsReadCfg (cfgfile, cfg, err)) return;
	for (const TsBodyCfg &c : cfg) {
		std::string dir = bodyDir (c.name);
		std::replace (dir.begin(), dir.end(), '\\', '/');
		while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
		if (dir.empty()) continue;
		std::error_code ec;
		for (const char *s : {"/Surf", "/Mask", "/Elev", "/Class", "/TileStream/tmp", "/Cloud", "/Elev_mod"}) fs::create_directories (dir + s, ec);
	}
}

bool TileStream::Enabled (const std::string &body) const
{
	return d->FindBody (body) >= 0;
}

int TileStream::Want (const std::string &body, int L, int ilat, int ilng, int flags)
{
	return d->Want (body, L, ilat, ilng, flags);
}

int TileStream::Done (const std::string &body, int *keys, int nmax)
{
	int b = d->FindBody (body);
	if (b < 0 || !keys || nmax <= 0) return 0;
	std::lock_guard<std::mutex> lk (d->mx);
	Impl::Body &B = *d->bodies[b];
	int n = 0;
	for (; n < nmax && !B.done.empty(); n++) {
		uint64_t k = B.done.front();
		B.done.pop_front();
		keys[3 * n] = KL (k);
		keys[3 * n + 1] = KLat (k);
		keys[3 * n + 2] = KLng (k);
	}
	return n;
}

int TileStream::WantBldg (const std::string &body, int x, int y)
{
	int b = d->FindBody (body);
	return b < 0 ? -1 : d->WantBldg (b, x, y);
}

std::string TileStream::BldgPath (const std::string &body, int x, int y) const
{
	int b = d->FindBody (body);
	return b < 0 || x < 0 || y < 0 ? std::string() : BldgFile (d->bodies[b]->dir, Key (14, x, y));
}

std::string TileStream::Credit (const std::string &body) const
{
	int b = d->FindBody (body);
	if (b < 0) return std::string();
	std::string s;
	for (const std::string &c : d->bodies[b]->cfg.credit) s += (s.empty() ? "" : "\n") + c;
	return s;
}

size_t TileStream::Pending () const
{
	std::lock_guard<std::mutex> lk (d->mx);
	return d->jobs.size();
}

void TileStream::Evict ()
{
	d->Evict();
}

// -----------------------------------------------------------------------
// earth-sky: ground-track prefetch, clouds, base flattening

int TileStream::Prefetch (const std::string &body, int L, int ilat, int ilng)
{
	return d->Prefetch (body, L, ilat, ilng);
}

size_t TileStream::Prefetching () const
{
	std::lock_guard<std::mutex> lk (d->mx);
	size_t n = 0;
	for (auto &p : d->jobs) n += p.second.pre;
	return n;
}

int TileStream::Impl::Prefetch (const std::string &name, int L, int ilat, int ilng)
{
	int b = FindBody (name);
	if (b < 0) return -1;
	Body &B = *bodies[b];
	if (!net || L < 4 || L > B.cfg.surf.lmax || ilat < 0 || ilng < 0 || ilat >= 1 << (L - 4) || ilng >= 1 << (L - 3)) return -1;
	uint64_t k = Key (L, ilat, ilng), id = Id (b, k);
	auto count = [this] { int n = 0; for (auto &p : jobs) n += p.second.pre; return n; };
	std::unique_lock<std::mutex> lk (mx);
	if (stop || Ocean (B, k)) return -1;
	auto nn = B.none.find (k);
	if (nn != B.none.end() && Today() - nn->second < 30) return -1;
	auto h = hold.find (id);
	if (h != hold.end() && opt.now() < h->second) return -1;
	auto j = jobs.find (id);
	if (j != jobs.end()) {
		if (j->second.pre) { j->second.want = opt.now(); j->second.seq = ++preSeq; } // still on the track: kept, in this call's order
		return 1;
	}
	if (count() >= 64 || jobs.size() >= 200) return 0; // room left for the camera's wants
	lk.unlock();
	std::error_code ec;
	bool have = fs::exists (Tile (B.dir, "Surf", k), ec);
	lk.lock();
	if (stop) return -1;
	if (have || jobs.count (id)) return 1;
	Job &J = NewJob (b, k, opt.now());
	J.pre = true;
	J.seq = ++preSeq;
	kick = true;
	cv.notify_all();
	return 1;
}

// a GET on the net thread, waited for here up to sec (0: until stop)
QByteArray TileStream::Impl::Fetch (const std::string &url, int sec)
{
	if (!net) return QByteArray();
	struct W { std::mutex m; std::condition_variable c; bool done = false; int st = 0; QByteArray body; };
	auto w = std::make_shared<W>();
	net->Get (url, [w] (int st, QByteArray body, int) {
		std::lock_guard<std::mutex> l (w->m);
		w->st = st;
		w->body = std::move (body);
		w->done = true;
		w->c.notify_all();
	});
	Clock::time_point until = Clock::now() + std::chrono::seconds (sec);
	std::unique_lock<std::mutex> l (w->m);
	while (!w->done && !quit && (!sec || Clock::now() < until)) w->c.wait_for (l, std::chrono::milliseconds (200));
	return w->done && w->st == 200 ? w->body : QByteArray();
}

// a cloud source image: kept in TileStream/src, fetched once
QByteArray TileStream::Impl::Source (Body &B, const std::string &url, const char *file, int sec)
{
	std::string p = B.dir + "/TileStream/src/" + file;
	QFile f (QString::fromStdString (p));
	if (f.open (QIODevice::ReadOnly)) return f.readAll();
	QByteArray d = Fetch (url, sec);
	if (!d.isEmpty()) {
		std::error_code ec;
		fs::create_directories (B.dir + "/TileStream/src", ec);
		Write (B, p, d.toStdString());
	}
	return d;
}

// Cloud L1-L5 now from the small image when L1-L4 are missing; the large image's levels on a worker
void TileStream::Impl::Clouds (Body &B)
{
	if (B.cfg.cloudSmall.empty()) return;
	{
		std::ifstream f (Idx (B, "cloud"));
		int L, a, c;
		while (f >> L >> a >> c) B.cloud.insert (Key (L, a, c));
	}
	auto missing = [&B] (int l0, int l1) {
		for (int L = l0; L <= l1; L++)
			for (int a = 0; a < (L < 4 ? 1 : 1 << (L - 4)); a++)
				for (int c = 0; c < (L < 4 ? 1 : 1 << (L - 3)); c++) {
					std::error_code ec;
					if (!fs::exists (Tile (B.dir, "Cloud", Key (L, a, c)), ec)) return true;
				}
		return false;
	};
	if (missing (1, 4)) {
		QImage im;
		if (im.loadFromData (Source (B, B.cfg.cloudSmall, "cloud_small.jpg", opt.cloudWait)) && !im.isNull()) BakeClouds (B, im, 1, 5);
		else Log ("TileStream: no cloud image for " + B.cfg.name);
	}
	if (B.cfg.cloudMax < 6 || !missing (6, B.cfg.cloudMax)) return;
	Body *pb = &B;
	work.push_back ([this, pb] {
		QImage im;
		if (!im.loadFromData (Source (*pb, pb->cfg.cloudBig, "cloud_large.tif", 0)) || im.isNull()) {
			Log ("TileStream: the large cloud image did not load, upper cloud levels from the small one");
			im.loadFromData (Source (*pb, pb->cfg.cloudSmall, "cloud_small.jpg", 30));
		}
		if (!im.isNull() && !quit) BakeClouds (*pb, im, 6, pb->cfg.cloudMax);
	});
}

// every tile of levels l0..l1 that isn't someone else's file
void TileStream::Impl::BakeClouds (Body &B, const QImage &src, int l0, int l1)
{
	QImage g = src.convertToFormat (QImage::Format_Grayscale8);
	int n = 0;
	for (int L = l0; L <= l1 && !quit; L++)
		for (int a = 0; a < (L < 4 ? 1 : 1 << (L - 4)); a++)
			for (int c = 0; c < (L < 4 ? 1 : 1 << (L - 3)) && !quit; c++) {
				uint64_t k = Key (L, a, c);
				std::string p = Tile (B.dir, "Cloud", k);
				bool ours;
				{
					std::lock_guard<std::mutex> lk (mx);
					ours = B.cloud.count (k) > 0;
				}
				std::error_code ec;
				if (fs::exists (p, ec) && !ours) continue;
				if (!Write (B, p, TsSky::Dds5 (TsSky::Cloud (g, L, a, c)))) continue;
				n++;
				if (ours) continue;
				std::lock_guard<std::mutex> lk (mx);
				B.cloud.insert (k);
				Append (B, "cloud", k, -1);
			}
	Log ("TileStream: " + std::to_string (n) + " cloud tiles L" + std::to_string (l0) + "-L" + std::to_string (l1) + " for " + B.cfg.name);
}

namespace {
const int ModLevel = 11;    // Elev_mod from this level up; below, a base is under a node or two
const double Blend = 150.0; // metres from flat to the terrain
std::string ReadAll (const std::string &p)
{
	std::ifstream f (p, std::ios::binary);
	return std::string (std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>());
}
}

// the body's bases, their known heights, and Elev_mod for Elev tiles already on disk (before the client loads them)
void TileStream::Impl::LoadBases (Body &B)
{
	if (opt.radius) B.radius = opt.radius (B.cfg.name);
	if (B.cfg.elev.kind.empty() || !opt.baseDir) return;
	std::string dir = opt.baseDir (B.cfg.name);
	std::error_code ec;
	std::vector<fs::path> files;
	for (fs::directory_iterator it (dir, ec), end; !ec && it != end; it.increment (ec))
		if (it->path().extension() == ".cfg") files.push_back (it->path());
	std::sort (files.begin(), files.end());
	for (const fs::path &p : files) {
		TsBase b;
		if (!TsReadBase (p.string(), b)) continue;
		b.name = p.stem().string();
		B.bases.push_back (b);
		B.baseH.push_back (NAN);
	}
	{
		std::ifstream f (Idx (B, "bases"));
		std::string n;
		double h;
		while (f >> n >> h)
			for (size_t i = 0; i < B.bases.size(); i++)
				if (B.bases[i].name == n) B.baseH[i] = h;
		std::ifstream m (Idx (B, "mod"));
		int L, a, c;
		while (m >> L >> a >> c) B.mod.insert (Key (L, a, c));
	}
	int made = 0;
	for (size_t i = 0; i < B.bases.size(); i++) {
		const TsBase &b = B.bases[i];
		double lng = b.lng * 180.0 / M_PI, lat = b.lat * 180.0 / M_PI;
		for (int L = B.cfg.elev.lmax; std::isnan (B.baseH[i]) && L >= 14; L--) { // a fine Elev tile at the base point
			double span = 180.0 / (1 << (L - 4));
			uint64_t k = Key (L, std::clamp ((int)((90.0 - lat) / span), 0, (1 << (L - 4)) - 1), std::clamp ((int)((lng + 180.0) / span), 0, (1 << (L - 3)) - 1));
			double h = TsSky::ElvAt (ReadAll (Tile (B.dir, "Elev", k)), b.lng, b.lat);
			if (!std::isnan (h)) {
				B.baseH[i] = std::max (0.0, h);
				std::ofstream (Idx (B, "bases"), std::ios::app) << b.name << ' ' << B.baseH[i] << '\n';
			}
		}
		if (std::isnan (B.baseH[i])) continue;
		for (int L = ModLevel; L <= B.cfg.elev.lmax; L++) {
			double span = 180.0 / (1 << (L - 4)), r = (b.ext + Blend) / B.radius * 180.0 / M_PI, rl = r / std::max (0.05, cos (b.lat));
			int a0 = std::max (0, (int)((90.0 - lat - r) / span)), a1 = std::min ((1 << (L - 4)) - 1, (int)((90.0 - lat + r) / span));
			int nl = 1 << (L - 3), c0 = (int)std::floor ((lng + 180.0 - rl) / span), c1 = std::min (c0 + nl - 1, (int)std::floor ((lng + 180.0 + rl) / span)); // fix-decoders: across the date line
			for (int a = a0; a <= a1; a++)
				for (int c = c0; c <= c1; c++) {
					uint64_t k = Key (L, a, (c % nl + nl) % nl); // fix-decoders: wrapped column
					if (!fs::exists (Tile (B.dir, "Elev", k), ec) || fs::exists (Tile (B.dir, "Elev_mod", k), ec)) continue;
					made += Mods (B, k, ReadAll (Tile (B.dir, "Elev", k))) && fs::exists (Tile (B.dir, "Elev_mod", k), ec) ? 1 : 0;
				}
		}
	}
	Log ("TileStream: " + std::to_string (B.bases.size()) + " bases for " + B.cfg.name + ", " + std::to_string (made) + " Elev_mod tiles made for Elev on disk");
}

// with mx held: base centre tiles appended to an Elev job's sources, for the bases it touches whose height isn't known
void TileStream::Impl::BaseSrcs (Body &B, Job &J)
{
	J.baseIdx.clear();
	int L = KL (J.key), z = std::min (13, B.cfg.elev.nmax);
	if (L < ModLevel) return;
	for (size_t i = 0; i < B.bases.size(); i++) {
		if (!std::isnan (B.baseH[i]) || !TsSky::Hits (B.bases[i], B.radius, L, KLat (J.key), KLng (J.key), Blend)) continue;
		int row, col;
		TsSky::TerrTile (B.bases[i].lng * 180.0 / M_PI, B.bases[i].lat * 180.0 / M_PI, z, row, col);
		Src s;
		s.url = TsImg::Url (B.cfg.elev.url, z, row, col);
		s.host = TERR;
		J.elev.push_back (s);
		J.baseIdx.push_back ((int)i);
	}
}

// the heights from those tiles, kept in bases.idx
void TileStream::Impl::BaseHeights (Body &B, const Job &J, const std::vector<QImage> &et)
{
	size_t n0 = et.size() - std::min (et.size(), J.baseIdx.size());
	int z = std::min (13, B.cfg.elev.nmax);
	for (size_t j = 0; j < J.baseIdx.size() && n0 + j < et.size(); j++) {
		const TsBase &b = B.bases[(size_t)J.baseIdx[j]];
		double lng = b.lng * 180.0 / M_PI, lat = b.lat * 180.0 / M_PI;
		int row, col;
		TsSky::TerrTile (lng, lat, z, row, col);
		double h = TsSky::TerrAt (et[n0 + j], z, row, col, lng, lat);
		if (std::isnan (h)) continue;
		h = std::round (std::max (0.0, h) / B.cfg.elevScale) * B.cfg.elevScale;
		std::lock_guard<std::mutex> lk (mx);
		double &H = B.baseH[(size_t)J.baseIdx[j]];
		if (!std::isnan (H)) continue;
		H = h;
		std::ofstream (Idx (B, "bases"), std::ios::app) << b.name << ' ' << H << '\n';
		Log ("TileStream: base " + b.name + " flat at " + std::to_string (H) + " m");
	}
}

// the Elev_mod of tile k over elv, written before elv; false while a base it touches has no height yet
bool TileStream::Impl::Mods (Body &B, uint64_t k, const std::string &elv)
{
	int L = KL (k);
	if (B.bases.empty() || L < ModLevel) return true;
	std::vector<std::pair<const TsBase *, double>> hit;
	bool ours;
	{
		std::lock_guard<std::mutex> lk (mx);
		for (size_t i = 0; i < B.bases.size(); i++) {
			if (!TsSky::Hits (B.bases[i], B.radius, L, KLat (k), KLng (k), Blend)) continue;
			if (std::isnan (B.baseH[i])) return false;
			hit.push_back ({&B.bases[i], B.baseH[i]});
		}
		ours = B.mod.count (k) > 0;
	}
	if (hit.empty()) return true;
	std::string path = Tile (B.dir, "Elev_mod", k);
	std::error_code ec;
	if (!ours && fs::exists (path, ec)) return true; // someone else's Elev_mod: left alone
	std::string m = TsSky::ElevMod (L, KLat (k), KLng (k), hit, B.radius, B.cfg.elevScale, elv, Blend);
	if (m.empty()) return true;
	if (!Write (B, path, m)) return false;
	if (ours) return true;
	std::lock_guard<std::mutex> lk (mx);
	if (B.mod.insert (k).second) Append (B, "mod", k, -1);
	return true;
}

// not upstream: Launchpad cache controls and region prefetch (TabStream.cpp)

#include <cmath>

namespace {

std::vector<uint64_t> ReadIdx (const std::string &file)
{
	std::vector<uint64_t> v;
	std::ifstream f (file);
	std::string line;
	while (std::getline (f, line)) {
		std::istringstream is (line);
		int L, a, b;
		if (!(is >> L >> a >> b) || L < 1 || L > 21 || a < 0 || b < 0) continue;
		v.push_back (Key (L, a, b));
	}
	std::sort (v.begin(), v.end());
	v.erase (std::unique (v.begin(), v.end()), v.end());
	return v;
}

// to <file>.new, then renamed over it
bool WriteIdx (const std::string &file, const std::vector<uint64_t> &v)
{
	std::string tmp = file + ".new";
	std::error_code ec;
	{
		std::ofstream f (tmp, std::ios::trunc);
		for (uint64_t k : v) f << KL (k) << ' ' << KLat (k) << ' ' << KLng (k) << '\n';
		f.close();
		if (f.fail()) {
			fs::remove (tmp, ec);
			return false;
		}
	}
	fs::rename (tmp, file, ec);
	return !ec;
}

std::string CleanDir (std::string dir)
{
	std::replace (dir.begin(), dir.end(), '\\', '/');
	while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
	return dir;
}

uint64_t TileBytes (const std::string &dir, uint64_t k)
{
	uint64_t n = 0;
	for (const char *layer : {"Surf", "Mask", "Elev", "Class"}) {
		std::error_code ec;
		uint64_t s = fs::file_size (Tile (dir, layer, k), ec);
		if (!ec) n += s;
	}
	return n;
}

}

uint64_t TileStream::CacheBytes (const std::string &bodyDir, uint64_t *total, size_t *tiles)
{
	std::string dir = CleanDir (bodyDir);
	uint64_t n = 0;
	size_t t = 0;
	for (uint64_t k : ReadIdx (dir + "/TileStream/written.idx")) {
		uint64_t b = TileBytes (dir, k);
		if (b) { n += b; t++; }
	}
	for (const char *idx : {"bldg", "mod", "cloud"})
		for (uint64_t k : ReadIdx (dir + "/TileStream/" + idx + ".idx")) {
			std::error_code ec;
			uint64_t s = fs::file_size (StreamFile (dir, idx, k), ec);
			if (!ec) { n += s; t++; }
		}
	if (tiles) *tiles = t;
	if (total) {
		*total = 0;
		for (const char *layer : {"/Surf", "/Mask", "/Elev", "/Class", "/Bldg", "/Cloud", "/Elev_mod"}) {
			std::error_code ec;
			for (fs::recursive_directory_iterator it (dir + layer, ec), end; !ec && it != end; it.increment (ec)) {
				std::error_code fe;
				if (!it->is_regular_file (fe)) continue;
				uint64_t s = it->file_size (fe);
				if (!fe) *total += s;
			}
		}
	}
	return n;
}

// files first, then the indexes: a failed rewrite only lists files that are gone
size_t TileStream::Clear (const std::string &bodyDir, bool keepBase)
{
	std::string dir = CleanDir (bodyDir), ts = dir + "/TileStream/";
	std::vector<uint64_t> kept;
	size_t n = 0;
	std::error_code ec;
	bool had = fs::exists (ts + "written.idx", ec);
	for (uint64_t k : ReadIdx (ts + "written.idx")) {
		if (keepBase && KL (k) <= 8) {
			kept.push_back (k);
			continue;
		}
		bool any = false;
		for (const char *layer : {"Surf", "Mask", "Elev", "Class"}) {
			std::string p = Tile (dir, layer, k);
			if (fs::remove (p, ec)) any = true;
			fs::remove (fs::path (p).parent_path(), ec); // its row folder, when empty
		}
		if (any) n++;
	}
	if (had) WriteIdx (ts + "written.idx", kept);
	for (const char *idx : {"bldg", "mod", "cloud"}) { // buildings, Elev_mod and clouds this stream wrote
		if (!fs::exists (ts + idx + ".idx", ec)) continue;
		std::vector<uint64_t> keep;
		for (uint64_t k : ReadIdx (ts + idx + ".idx")) {
			if (keepBase && KL (k) <= 8 && std::string (idx) != "bldg") { keep.push_back (k); continue; }
			std::string p = StreamFile (dir, idx, k);
			if (fs::remove (p, ec)) n++;
			fs::remove (fs::path (p).parent_path(), ec);
		}
		WriteIdx (ts + idx + ".idx", keep);
	}
	if (fs::exists (ts + "refetch.idx", ec)) {
		std::vector<uint64_t> r;
		for (uint64_t k : ReadIdx (ts + "refetch.idx"))
			if (std::binary_search (kept.begin(), kept.end(), k)) r.push_back (k);
		WriteIdx (ts + "refetch.idx", r);
	}
	fs::remove (ts + "none.idx", ec);
	if (!keepBase) fs::remove (ts + "ocean.idx", ec);
	for (fs::directory_iterator it (ts + "tmp", ec), end; !ec && it != end; it.increment (ec)) {
		std::error_code rc;
		fs::remove (it->path(), rc);
	}
	return n;
}

std::vector<TileStream::TileKey> TileStream::RegionKeys (double lat, double lng, double radiusKm, int maxL)
{
	const double D = 3.14159265358979323846 / 180.0, R = 6371.0;
	const size_t maxKeys = 100000; // a region past this is cut off: the caller says so
	std::vector<TileKey> v;
	if (!(radiusKm >= 0.0) || !(lat >= -90.0 && lat <= 90.0) || !std::isfinite (lng)) return v;
	maxL = std::min (maxL, 21);
	lng = std::remainder (lng, 360.0);
	double ang = radiusKm / R;
	auto dist = [&] (double la, double ln) {
		double s1 = std::sin ((la - lat) * D / 2.0), s2 = std::sin ((ln - lng) * D / 2.0);
		return 2.0 * std::asin (std::min (1.0, std::sqrt (s1 * s1 + std::cos (lat * D) * std::cos (la * D) * s2 * s2)));
	};
	double lat0 = std::max (-90.0, lat - ang / D), lat1 = std::min (90.0, lat + ang / D);
	bool allLng = lat0 <= -90.0 || lat1 >= 90.0;
	double dlng = 180.0;
	if (!allLng) {
		double s = std::sin (ang) / std::cos (lat * D);
		if (s >= 1.0) allLng = true;
		else dlng = std::asin (s) / D;
	}
	for (int L = 1; L <= maxL; L++) {
		if (L < 4) {
			v.push_back ({L, 0, 0});
			continue;
		}
		int nlat = 1 << (L - 4), nlng = 1 << (L - 3);
		double sz = 180.0 / nlat;
		int r0 = std::clamp ((int)std::floor ((90.0 - lat1) / sz), 0, nlat - 1);
		int r1 = std::clamp ((int)std::floor ((90.0 - lat0) / sz), 0, nlat - 1);
		int c0 = 0, c1 = nlng - 1;
		if (!allLng) {
			c0 = (int)std::floor ((lng - dlng + 180.0) / sz);
			c1 = (int)std::floor ((lng + dlng + 180.0) / sz);
			if (c1 - c0 + 1 >= nlng) c0 = 0, c1 = nlng - 1;
		}
		for (int r = r0; r <= r1; r++) {
			double n = 90.0 - r * sz, s = n - sz;
			double pla = std::clamp (lat, s, n);
			for (int c = c0; c <= c1; c++) {
				int cc = ((c % nlng) + nlng) % nlng;
				double w = -180.0 + cc * sz, e = w + sz;
				double dw = std::remainder (lng - w, 360.0), de = std::remainder (lng - e, 360.0);
				double pln = (dw >= 0.0 && de <= 0.0) ? lng : (std::fabs (dw) < std::fabs (de) ? w : e);
				if (dist (pla, pln) > ang + 1e-12) continue;
				v.push_back ({L, r, cc});
				if (v.size() >= maxKeys) return v;
			}
		}
	}
	return v;
}

// a level starts once every coarser key is answered: an ocean or missing parent rules out its children
int TileStream::Prefetch (Options opt, const std::string &body, const std::vector<TileKey> &keys, Progress &p, const std::atomic<bool> &cancel, double stallSec)
{
	opt.offline = false;
	opt.staleSec = 1e9; // a prefetch want never goes stale
	opt.sky = false;    // clouds and Elev_mod are the session's
	if (std::find (opt.bodies.begin(), opt.bodies.end(), body) == opt.bodies.end()) opt.bodies.push_back (body);
	std::string dir = opt.bodyDir ? CleanDir (opt.bodyDir (body)) : std::string();
	p.total = (int)keys.size();
	TileStream ts (opt);
	if (!ts.Enabled (body)) return 3;
	std::vector<size_t> open;
	size_t next = 0, pending = 0;
	Clock::time_point last = Clock::now();
	auto answer = [&] (size_t i, int r) {
		const TileKey &k = keys[i];
		if (r > 0) {
			p.have++;
			p.bytes += TileBytes (dir, Key (k.L, k.ilat, k.ilng));
		} else p.none++;
		p.done++;
		last = Clock::now();
	};
	for (;;) {
		if (cancel) return 1;
		for (size_t j = 0; j < open.size(); ) {
			const TileKey &k = keys[open[j]];
			int r = ts.Want (body, k.L, k.ilat, k.ilng, 0);
			if (r) {
				answer (open[j], r);
				open[j] = open.back();
				open.pop_back();
			} else j++;
		}
		int lmin = 1 << 30;
		for (size_t i : open) lmin = std::min (lmin, keys[i].L);
		while (next < keys.size() && open.size() < 48 && ts.Pending() < 200 && keys[next].L <= lmin) {
			const TileKey &k = keys[next];
			int r = ts.Want (body, k.L, k.ilat, k.ilng, 0);
			if (r) answer (next, r);
			else {
				open.push_back (next);
				lmin = std::min (lmin, k.L);
			}
			next++;
		}
		size_t pn = ts.Pending();
		if (pn < pending && next == keys.size() && open.empty()) last = Clock::now(); // the Elev jobs moving
		pending = pn;
		bool stalled = std::chrono::duration<double> (Clock::now() - last).count() > stallSec;
		if (next == keys.size() && open.empty() && (!pn || stalled)) return 0; // an Elev that never comes doesn't fail the tiles
		if (stalled) return 2;
		std::this_thread::sleep_for (std::chrono::milliseconds (100));
	}
}
