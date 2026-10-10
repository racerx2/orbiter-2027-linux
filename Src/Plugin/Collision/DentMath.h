// not upstream: dent math (D4): field, solve, caps, normals, refinement, record text; Orbiter-free

#ifndef __DENTMATH_H
#define __DENTMATH_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Vecmat.h"

// D4 values (2, 4, 5, 9)
constexpr double   DENT_VN_GATE        = 1.0;     // a dent needs event vn > this [m/s] (Y3')
constexpr double   DENT_V_EL           = 1.0;     // elastic speed of hull material [m/s] (3.1)
constexpr double   DENT_DESTROY_ENERGY = 1000.0;  // DestroyEnergy default [J/kg] (U2)
constexpr double   DENT_CATASTROPHIC   = 40000.0; // DMGF_CATASTROPHIC when one event's E_i / m reaches this [J/kg]
constexpr double   DENT_WF_SHARE       = 0.25;    // share of Wf added to each passing side's eabs (2.1 step 7)
constexpr double   DENT_KAPPA          = 0.3;     // h / R (4.2)
constexpr double   DENT_RMAX_SIZE      = 0.5;     // R_max: vessels this * Size, buildings this * rb
constexpr double   DENT_FLOOR          = 0.66;    // vessel low-poly floor R >= d4 / this
constexpr double   DENT_SLAB_LO        = 0.4;     // slab smoothstep from this * T (4.1)
constexpr double   DENT_SLAB_HI        = 0.8;     // to this * T
constexpr double   DENT_PLATE          = 0.5;     // plate mode when the slab ray hits nearer than this * R (4.3)
constexpr double   DENT_RAY_TMIN       = 0.01;    // slab ray start [m]
constexpr double   DENT_DMAX_T         = 0.6;     // D_max <= this * T (crush mode)
constexpr double   DENT_DMAX_R         = 0.5;     // D_max <= this * R (plate mode)
constexpr double   DENT_DMAX_L         = 0.25;    // D_max <= this * L
constexpr double   DENT_CAP_Q          = 0.05;    // cumulative cap over weights above this
constexpr double   DENT_MIN_DEPTH      = 0.002;   // h * max(q) below this: no record [m]
constexpr double   DENT_COALESCE_R     = 0.5;     // coalesce when |c - c_r| < this * R_r
constexpr double   DENT_COALESCE_ANGLE = 30.0;    // and n . n_r > cos(this) [deg]
constexpr uint32_t DENT_MAX_VESSEL     = 512;     // records per vessel after coalescing
constexpr uint32_t DENT_MAX_OBJECT     = 64;      // records per base object
constexpr uint32_t DENT_MAX_GRPLIST    = 65536;   // group list entries per record (= COLL_RANGE_MAX)
constexpr double   DENT_REFINE_EDGE    = 0.25;    // refinement target: longest rest edge <= this * R (4.9)
constexpr uint32_t DENT_REFINE_NEW     = 4096;    // new triangles per dent
constexpr uint32_t DENT_REFINE_MAX     = 65536;   // triangles per object
constexpr uint32_t DENT_MERGED_VTX     = 65535;   // vertices per merged structure group (WORD indices)
constexpr double   DENT_WELD           = 1e-4;    // weld map grid [m] (4.8)
constexpr int      DENT_LINE_MAX       = 200;     // damage text line incl. leading spaces (9.1)
constexpr int      DENT_EVENT_MAX      = 180;     // recorder event payload (10)
constexpr int      DENT_VERSION        = 1;       // XDMG version
constexpr double   DENT_LIM_POS        = 1e7;     // |c| per axis and OBJ x, z: larger values are never stored or read [m] (9.2)
constexpr double   DENT_LIM_R          = 1e4;     // R limit [m]; Solve gives no record above it
constexpr double   DENT_LIM_H          = 1e3;     // h limit [m]; Solve and CoalesceDepth cap h at it
constexpr double   DENT_LIM_T          = 1e6;     // T limit [m]; a slab ray hit beyond it counts as no hit
constexpr double   DENT_LIM_E          = 1e30;    // eabs limit [J]; writers clamp to it
constexpr double   DENT_CRUSH_RHO      = 0.30;    // dmg3: crush when the band coverage of the kernel disk is below this
constexpr double   DENT_CRUSH_BAND     = 0.15;    // dmg3: band |s| < this * R for the coverage
constexpr double   DENT_CRUSH_L        = 0.3;     // dmg3: crush plane depth P <= this * L
constexpr double   DENT_CRUSH_CORE     = 0.6;     // dmg3: flat core r_perp < this * R (k_flat = 1)
constexpr double   DENT_CRUSH_MOVE     = 0.25;    // dmg3: contact move along n <= this * R and <= P / 2
constexpr int      DENT_CRUSH_STEPS    = 48;      // dmg3: bisection steps for P
constexpr int      DENT_VERSION_X      = 2;       // dmg3: extension section version
enum : uint32_t { DENTM_BOWL = 0, DENTM_CRUSH = 1, DENTM_HINGE = 2, DENTM_CUT = 3, DENTM_VCUT = 4 }; // DentParams::mode; VCUT: blast Voronoi cell cut (P = cell, seed = site count)
enum : uint32_t { DENTI_CRUSH = 1, DENTI_HINGE = 2, DENTI_NOISE = 4 }; // DentInput::modes bits
enum : uint32_t { DENTB_LOBES = 1 };              // DentParams::bits
enum : uint32_t { DENTC_KEEP = 2 };               // dmg3 tear: cut bits, complement side (debris)
constexpr double   DENT_CUT_PINCH      = 0.4;     // dmg3 tear: rim pinch beta of a cut (format constant)
constexpr double   DENT_CRUSH_NOISE    = 0.15;    // dmg3: crush face noise amplitude limit [m]
constexpr double   DENT_HINGE_D        = 0.25;    // dmg3: plate bowl depth cap before a hinge, times the plate thickness
constexpr double   DENT_HINGE_TMAX     = 0.32;    // dmg3: hinge tan(theta/2) cap (35.5 deg)
constexpr double   DENT_HINGE_K        = 8.0;     // dmg3: thin-walled box factor on the solid-section plastic moment
constexpr double   DENT_LOBE_EDGE      = 1.5;     // dmg3: lobes only where the area-weighted median rest edge is below this [m]
constexpr double   DENT_NOISE_J        = 0.25;    // dmg3: bowl depth noise amplitude
constexpr uint32_t DENTR_NOREFINE      = 1;       // DentRecord::flags: the live refinement hit a cap, replay skips Refine (4.9 item 5)

