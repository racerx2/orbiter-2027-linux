// not upstream: the tile stream service: queue, fetch, build, write and size cap (TileStream.h)

#include "TileStream.h"
#include "TileStreamNet.h"
#include "ZTreeMgr.h"
#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QImageReader>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <iterator>
#include <deque>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace fs = std::filesystem;

static std::atomic<unsigned> tsElevGen {0}; // not upstream (elev-consistency): TsElevGen
unsigned TsElevGen () { return tsElevGen.load (std::memory_order_acquire); }
using Clock = TileStream::Clock;

int (*TsFlock) (int, int) = flock;

namespace {

enum { EOX, GIBS, TERR, WC, BLDG, USGS, NHOST }; // USGS: not upstream (mars-stream), USGS Astrogeology's COGs
const char *HostName[NHOST] = {"EOX", "GIBS", "Terrain Tiles", "WorldCover", "OpenFreeMap", "USGS Astrogeology"};
enum { A_SURF, A_MASK, A_ELEV, A_CLOUD, NARCH };
const int HoldSec = 600; // a tile given up on waits this long

// a line of an index: three integers, a fourth where extra (written, none); anything else is no line
bool ParseLine (const std::string &line, bool extra, int &L, int &a, int &b, long long &t)
{
	const char *p = line.c_str();
	long long v[4];
	int n = 0;
	for (;;) {
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (!*p) break;
		if (n == 4) return false;
		char *e;
		errno = 0;
		v[n] = strtoll (p, &e, 10);
		if (e == p || errno || (*e && *e != ' ' && *e != '\t' && *e != '\r')) return false;
		n++;
		p = e;
	}
	if (n != 3 && !(extra && n == 4)) return false;
	if (v[0] < 1 || v[0] > 21 || v[1] < 0 || v[2] < 0 || v[1] > INT_MAX || v[2] > INT_MAX) return false;
	L = (int)v[0];
	a = (int)v[1];
	b = (int)v[2];
	t = n == 4 ? v[3] : 0;
	return true;
}

// inside the Orbiter grid: one tile at L1-L3, then 2^(L-4) by 2^(L-3)
bool InGrid (int L, int a, int b)
{
	if (L < 1 || L > 21 || a < 0 || b < 0) return false;
	return L < 4 ? !a && !b : a < 1 << (L - 4) && b < 1 << (L - 3);
}

// bldg, way: z14 x/y; the rest: the Orbiter grid from lmin to lmax
bool KeyOk (const std::string &idx, int L, int a, int b, int lmin = 1, int lmax = 21)
{
	if (idx == "bldg" || idx == "way") return L == 14 && a >= 0 && b >= 0 && a < 1 << 14 && b < 1 << 14; // not upstream (design-ways): way
	return L >= lmin && L <= lmax && InGrid (L, a, b);
}

std::string CleanDir (std::string dir)
{
	std::replace (dir.begin(), dir.end(), '\\', '/');
	while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
	return dir;
}

// the table of contents of an installed Archive/<Layer>.tree, as ZTreeMgr reads it; no tile data
struct Toc {
	std::vector<TreeNode> node;
	uint32_t root[5] = {(uint32_t)-1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};
	uint32_t Idx (int L, int a, int b) const
	{
		if (L < 1 || node.empty()) return (uint32_t)-1;
		uint32_t i;
		if (L <= 4) {
			if (a || b < 0 || b > (L == 4 ? 1 : 0)) return (uint32_t)-1;
			i = root[L < 4 ? L - 1 : 3 + b];
		} else {
			uint32_t p = Idx (L - 1, a / 2, b / 2);
			if (p == (uint32_t)-1) return p;
			i = node[p].child[((a & 1) << 1) + (b & 1)];
		}
		return i < node.size() ? i : (uint32_t)-1;
	}
	bool Has (int L, int a, int b) const { uint32_t i = Idx (L, a, b); return i != (uint32_t)-1 && node[i].size > 0; }
};

bool ReadToc (const std::string &file, Toc &t)
{
	t = Toc();
	FILE *f = fopen (file.c_str(), "rb");
	if (!f) return false;
	struct { char magic[4]; uint32_t size, flags, dataOfs; int64_t dataLength; uint32_t nodeCount, root[5]; } h;
	static_assert (sizeof h == sizeof (TreeFileHeader), "the .tree header");
	struct stat st;
	const uint32_t maxNodes = 1u << 26; // a sane archive's TOC is far smaller
	bool ok = fread (&h, sizeof h, 1, f) == 1 && !memcmp (h.magic, "TX\1\0", 4) && h.size == sizeof h && !fstat (fileno (f), &st)
		&& h.dataOfs >= h.size && (int64_t)h.dataOfs <= (int64_t)st.st_size && h.nodeCount <= maxNodes
		&& (uint64_t)h.nodeCount <= (uint64_t)(h.dataOfs - h.size) / sizeof (TreeNode); // the TOC lies between the header and the data
	if (ok) {
		try {
			t.node.resize (h.nodeCount);
			ok = fread (t.node.data(), sizeof (TreeNode), h.nodeCount, f) == h.nodeCount;
		} catch (const std::bad_alloc &) {
			ok = false;
		}
		memcpy (t.root, h.root, sizeof t.root);
	}
	fclose (f);
	if (!ok) t = Toc();
	return ok && !t.node.empty();
}

// one flock on <dir>/TileStream/lock per folder and process; Clear holds it alone
struct Lock { int fd = -1, refs = 0; bool sole = false; };
std::mutex lockMx;
std::map<std::string, Lock> locks;

std::string LockKey (const std::string &dir)
{
	std::error_code ec;
	fs::path p = fs::weakly_canonical (dir, ec);
	return ec ? dir : p.string();
}

// flock that retries an interrupted call; errno kept
int TryFlock (int fd)
{
	int r;
	while ((r = TsFlock (fd, LOCK_EX | LOCK_NB)) != 0 && errno == EINTR) {}
	return r;
}

// 1 taken, 0 held by another process (or by this one for sole), -1 the lock file can't be made
// a file system without working locks: taken as the only Orbiter on it, flock's errno in *err
int TakeLock (const std::string &key, bool sole, int *err = nullptr)
{
	std::lock_guard<std::mutex> lk (lockMx);
	auto it = locks.find (key);
	if (it != locks.end()) {
		if (sole || it->second.sole) return 0;
		it->second.refs++;
		return 1;
	}
	Lock &l = locks[key]; // made before the file, so nothing below throws with it open
	int fd = open ((key + "/TileStream/lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (fd < 0) {
		locks.erase (key);
		return -1;
	}
	if (TryFlock (fd)) {
		int e = errno;
		if (e == EWOULDBLOCK) {
			close (fd);
			locks.erase (key);
			return 0;
		}
		if (err) *err = e;
	}
	l.fd = fd;
	l.refs = 1;
	l.sole = sole;
	return 1;
}

void DropLock (const std::string &key)
{
	std::lock_guard<std::mutex> lk (lockMx);
	auto it = locks.find (key);
	if (it == locks.end() || --it->second.refs > 0) return;
	close (it->second.fd);
	locks.erase (it);
}

// a cloud source: the format's reader takes its header (jpg, tif)
bool ImageOk (const QByteArray &d, const char *fmt)
{
	if (d.isEmpty()) return false;
	QBuffer b;
	b.setData (d);
	if (!b.open (QIODevice::ReadOnly)) return false;
	QImageReader r (&b, fmt);
	return r.canRead() && r.size().isValid();
}

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

// not upstream (design-ways): the two files of a z14 vector tile, 0 buildings, 1 ways
const char *const VecIdx[2] = {"bldg", "way"};

std::string VecFile (const std::string &dir, int v, uint64_t k)
{
	return dir + (v ? "/Way/14/" : "/Bldg/14/") + std::to_string (KLat (k)) + "/" + std::to_string (KLng (k)) + (v ? ".way" : ".bld");
}

std::string BldgFile (const std::string &dir, uint64_t k)
{
	return VecFile (dir, 0, k);
}

// the file behind an entry of bldg.idx, way.idx, mod.idx or cloud.idx
std::string StreamFile (const std::string &dir, const std::string &idx, uint64_t k)
{
	if (idx == "way") return VecFile (dir, 1, k); // not upstream (design-ways)
	return idx == "bldg" ? BldgFile (dir, k) : Tile (dir, idx == "mod" ? "Elev_mod" : "Cloud", k);
}

// design dropin-addons: a file the stream wrote has this in its mtime's nanoseconds (a multiple of 100 ns); a copy over it hasn't
const long OwnNs = 271828100;

void MarkFile (const std::string &p)
{
	struct stat st;
	if (stat (p.c_str(), &st)) return;
	struct timespec t[2] = {{0, UTIME_OMIT}, {st.st_mtim.tv_sec, OwnNs}};
	utimensat (AT_FDCWD, p.c_str(), t, 0);
}

// a file that isn't there, or carries the mark; marked: the body's files carry it (TileStream/marked)
bool OwnFile (const std::string &p, bool marked)
{
	struct stat st;
	if (!marked || stat (p.c_str(), &st)) return true;
	return st.st_mtim.tv_nsec == OwnNs;
}

// a use moves the file's time to now; the mark goes with it, and a file without it doesn't gain it
void TouchFile (const std::string &p)
{
	struct stat st;
	struct timespec now;
	if (stat (p.c_str(), &st) || clock_gettime (CLOCK_REALTIME, &now)) return;
	long ns = st.st_mtim.tv_nsec == OwnNs ? OwnNs : (now.tv_nsec == OwnNs ? OwnNs + 100 : now.tv_nsec);
	struct timespec t[2] = {{0, UTIME_OMIT}, {now.tv_sec, ns}};
	utimensat (AT_FDCWD, p.c_str(), t, 0);
}

int MarkAll (const std::string &dir); // the mark on every file the indexes list: 0, -1 no sub-second times, -2 TileStream/marked not written

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
		std::unordered_set<uint64_t> way;  // not upstream (design-ways): way tiles written, Key (14, x, y)
		std::unordered_set<uint64_t> &Vec (int v) { return v ? way : bldg; }
		const std::unordered_set<uint64_t> &Vec (int v) const { return v ? way : bldg; }
		std::string bldgUrl;               // from the TileJSON
		int bldgState = 0;                 // 0 TileJSON not asked, 1 asking, 2 known
		std::unordered_set<uint64_t> elev; // Elev files this stream wrote (elev.idx)
		Toc arch[NARCH];                   // installed Archive/<Layer>.tree tables of contents
		struct Inh {                       // elev-consistency: a key under a source: the source, its file's stamp, a file of ours written
			uint64_t src = 0;
			int64_t mtime = 0;
			uint64_t size = 0;
			bool has = false;
			bool operator== (const Inh &) const = default;
		};
		bool modsKnown = false;                // elev-consistency: the startup scan ran (LoadBases)
		std::unordered_set<uint64_t> addonMod; // elev-consistency: add-ons' Elev_mod files at startup, fixed after it
		std::unordered_map<uint64_t, Inh> inh; // elev-consistency: keys under a source (inh.idx), under modMx
		std::unordered_set<uint64_t> badSrc;   // elev-consistency: sources that would not read, logged once, under modMx
		std::unordered_map<uint64_t, uint32_t> addonLevels; // elev-consistency: per add-on tile, the levels of the add-on's tiles under it (bits)
		std::vector<uint64_t> elevArch;        // elev-consistency: the Elev archive's tiles, sorted, read when first needed, under modMx
		bool elevArchRead = false;
		size_t inhAppends = 0;
		std::unordered_set<std::string> unclean; // indexes with dropped or doubled lines: rewritten at startup
		uint64_t bytes = 0;                // the size cap's running total, once a scan has counted it
		bool bytesKnown = false, writtenDirty = false;
		std::atomic<bool> marked {false};  // design dropin-addons: its files carry the mark, so a file without it is someone else's
		bool Arch (int layer, uint64_t k) const { return arch[layer].Has (KL (k), KLat (k), KLng (k)); }
	};
	struct BJob { int body = 0, stage = 0, tries = 0; uint64_t key = 0; Clock::time_point want, retry; bool pre = false; uint64_t seq = 0; }; // pre, seq: design-preload
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
		bool warm = false;           // not upstream (mars-stream): a cog body's low levels at startup, a prefetch never dropped
		bool eoxWait = false;        // waits out a retry after an EOX failure: falls back at once when EOX pauses
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
	struct Task { std::function<void ()> fn, fail; }; // fail: with mx held, after fn threw
	std::deque<Task> work;
	bool ro = false;                   // another process has the folder: offline, no writes
	std::vector<std::string> lockKeys;
	std::atomic<bool> writeLogged {false};
	bool exLogged = false;
	std::unique_ptr<TsNet> net;
	std::thread ctl;
	std::vector<std::thread> workers;
	Clock::time_point nextEvict, nextHits;
	std::mutex imx;
	std::list<std::pair<uint64_t, QImage>> imgs;
	int tmpSeq = 0;
	std::unordered_set<std::string> crossDirs; // layer folders on another disk than TileStream/tmp, under imx
	std::condition_variable scv; // a worker's WorldCover range reads
	std::unordered_map<std::string, int> syncWait;
	std::unordered_map<std::string, std::pair<int, std::shared_ptr<const QByteArray>>> syncRes;
	int coverFails = 0;
	void FetchAll (std::vector<TsCog::Req> &rq, int h);
	std::unordered_map<uint64_t, BJob> bjobs;               // building tiles, by Id (body, Key (14, x, y))
	std::unordered_map<std::string, uint64_t> binflight;    // url -> bjob id
	std::unordered_map<std::string, int> bjson;             // TileJSON url -> body
	std::unordered_map<uint64_t, Clock::time_point> bhold;

