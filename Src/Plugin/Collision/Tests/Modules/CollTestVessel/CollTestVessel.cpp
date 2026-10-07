// not upstream: test-only vessel module of the collision addon scenario tests: frame-exact actions, notice handling, probes (Design CA E4 7.4, design-C-T 4.6)
#define ORBITER_MODULE
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <xmmintrin.h>
#include "Orbitersdk.h"
#include "CollisionAPI.h"

namespace {

// ShuttlePB's orbit physics (Src/Vessel/ShuttlePB/ShuttlePB.cpp): size, mass, PMI, touchdown points, dock, thrusters; no airfoils or particle streams
const double PB_SIZE = 3.5, PB_EMPTYMASS = 500.0, PB_FUELMASS = 750.0, PB_ISP = 5e4;
const VECTOR3 PB_CS = {10.5, 15.0, 5.8}, PB_PMI = {2.28, 2.31, 0.79}, PB_RD = {0.025, 0.025, 0.02};
const double PB_MAXMAINTH = 3e4, PB_MAXHOVERTH = 1.5e4, PB_MAXRCSTH = 2e2;
TOUCHDOWNVTX tdvtx[12] = {
	{{0, -1.5, 2}, 2e4, 1e3, 1.6, 1}, {{-1, -1.5, -1.5}, 2e4, 1e3, 3.0, 1}, {{1, -1.5, -1.5}, 2e4, 1e3, 3.0, 1},
	{{-0.5, -0.75, 3}, 2e4, 1e3, 3.0}, {{0.5, -0.75, 3}, 2e4, 1e3, 3.0}, {{-2.6, -1.1, -1.9}, 2e4, 1e3, 3.0},
	{{2.6, -1.1, -1.9}, 2e4, 1e3, 3.0}, {{-1, 1.3, 0}, 2e4, 1e3, 3.0}, {{1, 1.3, 0}, 2e4, 1e3, 3.0},
	{{-1, 1.3, -2}, 2e4, 1e3, 3.0}, {{1, 1.3, -2}, 2e4, 1e3, 3.0}, {{0, 0.3, -3.8}, 2e4, 1e3, 3.0}};
TOUCHDOWNVTX tddir[3] = {{{0, -0.5, 0.5}, 1e4, 1e3, 1.0, 1}, {{-0.5, -0.5, -0.5}, 1e4, 1e3, 1.0, 1}, {{0.5, -0.5, -0.5}, 1e4, 1e3, 1.0, 1}};

const std::map<std::string, THGROUP_TYPE> GROUPS = {
	{"main", THGROUP_MAIN}, {"retro", THGROUP_RETRO}, {"hover", THGROUP_HOVER}, {"pitchup", THGROUP_ATT_PITCHUP},
	{"pitchdown", THGROUP_ATT_PITCHDOWN}, {"yawleft", THGROUP_ATT_YAWLEFT}, {"yawright", THGROUP_ATT_YAWRIGHT},
	{"bankleft", THGROUP_ATT_BANKLEFT}, {"bankright", THGROUP_ATT_BANKRIGHT}, {"right", THGROUP_ATT_RIGHT},
	{"left", THGROUP_ATT_LEFT}, {"up", THGROUP_ATT_UP}, {"down", THGROUP_ATT_DOWN}, {"forward", THGROUP_ATT_FORWARD},
	{"back", THGROUP_ATT_BACK}};
const std::map<std::string, int> PROPMODES = {
	{"PROP_ORBITAL_FIXEDSTATE", PROP_ORBITAL_FIXEDSTATE}, {"PROP_ORBITAL_FIXEDSURF", PROP_ORBITAL_FIXEDSURF},
	{"PROP_ORBITAL_ELEMENTS", PROP_ORBITAL_ELEMENTS}, {"PROP_SORBITAL_FIXEDSTATE", PROP_SORBITAL_FIXEDSTATE},
	{"PROP_SORBITAL_FIXEDSURF", PROP_SORBITAL_FIXEDSURF}, {"PROP_SORBITAL_ELEMENTS", PROP_SORBITAL_ELEMENTS},
	{"PROP_SORBITAL_DESTROY", PROP_SORBITAL_DESTROY}};
const char LETTERKEYS[] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C}; // OAPI_KEY_A..Z

typedef std::vector<std::string> Words;

