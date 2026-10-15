// not upstream: fixed structures' keep-out records for BldgMgr and TreeMgr (design fixed-structures)

#include "FixedVes.h"
#include "VesselAPI.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

namespace FixedVes {

namespace {

struct Keys { bool fixed = false; double clear = 0.0; };
std::map<std::string, Keys> classes; // render thread only

void ReadKey (FILEHANDLE f, int &key, double &clear)
{
	bool b;
	double d;
	if (oapiReadItem_bool (f, (char *)"FixedStructure", b)) key = b ? 1 : 0;
	if (oapiReadItem_float (f, (char *)"FixedStructureClear", d)) clear = d;
}

bool ReadClass (const std::string &file, int &key, double &clear) // as the collision addon's ReadKeys: BaseClass first, then the class's own keys
{
	FILEHANDLE f = oapiOpenFile (file.c_str(), FILE_IN_ZEROONFAIL, CONFIG);
	if (!f) return false;
	char base[512] = "";
	if (oapiReadItem_string (f, (char *)"BaseClass", base) && base[0]) {
		FILEHANDLE g = oapiOpenFile ((std::string (base) + ".cfg").c_str(), FILE_IN_ZEROONFAIL, CONFIG);
		if (g) { ReadKey (g, key, clear); oapiCloseFile (g, FILE_IN_ZEROONFAIL); }
	}
	ReadKey (f, key, clear);
	oapiCloseFile (f, FILE_IN_ZEROONFAIL);
	return true;
}

MATRIX3 TMul (const MATRIX3 &A, const MATRIX3 &B) // A^T B
{
	MATRIX3 C;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) C.data[i * 3 + j] = A.data[i] * B.data[j] + A.data[3 + i] * B.data[3 + j] + A.data[6 + i] * B.data[6 + j];
	return C;
}

double Turned (const MATRIX3 &A, const MATRIX3 &B) // the largest column change
{
	double d = 0.0;
	for (int c = 0; c < 3; c++) d = std::max (d, length (_V(A.data[c] - B.data[c], A.data[3 + c] - B.data[3 + c], A.data[6 + c] - B.data[6 + c])));
	return d;
}

// the external meshes of a vessel as drawn at rest: template, else the mesh its name loads; with offsets
template <class F> void Meshes (VESSEL *v, F &&f)
{
	for (UINT k = 0, n = v->GetMeshCount(); k < n; k++) {
		if (!(v->GetMeshVisibilityMode (k) & MESHVIS_EXTERNAL)) continue; // 0 for a deleted slot
		MESHHANDLE hm = v->GetMeshTemplate (k);
		const char *name = hm ? nullptr : v->GetMeshName (k);
		if (!hm && !name) continue;
		VECTOR3 ofs = _V(0, 0, 0);
		v->GetMeshOffset (k, ofs);
		f (k, hm, name, ofs);
	}
}

} // namespace

const Keys &ClassKeys (VESSEL *v)
{
	static const Keys none;
	if (!v) return none;
	const char *c = v->GetClassName();
	std::string cls = c && c[0] ? std::string (c) : std::string (v->GetName());
	std::string key = c && c[0] ? cls : "#" + cls; // a vessel without a class name keys on its own name, as the addon
	auto it = classes.find (key);
	if (it != classes.end()) return it->second;
	int k = -1;
	double clear = 0.0;
	if (!ReadClass ("Vessels\\" + cls + ".cfg", k, clear)) ReadClass (cls + ".cfg", k, clear);
	Keys &r = classes[key];
	r.fixed = k == 1;
	r.clear = std::isfinite (clear) && clear > 0.0 ? std::min (clear, MaxClear) : 0.0;
	return r;
}

bool FixedClass (OBJHANDLE hVessel)
{
	return ClassKeys (oapiGetVesselInterface (hVessel)).fixed;
}

double ClearOf (OBJHANDLE hVessel)
{
	return ClassKeys (oapiGetVesselInterface (hVessel)).clear;
}