// one dent in the rest frame of its target (4.1)
struct DentParams {
	Vector c;   // centre on the surface [m]
	Vector n;   // outward unit normal; displacement is -n
	double R;   // kernel radius [m]
	double h;   // depth scale [m]
	double T;   // slab thickness [m]; 0 = plate mode
	uint32_t mode = 0, seed = 0; // dmg3: DENTM_*; noise seed (0: none); mode 0 with seed 0 is the legacy field
	Vector t;                    // dmg3: unit tangent perpendicular to n
	double P = 0, hd = 0, hz = 0; // dmg3: crush plane depth [m]; hinge offsets (reserved)
	uint32_t bits = 0, pad = 0;  // dmg3: flags (reserved); pad keeps memcmp exact
};
// one dent record (vessel mesh or base object), quantised through %.9g as saved; fixed-width ints
struct DentRecord {
	DentParams p;              // rest frame of the target
	uint32_t slot;             // vessel mesh index, or base object index
	uint32_t key;              // vessel: FNV-1a 32 of the normalised mesh name; base: 0
	uint16_t ngrp; uint32_t nvtx; // vessel mesh signature with the key
	std::vector<uint16_t> grp; // groups it applies to; empty = all
	uint32_t flags = 0;        // DENTR_*: live refinement outcome (buildings; ODENT, BDENT); 0 for vessels and lines without it
};
// material table row (3.1); sigma_c in Pa; host: glass takes the host group's values
struct DentMaterial { const char *id; double sigma_c, t_cap, v_el, e0, vy, mu; bool host; };
enum { DENTB_BLOCK, DENTB_HANGAR, DENTB_HANGAR2, DENTB_HANGAR3, DENTB_TANK, DENTB_MESH }; // building classes for BuildingMass (5.1)

