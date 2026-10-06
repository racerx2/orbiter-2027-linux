// not upstream: unit tests for Src/Orbiter/CollAnim (D1 9.2): replica vs client animation port
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>
#include "CollAnimTest.h"

// tree classes: A disjoint, B shared in one anim, C1 anisotropic parent scale, C2 shared, C3 whole

enum { CLS_A, CLS_B, CLS_C1, CLS_C2, CLS_C3, CLS_N };
static const char *clsName[CLS_N] = { "A", "B", "C1", "C2", "C3" };
static const UINT NG = 24;  // groups per test mesh
static const double EXACT_TOL = 1e-12; // replica vs the double instance of the client port (same arithmetic)
static const double FLOAT_TOL = 1e-4;  // replica vs the float client: its own float drift, unit axes and stock paths only

struct Gen {
	TestVessel &v; TestModule &mod; TestRng &r;
	int cls; bool nonunit;
	std::vector<std::vector<UINT>> pool;  // free groups per mesh (A, C1, B sets)

	Gen (TestVessel &v_, TestModule &m_, TestRng &r_, int c, bool nu) : v (v_), mod (m_), r (r_), cls (c), nonunit (nu)
	{
		pool.resize (3);
		for (UINT m = 0; m < 3; m++) {
			for (UINT g = 0; g < NG; g++) pool[m].push_back (g);
			for (UINT i = NG-1; i > 0; i--) std::swap (pool[m][i], pool[m][r.I (i+1)]);
		}
	}
	Vector RandAxis ()
	{
		Vector a (r.N(), r.N(), r.N());
		double l = a.length ();
		if (l < 1e-3) { a = Vector (1, 0, 0); l = 1; }
		a /= l;
		return nonunit ? a * r.U (0.6, 1.4) : a;
	}
	VECTOR3 V3 (const Vector &a) { return _V (a.x, a.y, a.z); }
	VECTOR3 RandV (double s) { return _V (s*r.N(), s*r.N(), s*r.N()); }
	UINT PickMesh ()
	{
		if (r.P (0.04)) return 7;  // beyond nmesh
		return r.I (3);
	}
	std::vector<UINT> Take (UINT mesh, UINT n)
	{
		std::vector<UINT> g;
		for (UINT i = 0; i < n; i++) {
			if (mesh < 3 && !pool[mesh].empty()) { g.push_back (pool[mesh].back()); pool[mesh].pop_back (); }
			else g.push_back (NG + r.I (8));
		}
		return g;
	}
	std::vector<UINT> Shared (UINT n) { std::vector<UINT> g; for (UINT i = 0; i < n; i++) g.push_back (r.I (6)); return g; }
	void Extras (std::vector<UINT> &g)
	{
		if (r.P (0.06)) g.push_back (NG + r.I (8));               // bad group index
		if (!g.empty() && r.P (0.05)) g.push_back (g[r.I ((UINT)g.size())]); // group listed twice
	}
	std::vector<ANIMATIONCOMP*> Live () const
	{
		std::vector<ANIMATIONCOMP*> c;
		for (UINT i = 0; i < v.nanim; i++) for (UINT k = 0; k < v.anim[i].ncomp; k++) c.push_back (v.anim[i].comp[k]);
		return c;
	}
	// one component of animation an; set = B's group set of the animation
	void AddComp (UINT an, int k, int nc, UINT setMesh, const std::vector<UINT> &set)
	{
		int type;
		double t = r.U ();
		if (cls == CLS_C1) type = t < 0.45 ? 1 : t < 0.6 ? 2 : t < 0.95 ? 3 : 0;
		else type = t < 0.55 ? 1 : t < 0.75 ? 2 : t < 0.92 ? 3 : 0;
		bool lvl = r.P (0.12);
		UINT mesh = (cls == CLS_B && !lvl) ? setMesh : PickMesh ();
		std::vector<UINT> g;
		UINT *grp = nullptr; UINT ngrp = 0;
		if (lvl) {
			mesh = LOCALVERTEXLIST;
			std::vector<VECTOR3> vt;
			for (UINT i = 0, n = 1 + r.I (3); i < n; i++) vt.push_back (RandV (2.0));
			grp = MAKEGROUPARRAY (mod.Vtx (vt)); ngrp = (UINT)vt.size();
		} else {
			bool whole = (cls == CLS_C3 && r.P (0.35));
			if (!whole) {
				UINT n = 1 + r.I (3);
				if (cls == CLS_A || cls == CLS_C1) g = Take (mesh, n);
				else if (cls == CLS_B) { for (UINT i = 0; i < n; i++) g.push_back (set[r.I ((UINT)set.size())]); }
				else g = Shared (n);
				Extras (g);
				grp = mod.Grp (g); ngrp = (UINT)g.size();
			}
		}
		double s0, s1;
		if (cls == CLS_B) { s0 = (double)k/nc; s1 = (double)(k+1)/nc; }
		else if (r.P (0.4)) { s0 = 0; s1 = 1; }
		else { s0 = r.U (0, 0.7); s1 = s0 + r.U (0.1, 0.8); }
		if (r.P (0.05)) std::swap (s0, s1);  // reversed range
		else if (r.P (0.04)) s1 = s0;         // degenerate range
		bool aniso = (cls == CLS_C1 || ((cls == CLS_C2 || cls == CLS_C3) && r.P (0.5)));
		MGROUP_TRANSFORM *tr;
		switch (type) {
		case 1: tr = mod.Rot (mesh, grp, ngrp, RandV (2.0), V3 (RandAxis ()), (float)r.U (-3, 3)); break;
		case 2: tr = mod.Lin (mesh, grp, ngrp, RandV (1.0)); break;
		case 3: {
			double si = r.U (0.6, 1.4);
			VECTOR3 sc = aniso ? _V (r.U (0.6, 1.4), r.U (0.6, 1.4), r.U (0.6, 1.4)) : _V (si, si, si);
			tr = mod.Scl (mesh, grp, ngrp, RandV (2.0), sc);
			} break;
		default: tr = mod.Nul (mesh, grp, ngrp); break;
		}
		ANIMATIONCOMP *parent = nullptr;
		std::vector<ANIMATIONCOMP*> live = Live ();
		if (!live.empty() && (r.P (0.45) || (cls == CLS_C1 && type == 3))) parent = live[r.I ((UINT)live.size())];
		v.AddAnimationComponent (an, s0, s1, tr, parent);
	}
	void AddAnim ()
	{
		static const double defs[3] = { 0.0, 0.5, 1.0 };
		UINT an = v.CreateAnimation (defs[r.I (3)]);
		int nc = (cls == CLS_B ? 2 : 1) + (int)r.I (cls == CLS_B ? 2 : 3);
		UINT setMesh = r.I (3);
		std::vector<UINT> set;
		if (cls == CLS_B) set = Take (setMesh, 2 + r.I (3));
		for (int k = 0; k < nc; k++) AddComp (an, k, nc, setMesh, set);
	}
};