double D (const Words &w, size_t i) { return i < w.size () ? std::strtod (w[i].c_str (), nullptr) : 0.0; }
int I (const Words &w, size_t i) { return i < w.size () ? (int)std::strtol (w[i].c_str (), nullptr, 0) : 0; }
VECTOR3 V3 (const Words &w, size_t i) { return _V (D (w, i), D (w, i + 1), D (w, i + 2)); }
const std::string &S (const Words &w, size_t i) { static const std::string none; return i < w.size () ? w[i] : none; }

MATRIX3 Arot (const VECTOR3 &a) // AROT in degrees, the convention of Vessel::SetGlobalOrientation (Vessel.cpp:882-895)
{
	double sx = sin (a.x * RAD), cx = cos (a.x * RAD), sy = sin (a.y * RAD), cy = cos (a.y * RAD), sz = sin (a.z * RAD), cz = cos (a.z * RAD);
	return _M (cy * cz, cy * sz, -sy, sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy, cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy);
}

MATRIX3 Columns (const VECTOR3 &x, const VECTOR3 &y, const VECTOR3 &z) { return _M (x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z); }

std::string Fmt (const VECTOR3 &v)
{
	char b[96];
	snprintf (b, sizeof b, "%.17g,%.17g,%.17g", v.x, v.y, v.z);
	return b;
}

OBJHANDLE Vh (const std::string &name) { return oapiGetVesselByName ((char *)name.c_str ()); }
VESSEL *Vi (const std::string &name) { OBJHANDLE h = Vh (name); return h ? oapiGetVesselInterface (h) : nullptr; }

void Place (VESSEL *v, OBJHANDLE gref, const VECTOR3 &gpos, const VECTOR3 &gvel, const MATRIX3 &R, const VECTOR3 &omega) // DefSetStateEx in the frame of gref, then attitude and spin
{
	VESSELSTATUS2 vs;
	memset (&vs, 0, sizeof vs);
	vs.version = 2;
	v->GetStatusEx (&vs);
	VECTOR3 rp, rv;
	oapiGetGlobalPos (gref, &rp);
	oapiGetGlobalVel (gref, &rv);
	vs.rbody = gref;
	vs.status = 0;
	vs.rpos = gpos - rp;
	vs.rvel = gvel - rv;
	vs.vrot = omega;
	vs.flag = 0;
	v->DefSetStateEx (&vs);
	v->SetRotationMatrix (R);
	v->SetAngularVel (omega);
}

class CollTestVessel : public VESSEL4 {
public:
	CollTestVessel (OBJHANDLE h, int fm) : VESSEL4 (h, fm) {}
	void clbkSetClassCaps (FILEHANDLE cfg) override;
	void clbkLoadStateEx (FILEHANDLE scn, void *status) override;
	void clbkPostCreation () override;
	void clbkPreStep (double simt, double simdt, double mjd) override;
	void clbkPostStep (double simt, double simdt, double mjd) override;
	int clbkGeneric (int msgid, int prm, void *context) override;

private:
	struct Slot { bool live; std::string name; VECTOR3 ofs; };
	void Log (const char *fmt, ...) const;
	void Act (const Words &w, double simt);
	bool Pre (const Words &w, double simt);
	bool Post (const Words &w, double simt);
	void Mesh (VESSEL *v, const Words &w, size_t op);
	void MeshCycle (VESSEL *v, int step);
	void MeshLog ();
	void Jump (double dt, const std::string &mode);
	bool Notice (int kind, void *context);

	bool director = false;
	int k = 0;                               // frames: own pre-step count, frame k's post-step uses the same k
	std::vector<Words> acts;                 // TEST* lines of this vessel's scenario block
	std::vector<Slot> slots;                 // this module's own mesh slot table (TESTMESHLOG)
	std::map<std::string, double> massKeep;  // TESTMASS: empty mass to restore
	std::map<std::string, bool> attached;    // TESTATTACHNEAR: done
	std::map<int, int> replies;              // TESTREPLY kind -> value
	std::map<int, std::string> onmsg;        // TESTONMSG kind -> action
	std::map<int, bool> onmsgDone;
	int rngEvery = 0, meshCycleSelf = 0, meshCycleStep = 0;
	bool logMsg = false, meshLog = false;
	double paceMs = 0;
	std::chrono::steady_clock::time_point paceT;
};

void CollTestVessel::Log (const char *fmt, ...) const
{
	char b[1024];
	va_list a;
	va_start (a, fmt);
	vsnprintf (b, sizeof b, fmt, a);
	va_end (a);
	oapiWriteLogV ("CollTestVessel %s", b);
}