	explicit Impl (const Options &o);
	~Impl ();
	void Init ();
	void Shutdown ();

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
	int WantElev (int b, int L, int ilat, int ilng);
	void Trim ();
	void Run ();
	void Work ();
	void Queue (std::function<void ()> fn, std::function<void ()> fail = nullptr) { work.push_back ({std::move (fn), std::move (fail)}); wcv.notify_one(); }
	void Crash (const char *what, const std::function<void ()> &fail); // with mx held
	std::function<void ()> GiveUp (uint64_t id); // a job that threw: held, erased
	bool Held (int b, uint64_t k);
	void LoadArchives (Body &B);
	void Prune ();
	using VecKeys = std::array<std::vector<uint64_t>, 2>; // not upstream (design-ways): per VecIdx
	void Drop (Body &B, const std::vector<uint64_t> &gone, const std::vector<uint64_t> &egone, const VecKeys &vgone);
	void Dirty (Body &B);
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
	bool CogSurf (const Body &B, int L, int la, int ln, QImage &img); // not upstream (mars-stream): false when a read failed
	bool CogElev (Body &B, const Job &J, std::string &elv);
	void ElevRetry (const Job &J);
	void Retire (Body &B);
	void Failed (const Job &J);
	QImage Ancestor (int body, int L, int la, int ln);
	void Remember (uint64_t id, const QImage &img);
	QImage Recall (uint64_t id);
	bool Write (const Body &B, const std::string &path, const std::string &bytes);
	std::string Idx (const Body &B, const char *name) const { return B.dir + "/TileStream/" + name + ".idx"; }
	void LoadIdx (Body &B);
	void Append (const Body &B, const char *name, uint64_t k, int64_t t);
	void Rewrite (const Body &B, const char *name, const std::unordered_set<uint64_t> &s);
	void RewriteNone (const Body &B);
	bool WriteText (const std::string &p, const std::string &text);
	void MarkNone (Body &B, uint64_t k);
	void CleanTmp ();
	void Evict ();
	bool Ours (const Body &B, const std::string &p) const { return OwnFile (p, B.marked.load (std::memory_order_acquire)); } // design dropin-addons
	void Disown (Body &B, const char *idx, uint64_t k, const std::string &file); // design dropin-addons, with mx held
	bool markLogged = false;
	// earth-sky: prefetch, clouds, base flattening (end of this file)
	uint64_t preSeq = 0;
	int camBody = -1;                  // not upstream (design-preload): SetCamera's ground point
	double camLat = 0.0, camLng = 0.0;
	double CamDist (const Job &J) const; // its tile centre from that point (rad), 0 without one
	int PrefetchVec (int b, int x, int y);
	int Prefetch (const std::string &name, int L, int ilat, int ilng);
	QByteArray Fetch (const std::string &url, int sec);
	QByteArray Source (Body &B, const std::string &url, const char *file, const char *fmt, int sec);
	void Clouds (Body &B);
	void BakeClouds (Body &B, const QImage &src, int l0, int l1);
	void LoadBases (Body &B);
	void BaseSrcs (Body &B, Job &J);
	void BaseHeights (Body &B, const Job &J, const std::vector<QImage> &et);
	void BaseFlat (Body &B, size_t i, double h);
	bool Mods (Body &B, uint64_t k, const std::string &elv);
	std::mutex modMx; // elev-consistency: Elev_mod work, one key at a time; taken before mx
	bool ModsAt (Body &B, uint64_t k, const std::string &elv, bool *changed);
	bool ArchElev (Body &B, uint64_t k);
	bool ElevData (Body &B, uint64_t k);
	uint64_t ModSource (Body &B, uint64_t k);
	Body::Inh Stamp (const Body &B, uint64_t src, bool has) const;
	void SetInh (Body &B, uint64_t k, uint64_t src, bool has);
	void InhLine (Body &B, uint64_t k, const Body::Inh &n);
	void AddonMods (Body &B);
	void Settle (Body &B, uint64_t k);
	int WantVec (int v, int b, int x, int y); // TileStreamMvt.cpp's tiles, below imagery; v: VecIdx
	void StartBldg (Clock::time_point now);
	void BldgDeliver (const std::string &url, int status, const std::shared_ptr<const QByteArray> &data, Clock::time_point now);
	void BuildVec (int b, uint64_t k, std::shared_ptr<const QByteArray> data);
};

static bool Better (const TileStream::Impl &d, const TileStream::Impl::Job &a, const TileStream::Impl::Job &b)
{
	if (a.low != b.low) return !a.low;
	if (a.pre != b.pre) return !a.pre; // earth-sky: prefetch below wants, above refetches
	if (a.pre) return a.seq < b.seq;  // along the track, nearest first
	if (a.required != b.required) return a.required;
	if (a.elevOnly != b.elevOnly) return !a.elevOnly;
	if (KL (a.key) != KL (b.key)) return KL (a.key) < KL (b.key);
	if (d.camBody == a.body && d.camBody == b.body) { // not upstream (design-preload): nearest the camera first
		double da = d.CamDist (a), db = d.CamDist (b);
		if (da != db) return da < db;
	}
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
	host[USGS].cap = 8;
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
		bodies.push_back (std::move (b));
	}
	if (bodies.empty()) return;
	try {
		Init ();
	} catch (...) { // ~Impl never runs when the constructor throws: the locks and threads go here
		Shutdown ();
		throw;
	}
}

// the folder locks, indexes, archives, net, clouds and threads
void TileStream::Impl::Init ()
{
	lockKeys.reserve (bodies.size());
	for (auto &b : bodies) { // one writer per folder: a second Orbiter streams offline and writes nothing
		std::string key = LockKey (b->dir);
		int e = 0;
		int r = TakeLock (key, false, &e);
		if (r > 0) {
			lockKeys.push_back (key);
			if (e) Log ("TileStream: " + b->dir + " has no working file lock (" + std::error_code (e, std::generic_category()).message() + "), streaming as the only Orbiter on it");
			continue;
		}
		ro = true;
		Log ("TileStream: " + b->dir + (r ? " cannot be locked" : " is in use by another Orbiter") + ", streaming offline with no writes");
		break;
	}
	if (ro) {
		for (const std::string &k : lockKeys) DropLock (k);
		lockKeys.clear();
	}
	for (auto &b : bodies) {
		LoadIdx (*b);
		LoadArchives (*b);
		Log ("TileStream: " + b->cfg.name + " tiles go to " + b->dir + (opt.offline || ro ? " (offline)" : ""));
	}
	if (!opt.offline && !ro) net = std::make_unique<TsNet> (opt.userAgent);
	for (auto &b : bodies) Retire (*b); // mars-stream: before the planets look for their tiles
	if (opt.sky && !ro) for (auto &b : bodies) { LoadBases (*b); Clouds (*b); } // earth-sky: before the planet visuals load their tiles
	Clock::time_point now = opt.now();
	nextHits = now + std::chrono::seconds (5);
	nextEvict = now + std::chrono::seconds (300);
	if (!ro) work.push_back ({[this] { CleanTmp (); Prune (); Evict (); }, nullptr});
	if (net) {
		for (int b = 0; b < (int)bodies.size(); b++) {
			Body &B = *bodies[b];
			for (uint64_t k : {Key (1, 0, 0), Key (2, 0, 0), Key (3, 0, 0), Key (4, 0, 0), Key (4, 0, 1)}) {
				std::error_code ec;
				if (!B.Arch (A_SURF, k) && !fs::exists (Tile (B.dir, "Surf", k), ec)) NewJob (b, k, now).required = true;
			}
			for (uint64_t k : B.refetch)
				if (B.written.count (k)) B.refetchQueue.push_back (k);
			if (B.cfg.Cog()) // not upstream (mars-stream): the whole body to L7 once, so it is never blank offline
				for (int L = 4; L <= std::min (7, B.cfg.surf.lmax); L++)
					for (int a = 0; a < 1 << (L - 4); a++)
						for (int c = 0; c < 1 << (L - 3); c++) {
							uint64_t k = Key (L, a, c);
							std::error_code ec;
							if (jobs.count (Id (b, k)) || B.Arch (A_SURF, k)) continue;
							bool surf = fs::exists (Tile (B.dir, "Surf", k), ec);
							const TsSource &es = B.cfg.elev;
							bool elev = es.kind.empty() || L < es.lmin || L > es.lmax || B.Arch (A_ELEV, k) || fs::exists (Tile (B.dir, "Elev", k), ec);
							if (surf && elev) continue;
							Job &J = NewJob (b, k, now);
							J.pre = J.warm = true;
							J.seq = ++preSeq;
							if (surf) { J.elevOnly = true; B.elevSeen.insert (k); }
						}
		}
	}
	workers.reserve (4);
	for (int i = 0; i < 4; i++) workers.emplace_back ([this] { Work (); }); // WorldCover reads and the cloud bake wait in a worker
	ctl = std::thread ([this] { Run (); });
}

TileStream::Impl::~Impl ()
{
	Shutdown ();
}

// the threads stopped and joined, the net gone, the folder locks dropped
void TileStream::Impl::Shutdown ()
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
	for (std::thread &t : workers)
		if (t.joinable()) t.join();
	net.reset();
	for (const std::string &k : lockKeys) DropLock (k);
	lockKeys.clear();
}

int TileStream::Impl::Want (const std::string &name, int L, int ilat, int ilng, int flags)
{
	int b = FindBody (name);
	if (b < 0) return -1;
	if (flags & 4) return WantElev (b, L, ilat, ilng);
	Body &B = *bodies[b];
	if (L < 1 || L > B.cfg.SurfMax() || ilat < 0 || ilng < 0) return -1;
	if (L < 4 ? (ilat || ilng) : (ilat >= 1 << (L - 4) || ilng >= 1 << (L - 3))) return -1;
	if (!B.cfg.Covers (L, ilat, ilng)) return -1; // not upstream (mars-stream): past the colour's levels, outside every DETAIL file
	uint64_t k = Key (L, ilat, ilng), id = Id (b, k);
	std::unique_lock<std::mutex> lk (mx);
	if (stop) return -1;
	if (flags & 2) {
		if (ro || !B.written.count (k)) return 1;
		B.hits.push_back (k);
		const TsSource &es = B.cfg.elev;
		if (!net || es.kind.empty() || L < 4 || L > es.lmax || B.Arch (A_ELEV, k) || !B.elevSeen.insert (k).second || B.noElev.count (k) || jobs.count (id)) return 1;
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
		if (ro || !B.written.count (k)) return -1; // not ours: never replaced
		for (const char *layer : {"Surf", "Mask", "Class"})
			if (!Ours (B, Tile (B.dir, layer, k))) { // design dropin-addons: an add-on's file over ours
				Disown (B, "written", k, Tile (B.dir, layer, k));
				return -1;
			}
		B.written.erase (k);
		Dirty (B);
		lk.unlock();
		std::error_code ec;
		fs::remove (Tile (B.dir, "Surf", k), ec);
		fs::remove (Tile (B.dir, "Mask", k), ec);
		fs::remove (Tile (B.dir, "Class", k), ec);
		lk.lock();
		if (stop) return -1;
	}
	if (Ocean (B, k) || B.Arch (A_SURF, k)) return -1; // the installed archive's tile: never streamed
	if (!net) {
		lk.unlock();
		std::error_code ec;
		return fs::exists (Tile (B.dir, "Surf", k), ec) ? 1 : -1;
	}
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
		j->second.low = j->second.pre = j->second.warm = false; // earth-sky: a want takes over a prefetch
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
			j->second.low = j->second.pre = j->second.warm = false;
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

// the Elev of a tile whose imagery isn't the stream's (an installed archive's, say): an Elev job of its own
int TileStream::Impl::WantElev (int b, int L, int ilat, int ilng)
{
	Body &B = *bodies[b];
	const TsSource &es = B.cfg.elev;
	if (es.kind.empty() || L < 4 || L > es.lmax || ilat < 0 || ilng < 0 || ilat >= 1 << (L - 4) || ilng >= 1 << (L - 3)) return -1;
	uint64_t k = Key (L, ilat, ilng), id = Id (b, k);
	std::error_code ec;
	if (fs::exists (Tile (B.dir, "Elev", k), ec)) return 1;
	std::lock_guard<std::mutex> lk (mx);
	if (stop || !net || Ocean (B, k) || B.noElev.count (k)) return -1;
	if (jobs.count (id)) return 0; // a Surf job of ours queues its Elev when it is built
	B.elevSeen.insert (k);
	NewJob (b, k, opt.now()).elevOnly = true;
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
		try {
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
				if (!ro) Hits ();
			}
			if (now >= nextEvict) {
				nextEvict = now + std::chrono::seconds (300);
				if (!ro) Queue ([this] { Evict (); });
			}
		} catch (const std::exception &e) {
			Crash (e.what(), nullptr);
		} catch (...) {
			Crash ("unknown", nullptr);
		}
	}
}

void TileStream::Impl::Work ()
{
	std::unique_lock<std::mutex> lk (mx);
	for (;;) {
		wcv.wait (lk, [this] { return stop || !work.empty(); });
		if (stop) return;
		Task t = std::move (work.front());
		work.pop_front();
		lk.unlock();
		const char *what = nullptr;
		std::string msg;
		try {
			t.fn ();
		} catch (const std::exception &e) {
			msg = e.what();
			what = msg.c_str();
		} catch (...) {
			what = "unknown";
		}
		lk.lock();
		if (what) Crash (what, t.fail);
	}
}

// a thread caught an exception: logged once, the job given up
void TileStream::Impl::Crash (const char *what, const std::function<void ()> &fail)
{
	if (!exLogged) Log (std::string ("TileStream: a job failed: ") + what);
	exLogged = true;
	if (!fail) return;
	try {
		fail ();
	} catch (...) {
	}
	kick = true;
	cv.notify_all();
}

std::function<void ()> TileStream::Impl::GiveUp (uint64_t id)
{
	return [this, id] {
		auto j = jobs.find (id);
		if (j != jobs.end() && j->second.elevOnly) bodies[j->second.body]->noElev.insert (j->second.key);
		else hold[id] = opt.now() + std::chrono::seconds (HoldSec);
		jobs.erase (id);
	};
}

bool TileStream::Impl::Held (int b, uint64_t k)
{
	std::lock_guard<std::mutex> lk (mx);
	auto h = hold.find (Id (b, k));
	return h != hold.end() && opt.now() < h->second;
}