// next state of a random path: small steps or coarse jumps, also outside [0,1]
static double RandState (TestRng &r, double cur, bool coarse)
{
	static const double jumps[7] = { 0.0, 0.25, 0.5, 0.75, 1.0, -0.3, 1.3 };
	if (coarse) return jumps[r.I (7)];
	return std::min (1.25, std::max (-0.25, cur + r.U (-0.08, 0.08)));
}

enum { EV_NONE, EV_INSMESH, EV_DELMESH, EV_ADD, EV_DEL, EV_CLEAR, EV_N };

// one vessel with its module memory and generator; twin worlds from one seed hold identical trees
struct World {
	TestVessel v; TestModule mod; TestRng r; Gen gen;
	World (uint64_t seed, int cls, bool nonunit) : r (seed), gen (v, mod, r, cls, nonunit) {}
};

struct RunStats { double errF = 0, errD = 0; int nfF = 0, nfD = 0, trees = 0, frames = 0, events = 0; bool workOk = true; };

// float client: max over non-overflow comparisons, overflows counted; double: all, +inf fails
static void Compare (RunStats &st, const RefClient &cl, const RefClientD &cd, const CollAnim &ca, const TestVessel &va, const TestVessel &vb)
{
	double eF = RefCompare (cl, ca, va, &st.nfF);
	if (std::isfinite (eF)) st.errF = std::max (st.errF, eF);
	double eD = RefCompare (cd, ca, vb, &st.nfD);
	if (!(eD <= st.errD)) st.errD = eD;
}