void CollTestVessel::clbkSetClassCaps (FILEHANDLE cfg)
{
	oapiReadItem_bool (cfg, (char *)"Director", director);
	if (director) { // mesh-less, so the addon builds no collider; no thrusters
		SetSize (1.0);
		SetEmptyMass (100.0);
		SetPMI (_V (1, 1, 1));
		SetTouchdownPoints (tddir, 3);
		return;
	}
	SetSize (PB_SIZE);
	SetEmptyMass (PB_EMPTYMASS);
	SetPMI (PB_PMI);
	SetCrossSections (PB_CS);
	SetRotDrag (PB_RD);
	SetTouchdownPoints (tdvtx, 12);
	SetDockParams (_V (0, 1.3, -1), _V (0, 1, 0), _V (0, 0, -1));
	CreateAttachment (false, _V (0, 2.5, 0), _V (0, 1, 0), _V (0, 0, 1), "COLLT"); // parent point 0: a payload 3.8 m above the CG (Coll.Stack.Attached)
	CreateAttachment (true, _V (0, -1.3, 0), _V (0, -1, 0), _V (0, 0, 1), "COLLT"); // child point 0
	PROPELLANT_HANDLE hpr = CreatePropellantResource (PB_FUELMASS);
	THRUSTER_HANDLE th_main = CreateThruster (_V (0, 0, -4.35), _V (0, 0, 1), PB_MAXMAINTH, hpr, PB_ISP);
	CreateThrusterGroup (&th_main, 1, THGROUP_MAIN);
	AddExhaust (th_main, 8, 1, _V (0, 0.3, -4.35), _V (0, 0, -1));
	THRUSTER_HANDLE th_retro = CreateThruster (_V (0, 0, 3), _V (0, 0, -1), PB_MAXMAINTH, hpr, PB_ISP); // a retro group for TESTTHRUST retro
	CreateThrusterGroup (&th_retro, 1, THGROUP_RETRO);
	THRUSTER_HANDLE th_hover = CreateThruster (_V (0, -1.5, 0), _V (0, 1, 0), PB_MAXHOVERTH, hpr, PB_ISP);
	CreateThrusterGroup (&th_hover, 1, THGROUP_HOVER);
	AddExhaust (th_hover, 8, 1, _V (0, -1.5, 1), _V (0, -1, 0));
	AddExhaust (th_hover, 8, 1, _V (0, -1.5, -1), _V (0, -1, 0));
	THRUSTER_HANDLE r[14], g[4];
	const double rp[14][6] = {{1, 0, 3, 0, 1, 0}, {1, 0, 3, 0, -1, 0}, {-1, 0, 3, 0, 1, 0}, {-1, 0, 3, 0, -1, 0}, {1, 0, -3, 0, 1, 0},
		{1, 0, -3, 0, -1, 0}, {-1, 0, -3, 0, 1, 0}, {-1, 0, -3, 0, -1, 0}, {1, 0, 3, -1, 0, 0}, {-1, 0, 3, 1, 0, 0}, {1, 0, -3, -1, 0, 0},
		{-1, 0, -3, 1, 0, 0}, {0, 0, -3, 0, 0, 1}, {0, 0, 3, 0, 0, -1}};
	for (int i = 0; i < 14; i++) r[i] = CreateThruster (_V (rp[i][0], rp[i][1], rp[i][2]), _V (rp[i][3], rp[i][4], rp[i][5]), PB_MAXRCSTH, hpr, PB_ISP);
	const int grp[6][4] = {{0, 2, 5, 7}, {1, 3, 4, 6}, {0, 4, 3, 7}, {1, 5, 2, 6}, {0, 4, 2, 6}, {1, 5, 3, 7}};
	const THGROUP_TYPE gt[6] = {THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_UP, THGROUP_ATT_DOWN};
	for (int i = 0; i < 6; i++) {
		for (int j = 0; j < 4; j++) g[j] = r[grp[i][j]];
		CreateThrusterGroup (g, 4, gt[i]);
	}
	const int grp2[4][2] = {{8, 11}, {9, 10}, {8, 10}, {9, 11}};
	const THGROUP_TYPE gt2[4] = {THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_LEFT, THGROUP_ATT_RIGHT};
	for (int i = 0; i < 4; i++) {
		g[0] = r[grp2[i][0]];
		g[1] = r[grp2[i][1]];
		CreateThrusterGroup (g, 2, gt2[i]);
	}
	CreateThrusterGroup (r + 12, 1, THGROUP_ATT_FORWARD);
	CreateThrusterGroup (r + 13, 1, THGROUP_ATT_BACK);
	SetCameraOffset (_V (0, 0.8, 0));
	UINT m = AddMesh ("ShuttlePB");
	if (slots.size () <= m) slots.resize (m + 1);
	slots[m] = Slot {true, "ShuttlePB", _V (0, 0, 0)};
}

