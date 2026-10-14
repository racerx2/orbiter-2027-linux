// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: re-entry hull heating (design-reentry-heat 2, 4, 7, Rev 2, Rev 3): per-vessel wall temperatures in sim time, the real edges' attachment lines, the shader block of its exterior draws

#include "HullHeat.h"
#include "HeatMath.h"
#include "VVessel.h"
#include "Mesh.h"
#include "Scene.h"
#include "D3D9Effect.h"
#include "D3D9Config.h"
#include "OapiExtension.h"
#include "Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace HeatMath;
typedef std::array<double, 3> P3;

namespace {

const int DIRS = 128;						// support directions (design 2.5)
const DWORD BUILD_STEP = 20000;				// vertices per Step while a support set builds (review N11)
const size_t EDGE_RAYS = 2500;				// exposure rays per Step while a mesh's real edges build (Rev 3)
const DWORD FRAME_BUILD = 4 * BUILD_STEP;	// and per frame over all vessels (a breakup adds dozens of new meshes at once)
const size_t FRAME_RAYS = 4 * EDGE_RAYS;
DWORD frameBuild = 0;
size_t frameRays = 0;
DWORD budgetFrame = ~0u;

struct MeshSup {							// one mesh's extreme vertices along DIRS directions, mesh coordinates
	DWORD nv = 0, ng = 0;
	const NMVERTEX *vb = nullptr;			// the vertex copy it was read from: a new mesh at a reused address rebuilds
	DWORD g = 0, k = 0;						// build position: group, vertex
	bool done = false;
	double best[DIRS];
	P3 pt[DIRS];
	std::vector<P3> pts;					// the unique points, once done
	double used = 0.0;						// system time of the last query (purge, review NIT 5)
	HeatMath::EdgeSet es;					// the real edges (Rev 3), after the support set
	bool esBegun = false;
	double esMs = 0.0;
	int esSteps = 0;
	MeshSup () { for (double &b : best) b = -1e300; }
};

P3 Dir (int i)								// Fibonacci sphere
{
	const double ga = HPI * (3.0 - std::sqrt(5.0));
	double z = 1.0 - (2.0 * i + 1.0) / DIRS, r = std::sqrt(std::max(0.0, 1.0 - z * z)), p = ga * i;
	return { r * std::cos(p), z, r * std::sin(p) };
}

void Mul (const P3 &p, const D3DXMATRIX &M, P3 &o)	// row vector times M, with the translation
{
	P3 r;
	r[0] = p[0] * M._11 + p[1] * M._21 + p[2] * M._31 + M._41;
	r[1] = p[0] * M._12 + p[1] * M._22 + p[2] * M._32 + M._42;
	r[2] = p[0] * M._13 + p[1] * M._23 + p[2] * M._33 + M._43;
	o = r;
}

}

struct HullHeat::State {
	const VESSEL *v = nullptr;				// the vessel and class it was made for: a vessel at a reused handle starts afresh (review S3)
	std::string cls;
	bool init = false, cfgRead = false, glow = false, told = false, flowOk = false;
	double t = 0.0, sys = 0.0, nextLog = 0.0;
	double T[NBIN] = {};
	HeatCfg cfg;
	double Rn = 0.5, kfp = 0.0;
	double fl[3] = { 0.0, 0.0, -1.0 };		// downstream, vessel axes, low-passed (review S3)
	double ps[3] = { 0.0, 0.0, 0.0 };		// stagnation point, vessel axes, low-passed
	float lnB[NBIN][3] = {};
	std::vector<EdgeSeg> segs;				// attachment segments, vessel axes (Rev 3)
	double segFl[3] = { 0.0, 0.0, 0.0 };	// the flow they were found for
	double segT = -1e30;
	bool segAll = false;					// every mesh's edges were built
};