// one random tree, path, up to two events; float client on world a (replica hooks), double on b
static void RunTree (int cls, bool nonunit, uint64_t seed, int fmin, int fmax, bool events, RunStats &st)
{
	uint64_t s = seed * 0x100000001B3ull + (uint64_t)cls * 7919 + (nonunit ? 1 : 0);
	World a (s, cls, nonunit), b (s, cls, nonunit);
	World *w[2] = { &a, &b };
	TestRng path (s ^ 0x5DEECE66Dull);
	CollAnim ca;
	a.v.coll = &ca;
	bool miss = path.P (0.3);
	for (World *x : w) {
		x->v.meshGrp = { NG, NG, miss ? 0u : NG };
		for (UINT i = 0, n = 2 + x->r.I (5); i < n; i++) x->gen.AddAnim ();
	}
	for (UINT i = 0; i < a.v.nanim; i++) {
		static const double init[6] = { 0.0, 1.0, 0.3, 0.7, -0.25, 1.3 };
		if (path.P (0.5)) a.v.anim[i].state = b.v.anim[i].state = init[path.I (6)];
	}
	RefClient cl (&a.v);   // visual created: one step from defstate (VVessel.cpp:80-94)
	RefClientD cd (&b.v);
	a.v.Step ();            // first P1: one step from defstate
	Compare (st, cl, cd, ca, a.v, b.v);

	int nf = fmin + (int)path.I ((UINT)(fmax - fmin + 1));
	int ev1 = events ? (int)path.I ((UINT)nf) : -1, ev2 = events && path.P (0.5) ? (int)path.I ((UINT)nf) : -1;
	bool coarse = path.P (0.25);
	for (int f = 0; f < nf; f++) {
		for (UINT i = 0; i < a.v.nanim; i++)
			if (path.P (0.5)) a.v.anim[i].state = b.v.anim[i].state = RandState (path, a.v.anim[i].state, coarse || path.P (0.05));
		a.v.Step ();
		cl.UpdateAnimations ();
		cd.UpdateAnimations ();
		Compare (st, cl, cd, ca, a.v, b.v);
		st.frames++;
		if (f == ev1 || f == ev2) {
			// between frames: both sides are in step, so INSMESH has no pending change
			int ev = 1 + (int)path.I (EV_N - 1);
			UINT m = path.I (3);
			bool newAnim = path.P (0.5);
			uint32_t pick = (uint32_t)path.Next ();
			switch (ev) {
			case EV_INSMESH:
				for (World *x : w) x->v.meshGrp[m] = NG;
				cl.InsertMesh (m); cd.InsertMesh (m); ca.OnMeshInsert (m);
				break;
			case EV_DELMESH:
				for (World *x : w) x->v.meshGrp[m] = 0;
				cl.DelMesh (m); cd.DelMesh (m); ca.OnMeshDelete (m);
				break;
			case EV_ADD:
				for (World *x : w) {
					if (newAnim || !x->v.nanim) x->gen.AddAnim ();
					else x->gen.AddComp (pick % x->v.nanim, 0, 1, m, x->gen.Take (m, 2));
				}
				break;
			case EV_DEL:
				for (World *x : w) {
					std::vector<ANIMATIONCOMP*> live = x->gen.Live ();
					if (live.empty()) break;
					ANIMATIONCOMP *c = live[pick % live.size()];
					for (UINT i = 0; i < x->v.nanim; i++) if (x->v.DelAnimationComponent (i, c)) break;
				}
				break;
			case EV_CLEAR:
				for (World *x : w) {
					x->v.ClearAnimations ();
					for (UINT i = 0, n = 1 + x->r.I (4); i < n; i++) x->gen.AddAnim ();
				}
				break;
			}
			st.events++;
			if (CollAnimProbe::nWork (ca) != a.v.nComp ()) st.workOk = false;
			Compare (st, cl, cd, ca, a.v, b.v);
		}
	}
	st.trees++;
}

// pass: double client exact and finite; float client drift checked on short unit-axis paths
static void RunClasses (int ntree, int fmin, int fmax, const char *label, bool checkFloat)
{
	for (int nu = 0; nu < 2; nu++) {
		for (int cls = 0; cls < CLS_N; cls++) {
			RunStats st;
			for (int s = 0; s < ntree; s++) RunTree (cls, nu != 0, (uint64_t)s, fmin, fmax, (s % 2) == 1, st);
			std::printf ("CollAnim %s: class %-2s %-8s axes, %4d trees, %6d frames, %4d events: replica vs client float %.2e (%d non-finite), double %.2e (%d non-finite)\n",
				label, clsName[cls], nu ? "non-unit" : "unit", st.trees, st.frames, st.events, st.errF, st.nfF, st.errD, st.nfD);
			INFO ("class " << clsName[cls] << (nu ? " non-unit" : " unit"));
			CHECK (st.workOk);
			CHECK (st.errD < EXACT_TOL);
			CHECK (st.nfD == 0);
			if (checkFloat && !nu) { CHECK (st.errF < FLOAT_TOL); CHECK (st.nfF == 0); }
		}
	}
}

TEST_CASE("CollAnim equals the client's incremental mode on random trees", "[collanim]")
{
	RunClasses (40, 60, 160, "fast", true);
}

TEST_CASE("CollAnim equals the client's incremental mode, 1000 trees per class", "[collanim][.slow]")
{
	RunClasses (1000, 60, 600, "slow", false);
}

// stock definitions

static Vector Door (const RMAT &R, const Vector &p) { return RefPoint (R, p); }

TEST_CASE("Atlantis gear door: one step and 30 frames equal the client; absolute mode printed", "[collanim]")
{
	const Vector edge (4.35-1.5, -2.64, -1.69); // a door point 1.5 m from the hinge (Atlantis.cpp:678-679)
	for (int nfr : { 1, 30 }) {
		TestVessel v; TestModule mod; AtlantisAnims a; CollAnim ca;
		v.coll = &ca;
		v.meshGrp = { 1, ATL_NGRP, 1 };
		DefineAtlantis (v, mod, a);
		std::unique_ptr<RefClient> cl;
		if (nfr == 1) {
			v.SetAnimation (a.anim_gear, 1.0);  // loaded with gear down
			cl.reset (new RefClient (&v));
			v.Step ();
		} else {
			cl.reset (new RefClient (&v));
			v.Step ();
			for (int k = 1; k <= nfr; k++) { v.SetAnimation (a.anim_gear, (double)k/nfr); v.Step (); cl->UpdateAnimations (); }
		}
		RMAT R; CollAffine F;
		REQUIRE (cl->GroupTF (1, ATL_geardoorR, R));
		REQUIRE (ca.GroupTransform (1, ATL_geardoorR, F));
		double d = (Door (R, edge) - CollApply (F, edge)).length ();

		// the client's absolute mode on its own module memory (StoreDefaultState normalises the axis)
		TestVessel va; TestModule moda; AtlantisAnims aa;
		va.meshGrp = v.meshGrp;
		DefineAtlantis (va, moda, aa);
		va.SetAnimation (aa.anim_gear, 1.0);
		RefClient abs (&va, true);
		RMAT Ra;
		REQUIRE (abs.GroupTF (1, ATL_geardoorR, Ra));
		double dabs = (Door (Ra, edge) - Door (R, edge)).length ();
		std::printf ("CollAnim gear door, %d frame(s): replica vs client %.2e m, client absolute vs incremental %.3f m\n", nfr, d, dabs);
		CHECK (d < 1e-5);
		CHECK (dabs > 0.1); // the modes differ for this non-unit axis (D1 6.5); printed, not a pass criterion
	}
}