void CollTestVessel::clbkLoadStateEx (FILEHANDLE scn, void *status)
{
	char *line;
	while (oapiReadScenario_nextline (scn, line)) {
		if (!strncmp (line, "TEST", 4)) { // actions stay out of the saved state: clbkSaveState writes no TEST* line
			std::istringstream is (line);
			Words w;
			for (std::string t; is >> t;) w.push_back (t);
			const std::string &a = w[0];
			if (a == "TESTREPLY" && w.size () > 2) replies[I (w, 1)] = I (w, 2);
			else if (a == "TESTONMSG" && w.size () > 2) onmsg[I (w, 1)] = S (w, 2);
			else if (a == "TESTLOGMSG") logMsg = I (w, 1) != 0;
			else if (a == "TESTMESHLOG") meshLog = I (w, 1) != 0;
			else if (a == "TESTRNGPROBE") rngEvery = I (w, 1);
			else if (a == "TESTPACE") paceMs = D (w, 1);
			else acts.push_back (w);
		} else ParseScenarioLineEx (line, status);
	}
}

void CollTestVessel::clbkPostCreation ()
{
	unsigned csr = _mm_getcsr ();
	Log ("ready '%s' mxcsr=0x%08x ctl=0x%04x", GetName (), csr, csr & 0xffc0); // ctl: DAZ, masks, rounding, FTZ; the low bits are sticky status flags
	if (rngEvery) Log ("rng k=0 v=%.17g", oapiRand ());
}

void CollTestVessel::clbkPreStep (double simt, double, double)
{
	k++;
	if (rngEvery && k % rngEvery == 0) Log ("rng k=%d v=%.17g", k, oapiRand ());
	if (meshCycleSelf && k % meshCycleSelf == 0) MeshCycle (this, meshCycleStep++);
	for (const Words &w : acts)
		if (Pre (w, simt)) Act (w, simt);
	if (paceMs > 0) { // TESTPACE, after the actions (TESTSLEEP included): the next SysDT comes from the steady clock (paced runs only)
		if (k > 1) std::this_thread::sleep_until (paceT + std::chrono::microseconds ((long long)(paceMs * 1000)));
		paceT = std::chrono::steady_clock::now ();
	}
}

void CollTestVessel::clbkPostStep (double simt, double, double)
{
	for (const Words &w : acts)
		if (Post (w, simt)) Act (w, simt);
	if (meshLog) MeshLog ();
}

void CollTestVessel::Act (const Words &w, double simt)
{
	std::string l;
	for (const std::string &t : w) l += (l.empty () ? "" : " ") + t;
	Log ("act k=%d simt=%.17g %s", k, simt, l.c_str ());
}