namespace {

typedef HullHeat::State State;

VkFxHandle hHeat = nullptr;
bool on = false;
ColourTable table;
std::unordered_map<OBJHANDLE, State> *states = nullptr;
std::unordered_map<const D3D9Mesh *, MeshSup> *meshSup = nullptr;
std::map<std::string, HeatCfg> *cfgs = nullptr;
int logLines = 0;
unsigned steps = 0;
double stepMs = 0.0;
int stepN = 0;

// Config/GC/Plume/<class>.cfg, the HEAT line (design 8); the class name as PostPlume::CfgOf parses it
const HeatCfg &CfgOf (VESSEL *v)
{
	char cn[256];
	snprintf(cn, sizeof(cn), "%s", v->GetClassName() ? v->GetClassName() : "");
	for (char *c = cn; *c; c++) if (*c == '/' || *c == '\\') *c = '_';
	auto it = cfgs->find(cn);
	if (it != cfgs->end()) return it->second;
	HeatCfg &c = (*cfgs)[cn];
	std::string dir = OapiExtension::GetConfigDir();
	for (char &ch : dir) if (ch == '\\') ch = '/';
	if (!dir.empty() && dir.back() != '/') dir += '/';
	std::string path = dir + "GC/Plume/" + cn + ".cfg";
	std::ifstream fs(path);
	if (!fs) return c;
	std::string line;
	bool any = false;
	while (std::getline(fs, line)) any |= ParseHeatLine(line, c);
	if (c.bad) LogErr("HullHeat: %s: %d HEAT values not understood", path.c_str(), c.bad);
	if (any) LogAlw("HullHeat: %s: %s, nose %.2f, emis %.2f, lag %.0f, glow %.2f", path.c_str(), c.off ? "off" : "on", c.nose, c.emis, c.lag, c.glow);
	return c;
}

// part of one mesh's support set: at most budget vertices; true when the set is complete (review N11)
bool BuildSome (D3D9Mesh *m, MeshSup &ms, DWORD &budget, size_t &done)
{
	DWORD n = m->GetVertexCount(), ng = m->GetGroupCount();
	const NMVERTEX *vb = (m->pBuf) ? m->pBuf->pVBSys : nullptr;
	if (ms.nv != n || ms.ng != ng || ms.vb != vb) { ms = MeshSup(); ms.nv = n; ms.ng = ng; ms.vb = vb; }
	if (ms.done) return true;
	static P3 dirs[DIRS];
	static bool dirsOk = false;
	if (!dirsOk) { for (int i = 0; i < DIRS; i++) dirs[i] = Dir(i); dirsOk = true; }
	while (vb && ms.g < ng && budget > 0) {
		const D3D9Mesh::GROUPREC *gr = m->GetGroup(ms.g);
		if (!gr || gr->bDeleted || ms.k >= gr->nVert) { ms.g++; ms.k = 0; continue; }
		D3DXMATRIX M;
		if (gr->bTransform) M = m->GetTransform(int(ms.g), true);
		else if (m->GetTransform()) M = *m->GetTransform();
		else D3DXMatrixIdentity(&M);
		DWORD e = std::min(gr->nVert, ms.k + budget);
		for (; ms.k < e; ms.k++) {
			const NMVERTEX &v = vb[gr->VertOff + ms.k];
			P3 p;
			Mul({ v.x, v.y, v.z }, M, p);
			for (int i = 0; i < DIRS; i++) {
				double d = p[0] * dirs[i][0] + p[1] * dirs[i][1] + p[2] * dirs[i][2];
				if (d > ms.best[i]) { ms.best[i] = d; ms.pt[i] = p; }
			}
			budget--;
			done++;
		}
	}
	if (vb && ms.g < ng) return false;
	ms.done = true;
	ms.pts.clear();
	for (int i = 0; i < DIRS; i++) {
		if (ms.best[i] < -1e299) continue;
		if (std::find(ms.pts.begin(), ms.pts.end(), ms.pt[i]) == ms.pts.end()) ms.pts.push_back(ms.pt[i]);
	}
	return true;
}

// part of one mesh's real edges (Rev 3): the triangles in mesh coordinates as the support set reads them, then the build in parts
void EdgeStep (const char *name, D3D9Mesh *m, MeshSup &ms, size_t &rays)
{
	if (rays < size_t(Tune::edgeRays)) return;
	auto t0 = std::chrono::steady_clock::now();
	if (!ms.esBegun) {
		ms.esBegun = true;
		std::vector<float> xyz;
		std::vector<uint32_t> tri;
		const NMVERTEX *vb = m->pBuf ? m->pBuf->pVBSys : nullptr;
		const WORD *ib = m->pBuf ? m->pBuf->pIBSys : nullptr;
		DWORD ng = m->GetGroupCount();
		for (DWORD g = 0; vb && ib && g < ng; g++) {
			const D3D9Mesh::GROUPREC *gr = m->GetGroup(g);
			if (!gr || gr->bDeleted) continue;
			D3DXMATRIX M;
			if (gr->bTransform) M = m->GetTransform(int(g), true);
			else if (m->GetTransform()) M = *m->GetTransform();
			else D3DXMatrixIdentity(&M);
			uint32_t base = uint32_t(xyz.size() / 3);
			for (DWORD k = 0; k < gr->nVert; k++) {
				const NMVERTEX &v = vb[gr->VertOff + k];
				P3 p;
				Mul({ v.x, v.y, v.z }, M, p);
				for (int j = 0; j < 3; j++) xyz.push_back(float(p[j]));
			}
			for (DWORD f = 0; f < gr->nFace; f++) {
				const WORD *t = ib + gr->IdexOff + 3 * f;
				if (t[0] >= gr->nVert || t[1] >= gr->nVert || t[2] >= gr->nVert) continue;
				for (int j = 0; j < 3; j++) tri.push_back(base + t[j]);
			}
		}
		ms.es.Begin(xyz, tri);
	}
	bool fin = ms.es.Step(rays);
	ms.esMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	ms.esSteps++;
	if (fin) LogAlw("HullHeat: %s: hull edges, %d triangles, %d convex, %d exposed, %d real (voxel %.3f m, %.1f ms over %d steps)", name,
		int(ms.es.tris), int(ms.es.convex), int(ms.es.exposed), int(ms.es.real), ms.es.vox, ms.esMs, ms.esSteps);
}

}

