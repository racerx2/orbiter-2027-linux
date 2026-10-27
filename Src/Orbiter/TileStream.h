// not upstream: fills a planet's tile cache (Textures/<Body>/Surf, Mask) from free imagery servers while the sim runs

#ifndef __TILESTREAM_H
#define __TILESTREAM_H

#include <array>
#include <atomic> // not upstream: Progress
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <QByteArray>
#include <QImage>

// one source: eox (EOX WGS84 256 px grid), gibs (NASA GIBS EPSG:4326 512 px grid) or terrarium (Web Mercator 256 px elevation)
struct TsSource {
	std::string kind;
	std::string url;  // {z} or {n}, {row}, {col}
	int lmin = 1;     // file levels served
	int lmax = 0;
	int nmax = 0;     // gibs: top grid level
};

struct TsBodyCfg {
	std::string name;
	TsSource surf, fallback, lights, water, elev;
	TsSource cover; // worldcover: land classes and the water bit from level lmin
	double elevScale = 0.5;    // metres per .elv step
	int black[3] = {0, 0, 0};  // night light background taken off
	double waterGrey = 101.5;  // water at or above this grey
	int fill[3] = {24, 44, 78}; // EOX open-ocean fill colour
	std::vector<std::string> credit;
	std::string cloudSmall, cloudBig; // not upstream (earth-sky): CLOUD equirect images, L1-L5 and up to cloudMax
	int cloudMax = 7;
	std::string bldgJson, bldgUrl; // TileJSON of the building tiles, last known {z}/{x}/{y} template
	struct Floor { double lng0, lat0, lng1, lat1, m; }; // FLOOR: degrees, the lowest metres inside; elsewhere 0
	std::vector<Floor> floor;
};

// not upstream (earth-sky): a surface base's footprint for Elev_mod flattening
struct TsBase {
	struct Part { double n0, e0, n1, e1, r; }; // capsule, metres north/east of the base point
	std::string name;
	double lng = 0.0, lat = 0.0; // radians
	std::vector<Part> parts;
	double ext = 0.0;            // reach of the furthest part, metres
};
bool TsReadBase (const std::string &file, TsBase &b);

namespace TsSky {
	QImage Cloud (const QImage &src, int L, int ilat, int ilng);  // RGBA white, alpha = density
	std::string Dds5 (const QImage &img);                          // DXT5, full mip chain
	double ElvAt (const std::string &elv, double lng, double lat);  // radians; NaN outside
	double TerrAt (const QImage &tile, int z, int row, int col, double lng, double lat); // degrees
	void TerrTile (double lng, double lat, int z, int &row, int &col);
	bool Hits (const TsBase &b, double R, int L, int ilat, int ilng, double blend);
	std::string ElevMod (int L, int ilat, int ilng, const std::vector<std::pair<const TsBase *, double>> &bases, double R, double scale, const std::string &elev, double blend);
	// elev-consistency: tile (L, ilat, ilng)'s Elev_mod from an add-on's Elev_mod of its ancestor (aL, ailat, ailng); "" if it modifies nothing there
	std::string InheritMod (int L, int ilat, int ilng, int aL, int ailat, int ailng, const std::string &amod, const std::string &elev, double scale);
	bool ModOk (const std::string &m); // elev-consistency: an Elev_mod file the core applies (grid, dtype, sizes)
}

namespace TsTrack {
	struct Pt { double lng, lat, t; };
	std::vector<Pt> Propagate (const double r0[3], const double v0[3], double mu, double omega, double horizon, int n);
	std::vector<std::array<int, 3>> Keys (const std::vector<Pt> &pts, int L, int half);
	int Level (double alt, int lmin, int lmax);
}