bool CollTestVessel::Pre (const Words &w, double simt) // the actions of a vessel pre-step (Y15): buffers are not split here
{
	const std::string &a = w[0];
	if (a == "TESTATTACHNEAR") { // every pre-step: attach once the two points are within dist
		VESSEL *p = Vi (S (w, 1)), *c = Vi (S (w, 3));
		if (!p || !c || attached[S (w, 3)]) return false;
		ATTACHMENTHANDLE hp = p->GetAttachmentHandle (false, I (w, 2)), hc = c->GetAttachmentHandle (true, I (w, 4));
		if (!hp || !hc) return false;
		VECTOR3 pp, pd, pr, cp, cd, cr, gp, gc;
		p->GetAttachmentParams (hp, pp, pd, pr);
		c->GetAttachmentParams (hc, cp, cd, cr);
		p->Local2Global (pp, gp);
		c->Local2Global (cp, gc);
		if (length (gp - gc) > D (w, 5)) return false;
		attached[S (w, 3)] = p->AttachChild (c->GetHandle (), hp, hc);
		return true;
	}
	if (a == "TESTTHRUSTRAMP") { // k0..k1, level linear in the frame index
		int k0 = I (w, 1), k1 = I (w, 2);
		VESSEL *v = Vi (S (w, 3));
		auto g = GROUPS.find (S (w, 4));
		if (k < k0 || k > k1 || !v || g == GROUPS.end ()) return false;
		double l0 = D (w, 5), l1 = D (w, 6);
		v->SetThrusterGroupLevel (g->second, k1 > k0 ? l0 + (l1 - l0) * (k - k0) / (double)(k1 - k0) : l1);
		return k == k0 || k == k1;
	}
	if (a == "TESTMASS") { // k0: empty mass set, k1: restored
		VESSEL *v = Vi (S (w, 3));
		if (!v) return false;
		if (k == I (w, 1)) {
			massKeep[S (w, 3)] = v->GetEmptyMass ();
			v->SetEmptyMass (D (w, 4));
			return true;
		}
		if (k == I (w, 2) && massKeep.count (S (w, 3))) {
			v->SetEmptyMass (massKeep[S (w, 3)]);
			return true;
		}
		return false;
	}
	if (a == "TESTRECORD") { // TESTRECORD k0 k1: Ctrl-C (ToggleRecPlay, Keymap.cpp:214) starts the recorder at k0 and stops it at k1 (E4 7.5)
		if (k != I (w, 1) && k != I (w, 2)) return false;
		DWORD mod[1] = {OAPI_KEY_LCONTROL};
		oapiSimulateBufferedKey (OAPI_KEY_C, mod, 1);
		return true;
	}
	if (a == "TESTDELETEPOST" || a == "TESTJUMPPOST" || I (w, 1) != k) return false;
	if (a == "TESTPAUSE") { // nothing in-process runs while paused: the runner resumes through Orbiter's stdin, "pause off" (T 4.6)
		oapiSetPause (true);
		return true;
	}
	VESSEL *v = Vi (S (w, 2));
	if (a == "TESTPLACEREL") { // TESTPLACEREL k v ref x y z vx vy vz ax ay az: v at ref's state, offsets in ref's frame
		VESSEL *ref = Vi (S (w, 3));
		if (!v || !ref) return false;
		MATRIX3 R;
		VECTOR3 p, vel, w0;
		ref->GetRotationMatrix (R);
		ref->GetGlobalPos (p);
		ref->GetGlobalVel (vel);
		ref->GetAngularVel (w0);
		Place (v, ref->GetGravityRef (), p + mul (R, V3 (w, 4)), vel + mul (R, V3 (w, 7)), mul (R, Arot (V3 (w, 10))), w0);
	} else if (a == "TESTPLACEDOCK") { // TESTPLACEDOCK k v port t tport d0 vclose: v's port on t's port axis at d0, closing at vclose
		VESSEL *t = Vi (S (w, 4));
		if (!v || !t) return false;
		DOCKHANDLE dv = v->GetDockHandle (I (w, 3)), dt = t->GetDockHandle (I (w, 5));
		if (!dv || !dt) return false;
		VECTOR3 vp, vd, vr, tp, td, tr, p, vel, w0;
		MATRIX3 Rt;
		v->GetDockParams (dv, vp, vd, vr);
		t->GetDockParams (dt, tp, td, tr);
		t->GetRotationMatrix (Rt);
		t->GetGlobalPos (p);
		t->GetGlobalVel (vel);
		t->GetAngularVel (w0);
		VECTOR3 gd = mul (Rt, td), gr = mul (Rt, tr), ad = -gd;
		MATRIX3 Rv = mul (Columns (ad, gr, crossp (ad, gr)), transp (Columns (vd, vr, crossp (vd, vr)))); // v's dir against t's, rot along t's
		VECTOR3 port = p + mul (Rt, tp) + gd * D (w, 6);
		Place (v, t->GetGravityRef (), port - mul (Rv, vp), vel + ad * D (w, 7), Rv, _V (0, 0, 0));
	} else if (a == "TESTPLACEBASE") { // TESTPLACEBASE k v base x y z vx vy vz heading: base-local metres, +z east, +x south, +y up (Base.cpp:556-560)
		OBJHANDLE hb = nullptr, hp = nullptr;
		std::string bn = S (w, 3);
		for (char &c : bn) if (c == '_') c = ' '; // base names with blanks are written with '_'
		for (DWORD i = 0; i < oapiGetGbodyCount () && !hb; i++) {
			OBJHANDLE g = oapiGetGbodyByIndex (i);
			if (oapiGetObjectType (g) == OBJTP_PLANET && (hb = oapiGetBaseByName (g, (char *)bn.c_str ()))) hp = g;
		}
		if (!v || !hb) return false;
		double blng, blat, brad, size = oapiGetSize (hp);
		oapiGetBaseEquPos (hb, &blng, &blat, &brad);
		VECTOR3 ofs = V3 (w, 4), lv = V3 (w, 7);
		double lng = blng + ofs.z / (size * cos (blat)), lat = blat - ofs.x / size, rad = brad + ofs.y;
		VECTOR3 gp, pp, pv;
		MATRIX3 Rp;
		oapiEquToGlobal (hp, lng, lat, rad, &gp);
		oapiGetGlobalPos (hp, &pp);
		oapiGetGlobalVel (hp, &pv);
		oapiGetRotationMatrix (hp, &Rp);
		VECTOR3 up = mul (Rp, _V (cos (lat) * cos (lng), sin (lat), cos (lat) * sin (lng))), east = mul (Rp, _V (-sin (lng), 0, cos (lng)));
		VECTOR3 north = mul (Rp, _V (-sin (lat) * cos (lng), cos (lat), -sin (lat) * sin (lng)));
		double hd = D (w, 10) * RAD, T = oapiGetPlanetPeriod (hp);
		VECTOR3 nose = north * cos (hd) + east * sin (hd), right = east * cos (hd) - north * sin (hd);
		VECTOR3 vel = pv + east * (T != 0 ? PI2 * rad * cos (lat) / T : 0) - north * lv.x + up * lv.y + east * lv.z; // ground speed plus base-local velocity
		Place (v, hp, gp, vel, Columns (right, up, nose), _V (0, 0, 0));
		Log ("placed '%s' at '%s' lng=%.17g lat=%.17g rad=%.17g", v->GetName (), bn.c_str (), lng, lat, rad);
	} else if (a == "TESTATT") {
		if (!v) return false;
		v->SetRotationMatrix (Arot (V3 (w, 3)));
	} else if (a == "TESTSPIN") {
		if (!v) return false;
		v->SetAngularVel (V3 (w, 3));
	} else if (a == "TESTDETACH") { // TESTDETACH k parent idx vel
		if (!v) return false;
		ATTACHMENTHANDLE h = v->GetAttachmentHandle (false, I (w, 3));
		if (!h || !v->DetachChild (h, D (w, 4))) return false;
	} else if (a == "TESTUNDOCK") {
		if (!v) return false;
		v->Undock (I (w, 3));
	} else if (a == "TESTDOCK") { // TESTDOCK k v port t tport mode
		OBJHANDLE t = Vh (S (w, 4));
		if (!v || !t) return false;
		v->Dock (t, I (w, 3), I (w, 5), I (w, 6));
	} else if (a == "TESTMOVEDOCK") { // TESTMOVEDOCK k v port pos dir rot
		DOCKHANDLE d = v ? v->GetDockHandle (I (w, 3)) : nullptr;
		if (!d) return false;
		v->SetDockParams (d, V3 (w, 4), V3 (w, 7), V3 (w, 10));
	} else if (a == "TESTSHIFTCG") {
		if (!v) return false;
		v->ShiftCG (V3 (w, 3));
	} else if (a == "TESTSHIFTCOM") {
		if (!v) return false;
		v->ShiftCentreOfMass (V3 (w, 3));
	} else if (a == "TESTJUMP") { // TESTJUMP k dt pmode
		Jump (D (w, 2), S (w, 3));
	} else if (a == "TESTWARP") {
		oapiSetTimeAcceleration (D (w, 2));
	} else if (a == "TESTTHRUST") { // TESTTHRUST k v group level: holds until changed
		auto g = GROUPS.find (S (w, 3));
		if (!v || g == GROUPS.end ()) return false;
		v->SetThrusterGroupLevel (g->second, D (w, 4));
	} else if (a == "TESTKEY") { // TESTKEY k v key: a letter, or a key code
		if (!v) return false;
		const std::string &kn = S (w, 3);
		DWORD key = kn.size () == 1 && kn[0] >= 'A' && kn[0] <= 'Z' ? (DWORD)LETTERKEYS[kn[0] - 'A'] : (DWORD)strtol (kn.c_str (), nullptr, 0);
		v->SendBufferedKey (key);
	} else if (a == "TESTDV") { // TESTDV k v dvx dvy dvz: dv in the vessel frame, a relative module write (seen as JUMP)
		if (!v) return false;
		VESSELSTATUS2 vs;
		memset (&vs, 0, sizeof vs);
		vs.version = 2;
		v->GetStatusEx (&vs);
		MATRIX3 R;
		v->GetRotationMatrix (R);
		vs.rvel += mul (R, V3 (w, 3));
		vs.flag = 0;
		v->DefSetStateEx (&vs);
	} else if (a == "TESTSLEEP") { // TESTSLEEP k ms: the next frame's SysDT grows by about ms
		std::this_thread::sleep_for (std::chrono::microseconds ((long long)(D (w, 2) * 1000)));
	} else if (a == "TESTMESH") { // TESTMESH k v op ...
		if (!v) return false;
		if (S (w, 3) == "cycle") {
			if (v == this) meshCycleSelf = I (w, 4);
			else return false;
		} else Mesh (v, w, 3);
	} else if (a == "TESTCREATE") { // TESTCREATE k name class ref x y z vx vy vz: at ref's state, offsets in ref's frame
		VESSEL *ref = Vi (S (w, 4));
		if (!ref) return false;
		VESSELSTATUS2 vs;
		memset (&vs, 0, sizeof vs);
		vs.version = 2;
		ref->GetStatusEx (&vs);
		MATRIX3 R;
		ref->GetRotationMatrix (R);
		vs.status = 0;
		vs.rpos += mul (R, V3 (w, 5));
		vs.rvel += mul (R, V3 (w, 8));
		vs.flag = 0;
		vs.fuel = 0;
		vs.nfuel = vs.nthruster = vs.ndockinfo = 0;
		if (!oapiCreateVesselEx (S (w, 2).c_str (), S (w, 3).c_str (), &vs)) return false;
	} else if (a == "TESTREPAIRAPI") { // collaRepairVessel of the loaded addon (E3 6.1), looked up now
		COLLA_PFN_REPAIRVESSEL f = (COLLA_PFN_REPAIRVESSEL)collaFind ("collaRepairVessel");
		if (!f) {
			Log ("collaRepairVessel missing"); // until E3 adds the export (E4 7.4); a test that needs it fails on this line
			return true;
		}
		if (!v) return false;
		Log ("collaRepairVessel '%s' = %d", v->GetName (), f (v->GetHandle ()));
	} else return false;
	return true;
}

