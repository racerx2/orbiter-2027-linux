// not upstream: animation test doubles (D1 9.2): core list with CollAnim hooks, client port

#ifndef __COLLANIMTEST_H
#define __COLLANIMTEST_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <unordered_set>
#include <vector>
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

// unit-test access to CollAnim's snapshot table (friend in CollAnim.h)
struct CollAnimProbe { static size_t nWork (const CollAnim &a) { return a.work.size(); } };

// deterministic test random numbers (splitmix64), the same on every platform
struct TestRng {
	uint64_t s;
	explicit TestRng (uint64_t seed) : s (seed) {}
	uint64_t Next () { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
	double U () { return (double)(Next () >> 11) * (1.0 / 9007199254740992.0); }
	double U (double a, double b) { return a + (b - a) * U (); }
	uint32_t I (uint32_t n) { return (uint32_t)(Next () % n); }
	bool P (double p) { return U () < p; }
	double N () { double u1 = U (), u2 = U (); if (u1 < 1e-300) u1 = 1e-300; return sqrt (-2.0 * log (u1)) * cos (Pi2 * u2); }
};

// module memory: transforms, group lists and vertex lists the module owns for the vessel's lifetime
template <class T> struct TestOwn final : T { using T::T; }; // final: the SDK classes have virtual functions but no virtual destructor
struct TestModule {
	std::deque<TestOwn<MGROUP_TRANSFORM>> nul; std::deque<TestOwn<MGROUP_ROTATE>> rot; std::deque<TestOwn<MGROUP_TRANSLATE>> lin; std::deque<TestOwn<MGROUP_SCALE>> scl;
	std::deque<std::vector<UINT>> grp; std::deque<std::vector<VECTOR3>> vtx;
	UINT *Grp (std::initializer_list<UINT> g) { grp.emplace_back (g); return grp.back().data(); }
	UINT *Grp (const std::vector<UINT> &g) { grp.push_back (g); return grp.back().data(); }
	VECTOR3 *Vtx (const std::vector<VECTOR3> &v) { vtx.push_back (v); return vtx.back().data(); }
	MGROUP_ROTATE *Rot (UINT mesh, UINT *g, UINT n, const VECTOR3 &ref, const VECTOR3 &axis, float angle) { rot.emplace_back (mesh, g, n, ref, axis, angle); return &rot.back(); }
	MGROUP_TRANSLATE *Lin (UINT mesh, UINT *g, UINT n, const VECTOR3 &shift) { lin.emplace_back (mesh, g, n, shift); return &lin.back(); }
	MGROUP_SCALE *Scl (UINT mesh, UINT *g, UINT n, const VECTOR3 &ref, const VECTOR3 &scale) { scl.emplace_back (mesh, g, n, ref, scale); return &scl.back(); }
	MGROUP_TRANSFORM *Nul (UINT mesh, UINT *g, UINT n) { nul.emplace_back (mesh, g, n); return &nul.back(); }
};

// core animation list (Vessel.cpp:5778-5954) with the D1 6.3 hooks; meshes as group counts per slot
struct TestVessel {
	ANIMATION *anim = nullptr;
	UINT nanim = 0;
	std::vector<UINT> meshGrp;   // group count of the mesh in each slot, 0 = empty slot
	CollAnim *coll = nullptr;    // hook target

	~TestVessel () { coll = nullptr; ClearAnimations (); }
	UINT GetAnimPtr (ANIMATION **a) const { *a = anim; return nanim; }
	std::vector<uint8_t> Present () const { std::vector<uint8_t> p (meshGrp.size()); for (size_t i = 0; i < p.size(); i++) p[i] = meshGrp[i] ? 1 : 0; return p; }
	UINT nComp () const { UINT n = 0; for (UINT i = 0; i < nanim; i++) n += anim[i].ncomp; return n; }
	bool Step () { std::vector<uint8_t> p = Present (); return coll->Step (anim, nanim, p.data(), (uint32_t)p.size()); }

	// Vessel.cpp:5778-5791
	UINT CreateAnimation (double initial_state)
	{
		ANIMATION *tmp = new ANIMATION[nanim+1];
		if (nanim) {
			memcpy (tmp, anim, nanim*sizeof(ANIMATION));
			delete []anim;
		}
		anim = tmp;
		anim[nanim].defstate = initial_state;
		anim[nanim].state    = initial_state;
		anim[nanim].ncomp    = 0;
		anim[nanim].comp     = nullptr;
		return nanim++;
	}

	// Vessel.cpp:5793-5825 (delete[] for the comp list; upstream uses delete)
	ANIMATIONCOMP *AddAnimationComponent (UINT an, double state0, double state1, MGROUP_TRANSFORM *trans, ANIMATIONCOMP *parent = nullptr)
	{
		if (an >= nanim) return 0;
		ANIMATION *A = anim+an;
		UINT ncomp = A->ncomp;
		ANIMATIONCOMP **tmp = new ANIMATIONCOMP*[ncomp+1];
		if (ncomp) {
			memcpy (tmp, A->comp, ncomp*sizeof(ANIMATIONCOMP*));
			delete []A->comp;
		}
		A->comp = tmp;
		ANIMATIONCOMP *ac = new ANIMATIONCOMP;
		ac->state0     = state0;
		ac->state1     = state1;
		ac->trans      = trans;
		ac->parent     = parent;
		ac->children   = 0;
		ac->nchildren  = 0;
		A->comp[ncomp] = ac;

		if (parent) {
			ANIMATIONCOMP **ch = new ANIMATIONCOMP*[parent->nchildren+1];
			if (parent->nchildren) {
				memcpy (ch, parent->children, parent->nchildren*sizeof(ANIMATIONCOMP*));
				delete []parent->children;
			}
			parent->children = ch;
			parent->children[parent->nchildren++] = ac;
		}
		A->ncomp++;
		if (coll) coll->OnAdd (ac); // CollHook::OnAnimAdd (D1 6.3)
		return ac;
	}

