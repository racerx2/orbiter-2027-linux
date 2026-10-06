// not upstream: replica of the client's incremental animation mode, so collider poses match render

#include <cmath>
#include "CollAnim.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif
#include "OrbiterAPI.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static_assert (COLLANIM_LVL == LOCALVERTEXLIST, "COLLANIM_LVL must equal LOCALVERTEXLIST");
static_assert ((int)COLLANIM_NULL == (int)MGROUP_TRANSFORM::NULLTRANSFORM && (int)COLLANIM_ROTATE == (int)MGROUP_TRANSFORM::ROTATE &&
	(int)COLLANIM_TRANSLATE == (int)MGROUP_TRANSFORM::TRANSLATE && (int)COLLANIM_SCALE == (int)MGROUP_TRANSFORM::SCALE, "COLLANIM_* must equal MGROUP_TRANSFORM::TYPE");

static Vector Vec (const VECTOR3 &v) { return Vector (v.x, v.y, v.z); }

static bool Finite (const CollAffine &T)
{
	for (int i = 0; i < 9; i++) if (!std::isfinite (T.A.data[i])) return false;
	return std::isfinite (T.t.x) && std::isfinite (T.t.y) && std::isfinite (T.t.z);
}

// splitmix64 finaliser: per-component term of the multiset hash (3.3)
static uint64_t Mix (uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

// client's quaternion rotation, axis as stored (D3D9Util.cpp:835-860), about ref; column form
static CollAffine RotateT (const Vector &axis, double angle, const Vector &ref)
{
	angle *= 0.5;
	double w = cos (angle), sina = sin (angle);
	double x = sina * axis.x, y = sina * axis.y, z = sina * axis.z;
	double xx = x*x, yy = y*y, zz = z*z;
	double xy = x*y, xz = x*z, yz = y*z;
	double wx = w*x, wy = w*y, wz = w*z;
	CollAffine T;
	T.A = Matrix (1 - 2 * (yy+zz),     2 * (xy-wz),     2 * (xz+wy),
	                  2 * (xy+wz), 1 - 2 * (xx+zz),     2 * (yz-wx),
	                  2 * (xz-wy),     2 * (yz+wx), 1 - 2 * (xx+yy));
	double dx = ref.x, dy = ref.y, dz = ref.z;
	T.t.x = dx - T.A.m11*dx - T.A.m12*dy - T.A.m13*dz;
	T.t.y = dy - T.A.m21*dx - T.A.m22*dy - T.A.m23*dz;
	T.t.z = dz - T.A.m31*dx - T.A.m32*dy - T.A.m33*dz;
	return T;
}

void CollAnim::OnAdd (const ANIMATIONCOMP *ac)
{
	if (!ac) return;
	CollAnimComp c;
	const MGROUP_TRANSFORM *tr = ac->trans;
	c.angle = 0.0;
	if (!tr) {
		// module bug: a tree node without transform or groups (6.2)
		c.type = COLLANIM_NULL; c.mesh = COLLANIM_LVL; c.wholeMesh = false;
		if (!logMissing) { logMissing = true; CollLog (COLLLOG_WARN, "CollAnim: animation component without transform, used as an empty node"); }
	} else {
		c.type = (int)tr->Type ();
		c.mesh = tr->mesh;
		c.wholeMesh = (tr->mesh != COLLANIM_LVL && !tr->grp);
		if (tr->mesh != COLLANIM_LVL && tr->grp) c.grp.assign (tr->grp, tr->grp + tr->ngrp);
		switch (c.type) {
		case COLLANIM_ROTATE: {
			const MGROUP_ROTATE *rot = static_cast<const MGROUP_ROTATE*>(tr);
			c.ref = Vec (rot->ref); c.axis = Vec (rot->axis); c.angle = rot->angle;
			if (c.axis.x == 0 && c.axis.y == 0 && c.axis.z == 0 && !logZeroAxis) {
				logZeroAxis = true;
				CollLog (COLLLOG_WARN, "CollAnim: rotation with a zero axis (identity, as the client)");
			}
			} break;
		case COLLANIM_TRANSLATE:
			c.shift = Vec (static_cast<const MGROUP_TRANSLATE*>(tr)->shift);
			break;
		case COLLANIM_SCALE: {
			const MGROUP_SCALE *scl = static_cast<const MGROUP_SCALE*>(tr);
			c.ref = Vec (scl->ref); c.scale = Vec (scl->scale);
			} break;
		default:
			c.type = COLLANIM_NULL;
			break;
		}
	}
	work[ac] = std::move (c);
	ver++;
}

void CollAnim::OnDel (const ANIMATIONCOMP *ac)
{
	if (work.erase (ac)) ver++;
}

void CollAnim::OnClear ()
{
	work.clear ();
	ver++;
}

void CollAnim::OnMeshInsert (uint32_t mesh)
{
	M.erase (mesh);
	G.erase (G.lower_bound (std::make_pair (mesh, 0u)), G.upper_bound (std::make_pair (mesh, ~0u)));
}

void CollAnim::OnMeshDelete (uint32_t mesh)
{
	if (mesh == COLLANIM_LVL) { M.clear (); G.clear (); return; }
	OnMeshInsert (mesh);
}

bool CollAnim::Step (const ANIMATION *anim, uint32_t nanim, const uint8_t *present, uint32_t nmesh)
{
	bool changed = false;
	// new ids start at defstate (VVessel.cpp:531)
	for (uint32_t i = (uint32_t)cur.size(); i < nanim; i++) cur.push_back (anim[i].defstate);
	for (uint32_t an = 0; an < nanim; an++) {
		const ANIMATION &A = anim[an];
		if (A.state == cur[an]) continue;
		if (!std::isfinite (A.state)) {
			if (!logNaN) { logNaN = true; CollLog (COLLLOG_WARN, "CollAnim: animation %u has a non-finite state, skipped", an); }
			continue;
		}
		if (!std::isfinite (cur[an])) {
			if (!logNaN) { logNaN = true; CollLog (COLLLOG_WARN, "CollAnim: animation %u starts from a non-finite state, not animated", an); }
			cur[an] = A.state;
			continue;
		}
		Animate (A, an, present, nmesh, changed);
		cur[an] = A.state;
	}
	return changed;
}

// VVessel.cpp:1590-1661
void CollAnim::Animate (const ANIMATION &A, uint32_t an, const uint8_t *present, uint32_t nmesh, bool &changed)
{
	double s0, s1, ds;
	for (uint32_t ii = 0; ii < A.ncomp; ii++) {
		uint32_t i = (A.state > cur[an] ? ii : A.ncomp-ii-1);
		const ANIMATIONCOMP *AC = A.comp[i];
		if (!AC) continue;
		auto it = work.find (AC);
		if (it == work.end()) {
			if (!logMissing) { logMissing = true; CollLog (COLLLOG_WARN, "CollAnim: component of animation %u has no snapshot, skipped", an); }
			continue;
		}
		const CollAnimComp &w = it->second;

		s0 = cur[an];
		if      (s0 < AC->state0) s0 = AC->state0;
		else if (s0 > AC->state1) s0 = AC->state1;
		s1 = A.state;
		if      (s1 < AC->state0) s1 = AC->state0;
		else if (s1 > AC->state1) s1 = AC->state1;
		if ((ds = (s1-s0)) == 0) continue;
		ds /= (AC->state1 - AC->state0);

		CollAffine T;
		switch (w.type) {
		case COLLANIM_ROTATE:
			T = RotateT (w.axis, ds*w.angle, w.ref);
			break;
		case COLLANIM_TRANSLATE:
			T.t = w.shift * ds;
			break;
		case COLLANIM_SCALE: {
			s0 = (s0-AC->state0)/(AC->state1-AC->state0);
			s1 = (s1-AC->state0)/(AC->state1-AC->state0);
			double d[3];
			for (int k = 0; k < 3; k++) d[k] = (s1*(w.scale.data[k]-1)+1)/(s0*(w.scale.data[k]-1)+1);
			T.A = Matrix (d[0], 0, 0, 0, d[1], 0, 0, 0, d[2]);
			T.t = Vector (w.ref.x * (1.0-d[0]), w.ref.y * (1.0-d[1]), w.ref.z * (1.0-d[2]));
			} break;
		default:
			break;
		}
		if (!Finite (T)) {
			if (!logBadT) { logBadT = true; CollLog (COLLLOG_WARN, "CollAnim: animation %u gives a non-finite transform (scale through zero?), skipped", an); }
			continue;
		}
		Apply (AC, T, present, nmesh, changed);
	}
}

// VVessel.cpp:1665-1720; column form: the client's X = X*T is X = T o X here
void CollAnim::Apply (const ANIMATIONCOMP *c, const CollAffine &T, const uint8_t *present, uint32_t nmesh, bool &changed)
{
	auto it = work.find (c);
	if (it != work.end()) {
		const CollAnimComp &w = it->second;
		if (w.mesh != COLLANIM_LVL) {
			if (w.mesh >= nmesh || (present && !present[w.mesh])) return; // children untouched (:1679-1681)
			if (w.wholeMesh) {
				CollAffine &X = M[w.mesh];
				X = CollCompose (T, X);
				changed = true;
			} else {
				for (uint32_t g : w.grp) {
					CollAffine &X = G[std::make_pair (w.mesh, g)];
					X = CollCompose (T, X);
					changed = true;
				}
			}
		}
	} else if (!logMissing) {
		logMissing = true;
		CollLog (COLLLOG_WARN, "CollAnim: child component without snapshot, used as an empty node");
	}

	for (uint32_t i = 0; i < c->nchildren; i++) {
		const ANIMATIONCOMP *child = c->children[i];
		if (!child) continue;
		Apply (child, T, present, nmesh, changed);
		auto jt = work.find (child);
		if (jt == work.end()) continue;
		CollAnimComp &w = jt->second;
		switch (w.type) {
		case COLLANIM_ROTATE: {
			w.ref = CollApply (T, w.ref);
			Vector a = CollApplyDir (T, w.axis);
			double l2 = a.x*a.x + a.y*a.y + a.z*a.z;
			if (l2 > 0 && std::isfinite (l2)) {
				double len = 1.0/sqrt (l2);
				a.x *= len; a.y *= len; a.z *= len;
				w.axis = a;
			} else {
				// the client normalises a zero vector (NaN); the replica keeps it zero
				w.axis = Vector ();
				if (!logZeroAxis) { logZeroAxis = true; CollLog (COLLLOG_WARN, "CollAnim: child rotation axis became zero, kept zero (client: NaN)"); }
			}
			} break;
		case COLLANIM_TRANSLATE:
			w.shift = CollApplyDir (T, w.shift);
			break;
		case COLLANIM_SCALE:
			w.ref = CollApply (T, w.ref); // the scale vector itself is not transformed (:1712-1716)
			break;
		default:
			break;
		}
	}
}

bool CollAnim::GroupTransform (uint32_t mesh, uint32_t grp, CollAffine &F) const
{
	auto im = M.find (mesh);
	auto ig = G.find (std::make_pair (mesh, grp));
	if (im == M.end() && ig == G.end()) { F = CollAffine (); return false; }
	if (ig == G.end()) F = im->second;
	else if (im == M.end()) F = ig->second;
	else F = CollCompose (ig->second, im->second); // client pGrpTF = mTransform * Grp.Transform (Mesh.cpp:3071)
	return true;
}

// multiset of components whose transform reaches each group of mesh, as sum of component hashes
void CollAnim::Signatures (const ANIMATION *anim, uint32_t nanim, uint32_t mesh, uint32_t ngrp,
	const uint8_t *present, uint32_t nmesh, std::vector<uint64_t> &sig) const
{
	sig.assign (ngrp, 0);
	if (mesh >= nmesh || (present && !present[mesh])) return;
	for (uint32_t an = 0; an < nanim; an++) {
		const ANIMATION &A = anim[an];
		for (uint32_t k = 0; k < A.ncomp; k++) {
			if (!A.comp[k] || work.find (A.comp[k]) == work.end()) continue; // Animate skips a top-level component without snapshot, children included
			uint64_t h = Mix (((uint64_t)an << 32) | k);
			Reach (A.comp[k], h, mesh, ngrp, present, nmesh, sig);
		}
	}
}

// same walk as Apply: a node on a missing mesh stops the propagation
void CollAnim::Reach (const ANIMATIONCOMP *c, uint64_t h, uint32_t mesh, uint32_t ngrp,
	const uint8_t *present, uint32_t nmesh, std::vector<uint64_t> &sig) const
{
	auto it = work.find (c);
	if (it != work.end()) {
		const CollAnimComp &w = it->second;
		if (w.mesh != COLLANIM_LVL) {
			if (w.mesh >= nmesh || (present && !present[w.mesh])) return;
			if (w.mesh == mesh && !w.wholeMesh)
				for (uint32_t g : w.grp) if (g < ngrp) sig[g] += h;
		}
	}
	for (uint32_t i = 0; i < c->nchildren; i++)
		if (c->children[i]) Reach (c->children[i], h, mesh, ngrp, present, nmesh, sig);
}

uint64_t CollAnim::Version () const
{
	return ver;
}