// the upstream-most hull vertex along up (vessel axes): exterior meshes' support sets, each moved by its mesh offset this frame (review S1)
bool HullHeat::Upstream (vVessel *vv, const double up[3], double out[3])
{
	DWORD fid = vv->GetScene() ? vv->GetScene()->GetFrameId() : 0;
	if (fid != budgetFrame) { budgetFrame = fid; frameBuild = FRAME_BUILD; frameRays = FRAME_RAYS; }
	DWORD budget = std::min(BUILD_STEP, frameBuild);
	size_t done = 0, rays = std::min(EDGE_RAYS, frameRays);
	const DWORD budget0 = budget;
	const size_t rays0 = rays;
	struct Spend { DWORD &b; size_t &r; const DWORD b0; const size_t r0; ~Spend() { frameBuild -= std::min(frameBuild, b0 - b); frameRays -= std::min(frameRays, r0 - r); } } spend { budget, rays, budget0, rays0 };
	bool all = true, any = false;
	double bv = -1e300;
	auto t0 = std::chrono::steady_clock::now();
	for (UINT i = 0; i < vv->nmesh; i++) {
		D3D9Mesh *m = vv->meshlist[i].mesh;
		if (!m || !m->IsOK() || !(vv->meshlist[i].vismode & MESHVIS_EXTERNAL)) continue;
		MeshSup &ms = (*meshSup)[m];
		ms.used = oapiGetSysTime();
		if (!BuildSome(m, ms, budget, done)) { all = false; continue; }
		D3DXMATRIX T;
		if (vv->meshlist[i].trans) T = *vv->meshlist[i].trans;
		else D3DXMatrixIdentity(&T);
		if (!ms.es.Done()) EdgeStep(vv->GetInterface()->GetName(), m, ms, rays);
		for (const P3 &p : ms.pts) {
			P3 q;
			Mul(p, T, q);
			double v = q[0] * up[0] + q[1] * up[1] + q[2] * up[2];
			if (v > bv) { bv = v; for (int j = 0; j < 3; j++) out[j] = q[j]; any = true; }
		}
	}
	if (done) {
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		LogAlw("HullHeat: %s: support sets, %d vertices this step (%.2f ms)%s", vv->GetInterface()->GetName(), int(done), ms, all ? ", complete" : "");
	}
	return all && any;
}

