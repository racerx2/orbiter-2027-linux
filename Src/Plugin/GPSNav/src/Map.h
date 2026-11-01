// GPSNav: the OpenStreetMap map's water (OpenMapTiles vector tiles, Web Mercator): its polygons, filled and as shores
#pragma once
#include "Geo.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gps::map {

constexpr int MaxZoom = 14;     // the vector tiles' finest zoom
constexpr int Raster = 512;     // a tile's water mask, texels a side
constexpr double MaxLat = 85.0511287798 * RAD; // Web Mercator's edge

inline uint64_t Pack (int z, int x, int y) { return (uint64_t)z << 56 | (uint64_t)(uint32_t)x << 28 | (uint64_t)(uint32_t)y; }
inline int KeyZ (uint64_t k) { return (int)(k >> 56); }
inline int KeyX (uint64_t k) { return (int)((k >> 28) & 0xFFFFFFF); }
inline int KeyY (uint64_t k) { return (int)(k & 0xFFFFFFF); }

// a point's place in zoom z's tiles: whole part the tile, fraction the place in it (x east, y south); false past MaxLat
bool TileXY (LatLon p, int z, double &tx, double &ty);
LatLon TileLatLon (int z, double tx, double ty);

// a polygon of the water layer: its rings in the layer's extent units, x east, y south, closed (the last point isn't repeated)
struct Feature { std::vector<std::vector<int32_t>> rings; }; // each ring x0, y0, x1, y1, ...

// a tile's "water" layer message, as its bytes (empty: the tile has none, all land); false if the tile isn't a vector tile
bool WaterLayer (const uint8_t *tile, size_t n, std::string &layer);
// a water layer message's polygons and extent; false if it is malformed
bool DecodeLayer (const uint8_t *layer, size_t n, std::vector<Feature> &water, uint32_t &extent);

// one tile's water: a mask (each feature even-odd, its holes land, the features joined) and its shores
struct WaterTile {
	int z = 0, x = 0, y = 0;
	std::vector<uint64_t> bits;      // Raster x Raster from the north-west, water set
	std::vector<float> shore;        // segments as unit vectors on the sphere: x0, y0, z0, x1, y1, z1
	std::vector<float> mid;          // each segment's middle in the tile, 0 to 1: x, y
	void Build (int z, int x, int y, const std::vector<Feature> &water, uint32_t extent);
	bool Water (double fx, double fy) const; // fx, fy: the place in the tile, 0 to 1
	bool Bit (int px, int py) const { return (bits[((size_t)py * Raster + px) >> 6] >> (((size_t)py * Raster + px) & 63)) & 1; }
};

} // namespace gps::map
