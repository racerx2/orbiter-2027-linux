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
constexpr uint32_t DENTR_NOREFINE      = 1;       // DentRecord::flags: the live refinement hit a cap, replay skips Refine (4.9 item 5)

// one dent in the rest frame of its target (4.1)
struct DentParams {
	Vector c;   // centre on the surface [m]
	Vector n;   // outward unit normal; displacement is -n
	double R;   // kernel radius [m]
	double h;   // depth scale [m]
	double T;   // slab thickness [m]; 0 = plate mode
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
};
enum { DENT_OK = 0, DENT_SMALL = 1, DENT_NOSURFACE = 2, DENT_FLOOR_CAP = 3 }; // Solve: record, below 2 mm, S = 0 or beyond DENT_LIM_*; FLOOR_CAP: Damage's building cap only

// one vessel's damage as the text format sees it (9.2)
// one torn-off, broken or hidden group set of a vessel slot (dmg3 area P rows, area S line format)
struct DentTorn { uint8_t kind = 0; uint32_t slot = 0, key = 0; uint16_t ngrp = 0; uint32_t nvtx = 0; double simt = 0; std::string debris; std::vector<uint16_t> grp; };
struct DentVesselText {
	double eabs = 0; uint32_t flags = 0;          // XDMG: 1 destroyed, 2 module handles effects, 4 catastrophic seen
	std::vector<DentRecord> rec;                  // application order
	std::vector<std::string> slotName;            // mesh name per slot for XDMGM (readers only); may be shorter than the slot count
	std::vector<std::string> verbatim;            // XDMG* lines of unknown-version sections, <= 200 characters, written before the version-1 lines
	std::vector<DentTorn> torn;                   // dmg3: parts gone (area P decides, area S saves)
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
	double CoalesceDepth (const DentParams &old, double V, const DentMeshView &m, double Dmax); // depth added on old's kernel for volume V, capped (old.h + it <= DENT_LIM_H)
	double CoalesceDepth (const DentParams &old, double V, const DentMeshView &m, const DentMeshView *cap, double Dmax); // cap: extra vertices for DepthCap only
	void   Quantise (DentParams &p);                              // through %.9g, as saved (6)
	// geometry on plain arrays (4.8, 4.9, 13.3, 13.4)
	void   MakeView (const DentObject &o, DentViewData &d);       // flattens all groups (vertices not welded)
	size_t Apply (const DentParams &p, const std::vector<std::vector<DentVtx>> &rest, std::vector<std::vector<DentVtx>> &cur,
		const uint16_t *grp, size_t ngrp, std::vector<std::vector<uint8_t>> *dirty); // cur += Displace(rest) on listed groups (all if ngrp 0); vertices moved
	uint32_t WeldMap (const std::vector<std::vector<DentVtx>> &v, double tol, std::vector<std::vector<uint32_t>> &weld); // grid over all groups; returns the id count
	uint32_t WeldMap (const std::vector<std::vector<DentVtx>> &v, double tol, std::vector<std::vector<uint32_t>> &weld, const std::vector<uint32_t> *cls); // cls[g]: only groups of one class weld
	void   FaceNormalSums (const std::vector<std::vector<DentVtx>> &v, const std::vector<std::vector<uint16_t>> &idx,
		const std::vector<std::vector<uint32_t>> &weld, uint32_t nweld, std::vector<Vector> &sum); // per weld id, area weighted, guarded
	void   Normals (const std::vector<std::vector<DentVtx>> &rest, const std::vector<Vector> &restSum, const std::vector<std::vector<uint16_t>> &idx,
		const std::vector<std::vector<uint32_t>> &weld, uint32_t nweld, const std::vector<uint8_t> &touched, std::vector<std::vector<DentVtx>> &cur); // Rodrigues per touched weld id
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
	void   FormatVessel (const DentVesselText &v, const char *indent, std::vector<std::string> &lines); // XDMG, XDMGM, XDMGD; every line <= 200 incl. indent
	void   FormatBases (const std::vector<DentBaseText> &b, std::vector<std::string> &lines); // BEGIN_XDMG_BASES ... END_XDMG_BASES
	void   FormatDentEvent (const DentRecord &r, std::vector<std::string> &payload); // DENT payloads, groups split (continued lists end in ','), each <= 180
	bool   ParseDentEvent (const char *payload, DentRecord &r);  // a continued payload gives its own groups only; playback uses the overload below
	bool   ParseDentEvent (const char *payload, DentRecord &r, bool &more); // more: the group list continues in the next DENT payload (same slot and numbers)
	std::string FormatBaseDentEvent (int planet, int base, int obj, const DentParams &p); // BDENT payload
	std::string FormatBaseDentEvent (int planet, int base, int obj, const DentParams &p, uint32_t flags); // with DentRecord::flags (written if nonzero)
	bool   ParseBaseDentEvent (const char *payload, int &planet, int &base, int &obj, DentParams &p);
	bool   ParseBaseDentEvent (const char *payload, int &planet, int &base, int &obj, DentParams &p, uint32_t &flags);
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