// random state path on a stock vessel; vd is the twin for the double client
static void StockPath (TestVessel &v, TestVessel &vd, CollAnim &ca, RefClient &cl, RefClientD &cd, TestRng &r, int nf,
	const std::vector<UINT> &an, double &eF, double &eD)
{
	eF = std::max (eF, RefCompare (cl, ca, v));
	eD = std::max (eD, RefCompare (cd, ca, vd));
	for (int f = 0; f < nf; f++) {
		for (UINT i : an) if (r.P (0.3)) v.anim[i].state = vd.anim[i].state = std::min (1.0, std::max (0.0, v.anim[i].state + r.U (-0.05, 0.05)));
		v.Step ();
		cl.UpdateAnimations ();
		cd.UpdateAnimations ();
		eF = std::max (eF, RefCompare (cl, ca, v));
		eD = std::max (eD, RefCompare (cd, ca, vd));
	}
}

TEST_CASE("Stock definitions: Atlantis, HST, ShuttleA equal the client", "[collanim]")
{
	{
		TestVessel v, vd; TestModule mod, modd; AtlantisAnims a, ad; CollAnim ca; TestRng r (11);
		v.coll = &ca;
		v.meshGrp = vd.meshGrp = { 1, ATL_NGRP, 1 };
		DefineAtlantis (v, mod, a); DefineAtlantis (vd, modd, ad);
		RefClient cl (&v); RefClientD cd (&vd);
		v.Step ();
		std::vector<UINT> an; for (UINT i = 0; i < v.nanim; i++) an.push_back (i);
		double eF = 0, eD = 0;
		StockPath (v, vd, ca, cl, cd, r, 600, an, eF, eD);
		std::printf ("CollAnim Atlantis 600 frames: replica vs client float %.2e, double %.2e\n", eF, eD);
		CHECK (eD < EXACT_TOL);
		CHECK (eF < FLOAT_TOL);
		CHECK (CollAnimProbe::nWork (ca) == v.nComp ());
	}
	{
		TestVessel v, vd; TestModule mod, modd; CollAnim ca; TestRng r (12);
		UINT ant, hatch, arr;
		v.coll = &ca;
		v.meshGrp = vd.meshGrp = { 104 };
		DefineHST (v, mod, ant, hatch, arr); DefineHST (vd, modd, ant, hatch, arr);
		v.SetAnimation (arr, 0.0); vd.SetAnimation (arr, 0.0); // arrays folded at load
		RefClient cl (&v); RefClientD cd (&vd);
		v.Step ();
		double eF = 0, eD = 0;
		StockPath (v, vd, ca, cl, cd, r, 600, { ant, hatch, arr }, eF, eD);
		std::printf ("CollAnim HST 600 frames: replica vs client float %.2e, double %.2e\n", eF, eD);
		CHECK (eD < EXACT_TOL);
		CHECK (eF < FLOAT_TOL);
	}
	{
		TestVessel v, vd; TestModule mod, modd; CollAnim ca; TestRng r (13);
		UINT pod[2], dock;
		v.coll = &ca;
		v.meshGrp = vd.meshGrp = { 67 };
		DefineShuttleA (v, mod, pod, dock); DefineShuttleA (vd, modd, pod, dock);
		RefClient cl (&v); RefClientD cd (&vd);
		v.Step ();
		double eF = 0, eD = 0;
		StockPath (v, vd, ca, cl, cd, r, 600, { pod[0], pod[1], dock }, eF, eD);
		std::printf ("CollAnim ShuttleA 600 frames: replica vs client float %.2e, double %.2e\n", eF, eD);
		CHECK (eD < EXACT_TOL);
		CHECK (eF < FLOAT_TOL);
	}
}

// lifecycle and divergences