bool CollTestVessel::Post (const Words &w, double) // deliberate post-step actions: the test is about that phase (T 4.6)
{
	const std::string &a = w[0];
	if (I (w, 1) != k) return false;
	if (a == "TESTDELETEPOST") {
		OBJHANDLE h = Vh (S (w, 2));
		return h && oapiDeleteVessel (h);
	}
	if (a == "TESTJUMPPOST") {
		Jump (D (w, 2), S (w, 3));
		return true;
	}
	return false;
}

void CollTestVessel::Jump (double dt, const std::string &mode)
{
	auto m = PROPMODES.find (mode);
	int pm = m != PROPMODES.end () ? m->second : (int)strtol (mode.c_str (), nullptr, 0);
	oapiSetSimMJD (oapiGetSimMJD () + dt / 86400.0, pm);
}

void CollTestVessel::Mesh (VESSEL *v, const Words &w, size_t op) // add <name>, del <i>, clear, reinsert <i>, shift <i> <dx dy dz>
{
	const std::string &o = S (w, op);
	bool own = v == this;
	if (o == "add") {
		UINT i = v->AddMesh (S (w, op + 1).c_str ());
		if (own) {
			if (slots.size () <= i) slots.resize (i + 1);
			slots[i] = Slot {true, S (w, op + 1), _V (0, 0, 0)};
		}
	} else if (o == "del") {
		UINT i = I (w, op + 1);
		if (v->DelMesh (i) && own && i < slots.size ()) slots[i].live = false;
	} else if (o == "clear") {
		v->ClearMeshes (false);
		if (own) slots.clear ();
	} else if (o == "reinsert") {
		UINT i = I (w, op + 1);
		const char *n = v->GetMeshName (i);
		if (!n) return;
		std::string name = n;
		v->InsertMesh (name.c_str (), i);
		if (own && i < slots.size ()) slots[i] = Slot {true, name, _V (0, 0, 0)};
	} else if (o == "shift") {
		UINT i = I (w, op + 1);
		VECTOR3 d = V3 (w, op + 2);
		if (v->ShiftMesh (i, d) && own && i < slots.size ()) slots[i].ofs += d;
	}
}