// 3D buildings from z14 Mapbox vector tiles (TileStreamMvt.cpp)
namespace TsBldg {
	struct Pt { double x, y; };
	struct Feature { std::vector<std::vector<Pt>> rings; double height = 0.0, minHeight = 0.0; bool hide = false; }; // tile units, y down
	struct Building { double lat, lng; float hmin, hmax; uint32_t v0, nv, i0, ni; uint32_t r0 = 0, nr = 0; }; // anchor in radians; r0, nr: its rings in rsz
	struct Mesh {
		int z = 0, x = 0, y = 0;
		std::vector<Building> b;
		std::vector<float> vtx;     // x east, y up, z north (m from the anchor), nx, ny, nz
		std::vector<uint32_t> idx;  // per building, from its v0
		std::vector<uint32_t> rsz;  // ring sizes, buildings in order, outer ring first; none from a version 1 file
		std::vector<float> pts;     // the rings' points: x east, z north (m from the anchor)
	};
	bool Inflate (const std::string &in, std::string &out);   // gzip or zlib
	bool Decode (const std::string &pbf, std::vector<Feature> &out, uint32_t &extent); // layer "building"
	bool Triangulate (const std::vector<std::vector<Pt>> &rings, std::vector<uint32_t> &tri); // ring 0 outer, then holes; indices into the rings in order
	Mesh Build (int z, int x, int y, const std::vector<Feature> &f, uint32_t extent);
	std::string Encode (const Mesh &m); // version 2 (footprints) when every building has its rings, else version 1 (walls)
	bool Read (const std::string &bytes, Mesh &m); // either version; the same vtx and idx
	void TileOf (double lat, double lng, int z, int &x, int &y); // radians
	void LatLng (int z, double x, double y, double &lat, double &lng); // tile coordinates to radians
}

// not upstream (design-ways): roads, railways, airports and waterways from the same z14 tiles (TileStreamMvt.cpp)
namespace TsWay {
	enum Kind : uint8_t {
		NONE, MOTORWAY, TRUNK, PRIMARY, SECONDARY, TERTIARY, MINOR, SERVICE, DRIVEWAY, PARKING, TRACK, BUSWAY, RACEWAY,
		FOOTWAY, CYCLEWAY, PATH, STEPS, PEDESTRIAN, PLATFORM, PIER, RAIL, TRAM,
		RUNWAY, TAXIWAY, APRON, HELIPAD, RIVER, CANAL, STREAM, DRAIN, DITCH, NKIND
	};
	enum Flag : uint8_t { BRIDGE = 1, UNPAVED = 2, ONEWAY = 4, RAMP = 8, AREA = 16 };
	const int Extent = 4096;     // tile units a side; the tile's buffer lies outside 0..Extent
	struct Feature {
		uint8_t kind = NONE, flags = 0;
		int8_t layer = 0;
		std::string ref;                         // aeroway ref ("13L/31R"), at most 15 bytes
		std::vector<std::vector<int16_t>> parts; // x, y pairs, y down; lines, or with AREA rings (positive area outer, then its holes)
	};
	struct Tile { int z = 14, x = 0, y = 0; std::vector<Feature> f; };
	bool Decode (const std::string &pbf, Tile &t); // layers transportation, aeroway, waterway; z, x, y as set
	std::string Encode (const Tile &t);           // "OWAY" version 1
	bool Read (const std::string &bytes, Tile &t);
}

bool TsReadCfg (const std::string &file, std::vector<TsBodyCfg> &bodies, std::string &err);
int TsCfgElev (const std::string &file, const std::string &body); // the last level of the body's ELEV source, 0 for none
int TsSessionElev (const std::string &body); // TileStreamApi.cpp: TsCfgElev when this session streams; Elev archive tiles under the stream's files are not read
unsigned TsElevGen (); // not upstream (elev-consistency): Elev and Elev_mod files the streams of this process wrote or removed so far; a new value: heights may have changed, a missing Elev may be there now