// a failed-load Want dropped a key: written.idx rewritten on a worker, once for many
void TileStream::Impl::Dirty (Body &B)
{
	if (ro || B.writtenDirty) return;
	B.writtenDirty = true;
	Body *pb = &B;
	Queue ([this, pb] {
		std::lock_guard<std::mutex> lk (mx);
		if (!pb->writtenDirty) return;
		pb->writtenDirty = false;
		Rewrite (*pb, "written", pb->written);
	});
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
	if (h == EOX) // jobs that met EOX failures before the pause don't wait out their retry: StartJobs falls them back now
		for (auto &p : jobs)
			if (p.second.eoxWait && !p.second.stage) p.second.retry = now;
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
		Queue ([this, copy] { Build (copy); }, GiveUp (J.id));
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
		if (++J.tries > 5 && !J.required) {
			hold[J.id] = now + std::chrono::seconds (HoldSec); // given up: the next want waits out the hold
			jobs.erase (J.id);
		} else {
			Retry (J, now, 10);
			J.eoxWait = fail[0] && surfHost;
		}
		return;
	}
	if (miss[0] || miss[1] || miss[2]) {
		MarkNone (B, J.key);
		jobs.erase (J.id);
		return;
	}
	J.stage = 2;
	Job copy = J;
	Queue ([this, copy] { Build (copy); }, GiveUp (J.id));
}

// 0: not now, 1: from its own source, 2: from the fallback while EOX is paused
int TileStream::Impl::Startable (const Job &J, Clock::time_point now) const
{
	const Body &B = *bodies[J.body];
	if (B.cfg.Cog()) return Paused (USGS, now) ? 0 : 1; // not upstream (mars-stream)
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
			bool theirs = false;
			for (const char *layer : {"Surf", "Mask", "Class"})
				if (!theirs && !Ours (B, Tile (B.dir, layer, k))) { // design dropin-addons: not fetched again over an add-on's file
					Disown (B, "written", k, Tile (B.dir, layer, k));
					theirs = true;
				}
			if (theirs) continue;
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
			if (s && (!best || Better (*this, J, *best))) { best = &J; how = s; }
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
	if (B.cfg.Cog()) { // not upstream (mars-stream): the job reads its files itself
		J.stage = 2;
		J.waiting = 0;
		J.eoxWait = false;
		Job copy = J;
		Queue ([this, copy] { Build (copy); }, GiveUp (J.id));
		return;
	}
	if (!J.elevOnly) {
		J.surf = Srcs (J.fallback ? B.cfg.fallback : B.cfg.surf, L, la, ln);
		J.lights = Srcs (B.cfg.lights, L, la, ln);
		J.water = Srcs (B.cfg.water, L, la, ln);
	}
	if (J.elevOnly) J.elev = Srcs (B.cfg.elev, L, la, ln); // a Surf job never waits on Terrarium
	if (J.elevOnly) BaseSrcs (B, J); // earth-sky: base centre heights still unknown
	J.stage = 1;
	J.waiting = 0;
	J.eoxWait = false;
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
			if ((h == WC || h == USGS) && r != std::string::npos && sscanf (url.c_str() + r, "#%lld+%lld", &off, &len) == 2)
				net->Get (url.substr (0, r), back, off, len); // url#off+len: a byte range
			else net->Get (url, back, -1, 0, 0, TsNet::TileStallMs); // not upstream (design-preload): a stalled tile request fails in 8 s
		}
	}
}