void CollTestVessel::MeshCycle (VESSEL *v, int step) // all five operations with hole reuse, back to the start state every 7 steps
{
	static const char *const script[7] = {"add ShuttlePB", "shift 1 0 0 0.25", "del 0", "add ShuttlePB", "reinsert 1", "clear", "add ShuttlePB"};
	std::istringstream is (script[step % 7]);
	Words w;
	for (std::string t; is >> t;) w.push_back (t);
	Mesh (v, w, 0);
	Log ("act k=%d simt=%.17g TESTMESH cycle %s", k, oapiGetSimTime (), script[step % 7]);
}

void CollTestVessel::MeshLog ()
{
	std::string s;
	for (size_t i = 0; i < slots.size (); i++)
		s += " " + std::to_string (i) + ":" + (slots[i].live ? slots[i].name + ":" + Fmt (slots[i].ofs) : std::string ("-"));
	Log ("slots k=%d%s", k, s.c_str ());
}

bool CollTestVessel::Notice (int kind, void *context) // TESTONMSG: once per kind, inside the addon's pre-step (re-entrant)
{
	auto o = onmsg.find (kind);
	if (o == onmsg.end () || onmsgDone[kind]) return false;
	onmsgDone[kind] = true;
	const std::string &a = o->second;
	if (a == "delete") oapiDeleteVessel (GetHandle ());
	else if (a == "undock") Undock (ALLDOCKS);
	else if (a == "repair") {
		COLLA_PFN_REPAIRVESSEL f = (COLLA_PFN_REPAIRVESSEL)collaFind ("collaRepairVessel");
		if (f) f (GetHandle ());
		else Log ("collaRepairVessel missing");
	} else if (a == "create") {
		VESSELSTATUS2 vs;
		memset (&vs, 0, sizeof vs);
		vs.version = 2;
		GetStatusEx (&vs);
		vs.rpos.x += 100.0;
		vs.flag = 0;
		vs.fuel = 0;
		vs.nfuel = vs.nthruster = vs.ndockinfo = 0;
		std::string n = std::string (GetName ()) + "-new";
		oapiCreateVesselEx (n.c_str (), "CollTestVessel", &vs);
	} else if (a == "save") oapiSaveScenario ("Tests/Coll/Saved/onmsg", "collision test save in a notice");
	(void)context;
	Log ("act k=%d simt=%.17g TESTONMSG %d %s '%s'", k, oapiGetSimTime (), kind, a.c_str (), GetName ());
	return true;
}