	// Vessel.cpp:5827-5887
	bool DelAnimationComponent (UINT an, ANIMATIONCOMP *comp)
	{
		if (an >= nanim) return false;
		ANIMATION *A = anim+an;
		UINT i, j, k, ncomp = A->ncomp;

		for (i = 0; i < ncomp; i++)
			if (A->comp[i] == comp) break;
		if (i == ncomp) return false;

		while (comp->nchildren) {
			if (comp->children && DelAnimationComponent (an, comp->children[0])) continue;
			for (i = 0; i < nanim; i++)
				if (comp->children && DelAnimationComponent (i, comp->children[0])) break;
			if (i == nanim) {
				ANIMATIONCOMP **ch = 0;
				if (comp->nchildren > 1) {
					ch = new ANIMATIONCOMP*[comp->nchildren-1];
					for (j = 1; j < comp->nchildren; j++)
						if (comp->children) ch[j-1] = comp->children[j];
				}
				delete []comp->children;
				comp->children = ch;
				comp->nchildren--;
			}
		}
		// upstream reuses i in the search above, keeps a stale ncomp after child deletes; double re-finds
		for (i = 0; i < A->ncomp; i++)
			if (A->comp[i] == comp) break;
		ncomp = A->ncomp;

		if (comp->parent) {
			ANIMATIONCOMP **ch = 0;
			if (comp->parent->nchildren > 1) {
				ch = new ANIMATIONCOMP*[comp->parent->nchildren-1];
				for (j = k = 0; j < comp->parent->nchildren; j++)
					if (comp->parent->children[j] != comp) ch[k++] = comp->parent->children[j];
			}
			delete []comp->parent->children;
			comp->parent->children = ch;
			comp->parent->nchildren--;
		}

		ANIMATIONCOMP **tmp = 0;
		if (ncomp > 1) {
			tmp = new ANIMATIONCOMP*[ncomp-1];
			for (j = k = 0; j < ncomp; j++)
				if (j != i) tmp[k++] = A->comp[j];
		}
		delete []A->comp;
		A->comp = tmp;
		A->ncomp--;

		if (coll) coll->OnDel (comp); // CollHook::OnAnimDel (D1 6.3)
		delete comp;
		return true;
	}

	// Vessel.cpp:5889-5894
	bool SetAnimation (UINT an, double state)
	{
		if (an >= nanim) return false;
		anim[an].state = state;
		return true;
	}

