// not upstream: the tile stream service: queue, fetch, build, write and size cap (TileStream.h)

#include "TileStream.h"
#include "TileStreamNet.h"
#include <QColor>
#include <QFile>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

namespace fs = std::filesystem;
using Clock = TileStream::Clock;

namespace {

enum { EOX, GIBS, NHOST };

uint64_t Key (int L, int ilat, int ilng) { return (uint64_t)L << 48 | (uint64_t)ilat << 24 | (uint64_t)ilng; }
int KL (uint64_t k) { return (int)(k >> 48 & 0xFF); }
int KLat (uint64_t k) { return (int)(k >> 24 & 0xFFFFFF); }
int KLng (uint64_t k) { return (int)(k & 0xFFFFFF); }
uint64_t Id (int body, uint64_t k) { return (uint64_t)body << 56 | k; }

std::string Tile (const std::string &dir, const char *layer, uint64_t k)
{
	char b[48];
	snprintf (b, sizeof b, "/%s/%02d/%06d/%06d.dds", layer, KL (k), KLat (k), KLng (k));
	return dir + b;
}

int64_t Today ()
{
	return std::chrono::duration_cast<std::chrono::hours> (std::chrono::system_clock::now().time_since_epoch()).count() / 24;
}

int HostOf (const TsSource &s) { return s.kind == "eox" ? EOX : GIBS; }

}

struct TileStream::Impl {
	struct Body {
		TsBodyCfg cfg;
		std::string dir;
		std::unordered_set<uint64_t> written, ocean, refetch;
		std::unordered_map<uint64_t, int64_t> none; // key -> day
		std::deque<uint64_t> done;
		std::vector<uint64_t> hits;
	};
	struct Src {
		std::string url;
		int host = EOX;
		int status = -1;
		std::shared_ptr<const QByteArray> data;
	};
	struct Job {
		uint64_t id = 0, key = 0;
		int body = 0, stage = 0, tries = 0, waiting = 0; // stage 0 queued, 1 fetching, 2 building
		bool required = false, low = false, fallback = false;
		Clock::time_point want, retry;
		std::vector<Src> surf, lights, water;
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
	void Fetched (Job &J, Clock::time_point now);
	void Retry (Job &J, Clock::time_point now, int sec);
	bool Paused (int h, Clock::time_point now) const { return now < host[h].paused; }
	bool Startable (Job &J, Clock::time_point now);
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
	bool Write (const std::string &path, const std::string &bytes);
	std::string Idx (const Body &B, const char *name) const { return B.dir + "/TileStream/" + name + ".idx"; }
	void LoadIdx (Body &B);
	void Append (const Body &B, const char *name, uint64_t k, int64_t t);
	void Rewrite (const Body &B, const char *name, const std::unordered_set<uint64_t> &s);
	void MarkNone (Body &B, uint64_t k);
	void CleanTmp ();
	void Evict ();
};

static bool Better (const TileStream::Impl::Job &a, const TileStream::Impl::Job &b)
{
	if (a.low != b.low) return !a.low;
	if (a.required != b.required) return a.required;
	if (KL (a.key) != KL (b.key)) return KL (a.key) < KL (b.key);
	return a.want > b.want;
}

TileStream::Impl::Impl (const Options &o)
: opt(o)
{
	if (!opt.now) opt.now = [] { return Clock::now(); };
	host[EOX].cap = 6;
	host[GIBS].cap = 4;
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
		for (const char *s : {"/Surf", "/Mask", "/TileStream"})
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
			for (uint64_t k : B.refetch) NewJob (b, k, now).low = true;
		}
	}
	for (int i = 0; i < 2; i++) workers.emplace_back ([this] { Work (); });
	ctl = std::thread ([this] { Run (); });
}

TileStream::Impl::~Impl ()
{
	{
		std::lock_guard<std::mutex> lk (mx);
		stop = true;
	}
	cv.notify_all();
	wcv.notify_all();
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
		if (B.written.count (k)) B.hits.push_back (k);
		return 1;
	}
	if (flags & 1) {
		if (!B.written.erase (k)) return -1; // not ours: never replaced
		lk.unlock();
		std::error_code ec;
		fs::remove (Tile (B.dir, "Surf", k), ec);
		fs::remove (Tile (B.dir, "Mask", k), ec);
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
		if (now < h->second) return -1;
		hold.erase (h);
	}
	auto j = jobs.find (id);
	if (j != jobs.end()) {
		j->second.want = now;
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
			return 0;
		}
	}
	NewJob (b, k, now);
	Trim ();
	kick = true;
	cv.notify_all();
	return 0;
}