void HullHeat::GlobalInit ()
{
	GlobalExit();
	if (!Config->VolPlumes || !D3D9Effect::FX) return;
	const char *e = getenv("ORBITER_HULLHEAT_DISABLE"); // test hook (design Rev 2 M4), not a user setting
	if (e && e[0] && strcmp(e, "0")) { LogAlw("HullHeat: off (ORBITER_HULLHEAT_DISABLE)"); return; }
	hHeat = D3D9Effect::FX->GetParameterByName(0, "gHeat");
	if (!hHeat || ((VkConstHandle)hHeat)->size != sizeof(HeatPrm)) {
		LogErr("HullHeat: D3D9Client.fx gHeat missing or not %d bytes, hull heating off", int(sizeof(HeatPrm)));
		hHeat = nullptr;
		return;
	}
	table.Build(64, 500.0, 4000.0);
	states = new std::unordered_map<OBJHANDLE, State>();
	meshSup = new std::unordered_map<const D3D9Mesh *, MeshSup>();
	cfgs = new std::map<std::string, HeatCfg>();
	logLines = 0; steps = 0; stepMs = 0.0; stepN = 0;
	HeatPrm z;
	memset(&z, 0, sizeof(z));
	D3D9Effect::FX->SetValue(hHeat, &z, sizeof(z)); // review N12
	on = true;
	LogAlw("HullHeat: on, colour table 64 (500-4000 K), %d bins", NBIN);
}

void HullHeat::GlobalExit ()
{
	on = false;
	hHeat = nullptr;
	delete states; states = nullptr;
	delete meshSup; meshSup = nullptr;
	delete cfgs; cfgs = nullptr;
}

bool HullHeat::On ()
{
	return on;
}