	// Vessel.cpp:5930-5954; the CLEARANIM broadcast at entry reaches CollAnim via the vis-message hook
	void ClearAnimations ()
	{
		if (coll) coll->OnClear ();
		for (UINT i = 0; i < nanim; i++) {
			if (anim[i].ncomp && anim[i].comp != NULL) {
				for (UINT j = 0; j < anim[i].ncomp; j++) {
					if (anim[i].comp[j]->nchildren) {
						delete []anim[i].comp[j]->children;
						anim[i].comp[j]->children = NULL;
					}
					delete anim[i].comp[j];
				}
				delete []anim[i].comp;
				anim[i].comp = NULL;
			}
		}
		if (nanim) {
			delete []anim;
			anim = NULL;
		}
		nanim = 0;
	}
};

// port of the client's animation code (OVP/VulkanClient: D3DXMath, D3D9Util, Mesh, VVessel .cpp)
template <class FL> struct RMATT { // FL = float is the client; FL = double is the same code in double
	RMATT () { memset (m, 0, sizeof (m)); }
	union {
		struct { FL _11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44; };
		FL m[4][4];
	};
};
typedef RMATT<float> RMAT;

template <class FL> void D3DMAT_Identity (RMATT<FL> *mat)
{
	memset (mat->m, 0, sizeof (mat->m));
	mat->_11 = mat->_22 = mat->_33 = mat->_44 = 1.0f;
}

template <class FL> RMATT<FL> *D3DXMatrixMultiply (RMATT<FL> *pOut, const RMATT<FL> *pM1, const RMATT<FL> *pM2)
{
	RMATT<FL> r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = pM1->m[i][0]*pM2->m[0][j] + pM1->m[i][1]*pM2->m[1][j] + pM1->m[i][2]*pM2->m[2][j] + pM1->m[i][3]*pM2->m[3][j];
	*pOut = r;
	return pOut;
}

template <class FL> void D3DMAT_RotationFromAxis (const FL axis[3], FL angle, RMATT<FL> *rot)
{
	angle *= (FL)0.5;
	FL w = std::cos(angle), sina = std::sin(angle);
	FL x = sina * axis[0];
	FL y = sina * axis[1];
	FL z = sina * axis[2];
	FL xx = x*x, yy = y*y, zz = z*z;
	FL xy = x*y, xz = x*z, yz = y*z;
	FL wx = w*x, wy = w*y, wz = w*z;
	rot->_11 = 1 - 2 * (yy+zz);
	rot->_12 =     2 * (xy+wz);
	rot->_13 =     2 * (xz-wy);
	rot->_21 =     2 * (xy-wz);
	rot->_22 = 1 - 2 * (xx+zz);
	rot->_23 =     2 * (yz+wx);
	rot->_31 =     2 * (xz+wy);
	rot->_32 =     2 * (yz-wx);
	rot->_33 = 1 - 2 * (xx+yy);
	rot->_14 = rot->_24 = rot->_34 = rot->_41 = rot->_42 = rot->_43 = 0;
	rot->_44 = 1;
}

template <class FL> void RefTransformPoint (VECTOR3 &p, const RMATT<FL> &T)
{
	double x = p.x*T._11 + p.y*T._21 + p.z*T._31 + T._41;
	double y = p.x*T._12 + p.y*T._22 + p.z*T._32 + T._42;
	double z = p.x*T._13 + p.y*T._23 + p.z*T._33 + T._43;
	double w = 1.0/(p.x*T._14 + p.y*T._24 + p.z*T._34 + T._44);
	p.x = x*w;
	p.y = y*w;
	p.z = z*w;
}

template <class FL> void RefTransformDirection (VECTOR3 &a, const RMATT<FL> &T, bool normalise)
{
	double x = a.x*T._11 + a.y*T._21 + a.z*T._31;
	double y = a.x*T._12 + a.y*T._22 + a.z*T._32;
	double z = a.x*T._13 + a.y*T._23 + a.z*T._33;
	a.x = x, a.y = y, a.z = z;
	if (normalise) {
		double len = 1.0/sqrt (x*x + y*y + z*z);
		a.x *= len;
		a.y *= len;
		a.z *= len;
	}
}

// the parts of D3D9Mesh the animation touches
template <class FL> struct RefMeshT {
	typedef RMATT<FL> RMAT;
	UINT nGrp;
	RMAT mTransform;
	std::vector<RMAT> Transform, pGrpTF;
	std::vector<bool> bTransform;
	explicit RefMeshT (UINT n) : nGrp (n), Transform (n), pGrpTF (n), bTransform (n, false)
	{
		D3DMAT_Identity (&mTransform);
		for (UINT i = 0; i < n; i++) { D3DMAT_Identity (&Transform[i]); D3DMAT_Identity (&pGrpTF[i]); }
	}
	void ResetTransformations ()
	{
		D3DMAT_Identity (&mTransform);
		for (UINT i = 0; i < nGrp; i++) {
			D3DMAT_Identity (&Transform[i]);
			D3DMAT_Identity (&pGrpTF[i]);
			bTransform[i] = false;
		}
	}
	void TransformGroup (UINT n, const RMAT *m)
	{
		if (n >= nGrp) return; // the client posts an error notice
		D3DXMatrixMultiply (&Transform[n], &Transform[n], m);
		bTransform[n] = true;
		D3DXMatrixMultiply (&pGrpTF[n], &mTransform, &Transform[n]);
	}
	void TransformMesh (const RMAT *m)
	{
		D3DXMatrixMultiply (&mTransform, &mTransform, m);
		for (UINT i = 0; i < nGrp; i++) {
			if (bTransform[i]) D3DXMatrixMultiply (&pGrpTF[i], &mTransform, &Transform[i]);
			else pGrpTF[i] = mTransform;
		}
	}
};

// vVessel's animation state and code; mutates module memory like the client
template <class FL> struct RefClientT {
	typedef RMATT<FL> RMAT;
	typedef RefMeshT<FL> RefMesh;
	static FL D3DVAL (double x) { return (FL)x; }
	struct DefState { float fdata = 0; VECTOR3 ref {}, vdata {}; std::vector<VECTOR3> vtx; };
	TestVessel *vessel;
	bool bAbsAnims;
	std::vector<std::unique_ptr<RefMesh>> meshlist;
	ANIMATION *anim = nullptr;
	std::map<MGROUP_TRANSFORM*, DefState> defstate;
	std::unordered_set<UINT> applyanim;
	std::map<int, double> currentstate;

	// VVessel.cpp:47-95 (LoadMeshes: a mesh per non-empty slot)
	RefClientT (TestVessel *v, bool absAnims = false) : vessel (v), bAbsAnims (absAnims)
	{
		for (size_t i = 0; i < v->meshGrp.size(); i++)
			meshlist.emplace_back (v->meshGrp[i] ? new RefMesh (v->meshGrp[i]) : nullptr);
		UINT na = vessel->GetAnimPtr (&anim);
		for (UINT i = 0; i < na; i++) {
			currentstate[i] = anim[i].defstate;
			if (bAbsAnims) for (UINT k = 0; k < anim[i].ncomp; ++k) StoreDefaultState (anim[i].comp[k]);
		}
		UpdateAnimations ();
	}

	// VVessel.cpp:332-394
	void InsertMesh (UINT idx)
	{
		if (idx >= meshlist.size()) meshlist.resize (idx+1);
		meshlist[idx].reset ();
		if (idx < vessel->meshGrp.size() && vessel->meshGrp[idx]) meshlist[idx].reset (new RefMesh (vessel->meshGrp[idx]));
		UpdateAnimations (idx);
	}

	// VVessel.cpp:456-468
	void DelMesh (UINT idx)
	{
		if (idx == 0xFFFFFFFF) { meshlist.clear (); return; }
		if (idx >= meshlist.size()) return;
		meshlist[idx].reset ();
	}

	// VVessel.cpp:520-583
	void UpdateAnimations (int mshidx = -1)
	{
		UINT na = vessel->GetAnimPtr (&anim);
		for (UINT i = 0; i < na; ++i) {
			if (currentstate.count (i) == 0) currentstate[i] = anim[i].defstate;
			if (bAbsAnims) {
				for (UINT k = 0; k < anim[i].ncomp; ++k) {
					ANIMATIONCOMP *AC = anim[i].comp[k];
					if (defstate.count (AC->trans) == 0) StoreDefaultState (AC);
				}
			}
		}
		if (bAbsAnims) {
			for (UINT i = 0; i < meshlist.size(); ++i) if (meshlist[i]) meshlist[i]->ResetTransformations ();
			for (UINT i = 0; i < na; ++i) {
				currentstate[i] = anim[i].defstate;
				for (UINT k = 0; k < anim[i].ncomp; ++k) {
					if (anim[i].state != anim[i].defstate)
						RestoreDefaultState (anim[i].comp[k]);
				}
			}
			for (UINT i = 0; i < na; ++i) {
				if (!anim[i].ncomp) continue;
				if (applyanim.count (i)) continue;
				if (anim[i].state != anim[i].defstate) applyanim.insert (applyanim.end(), i);
			}
			for (auto i : applyanim) Animate (i, (UINT)mshidx);
		} else {
			for (UINT i = 0; i < na; ++i) {
				if (anim[i].state != currentstate[i]) {
					Animate (i, (UINT)mshidx);
					currentstate[i] = anim[i].state;
				}
			}
		}
	}

	// VVessel.cpp:1513-1546
	void StoreDefaultState (ANIMATIONCOMP *AC)
	{
		if (defstate.count (AC->trans)) return;
		auto trans = AC->trans;
		DefState def;
		switch (trans->Type()) {
		case MGROUP_TRANSFORM::NULLTRANSFORM:
			break;
		case MGROUP_TRANSFORM::ROTATE: {
			MGROUP_ROTATE *rot = (MGROUP_ROTATE*)trans;
			def.ref = rot->ref;
			def.vdata = unit (rot->axis);
			def.fdata = rot->angle;
			} break;
		case MGROUP_TRANSFORM::TRANSLATE: {
			MGROUP_TRANSLATE *lin = (MGROUP_TRANSLATE*)trans;
			def.vdata = lin->shift;
			} break;
		case MGROUP_TRANSFORM::SCALE: {
			MGROUP_SCALE *scl = (MGROUP_SCALE*)trans;
			def.ref = scl->ref;
			def.vdata = scl->scale;
			} break;
		}
		if (trans->mesh == LOCALVERTEXLIST) for (UINT j = 0; j < trans->ngrp; ++j) def.vtx.push_back (((VECTOR3 *)trans->grp)[j]);
		defstate[AC->trans] = def;
		for (UINT i = 0; i < AC->nchildren; ++i) StoreDefaultState (AC->children[i]);
	}

	// VVessel.cpp:1551-1584
	void RestoreDefaultState (ANIMATIONCOMP *AC)
	{
		auto trans = AC->trans;
		auto it = defstate.find (AC->trans);
		if (trans->mesh == LOCALVERTEXLIST) {
			VECTOR3 *vtx = (VECTOR3*)trans->grp;
			for (UINT i = 0; i < trans->ngrp; i++) vtx[i] = it->second.vtx[i];
		}
		switch (trans->Type()) {
		case MGROUP_TRANSFORM::NULLTRANSFORM:
			break;
		case MGROUP_TRANSFORM::ROTATE: {
			MGROUP_ROTATE *rot = (MGROUP_ROTATE*)trans;
			rot->ref = it->second.ref;
			rot->axis = it->second.vdata;
			rot->angle = it->second.fdata;
			} break;
		case MGROUP_TRANSFORM::TRANSLATE: {
			MGROUP_TRANSLATE *lin = (MGROUP_TRANSLATE*)trans;
			lin->shift = it->second.vdata;
			} break;
		case MGROUP_TRANSFORM::SCALE: {
			MGROUP_SCALE *scl = (MGROUP_SCALE*)trans;
			scl->ref = it->second.ref;
			scl->scale = it->second.vdata;
			} break;
		}
		for (UINT i = 0; i < AC->nchildren; ++i) RestoreDefaultState (AC->children[i]);
	}

	// VVessel.cpp:1590-1661
	void Animate (UINT an, UINT mshidx)
	{
		double s0, s1, ds;
		UINT i, ii;
		RMAT T;
		ANIMATION *A = anim+an;

		for (ii = 0; ii < A->ncomp; ii++) {

			i = (A->state > currentstate[an] ? ii : A->ncomp-ii-1);
			ANIMATIONCOMP *AC = A->comp[i];

			if ((mshidx != LOCALVERTEXLIST) && (mshidx != AC->trans->mesh)) continue;

			s0 = currentstate[an];
			if      (s0 < AC->state0) s0 = AC->state0;
			else if (s0 > AC->state1) s0 = AC->state1;
			s1 = A->state;
			if      (s1 < AC->state0) s1 = AC->state0;
			else if (s1 > AC->state1) s1 = AC->state1;
			if ((ds = (s1-s0)) == 0) continue;
			ds /= (AC->state1 - AC->state0);

			switch (AC->trans->Type())
			{
				case MGROUP_TRANSFORM::NULLTRANSFORM:
				{
					D3DMAT_Identity (&T);
					AnimateComponent (AC, T);
				}	break;

				case MGROUP_TRANSFORM::ROTATE:
				{
					MGROUP_ROTATE *rot = (MGROUP_ROTATE*)AC->trans;
					FL ax[3] = { FL(rot->axis.x), FL(rot->axis.y), FL(rot->axis.z) };
					D3DMAT_RotationFromAxis (ax, (FL)ds*rot->angle, &T);
					FL dx = D3DVAL(rot->ref.x), dy = D3DVAL(rot->ref.y), dz = D3DVAL(rot->ref.z);
					T._41 = dx - T._11*dx - T._21*dy - T._31*dz;
					T._42 = dy - T._12*dx - T._22*dy - T._32*dz;
					T._43 = dz - T._13*dx - T._23*dy - T._33*dz;
					AnimateComponent (AC, T);
				} break;

				case MGROUP_TRANSFORM::TRANSLATE:
				{
					MGROUP_TRANSLATE *lin = (MGROUP_TRANSLATE*)AC->trans;
					D3DMAT_Identity (&T);
					T._41 = (FL)(ds*lin->shift.x);
					T._42 = (FL)(ds*lin->shift.y);
					T._43 = (FL)(ds*lin->shift.z);
					AnimateComponent (AC, T);
				} break;

				case MGROUP_TRANSFORM::SCALE:
				{
					MGROUP_SCALE *scl = (MGROUP_SCALE*)AC->trans;
					s0 = (s0-AC->state0)/(AC->state1-AC->state0);
					s1 = (s1-AC->state0)/(AC->state1-AC->state0);
					D3DMAT_Identity (&T);
					T._11 = (FL)((s1*(scl->scale.x-1)+1)/(s0*(scl->scale.x-1)+1));
					T._22 = (FL)((s1*(scl->scale.y-1)+1)/(s0*(scl->scale.y-1)+1));
					T._33 = (FL)((s1*(scl->scale.z-1)+1)/(s0*(scl->scale.z-1)+1));
					T._41 = (FL)scl->ref.x * ((FL)1-T._11);
					T._42 = (FL)scl->ref.y * ((FL)1-T._22);
					T._43 = (FL)scl->ref.z * ((FL)1-T._33);
					AnimateComponent (AC, T);
				} break;
			}
		}
	}

	// VVessel.cpp:1665-1720
	void AnimateComponent (ANIMATIONCOMP *comp, const RMAT &T)
	{
		UINT i;
		MGROUP_TRANSFORM *trans = comp->trans;

		if (trans->mesh == LOCALVERTEXLIST) {
			VECTOR3 *vtx = (VECTOR3*)trans->grp;
			for (i = 0; i < trans->ngrp; i++) RefTransformPoint (vtx[i], T);
		}
		else {
			if (trans->mesh >= meshlist.size()) return;
			RefMesh *mesh = meshlist[trans->mesh].get();
			if (!mesh) return;
			if (trans->grp) {
				for (i = 0; i < trans->ngrp; i++) mesh->TransformGroup (trans->grp[i], &T);
			}
			else {
				mesh->TransformMesh (&T);
			}
		}

		for (i = 0; i < comp->nchildren; i++) {
			ANIMATIONCOMP *child = comp->children[i];
			AnimateComponent (child, T);
			switch (child->trans->Type()) {
				case MGROUP_TRANSFORM::NULLTRANSFORM:
					break;
				case MGROUP_TRANSFORM::ROTATE: {
					MGROUP_ROTATE *rot = (MGROUP_ROTATE*)child->trans;
					RefTransformPoint (rot->ref, T);
					RefTransformDirection (rot->axis, T, true);
				} break;
				case MGROUP_TRANSFORM::TRANSLATE: {
					MGROUP_TRANSLATE *lin = (MGROUP_TRANSLATE*)child->trans;
					RefTransformDirection (lin->shift, T, false);
				} break;
				case MGROUP_TRANSFORM::SCALE: {
					MGROUP_SCALE *scl = (MGROUP_SCALE*)child->trans;
					RefTransformPoint (scl->ref, T);
				} break;
			}
		}
	}

	// group matrix of mesh m (row form); false if the client holds no such mesh or group
	bool GroupTF (UINT m, UINT g, RMAT &out) const
	{
		if (m >= meshlist.size() || !meshlist[m] || g >= meshlist[m]->nGrp) return false;
		out = meshlist[m]->pGrpTF[g];
		return true;
	}
};

typedef RefMeshT<float> RefMesh;
typedef RefClientT<float> RefClient;
typedef RefClientT<double> RefClientD;

// max element diff, client rows vs replica columns, / max(1, max |elem|); +inf (counted): nonfinite
template <class FL> double RefDiff (const RMATT<FL> &R, const CollAffine &F, int *nonfinite = nullptr)
{
	double e = 0, mx = 1;
	bool bad = false;
	auto one = [&] (double a, double b) {
		if (std::isfinite (a) && std::isfinite (b)) { e = std::max (e, fabs (a - b)); mx = std::max (mx, fabs (a)); return; }
		bad = true;
		if (!(a == b || (std::isnan (a) && std::isnan (b)))) e = HUGE_VAL;
	};
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) one (R.m[i][j], F.A (j, i));
		one (R.m[3][i], F.t.data[i]);
	}
	if (bad && nonfinite) (*nonfinite)++;
	return e / mx;
}

