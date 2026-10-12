// not upstream: fills a planet's tile cache (Textures/<Body>/Surf, Mask) from free imagery servers while the sim runs

#ifndef __TILESTREAM_H
#define __TILESTREAM_H

#include <array>
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
	struct Building { double lat, lng; float hmin, hmax; uint32_t v0, nv, i0, ni; }; // anchor in radians
	struct Mesh {
		int z = 0, x = 0, y = 0;
		std::vector<Building> b;
		std::vector<float> vtx;     // x east, y up, z north (m from the anchor), nx, ny, nz
		std::vector<uint32_t> idx;  // per building, from its v0
	};
	bool Inflate (const std::string &in, std::string &out);   // gzip or zlib
	bool Decode (const std::string &pbf, std::vector<Feature> &out, uint32_t &extent); // layer "building"
	bool Triangulate (const std::vector<std::vector<Pt>> &rings, std::vector<uint32_t> &tri); // ring 0 outer, then holes; indices into the rings in order
	Mesh Build (int z, int x, int y, const std::vector<Feature> &f, uint32_t extent);
	std::string Encode (const Mesh &m);
	bool Read (const std::string &bytes, Mesh &m);
	void TileOf (double lat, double lng, int z, int &x, int &y); // radians
	void LatLng (int z, double x, double y, double &lat, double &lng); // tile coordinates to radians
}

bool TsReadCfg (const std::string &file, std::vector<TsBodyCfg> &bodies, std::string &err);

namespace TsImg {
	struct Grid { int z, row0, col0, nrow, ncol; };
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
	std::string Elev (int L, int ilat, int ilng, const Grid &g, const std::vector<QImage> &tiles, double scale); // .elv, header only (dtype 0) at sea level
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
	};

	explicit TileStream (const Options &opt);
	// the tile folders of every body in the cfg, made before the planets check for them
	static void PrepareDirs (const std::string &cfgfile, const std::function<std::string (const std::string &)> &bodyDir);
	~TileStream ();

	bool Enabled (const std::string &body) const;
	// flags 1: the file failed to load, 2: the client loaded it from the cache
	// 1: the Surf file is there, 0: wanted, -1: never
	int Want (const std::string &body, int L, int ilat, int ilng, int flags);
	int Done (const std::string &body, int *keys, int nmax); // (L, ilat, ilng) triples
	std::string Credit (const std::string &body) const;
	// z14 building tiles: 1 the file is there, 0 wanted, -1 never
	int WantBldg (const std::string &body, int x, int y);
	std::string BldgPath (const std::string &body, int x, int y) const;

	// not upstream (earth-sky): a ground-track tile, below camera wants and above refetches; 1 queued or there, 0 queue full, -1 never
	int Prefetch (const std::string &body, int L, int ilat, int ilng);
	size_t Prefetching () const; // tests

	size_t Pending () const; // tests
	void Evict ();           // tests: the size cap on the caller's thread

	struct Impl;
private:
	std::unique_ptr<Impl> d;
};

#endif // !__TILESTREAM_H