// plain-array geometry: DentVtx has the NTVERTEX layout, so Damage copies NTVERTEX arrays bytewise
struct DentVtx { float x, y, z, nx, ny, nz, tu, tv; };
static_assert (sizeof (DentVtx) == 32, "DentVtx must keep the NTVERTEX layout");
struct DentMeshView { const Vector *rest, *cur; size_t nv; const uint32_t *tri; size_t nt; }; // positions per vertex, 3 indices per triangle
struct DentObject {                                // one target's groups, rest frame (vessel mesh) or base frame (object)
	std::vector<std::vector<DentVtx>> rest, cur;   // per group
	std::vector<std::vector<uint16_t>> idx;        // per group, 3 per triangle
	std::vector<std::vector<uint32_t>> weld;       // per group: weld id per vertex
	uint32_t nweld = 0;                            // number of weld ids
};
struct DentViewData { std::vector<Vector> rest, cur; std::vector<uint32_t> tri; DentMeshView View () const; }; // owns the arrays of a DentMeshView
// inputs of one dent solve (4.2-4.5)
struct DentInput {
	double E;                  // energy for this side [J]
	const DentMaterial *mat;   // hit material (host values for glass)
	Vector c, n;               // centre and outward normal, rest frame
	double a;                  // contact patch radius [m]
	double Rmax;               // radius cap (4.2 step 2)
	double L;                  // Size (vessels) or rb (buildings) for the 0.25 L cap
	double rayT;               // slab ray hit distance along -n from c, < 0 = no hit (4.3)
	bool vessel;               // the low-poly floor applies (4.2 step 3); depth cap DmaxVessel (no t_cap)
	uint32_t modes = 0;        // dmg3: DENTI_* allowed (vessels); 0 = bowl only, as before
	int force = -1;            // dmg3: mode inherited from a coalesce partner, -1 none
	Vector tdir;               // dmg3: slip direction (unit, rest frame) or zero
	double vt = 0;             // dmg3: slip speed [m/s]
	uint32_t salt = 0;         // dmg3: record count, xored into the noise seed
	struct DentSolveX *x = nullptr; // dmg3 out: hinge record and surplus energy
};
struct DentSolveX { bool hinge = false; DentParams hp {}; double Esurplus = 0, H = 0, S = 0, Mp = 0; uint32_t cls = 0; }; // dmg3 Solve outputs
enum { DENT_OK = 0, DENT_SMALL = 1, DENT_NOSURFACE = 2, DENT_FLOOR_CAP = 3 }; // Solve: record, below 2 mm, S = 0 or beyond DENT_LIM_*; FLOOR_CAP: Damage's building cap only

// one vessel's damage as the text format sees it (9.2)
// one torn-off, broken or hidden group set of a vessel slot (dmg3 area P rows, area S line format)
struct DentTorn { uint8_t kind = 0; uint32_t slot = 0, key = 0; uint16_t ngrp = 0; uint32_t nvtx = 0; double simt = 0; std::string debris; std::vector<uint16_t> grp; bool kin = false; Vector dv, dw; double mass = 0;
	std::vector<uint32_t> cells, pieces; Vector c; bool crushed = false; }; // kin: dmg3 tear debris dv, dw, mass (recorder T); cells .. crushed: blast cell debris (recorder T cell payloads after the K one)