// point through the client's row matrix (Mesh.cpp vertex shader convention p * M)
template <class FL> Vector RefPoint (const RMATT<FL> &T, const Vector &p)
{
	VECTOR3 v = _V (p.x, p.y, p.z);
	RefTransformPoint (v, T);
	return Vector (v.x, v.y, v.z);
}

// max replica-vs-client diff over every group of every client mesh; +inf, nonfinite as RefDiff
template <class FL> double RefCompare (const RefClientT<FL> &cl, const CollAnim &ca, const TestVessel &v, int *nonfinite = nullptr)
{
	double e = 0;
	for (UINT m = 0; m < cl.meshlist.size(); m++) {
		if (!cl.meshlist[m]) continue;
		for (UINT g = 0; g < cl.meshlist[m]->nGrp; g++) {
			RMATT<FL> R; CollAffine F;
			cl.GroupTF (m, g, R);
			ca.GroupTransform (m, g, F);
			double d = RefDiff (R, F, nonfinite);
			if (!(d <= e)) e = d;
		}
	}
	return e;
}

// stock animation definitions, copied from the sources; module statics become TestModule storage

// Atlantis.msh group indices (meshres.h is generated by meshc from the LABEL lines)
enum {
	ATL_cargodooroutR = 2, ATL_cargodooroutL = 3, ATL_flapR = 4, ATL_flapL = 5, ATL_aileronR = 6, ATL_aileronL = 7,
	ATL_SSMER = 12, ATL_SSMEL = 13, ATL_SSMET = 14, ATL_nosedoorL = 15, ATL_geardoorL = 16, ATL_geardoorR = 17, ATL_nosedoorR = 18,
	ATL_nosewheel = 19, ATL_nosegear = 20, ATL_wheelR = 21, ATL_gearR = 22, ATL_wheelL = 23, ATL_gearL = 24, ATL_startrackers = 25,
	ATL_rudderR = 26, ATL_rudderL = 27, ATL_cargodoorinR = 31, ATL_cargodoorinL = 32, ATL_KUband1 = 33, ATL_KUband2 = 34,
	ATL_Shoulder = 35, ATL_Humerus = 36, ATL_radii = 37, ATL_wrist = 38, ATL_endeffecter = 39, ATL_RMScamera = 40, ATL_RMScamera_pivot = 41,
	ATL_radiatorFL = 50, ATL_radiatorFR = 51, ATL_radiatorBR = 52, ATL_radiatorBL = 53, ATL_NGRP = 59
};