TEST_CASE("Lifecycle: RMS component delete and ClearAnimations keep matrices", "[collanim]")
{
	TestVessel v; TestModule mod; AtlantisAnims a; CollAnim ca; TestRng r (21);
	v.coll = &ca;
	v.meshGrp = { 1, ATL_NGRP, 1 };
	DefineAtlantis (v, mod, a);
	RefClient cl (&v);
	v.Step ();
	REQUIRE (CollAnimProbe::nWork (ca) == v.nComp ());
	std::vector<UINT> arm = { a.anim_arm_sy, a.anim_arm_sp, a.anim_arm_ep, a.anim_arm_wp, a.anim_arm_wy, a.anim_arm_wr };
	for (int f = 0; f < 40; f++) {
		for (UINT i : arm) v.anim[i].state = std::min (1.0, std::max (0.0, v.anim[i].state + r.U (-0.03, 0.03)));
		v.Step (); cl.UpdateAnimations ();
		REQUIRE (CollAnimProbe::nWork (ca) == v.nComp ());
	}
	CHECK (RefCompare (cl, ca, v) < FLOAT_TOL);

	// elbow pitch: its children (wrist pitch, wrist yaw, wrist roll) go with it (Vessel.cpp:5837-5857)
	CollAffine before[3], after;
	const UINT grp[3] = { ATL_radii, ATL_wrist, ATL_endeffecter };
	for (int i = 0; i < 3; i++) REQUIRE (ca.GroupTransform (1, grp[i], before[i]));
	uint64_t ver = ca.Version ();
	UINT ncomp = v.nComp ();
	REQUIRE (v.DelAnimationComponent (a.anim_arm_ep, a.rms[2]));
	CHECK (v.nComp () == ncomp - 4);
	CHECK (CollAnimProbe::nWork (ca) == v.nComp ());
	CHECK (ca.Version () == ver + 4);
	for (int i = 0; i < 3; i++) {
		REQUIRE (ca.GroupTransform (1, grp[i], after));
		for (int k = 0; k < 9; k++) CHECK (after.A.data[k] == before[i].A.data[k]);
	}
	for (int f = 0; f < 40; f++) {
		for (UINT i : arm) v.anim[i].state = std::min (1.0, std::max (0.0, v.anim[i].state + r.U (-0.03, 0.03)));
		v.Step (); cl.UpdateAnimations ();
		REQUIRE (CollAnimProbe::nWork (ca) == v.nComp ());
	}
	CHECK (RefCompare (cl, ca, v) < FLOAT_TOL);

	// ClearAnimations: components forgotten, matrices and current states kept (VVessel.cpp:499-503)
	REQUIRE (ca.GroupTransform (1, ATL_Humerus, before[0]));
	v.ClearAnimations ();
	CHECK (CollAnimProbe::nWork (ca) == 0);
	REQUIRE (ca.GroupTransform (1, ATL_Humerus, after));
	for (int k = 0; k < 9; k++) CHECK (after.A.data[k] == before[0].A.data[k]);
	v.Step (); cl.UpdateAnimations ();
	CHECK (RefCompare (cl, ca, v) < FLOAT_TOL);

	// re-created animations reuse ids 0.. and start from the kept current states
	TestModule mod2; AtlantisAnims a2;
	DefineAtlantis (v, mod2, a2);
	CHECK (CollAnimProbe::nWork (ca) == v.nComp ());
	v.Step (); cl.UpdateAnimations ();
	CHECK (RefCompare (cl, ca, v) < FLOAT_TOL);
	TestRng r2 (22);
	for (int f = 0; f < 100; f++) {
		for (UINT i = 0; i < v.nanim; i++) if (r2.P (0.3)) v.anim[i].state = std::min (1.0, std::max (0.0, v.anim[i].state + r2.U (-0.05, 0.05)));
		v.Step (); cl.UpdateAnimations ();
	}
	CHECK (RefCompare (cl, ca, v) < FLOAT_TOL);
}

TEST_CASE("INSMESH: no pending change equals the client; after a pending change differs (divergence 2)", "[collanim]")
{
	for (int pending = 0; pending < 2; pending++) {
		TestVessel v; TestModule mod; CollAnim ca;
		v.coll = &ca;
		v.meshGrp = { 4, 4 };
		UINT an = v.CreateAnimation (0);
		v.AddAnimationComponent (an, 0, 1, mod.Rot (0, mod.Grp ({0}), 1, _V(1,0,0), _V(0,1,0), 1.0f));
		v.AddAnimationComponent (an, 0, 1, mod.Rot (1, mod.Grp ({0}), 1, _V(0,0,1), _V(1,0,0), 0.5f));
		RefClient cl (&v);
		v.Step ();
		v.SetAnimation (an, 0.3); v.Step (); cl.UpdateAnimations ();
		if (pending) v.SetAnimation (an, 0.8);
		cl.InsertMesh (1); ca.OnMeshInsert (1);  // INSMESH broadcast inside the frame (VVessel.cpp:332-394)
		if (!pending) v.SetAnimation (an, 0.8);
		v.Step (); cl.UpdateAnimations ();
		RMAT R0, R1; CollAffine F0, F1;
		cl.GroupTF (0, 0, R0); ca.GroupTransform (0, 0, F0);
		cl.GroupTF (1, 0, R1); ca.GroupTransform (1, 0, F1);
		CHECK (RefDiff (R1, F1) < 1e-6);
		if (pending) CHECK (RefDiff (R0, F0) > 0.1); // the client loses the increment on mesh 0; a future client fix shows up here
		else CHECK (RefDiff (R0, F0) < 1e-6);
	}
}

// API rules

