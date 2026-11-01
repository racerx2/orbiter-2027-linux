// GPSNav: the map's colours for heights (packed as ImGui's IM_COL32: alpha, blue, green, red from the top byte)
#pragma once
#include <cstdint>

namespace gps {

inline uint32_t Rgb (int r, int g, int b) { return 0xFF000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r; }
uint32_t Scale (uint32_t c, double f); // brighter or darker

// a point with a sea (Earth) is water by the map (water 1, land 0), else (-1, no map) at or below 0 m
inline bool IsWater (double elev, int water) { return water > 0 || (water < 0 && elev <= 0.0); }
// a chart's tints: with a sea (Earth), water blue by depth, land by height (below sea level as lowland); else over lo to hi
uint32_t Topo (double elev, bool sea, double lo, double hi, int water = -1);
// the ground against an altitude: red within 100 ft below or above it, yellow within 1000 ft below, else the tints darkened
uint32_t Warn (double elev, double alt, bool sea, double lo, double hi, int water = -1);

} // namespace gps