struct AtlantisAnims {
	UINT anim_door, anim_rad, anim_gear, anim_kubd, anim_elev, anim_laileron, anim_raileron, anim_rudder, anim_spdb;
	UINT anim_arm_sy, anim_arm_sp, anim_arm_ep, anim_arm_wp, anim_arm_wy, anim_arm_wr, anim_ssme;
	ANIMATIONCOMP *rms[6];
	VECTOR3 *arm_tip;
};

// Atlantis/Atlantis.cpp:636-806 (external animations; meshes: 0 cockpit, 1 orbiter, 2 VC)
inline void DefineAtlantis (TestVessel &v, TestModule &mod, AtlantisAnims &a)
{
	UINT midx = 1;
	a.arm_tip = mod.Vtx ({ _V(-2.26,1.71,-6.5), _V(-2.26,1.71,-7.5), _V(-2.26,2.71,-6.5) }); // Atlantis.cpp:281-283

	UINT *RCargoDoorGrp = mod.Grp ({ATL_cargodooroutR,ATL_cargodoorinR,ATL_radiatorFR,ATL_radiatorBR});
	MGROUP_ROTATE *RCargoDoor = mod.Rot (midx, RCargoDoorGrp, 4, _V(2.88, 1.3, 0), _V(0,0,1), (float)(-175.5*RAD));
	UINT *LCargoDoorGrp = mod.Grp ({ATL_cargodooroutL,ATL_cargodoorinL,ATL_radiatorFL,ATL_radiatorBL});
	MGROUP_ROTATE *LCargoDoor = mod.Rot (midx, LCargoDoorGrp, 4, _V(-2.88, 1.3, 0), _V(0,0,1), (float)(175.5*RAD));
	a.anim_door = v.CreateAnimation (0);
	v.AddAnimationComponent (a.anim_door, 0.0, 0.4632, RCargoDoor);
	v.AddAnimationComponent (a.anim_door, 0.5368, 1.0, LCargoDoor);

	UINT *RRadiatorGrp = mod.Grp ({ATL_radiatorFR});
	MGROUP_ROTATE *RRadiator = mod.Rot (midx, RRadiatorGrp, 1, _V(2.88, 1.3, 0), _V(0,0,1), (float)(35.5*RAD));
	UINT *LRadiatorGrp = mod.Grp ({ATL_radiatorFL});
	MGROUP_ROTATE *LRadiator = mod.Rot (midx, LRadiatorGrp, 1, _V(-2.88, 1.3, 0), _V(0,0,1), (float)(-35.5*RAD));
	a.anim_rad = v.CreateAnimation (0);
	v.AddAnimationComponent (a.anim_rad, 0, 1, RRadiator);
	v.AddAnimationComponent (a.anim_rad, 0, 1, LRadiator);

	UINT *LNosewheelDoorGrp = mod.Grp ({ATL_nosedoorL});
	MGROUP_ROTATE *LNosewheelDoor = mod.Rot (midx, LNosewheelDoorGrp, 1, _V(-0.78, -2.15, 17), _V(0, 0.195, 0.981), (float)(-60.0*RAD));
	UINT *RNosewheelDoorGrp = mod.Grp ({ATL_nosedoorR});
	MGROUP_ROTATE *RNosewheelDoor = mod.Rot (midx, RNosewheelDoorGrp, 1, _V(0.78, -2.15, 17), _V(0, 0.195, 0.981), (float)(60.0*RAD));
	UINT *NosewheelGrp = mod.Grp ({ATL_nosewheel,ATL_nosegear});
	MGROUP_ROTATE *Nosewheel = mod.Rot (midx, NosewheelGrp, 2, _V(0.0, -1.95, 17.45), _V(1, 0, 0), (float)(109.0*RAD));
	UINT *RGearDoorGrp = mod.Grp ({ATL_geardoorR});
	MGROUP_ROTATE *RGearDoor = mod.Rot (midx, RGearDoorGrp, 1, _V(4.35, -2.64, -1.69), _V(0, 0.02, 0.9), (float)(96.2*RAD));
	UINT *LGearDoorGrp = mod.Grp ({ATL_geardoorL});
	MGROUP_ROTATE *LGearDoor = mod.Rot (midx, LGearDoorGrp, 1, _V(-4.35, -2.64, -1.69), _V(0, 0.02, 0.9), (float)(-96.2*RAD));
	UINT *MainGearGrp = mod.Grp ({ATL_wheelR,ATL_gearR,ATL_wheelL,ATL_gearL});
	MGROUP_ROTATE *MainGear = mod.Rot (midx, MainGearGrp, 4, _V(0, -2.66, -3.68), _V(1, 0, 0), (float)(94.5*RAD));
	a.anim_gear = v.CreateAnimation (0);
	v.AddAnimationComponent (a.anim_gear, 0,   0.5, LNosewheelDoor);
	v.AddAnimationComponent (a.anim_gear, 0,   0.5, RNosewheelDoor);
	v.AddAnimationComponent (a.anim_gear, 0.4, 1.0, Nosewheel);
	v.AddAnimationComponent (a.anim_gear, 0,   0.5, RGearDoor);
	v.AddAnimationComponent (a.anim_gear, 0,   0.5, LGearDoor);
	v.AddAnimationComponent (a.anim_gear, 0.4, 1.0, MainGear);

	UINT *KuBand1Grp = mod.Grp ({ATL_startrackers,ATL_KUband1,ATL_KUband2});
	MGROUP_ROTATE *KuBand1 = mod.Rot (midx, KuBand1Grp, 3, _V(2.85, 0.85, 0), _V(0,0,1), (float)(-18*RAD));
	UINT *KuBand2Grp = mod.Grp ({ATL_KUband2});
	MGROUP_ROTATE *KuBand2 = mod.Rot (midx, KuBand2Grp, 1, _V(2.78, 1.7, 0), _V(0,0,1), (float)(-90*RAD));
	UINT *KuBand3Grp = mod.Grp ({ATL_KUband1,ATL_KUband2});
	MGROUP_ROTATE *KuBand3 = mod.Rot (midx, KuBand3Grp, 2, _V(2.75, 2.05, 11.47), _V(0,1,0), (float)(-113*RAD));
	a.anim_kubd = v.CreateAnimation (0);
	v.AddAnimationComponent (a.anim_kubd, 0,     0.333, KuBand1);
	v.AddAnimationComponent (a.anim_kubd, 0.333, 0.667, KuBand2);
	v.AddAnimationComponent (a.anim_kubd, 0.667, 0.999, KuBand3);

	UINT *ElevGrp = mod.Grp ({ATL_flapR,ATL_flapL,ATL_aileronL,ATL_aileronR});
	MGROUP_ROTATE *Elevator = mod.Rot (midx, ElevGrp, 4, _V(0,-2.173,-8.84), _V(1,0,0), (float)(30.0*RAD));
	a.anim_elev = v.CreateAnimation (0.5);
	v.AddAnimationComponent (a.anim_elev, 0, 1, Elevator);

	UINT *LAileronGrp = mod.Grp ({ATL_flapL,ATL_aileronL});
	MGROUP_ROTATE *LAileron = mod.Rot (midx, LAileronGrp, 2, _V(0,-2.173,-8.84), _V(-1,0,0), (float)(10.0*RAD));
	UINT *RAileronGrp = mod.Grp ({ATL_flapR,ATL_aileronR});
	MGROUP_ROTATE *RAileron = mod.Rot (midx, RAileronGrp, 2, _V(0,-2.173,-8.84), _V(1,0,0), (float)(10.0*RAD));
	a.anim_laileron = v.CreateAnimation (0.5);
	v.AddAnimationComponent (a.anim_laileron, 0, 1, LAileron);
	a.anim_raileron = v.CreateAnimation (0.5);
	v.AddAnimationComponent (a.anim_raileron, 0, 1, RAileron);

	UINT *RudderGrp = mod.Grp ({ATL_rudderR,ATL_rudderL});
	MGROUP_ROTATE *Rudder = mod.Rot (midx, RudderGrp, 2, _V(0,5.77,-12.17), _V(-0.037,0.833,-0.552), (float)(-54.2*RAD));
	a.anim_rudder = v.CreateAnimation (0.5);
	v.AddAnimationComponent (a.anim_rudder, 0, 1, Rudder);

	UINT *SB1Grp = mod.Grp ({ATL_rudderR});
	MGROUP_ROTATE *SB1 = mod.Rot (midx, SB1Grp, 1, _V(0.32,5.77,-12.17), _V(-0.037,0.833,-0.552), (float)(-49.3*RAD));
	UINT *SB2Grp = mod.Grp ({ATL_rudderL});
	MGROUP_ROTATE *SB2 = mod.Rot (midx, SB2Grp, 1, _V(-0.32,5.77,-12.17), _V(0.037,0.833,-0.552), (float)(49.3*RAD));
	a.anim_spdb = v.CreateAnimation (0);
	v.AddAnimationComponent (a.anim_spdb, 0, 1, SB1);
	v.AddAnimationComponent (a.anim_spdb, 0, 1, SB2);

	ANIMATIONCOMP *parent;
	MGROUP_ROTATE *rms_anim[6];
	UINT *RMSShoulderYawGrp = mod.Grp ({ATL_Shoulder});
	rms_anim[0] = mod.Rot (midx, RMSShoulderYawGrp, 1, _V(-2.26, 1.70, 9.65), _V(0, 1, 0), (float)(-360*RAD));
	a.anim_arm_sy = v.CreateAnimation (0.5);
	parent = a.rms[0] = v.AddAnimationComponent (a.anim_arm_sy, 0, 1, rms_anim[0]);
	UINT *RMSShoulderPitchGrp = mod.Grp ({ATL_Humerus});
	rms_anim[1] = mod.Rot (midx, RMSShoulderPitchGrp, 1, _V(-2.26, 1.70, 9.65), _V(1, 0, 0), (float)(147*RAD));
	a.anim_arm_sp = v.CreateAnimation (0.0136);
	parent = a.rms[1] = v.AddAnimationComponent (a.anim_arm_sp, 0, 1, rms_anim[1], parent);
	UINT *RMSElbowPitchGrp = mod.Grp ({ATL_radii,ATL_RMScamera,ATL_RMScamera_pivot});
	rms_anim[2] = mod.Rot (midx, RMSElbowPitchGrp, 3, _V(-2.26,1.55,3.10), _V(1,0,0), (float)(-162*RAD));
	a.anim_arm_ep = v.CreateAnimation (0.0123);
	parent = a.rms[2] = v.AddAnimationComponent (a.anim_arm_ep, 0, 1, rms_anim[2], parent);
	UINT *RMSWristPitchGrp = mod.Grp ({ATL_wrist});
	rms_anim[3] = mod.Rot (midx, RMSWristPitchGrp, 1, _V(-2.26,1.7,-3.55), _V(1,0,0), (float)(240*RAD));
	a.anim_arm_wp = v.CreateAnimation (0.5);
	parent = a.rms[3] = v.AddAnimationComponent (a.anim_arm_wp, 0, 1, rms_anim[3], parent);
	UINT *RMSWristYawGrp = mod.Grp ({ATL_endeffecter});
	rms_anim[4] = mod.Rot (midx, RMSWristYawGrp, 1, _V(-2.26,1.7,-4.9), _V(0,1,0), (float)(-240*RAD));
	a.anim_arm_wy = v.CreateAnimation (0.5);
	parent = a.rms[4] = v.AddAnimationComponent (a.anim_arm_wy, 0, 1, rms_anim[4], parent);
	rms_anim[5] = mod.Rot (LOCALVERTEXLIST, MAKEGROUPARRAY(a.arm_tip), 3, _V(-2.26,1.7,-6.5), _V(0,0,1), (float)(894*RAD));
	a.anim_arm_wr = v.CreateAnimation (0.5);
	a.rms[5] = v.AddAnimationComponent (a.anim_arm_wr, 0, 1, rms_anim[5], parent);

	double init_gimbal = -10*RAD;
	float max_gimbal = (float)(-0.2*PI);
	a.anim_ssme = v.CreateAnimation (init_gimbal/max_gimbal);
	UINT *SSMEL_Grp = mod.Grp ({ATL_SSMEL});
	v.AddAnimationComponent (a.anim_ssme, 0, 1, mod.Rot (midx, SSMEL_Grp, 1, _V(-1.55,-0.37,-12.5), _V(-1,0,0), max_gimbal));
	UINT *SSMER_Grp = mod.Grp ({ATL_SSMER});
	v.AddAnimationComponent (a.anim_ssme, 0, 1, mod.Rot (midx, SSMER_Grp, 1, _V( 1.55,-0.37,-12.5), _V(-1,0,0), max_gimbal));
	UINT *SSMET_Grp = mod.Grp ({ATL_SSMET});
	v.AddAnimationComponent (a.anim_ssme, 0, 1, mod.Rot (midx, SSMET_Grp, 1, _V(0.0,  2.7, -12.5), _V(-1,0,0), max_gimbal));
}

