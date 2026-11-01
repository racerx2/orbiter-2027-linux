// GPSNav: the map's colours for heights (packed as ImGui's IM_COL32: alpha, blue, green, red from the top byte)
#pragma once
#include <cstdint>

namespace gps {

inline uint32_t Rgb (int r, int g, int b) { return 0xFF000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r; }
uint32_t Scale (uint32_t c, double f); // brighter or darker

// a chart's tints: with a sea (Earth), by height above it, the sea blue; without, spread over lo to hi (the heights in view)
uint32_t Topo (double elev, bool sea, double lo, double hi);
// the ground against an altitude: red within 100 ft below or above it, yellow within 1000 ft below, else the tints darkened
uint32_t Warn (double elev, double alt, bool sea, double lo, double hi);

} // namespace gps