TEST_CASE("GroupTransform, Version and mesh events", "[collanim]")
{
	TestVessel v; TestModule mod; CollAnim ca;
	v.coll = &ca;
	v.meshGrp = { 4, 4 };
	CollAffine F;
	F.t = Vector (1, 2, 3);
	CHECK_FALSE (ca.GroupTransform (0, 0, F));
	CHECK (F.t.length () == 0);
	CHECK (F.A.m11 == 1); CHECK (F.A.m22 == 1); CHECK (F.A.m33 == 1); CHECK (F.A.m12 == 0);
	CHECK (ca.Version () == 0);

	UINT an = v.CreateAnimation (0);
	ANIMATIONCOMP *c0 = v.AddAnimationComponent (an, 0, 1, mod.Lin (0, mod.Grp ({1}), 1, _V(1,0,0)));
	ANIMATIONCOMP *c1 = v.AddAnimationComponent (an, 0, 1, mod.Lin (1, nullptr, 0, _V(0,2,0)));  // whole mesh 1
	CHECK (ca.Version () == 2);
	v.SetAnimation (an, 0.5);
	CHECK (v.Step ());
	CHECK_FALSE (v.Step ());                 // nothing changed
	CHECK (ca.Version () == 2);              // steps do not bump the version
	REQUIRE (ca.GroupTransform (0, 1, F));
	CHECK (F.t.x == 0.5);
	CHECK_FALSE (ca.GroupTransform (0, 0, F));
	REQUIRE (ca.GroupTransform (1, 3, F));   // whole-mesh transform reaches every group
	CHECK (F.t.y == 1.0);

	ca.OnMeshInsert (1);                     // fresh client mesh: identity
	CHECK_FALSE (ca.GroupTransform (1, 3, F));
	CHECK (ca.GroupTransform (0, 1, F));
	ca.OnMeshDelete (COLLANIM_LVL);          // all meshes
	CHECK_FALSE (ca.GroupTransform (0, 1, F));
	CHECK (ca.Version () == 2);

	v.DelAnimationComponent (an, c1);
	CHECK (ca.Version () == 3);
	ca.OnDel (c1);                           // unknown pointer: no change
	CHECK (ca.Version () == 3);
	v.DelAnimationComponent (an, c0);
	v.ClearAnimations ();
	CHECK (ca.Version () == 5);
}

TEST_CASE("Missing mesh stops propagation; LOCALVERTEXLIST nodes pass it on", "[collanim]")
{
	TestVessel v; TestModule mod; CollAnim ca;
	v.coll = &ca;
	v.meshGrp = { 4, 0, 4 };   // mesh 1 missing
	VECTOR3 *pts = mod.Vtx ({ _V(1,1,1) });
	UINT an = v.CreateAnimation (0);
	ANIMATIONCOMP *p = v.AddAnimationComponent (an, 0, 1, mod.Lin (1, mod.Grp ({0}), 1, _V(1,0,0)));       // on the missing mesh
	v.AddAnimationComponent (an, 0, 1, mod.Lin (0, mod.Grp ({0}), 1, _V(0,1,0)), p);                         // child: untouched
	ANIMATIONCOMP *l = v.AddAnimationComponent (an, 0, 1, mod.Lin (LOCALVERTEXLIST, MAKEGROUPARRAY (pts), 1, _V(0,0,1)));
	v.AddAnimationComponent (an, 0, 1, mod.Lin (2, mod.Grp ({3}), 1, _V(0,0,0)), l);                         // child of a vertex list
	RefClient cl (&v);
	v.Step ();
	v.SetAnimation (an, 1.0);
	v.Step (); cl.UpdateAnimations ();
	CollAffine F;
	CHECK (ca.GroupTransform (0, 0, F));
	CHECK (F.t.y == 1.0);                      // its own translation only, not the missing parent's
	REQUIRE (ca.GroupTransform (2, 3, F));
	CHECK (F.t.z == 1.0);                      // the vertex list's T reached it
	CHECK (pts[0].z == 2.0);                   // the client moved module memory; the replica never writes it
	CHECK (RefCompare (cl, ca, v) < 1e-6);
}