void TileStream::Impl::Trim ()
{
	while (jobs.size() > 256) {
		auto worst = jobs.end();
		for (auto it = jobs.begin(); it != jobs.end(); ++it) {
			const Job &J = it->second;
			if (J.stage || J.required || J.low) continue;
			if (worst == jobs.end() || J.want < worst->second.want) worst = it;
		}
		if (worst == jobs.end()) return;
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
	if (r.status == 200 || r.status == 404 || r.status == 400) {
		h.fails = 0;
		h.backoff = 60.0;
	} else if (r.status == 429 || r.status == 503) {
		h.paused = now + std::chrono::seconds (r.retryAfter > 0 ? std::min (r.retryAfter, 3600) : 30);
	} else if (++h.fails >= 5) {
		h.paused = now + std::chrono::milliseconds ((int64_t)(h.backoff * 1000.0));
		Log (std::string ("TileStream: ") + (r.host == EOX ? "EOX" : "GIBS") + " is not answering, paused for " + std::to_string ((int)h.backoff) + " s");
		h.backoff = std::min (h.backoff * 2.0, 600.0);
		h.fails = 0;
	}
	std::shared_ptr<const QByteArray> data;
	if (r.status == 200) {
		data = std::make_shared<const QByteArray> (std::move (r.body));
		CachePut (r.url, data);
	}
	auto it = inflight.find (r.url);
	if (it == inflight.end()) return;
	std::vector<uint64_t> ids = std::move (it->second);
	inflight.erase (it);
	for (uint64_t id : ids) {
		auto j = jobs.find (id);
		if (j == jobs.end() || j->second.stage != 1) continue;
		Job &J = j->second;
		for (std::vector<Src> *v : {&J.surf, &J.lights, &J.water})
			for (Src &s : *v)
				if (s.status < 0 && s.url == r.url) {
					s.status = r.status;
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
}

// all sources answered: build, fall back, retry or give up
void TileStream::Impl::Fetched (Job &J, Clock::time_point now)
{
	bool miss[3] = {false, false, false}, fail = false;
	int i = 0;
	for (std::vector<Src> *v : {&J.surf, &J.lights, &J.water}) {
		for (const Src &s : *v) {
			if (s.status == 404 || s.status == 400) miss[i] = true;
			else if (s.status != 200) fail = true;
		}
		i++;
	}
	Body &B = *bodies[J.body];
	int L = KL (J.key);
	if (fail) {
		if (++J.tries > 5 && !J.required) jobs.erase (J.id);
		else Retry (J, now, 10);
		return;
	}
	const TsSource &fb = B.cfg.fallback;
	if (miss[0] && !J.fallback && !fb.kind.empty() && L >= fb.lmin && L <= fb.lmax) {
		J.fallback = true;
		Retry (J, now, 0);
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

bool TileStream::Impl::Startable (Job &J, Clock::time_point now)
{
	const Body &B = *bodies[J.body];
	if (Paused (GIBS, now)) return false; // the mask needs GIBS
	if (J.fallback || HostOf (B.cfg.surf) != EOX || !Paused (EOX, now)) return true;
	const TsSource &fb = B.cfg.fallback;
	int L = KL (J.key);
	if (fb.kind.empty() || L < fb.lmin || L > fb.lmax) return false;
	J.fallback = true;
	return true;
}

void TileStream::Impl::StartJobs (Clock::time_point now)
{
	int active = 0;
	for (auto &p : jobs) if (p.second.stage) active++;
	while (active < opt.maxActive) {
		Job *best = nullptr;
		for (auto &p : jobs) {
			Job &J = p.second;
			if (J.stage || J.retry > now || !Startable (J, now)) continue;
			if (!best || Better (J, *best)) best = &J;
		}
		if (!best) return;
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
	J.surf = Srcs (J.fallback ? B.cfg.fallback : B.cfg.surf, L, la, ln);
	J.lights = Srcs (B.cfg.lights, L, la, ln);
	J.water = Srcs (B.cfg.water, L, la, ln);
	J.stage = 1;
	J.waiting = 0;
	for (std::vector<Src> *v : {&J.surf, &J.lights, &J.water})
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
			net->Get (url, [this, url, h] (int st, QByteArray body, int ra) {
				std::lock_guard<std::mutex> lk (mx);
				results.push_back ({url, h, st, ra, std::move (body)});
				kick = true;
				cv.notify_all();
			});
		}
	}
}

void TileStream::Impl::DropStale (Clock::time_point now)
{
	std::chrono::duration<double> stale (opt.staleSec);
	for (auto it = jobs.begin(); it != jobs.end(); ) {
		const Job &J = it->second;
		if (!J.stage && !J.required && !J.low && now - J.want > stale) it = jobs.erase (it);
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
		QImage mask = TsImg::Mask (lights, water, C.black, C.waterGrey);
		ok = Write (Tile (B.dir, "Mask", J.key), TsImg::Dds (mask, true)) && Write (Tile (B.dir, "Surf", J.key), TsImg::Dds (surf, false));
		if (ok && L >= 4) Remember (Id (J.body, J.key), surf);
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
	kick = true;
	cv.notify_all();
}

// a source didn't decode: drop it from memory and fetch again, a few times
void TileStream::Impl::Failed (const Job &J)
{
	std::lock_guard<std::mutex> lk (mx);
	for (const std::vector<Src> *v : {&J.surf, &J.lights, &J.water})
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

// to a unique .tmp, then renamed: the client never reads half a file
bool TileStream::Impl::Write (const std::string &path, const std::string &bytes)
{
	std::error_code ec;
	fs::create_directories (fs::path (path).parent_path(), ec);
	int seq;
	{
		std::lock_guard<std::mutex> lk (imx);
		seq = ++tmpSeq;
	}
	std::string tmp = path + "." + std::to_string (getpid()) + "." + std::to_string (seq) + ".tmp";
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
}

void TileStream::Impl::Append (const Body &B, const char *name, uint64_t k, int64_t t)
{
	std::ofstream f (Idx (B, name), std::ios::app);
	f << KL (k) << ' ' << KLat (k) << ' ' << KLng (k);
	if (t >= 0) f << ' ' << t;
	f << '\n';
}

void TileStream::Impl::Rewrite (const Body &B, const char *name, const std::unordered_set<uint64_t> &s)
{
	std::string p = Idx (B, name), tmp = p + ".new";
	{
		std::ofstream f (tmp, std::ios::trunc);
		for (uint64_t k : s) f << KL (k) << ' ' << KLat (k) << ' ' << KLng (k) << '\n';
	}
	std::error_code ec;
	fs::rename (tmp, p, ec);
}

void TileStream::Impl::MarkNone (Body &B, uint64_t k)
{
	int64_t t = Today();
	B.none[k] = t;
	Append (B, "none", k, t);
}

// .tmp left by an earlier run that was killed mid-write
void TileStream::Impl::CleanTmp ()
{
	std::string mine = "." + std::to_string (getpid()) + ".";
	for (auto &b : bodies)
		for (const char *layer : {"/Surf", "/Mask"}) {
			std::error_code ec;
			for (fs::recursive_directory_iterator it (b->dir + layer, ec), end; !ec && it != end; it.increment (ec)) {
				std::string n = it->path().filename().string();
				if (n.size() > 8 && !n.compare (n.size() - 4, 4, ".tmp") && n.find (".dds.") != std::string::npos && n.find (mine) == std::string::npos) {
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
	for (auto &bp : bodies) {
		Body &B = *bp;
		std::vector<uint64_t> keys;
		{
			std::lock_guard<std::mutex> lk (mx);
			keys.assign (B.written.begin(), B.written.end());
		}
		struct E { fs::file_time_type t; uint64_t k, size; };
		std::vector<E> v;
		std::vector<uint64_t> gone;
		uint64_t total = 0;
		for (uint64_t k : keys) {
			std::error_code e1, e2;
			std::string s = Tile (B.dir, "Surf", k), m = Tile (B.dir, "Mask", k);
			uint64_t ss = fs::file_size (s, e1);
			if (e1) { gone.push_back (k); continue; }
			uint64_t ms = fs::file_size (m, e2);
			uint64_t size = ss + (e2 ? 0 : ms);
			total += size;
			if (KL (k) > 8) v.push_back ({fs::last_write_time (s, e1), k, size});
		}
		if (cap && total > cap) {
			std::sort (v.begin(), v.end(), [] (const E &a, const E &b) { return a.t < b.t; });
			for (const E &e : v) {
				if (total <= cap / 10 * 9) break;
				std::error_code ec;
				fs::remove (Tile (B.dir, "Surf", e.k), ec);
				fs::remove (Tile (B.dir, "Mask", e.k), ec);
				total -= e.size;
				gone.push_back (e.k);
			}
		}
		if (gone.empty()) continue;
		std::lock_guard<std::mutex> lk (mx);
		for (uint64_t k : gone) B.written.erase (k);
		Rewrite (B, "written", B.written);
	}
}

// -----------------------------------------------------------------------

TileStream::TileStream (const Options &opt)
: d(std::make_unique<Impl> (opt))
{
}

TileStream::~TileStream () = default;

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
