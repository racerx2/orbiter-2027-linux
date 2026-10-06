// not upstream: vessel and building colliders (D1 3-5, 7): parts, sidecar, damage; Orbiter-free

#ifndef __COLLSHAPE_H
#define __COLLSHAPE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "CollGeom.h"
#include "CollAnim.h"

// group and sidecar rules (D1 3.2, 4.2, 4.4)
constexpr uint32_t COLL_FLAG_NORENDER = 0x02;      // UsrFlag bit: not rendered in the main pass, excluded unless INCLUDE
constexpr size_t   COLL_SIDECAR_MAX   = 1u << 20;  // bytes read of a .col file
constexpr uint32_t COLL_RANGE_MAX     = 65536;     // a GROUP range with more entries is rejected
inline constexpr char COLL_SIDECAR_HEADER[] = "COLLIDER-V1";

// material names interned by sidecar, COLLMAT, Damage::BaseMaterial; id 0 = vessel default; stable
uint16_t CollInternMaterial (const char *name);    // ASCII case-folded; same name, same id
const char *CollMaterialName (uint16_t id);        // "" for 0 or unknown ids

// one parsed sidecar (4.2)
struct CollSelector { enum Kind { ALL, GROUP, LABEL, MATERIAL, TEXTURE } kind; std::vector<std::pair<uint32_t,uint32_t>> range; std::vector<std::string> pat; };
struct CollSideRule { enum Op { EXCLUDE, INCLUDE, MAT } op; uint16_t mat; CollSelector sel; uint32_t line; };
struct CollSidecar {
	std::vector<CollSideRule> rule;              // file order
	double skin = -1, weld = -1;                 // -1 = default
	std::string mesh;                            // MESH replacement, "" = visual mesh
	std::vector<std::pair<std::vector<uint32_t>, uint32_t>> follow; // FOLLOW: collision groups -> visual group; at most COLL_RANGE_MAX groups per file
	bool needNames = false;                      // a LABEL/MATERIAL/TEXTURE selector is used
};
bool CollParseSidecar (const char *text, size_t len, const char *fname, CollSidecar &out, std::vector<std::string> &warn); // never fatal; one warning per skipped line

// name selectors (4.3): CollSource scans the .msh only if needNames, resolves the sidecar once
struct CollMeshTags { std::vector<std::string> label, material, texture; }; // per core group; MATERIAL/TEXTURE inherit from the previous group, group 0 "default"
bool CollScanMeshTags (const char *text, size_t len, CollMeshTags &out, std::vector<std::string> &warn); // Mesh.cpp:800-925 group rules, empty file groups dropped
bool CollResolveNames (CollSidecar &sc, const CollMeshTags *tags, uint32_t ngrp, std::vector<std::string> &warn); // names -> GROUP ranges; null tags or count mismatch: name rules dropped, one warning ("line N:")
void CollGroupRules (const CollSidecar *sc, const CollRestMesh &rm, std::vector<uint8_t> &on, std::vector<uint16_t> &mat); // per group: collides (FLAG 0x02 default, last EXCLUDE/INCLUDE wins), MAT id (0 default)

// shared per-mesh collider template and the session cache (3.4)
struct CollPartition { std::vector<uint32_t> part; std::vector<uint32_t> rep; uint64_t hash = 0; }; // group -> part; part -> rep group
struct CollMeshTpl {
	std::vector<CollGeom> part;                  // one geometry per part; CollPart::tpl aliases into it
	CollPartition map;                           // group -> part
	std::vector<uint16_t> mat; std::vector<uint8_t> on; // per group: material id, collides
};
struct CollTemplateCache {
	std::unordered_map<std::string, std::weak_ptr<const CollMeshTpl>> tpl;    // key + partition hash
	std::unordered_map<std::string, std::weak_ptr<const CollRestMesh>> rest;  // key only
};

// one vessel mesh slot as CollSource sees it (pure input, 3.6)
struct CollMeshInfo {
	bool present;                                // the client would hold a mesh here (6.2)
	bool collide;                                // external, loaded, not EXCLUDE ALL, collider on
	std::string key;                             // template key without partition; "" = private
	std::shared_ptr<const CollRestMesh> rest;    // CollSource::RestMesh
	std::shared_ptr<const CollSidecar> side;     // parsed sidecar (names resolved), may be null
	Vector ofs;                                  // mesh offset, vessel frame
	uint32_t serial;                             // bumps on each INSMESH of this slot
	std::shared_ptr<const CollRestMesh> coll;    // sidecar MESH geometry, null = rest (field added here, D1 4.2)
};

