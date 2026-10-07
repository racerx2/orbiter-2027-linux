// not upstream: collision addon E1 6.1, production mirror of Orbiter's rigid-body step for one body; Orbiter-free
#ifndef __COLLORBMIRROR_H
#define __COLLORBMIRROR_H
#include <functional>
#include <string>
#include "Vecmat.h"

enum { COLLM_RK2, COLLM_RK4, COLLM_RK5, COLLM_RK6, COLLM_RK7, COLLM_RK8, COLLM_SY2, COLLM_SY4, COLLM_SY6, COLLM_SY8, COLLM_N }; // PROP_* values (Config.h:27-36)
constexpr int COLLM_LEVELS = 5;               // MAX_PROP_LEVEL

// one body as Orbiter holds it in the pre-step (s0 == s1, Body.cpp:169-173)
struct CollOrbState {
	StateVectors s;                           // pos, vel, Q, R, omega (body frame)
	Vector acc, arot;                         // cached end-of-step accelerations: global, body (Rigidbody.cpp:259-260)
	double m = 1.0; Vector pmi = Vector (1, 1, 1); // mass, mass-normalised PMI
	Vector Fadd, Madd;                        // Flin_add, Amom_add, body frame (Vessel.h:1311-1315)
	Vector aC;                                // constant global acceleration in every stage: gravity and the persistent unseen part (3.1)
	Vector tauU;                              // persistent unseen torque per mass, body frame
	Vector gReset;                            // gravity at the written position: acc after DefSetStateEx (Vessel.cpp:869-871)
	bool ground = false, stack = false;       // GroundContact (last level, PropSubMax); stack: RPlace keeps acc (E1 6.5)
	int lv = 0, nsub = 1, method = COLLM_RK2; // of the last Step
	void DefSetStateEx (const Vector &p, const Vector &v, const Vector &w) { s.pos = p; s.vel = v; s.omega = w; if (!stack) acc = gReset; }
	void SetRotationMatrix (const Matrix &R) { s.R = R; s.Q.Set (R); }
	void SetAngularVel (const Vector &w) { s.omega = w; }
	void AddForce (const Vector &F, const Vector &r) { Fadd += F; Madd += crossp (F, r); }
	Vector SpinL () const;                    // global spin momentum m R (pmi * omega)
	Vector EulerInv (const Vector &tau, const Vector &w) const; // Rigidbody.cpp:468-481
};

class CollOrbMirror {
public:
	int nLevel = 4;                           // PropStages
	int mode[COLLM_LEVELS] = { COLLM_RK2, COLLM_RK4, COLLM_RK6, COLLM_RK8, COLLM_RK8 };
	double ttgt[COLLM_LEVELS] = { 0.1, 2.0, 20.0, 200.0, 500.0 };
	double atgt[COLLM_LEVELS], tlim[COLLM_LEVELS] = { 0.5, 10.0, 100.0, 1e10, 1e10 }, alim[COLLM_LEVELS];
	int subMax = 10;                          // PropSubsampling
	bool stabilise = true;                    // StabiliseOrbits
	double sLimit = 0.01, pLimit = 0.05;      // StabiliseSLimit, StabilisePLimit
	CollOrbMirror ();                         // Config.cpp:55-62 defaults
	void ReadCfg (const std::function<bool (const char *key, std::string &val)> &str); // Config.cpp:596-619 semantics on Orbiter.cfg strings
	void Choose (double H, double wlen, bool ground, int &lv, int &ns) const; // Rigidbody.cpp:165-178, Vesselbase.cpp:387-397
	void Step (CollOrbState &o, double H, int forceLv = -1, int forceN = 0) const; // one Orbiter step; forced level for a re-run (2.5)
	bool Encke (const Vector &r, const Vector &v, double H) const; // Rigidbody.cpp:197, :214-217 with the share taken as met
	double HRest (double g = 9.81, double tol = 0.004) const;     // 7.4: largest step whose 1 g first-stage miss moves a body by at most tol
	std::string Describe () const;            // `Collision prop:` line content
	static double Gamma0 (int method);        // velocity share of a first-stage miss (6.2)
	static double DxCoef (int method);        // position share of a first-stage miss once dv = gamma0 k a is written
};
#endif