int CollTestVessel::clbkGeneric (int msgid, int prm, void *context)
{
	if (msgid != COLLA_VMSG) return 0;
	if (logMsg) {
		const COLLA_HDR *h = (const COLLA_HDR *)context;
		char b[768] = "";
		if (h && h->kind == COLLA_KIND_CONTACT && h->size >= sizeof (COLLA_CONTACTINFO)) {
			const COLLA_CONTACTINFO *c = (const COLLA_CONTACTINFO *)context;
			char on[64] = "-";
			if (c->hOther) oapiGetObjectName (c->hOther, on, sizeof on);
			snprintf (b, sizeof b, " flags=0x%x other=%s obj=%d simt=%.17g pos=%s nml=%s vn=%.17g vt=%.17g J=%.17g dE=%.17g", c->flags, on, c->otherObj,
				c->simt, Fmt (c->pos).c_str (), Fmt (c->nml).c_str (), c->vn, c->vt, c->J, c->dE);
		} else if (h && h->size >= sizeof (COLLA_DAMAGEINFO)) {
			const COLLA_DAMAGEINFO *d = (const COLLA_DAMAGEINFO *)context;
			snprintf (b, sizeof b, " flags=0x%x ndent=%u simt=%.17g energy=%.17g total=%.17g depth=%.17g destroy=%.17g", d->flags, d->ndent, d->simt,
				d->energy, d->energy_total, d->depth, d->destroyEnergy);
		}
		Log ("'%s' msg=0x%08x kind=%d t=%.17g%s", GetName (), (unsigned)msgid, prm, oapiGetSimTime (), b);
	}
	Notice (prm, context);
	auto r = replies.find (prm);
	return r != replies.end () ? r->second : 0;
}

}

DLLCLBK VESSEL *ovcInit (OBJHANDLE h, int fm) { return new CollTestVessel (h, fm); }

DLLCLBK void ovcExit (VESSEL *v) { delete (CollTestVessel *)v; }