void TileStream::Impl::DropStale (Clock::time_point now)
{
	std::chrono::duration<double> stale (opt.staleSec);
	for (auto it = jobs.begin(); it != jobs.end(); ) {
		const Job &J = it->second;
		bool old = now - J.want > (J.pre ? std::chrono::duration<double> (10.0) : stale); // earth-sky: a prefetch the track left behind
		if (!J.stage && !J.required && !J.low && !J.elevOnly && !J.warm && old) it = jobs.erase (it);
		else ++it;
	}
	for (auto it = bjobs.begin(); it != bjobs.end(); ) {
		if (!it->second.stage && now - it->second.want > (it->second.pre ? std::chrono::duration<double> (10.0) : 3 * stale)) it = bjobs.erase (it); // design-preload: a path tile the path left
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
		for (const std::string &p : paths) TouchFile (p); // design dropin-addons: the stream's mark kept
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
	if (opt.onBuild) opt.onBuild (L, la, ln);
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
	if (C.Cog() && J.elevOnly && !CogElev (B, J, elv)) { // not upstream (mars-stream): the heights read here
		ElevRetry (J);
		return;
	}
	if (J.elevOnly) {
		bool mine;
		{
			std::lock_guard<std::mutex> lk (mx);
			mine = B.elev.count (J.key) > 0;
		}
		std::error_code ec;
		bool theirs = !mine && fs::exists (Tile (B.dir, "Elev", J.key), ec); // someone else's Elev: never overwritten
		if (mine && !Ours (B, Tile (B.dir, "Elev", J.key))) { // design dropin-addons: an add-on's Elev over ours
			std::lock_guard<std::mutex> lk (mx);
			Disown (B, "elev", J.key, Tile (B.dir, "Elev", J.key));
			theirs = true;
		}
		bool ok = !theirs && !elv.empty() && Mods (B, J.key, elv) && Write (B, Tile (B.dir, "Elev", J.key), elv); // earth-sky: its Elev_mod first
		if (ok) Settle (B, J.key); // elev-consistency: then the tiles under it
		std::lock_guard<std::mutex> lk (mx);
		if (ok) {
			if (B.elev.insert (J.key).second) Append (B, "elev", J.key, -1);
			B.bytes += elv.size();
		} else {
			B.noElev.insert (J.key);
			for (const Src &s : J.elev) CacheDrop (s.url);
		}
		jobs.erase (J.id);
		kick = true;
		cv.notify_all();
		return;
	}
	std::vector<QImage> st, lt, wt;
	QImage cog; // not upstream (mars-stream)
	if (C.Cog() ? !CogSurf (B, L, la, ln, cog) : (!decode (J.surf, st) || !decode (J.lights, lt) || !decode (J.water, wt))) {
		Failed (J);
		return;
	}
	bool ocean = false, ok = true, theirs = false;
	uint64_t made = 0; // bytes written, for the size cap's total
	{
		bool over;
		{
			std::lock_guard<std::mutex> lk (mx);
			over = B.written.count (J.key) > 0;
		}
		for (const char *layer : {"Surf", "Mask", "Class"})
			if (over && !theirs && !Ours (B, Tile (B.dir, layer, J.key))) { // design dropin-addons: never written over an add-on's file
				std::lock_guard<std::mutex> lk (mx);
				Disown (B, "written", J.key, Tile (B.dir, layer, J.key));
				theirs = true;
			}
	}
	if (theirs) {
		std::lock_guard<std::mutex> lk (mx);
		jobs.erase (J.id);
		kick = true;
		cv.notify_all();
		return;
	}
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
	if (!ocean && C.Cog()) { // not upstream (mars-stream): the Surf alone
		std::string sdds = TsImg::Dds (cog, false);
		ok = Write (B, Tile (B.dir, "Surf", J.key), sdds);
		if (ok) made = sdds.size();
	} else if (!ocean) {
		QImage surf = TsImg::Build (sv, L, la, ln, TsImg::SrcGrid (sv, L, la, ln), st);
		QImage lights = TsImg::Build (C.lights, L, la, ln, TsImg::SrcGrid (C.lights, L, la, ln), lt);
		QImage water = TsImg::Build (C.water, L, la, ln, TsImg::SrcGrid (C.water, L, la, ln), wt);
		QImage cls; // WorldCover: the water bit and the class tile, else GIBS water as before
		if (!C.cover.kind.empty() && L >= C.cover.lmin && L <= C.cover.lmax && net) {
			std::vector<uint8_t> cv;
			int n = water.width();
			if (TsCog::Classes (C.cover.url, L, la, ln, n, [this] (std::vector<TsCog::Req> &r) { FetchAll (r, WC); }, cv)) {
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
		std::string cdds = cls.isNull() ? std::string() : TsImg::Dds (cls, false), mdds = TsImg::Dds (mask, true), sdds = TsImg::Dds (surf, false);
		if (cdds.empty() || !Write (B, cpath, cdds)) { fs::remove (cpath, ec); cdds.clear(); } // before Surf: the client reads them together
		ok = Write (B, mpath, mdds);
		if (ok && !(ok = Write (B, Tile (B.dir, "Surf", J.key), sdds))) {
			fs::remove (mpath, ec); // no Mask without its Surf
			fs::remove (cpath, ec);
		}
		if (ok && L >= 4) Remember (Id (J.body, J.key), surf);
		if (ok) made = sdds.size() + mdds.size() + cdds.size();
	}
	const TsSource &es = C.elev;
	bool elevWanted = ok && !ocean && net && !es.kind.empty() && L >= 4 && L <= es.lmax && !B.Arch (A_ELEV, J.key);
	std::error_code eec;
	bool elevFree = elevWanted && !fs::exists (Tile (B.dir, "Elev", J.key), eec);
	std::lock_guard<std::mutex> lk (mx);
	if (ocean) {
		if (B.ocean.insert (J.key).second) Append (B, "ocean", J.key, -1);
	} else if (ok) {
		if (B.written.insert (J.key).second) Append (B, "written", J.key, -1);
		B.bytes += made;
		B.done.push_back (J.key);
		if (B.done.size() > 1024) B.done.pop_front();
		if (J.fallback) {
			if (B.refetch.insert (J.key).second) Append (B, "refetch", J.key, -1);
		} else if (B.refetch.erase (J.key)) Rewrite (B, "refetch", B.refetch);
	} else hold[J.id] = opt.now() + std::chrono::seconds (60); // Write logged the first failure
	jobs.erase (J.id);
	if (elevWanted && (elevFree || B.elev.count (J.key)) && B.elevSeen.insert (J.key).second)
		NewJob (J.body, J.key, opt.now()).elevOnly = true; // its Elev, a job of its own, never over someone else's
	kick = true;
	cv.notify_all();
}

// a worker's range reads on host h (WorldCover, USGS), through its cap and pause; waits for all of them
void TileStream::Impl::FetchAll (std::vector<TsCog::Req> &rq, int h)
{
	std::unique_lock<std::mutex> lk (mx);
	std::vector<std::string> keys;
	for (TsCog::Req &r : rq) {
		keys.push_back (r.url + "#" + std::to_string (r.off) + "+" + std::to_string (r.len));
		const std::string &k = keys.back();
		if ((r.data = CacheGet (k))) { r.status = 200; continue; }
		if (!net || stop || Paused (h, opt.now())) { r.status = 0; continue; }
		r.status = -1;
		syncWait[k]++;
		if (!inflight.count (k)) {
			inflight[k];
			host[h].queue.push_back (k);
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

// not upstream (mars-stream): a cog body's Surf: the colour file, times its DETAIL over the detail's low-pass past the colour's levels
bool TileStream::Impl::CogSurf (const Body &B, int L, int la, int ln, QImage &img)
{
	const TsBodyCfg &C = B.cfg;
	auto fetch = [this] (std::vector<TsCog::Req> &r) { FetchAll (r, USGS); };
	int N = TsImg::Size (L);
	double w, n, dw, dh;
	TsImg::Bounds (L, la, ln, w, n, dw, dh);
	TsCog::Pts p {n - 0.5 * dh / N, w + 0.5 * dw / N, -dh / N, dw / N, N, N, dh / N}; // pixel centres, rows from the north
	std::vector<float> c, col, hi, lo;
	std::vector<uint8_t> cv, dv;
	int spp = 0;
	if (!TsCog::Sample (C.surf.url, p, fetch, c, cv, &spp) || !spp) return false; // no colour file (404): never a black tile
	size_t np = (size_t)N * N;
	col.assign (3 * np, 0.0f);
	if (spp > 0 && c.size() >= np * spp)
		for (size_t i = 0; i < np; i++) {
			const float *s = &c[i * spp];
			float *o = &col[3 * i];
			if (!C.palette.empty()) TsImg::Paint (C.palette, spp >= 3 ? 0.299 * s[0] + 0.587 * s[1] + 0.114 * s[2] : s[0], o); // design Rev 3: the file's brightness, the palette's colour
			else for (int k = 0; k < 3; k++) o[k] = s[spp >= 3 ? k : 0]; // grey: the same in each channel
		}
	if (L > C.surf.lmax) {
		TsCog::Pts q = p;
		q.pix = TsCog::Pixel (C.surf.url, fetch); // the low-pass: the detail at the colour's own pixel
		hi.assign (np, 0.0f);
		lo.assign (np, 0.0f);
		dv.assign (np, 0);
		for (const TsSource &d : C.detail) {
			if (!d.Over (L, la, ln)) continue;
			std::vector<float> h1, l1;
			std::vector<uint8_t> v1, v2;
			if (!TsCog::Sample (d.url, p, fetch, h1, v1) || !TsCog::Sample (d.url, q, fetch, l1, v2)) return false;
			if (h1.size() < np || l1.size() < np) continue; // no file there
			for (size_t i = 0; i < np; i++)
				if (!dv[i] && v1[i] && v2[i]) { hi[i] = h1[i]; lo[i] = l1[i]; dv[i] = 1; } // the first file with data
		}
	}
	img = TsImg::Detail (N, col, cv, hi, lo, dv);
	return true;
}

// not upstream (mars-stream): an Elev read that failed: again in 30 s, twice, then none this session (as a failed Terrain Tiles fetch)
void TileStream::Impl::ElevRetry (const Job &J)
{
	std::lock_guard<std::mutex> lk (mx);
	auto it = jobs.find (J.id);
	if (it != jobs.end()) {
		if (++it->second.tries >= 3) {
			bodies[J.body]->noElev.insert (J.key);
			jobs.erase (it);
		} else Retry (it->second, opt.now(), 30);
	}
	kick = true;
	cv.notify_all();
}

// not upstream (mars-stream): an old pack's files as installed (size and CRC-32 from its list) deleted once, so the stream makes them;
// a changed or other file is the user's; online only, so the body is never left without its tiles
void TileStream::Impl::Retire (Body &B)
{
	if (B.cfg.retire.empty() || ro || !net) return;
	std::string mark = B.dir + "/TileStream/retired-" + B.cfg.retire;
	std::error_code ec;
	if (fs::exists (mark, ec)) return;
	std::string list = (fs::path (opt.cfgfile).parent_path() / "TileStream" / B.cfg.retire).string();
	std::ifstream f (list);
	if (!f) {
		Log ("TileStream: cannot read " + list);
		return;
	}
	std::string line;
	size_t gone = 0, kept = 0;
	std::vector<char> buf ((size_t)1 << 20);
	while (std::getline (f, line)) {
		std::istringstream is (line);
		std::string rel;
		unsigned long long size;
		std::string hex;
		if (line.empty() || line[0] == ';' || !(is >> rel >> size >> hex)) continue;
		if (rel.find ("..") != std::string::npos || rel[0] == '/' || rel.find ('\\') != std::string::npos) continue;
		std::string p = B.dir + "/" + rel;
		struct stat st;
		if (stat (p.c_str(), &st) || !S_ISREG (st.st_mode) || (unsigned long long)st.st_size != size) continue;
		std::ifstream in (p, std::ios::binary);
		uLong crc = crc32 (0L, Z_NULL, 0);
		while (in) {
			in.read (buf.data(), (std::streamsize)buf.size());
			if (in.gcount() > 0) crc = crc32 (crc, (const Bytef*)buf.data(), (uInt)in.gcount());
		}
		if (crc == std::strtoul (hex.c_str(), nullptr, 16)) {
			if (fs::remove (p, ec)) gone++;
		} else kept++;
	}
	WriteText (mark, std::to_string (gone) + " retired, " + std::to_string (kept) + " changed and kept\n");
	Log ("TileStream: " + B.cfg.name + ": " + std::to_string (gone) + " files of the old pack retired (" + B.cfg.retire + ")" + (kept ? ", " + std::to_string (kept) + " changed ones kept" : ""));
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
	if (ro) return false;
	std::error_code ec;
	fs::create_directories (fs::path (path).parent_path(), ec);
	size_t sl = path.compare (0, B.dir.size() + 1, B.dir + "/") ? std::string::npos : path.find ('/', B.dir.size() + 1);
	std::string layer = sl == std::string::npos ? fs::path (path).parent_path().string() : path.substr (0, sl); // Textures/<Body>/<Layer>
	auto ends = [&layer] (const std::string &s) { return layer.size() >= s.size() && !layer.compare (layer.size() - s.size(), s.size(), s); };
	bool heights = ends ("/Elev") || ends ("/Elev_mod"); // elev-consistency: TsElevGen
	auto placed = [heights, &path] {
		MarkFile (path); // design dropin-addons: the stream's file
		if (heights) tsElevGen.fetch_add (1, std::memory_order_release);
		return true;
	};
	int seq;
	bool cross;
	{
		std::lock_guard<std::mutex> lk (imx);
		seq = ++tmpSeq;
		cross = crossDirs.count (layer) > 0;
	}
	std::string tmp = B.dir + "/TileStream/tmp/" + std::to_string (getpid()) + "." + std::to_string (seq) + ".tmp";
	auto put = [&bytes] (const std::string &f) {
		std::ofstream o (f, std::ios::binary | std::ios::trunc);
		o.write (bytes.data(), (std::streamsize)bytes.size());
		o.close();
		return !o.fail();
	};
	std::error_code rc;
	if (cross) ec = std::make_error_code (std::errc::cross_device_link); // known to be on another disk: straight to .part
	else if (!put (tmp)) ec = std::make_error_code (std::errc::io_error);
	else if (opt.crossDisk) ec = std::make_error_code (std::errc::cross_device_link);
	else fs::rename (tmp, path, ec);
	if (!ec) return placed ();
	if (!cross) fs::remove (tmp, rc);
	if (!cross && ec == std::errc::cross_device_link) {
		std::lock_guard<std::mutex> lk (imx);
		crossDirs.insert (layer);
	}
	std::string part = path + ".part"; // another disk: written beside the target, then renamed there
	std::error_code pc;
	if (put (part)) {
		fs::rename (part, path, pc);
		if (!pc) return placed ();
	} else pc = std::make_error_code (std::errc::io_error);
	fs::remove (part, rc);
	if (!writeLogged.exchange (true)) Log ("TileStream: cannot write " + path + " (" + ec.message() + ", then " + pc.message() + ")");
	return false;
}

void TileStream::Impl::LoadIdx (Body &B)
{
	auto load = [&] (const char *name, bool extra, int lmin, int lmax, auto add) {
		std::ifstream f (Idx (B, name));
		std::string line;
		size_t lines = 0, good = 0;
		while (std::getline (f, line)) {
			if (line.find_first_not_of (" \t\r") == std::string::npos) continue;
			lines++;
			int L, a, b;
			long long t = 0;
			if (ParseLine (line, extra, L, a, b, t) && KeyOk (name, L, a, b, lmin, lmax) && add (Key (L, a, b), (int64_t)t)) good++;
		}
		if (good != lines) B.unclean.insert (name);
	};
	load ("written", true, 1, 21, [&] (uint64_t k, int64_t) { return B.written.insert (k).second; });
	load ("ocean", false, 4, 21, [&] (uint64_t k, int64_t) { return B.ocean.insert (k).second; });
	load ("refetch", false, 1, 21, [&] (uint64_t k, int64_t) { return B.refetch.insert (k).second; });
	load ("bldg", false, 14, 14, [&] (uint64_t k, int64_t) { return B.bldg.insert (k).second; });
	load ("way", false, 14, 14, [&] (uint64_t k, int64_t) { return B.way.insert (k).second; }); // not upstream (design-ways)
	std::error_code ec;
	auto age = fs::file_time_type::clock::now() - fs::last_write_time (Idx (B, "none"), ec);
	if (!ec && age > std::chrono::hours (24 * 30)) { // nothing marked for 30 days: every line is out of date
		if (!ro) fs::remove (Idx (B, "none"), ec);
	} else load ("none", true, 1, 21, [&] (uint64_t k, int64_t t) {
		if (Today() - t >= 30) return false; // asked again: the line goes at startup
		bool fresh = !B.none.count (k);
		B.none[k] = t;
		return fresh;
	});
	B.marked.store (fs::exists (B.dir + "/TileStream/marked", ec), std::memory_order_release); // design dropin-addons
	if (fs::exists (Idx (B, "elev"), ec)) load ("elev", false, 4, 21, [&] (uint64_t k, int64_t) { return B.elev.insert (k).second; });
	else { // from before elev.idx: the Elev of every streamed tile is the stream's, once
		B.elev = B.written;
		if (!ro) Rewrite (B, "elev", B.elev);
	}
}

void TileStream::Impl::Append (const Body &B, const char *name, uint64_t k, int64_t t)
{
	if (ro) return;
	std::ofstream f (Idx (B, name), std::ios::app);
	f << KL (k) << ' ' << KLat (k) << ' ' << KLng (k);
	if (t >= 0) f << ' ' << t;
	f << '\n';
}

// the old index stays when the new one can't be written whole
bool TileStream::Impl::WriteText (const std::string &p, const std::string &text)
{
	if (ro) return false;
	std::string tmp = p + ".new";
	bool ok;
	{
		std::ofstream f (tmp, std::ios::trunc);
		f << text;
		f.close();
		ok = !f.fail();
	}
	std::error_code ec;
	if (ok) fs::rename (tmp, p, ec);
	if (!ok || ec) {
		std::error_code rc;
		fs::remove (tmp, rc);
		Log ("TileStream: cannot rewrite " + p);
		return false;
	}
	return true;
}

void TileStream::Impl::Rewrite (const Body &B, const char *name, const std::unordered_set<uint64_t> &s)
{
	std::string text;
	for (uint64_t k : s) text += std::to_string (KL (k)) + ' ' + std::to_string (KLat (k)) + ' ' + std::to_string (KLng (k)) + '\n';
	WriteText (Idx (B, name), text);
}

void TileStream::Impl::RewriteNone (const Body &B)
{
	std::string text;
	for (auto &p : B.none) text += std::to_string (KL (p.first)) + ' ' + std::to_string (KLat (p.first)) + ' ' + std::to_string (KLng (p.first)) + ' ' + std::to_string (p.second) + '\n';
	WriteText (Idx (B, "none"), text);
}

void TileStream::Impl::MarkNone (Body &B, uint64_t k)
{
	int64_t t = Today();
	bool fresh = !B.none.count (k);
	B.none[k] = t;
	if (fresh) Append (B, "none", k, t);
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
	if (!cap || ro) return;
	for (auto &bp : bodies) {
		Body &B = *bp;
		std::vector<std::pair<uint64_t, bool>> keys; // with: its Elev is ours
		std::vector<uint64_t> ekeys;
		VecKeys vkeys, vgone; // not upstream (design-ways): buildings and ways
		{
			std::lock_guard<std::mutex> lk (mx);
			if (B.bytesKnown && B.bytes <= cap) continue; // the running total: no scan below the cap
			for (uint64_t k : B.written) keys.push_back ({k, B.elev.count (k) > 0});
			for (uint64_t k : B.elev) if (!B.written.count (k)) ekeys.push_back (k); // our Elev whose Surf went
			for (int vi = 0; vi < 2; vi++) vkeys[vi].assign (B.Vec (vi).begin(), B.Vec (vi).end());
		}
		struct E { fs::file_time_type t; uint64_t k, size; int vec; bool elev, alone; }; // vec: VecIdx, -1 a tile
		std::vector<E> v;
		std::vector<uint64_t> gone, egone;
		uint64_t total = 0;
		for (int vi = 0; vi < 2; vi++)
			for (uint64_t k : vkeys[vi]) {
				if (quit) return;
				std::error_code e1;
				std::string s = VecFile (B.dir, vi, k);
				uint64_t size = fs::file_size (s, e1);
				if (e1) { vgone[vi].push_back (k); continue; }
				total += size;
				v.push_back ({fs::last_write_time (s, e1), k, size, vi, false, false});
			}
		for (uint64_t k : ekeys) {
			if (quit) return;
			std::error_code e1;
			std::string s = Tile (B.dir, "Elev", k);
			uint64_t size = fs::file_size (s, e1);
			if (e1) { egone.push_back (k); continue; }
			total += size;
			if (KL (k) > 8) v.push_back ({fs::last_write_time (s, e1), k, size, -1, true, true});
		}
		for (auto &p : keys) {
			if (quit) return;
			uint64_t k = p.first;
			std::error_code e1, e2;
			std::string s = Tile (B.dir, "Surf", k);
			uint64_t size = fs::file_size (s, e1);
			if (e1) { gone.push_back (k); continue; }
			for (const char *layer : {"Mask", "Elev", "Class"}) {
				if (!p.second && !strcmp (layer, "Elev")) continue; // someone else's Elev: not counted, never removed
				uint64_t n = fs::file_size (Tile (B.dir, layer, k), e2);
				if (!e2) size += n;
			}
			total += size;
			if (KL (k) > 8) v.push_back ({fs::last_write_time (s, e1), k, size, -1, p.second, false});
		}
		if (total > cap) {
			std::sort (v.begin(), v.end(), [] (const E &a, const E &b) { return a.t < b.t; });
			for (const E &e : v) {
				if (total <= cap / 10 * 9) break;
				std::error_code ec;
				if (e.alone && fs::exists (Tile (B.dir, "Surf", e.k), ec)) continue; // its tile made again since the scan
				std::string other; // design dropin-addons: an add-on's file over ours is left, its key leaves the index
				const char *oidx = e.vec >= 0 ? VecIdx[e.vec] : "written";
				if (e.vec >= 0) { if (!Ours (B, VecFile (B.dir, e.vec, e.k))) other = VecFile (B.dir, e.vec, e.k); }
				else for (const char *layer : {"Surf", "Mask", "Elev", "Class"}) {
					if ((!e.elev && !strcmp (layer, "Elev")) || (e.alone && strcmp (layer, "Elev"))) continue;
					if (other.empty() && !Ours (B, Tile (B.dir, layer, e.k))) {
						other = Tile (B.dir, layer, e.k);
						oidx = strcmp (layer, "Elev") ? "written" : "elev";
					}
				}
				if (!other.empty()) {
					total -= e.size; // no longer the stream's bytes
					std::lock_guard<std::mutex> lk (mx);
					Disown (B, oidx, e.k, other);
					continue;
				}
				total -= e.size;
				if (e.vec >= 0) {
					fs::remove (VecFile (B.dir, e.vec, e.k), ec);
					vgone[e.vec].push_back (e.k);
					continue;
				}
				for (const char *layer : {"Surf", "Mask", "Elev", "Class"}) {
					if ((!e.elev && !strcmp (layer, "Elev")) || (e.alone && strcmp (layer, "Elev"))) continue;
					std::string f = Tile (B.dir, layer, e.k);
					fs::remove (f, ec);
					fs::remove (f + ".part", ec);
				}
				if (!e.alone) gone.push_back (e.k);
				if (e.elev) {
					egone.push_back (e.k);
					tsElevGen.fetch_add (1, std::memory_order_release); // elev-consistency: the heights there changed
				}
			}
		}
		if (opt.evictScanned) opt.evictScanned ();
		{
			std::lock_guard<std::mutex> lk (mx);
			B.bytes = total;
			B.bytesKnown = true;
			Drop (B, gone, egone, vgone);
		}
		for (uint64_t k : egone) Settle (B, k); // elev-consistency: the tiles under a removed Elev
	}
}

// with mx held: index entries whose own file is gone (a tile written again since the scan stays)
void TileStream::Impl::Drop (Body &B, const std::vector<uint64_t> &gone, const std::vector<uint64_t> &egone, const VecKeys &vgone)
{
	std::error_code ec;
	bool w = false, r = false, e = false;
	for (int vi = 0; vi < 2; vi++) { // not upstream (design-ways): buildings and ways
		bool b = false;
		for (uint64_t k : vgone[vi])
			if (!fs::exists (VecFile (B.dir, vi, k), ec)) b |= B.Vec (vi).erase (k) > 0;
		if (b) Rewrite (B, VecIdx[vi], B.Vec (vi));
	}
	for (uint64_t k : gone)
		if (!fs::exists (Tile (B.dir, "Surf", k), ec) && B.written.erase (k)) {
			w = true;
			r |= B.refetch.erase (k) > 0;
		}
	for (uint64_t k : egone)
		if (!fs::exists (Tile (B.dir, "Elev", k), ec)) e |= B.elev.erase (k) > 0;
	if (w) Rewrite (B, "written", B.written);
	if (r) Rewrite (B, "refetch", B.refetch);
	if (e) Rewrite (B, "elev", B.elev);
}

// design dropin-addons, with mx held: someone else put a file over one of key k's (an add-on's zip): the key leaves the index, the file stays
void TileStream::Impl::Disown (Body &B, const char *idx, uint64_t k, const std::string &file)
{
	bool had;
	if (!strcmp (idx, "written")) {
		had = B.written.erase (k) > 0;
		if (B.refetch.erase (k)) Rewrite (B, "refetch", B.refetch);
		if (had) Rewrite (B, "written", B.written);
	} else if (!strcmp (idx, "elev")) {
		if ((had = B.elev.erase (k) > 0)) Rewrite (B, "elev", B.elev);
	} else if (!strcmp (idx, "mod")) {
		if ((had = B.mod.erase (k) > 0)) Rewrite (B, "mod", B.mod);
	} else if (!strcmp (idx, "bldg")) {
		if ((had = B.bldg.erase (k) > 0)) Rewrite (B, "bldg", B.bldg);
	} else if (!strcmp (idx, "way")) { // not upstream (design-ways)
		if ((had = B.way.erase (k) > 0)) Rewrite (B, "way", B.way);
	} else return;
	if (had) Log ("TileStream: " + file + " is not the stream's file now (an add-on's?): left alone");
}

// at startup, on a worker: index lines whose file is gone go, doubled or bad lines are written out, and the size total is counted
void TileStream::Impl::Prune ()
{
	for (int bi = 0; bi < (int)bodies.size(); bi++) {
		Body &B = *bodies[bi];
		if (!ro && !B.marked.load()) { // design dropin-addons: once, the mark on the files the indexes list
			int m = MarkAll (B.dir);
			if (!m) {
				B.marked.store (true, std::memory_order_release);
				Log ("TileStream: the stream's files in " + B.dir + " carry its mark; an add-on's file put over one is left alone");
			} else if (!markLogged) {
				markLogged = true;
				Log ("TileStream: " + B.dir + (m == -1 ? " keeps no sub-second file times" : ": TileStream/marked can't be written") + ", an add-on's tile over a streamed one is not told apart");
			}
		}
		std::vector<std::pair<uint64_t, bool>> keys;
		std::vector<uint64_t> ekeys, gone, egone;
		VecKeys vkeys, vgone; // not upstream (design-ways): buildings and ways
		{
			std::lock_guard<std::mutex> lk (mx);
			for (uint64_t k : B.written) keys.push_back ({k, B.elev.count (k) > 0});
			for (uint64_t k : B.elev) if (!B.written.count (k)) ekeys.push_back (k);
			for (int vi = 0; vi < 2; vi++) vkeys[vi].assign (B.Vec (vi).begin(), B.Vec (vi).end());
		}
		uint64_t total = 0;
		for (auto &p : keys) {
			if (quit) return;
			std::error_code ec;
			uint64_t n = fs::file_size (Tile (B.dir, "Surf", p.first), ec);
			if (ec) {
				gone.push_back (p.first);
				if (p.second) egone.push_back (p.first);
				continue;
			}
			total += n;
			for (const char *layer : {"Mask", "Elev", "Class"}) {
				if (!p.second && !strcmp (layer, "Elev")) continue;
				n = fs::file_size (Tile (B.dir, layer, p.first), ec);
				if (!ec) total += n;
				else if (layer[0] == 'E') egone.push_back (p.first);
			}
		}
		for (uint64_t k : ekeys) { // our Elev whose Surf went: counted as Evict counts it
			if (quit) return;
			std::error_code ec;
			uint64_t n = fs::file_size (Tile (B.dir, "Elev", k), ec);
			if (ec) egone.push_back (k);
			else total += n;
		}
		for (int vi = 0; vi < 2; vi++)
			for (uint64_t k : vkeys[vi]) {
				if (quit) return;
				std::error_code ec;
				uint64_t n = fs::file_size (VecFile (B.dir, vi, k), ec);
				if (ec) vgone[vi].push_back (k);
				else total += n;
			}
		std::lock_guard<std::mutex> lk (mx);
		for (uint64_t k : gone) { // the rest of a tile whose Surf is gone, unless it is being made again
			std::error_code ec;
			if (B.written.count (k) && !jobs.count (Id (bi, k)) && !fs::exists (Tile (B.dir, "Surf", k), ec))
				for (const char *layer : {"Surf", "Mask", "Class"}) {
					if (!Ours (B, Tile (B.dir, layer, k))) continue; // design dropin-addons: an add-on's file stays
					fs::remove (Tile (B.dir, layer, k), ec);
					fs::remove (Tile (B.dir, layer, k) + ".part", ec);
				}
		}
		size_t nw = B.written.size(), ne = B.elev.size(), nv[2] = {B.bldg.size(), B.way.size()};
		Drop (B, gone, egone, vgone);
		if (B.unclean.count ("written") && nw == B.written.size()) Rewrite (B, "written", B.written);
		if (B.unclean.count ("elev") && ne == B.elev.size()) Rewrite (B, "elev", B.elev);
		for (int vi = 0; vi < 2; vi++)
			if (B.unclean.count (VecIdx[vi]) && nv[vi] == B.Vec (vi).size()) Rewrite (B, VecIdx[vi], B.Vec (vi));
		if (B.unclean.count ("ocean")) Rewrite (B, "ocean", B.ocean);
		if (B.unclean.count ("refetch")) Rewrite (B, "refetch", B.refetch);
		if (B.unclean.count ("none")) RewriteNone (B);
		if (B.unclean.count ("cloud")) Rewrite (B, "cloud", B.cloud);
		if (B.unclean.count ("mod")) Rewrite (B, "mod", B.mod);
		B.unclean.clear();
		B.bytes = total;
		B.bytesKnown = true;
	}
}

// installed archives: their tables of contents, and our loose tiles they hold deleted (they would hide the archive)
// with an ELEV source the elevation is ours: an Elev archive (Orbiter 2016's is a surface model, canopy and roofs in it) is not read
void TileStream::Impl::LoadArchives (Body &B)
{
	const char *name[NARCH] = {"Surf", "Mask", "Elev", "Cloud"};
	int n = 0;
	for (int a = 0; a < NARCH; a++)
		if (a != A_ELEV || B.cfg.elev.kind.empty()) n += ReadToc (B.dir + "/Archive/" + name[a] + ".tree", B.arch[a]);
	if (!n || ro) return;
	size_t dropped = 0;
	std::error_code ec;
	auto rm = [&] (const char *layer, uint64_t k) {
		std::string f = Tile (B.dir, layer, k);
		if (!Ours (B, f)) return; // design dropin-addons: an add-on's file stays (its key leaves the index below)
		fs::remove (f, ec);
		fs::remove (f + ".part", ec);
	};
	bool w = false, r = false, e = false;
	for (auto it = B.written.begin(); it != B.written.end(); ) {
		uint64_t k = *it;
		if (B.Arch (A_SURF, k)) {
			for (const char *layer : {"Surf", "Mask", "Class"}) rm (layer, k);
			r |= B.refetch.erase (k) > 0;
			it = B.written.erase (it);
			w = true;
			dropped++;
			continue;
		}
		if (B.Arch (A_MASK, k)) rm ("Mask", k);
		++it;
	}
	for (auto it = B.elev.begin(); it != B.elev.end(); ) {
		if (B.Arch (A_ELEV, *it)) {
			rm ("Elev", *it);
			it = B.elev.erase (it);
			e = true;
			dropped++;
		} else ++it;
	}
	if (w) Rewrite (B, "written", B.written);
	if (r) Rewrite (B, "refetch", B.refetch);
	if (e) Rewrite (B, "elev", B.elev);
	Log ("TileStream: " + std::to_string (n) + " installed archives for " + B.cfg.name + ", " + std::to_string (dropped) + " streamed tiles they hold deleted");
}

// building and way tiles: their own jobs on the OpenFreeMap host, started when no imagery job waits; one fetch makes both files
int TileStream::Impl::WantVec (int v, int b, int x, int y)
{
	Body &B = *bodies[b];
	if (!(v ? opt.ways : opt.buildings) || B.cfg.bldgUrl.empty() || x < 0 || y < 0 || x >= 1 << 14 || y >= 1 << 14) return -1;
	uint64_t k = Key (14, x, y), id = Id (b, k);
	std::unique_lock<std::mutex> lk (mx);
	if (stop) return -1;
	if (B.Vec (v).count (k)) return 1;
	Clock::time_point now = opt.now();
	auto j = bjobs.find (id);
	if (j != bjobs.end()) {
		j->second.want = now;
		j->second.pre = false; // design-preload: the camera wants it now
		return 0;
	}
	lk.unlock();
	std::error_code ec;
	bool have = fs::exists (VecFile (B.dir, v, k), ec);
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
		for (auto it = bjobs.begin(); it != bjobs.end(); ++it) {
			if (it->second.stage) continue;
			const BJob &a = it->second;
			if (worst == bjobs.end()) { worst = it; continue; }
			const BJob &w = worst->second; // design-preload: path jobs first, the furthest along the path
			if (a.pre != w.pre ? a.pre : a.pre ? a.seq > w.seq : a.want < w.want) worst = it;
		}
		if (worst == bjobs.end()) break;
		bjobs.erase (worst);
	}
	kick = true;
	cv.notify_all();
	return 0;
}

// not upstream (design-preload): a z14 tile on the path ahead; a path job, after the camera's
int TileStream::Impl::PrefetchVec (int b, int x, int y)
{
	Body &B = *bodies[b];
	if (!(opt.buildings || opt.ways) || B.cfg.bldgUrl.empty() || !net || x < 0 || y < 0 || x >= 1 << 14 || y >= 1 << 14) return -1;
	uint64_t k = Key (14, x, y), id = Id (b, k);
	std::unique_lock<std::mutex> lk (mx);
	if (stop) return -1;
	bool need[2] = {opt.buildings && !B.Vec (0).count (k), opt.ways && !B.Vec (1).count (k)};
	if (!need[0] && !need[1]) return 1;
	Clock::time_point now = opt.now();
	auto j = bjobs.find (id);
	if (j != bjobs.end()) {
		if (j->second.pre) { j->second.want = now; j->second.seq = ++preSeq; }
		return 1;
	}
	auto h = bhold.find (id);
	if (h != bhold.end() && now < h->second) return -1;
	int pre = 0;
	for (auto &p : bjobs) pre += p.second.pre;
	if (pre >= 24 || bjobs.size() >= 64) return 0; // room left for the camera's wants
	lk.unlock();
	std::error_code ec;
	for (int v = 0; v < 2; v++) if (need[v]) need[v] = !fs::exists (VecFile (B.dir, v, k), ec);
	lk.lock();
	if (stop) return -1;
	if (!need[0] && !need[1]) return 1;
	if (bjobs.count (id)) return 1;
	if (!B.bldgState) {
		B.bldgState = 1;
		bjson[B.cfg.bldgJson] = b;
		host[BLDG].queue.push_front (B.cfg.bldgJson);
	}
	BJob &J = bjobs[id];
	J.body = b;
	J.key = k;
	J.want = now;
	J.pre = true;
	J.seq = ++preSeq;
	kick = true;
	cv.notify_all();
	return 1;
}

// not upstream (design-preload): a job's tile centre from the camera's ground point
double TileStream::Impl::CamDist (const Job &J) const
{
	int L = KL (J.key);
	if (L < 4) return 0.0;
	double span = M_PI / (1 << (L - 4)), lat = M_PI / 2 - (KLat (J.key) + 0.5) * span, lng = (KLng (J.key) + 0.5) * span - M_PI;
	double c = sin (lat) * sin (camLat) + cos (lat) * cos (camLat) * cos (lng - camLng);
	return acos (std::clamp (c, -1.0, 1.0));
}

void TileStream::Impl::StartBldg (Clock::time_point now)
{
	if (bjobs.empty() || Paused (BLDG, now)) return;
	int cap = host[BLDG].cap; // not upstream (design-preload): its 4 requests while imagery waits, another server
	auto BldgCamDist = [this] (const BJob &J) { // design-preload: a z14 tile centre from the camera's ground point
		if (camBody != J.body) return 0.0;
		double lat, lng;
		TsBldg::LatLng (14, KLat (J.key) + 0.5, KLng (J.key) + 0.5, lat, lng);
		return acos (std::clamp (sin (lat) * sin (camLat) + cos (lat) * cos (camLat) * cos (lng - camLng), -1.0, 1.0));
	};
	int active = 0;
	for (auto &p : bjobs) if (p.second.stage) active++;
	while (active < cap) {
		BJob *best = nullptr;
		for (auto &p : bjobs) {
			BJob &J = p.second;
			if (J.stage || J.retry > now || bodies[J.body]->bldgState != 2) continue;
			if (!best) { best = &J; continue; }
			if (J.pre != best->pre) { if (!J.pre) best = &J; continue; } // design-preload: the camera's first
			if (J.pre) { if (J.seq < best->seq) best = &J; continue; } // along the path
			double dj = BldgCamDist (J), db = BldgCamDist (*best);
			if (dj != db ? dj < db : J.want > best->want) best = &J; // nearest the camera, else the latest want
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
		Queue ([this, b, k, d] { BuildVec (b, k, d); }, [this, id] {
			bhold[id] = opt.now() + std::chrono::seconds (HoldSec);
			bjobs.erase (id);
		});
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

// the files of the tile still missing, buildings and (design-ways) ways, from one pbf
void TileStream::Impl::BuildVec (int b, uint64_t k, std::shared_ptr<const QByteArray> data)
{
	Body &B = *bodies[b];
	bool need[2];
	{
		std::lock_guard<std::mutex> lk (mx);
		for (int vi = 0; vi < 2; vi++) need[vi] = (vi ? opt.ways : opt.buildings) && !B.Vec (vi).count (k);
	}
	std::error_code ec;
	for (int vi = 0; vi < 2; vi++)
		if (need[vi] && fs::exists (VecFile (B.dir, vi, k), ec)) need[vi] = false; // there now, or an add-on's
	std::string raw = data ? data->toStdString() : std::string(), pbf;
	bool ok = true;
	if (raw.size() >= 2 && (uint8_t)raw[0] == 0x1f && (uint8_t)raw[1] == 0x8b) ok = TsBldg::Inflate (raw, pbf);
	else pbf.swap (raw);
	std::string enc[2];
	size_t n[2] = {0, 0};
	if (ok && need[0]) {
		std::vector<TsBldg::Feature> f;
		uint32_t ext = 4096;
		ok = TsBldg::Decode (pbf, f, ext);
		if (ok) {
			TsBldg::Mesh m = TsBldg::Build (14, KLat (k), KLng (k), f, ext);
			n[0] = m.b.size();
			enc[0] = TsBldg::Encode (m);
		}
	}
	if (ok && need[1]) {
		TsWay::Tile t;
		t.x = KLat (k);
		t.y = KLng (k);
		ok = TsWay::Decode (pbf, t);
		if (ok) {
			n[1] = t.f.size();
			enc[1] = TsWay::Encode (t);
		}
	}
	bool put[2] = {false, false};
	for (int vi = 0; vi < 2 && ok; vi++)
		if (need[vi]) ok = put[vi] = Write (B, VecFile (B.dir, vi, k), enc[vi]);
	std::lock_guard<std::mutex> lk (mx);
	for (int vi = 0; vi < 2; vi++)
		if (put[vi]) {
			if (B.Vec (vi).insert (k).second) Append (B, VecIdx[vi], k, -1);
			B.bytes += enc[vi].size();
			Log ("TileStream: " + std::string (vi ? "ways" : "buildings") + " 14/" + std::to_string (KLat (k)) + "/" + std::to_string (KLng (k)) + ": " + std::to_string (n[vi]));
		}
	if (!ok) {
		Log ("TileStream: cannot make " + std::string (need[0] && !put[0] ? "buildings" : "ways") + " 14/" + std::to_string (KLat (k)) + "/" + std::to_string (KLng (k)));
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

int TileStream::Layers (const std::string &body) const // not upstream (mars-stream)
{
	int b = d->FindBody (body);
	if (b < 0) return 0;
	const TsBodyCfg &c = d->bodies[b]->cfg;
	return (c.Masks() ? 1 : 0) | (c.cover.kind.empty() ? 0 : 2) | (c.bldgUrl.empty() ? 0 : 4);
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
	return b < 0 ? -1 : d->WantVec (0, b, x, y);
}

std::string TileStream::BldgPath (const std::string &body, int x, int y) const
{
	int b = d->FindBody (body);
	return b < 0 || x < 0 || y < 0 ? std::string() : BldgFile (d->bodies[b]->dir, Key (14, x, y));
}

int TileStream::PrefetchVec (const std::string &body, int x, int y) // not upstream (design-preload)
{
	int b = d->FindBody (body);
	return b < 0 ? -1 : d->PrefetchVec (b, x, y);
}

void TileStream::SetCamera (const std::string &body, double lat, double lng) // not upstream (design-preload)
{
	int b = d->FindBody (body);
	std::lock_guard<std::mutex> lk (d->mx);
	d->camBody = b;
	d->camLat = lat;
	d->camLng = lng;
}

int TileStream::WantWay (const std::string &body, int x, int y) // not upstream (design-ways)
{
	int b = d->FindBody (body);
	return b < 0 ? -1 : d->WantVec (1, b, x, y);
}

std::string TileStream::WayPath (const std::string &body, int x, int y) const
{
	int b = d->FindBody (body);
	return b < 0 || x < 0 || y < 0 ? std::string() : VecFile (d->bodies[b]->dir, 1, Key (14, x, y));
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
	if (!net || L < 4 || L > B.cfg.SurfMax() || ilat < 0 || ilng < 0 || ilat >= 1 << (L - 4) || ilng >= 1 << (L - 3) || !B.cfg.Covers (L, ilat, ilng)) return -1;
	uint64_t k = Key (L, ilat, ilng), id = Id (b, k);
	auto count = [this] { int n = 0; for (auto &p : jobs) n += p.second.pre; return n; };
	if (B.Arch (A_SURF, k)) return 1; // the installed archive's
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
	}, -1, 0, sec ? sec * 1000 : TsNet::LongMs); // a wait given up stops its download too
	Clock::time_point until = Clock::now() + std::chrono::seconds (sec);
	std::unique_lock<std::mutex> l (w->m);
	while (!w->done && !quit && (!sec || Clock::now() < until)) w->c.wait_for (l, std::chrono::milliseconds (200));
	return w->done && w->st == 200 ? w->body : QByteArray();
}

// a cloud source image: kept in TileStream/src once its header reads; a bad answer is asked again the next day
QByteArray TileStream::Impl::Source (Body &B, const std::string &url, const char *file, const char *fmt, int sec)
{
	if (!QImageReader::supportedImageFormats().contains (fmt)) {
		Log (std::string ("TileStream: no ") + fmt + " reader, " + file + " is not fetched");
		return QByteArray();
	}
	std::string p = B.dir + "/TileStream/src/" + file, stamp = p + ".day";
	std::error_code ec;
	QFile f (QString::fromStdString (p));
	if (f.open (QIODevice::ReadOnly)) {
		QByteArray d = f.readAll();
		f.close();
		if (ImageOk (d, fmt)) return d;
		fs::remove (p, ec);
		Log (std::string ("TileStream: the cached ") + file + " is not an image, fetched again");
	}
	long long day = -1;
	std::ifstream (stamp) >> day;
	if (day == Today()) return QByteArray(); // a bad answer today already
	QByteArray d = Fetch (url, sec);
	if (d.isEmpty()) return d;
	fs::create_directories (B.dir + "/TileStream/src", ec);
	if (!ImageOk (d, fmt)) {
		Log ("TileStream: " + url + " did not answer with an image");
		std::ofstream (stamp, std::ios::trunc) << Today() << '\n';
		return QByteArray();
	}
	fs::remove (stamp, ec);
	Write (B, p, d.toStdString());
	return d;
}

// Cloud L1-L5 now from the small image when L1-L4 are missing; the large image's levels on a worker
void TileStream::Impl::Clouds (Body &B)
{
	if (B.cfg.cloudSmall.empty()) return;
	{
		std::ifstream f (Idx (B, "cloud"));
		std::string line;
		size_t lines = 0;
		while (std::getline (f, line)) {
			if (line.find_first_not_of (" \t\r") == std::string::npos) continue;
			lines++;
			int L, a, c;
			long long t;
			if (ParseLine (line, false, L, a, c, t) && KeyOk ("cloud", L, a, c, 1, B.cfg.cloudMax)) B.cloud.insert (Key (L, a, c));
		}
		bool held = false;
		for (auto it = B.cloud.begin(); it != B.cloud.end(); ) { // the installed archive's clouds go first
			std::error_code ec;
			if (B.Arch (A_CLOUD, *it)) {
				if (Ours (B, Tile (B.dir, "Cloud", *it))) fs::remove (Tile (B.dir, "Cloud", *it), ec); // design dropin-addons
				it = B.cloud.erase (it);
				held = true;
			} else ++it;
		}
		if (held) Rewrite (B, "cloud", B.cloud);
		else if (lines != B.cloud.size()) B.unclean.insert ("cloud");
	}
	auto missing = [&B] (int l0, int l1) {
		for (int L = l0; L <= l1; L++)
			for (int a = 0; a < (L < 4 ? 1 : 1 << (L - 4)); a++)
				for (int c = 0; c < (L < 4 ? 1 : 1 << (L - 3)); c++) {
					std::error_code ec;
					uint64_t k = Key (L, a, c);
					if (!B.Arch (A_CLOUD, k) && !fs::exists (Tile (B.dir, "Cloud", k), ec)) return true;
				}
		return false;
	};
	if (missing (1, 4)) {
		QImage im;
		if (im.loadFromData (Source (B, B.cfg.cloudSmall, "cloud_small.jpg", "jpg", opt.cloudWait)) && !im.isNull()) BakeClouds (B, im, 1, 5);
		else Log ("TileStream: no cloud image for " + B.cfg.name);
	}
	if (B.cfg.cloudMax < 6 || !missing (6, B.cfg.cloudMax)) return;
	Body *pb = &B;
	work.push_back ({[this, pb] {
		QImage im;
		if (!im.loadFromData (Source (*pb, pb->cfg.cloudBig, "cloud_large.tif", "tif", 0)) || im.isNull()) {
			Log ("TileStream: the large cloud image did not load, upper cloud levels from the small one");
			im.loadFromData (Source (*pb, pb->cfg.cloudSmall, "cloud_small.jpg", "jpg", 30));
		}
		if (!im.isNull() && !quit) BakeClouds (*pb, im, 6, pb->cfg.cloudMax);
	}, nullptr});
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
				if (B.Arch (A_CLOUD, k)) continue;
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

// elev-consistency: exactly n integers on a line
bool ParseInts (const std::string &line, long long *v, int n)
{
	const char *p = line.c_str();
	for (int i = 0; i < n; i++) {
		while (*p == ' ' || *p == '\t') p++;
		char *e;
		errno = 0;
		v[i] = strtoll (p, &e, 10);
		if (e == p || errno) return false;
		p = e;
	}
	while (*p == ' ' || *p == '\t' || *p == '\r') p++;
	return !*p;
}

bool KeyIn (long long L, long long a, long long b, int lmin)
{
	return L >= lmin && L <= 21 && a >= 0 && b >= 0 && a <= INT_MAX && b <= INT_MAX && InGrid ((int)L, (int)a, (int)b);
}
}

// the body's bases, their known heights, and Elev_mod for Elev tiles already on disk (before the client loads them)
void TileStream::Impl::LoadBases (Body &B)
{
	if (opt.radius) B.radius = opt.radius (B.cfg.name);
	if (B.cfg.elev.kind.empty()) return;
	std::error_code ec;
	if (opt.baseDir) { // elev-consistency: the Elev_mod scan below runs without bases too
		std::string dir = opt.baseDir (B.cfg.name);
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
	}
	{
		std::ifstream f (Idx (B, "bases"));
		std::string n;
		double h;
		while (f >> n >> h)
			for (size_t i = 0; i < B.bases.size(); i++)
				if (B.bases[i].name == n) B.baseH[i] = h;
		std::ifstream m (Idx (B, "mod"));
		std::string line;
		size_t lines = 0;
		while (std::getline (m, line)) {
			if (line.find_first_not_of (" \t\r") == std::string::npos) continue;
			lines++;
			int L, a, c;
			long long t;
			if (ParseLine (line, false, L, a, c, t) && KeyOk ("mod", L, a, c, 4, std::max (4, B.cfg.elev.lmax))) B.mod.insert (Key (L, a, c));
		}
		if (lines != B.mod.size()) B.unclean.insert ("mod");
	}
	for (size_t i = 0; i < B.bases.size(); i++) {
		const TsBase &b = B.bases[i];
		double lng = b.lng * 180.0 / M_PI, lat = b.lat * 180.0 / M_PI;
		for (int L = B.cfg.elev.lmax; std::isnan (B.baseH[i]) && L >= 14; L--) { // a fine Elev tile at the base point
			double span = 180.0 / (1 << (L - 4));
			uint64_t k = Key (L, std::clamp ((int)((90.0 - lat) / span), 0, (1 << (L - 4)) - 1), std::clamp ((int)((lng + 180.0) / span), 0, (1 << (L - 3)) - 1));
			double h = TsSky::ElvAt (ReadAll (Tile (B.dir, "Elev", k)), b.lng, b.lat);
			if (!std::isnan (h)) {
				B.baseH[i] = std::max (TsImg::FloorAt (B.cfg.floor, lng, lat), h);
				std::ofstream (Idx (B, "bases"), std::ios::app) << b.name << ' ' << B.baseH[i] << '\n';
			}
		}
	}
	AddonMods (B); // elev-consistency: before the bases' tiles, which then follow it
	int made = 0;
	for (size_t i = 0; i < B.bases.size(); i++) {
		const TsBase &b = B.bases[i];
		double lng = b.lng * 180.0 / M_PI, lat = b.lat * 180.0 / M_PI;
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
		BaseFlat (B, (size_t)J.baseIdx[j], std::round (std::max (TsImg::FloorAt (B.cfg.floor, lng, lat), h) / B.cfg.elevScale) * B.cfg.elevScale);
	}
}

// a base's flat height, once, kept in bases.idx
void TileStream::Impl::BaseFlat (Body &B, size_t i, double h)
{
	std::lock_guard<std::mutex> lk (mx);
	double &H = B.baseH[i];
	if (!std::isnan (H)) return;
	H = h;
	std::ofstream (Idx (B, "bases"), std::ios::app) << B.bases[i].name << ' ' << H << '\n';
	Log ("TileStream: base " + B.bases[i].name + " flat at " + std::to_string (H) + " m");
}

// not upstream (mars-stream): a cog body's Elev from its file, and the flat heights of the bases on it from the file at their centres
bool TileStream::Impl::CogElev (Body &B, const Job &J, std::string &elv)
{
	const TsBodyCfg &C = B.cfg;
	auto fetch = [this] (std::vector<TsCog::Req> &r) { FetchAll (r, USGS); };
	int L = KL (J.key), la = KLat (J.key), ln = KLng (J.key);
	double w, n, dw, dh;
	TsImg::Bounds (L, la, ln, w, n, dw, dh);
	TsCog::Pts p {(n - dh) - dh / 256.0, w - dw / 256.0, dh / 256.0, dw / 256.0, 259, 259, dh / 256.0}; // the .elv nodes, rows from the south
	std::vector<float> h;
	std::vector<uint8_t> v;
	if (!TsCog::Sample (C.elev.url, p, fetch, h, v)) return false;
	elv = TsImg::ElevGrid (L, la, ln, h, v, C.elevScale);
	if (elv.empty() || L < ModLevel) return true;
	for (size_t i = 0; i < B.bases.size(); i++) {
		{
			std::lock_guard<std::mutex> lk (mx);
			if (!std::isnan (B.baseH[i])) continue;
		}
		if (!TsSky::Hits (B.bases[i], B.radius, L, la, ln, Blend)) continue;
		TsCog::Pts b {B.bases[i].lat * 180.0 / M_PI, B.bases[i].lng * 180.0 / M_PI, 0.0, 0.0, 1, 1, 0.0};
		std::vector<float> bh;
		std::vector<uint8_t> bv;
		if (!TsCog::Sample (C.elev.url, b, fetch, bh, bv)) return false;
		if (!bv.empty() && bv[0]) BaseFlat (B, i, std::round (bh[0] / C.elevScale) * C.elevScale);
	}
	return true;
}

// the Elev_mod of tile k over elv, written before elv; false while a base it touches has no height yet
bool TileStream::Impl::Mods (Body &B, uint64_t k, const std::string &elv)
{
	std::lock_guard<std::mutex> ml (modMx);
	return ModsAt (B, k, elv, nullptr);
}

// with modMx held. elev-consistency: someone else's Elev_mod at k is left alone; under a source, k's is the source's carried down
// (none if it modifies nothing here), never the bases' flattening; else the bases' as before. A file of ours no longer needed goes
bool TileStream::Impl::ModsAt (Body &B, uint64_t k, const std::string &elv, bool *changed)
{
	if (changed) *changed = false;
	if (!B.modsKnown) return true; // no startup scan (a region download): the next session's pass makes them
	int L = KL (k);
	std::string path = Tile (B.dir, "Elev_mod", k);
	std::error_code ec;
	bool ours;
	{
		std::lock_guard<std::mutex> lk (mx);
		ours = B.mod.count (k) > 0;
	}
	if (ours && !Ours (B, path)) { // design dropin-addons: an add-on's Elev_mod over ours
		std::lock_guard<std::mutex> lk (mx);
		Disown (B, "mod", k, path);
		ours = false;
	}
	if (B.addonMod.count (k) || (!ours && fs::exists (path, ec))) { // someone else's Elev_mod: left alone
		SetInh (B, k, 0, false);
		return true;
	}
	std::string m;
	uint64_t s = ModSource (B, k);
	if (s) {
		std::string src = ReadAll (Tile (B.dir, "Elev_mod", s));
		if (TsSky::ModOk (src)) m = TsSky::InheritMod (L, KLat (k), KLng (k), KL (s), KLat (s), KLng (s), src, elv, B.cfg.elevScale);
		else if (!src.empty() && B.badSrc.insert (s).second) Log ("TileStream: " + Tile (B.dir, "Elev_mod", s) + " can't be read, the Elev under it stays unmodified");
	} else if (L >= ModLevel && !B.bases.empty()) {
		std::vector<std::pair<const TsBase *, double>> hit;
		{
			std::lock_guard<std::mutex> lk (mx);
			for (size_t i = 0; i < B.bases.size(); i++) {
				if (!TsSky::Hits (B.bases[i], B.radius, L, KLat (k), KLng (k), Blend)) continue;
				if (std::isnan (B.baseH[i])) return false;
				hit.push_back ({&B.bases[i], B.baseH[i]});
			}
		}
		if (!hit.empty()) m = TsSky::ElevMod (L, KLat (k), KLng (k), hit, B.radius, B.cfg.elevScale, elv, Blend);
	}
	bool ch = false;
	if (m.empty()) {
		if (ours) {
			ch = fs::remove (path, ec);
			fs::remove (path + ".part", ec);
			std::lock_guard<std::mutex> lk (mx);
			B.mod.erase (k);
			Rewrite (B, "mod", B.mod);
		}
		if (ch) tsElevGen.fetch_add (1, std::memory_order_release);
	} else if (m != ReadAll (path)) {
		if (!Write (B, path, m)) return false;
		ch = true;
		if (!ours) {
			std::lock_guard<std::mutex> lk (mx);
			if (B.mod.insert (k).second) Append (B, "mod", k, -1);
		}
	}
	SetInh (B, k, s, !m.empty());
	if (changed) *changed = ch;
	return true;
}

// elev-consistency, with modMx held: the installed Elev archive has tile k (the stream may hide it); its table of contents is walked once
bool TileStream::Impl::ArchElev (Body &B, uint64_t k)
{
	if (!B.elevArchRead) {
		B.elevArchRead = true;
		Toc t;
		if (ReadToc (B.dir + "/Archive/Elev.tree", t)) {
			std::vector<std::pair<uint32_t, uint64_t>> st;
			std::vector<bool> seen (t.node.size());
			for (int b = 0; b < 2; b++)
				if (t.root[3 + b] < t.node.size()) st.push_back ({t.root[3 + b], Key (4, 0, b)});
			while (!st.empty()) {
				auto [i, n] = st.back();
				st.pop_back();
				if (seen[i]) continue; // a damaged archive's loops
				seen[i] = true;
				if (t.node[i].size) B.elevArch.push_back (n);
				int L = KL (n);
				if (L >= 21) continue;
				for (int q = 0; q < 4; q++) {
					uint32_t c = t.node[i].child[q];
					if (c < t.node.size()) st.push_back ({c, Key (L + 1, KLat (n) * 2 + (q >> 1), KLng (n) * 2 + (q & 1))});
				}
			}
			std::sort (B.elevArch.begin(), B.elevArch.end());
		}
	}
	return std::binary_search (B.elevArch.begin(), B.elevArch.end(), k);
}

// elev-consistency, with modMx held: Elev the core and the client read at k, a loose file or an archive tile no Elev of ours hides (elevmgr's StreamCovers)
bool TileStream::Impl::ElevData (Body &B, uint64_t k)
{
	std::error_code ec;
	if (fs::exists (Tile (B.dir, "Elev", k), ec)) return true;
	int L = KL (k);
	if (!ArchElev (B, k)) return false;
	int d = L > B.cfg.elev.lmax ? L - B.cfg.elev.lmax : 1;
	return L <= 4 || !fs::exists (Tile (B.dir, "Elev", Key (L - d, KLat (k) >> d, KLng (k) >> d)), ec);
}

// elev-consistency, with modMx held: the Elev_mod tile k is drawn under; 0 for none. An add-on was made with Elev wherever the archive
// has it, and at the levels of its own tiles under its nearest tile above k: there it says itself how k looks. Below them, the first
// coarser level with Elev data (a level without it never applies its Elev_mod): an add-on's there, or ours when that key is under one
uint64_t TileStream::Impl::ModSource (Body &B, uint64_t k)
{
	int L = KL (k);
	if (ArchElev (B, k)) return 0;
	for (int d = 1; L - d >= 4; d++) {
		auto it = B.addonLevels.find (Key (L - d, KLat (k) >> d, KLng (k) >> d));
		if (it == B.addonLevels.end()) continue;
		if (it->second >> L & 1) return 0;
		break;
	}
	for (int d = 1; L - d >= 4; d++) {
		uint64_t p = Key (L - d, KLat (k) >> d, KLng (k) >> d);
		if (!ElevData (B, p)) continue;
		return B.addonMod.count (p) || B.inh.count (p) ? p : 0;
	}
	return 0;
}

TileStream::Impl::Body::Inh TileStream::Impl::Stamp (const Body &B, uint64_t src, bool has) const
{
	Body::Inh n;
	n.src = src;
	n.has = has;
	if (!src) return n;
	std::error_code ec;
	std::string f = Tile (B.dir, "Elev_mod", src);
	auto t = fs::last_write_time (f, ec);
	if (ec) return n;
	uint64_t sz = fs::file_size (f, ec);
	if (ec) return n;
	n.mtime = (int64_t)t.time_since_epoch().count();
	n.size = sz;
	return n;
}

// elev-consistency, with modMx held: k's source, and its inh.idx line; a line with no source ends one
void TileStream::Impl::SetInh (Body &B, uint64_t k, uint64_t src, bool has)
{
	auto it = B.inh.find (k);
	if (!src) {
		if (it == B.inh.end()) return;
		B.inh.erase (it);
		InhLine (B, k, Body::Inh());
		return;
	}
	Body::Inh n = Stamp (B, src, has);
	if (it != B.inh.end() && it->second == n) return;
	B.inh[k] = n;
	InhLine (B, k, n);
}

void TileStream::Impl::InhLine (Body &B, uint64_t k, const Body::Inh &n)
{
	B.inhAppends++;
	if (ro) return;
	std::ofstream f (Idx (B, "inh"), std::ios::app);
	f << KL (k) << ' ' << KLat (k) << ' ' << KLng (k) << ' ' << KL (n.src) << ' ' << KLat (n.src) << ' ' << KLng (n.src) << ' ' << n.mtime << ' ' << n.size << ' ' << (n.has ? 1 : 0) << '\n';
}

// elev-consistency, at startup before the workers: the add-ons' Elev_mod files, inh.idx, and our Elev whose source moved, changed or
// lost our file made again, coarse to fine so a source of ours is current before the tiles under it read it
void TileStream::Impl::AddonMods (Body &B)
{
	std::lock_guard<std::mutex> ml (modMx);
	std::unordered_set<uint64_t> mod;
	std::vector<uint64_t> keys;
	{
		std::lock_guard<std::mutex> lk (mx);
		mod = B.mod;
		keys.assign (B.elev.begin(), B.elev.end());
	}
	std::error_code ec;
	std::string top = B.dir + "/Elev_mod";
	for (fs::recursive_directory_iterator it (top, ec), end; !ec && it != end; it.increment (ec)) {
		std::error_code fe;
		if (it.depth() != 2 || it->path().extension() != ".elv" || !it->is_regular_file (fe)) continue;
		const fs::path &p = it->path();
		long long v[3];
		std::string name = p.parent_path().parent_path().filename().string() + ' ' + p.parent_path().filename().string() + ' ' + p.stem().string();
		if (!ParseInts (name, v, 3) || !KeyIn (v[0], v[1], v[2], 4)) continue;
		uint64_t k = Key ((int)v[0], (int)v[1], (int)v[2]);
		if (mod.count (k) && !Ours (B, p.string())) { // design dropin-addons: an add-on's Elev_mod over ours
			std::lock_guard<std::mutex> lk (mx);
			Disown (B, "mod", k, p.string());
			mod.erase (k);
		}
		if (!mod.count (k)) B.addonMod.insert (k);
	}
	for (uint64_t a : B.addonMod) {
		B.addonLevels.emplace (a, 0u);
		int L = KL (a);
		for (int d = 1; L - d >= 4; d++) {
			uint64_t p = Key (L - d, KLat (a) >> d, KLng (a) >> d);
			if (B.addonMod.count (p)) B.addonLevels[p] |= 1u << L;
		}
	}
	B.inh.clear();
	size_t lines = 0;
	{
		std::ifstream f (Idx (B, "inh"));
		std::string line;
		while (std::getline (f, line)) {
			if (line.find_first_not_of (" \t\r") == std::string::npos) continue;
			lines++;
			long long v[9];
			if (!ParseInts (line, v, 9) || !KeyIn (v[0], v[1], v[2], 5)) continue;
			uint64_t k = Key ((int)v[0], (int)v[1], (int)v[2]);
			if (!v[3] && !v[4] && !v[5]) {
				B.inh.erase (k);
				continue;
			}
			int d = (int)(v[0] - v[3]);
			if (!KeyIn (v[3], v[4], v[5], 4) || d < 1 || v[4] != v[1] >> d || v[5] != v[2] >> d || v[7] < 0 || (v[8] != 0 && v[8] != 1)) continue;
			Body::Inh n;
			n.src = Key ((int)v[3], (int)v[4], (int)v[5]);
			n.mtime = v[6];
			n.size = (uint64_t)v[7];
			n.has = v[8] == 1;
			B.inh[k] = n;
		}
	}
	B.modsKnown = true;
	size_t appends = B.inhAppends, carried = 0, redone = 0;
	std::sort (keys.begin(), keys.end()); // Key: the level first
	for (uint64_t k : keys) {
		std::string ep = Tile (B.dir, "Elev", k), mp = Tile (B.dir, "Elev_mod", k);
		if (!fs::exists (ep, ec)) continue; // Prune drops it
		uint64_t s = ModSource (B, k);
		auto it = B.inh.find (k);
		Body::Inh rec = it != B.inh.end() ? it->second : Body::Inh();
		if (!s && !rec.src) continue; // nothing of ours to undo; the bases' tiles follow
		if (s && rec == Stamp (B, s, rec.has) && rec.has == fs::exists (mp, ec)) continue;
		bool ch = false;
		if (!ModsAt (B, k, ReadAll (ep), &ch) || !ch) continue;
		if (B.inh.count (k)) carried++;
		else redone++;
	}
	if (B.inhAppends != appends || lines != B.inh.size()) {
		std::string text;
		for (auto &p : B.inh) {
			const Body::Inh &n = p.second;
			text += std::to_string (KL (p.first)) + ' ' + std::to_string (KLat (p.first)) + ' ' + std::to_string (KLng (p.first)) + ' '
				+ std::to_string (KL (n.src)) + ' ' + std::to_string (KLat (n.src)) + ' ' + std::to_string (KLng (n.src)) + ' '
				+ std::to_string (n.mtime) + ' ' + std::to_string (n.size) + ' ' + (n.has ? "1" : "0") + '\n';
		}
		WriteText (Idx (B, "inh"), text);
	}
	Log ("TileStream: " + std::to_string (B.addonMod.size()) + " add-on Elev_mod tiles for " + B.cfg.name + ", " + std::to_string (carried) + " Elev_mod tiles carried from them, " + std::to_string (redone) + " redone without them");
}

// elev-consistency: after k's Elev is written or removed: k again when its source moved, then the stream's Elev among its children
// when their source moved or the file they carry down changed, and theirs in turn
void TileStream::Impl::Settle (Body &B, uint64_t k)
{
	if (!B.modsKnown) return;
	std::lock_guard<std::mutex> ml (modMx);
	std::error_code ec;
	auto it = B.inh.find (k);
	if (fs::exists (Tile (B.dir, "Elev", k), ec) && ModSource (B, k) != (it != B.inh.end() ? it->second.src : 0)) {
		std::string e = ReadAll (Tile (B.dir, "Elev", k));
		if (!e.empty()) ModsAt (B, k, e, nullptr);
	}
	std::vector<uint64_t> todo {k}; // the walk of a tile under one of these may now stop at it, or find it under a source
	while (!todo.empty()) {
		uint64_t p = todo.back();
		todo.pop_back();
		int L = KL (p);
		if (L >= std::min (21, B.cfg.elev.lmax)) continue;
		for (int q = 0; q < 4; q++) {
			uint64_t c = Key (L + 1, KLat (p) * 2 + (q >> 1), KLng (p) * 2 + (q & 1));
			{
				std::lock_guard<std::mutex> lk (mx);
				if (!B.elev.count (c)) continue;
			}
			auto ci = B.inh.find (c);
			uint64_t s = ModSource (B, c), r = ci != B.inh.end() ? ci->second.src : 0;
			if (s == r && (!s || ci->second == Stamp (B, s, ci->second.has))) continue;
			std::string ce = ReadAll (Tile (B.dir, "Elev", c));
			if (!ce.empty() && ModsAt (B, c, ce, nullptr)) todo.push_back (c);
		}
	}
}

// not upstream: Launchpad cache controls and region prefetch (TabStream.cpp)

#include <cmath>

namespace {

// idx: written, none, ocean, refetch, elev, bldg, mod or cloud, its lines checked as the stream checks them
std::vector<uint64_t> ReadIdx (const std::string &dir, const std::string &idx)
{
	std::vector<uint64_t> v;
	std::ifstream f (dir + "/TileStream/" + idx + ".idx");
	std::string line;
	bool extra = idx == "written" || idx == "none";
	while (std::getline (f, line)) {
		int L, a, b;
		long long t;
		if (ParseLine (line, extra, L, a, b, t) && KeyOk (idx, L, a, b)) v.push_back (Key (L, a, b));
	}
	std::sort (v.begin(), v.end());
	v.erase (std::unique (v.begin(), v.end()), v.end());
	return v;
}

// the Elev files the stream wrote: elev.idx, or before it the Elev of every key in written.idx
std::vector<uint64_t> ElevIdx (const std::string &dir)
{
	std::error_code ec;
	return ReadIdx (dir, fs::exists (dir + "/TileStream/elev.idx", ec) ? "elev" : "written");
}

// design dropin-addons: the mark on every file the indexes list, then TileStream/marked
int MarkAll (const std::string &dir)
{
	std::string ts = dir + "/TileStream/", probe = ts + "tmp/mark.probe";
	std::error_code ec;
	fs::create_directories (ts + "tmp", ec);
	std::ofstream (probe, std::ios::trunc) << '\n';
	MarkFile (probe);
	struct stat st;
	bool ok = !stat (probe.c_str(), &st) && st.st_mtim.tv_nsec == OwnNs;
	fs::remove (probe, ec);
	if (!ok) return -1;
	for (uint64_t k : ReadIdx (dir, "written"))
		for (const char *layer : {"Surf", "Mask", "Class"}) MarkFile (Tile (dir, layer, k));
	for (uint64_t k : ElevIdx (dir)) MarkFile (Tile (dir, "Elev", k));
	for (const char *idx : {"bldg", "way", "mod", "cloud"}) // not upstream (design-ways): way
		for (uint64_t k : ReadIdx (dir, idx)) MarkFile (StreamFile (dir, idx, k));
	std::ofstream f (ts + "marked", std::ios::trunc);
	f << "the stream's files carry its mark (design dropin-addons)\n";
	f.close();
	return f.fail() ? -2 : 0;
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

// a file and its leftover .part (a write across disks cut short)
uint64_t FileBytes (const std::string &p)
{
	uint64_t n = 0;
	for (const std::string &f : {p, p + ".part"}) {
		std::error_code ec;
		uint64_t s = fs::file_size (f, ec);
		if (!ec) n += s;
	}
	return n;
}

// Surf, Mask and Class of a streamed tile; Elev only when the stream wrote it
uint64_t TileBytes (const std::string &dir, uint64_t k, bool elev = true)
{
	uint64_t n = 0;
	for (const char *layer : {"Surf", "Mask", "Elev", "Class"})
		if (elev || strcmp (layer, "Elev")) n += FileBytes (Tile (dir, layer, k));
	return n;
}

// a file, its .part and its row folder when that is left empty; true when the file was there
bool RemoveFile (const std::string &p)
{
	std::error_code ec;
	bool had = fs::remove (p, ec);
	fs::remove (p + ".part", ec);
	fs::remove (fs::path (p).parent_path(), ec);
	return had;
}

}

uint64_t TileStream::CacheBytes (const std::string &bodyDir, uint64_t *total, size_t *tiles)
{
	std::string dir = CleanDir (bodyDir);
	uint64_t n = 0;
	size_t t = 0;
	std::vector<uint64_t> w = ReadIdx (dir, "written"), el = ElevIdx (dir);
	for (uint64_t k : w) {
		uint64_t b = TileBytes (dir, k, std::binary_search (el.begin(), el.end(), k));
		if (b) { n += b; t++; }
	}
	for (uint64_t k : el) { // Elev of ours whose Surf is no longer listed
		if (std::binary_search (w.begin(), w.end(), k)) continue;
		uint64_t b = FileBytes (Tile (dir, "Elev", k));
		if (b) { n += b; t++; }
	}
	for (const char *idx : {"bldg", "way", "mod", "cloud"}) // not upstream (design-ways): way
		for (uint64_t k : ReadIdx (dir, idx)) {
			uint64_t s = FileBytes (StreamFile (dir, idx, k));
			if (s) { n += s; t++; }
		}
	if (tiles) *tiles = t;
	if (total) {
		*total = 0;
		for (const char *layer : {"/Surf", "/Mask", "/Elev", "/Class", "/Bldg", "/Way", "/Cloud", "/Elev_mod"}) { // not upstream (design-ways): Way
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
size_t TileStream::Clear (const std::string &bodyDir, bool keepBase, bool *refused)
{
	std::string dir = CleanDir (bodyDir), ts = dir + "/TileStream/";
	std::error_code ec;
	if (refused) *refused = false;
	if (!fs::is_directory (ts, ec)) return 0;
	std::string key = LockKey (dir);
	int lock = TakeLock (key, true); // no lock file to be had: cleared unless someone holds it
	if (!lock || (lock < 0 && Busy (dir))) { // a stream or another Orbiter has the folder: nothing removed
		if (refused) *refused = true;
		return 0;
	}
	std::vector<uint64_t> kept, w = ReadIdx (dir, "written"), el = ElevIdx (dir), elKept;
	size_t n = 0;
	bool marked = fs::exists (ts + "marked", ec); // design dropin-addons: an add-on's file over ours stays, its line goes
	auto rmOurs = [marked] (const std::string &p) { return OwnFile (p, marked) && RemoveFile (p); };
	bool had = fs::exists (ts + "written.idx", ec);
	for (uint64_t k : w) {
		if (keepBase && KL (k) <= 8) {
			kept.push_back (k);
			continue;
		}
		bool any = false;
		for (const char *layer : {"Surf", "Mask", "Elev", "Class"})
			if (strcmp (layer, "Elev") || std::binary_search (el.begin(), el.end(), k)) any |= rmOurs (Tile (dir, layer, k));
		if (any) n++;
	}
	if (had) WriteIdx (ts + "written.idx", kept);
	for (uint64_t k : el) { // Elev this stream wrote: with its tile above, alone here
		if (keepBase && KL (k) <= 8) elKept.push_back (k);
		else if (!std::binary_search (w.begin(), w.end(), k) && rmOurs (Tile (dir, "Elev", k))) n++;
	}
	WriteIdx (ts + "elev.idx", elKept);
	for (const char *idx : {"bldg", "way", "mod", "cloud"}) { // buildings, ways (not upstream, design-ways), Elev_mod and clouds this stream wrote
		if (!fs::exists (ts + idx + ".idx", ec)) continue;
		std::vector<uint64_t> keep;
		for (uint64_t k : ReadIdx (dir, idx)) {
			if (keepBase && KL (k) <= 8 && std::string (idx) != "bldg" && std::string (idx) != "way") { keep.push_back (k); continue; }
			if (rmOurs (StreamFile (dir, idx, k))) n++;
		}
		WriteIdx (ts + idx + ".idx", keep);
	}
	if (fs::exists (ts + "refetch.idx", ec)) {
		std::vector<uint64_t> r;
		for (uint64_t k : ReadIdx (dir, "refetch"))
			if (std::binary_search (kept.begin(), kept.end(), k)) r.push_back (k);
		WriteIdx (ts + "refetch.idx", r);
	}
	fs::remove (ts + "none.idx", ec);
	fs::remove (ts + "inh.idx", ec); // elev-consistency
	if (!keepBase) fs::remove (ts + "ocean.idx", ec);
	for (fs::directory_iterator it (ts + "tmp", ec), end; !ec && it != end; it.increment (ec)) {
		std::error_code rc;
		fs::remove (it->path(), rc);
	}
	if (lock > 0) DropLock (key);
	return n;
}

bool TileStream::Busy (const std::string &bodyDir)
{
	std::string key = LockKey (CleanDir (bodyDir)), file = key + "/TileStream/lock";
	std::lock_guard<std::mutex> lk (lockMx);
	if (locks.count (key)) return true; // a stream or a Clear of this process
	int fd = open (file.c_str(), O_RDWR | O_CLOEXEC); // NFS takes an exclusive flock only on a file open for writing
	if (fd < 0) fd = open (file.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) return false; // never locked
	bool busy = TryFlock (fd) && errno == EWOULDBLOCK; // any other error: no working locks, nobody can hold it
	close (fd);
	return busy;
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
	try {
		return PrefetchKeys (opt, body, keys, p, cancel, stallSec);
	} catch (const std::exception &e) {
		if (opt.log) opt.log (std::string ("TileStream: the region download failed: ") + e.what());
	} catch (...) {
		if (opt.log) opt.log ("TileStream: the region download failed");
	}
	return 5;
}

int TileStream::PrefetchKeys (Options opt, const std::string &body, const std::vector<TileKey> &keys, Progress &p, const std::atomic<bool> &cancel, double stallSec)
{
	opt.offline = false;
	opt.staleSec = 1e9; // a prefetch want never goes stale
	opt.sky = false;    // clouds and Elev_mod are the session's
	if (std::find (opt.bodies.begin(), opt.bodies.end(), body) == opt.bodies.end()) opt.bodies.push_back (body);
	std::string dir = opt.bodyDir ? CleanDir (opt.bodyDir (body)) : std::string();
	p.total = (int)keys.size();
	if (!dir.empty() && Busy (dir)) return 4;
	TileStream ts (opt);
	if (!ts.Enabled (body)) return 3;
	if (ts.d->ro) return 4;
	int b = ts.d->FindBody (body);
	const Impl::Body &B = *ts.d->bodies[b];
	std::vector<size_t> open;
	size_t next = 0, pending = 0;
	Clock::time_point last = Clock::now();
	auto answer = [&] (size_t i, int r) {
		const TileKey &k = keys[i];
		if (r > 0) {
			p.have++;
			p.bytes += TileBytes (dir, Key (k.L, k.ilat, k.ilng));
		} else if (r == -2) p.failed++;
		else p.none++;
		p.done++;
		last = Clock::now();
	};
	auto want = [&] (const TileKey &k) { // -2: given up on after its tries (held)
		uint64_t key = Key (k.L, k.ilat, k.ilng);
		if (InGrid (k.L, k.ilat, k.ilng) && B.Arch (A_SURF, key)) { // in the installed archive: only its Elev is ours
			ts.Want (body, k.L, k.ilat, k.ilng, 4);
			return 1;
		}
		int r = ts.Want (body, k.L, k.ilat, k.ilng, 0);
		return !r && ts.d->Held (b, key) ? -2 : r;
	};
	for (;;) {
		if (cancel) return 1;
		for (size_t j = 0; j < open.size(); ) {
			const TileKey &k = keys[open[j]];
			int r = want (k);
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
			int r = want (k);
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
		if (next == keys.size() && open.empty() && (!pn || stalled)) break; // an Elev that never comes doesn't fail the tiles
		if (stalled) return 2;
		std::this_thread::sleep_for (std::chrono::milliseconds (100));
	}
	// not upstream (design-preload): then the z14 building and road tiles under the finest tiles
	if ((!opt.buildings && !opt.ways) || B.cfg.bldgUrl.empty()) return 0;
	int lf = 0;
	for (const TileKey &k : keys) lf = std::max (lf, k.L);
	if (lf < 12) return 0; // a coarse region: no buildings or roads to show
	std::vector<std::pair<int, int>> vk;
	std::unordered_set<uint64_t> vseen;
	for (const TileKey &k : keys) {
		if (k.L != lf || k.L < 4) continue;
		double span = M_PI / (1 << (k.L - 4)), la1 = M_PI / 2 - k.ilat * span, la0 = la1 - span, lo0 = k.ilng * span - M_PI, lo1 = lo0 + span;
		int x0, y0, x1, y1;
		TsBldg::TileOf (la1, lo0, 14, x0, y0);
		TsBldg::TileOf (la0, lo1, 14, x1, y1);
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++)
				if (vseen.insert ((uint64_t)x << 32 | (uint32_t)y).second) vk.push_back ({x, y});
		if (vk.size() > 20000) { // over ~100,000 km2: left to the flight
			if (opt.log) opt.log ("TileStream: the region is too large for its building and road tiles, they come in flight");
			return 0;
		}
	}
	p.total += (int)vk.size();
	auto vwant = [&] (const std::pair<int, int> &t) { // 1 there, -1 none, -2 given up on, 0 coming
		int a = opt.buildings ? ts.WantBldg (body, t.first, t.second) : -1, w = opt.ways ? ts.WantWay (body, t.first, t.second) : -1;
		if (a && w) return a > 0 || w > 0 ? 1 : -1;
		std::lock_guard<std::mutex> lk (ts.d->mx);
		return ts.d->bhold.count (Id (b, Key (14, t.first, t.second))) ? -2 : 0;
	};
	auto vanswer = [&] (int r) {
		if (r > 0) p.have++;
		else if (r == -2) p.failed++;
		else p.none++;
		p.done++;
		last = Clock::now();
	};
	std::vector<size_t> vo;
	size_t vn = 0;
	last = Clock::now();
	for (;;) {
		if (cancel) return 1;
		for (size_t j = 0; j < vo.size(); ) {
			int r = vwant (vk[vo[j]]);
			if (r) {
				vanswer (r);
				vo[j] = vo.back();
				vo.pop_back();
			} else j++;
		}
		while (vn < vk.size() && vo.size() < 16) {
			int r = vwant (vk[vn]);
			if (r) vanswer (r);
			else vo.push_back (vn);
			vn++;
		}
		if (vn == vk.size() && vo.empty()) return 0;
		if (std::chrono::duration<double> (Clock::now() - last).count() > stallSec) return 2;
		std::this_thread::sleep_for (std::chrono::milliseconds (100));
	}
}