// one debris piece spawned from a vessel slot (dmg3 area P meaning, area S rows XDMGM B/Q and XDMGD B)
struct DentDebrisPose { Vector p; double q[4] = { 0, 0, 0, 1 }; std::vector<uint16_t> grp; }; // piece origin and rotation (x y z w) in the parent mesh frame
struct DentDebris { uint32_t id = 0, slot = 0, key = 0; uint16_t ngrp = 0; uint32_t nvtx = 0; double simt = 0, mass = 0; std::string name; std::vector<DentDebrisPose> pose; std::vector<DentRecord> rec; std::string other; }; // other: impactor name (B row O= token)
// blast: Voronoi sites of one vessel slot (rest frame of the static class), rounded through %.9g
struct DentSites { uint32_t slot = 0, key = 0; std::vector<Vector> s; };
// blast: VCUT records of one record list by cell (null: not in the set); on: MapVCut applies the keep set
struct DentVCut { const DentSites *s = nullptr; std::vector<const DentParams *> rm, keep; size_t nrm = 0, nkeep = 0; bool on = false; };
struct DentVesselText {
	double eabs = 0; uint32_t flags = 0;          // XDMG: 1 destroyed, 2 module handles effects, 4 catastrophic seen
	std::vector<DentRecord> rec;                  // application order
	std::vector<std::string> slotName;            // mesh name per slot for XDMGM (readers only); may be shorter than the slot count
	std::vector<std::string> verbatim;            // XDMG* lines of unknown-version sections, <= 200 characters, written before the version-1 lines
	std::vector<DentTorn> torn;                   // dmg3: parts gone (area P decides, area S saves)
	std::vector<DentDebris> debris;               // dmg3: live debris spawned from this vessel (area P rebuilds them on load)
	std::vector<DentSites> sites;                 // blast: Voronoi sites per slot (XDMGM S rows)
	std::vector<std::pair<uint32_t, std::vector<uint32_t>>> brokenBonds; // blast: slot -> broken bond indices (XDMGM K rows)
	std::vector<std::pair<uint32_t, std::vector<uint32_t>>> weakBonds; // blast: slot -> chunk key pair, remaining health x 1e6, ... (XDMGM W rows)
};
// building damage section (9.4)
struct DentBaseObjText { uint32_t index; std::string type; double x, z, eabs; uint32_t flags; }; // OBJ line
struct DentBaseText {
	std::string planet, name;                     // BASE <planet>:<name>
	uint32_t nameHash = 0;                        // BASEH <hash8> <planet> when planet:name does not fit the line
	std::vector<DentBaseObjText> obj;             // OBJ lines
	std::vector<DentRecord> rec;                  // ODENT lines: slot = object index, all groups, application order
};