// one rigid part of a vessel collider (3.6)
struct CollPart {
	uint32_t mesh, rep;                          // vessel mesh index, representative group
	std::shared_ptr<const CollGeom> tpl;         // shared template geometry
	std::unique_ptr<CollGeom> own;               // private copy after the first dent
	CollAffine anim[2];                          // part rest frame -> mesh frame (F_rep, no offset) at t0, t1
	CollAffine pose[2];                          // part rest frame -> vessel frame at t0, t1 = Translate(current offset) o anim[i] (X2)
	Vector sc[2]; double sr[2];                  // bounding sphere in the vessel frame at t0, t1
	double motion;                               // CollPoseMotion over [t0, t1] [m]
	uint32_t version;                            // bumps when this part's geometry is replaced
	const CollGeom &Geom () const { return own ? *own : *tpl; }
};
enum : uint32_t { COLLSH_NONE = 1, COLLSH_BUILT = 2, COLLSH_REBUILT = 4, COLLSH_PARTITION = 8, COLLSH_JUMP = 16, COLLSH_MOVED = 32, COLLSH_OFFSET = 64 };

// displacement field callback; D4 passes DentMath's field with the record as ctx (7.3)
typedef Vector (*CollDisplaceFn) (const void *ctx, const Vector &rest);

// collider of one vessel: parts, poses at t0 and t1, bounds, damage API (3.6, 7.3)
class CollShape {
public:
	uint32_t Update (const CollMeshInfo *mi, uint32_t nmesh, const CollAnim &anim, const ANIMATION *a, uint32_t na,
		CollTemplateCache &cache);               // P1; returns COLLSH_* flags; rest, coll, side compared by pointer; no animations: empty CollAnim
	bool Replaced (uint32_t mesh) const;         // mesh parts built, rebuilt, re-templated or dropped at last Update: dents gone, CollSource re-adds
	void MarkJump ();                            // next Update: anim[0] = anim[1] for all parts (time jump)
	const Vector &OffsetChange (uint32_t mesh) const; // raw mesh offset change since the last Update, vessel frame (D2 1.7)
	uint32_t nPart () const; const CollPart &Part (uint32_t i) const;
	void Bound (int slot, Vector &c, double &r) const; // whole vessel, slot 0 = t0, 1 = t1, centre = vessel origin
	int  PartOf (uint32_t mesh, uint32_t grp) const;   // part index, -1 if the group has no collider
	void SetGroupHidden (uint32_t mesh, uint32_t grp, bool hidden); // indices >= COLL_RANGE_MAX ignored; a slot rebuild clears the mesh's flags (new client mesh)
	const uint8_t *GroupMask (uint32_t part) const;    // per srcTab index, nonzero = hidden (all if matrix singular); null: none; valid until next Update
	// damage API (7.3): mesh = vessel mesh index; vectors in the part rest frame (pre-animation mesh)
	CollGeom &MakePrivate (uint32_t part);                                                     // copy on write; rest = template vertices
	bool     RenderFeature (uint32_t part, uint32_t tri, uint32_t &mesh, uint32_t &grp, uint32_t &otri) const; // MESH replacement: grp = FOLLOW visual group or ~0u, otri ~0u
	bool     GroupPose (uint32_t mesh, uint32_t grp, CollAffine &F) const;         // animation transform at the last Update, no mesh offset
	double   PartRadius (uint32_t mesh, uint32_t grp) const;                       // part bounding radius [m], -1 if none
	uint16_t Material (uint32_t mesh, uint32_t grp) const;                         // sidecar MAT id, 0 = vessel default
	bool     RayRest (uint32_t mesh, uint32_t grp, const Vector &o, const Vector &d,
		double tmin, double tmax, CollRayHit &hit) const;                          // first hit over all parts at pose[1]; o, d in grp's part frame
	size_t   ApplyDent (uint32_t mesh, const uint32_t *grp, size_t ngrp,
		CollDisplaceFn fn, const void *ctx);                                       // vtx += fn(rest), then Refit; returns vertices moved; non-finite displacements skipped (log once)
	void     ResetDents (uint32_t mesh);                                           // repair: drop private copies of that mesh
private:
	struct MeshEntry {                           // one slot as last built (implementation may extend)
		bool collide = false; uint32_t serial = 0; std::string key;
		Vector ofs, dofs;                        // offset at the last Update, its raw change
		std::shared_ptr<const CollRestMesh> rest, coll;
		std::shared_ptr<const CollSidecar> side;
		std::shared_ptr<const CollMeshTpl> tpl;
		std::vector<uint32_t> part;              // template part -> shape part index (~0u: no geometry)
		bool valid = false;                      // slot seen at the last Update
		std::vector<uint32_t> cls;               // visual group -> pose class (equal signature and matrix, 3.3)
		std::vector<uint32_t> clsRep;            // pose class -> representative visual group, COLL_STATIC_REP = mesh transform only
		std::vector<CollAffine> clsF;            // pose class -> transform at the last Update
		std::vector<uint32_t> partCls;           // template part -> pose class
		std::vector<uint32_t> follow;            // MESH replacement: collision group -> visual group, ~0u = static
	};
	static constexpr uint32_t COLL_STATIC_REP = 0xFFFFFFFEu; // no component lists it, so CollAnim gives the mesh transform
	void Classify (MeshEntry &e, uint32_t mesh, const CollAnim &anim, const ANIMATION *a, uint32_t na, const uint8_t *present, uint32_t nmesh);
	bool Partition (const MeshEntry &e, CollPartition &map, std::vector<uint8_t> &on, std::vector<uint16_t> &mat, std::vector<uint32_t> &partCls) const;
	void UpdateMasks ();
	int  PartOfTpl (const MeshEntry &e, uint32_t grp) const;
	std::vector<MeshEntry> slots;
	std::vector<CollPart> parts;
	std::vector<std::vector<uint8_t>> masks;     // per part: per srcTab index, nonzero = hidden
	std::vector<std::vector<uint8_t>> hidden;    // per mesh: per group, SetGroupHidden
	std::vector<uint8_t> singular;               // per part: a pose matrix is singular (scale to zero), all groups masked
	std::vector<uint8_t> replaced;               // per mesh: Replaced () of the last Update
	uint64_t animVersion = ~0ull;                // CollAnim::Version at the last partition
	std::vector<uint8_t> present;                // CollMeshInfo::present at the last partition (propagation stops at missing meshes)
	uint32_t partSerial = 0;                     // source of part versions, unique per shape
	bool everBuilt = false, loggedNone = false;
	bool loggedSingular = false, loggedBadDent = false;
	bool jumpPending = false;
	double bound[2] = { 0, 0 };
	Vector zero;
};

