// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: what streamed trees and ways keep off (BaseShapes.h)

#include "BaseShapes.h"
#include "VPlanet.h"
#include "D3D9Client.h"
#include <algorithm>
#include <cmath>

namespace BaseShapes {

VECTOR3 Up (double lat, double lng) { return _V(cos (lat) * cos (lng), sin (lat), cos (lat) * sin (lng)); }

bool Keep::Blocked (const VECTOR3 &up, double R, double margin) const
{
	for (const auto &z : zones) if (ClearZones::Covers (z, up, R)) return true; // a base file's clear zone (design base-clear)
	for (const auto &b : bases) {
		VECTOR3 p = up * R - b->p, l = tmul (b->rb, mul (b->rp, p));
		if (b->idx.Covers (l.x, l.z, margin)) return true;
	}
	for (const auto &b : fixed) {
		VECTOR3 p = up * R - b->p, l = tmul (b->rb, mul (b->rp, p));
		if (b->clear > 0.0 && l.x * l.x + l.z * l.z <= b->clear * b->clear) return true; // its clear zone (design fixed-clear)
		if (b->idx.Covers (l.x, l.z, margin)) return true;
	}
	return false;
}

// a base's structures, every triangle, once
const std::shared_ptr<const Base> &Set::Of (vPlanet *vp, OBJHANDLE hPlanet, OBJHANDLE hBase)
{
	auto it = bases.find (hBase);
	if (it != bases.end()) return it->second;
	auto b = std::make_shared<Base>();
	VECTOR3 gp, gb;
	oapiGetRotationMatrix (hPlanet, &b->rp);
	oapiGetRotationMatrix (hBase, &b->rb);
	oapiGetGlobalPos (hPlanet, &gp);
	oapiGetGlobalPos (hBase, &gb);
	b->p = tmul (b->rp, gb - gp);
	MESHHANDLE *sbs = nullptr, *sas = nullptr;
	DWORD nbs = 0, nas = 0;
	vp->GetClient()->GetBaseStructures (hBase, &sbs, &nbs, &sas, &nas);
	for (int pass = 0; pass < 2; pass++)
		for (DWORD k = 0; k < (pass ? nas : nbs); k++) {
			MESHHANDLE hm = pass ? sas[k] : sbs[k];
			for (DWORD g = 0; hm && g < oapiMeshGroupCount (hm); g++) {
				const MESHGROUP *G = oapiMeshGroup (hm, g);
				if (!G || !G->Vtx || !G->Idx) continue;
				for (DWORD i = 0; i + 2 < G->nIdx; i += 3) {
					double v[3][3];
					bool ok = true;
					for (int c = 0; c < 3; c++) {
						WORD n = G->Idx[i + c];
						if (n >= G->nVtx) { ok = false; break; }
						v[c][0] = G->Vtx[n].x; v[c][1] = G->Vtx[n].y; v[c][2] = G->Vtx[n].z;
					}
					if (ok) b->idx.Add (v[0], v[1], v[2]);
				}
			}
		}
	return bases[hBase] = b;
}

void Set::Near (vPlanet *vp, OBJHANDLE hPlanet, double R, const VECTOR3 &up, double range, std::vector<std::shared_ptr<const Base>> &out)
{
	for (DWORD i = 0, nb = oapiGetBaseCount (hPlanet); i < nb; i++) {
		OBJHANDLE hb = oapiGetBaseByIndex (hPlanet, i);
		double lng, lat, rad;
		oapiGetBaseEquPos (hb, &lng, &lat, &rad);
		if (R * acos (std::min (1.0, dotp (Up (lat, lng), up))) > range) continue;
		const std::shared_ptr<const Base> &b = Of (vp, hPlanet, hb);
		if (!b->idx.Empty()) out.push_back (b);
	}
}

} // namespace BaseShapes