namespace DentMath {
	// field (4.1)
	double Kernel (double t2);                                   // (1 - t2)^2 for t2 < 1, else 0
	double Weight (const DentParams &p, const Vector &rest);     // q = k * slab
	Vector Displace (const DentParams &p, const Vector &rest);   // D = -h * q * n
	Vector Field (const void *ctx, const Vector &rest);          // CollDisplaceFn for CollShape::ApplyDent: ctx = const DentRecord *
	// radius, slab, depth, caps (4.2-4.5)
	double Radius (double E, double sigma_c, double a, double Rmax); // V = E / sigma_c, R0 = cbrt(3V / (pi kappa)), R = min(max(R0, a), Rmax)
	double LowPolyFloor (const DentMeshView &m, const Vector &c); // d4 / 0.66 over distinct vertex positions
	bool   RayCast (const DentMeshView &m, const Vector &o, const Vector &d, double tmin, double tmax, double &t); // first hit on rest triangles (buildings, 4.3)
	double SlabT (double rayT, double R);                         // T: 0 (plate) without a hit, below 0.5 R or beyond DENT_LIM_T, else rayT
	double VolumeFactor (const DentParams &p, const DentMeshView &m); // S of 4.4 on current front faces; coincident faces (0.1 mm grid) once; h = V / S
	double Dmax (double tcap, double T, double R, double L);      // min(t_cap, T > 0 ? 0.6 T : 0.5 R, 0.25 L)
	double DmaxVessel (double T, double R, double L);             // min(T > 0 ? 0.6 T : inf, 0.5 R, 0.25 L): vessel crush, no t_cap
	double DepthCap (const DentParams &p, const DentMeshView &m, double Dmax); // cumulative cap: min over q > 0.05 (none there: q > 0) of (Dmax - u) / q
	double DepthCap (const DentParams &p, const DentMeshView &m, const DentMeshView *extra, double Dmax); // extra: more vertices for the cap only (no triangles needed)
	int    Solve (const DentInput &in, const DentMeshView &m, DentParams &out); // 4.2-4.5 in order; vessel floor wins over Rmax; DENT_*
	int    Solve (const DentInput &in, const DentMeshView &m, const DentMeshView *cap, DentParams &out); // cap: extra vertices for DepthCap only
	int    FindCoalesce (const std::vector<DentRecord> &rec, const DentRecord &r); // record of the same target and partition to grow (4.5), -1 if none
	int    FindCoalesce (const std::vector<DentRecord> &rec, const DentRecord &r, const std::vector<uint8_t> *key); // key[g]: group g counts in the partition compare (null: all)
	int    FindCoalesce (const std::vector<DentRecord> &rec, const DentRecord &r, const std::vector<uint8_t> *key, bool anyMode); // dmg3: anyMode: partner of any mode (mode reuse)
	double Coverage (const DentParams &p, const DentMeshView &m); // dmg3: band coverage of the kernel disk on rest front faces
	double CrushW (const DentParams &p, const DentMeshView &m);   // dmg3: P * VolumeFactor at h = P
	int    SolveCrush (const DentInput &in, const DentMeshView &m, const DentMeshView *cap, DentParams &out); // dmg3: crush from out's c, n, R
	bool   CoalesceCrush (const DentParams &old, double V, const DentMeshView &m, const DentMeshView *cap, double L, double &P, double &h); // dmg3: grown P and h
	double DmaxCrush (double L);                                  // dmg3: DENT_CRUSH_L * L
	double MaxDisplace (const DentParams &p, const DentMeshView &m); // dmg3: max |D| over the view's rest vertices
	Vector DisplaceLow (const DentParams &p, const Vector &rest); // dmg3: Displace without noise and lobes (collider)
	double CutJag (const DentParams &p, const Vector &x, bool full); // dmg3 tear: jag J at x (0 when not full)
	Vector CutMap (const DentParams &p, const Vector &cur, bool full); // dmg3 tear: mode 3 map of a current position
	Vector Fold (const DentParams *const *rec, size_t n, const Vector &rest, bool full, bool *cut = nullptr); // dmg3 tear: displacement of records in order, cuts as maps; cut: a cut moved it
	Vector Fold (const std::vector<DentRecord> &rec, int g, const Vector &rest, bool full); // records listing group g (g < 0: all)
	Vector Fold (const DentParams *const *rec, size_t n, const Vector &rest, bool full, bool *cut, const DentSites *sites); // blast: VCUT records with the slot's sites (null: no effect)
	Vector Fold (const DentParams *const *rec, size_t n, const Vector &rest, bool full, bool *cut, const DentVCut *vc); // blast: vc from VCutSet of the same list
	Vector Fold (const std::vector<DentRecord> &rec, int g, const Vector &rest, bool full, const DentSites *sites); // blast: records listing group g with sites
	void   VCutSet (const DentParams *const *rec, size_t n, const DentSites *s, DentVCut &out); // blast: VCUT cells of the list whose seed is the site count
	void   VCutSet (const std::vector<const DentRecord *> &rec, const DentSites *s, DentVCut &out); // blast: same from records
	size_t Nearest (const DentSites &s, const Vector &x);          // blast: nearest site, lowest index on ties
	Vector VCutMap (const DentVCut &v, bool keep, const Vector &rest, const Vector &cur, bool full); // blast: cells by rest; cur of removed cells to the bisector with the nearest kept site; keep: the complement
	Vector MapVCut (const void *ctx, const Vector &rest, const Vector &cur); // blast: CollMapFn of VCutMap low, ctx = const DentVCut * (on = keep set)
	bool   IsCut (uint32_t mode);                                 // blast: DENTM_CUT or DENTM_VCUT
	Vector MapLow (const void *ctx, const Vector &rest, const Vector &cur); // dmg3 tear: CollMapFn of CutMap low, ctx = const DentRecord *
	bool   HasCut (const std::vector<DentRecord> &rec, int g); // dmg3 tear: a mode-3 record lists group g (g < 0: any)
	Vector FieldLow (const void *ctx, const Vector &rest);       // dmg3: CollDisplaceFn of DisplaceLow
	double Noise (uint32_t seed, double u, double w);            // dmg3: smooth lattice noise in [-1, 1]
	uint32_t Classify (const DentParams &p, const DentMeshView &m, double &H, Vector &a); // dmg3: DENTM_* for the rest view at c, n, R; H plate thickness, a hinge axis
	double PlateThickness (const DentMeshView &m, const Vector &c, const Vector &n, double R); // dmg3: first back-facing hit along -n, 0 none
	double HingeMp (const DentParams &p, const DentMaterial &mat); // dmg3: K sigma_c b H^2 / 4 with b = 2R, H = 2 hz
	bool   LobeGate (const DentParams &p, const DentMeshView &m);  // dmg3: area-weighted median edge within R below DENT_LOBE_EDGE
	void   MapToRest (const std::vector<const DentParams *> &rec, Vector &c, Vector &n); // dmg3: hit on deformed parts back to rest (mode >= 1 records)
	Vector Tangent (const Vector &n, const Vector &tdir, double vt); // dmg3: tdir across n when vt > 0.5, else n x least aligned axis
	uint32_t ParamsHash (const DentParams &p);                    // dmg3: Fnv1a of the %.9g text of c, n, R
	bool   Legacy (const DentParams &p);                          // dmg3: mode 0 and seed 0
	double CoalesceDepth (const DentParams &old, double V, const DentMeshView &m, double Dmax); // depth added on old's kernel for volume V, capped (old.h + it <= DENT_LIM_H)
	double CoalesceDepth (const DentParams &old, double V, const DentMeshView &m, const DentMeshView *cap, double Dmax); // cap: extra vertices for DepthCap only
	void   Quantise (DentParams &p);                              // through %.9g, as saved (6)
	// geometry on plain arrays (4.8, 4.9, 13.3, 13.4)
	void   MakeView (const DentObject &o, DentViewData &d);       // flattens all groups (vertices not welded)
	size_t Apply (const DentParams &p, const std::vector<std::vector<DentVtx>> &rest, std::vector<std::vector<DentVtx>> &cur,
		const uint16_t *grp, size_t ngrp, std::vector<std::vector<uint8_t>> *dirty); // cur += Displace(rest) on listed groups (all if ngrp 0); vertices moved
	size_t Apply (const DentParams &p, const std::vector<std::vector<DentVtx>> &rest, std::vector<std::vector<DentVtx>> &cur,
		const uint16_t *grp, size_t ngrp, std::vector<std::vector<uint8_t>> *dirty, const DentVCut *vc); // blast: a VCUT record applies vc's set of its kind
	void   FormatSites (const DentSites &s, const std::string &ind, std::vector<std::string> &lines); // blast: XDMGM S rows (continued with a trailing ',')
	void   FormatBonds (uint32_t slot, const std::vector<uint32_t> &b, const std::string &ind, std::vector<std::string> &lines, const char *tag = "K"); // blast: XDMGM K rows (W: weakened bonds)
	void   FormatSitesEvent (const DentSites &s, std::vector<std::string> &payload); // blast: recorder V payloads <slot> <key8> <n> <first> x y z ..., each <= 180
	bool   ParseSitesEvent (const char *payload, DentSites &s, uint32_t &n, uint32_t &first); // blast: s.s holds this payload's sites only
	void   FormatBondsEvent (uint32_t slot, const std::vector<uint32_t> &b, std::vector<std::string> &payload); // blast: recorder K payloads
	bool   ParseBondsEvent (const char *payload, uint32_t &slot, std::vector<uint32_t> &b, bool &more);
	uint32_t WeldMap (const std::vector<std::vector<DentVtx>> &v, double tol, std::vector<std::vector<uint32_t>> &weld); // grid over all groups; returns the id count
	uint32_t WeldMap (const std::vector<std::vector<DentVtx>> &v, double tol, std::vector<std::vector<uint32_t>> &weld, const std::vector<uint32_t> *cls); // cls[g]: only groups of one class weld
	void   FaceNormalSums (const std::vector<std::vector<DentVtx>> &v, const std::vector<std::vector<uint16_t>> &idx,
		const std::vector<std::vector<uint32_t>> &weld, uint32_t nweld, std::vector<Vector> &sum); // per weld id, area weighted, guarded
	void   Normals (const std::vector<std::vector<DentVtx>> &rest, const std::vector<Vector> &restSum, const std::vector<std::vector<uint16_t>> &idx,
		const std::vector<std::vector<uint32_t>> &weld, uint32_t nweld, const std::vector<uint8_t> &touched, std::vector<std::vector<DentVtx>> &cur,
		const std::vector<uint8_t> *facet = nullptr); // Rodrigues per touched weld id; dmg3 facet[w]: crease rule for ids moved by a crush or hinge
	int    Refine (DentObject &o, const Vector &c, double R, const DentParams *prior, size_t nprior, const uint32_t *vtxRoom); // LEPP to edge <= R/4 in the Quantise'd ball; new cur = rest + prior; tris added, -1 = cap hit
	int    Refine (DentObject &o, const Vector &c, double R, const DentParams *prior, size_t nprior, const uint32_t *vtxRoom,
		const uint32_t *roomOf, std::vector<uint8_t> *split); // roomOf[g]: vtxRoom slot of group g (null: g); split: per weld id, 1 on a split triangle
	// energy and destroyed state (2.1, 5.1)
	double PlasticFactor (double vn, double v_el);                // max(0, 1 - (v_el / vn)^2)
	bool   SplitEnergy (double dKE, double Wf, double vn, bool first, const DentMaterial &a, const DentMaterial &b,
		double E[2], double eabs[2]);                             // 2.1 steps 2-7; false: an event without FIRST carries energy (Damage asserts in debug)
	bool   Destroyed (double eabs, double mEmpty, double mTotal, double epsD); // eabs / m >= epsD; m = mEmpty, or mTotal if 0
	double BuildingMass (int cls, const Vector &size);            // rho_env * V_env; size = SCALE (primitives) or the rest box in object axes (MESH)
	const DentMaterial *FindMaterial (const char *id);            // table 3.1, ASCII case-insensitive; null if unknown
	const DentMaterial &DefaultMaterial (int cls);                // al_structure for vessels (cls < 0), else by DENTB_* class (3.2)
	// keys and text (7.3, 9, 10)
	uint32_t Fnv1a (const char *s, size_t n);                     // FNV-1a 32
	uint32_t MeshKey (const char *name);                          // lower case, '\\' -> '/', "#" if null or empty
	bool   NameHash (const std::string &name, uint32_t &h);       // a "#<fnv8>" name written for one too long for its row: true and the hash
	void   FormatVessel (const DentVesselText &v, const char *indent, std::vector<std::string> &lines); // XDMG, XDMGM, XDMGD; every line <= 200 incl. indent
	void   FormatBases (const std::vector<DentBaseText> &b, std::vector<std::string> &lines); // BEGIN_XDMG_BASES ... END_XDMG_BASES
	void   FormatDentEvent (const DentRecord &r, std::vector<std::string> &payload); // DENT payloads, groups split (continued lists end in ','), each <= 180
	bool   ParseDentEvent (const char *payload, DentRecord &r);  // a continued payload gives its own groups only; playback uses the overload below
	bool   ParseDentEvent (const char *payload, DentRecord &r, bool &more); // more: the group list continues in the next DENT payload (same slot and numbers)
	std::string FormatBaseDentEvent (int planet, int base, int obj, const DentParams &p); // BDENT payload
	std::string FormatBaseDentEvent (int planet, int base, int obj, const DentParams &p, uint32_t flags); // with DentRecord::flags (written if nonzero)
	bool   ParseBaseDentEvent (const char *payload, int &planet, int &base, int &obj, DentParams &p);
	bool   ParseBaseDentEvent (const char *payload, int &planet, int &base, int &obj, DentParams &p, uint32_t &flags);
	std::string FormatExtEvent (uint32_t recidx, const DentParams &p, double E, double vn, double vt, int hit = -1, uint32_t evflags = 0); // dmg3: recorder X payload
	bool   ParseExtEvent (const char *payload, uint32_t &recidx, uint32_t &h8, DentParams &ext, double &E, double &vn, double &vt, int *hit = nullptr, uint32_t *evflags = nullptr); // hit: -1 when the line has no flag token
	void   FormatTornEvent (const DentTorn &t, std::vector<std::string> &payload); // dmg3: recorder T payloads, groups split like D
	bool   ParseTornEvent (const char *payload, DentTorn &t, bool &more);
	void   FormatDebris (const DentDebris &d, const std::string &ind, std::vector<std::string> &lines); // dmg3: XDMGM B, XDMGM Q, XDMGD B rows
	void   ApplyExt (DentParams &p, const DentParams &ext);       // dmg3: copies mode, seed, t, P, hd, hz, bits
	std::string FormatStateEvent (double eabs, uint32_t flags);   // DMGSTATE payload; BDMG = "<planet>:<base>:<obj> " + this
	bool   ParseStateEvent (const char *payload, double &eabs, uint32_t &flags);
}

