// not upstream: unit tests of the E1 propagator mirror (A8 compensation, A14 h_rest, Orbiter.cfg keys)
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <map>
#include <string>
#include "CollOrbMirror.h"

namespace {
CollOrbState Body (const Vector &a)
{
	CollOrbState o;
	o.s.R = IMatrix (); o.s.Q.Set (o.s.R);
	o.s.pos = Vector (10, 20, 30); o.s.vel = Vector (1, -2, 3);
	o.m = 1000; o.pmi = Vector (2, 3, 4);
	o.aC = a; o.acc = a;
	return o;
}
}

TEST_CASE ("A8 first-stage compensation per method and substep count", "[CollOrbMirror]")
{
	const Vector a (0.3, -9.81, 0.2), miss (0.0, 12.7, -1.0);
	for (int meth : { COLLM_RK2, COLLM_RK4, COLLM_RK5, COLLM_RK6, COLLM_RK7, COLLM_RK8 })
		for (int n = 1; n <= 3; n++) {
			CollOrbMirror mir;
			mir.mode[0] = meth;
			const double H = 1.0, k = H/n;
			CollOrbState ref = Body (a + miss), c = Body (a + miss);
			c.acc = a;                                       // stale cache: the miss is absent from the first stage
			c.s.pos += miss*(CollOrbMirror::DxCoef (meth)*k*k);
			c.s.vel += miss*(CollOrbMirror::Gamma0 (meth)*k);
			mir.Step (ref, H, 0, n); mir.Step (c, H, 0, n);
			double ex = (c.s.pos - ref.s.pos).length (), ev = (c.s.vel - ref.s.vel).length ();
			CAPTURE (meth, n, ex, ev);
			REQUIRE (ev <= 1e-12*miss.length ());
			REQUIRE (ex <= 1e-12*(1.0 + ref.s.pos.length ()));
		}
}

TEST_CASE ("A14 h_rest from the mirror: default 0.1 s, RK4 0.035 s, RK8 0.029 s", "[CollOrbMirror]")
{
	CollOrbMirror def;
	REQUIRE (std::fabs (def.HRest () - 0.1) <= 1e-3);
	CollOrbMirror rk4; rk4.mode[0] = COLLM_RK4;
	REQUIRE (std::fabs (rk4.HRest () - 0.035) <= 0.01*0.035);
	CollOrbMirror rk8; rk8.mode[0] = COLLM_RK8;
	REQUIRE (std::fabs (rk8.HRest () - 0.0286) <= 0.02*0.0286);
}

TEST_CASE ("Propagator choice and Orbiter.cfg keys", "[CollOrbMirror]")
{
	CollOrbMirror m;
	int lv, ns;
	m.Choose (0.05, 0, false, lv, ns); REQUIRE ((lv == 0 && ns == 1));
	m.Choose (0.25, 0, false, lv, ns); REQUIRE ((lv == 0 && ns == 3));
	m.Choose (4.0, 0, false, lv, ns); REQUIRE ((lv == 1 && ns == 2));
	m.Choose (0.05, 0, true, lv, ns); REQUIRE ((lv == 3 && ns == 10));
	std::map<std::string, std::string> cfg { { "PropStages", "2" }, { "PropStage0", "1 0.05" }, { "PropSubsampling", "4" }, { "StabiliseOrbits", "FALSE" }, { "StabiliseSLimit", "0.02" } };
	m.ReadCfg ([&] (const char *k, std::string &v) { auto it = cfg.find (k); if (it == cfg.end ()) return false; v = it->second; return true; });
	REQUIRE (m.nLevel == 2);
	REQUIRE (m.mode[0] == COLLM_RK4);
	REQUIRE (m.ttgt[0] == 0.05);
	REQUIRE (m.tlim[1] == 1e10);
	REQUIRE (m.subMax == 4);
	REQUIRE (!m.stabilise);
	REQUIRE (m.sLimit == 0.02);
	REQUIRE (m.Encke (Vector (7e6, 0, 0), Vector (0, 7.5e3, 0), 1000.0) == false);
}

TEST_CASE ("Symplectic levels step and keep a free body on its line", "[CollOrbMirror]")
{
	for (int meth : { COLLM_SY2, COLLM_SY4, COLLM_SY6, COLLM_SY8 }) {
		CollOrbMirror mir; mir.mode[0] = meth;
		CollOrbState o = Body (Vector ());
		mir.Step (o, 0.1);
		REQUIRE ((o.s.pos - (Vector (10, 20, 30) + Vector (1, -2, 3)*0.1)).length () <= 1e-12);
	}
}

TEST_CASE ("A state write clears the ground contact: the next step uses the normal level (Vessel.cpp:865, review CA-F 9)", "[CollOrbMirror]")
{
	CollOrbMirror mir;
	CollOrbState o = Body (Vector (0, -9.81, 0));
	o.ground = true;
	CollOrbState g = o;
	mir.Step (g, 0.02);
	o.DefSetStateEx (o.s.pos, o.s.vel, o.s.omega);
	REQUIRE (!o.ground);
	mir.Step (o, 0.02);
	CAPTURE (g.lv, g.nsub, o.lv, o.nsub);
	REQUIRE (o.lv == 0);
	REQUIRE (o.nsub == 1);
	REQUIRE ((g.lv != o.lv || g.nsub != o.nsub));
}