namespace TsImg {
	struct Grid { int z, row0, col0, nrow, ncol; };
	double FloorAt (const std::vector<TsBodyCfg::Floor> &floor, double lng, double lat); // metres a node may go down to
	int Size (int L);                                   // 128, 256, 512
	Grid EoxGrid (int L, int ilat, int ilng);
	int GibsLevel (int L, int nmax);
	Grid GibsGrid (int L, int ilat, int ilng, int nmax);
	Grid TerrGrid (int L, int ilat, int ilng, int zmax);
	Grid SrcGrid (const TsSource &s, int L, int ilat, int ilng);
	std::string Url (const std::string &tmpl, int z, int row, int col);
	bool IsFill (const QImage &img, const int rgb[3]);
	QImage Eox (const std::vector<QImage> &tiles, const Grid &g, int L);
	QImage Gibs (int L, int ilat, int ilng, const Grid &g, const std::vector<QImage> &tiles);
	QImage Build (const TsSource &s, int L, int ilat, int ilng, const Grid &g, const std::vector<QImage> &tiles);
	QImage Mask (const QImage &lights, const QImage &water, const int black[3], double grey);
	QImage Region (const QImage &anc, int depth, int ilat, int ilng);
	std::string Dds (const QImage &img, bool alpha);   // DXT1, full mip chain
	QImage DdsImage (const QByteArray &dds);             // level 0 of a DXT1 DDS
	std::string Elev (int L, int ilat, int ilng, const Grid &g, const std::vector<QImage> &tiles, double scale, const std::vector<TsBodyCfg::Floor> &floor = {}); // .elv, header only (dtype 0) at sea level
}

// Cloud-Optimized GeoTIFF reads: ESA WorldCover 3 degree cells (TileStreamCog.cpp)
namespace TsCog {
	struct Ifd { uint32_t w = 0, h = 0, tw = 0, th = 0, bps = 8, spp = 1, comp = 1, pred = 1, planar = 1; std::vector<uint64_t> off, cnt; };
	struct Req { std::string url; uint64_t off = 0, len = 0; int status = 0; std::shared_ptr<const QByteArray> data; };
	using Fetch = std::function<void (std::vector<Req> &)>;          // fills status (200, 404, else failed) and data
	int64_t Parse (const QByteArray &head, std::vector<Ifd> &ifds);   // 0 parsed, > 0 bytes of the file start needed, -1 no tiled TIFF
	bool Decode (const Ifd &f, const QByteArray &data, std::vector<uint8_t> &out); // one tile, tw*th*spp bytes
	std::string CellName (int lat0, int lng0);                        // N27W081
	// class per pixel over an Orbiter tile, n x n: 0 open sea, 255 outside WorldCover; false when a read failed
	bool Classes (const std::string &tmpl, int L, int ilat, int ilng, int n, const Fetch &fetch, std::vector<uint8_t> &out);
	bool IsWater (uint8_t c);
	QImage Water (const std::vector<uint8_t> &cls, int n, const QImage &gibs); // grey 255 water; gibs where outside
	QImage ClassImage (const std::vector<uint8_t> &cls, int n, int size);      // RGB weights for the client's micro detail
}

// flock (2) for the folder lock; tests put a stand-in here to fake its errors
extern int (*TsFlock) (int fd, int op);

class TileStream {
public:
	using Clock = std::chrono::steady_clock;