TEST_CASE("Zero axis, NaN state and a NULL transform", "[collanim]")
{
	TestVessel v; TestModule mod; CollAnim ca;
	v.coll = &ca;
	v.meshGrp = { 6 };
	int nlog = 0;
	static int *s_nlog; s_nlog = &nlog;
	g_collLog = [] (int, const char *) { (*s_nlog)++; };

	UINT a0 = v.CreateAnimation (0);
	v.AddAnimationComponent (a0, 0, 1, mod.Rot (0, mod.Grp ({0}), 1, _V(1,0,0), _V(0,0,0), 1.0f));  // zero axis: identity
	v.AddAnimationComponent (a0, 0, 1, mod.Scl (0, mod.Grp ({1}), 1, _V(0,0,0), _V(0,0,0)));        // scale to zero
	UINT a1 = v.CreateAnimation (0);
	ANIMATIONCOMP *par = v.AddAnimationComponent (a1, 0, 1, mod.Rot (0, mod.Grp ({2}), 1, _V(0,0,0), _V(0,1,0), 1.0f));
	UINT a3 = v.CreateAnimation (0);
	v.AddAnimationComponent (a3, 0, 1, mod.Rot (0, mod.Grp ({4}), 1, _V(0,0,0), _V(0,0,0), 1.0f), par); // zero axis moved by a parent
	UINT a2 = v.CreateAnimation (0);
	ANIMATIONCOMP *nul = v.AddAnimationComponent (a2, 0, 1, nullptr);                                    // module bug
	v.AddAnimationComponent (a2, 0, 1, mod.Lin (0, mod.Grp ({3}), 1, _V(1,0,0)), nul);
	v.Step ();

	v.SetAnimation (a0, 1.0); v.SetAnimation (a2, 1.0);
	v.Step ();
	CollAffine F;
	REQUIRE (ca.GroupTransform (0, 0, F));
	CHECK (F.A.m11 == 1.0); CHECK (F.A.m22 == 1.0); CHECK (F.t.length () == 0);
	REQUIRE (ca.GroupTransform (0, 3, F));
	CHECK (F.t.x == 1.0);                     // NULL transform: an empty node, its child still animates
	v.SetAnimation (a1, 1.0);                 // moves the child's zero axis: kept zero (the client gets NaN)
	v.Step ();
	v.SetAnimation (a3, 1.0);
	v.Step ();
	CollAffine Fp;
	REQUIRE (ca.GroupTransform (0, 4, F));
	REQUIRE (ca.GroupTransform (0, 2, Fp));
	for (int k = 0; k < 9; k++) CHECK (std::isfinite (F.A.data[k]));
	for (int k = 0; k < 9; k++) CHECK (F.A.data[k] == Fp.A.data[k]); // only the parent's rotation: its own zero axis is the identity

	// scale back from zero divides by zero in the client; the replica skips it and stays finite
	v.SetAnimation (a0, 0.0);
	v.Step ();
	REQUIRE (ca.GroupTransform (0, 1, F));
	for (int k = 0; k < 9; k++) CHECK (std::isfinite (F.A.data[k]));

	// NaN state: skipped, current state kept; a later finite state animates from the kept one
	v.SetAnimation (a0, NAN);
	CHECK_FALSE (v.Step ());
	v.SetAnimation (a0, 1.0);
	CHECK (v.Step ());
	CHECK (nlog >= 4);
	g_collLog = nullptr;
}

TEST_CASE("Signatures: Atlantis mesh 1 has 26 classes; equal signatures stay equal", "[collanim]")
{
	{
		TestVessel v; TestModule mod; AtlantisAnims a; CollAnim ca;
		v.coll = &ca;
		v.meshGrp = { 1, ATL_NGRP, 1 };
		DefineAtlantis (v, mod, a);
		v.Step ();
		std::vector<uint8_t> pr = v.Present ();
		std::vector<uint64_t> sig;
		ca.Signatures (v.anim, v.nanim, 1, ATL_NGRP, pr.data(), (uint32_t)pr.size(), sig);
		std::vector<uint64_t> u = sig;
		std::sort (u.begin(), u.end());
		u.erase (std::unique (u.begin(), u.end()), u.end());
		CHECK (u.size() == 26);   // D1 3.3 worked example
		CHECK (sig[ATL_nosewheel] == sig[ATL_nosegear]);
		CHECK (sig[ATL_cargodooroutR] == sig[ATL_cargodoorinR]);
		CHECK (sig[ATL_cargodooroutR] != sig[ATL_radiatorFR]);
		CHECK (sig[ATL_radii] == sig[ATL_RMScamera]);
		CHECK (sig[ATL_flapR] == sig[ATL_aileronR]);
		CHECK (sig[ATL_flapR] != sig[ATL_flapL]);
		CHECK (sig[0] == 0);
	}
	{
		// whole-mesh components are not in their own mesh's signatures; their children's groups are
		TestVessel v; TestModule mod; CollAnim ca;
		v.coll = &ca;
		v.meshGrp = { 4 };
		UINT an = v.CreateAnimation (0);
		ANIMATIONCOMP *w = v.AddAnimationComponent (an, 0, 1, mod.Lin (0, nullptr, 0, _V(1,0,0)));
		v.AddAnimationComponent (an, 0, 1, mod.Lin (0, mod.Grp ({2}), 1, _V(0,1,0)), w);
		std::vector<uint8_t> pr = v.Present ();
		std::vector<uint64_t> sig;
		ca.Signatures (v.anim, v.nanim, 0, 4, pr.data(), 1, sig);
		CHECK (sig[0] == 0); CHECK (sig[1] == 0); CHECK (sig[3] == 0);
		CHECK (sig[2] != 0);
	}
	// no events: groups with equal (signature, matrix) after step 1 keep bitwise-equal matrices
	for (int cls = 0; cls < CLS_N; cls++) {
		for (uint64_t s = 0; s < 40; s++) {
			TestRng r (s * 31 + (uint64_t)cls);
			TestVessel v; TestModule mod; CollAnim ca;
			v.coll = &ca;
			v.meshGrp = { NG, NG, NG };
			Gen gen (v, mod, r, cls, true);
			for (UINT i = 0, n = 2 + r.I (5); i < n; i++) gen.AddAnim ();
			v.Step ();
			std::vector<uint8_t> pr = v.Present ();
			std::vector<std::vector<uint64_t>> sig (3);
			std::vector<std::pair<std::pair<UINT,UINT>, std::pair<UINT,UINT>>> same;
			for (UINT m = 0; m < 3; m++) {
				ca.Signatures (v.anim, v.nanim, m, NG, pr.data(), 3, sig[m]);
				for (UINT g = 0; g < NG; g++) for (UINT h = g+1; h < NG; h++) {
					CollAffine Fg, Fh;
					ca.GroupTransform (m, g, Fg); ca.GroupTransform (m, h, Fh);
					bool eq = true;
					for (int k = 0; k < 9; k++) eq = eq && Fg.A.data[k] == Fh.A.data[k];
					for (int k = 0; k < 3; k++) eq = eq && Fg.t.data[k] == Fh.t.data[k];
					if (sig[m][g] == sig[m][h] && eq) same.push_back ({ { m, g }, { m, h } });
				}
			}
			for (int f = 0; f < 80; f++) {
				for (UINT i = 0; i < v.nanim; i++) if (r.P (0.5)) v.anim[i].state = RandState (r, v.anim[i].state, r.P (0.1));
				v.Step ();
			}
			bool ok = true;
			for (auto &p : same) {
				CollAffine Fg, Fh;
				ca.GroupTransform (p.first.first, p.first.second, Fg); ca.GroupTransform (p.second.first, p.second.second, Fh);
				for (int k = 0; k < 9; k++) ok = ok && Fg.A.data[k] == Fh.A.data[k];
				for (int k = 0; k < 3; k++) ok = ok && Fg.t.data[k] == Fh.t.data[k];
			}
			INFO ("class " << clsName[cls] << " seed " << s);
			CHECK (ok);
		}
	}
}