// tolerant parser of a vessel block's XDMG* lines (9.2, 9.3); one section per version, from XDMG
class DentVesselParser {
public:
	bool Line (const char *line);        // true: an XDMG, XDMGM or XDMGD line (consumed); whole keyword, case-insensitive
	void Finish (DentVesselText &out);   // resolves keys at the end of the block; unknown-version sections: lines kept verbatim
	int  Skipped () const;               // bad lines skipped (Damage logs one line per vessel)
	bool Capped () const;                // records past DENT_MAX_VESSEL or a group list past DENT_MAX_GRPLIST were dropped
private:
	void V1 (const std::string &line);   // one XDMGM or XDMGD line of the version-1 section
	void Close ();                       // a group list left open (trailing ','): its record is dropped
	void Keep (const std::string &line); // one line of an unknown-version section
	int m_sec = 0;                                        // section of next lines: 0 none yet, 1 version 1, 2 unknown version, 3 unknown, XDMG line over 200
	bool m_head = false;                                  // a valid XDMG 1 line was read
	double m_eabs = 0; uint32_t m_flags = 0;
	std::vector<std::pair<uint32_t, DentRecord>> m_mesh;  // XDMGM: key k -> slot, key, ngrp, nvtx
	std::unordered_map<uint32_t, size_t> m_meshIdx;       // key k -> index in m_mesh
	std::vector<std::pair<uint32_t, DentRecord>> m_dent;  // XDMGD: key k -> params and groups
	size_t m_nKnown = 0;                                  // m_dent records whose key is in m_meshIdx: the parse-time cap counts only these
	std::vector<std::string> m_pending;                   // lines before the first XDMG line; they join its section
	std::vector<std::string> m_lines;                     // unknown-version lines up to 200 characters, verbatim
	std::vector<std::pair<uint32_t, std::string>> m_names; // XDMGM slot -> mesh name (readers only)
	int m_skipped = 0;                                    // bad lines (version 1 rules)
	int m_long = 0;                                       // unknown-version lines dropped (over 200 characters)
	int m_result = -1;                                    // final count after Finish
	void V2 (const std::string &line);                    // dmg3: one line of the extension section
	struct Ext { uint32_t j, h8; DentParams p; };
	std::vector<Ext> m_ext;                               // dmg3: XDMGD rows of section 2
	std::vector<DentTorn> m_torn;                         // dmg3: XDMGM T rows
	std::vector<DentSites> m_sites;                       // blast: XDMGM S rows
	std::vector<std::pair<uint32_t, std::vector<uint32_t>>> m_bonds; // blast: XDMGM K rows
	std::vector<std::pair<uint32_t, std::vector<uint32_t>>> m_weak; // blast: XDMGM W rows
	int m_sOpen = 0;                                      // blast: 1 the last S row continues, 2 the last K row continues, 3 the last W row
	uint32_t m_sLeft = 0;                                 // blast: sites still expected by the open S row
	std::vector<DentDebris> m_debris;                     // dmg3: XDMGM B, Q and XDMGD B rows
	int m_qOpen = 0;                                      // dmg3: 1 the last Q row continues, 2 the last XDMGD B row continues
	bool m_tornOpen = false;                              // dmg3: the last torn row's group list continues
	bool m_x2 = false; uint32_t m_xn = 0, m_xh = 0; double m_xe = 0; // dmg3: XDMG 2 header
	std::vector<uint32_t> m_ord;                          // dmg3: written ordinal of each m_dent entry
	uint32_t m_nord = 0;                                  // dmg3: record starts seen in section 1
	bool m_open = false;                                  // the last version-1 record's group list ended in ',' (continued on the next XDMGD)
	bool m_over = false;                                  // the open record's group list passed DENT_MAX_GRPLIST: dropped when it ends
	bool m_capped = false;                                // Capped ()
};
// tolerant parser of the BEGIN_XDMG_BASES section, fed the lines after its BEGIN line (9.4)
class DentBasesParser {
public:
	bool Line (const char *line);        // false once END_XDMG_BASES was read
	void Finish (std::vector<DentBaseText> &out);
	int  Skipped () const;
private:
	std::vector<DentBaseText> m_base;
	std::unordered_map<uint32_t, uint32_t> m_nrec;        // ODENT records per object index of the current base
	bool m_inBase = false, m_done = false;
	int m_skipped = 0;
};

#endif // !__DENTMATH_H