	struct Options {
		std::string cfgfile;
		std::function<std::string (const std::string &)> bodyDir; // Textures/<Body>, resolved
		std::vector<std::string> bodies;                          // bodies in the system
		uint64_t capMB = 2048;                                     // 0: no cap
		bool offline = false;
		std::string userAgent = "OrbiterLinux";
		std::function<void (const std::string &)> log;
		std::function<Clock::time_point ()> now;                  // tests
		int maxActive = 12;                                        // tests: 0 holds every job
		double staleSec = 2.0;
		std::function<std::string (const std::string &)> baseDir; // not upstream (earth-sky): Config/<Body>/Base, resolved
		std::function<double (const std::string &)> radius;       // metres
		int cloudWait = 15;                                        // seconds for the small cloud image at startup
		bool buildings = true;                                     // BLDG lines are used
		bool ways = true;                                          // not upstream (design-ways): the BLDG tiles' roads, rail, airports and waterways
		bool sky = true;                                           // clouds and base flattening at startup
		bool crossDisk = false;                                    // tests: the rename out of TileStream/tmp fails as across disks
		std::function<void (int, int, int)> onBuild;              // tests: a tile's build starting on its worker (L, ilat, ilng)
		std::function<void ()> evictScanned;                       // tests: Evict between its scan and its commit
	};

	explicit TileStream (const Options &opt);
	// the tile folders of every body in the cfg, made before the planets check for them
	static void PrepareDirs (const std::string &cfgfile, const std::function<std::string (const std::string &)> &bodyDir);
	~TileStream ();

	bool Enabled (const std::string &body) const;
	// flags 1: the file failed to load, 2: the client loaded it from the cache
	// 1: the Surf file is there, 0: wanted, -1: never (an installed archive's tile included)
	// flags 4: only the tile's Elev (its imagery from elsewhere); 1 the Elev file is there, 0 wanted, -1 never
	int Want (const std::string &body, int L, int ilat, int ilng, int flags);
	int Done (const std::string &body, int *keys, int nmax); // (L, ilat, ilng) triples
	// z14 building tiles: 1 the file is there, 0 wanted, -1 never
	int WantBldg (const std::string &body, int x, int y);
	std::string BldgPath (const std::string &body, int x, int y) const;
	int WantWay (const std::string &body, int x, int y); // not upstream (design-ways): as WantBldg, made from the same fetch
	std::string WayPath (const std::string &body, int x, int y) const;

	// not upstream (earth-sky): a ground-track tile, after camera wants, before refetches; 1 queued or there, 0 full, -1 never
	int Prefetch (const std::string &body, int L, int ilat, int ilng);
	size_t Prefetching () const; // tests

	size_t Pending () const; // tests
	void Evict ();           // tests: the size cap on the caller's thread

	// not upstream: Launchpad cache controls and region prefetch (TabStream.cpp)
	struct TileKey { int L, ilat, ilng; };
	struct Progress { std::atomic<int> total {0}, done {0}, have {0}, none {0}, failed {0}; std::atomic<uint64_t> bytes {0}; }; // failed: given up on after its tries
	// bytes the stream wrote (tiles, Elev, buildings, Elev_mod, clouds, .part); total: all files under the layer folders
	static uint64_t CacheBytes (const std::string &bodyDir, uint64_t *total = nullptr, size_t *tiles = nullptr);
	// deletes the stream's files (keepBase: L <= 8 stay), rewrites the indexes; tiles removed, 0 and *refused while Busy
	static size_t Clear (const std::string &bodyDir, bool keepBase, bool *refused = nullptr);
	// a stream (this process or another Orbiter) or a Clear has the folder
	static bool Busy (const std::string &bodyDir);
	// tiles within radiusKm of (lat, lng) degrees, L1..maxL, coarse to fine
	static std::vector<TileKey> RegionKeys (double lat, double lng, double radiusKm, int maxL);
	// headless fetch of keys, coarse to fine: 0 done, 1 cancelled, 2 stalled stallSec, 3 no stream, 4 Busy, 5 error
	static int Prefetch (Options opt, const std::string &body, const std::vector<TileKey> &keys, Progress &p, const std::atomic<bool> &cancel, double stallSec = 120.0);

	struct Impl;
private:
	static int PrefetchKeys (Options opt, const std::string &body, const std::vector<TileKey> &keys, Progress &p, const std::atomic<bool> &cancel, double stallSec);
	std::unique_ptr<Impl> d;
};

#endif // !__TILESTREAM_H