TEST_CASE("Signatures: a component without snapshot reaches nobody, as in Animate (code review C-A-a 12)", "[collanim]")
{
	// x added before the hook (upstream DelAnimationComponent bug can leave it in A->comp)
	TestVessel v; TestModule mod; CollAnim ca;
	v.meshGrp = { 4 };
	UINT an = v.CreateAnimation (0);
	ANIMATIONCOMP *x = v.AddAnimationComponent (an, 0, 1, mod.Lin (0, mod.Grp ({0}), 1, _V(1,0,0)));
	v.coll = &ca;
	v.AddAnimationComponent (an, 0, 1, mod.Lin (0, mod.Grp ({1}), 1, _V(0,1,0)), x);
	// twin: same component slots, no parent link
	TestVessel w; TestModule modw; CollAnim cw;
	w.meshGrp = { 4 };
	UINT bn = w.CreateAnimation (0);
	w.AddAnimationComponent (bn, 0, 1, modw.Lin (0, modw.Grp ({0}), 1, _V(1,0,0)));
	w.coll = &cw;
	w.AddAnimationComponent (bn, 0, 1, modw.Lin (0, modw.Grp ({1}), 1, _V(0,1,0)));
	std::vector<uint8_t> pr = v.Present ();
	std::vector<uint64_t> sv, sw;
	ca.Signatures (v.anim, v.nanim, 0, 4, pr.data(), 1, sv);
	cw.Signatures (w.anim, w.nanim, 0, 4, pr.data(), 1, sw);
	CHECK (sv[0] == 0);
	CHECK (sv[1] != 0);
	CHECK (sv == sw);
	// and the matrices agree: x is skipped, its child moves by its own step only
	v.SetAnimation (an, 1.0); w.SetAnimation (bn, 1.0);
	v.Step (); w.Step ();
	CollAffine F, Fw;
	REQUIRE (ca.GroupTransform (0, 1, F));
	REQUIRE (cw.GroupTransform (0, 1, Fw));
	CHECK (F.t.x == 0.0); CHECK (F.t.y == 1.0);
	CHECK (F.t.y == Fw.t.y);
	CHECK_FALSE (ca.GroupTransform (0, 0, F));
}

TEST_CASE("Reference comparison: non-finite elements are never hidden (code review C-A-a 7)", "[collanim]")
{
	CollAffine F;
	RMAT R;
	D3DMAT_Identity (&R);
	int nf = 0;
	CHECK (RefDiff (R, F, &nf) == 0.0);
	CHECK (nf == 0);
	R._22 = std::numeric_limits<float>::quiet_NaN();
	CHECK (RefDiff (R, F, &nf) == HUGE_VAL);
	CHECK (nf == 1);
	R._22 = std::numeric_limits<float>::infinity();
	CHECK (RefDiff (R, F, &nf) == HUGE_VAL);
	F.A.m22 = std::numeric_limits<double>::infinity();
	CHECK (RefDiff (R, F, &nf) == 0.0);  // identical infinities: no difference, still counted
	CHECK (nf == 3);
	R._22 = 1.0f;
	F.A.m22 = std::numeric_limits<double>::quiet_NaN();
	CHECK (RefDiff (R, F) == HUGE_VAL);  // a NaN in the replica fails the exact check
	R._22 = std::numeric_limits<float>::quiet_NaN();
	CHECK (RefDiff (R, F) == 0.0);
	R._22 = 1.0f; F.A.m22 = 1.0;
	R._41 = 1e30f;
	F.t.x = HUGE_VAL;
	CHECK (RefDiff (R, F) == HUGE_VAL);
}