// Src/Vessel/HST/HST.cpp:45-80 (mesh 0, HST_STS-109.msh, 104 groups)
inline void DefineHST (TestVessel &v, TestModule &mod, UINT &anim_ant, UINT &anim_hatch, UINT &anim_array)
{
	UINT *HiGainAnt1Grp = mod.Grp ({1,3});
	MGROUP_ROTATE *HiGainAnt1 = mod.Rot (0, HiGainAnt1Grp, 2, _V(0.002579,1.993670,0.238158), _V(-1,0,0), (float)(PI*0.51));
	UINT *HiGainAnt2Grp = mod.Grp ({0,2});
	MGROUP_ROTATE *HiGainAnt2 = mod.Rot (0, HiGainAnt2Grp, 2, _V(0.002740,-2.013091,0.238118), _V(1,0,0), (float)(PI*0.51));
	anim_ant = v.CreateAnimation (0.0196);
	v.AddAnimationComponent (anim_ant, 0, 0.5, HiGainAnt1);
	v.AddAnimationComponent (anim_ant, 0, 1,   HiGainAnt2);

	UINT *HatchGrp = mod.Grp ({86});
	MGROUP_ROTATE *Hatch = mod.Rot (0, HatchGrp, 1, _V(0.089688,1.456229,7.526453), _V(-1,0,0), (float)(RAD*113));
	anim_hatch = v.CreateAnimation (0);
	v.AddAnimationComponent (anim_hatch, 0, 1, Hatch);

	anim_array = v.CreateAnimation (1);
	UINT *ArrayLFoldGrp = mod.Grp ({87,88,89,90,103});
	UINT *ArrayRFoldGrp = mod.Grp ({92,93,94,95,102});
	v.AddAnimationComponent (anim_array, 0,   0.4, mod.Rot (0, ArrayLFoldGrp, 5, _V(-1.9, 0.053583,1.429349), _V(0,-1,0), (float)(PI*0.5)));
	v.AddAnimationComponent (anim_array, 0.4, 0.6, mod.Rot (0, ArrayLFoldGrp, 5, _V(0,0.053583,1.429349), _V(-1,0,0), (float)(PI*0.5)));
	v.AddAnimationComponent (anim_array, 0.6, 1,   mod.Scl (0, ArrayLFoldGrp, 4, _V(0,0.053583,1.429349), _V(1,1,4)));
	v.AddAnimationComponent (anim_array, 0,   0.4, mod.Rot (0, ArrayRFoldGrp, 5, _V( 1.9, 0.053583,1.429349), _V(0, 1,0), (float)(PI*0.5)));
	v.AddAnimationComponent (anim_array, 0.4, 0.6, mod.Rot (0, ArrayRFoldGrp, 5, _V(0,0.053583,1.429349), _V(-1,0,0), (float)(PI*0.5)));
	v.AddAnimationComponent (anim_array, 0.6, 1,   mod.Scl (0, ArrayRFoldGrp, 4, _V(0,0.053583,1.429349), _V(1,1,4)));
}