bool Set::Update (OBJHANDLE hPlanet, std::vector<VECTOR3> &moved)
{
	MATRIX3 rp;
	VECTOR3 gp;
	oapiGetRotationMatrix (hPlanet, &rp);
	oapiGetGlobalPos (hPlanet, &gp);
	std::vector<Held> now;
	for (DWORD i = 0, n = oapiGetVesselCount(); i < n; i++) {
		OBJHANDLE h = oapiGetVesselByIndex ((int)i);
		VESSEL *v = h ? oapiGetVesselInterface (h) : nullptr;
		if (!v || !(v->GetFlightStatus() & 1) || v->GetSurfaceRef() != hPlanet || !FixedClass (h)) continue;
		VECTOR3 gv;
		MATRIX3 Rv;
		v->GetGlobalPos (gv);
		v->GetRotationMatrix (Rv);
		VECTOR3 loc = tmul (rp, gv - gp);
		MATRIX3 Rp = TMul (rp, Rv); // the vessel's axes, planet-local
		unsigned long long sig = 1469598103934665603ull;
		auto mix = [&sig] (unsigned long long w) { sig = (sig ^ w) * 1099511628211ull; };
		Meshes (v, [&] (UINT k, MESHHANDLE hm, const char *name, const VECTOR3 &ofs) {
			mix (k); mix ((unsigned long long)(uintptr_t)hm);
			if (name) for (const char *q = name; *q; q++) mix ((unsigned char)*q);
			unsigned long long o[3];
			std::memcpy (o, ofs.data, sizeof o);
			for (unsigned long long w : o) mix (w);
		});
		const Held *old = nullptr;
		for (const Held &x : held) if (x.h == h) old = &x;
		if (old && old->sig == sig && length (old->loc - loc) <= Move && Turned (old->Rp, Rp) <= Turn) { now.push_back (*old); continue; }
		double rl = length (loc), lat = std::asin (std::clamp (loc.y / rl, -1.0, 1.0)), lng = std::atan2 (loc.z, loc.x);
		double slat = std::sin (lat), clat = std::cos (lat), slng = std::sin (lng), clng = std::cos (lng);
		MATRIX3 H = _M(clng * slat, clng * clat, -slng, -clat, slat, 0, slng * slat, slng * clat, clng); // the base frame at the vessel, y up (CollBaseA)
		auto r = std::make_shared<Rec> (every);
		r->hVessel = h; r->name = v->GetName(); r->rp = rp; r->rb = mul (rp, H); r->p = loc; r->clear = ClearOf (h);
		Meshes (v, [&] (UINT, MESHHANDLE hm, const char *name, const VECTOR3 &ofs) {
			if (!hm) hm = oapiLoadMeshGlobal (name);
			for (DWORD g = 0; hm && g < oapiMeshGroupCount (hm); g++) {
				const MESHGROUP *G = oapiMeshGroup (hm, g);
				if (!G || !G->Vtx || !G->Idx) continue;
				for (DWORD i = 0; i + 2 < G->nIdx; i += 3) {
					double q[3][3];
					bool ok = true;
					for (int c = 0; c < 3 && ok; c++) {
						WORD n = G->Idx[i + c];
						ok = n < G->nVtx;
						if (!ok) break;
						VECTOR3 l = tmul (H, mul (Rp, _V(G->Vtx[n].x, G->Vtx[n].y, G->Vtx[n].z) + ofs));
						q[c][0] = l.x; q[c][1] = l.y; q[c][2] = l.z;
					}
					if (ok) r->idx.Add (q[0], q[1], q[2]);
				}
			}
		});
		oapiWriteLogV ("TileStream: fixed structure %s: %u %sfaces keep streamed %s out, clear within %.0f m", r->name.c_str(), (unsigned)r->idx.Count(), every ? "" : "structure ", every ? "trees" : "buildings", r->clear);
		if (old) moved.push_back (old->loc);
		moved.push_back (loc);
		now.push_back (Held { h, loc, Rp, sig, r });
	}
	for (const Held &x : held)
		if (std::none_of (now.begin(), now.end(), [&] (const Held &y) { return y.h == x.h; })) {
			oapiWriteLogV ("TileStream: fixed structure %s no longer keeps streamed %s out", x.rec->name.c_str(), every ? "trees" : "buildings");
			moved.push_back (x.loc);
		}
	held.swap (now);
	return !moved.empty();
}

void Set::Near (const VECTOR3 &up, double R, double range, std::vector<std::shared_ptr<const Rec>> &out) const
{
	for (const Held &x : held) {
		double l = length (x.loc);
		if (l > 0 && R * std::acos (std::min (1.0, dotp (x.loc / l, up))) <= range) out.push_back (x.rec);
	}
}

} // namespace FixedVes