// collider of one included base object: one part, base frame, derived from BaseGeom (5.3, Y13)
struct CollBaseObj {
	uint32_t obj;                      // object index in Base::obj (= BaseGeomObj::obj)
	uint32_t geomVersion = 0, topo = 0; // BaseGeomObj::version and ::topo followed, stored by CollBaseShape::Follow (SetObject leaves them)
	uint16_t mat = 0;                  // resolved material id (5.4 step 4)
	CollGeom geom;                     // welded double copy of BaseGeomObj::cur plus BVH; CollTri.otri = triangle index in BaseGeom group
};
// collider of one surface base, base frame (5.3); i = object slot in BaseGeom order
class CollBaseShape {
public:
	bool SetObject (uint32_t i, uint32_t obj, const CollGroupData *grp, size_t ngrp,
		uint16_t mat, bool topoChanged);               // Build on topology change, else move + Refit; skips undersh, index-less groups; i < COLL_RANGE_MAX
	void Follow (uint32_t i, uint32_t geomVersion, uint32_t topo); // SyncBase stores version and topo after SetObject; 5.4 step 1 compares obj, version and topo
	void Finish (const Vector &rpos, const Matrix &rrot, double rPlanet); // rmax, htop; version++ if any Build ran
	uint32_t nObj () const; const CollBaseObj &Obj (uint32_t i) const;
	bool     RenderFeature (uint32_t i, uint32_t tri, uint32_t &obj, uint32_t &grp, uint32_t &otri) const;
	uint16_t Material (uint32_t i) const;              // resolved at sync (5.4), never 0 after a sync
	double   rmax = 0, htop = 0;                       // farthest collider point from the base origin; highest point above the planet radius [m]
	uint32_t version = 0;                              // bumps when any object was rebuilt (D2 flushes caches of this base)
private:
	std::vector<CollBaseObj> objs;
	bool built = false;                                // a Build ran since the last Finish
	bool loggedBad = false;                            // non-finite input vertex logged once
	bool MoveObject (CollBaseObj &o, const CollGroupData *grp, size_t ngrp); // dent only: positions through the render refs
};
// Base::coll, the collider record of one base (Y13: no geometry of its own)
struct BaseColl {
	CollBaseShape shape;
	uint32_t geomVersion = 0;                          // BaseGeom::version at the last SyncBase
};

#endif // !__COLLSHAPE_H