// Src/Vessel/ShuttleA/ShuttleA.cpp:160-180, pods and dock hatches (mesh 0, ShuttleA.msh, 67 groups)
inline void DefineShuttleA (TestVessel &v, TestModule &mod, UINT anim_pod[2], UINT &anim_dock)
{
	UINT *LeftPodGrp = mod.Grp ({4,5,7,9});
	MGROUP_ROTATE *leftpod = mod.Rot (0,LeftPodGrp,4,_V(0,0,0),_V(1,0,0),(float)PI);
	UINT *RightPodGrp = mod.Grp ({6,8,10,11});
	MGROUP_ROTATE *rightpod = mod.Rot (0,RightPodGrp,4,_V(0,0,0),_V(1,0,0),(float)PI);
	anim_pod[0] = v.CreateAnimation (0);
	v.AddAnimationComponent (anim_pod[0], 0.0f,1.0f, leftpod);
	anim_pod[1] = v.CreateAnimation (0);
	v.AddAnimationComponent (anim_pod[1], 0.0f,1.0f, rightpod);

	UINT *UpperDockHatch = mod.Grp ({19});
	MGROUP_ROTATE *upperhatch = mod.Rot (0,UpperDockHatch,1,_V(0,0.554f,18.677401f),_V(-1,0,0),(float)PI);
	UINT *LowerDockHatch = mod.Grp ({18});
	MGROUP_ROTATE *lowerhatch = mod.Rot (0,LowerDockHatch,1,_V(0,-0.554f,18.677401f),_V(1,0,0),(float)PI);
	anim_dock = v.CreateAnimation (0);
	v.AddAnimationComponent (anim_dock,0.0f,1.0f, upperhatch);
	v.AddAnimationComponent (anim_dock,0.2f,1.0f, lowerhatch);
}

#endif // !__COLLANIMTEST_H
