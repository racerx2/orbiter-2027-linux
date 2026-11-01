// GPSNav: the OpenStreetMap map's water tiles (OpenFreeMap): fetched on a worker thread, kept in GPSNav/Cache/Map and in memory
#pragma once
#include "Map.h"
#include <memory>
#include <string>
#include <vector>

class MapData {
public:
	MapData ();
	~MapData ();
	// the tiles wanted now, most wanted first (replaces the last list): from the cache, else fetched when download is on
	void Want (const std::vector<uint64_t> &keys, bool download);
	std::shared_ptr<const gps::map::WaterTile> Get (uint64_t key); // a loaded tile, or null
	unsigned Generation () const;  // goes up as tiles come in
	void Stop ();                  // the worker ends (a session's end); the loaded tiles stay
	std::string Summary () const;  // for the log

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};
