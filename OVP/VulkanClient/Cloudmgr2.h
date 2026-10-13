// ==============================================================
//   ORBITER VISUALISATION PROJECT (OVP)
//   Copyright (C) 2006-2026 Martin Schweiger
//   Dual licensed under GPL v3 and LGPL v3
// ==============================================================

#ifndef __CLOUDMGR2_H
#define __CLOUDMGR2_H

#include "Tilemgr2_imp.hpp"
#include <atomic>
#include <memory>
#include <vector>

/**
 * \brief Visual representation of planetary cloud tile.
 *
 * Rendering of planetary cloud layers using texture tiles at
 * variable resolutions (new version).
 */
class CloudTile: public Tile {
	friend class TileManager2Base;
	template<class CloudTile> friend class TileManager2;

public:
	CloudTile (TileManager2Base *_mgr, int _lvl, int _ilat, int _ilng);
	~CloudTile ();

	inline QuadTreeNode<CloudTile>* GetNode() const { return node; }
	inline void SetNode (QuadTreeNode<CloudTile> *_node) { node = _node; }
	// Register the tile to a quad tree node

	double GetMinElev() const { return cloudalt; }		// virtual from Tile::
	double GetMaxElev() const { return cloudalt; }		// virtual from Tile::
	double GetMeanElev() const { return cloudalt; }		// virtual from Tile::

	// not upstream: the alpha as 256x256 bytes for the volume clouds, NULL until loaded or with VolClouds off
	std::shared_ptr<const std::vector<uint8_t>> Density() const { return densReady.load(std::memory_order_acquire) ? dens : nullptr; }

protected:
	virtual Tile *getParent() const { return node && node->Parent() ? node->Parent()->Entry() : NULL; }
	// Return pointer to parent tile, if exists

	virtual void Load ();
	virtual void PreLoad ();
	virtual void Render ();

	double cloudalt;
	std::shared_ptr<const std::vector<uint8_t>> dens; // not upstream: set by PreLoad before densReady
	std::atomic<bool> densReady{false};
	TileManager2<CloudTile> *cmgr;	// cloud tile manager interface
	QuadTreeNode<CloudTile> *node;	// my node in the quad tree, if I'm part of a tree
};

template<> int TileManager2<CloudTile>::VolTiles (double lat, double lng, double half, std::vector<VolCloud::Src> &out); // not upstream

#endif // !__CLOUDMGR2_H

