// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __ELEVMGR_H
#define __ELEVMGR_H

#include "OrbiterPlatform.h" // windows.h left out: DWORD, INT16
#include "Vecmat.h"
#include "ZTreeMgr.h"
#include <vector>
#include <cstdint>       // not upstream: the Elev file cache
#include <mutex>
#include <string>
#include <unordered_map>

class CelestialBody;

struct ElevationTile {
	ElevationTile() { 
		data = nullptr;
		Clear();
	}
	~ElevationTile() { if (data) delete []data; }

	void Clear() { 
		if (data) delete[]data; 
		data = nullptr;
		mgr = nullptr;
		lvl = tgtlvl = 0;
		latmin = latmax = 0.0;
		lngmin = lngmax = 0.0;
		emin = emax = 0.0;
		last_access = 0.0;
		lat0 = lng0 = 0;
		ilat = ilng = 0;
		quadrants = 0;
		celldiag = false;
		nmlidx = 0;
	}

	INT16 *data;
	int lvl, tgtlvl;
	double latmin, latmax;
	double lngmin, lngmax;
	double emin, emax;
	double last_access;
	int lat0, lng0;
	int ilat, ilng;
	int quadrants;
	bool celldiag;
	int nmlidx;
	const class ElevationManager* mgr;
};

class ElevationManager {
public:
	ElevationManager (const CelestialBody *_cbody);
	~ElevationManager();
	double Elevation (double lat, double lng, int reqlvl=0, std::vector<ElevationTile> *tilecache = 0, Vector *normal=0, int *lvl=0) const;
	/**
	* \brief Synthesize an elevation tile by interpolating from the parent
	* \param ilat latitude index of target tile
	* \param ilng longitude index of target tile
	* \param lvl resolution level of target tile
	* \param pilat latitude index of source tile
	* \param pilng longitude index of source tile
	* \param plvl resolution level of source tile
	* \param pelev source tile data
	* \param [out] elev target tile data
	* \param emean if != 0, receives mean elevation
	* \note Requires lvl > plvl. The parent tile parameters must be an actual parent of the target (i.e. target covers a sub-area of the parent)
	*/
	void ElevationGrid (int ilat, int ilng, int lvl, int pilat, int pilng, int plvl, INT16 *pelev, float *elev, double *emean=0) const;
	void ElevationGrid(int ilat, int ilng, int lvl, int pilat, int pilng, int plvl, INT16* pelev, INT16* elev, double* emean = 0) const;

protected:
	int  Quadrant(double lat, double lng, int lvl) const;
	bool TileIdx (double lat, double lng, int lvl, int *ilat, int *ilng) const;
	INT16 *LoadElevationTile (int lvl, int ilat, int ilng, double tgt_res) const;
	bool LoadElevationTile_mod (int lvl, int ilat, int ilng, double tgt_res, INT16 *elev) const;
	bool HasElevationTile(int lvl, int ilat, int ilng) const;
	bool LooseElev (int lvl, int ilat, int ilng) const;    // not upstream: an Elev tile file is there
	bool StreamCovers (int lvl, int ilat, int ilng) const; // not upstream: the stream's Elev of the parent (or of the ancestor at its last level) hides this archive tile
	bool LooseFile (int lvl, int ilat, int ilng, std::string *path = nullptr) const; // not upstream: the Elev file is there, from the cache below (vessels query heights many times a frame)
	void TileBad (int lvl, int ilat, int ilng, bool loose) const;                     // not upstream: a file or archive node that would not load counts as no tile from now on
	bool NodeBad (int lvl, int ilat, int ilng) const;                                 // not upstream: TileBad's archive nodes

private:
	const CelestialBody *cbody;
	int maxlvl = 0;
	int mode = 0;  // elevation mode (0=no elevation, 1=linear interpolation, 2=cubic interpolation)
	double elev_res = 1;  // elevation resolution [m]
	DWORD tilesource = 2; // bit 1: try loading from cache, bit 2: try loading from archive
	ZTreeMgr *treeMgr[5];
	bool bDirExists, bModExists;
	int streamMax = 0; // not upstream: the tile stream's last Elev level (TsSessionElev), 0 without it
	mutable std::vector<ElevationTile> *local_cache = nullptr;
	struct Loose { bool there; bool walked; unsigned gen; std::string path; }; // not upstream: one Elev file: found, the case-insensitive lookup done, TsWriteGen when looked for, its path
	std::string elevDir;                                      // not upstream: the resolved <body>/Elev folder
	mutable std::mutex looseMx;                               // not upstream: Elevation is called from more than one thread
	mutable std::unordered_map<uint64_t, Loose> loose;        // not upstream: by lvl, ilat, ilng
	mutable std::unordered_map<uint64_t, bool> badNode;       // not upstream: archive nodes that would not load
	mutable unsigned looseGen = 0;                            // not upstream: TsWriteGen when the missing files were last checked
	struct Shared { std::vector<INT16> data; unsigned gen; uint64_t used; }; // not upstream: a decoded tile with its Elev_mod, TsWriteGen when read, last use
	mutable std::unordered_map<uint64_t, Shared> shared;      // not upstream: by lvl, ilat, ilng; every vessel's tile cache copies from it (a breakup adds dozens of vessels on the same tile)
	mutable uint64_t sharedUse = 0;
	mutable std::unordered_map<uint64_t, unsigned> sharedMiss; // not upstream: levels with no tile, by TsWriteGen when looked for
	mutable std::mutex sharedMx;
	INT16 *SharedTile (int lvl, int ilat, int ilng) const;    // not upstream: LoadElevationTile and LoadElevationTile_mod, read once and copied
};

#endif // !__ELEVMGR_H