void HullHeat::Step (vVessel *vv)
{
	if (!on || !vv) return;
	VESSEL *v = vv->GetInterface();
	OBJHANDLE h = vv->Object();
	if (!v || !h) return;
	auto t0 = std::chrono::steady_clock::now();
	double sys = oapiGetSysTime();
	if ((++steps & 1023) == 0) { // forget vessels gone or not stepped for 30 s (review S5)
		for (auto it = states->begin(); it != states->end();) {
			if (!oapiIsVessel(it->first) || sys - it->second.sys > 30.0) it = states->erase(it);
			else ++it;
		}
		for (auto it = meshSup->begin(); it != meshSup->end();) { // and support sets not queried for 30 s (review NIT 5)
			if (sys - it->second.used > 30.0) it = meshSup->erase(it);
			else ++it;
		}
	}
	State &s = (*states)[h];
	const char *cn = v->GetClassName() ? v->GetClassName() : "";
	if (s.v != v || s.cls != cn) { s = State(); s.v = v; s.cls = cn; } // a new vessel at a reused handle: no inherited heat or cfg (review S3)
	s.sys = sys;
	if (!s.cfgRead) {
		s.cfg = CfgOf(v);
		s.cfgRead = true;
		const D9BBox &hb = vv->HullBox;
		double L = std::max(hb.max.x - hb.min.x, std::max(hb.max.y - hb.min.y, hb.max.z - hb.min.z));
		if (!(L > 0.0)) L = 2.0 * v->GetSize();
		s.Rn = s.cfg.nose > 0.0 ? s.cfg.nose : std::clamp(Tune::nose * L, 0.25, 3.0);
	}
	if (s.cfg.off) { s.glow = false; return; }
	double simt = oapiGetSimTime(), dt = simt - s.t;
	bool fresh = !s.init || dt < 0.0 || dt > 600.0;
	if (!fresh && dt == 0.0) return; // the same sim time: another camera this frame
	double rho = v->GetAtmDensity(), V = v->GetAirspeed();
	OBJHANDLE hA = v->GetAtmRef();
	const ATMCONST *ac = (hA && rho > 0.0) ? oapiGetPlanetAtmConstants(hA) : nullptr;
	double R = ac ? ac->R : 287.0, gam = ac ? ac->gamma : 1.4; // review N7
	double cp = gam > 1.0 ? gam * R / (gam - 1.0) : 1004.5;
	double Tinf = rho > 0.0 ? v->GetAtmTemperature() : 0.0;
	Wall w = { s.cfg.emis, HeatCapacity(s.cfg.lag, s.cfg.emis), std::clamp(Tinf > 0.0 ? Tinf : 250.0, 150.0, 400.0) };
	double qcw = SuttonGraves(rho, s.Rn, V, SuttonGravesK(R));
	double h0 = cp * Tinf + 0.5 * V * V;
	if (!fresh && !(qcw > 0.0) && s.T[0] <= w.Tsink + 0.5) { s.t = simt; s.glow = false; return; } // cold and unheated
	for (int k = 0; k < NBIN; k++) {
		double G = BinRatio(k);
		s.T[k] = fresh ? EquilibriumWall(qcw, G, cp, h0, w) : StepWall(s.T[k], dt, qcw, G, cp, h0, w);
	}
	s.init = true;
	s.t = simt;
	s.kfp = KFlatPlate(V);
	if (!(qcw > 100.0) && s.T[0] < Tune::glowOn) { s.glow = false; return; } // no flow frame or support needed yet (orbit: q is a few W/m2)

	// flow frame and stagnation point, vessel axes, low-passed in sim time (review M3b, S3)
	VECTOR3 va;
	double fn[3] = { 0.0, 0.0, 0.0 };
	bool fv = v->GetAirspeedVector(FRAME_LOCAL, va) && length(va) >= 1.0;
	if (fv) { double l = length(va); for (int j = 0; j < 3; j++) fn[j] = -va.data[j] / l; }
	bool snap = fresh || !s.flowOk;
	double a = snap ? 1.0 : LowPass(dt, FlowTau(s.cfg.lag, s.T[0]));
	if (fv) {
		double f[3], l = 0.0;
		for (int j = 0; j < 3; j++) { f[j] = s.fl[j] + a * (fn[j] - s.fl[j]); l += f[j] * f[j]; }
		l = std::sqrt(l);
		if (snap || !(l > 1e-3)) for (int j = 0; j < 3; j++) s.fl[j] = fn[j];
		else for (int j = 0; j < 3; j++) s.fl[j] = f[j] / l;
	}
	if (fv || s.flowOk) {
		double up[3] = { -s.fl[0], -s.fl[1], -s.fl[2] }, p[3];
		if (!Upstream(vv, up, p)) { // the hull-box ellipsoid until the sets are done (design 2.5)
			const D9BBox &hb = vv->HullBox;
			double c[3] = { 0.5 * (hb.min.x + hb.max.x), 0.5 * (hb.min.y + hb.max.y), 0.5 * (hb.min.z + hb.max.z) };
			double A[3] = { 0.55 * (hb.max.x - hb.min.x), 0.55 * (hb.max.y - hb.min.y), 0.55 * (hb.max.z - hb.min.z) };
			double n = 0.0;
			for (int j = 0; j < 3; j++) n += A[j] * A[j] * up[j] * up[j];
			n = n > 0.0 ? 1.0 / std::sqrt(n) : 0.0;
			for (int j = 0; j < 3; j++) p[j] = c[j] + A[j] * A[j] * up[j] * n;
		}
		for (int j = 0; j < 3; j++) s.ps[j] = snap ? p[j] : s.ps[j] + a * (p[j] - s.ps[j]);
		s.flowOk = true;
	}
	s.glow = s.flowOk && s.T[0] >= Tune::glowOn;
	if (!s.glow) return;
	Edges(vv, s);

	double gain = Tune::camera * s.cfg.glow / LREF;
	for (int k = 0; k < NBIN; k++) {
		double rgb[3];
		table.Lookup(std::min(s.T[k], 4000.0), rgb); // review N6
		for (int j = 0; j < 3; j++) s.lnB[k][j] = (rgb[j] * gain > 0.0) ? float(std::max(-60.0, std::log(rgb[j] * gain))) : -60.0f;
	}
	if (!s.told) {
		s.told = true;
		LogAlw("HullHeat: %s (%s) glows: R_n %.2f m, eps %.2f, lag %.0f s, glow %.2f", v->GetName(), v->GetClassName(), s.Rn, s.cfg.emis, s.cfg.lag, s.cfg.glow);
	}
	if (simt >= s.nextLog && logLines < 60) {
		s.nextLog = simt + 2.0;
		logLines++;
		double Teq = EquilibriumWall(qcw, BinRatio(0), cp, h0, w);
		double eb = 0.0;
		for (const EdgeSeg &e : s.segs) eb = std::max(eb, e.level);
		LogAlw("HullHeat: %s t %.1f alt %.1f km V %.2f km/s AOA %.1f slip %.1f q %.0f kW/m2 Teq %.0f T %.0f/%.0f/%.0f/%.0f K (G 2/0.32/0.05/0.008) edges %d (G_e %.2f)", v->GetName(), simt,
			v->GetAltitude() * 1e-3, V * 1e-3, v->GetAOA() * DEG, v->GetSlipAngle() * DEG, qcw * 1e-3, Teq, s.T[0], s.T[2], s.T[4], s.T[6], int(s.segs.size()), eb);
	}
	stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	if (++stepN == 600) LogAlw("HullHeat: Step %.4f ms mean over 600 glowing steps", stepMs / 600.0);
}

