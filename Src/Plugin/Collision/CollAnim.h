// not upstream: replica of the client's incremental animation mode for collider poses; Orbiter-free

#ifndef __COLLANIM_H
#define __COLLANIM_H

#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>
#include "CollGeom.h"

struct ANIMATION;      // Orbitersdk/include/OrbiterAPI.h; only CollAnim.cpp includes it, so this header stays light
struct ANIMATIONCOMP;

constexpr uint32_t COLLANIM_LVL = 0xFFFFFFFFu; // LOCALVERTEXLIST (OrbiterAPI.h): a tree node without geometry
enum { COLLANIM_NULL = 0, COLLANIM_ROTATE = 1, COLLANIM_TRANSLATE = 2, COLLANIM_SCALE = 3 }; // MGROUP_TRANSFORM::TYPE values

// copy of one component's transform parameters, taken when the module adds it (6.2)
struct CollAnimComp {
	int type;                  // MGROUP_TRANSFORM::Type() at add time (COLLANIM_*)
	uint32_t mesh;             // may be LOCALVERTEXLIST (COLLANIM_LVL)
	bool wholeMesh;            // grp == NULL (Orbitersdk/include/VesselAPI.h:3968-3971)
	std::vector<uint32_t> grp; // copy of grp[0..ngrp-1]; empty for LOCALVERTEXLIST
	Vector ref, axis, shift, scale; double angle; // raw at add; moved by parents like the client moves module memory
};

// per-vessel replica: own component copies, per-group and per-mesh matrices, one step per P1
class CollAnim {
public:
	void OnAdd (const ANIMATIONCOMP *ac);      // snapshot ac->trans (virtual Type() while the object is alive)
	void OnDel (const ANIMATIONCOMP *ac);      // forget it; matrices stay
	void OnClear ();                           // forget all components; matrices and cur stay (client keeps them)
	void OnMeshInsert (uint32_t mesh);         // fresh client mesh: identity matrices for that mesh
	void OnMeshDelete (uint32_t mesh);         // LOCALVERTEXLIST = all meshes
	bool Step (const ANIMATION *anim, uint32_t nanim, const uint8_t *present, uint32_t nmesh); // once per P1; true if any matrix changed
	bool GroupTransform (uint32_t mesh, uint32_t grp, CollAffine &F) const;                     // F = G o M; false (F identity) if neither was ever touched
	void Signatures (const ANIMATION *anim, uint32_t nanim, uint32_t mesh, uint32_t ngrp,
		const uint8_t *present, uint32_t nmesh, std::vector<uint64_t> &sig) const;            // per group (3.3)
	uint64_t Version () const;                 // add, del, clear
private:
	friend struct CollAnimProbe;               // unit tests read the snapshot count
	void Animate (const ANIMATION &A, uint32_t an, const uint8_t *present, uint32_t nmesh, bool &changed);
	void Apply (const ANIMATIONCOMP *c, const CollAffine &T, const uint8_t *present, uint32_t nmesh, bool &changed);
	void Reach (const ANIMATIONCOMP *c, uint64_t h, uint32_t mesh, uint32_t ngrp,
		const uint8_t *present, uint32_t nmesh, std::vector<uint64_t> &sig) const;
	std::unordered_map<const ANIMATIONCOMP*, CollAnimComp> work; // lookup only, never iterated (6.2)
	std::map<uint32_t, CollAffine> M;
	std::map<std::pair<uint32_t,uint32_t>, CollAffine> G;
	std::vector<double> cur;
	uint64_t ver = 0;                          // Version ()
	bool logNaN = false, logZeroAxis = false;  // log once
	bool logMissing = false, logBadT = false;  // log once: component without snapshot, non-finite transform
};

#endif // !__COLLANIM_H
