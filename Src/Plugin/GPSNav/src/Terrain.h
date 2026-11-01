// GPSNav: the map's terrain: a grid of elevations under the map, sampled a few ms a frame, coloured per point
#pragma once
#include "Geo.h"
#include "Orbitersdk.h"
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
	};
	Terrain ();
	~Terrain ();
	void Reset ();
	// samples (with a time budget) and draws the last whole grid; false if it has no elevation data to draw
	bool Draw (ImDrawList *dl, const View &v, Mode mode, double alt, bool sea, double sys);

private:
	struct Grid {
		OBJHANDLE body = nullptr;
		int nx = 0, ny = 0;
		gps::LatLon pos;
		double k = 0.0, up = 0.0, t = -1e9, lo = 0.0, hi = 0.0;
		bool any = false;      // an elevation tile answered somewhere
		std::vector<gps::LatLon> p;
		std::vector<double> u;  // each point as a unit vector (x, y, z), for a quick projection each frame
		std::vector<float> e, shade;
		std::vector<unsigned char> ok;
	};
	bool Need (const View &v, double sys) const;
	void Start (const View &v, double sys);
	void Sample ();
	void Finish ();

	std::vector<ElevationTile> *cache = nullptr;
	Grid cur, next;
	int done = -1;             // next's points sampled so far; -1: none under way
	int level = 0;             // the tile level for next's spacing
	double cellM = 1.0;        // next's spacing, metres
	double began = 0.0;        // when next was started (system time)
	int logged = 0;            // grids written to the log, the first few
};