// the attachment segments of the exterior meshes for the low-passed flow, again when it turned by 0.2 deg, after 2 s, or while edges build (Rev 3)
void HullHeat::Edges (vVessel *vv, State &s)
{
	double c = s.fl[0] * s.segFl[0] + s.fl[1] * s.segFl[1] + s.fl[2] * s.segFl[2];
	if (s.segAll && c > std::cos(0.2 * RAD) && s.t - s.segT < 2.0 && s.t >= s.segT) return;
	s.segs.clear();
	bool all = true;
	for (UINT i = 0; i < vv->nmesh; i++) {
		D3D9Mesh *m = vv->meshlist[i].mesh;
		if (!m || !m->IsOK() || !(vv->meshlist[i].vismode & MESHVIS_EXTERNAL)) continue;
		auto it = meshSup->find(m);
		if (it == meshSup->end() || !it->second.es.Done()) { all = false; continue; }
		D3DXMATRIX T;
		if (vv->meshlist[i].trans) T = *vv->meshlist[i].trans;
		else D3DXMatrixIdentity(&T);
		const float *R = &T._11;
		double f[3], l = 0.0;
		for (int j = 0; j < 3; j++) { f[j] = s.fl[0] * R[4 * j] + s.fl[1] * R[4 * j + 1] + s.fl[2] * R[4 * j + 2]; l += f[j] * f[j]; }
		l = std::sqrt(l);
		if (!(l > 0.0)) continue;
		for (double &x : f) x /= l;
		std::vector<EdgeSeg> o;
		it->second.es.Attach(f, s.Rn, o);
		for (EdgeSeg &e : o) {
			P3 a, b;
			Mul({ e.a[0], e.a[1], e.a[2] }, T, a);
			Mul({ e.b[0], e.b[1], e.b[2] }, T, b);
			double n[3], ln = 0.0;
			for (int j = 0; j < 3; j++) { n[j] = e.n[0] * R[j] + e.n[1] * R[4 + j] + e.n[2] * R[8 + j]; ln += n[j] * n[j]; }
			ln = std::sqrt(ln);
			for (int j = 0; j < 3; j++) { e.a[j] = a[j]; e.b[j] = b[j]; e.n[j] = ln > 0.0 ? n[j] / ln : 0.0; }
			s.segs.push_back(e);
		}
	}
	auto len = [](const EdgeSeg &e) { double d = 0.0; for (int j = 0; j < 3; j++) d += (e.b[j] - e.a[j]) * (e.b[j] - e.a[j]); return std::sqrt(d); };
	std::sort(s.segs.begin(), s.segs.end(), [&](const EdgeSeg &x, const EdgeSeg &y) { return x.level * len(x) > y.level * len(y); });
	if (s.segs.size() > size_t(HEAT_NSEG)) s.segs.resize(HEAT_NSEG);
	s.segAll = all;
	for (int j = 0; j < 3; j++) s.segFl[j] = s.fl[j];
	s.segT = s.t;
}

