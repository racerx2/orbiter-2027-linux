# GPSNav

A panel GPS navigator add-on for Orbiter on Linux: a moving map, Direct-To, flight plans, the nearest airports, bases and beacons, waypoint information and a glide path to a runway or pad, for any vessel on any body. It reads Orbiter's bases (built in or installed with add-ons), their pads and beacons, OurAirports' airports, runways and navaids for Earth, Earth's water and shores from the OpenStreetMap map, and your own airports and waypoints from simple CSV files.

The manual is `Html/GPSNav.htm` (in Orbiter: the dialog's help button).

## Build and install

In Orbiter's source tree (`Src/Plugin/GPSNav`) it builds, installs and is packaged with Orbiter; its test is `GPSNav.Test` in ctest.

Standalone, against an Orbiter folder (an installed Orbiter of 2026-10-31 or later, or a build tree), with Qt 6.5 or later (Core and Network, the Qt Orbiter runs on):

```
cmake -S . -B build -G Ninja -DORBITER_ROOT=/path/to/Orbiter
cmake --build build
ctest --test-dir build
cmake --install build
```

The install puts `Modules/Plugin/GPSNav.so`, `GPSNav/Data`, `GPSNav/User` and `Html/Plugin/GPSNav.chm` into that folder. Enable **GPSNav** in the Launchpad's Modules tab, then in flight click the GPS pin on the menu bar (or Custom functions, Ctrl+F4 > GPS navigator).

## Data

`Data/Earth` is made from [OurAirports](https://ourairports.com/data/) (public domain) by `tools/ourairports.py` from the files of github.com/davidmegginson/ourairports-data. Your own files go in `GPSNav/User/<body>/`; the manual has their format.

Earth's water and shores come from the OpenStreetMap map: the water layer of the OpenMapTiles vector tiles served by [OpenFreeMap](https://openfreemap.org) (free, no key), fetched as you fly and kept in `GPSNav/Cache/Map`. MENU's Map download OFF uses that folder only. Map data © OpenStreetMap contributors, [ODbL](https://www.openstreetmap.org/copyright); tiles © [OpenMapTiles](https://openmaptiles.org).
