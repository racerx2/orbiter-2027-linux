// GPSNav: the map's terrain: a grid of elevations under the map, sampled a few ms a frame, coloured per point
#pragma once
#include "Geo.h"
#include "MapData.h"
#include "Orbitersdk.h"
#include <memory>
#include <vector>

struct ImDrawList;

class Terrain {
public:
	enum Mode { TOPO, WARN };
	struct View {
		OBJHANDLE body;
		double R;
		gps::LatLon pos;
		double up, k;          // the map's up (true, radians) and pixels per metre
		float ox, oy;          // own ship on the screen
		float x0, y0, x1, y1;  // the map
		bool osm = false;      // Earth: water and shores from the OpenStreetMap map
		bool download = false; // its tiles fetched (else the cache only)
	};
	Terrain ();
	~Terrain ();
	void Reset ();
	// samples (with a time budget) and draws the last whole grid; false if it has no elevation data to draw. With the map, its
	// shores in shoreCol; shore: whether they were drawn (every map tile under the grid had come)
	bool Draw (ImDrawList *dl, const View &v, Mode mode, double alt, bool sea, double sys, unsigned shoreCol, bool *shore);

private:
	struct Shore { std::shared_ptr<const gps::map::WaterTile> t; float x0, y0, x1, y1; }; // a tile's shores in that part of it
	struct Grid {
		OBJHANDLE body = nullptr;
		int nx = 0, ny = 0;
		gps::LatLon pos;
		double k = 0.0, up = 0.0, t = -1e9, lo = 0.0, hi = 0.0;
		bool any = false;      // an elevation tile answered somewhere
		bool osm = false;      // water from the map
		int mz = 0;            // the map's zoom
		unsigned gen = ~0u;    // the map's generation w is from
		bool covered = false;  // every map tile under the grid had come, or an ancestor
		std::vector<gps::LatLon> p;
		std::vector<double> u;  // each point as a unit vector (x, y, z), for a quick projection each frame
		std::vector<float> e, shade;
		std::vector<unsigned char> ok;
		std::vector<signed char> w;  // 1 water, 0 land, -1 no map
		std::vector<double> mt;      // each point in the map's tiles at mz: x, y (NaN past Web Mercator)
		std::vector<uint64_t> keys;  // those tiles, nearest first
		std::vector<Shore> shores;
	};
	bool Need (const View &v, double sys) const;
	void Start (const View &v, double sys);
	void Sample ();
	void Finish ();
	void MapStart (Grid &g, const View &v); // the map's zoom and tiles for a new grid, and asks for them
	void MapWater (Grid &g);                // its water and shores from the tiles come so far

	std::vector<ElevationTile> *cache = nullptr;
	MapData map;
	Grid cur, next;
	int done = -1;             // next's points sampled so far; -1: none under way
	int level = 0;             // the tile level for next's spacing
	double cellM = 1.0;        // next's spacing, metres
	double began = 0.0;        // when next was started (system time)
	int logged = 0;            // grids written to the log, the first few
	bool mapUsed = false;      // this session asked the map
};