HullHeat::Scope::Scope (vVessel *vv, bool internal)
{
	if (!on || !hHeat || !vv || internal || !D3D9Effect::FX) return;
	Scene *scn = vv->GetScene();
	DWORD pass = scn ? scn->GetRenderPass() : 0;
	if (pass != RENDERPASS_MAINSCENE && pass != RENDERPASS_CUSTOMCAM && pass != RENDERPASS_ENVCAM) return;
	auto it = states->find(vv->Object());
	if (it == states->end() || !it->second.glow) return;
	const State &s = it->second;
	const D3DXMATRIX &M = *vv->MWorld();
	double f[3];
	for (int j = 0; j < 3; j++) f[j] = s.fl[0] * (&M._11)[j] + s.fl[1] * (&M._21)[j] + s.fl[2] * (&M._31)[j];
	double l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	if (!(l > 0.0) || !std::isfinite(l)) return;
	HeatPrm p;
	memset(&p, 0, sizeof(p));
	P3 ps;
	Mul({ s.ps[0], s.ps[1], s.ps[2] }, M, ps);
	for (int j = 0; j < 3; j++) { p.vFlow[j] = float(f[j] / l); p.vStag[j] = float(ps[j]); }
	p.vFlow[3] = 1.0f;
	p.vStag[3] = float(s.Rn);
	p.vCoef[0] = float(s.kfp);
	p.vCoef[1] = float(Tune::lee);
	p.vCoef[2] = float(Tune::epsVisMin);
	p.vCoef[3] = 0.0f;
	for (int k = 0; k < NBIN; k++) {
		for (int j = 0; j < 3; j++) p.vBin[k][j] = s.lnB[k][j];
		p.vBin[k][3] = float(s.T[k]);
	}
	int ne = 0;
	for (const EdgeSeg &e : s.segs) { // to the camera-relative render frame (Rev 3)
		if (ne >= HEAT_NSEG) break;
		P3 a, b;
		Mul({ e.a[0], e.a[1], e.a[2] }, M, a);
		Mul({ e.b[0], e.b[1], e.b[2] }, M, b);
		double n[3], ln = 0.0;
		for (int j = 0; j < 3; j++) { n[j] = e.n[0] * (&M._11)[j] + e.n[1] * (&M._21)[j] + e.n[2] * (&M._31)[j]; ln += n[j] * n[j]; }
		ln = std::sqrt(ln);
		if (!(ln > 0.0) || !std::isfinite(ln)) continue;
		for (int j = 0; j < 3; j++) {
			p.vEdge[3 * ne][j] = float(a[j]);
			p.vEdge[3 * ne + 1][j] = float(b[j]);
			p.vEdge[3 * ne + 2][j] = float(n[j] / ln);
		}
		p.vEdge[3 * ne][3] = float(e.w);
		p.vEdge[3 * ne + 1][3] = float(e.level);
		ne++;
	}
	p.vEdgeN[0] = float(ne);
	D3D9Effect::FX->SetValue(hHeat, &p, sizeof(p));
	set = true;
}

HullHeat::Scope::~Scope ()
{
	if (!set || !on || !hHeat || !D3D9Effect::FX) return;
	HeatPrm z;
	memset(&z, 0, sizeof(z));
	D3D9Effect::FX->SetValue(hHeat, &z, sizeof(z));
}
