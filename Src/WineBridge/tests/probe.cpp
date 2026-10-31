// WineBridge tests only: a plugin that writes vessels' properties or one vessel's flight to WbProbe.log
//   WB_PROBE=props          every vessel's properties after its first step
//   WB_PROBE=flight         the focus vessel at 10 km, 200 m/s east, main engine full and elevator 0.3; its state each second
//   WB_PROBE_KILL=<frame>   at that frame, the helper is killed (failure test)
//   WB_PROBE_SAVE=<name>    props: the scenario saved at frame 3 as Scenarios/<name>.scn
//   WB_PROBE=trace          every vessel's properties at frame 1; the focus vessel's state each frame, every vessel's each
//                           25 frames; beacons, exhausts, lights, docks and attachments of every vessel whenever they change
//   WB_PROBE_KEYS=f:k,...   trace: the buffered key k (0x26; +0x100 shift, +0x200 ctrl, +0x400 alt) sent to the focus vessel
//                           at frame f, then what it changed
//   WB_PROBE_WARP=f:w,...   trace: time acceleration w from frame f
//   WB_PROBE_FOCUS=f:name,... trace: the focus to that vessel at frame f
//   WB_PROBE_BODIES=name,... trace, flight: those bodies' global position and velocity each frame (flight: each second), a
//                           planet's atmosphere constants at the first, and the focus vessel's atmosphere (density, pressure,
//                           temperature)
//   WB_PROBE_CAMERA=1       trace: the camera whenever it changes (to 6 decimals): mode, internal, target, distance, azimuth, polar,
//                           aperture, and its direction in the focus vessel's frame
#define ORBITER_MODULE
#include "Orbitersdk.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static FILE *g_out;
static std::string g_mode;
static const char *g_save;
static long g_frame, g_kill = -1;
static double g_t0 = -1, g_next;
static std::vector<std::pair<long, double>> g_keys, g_warps, g_mfdkeys, g_mfdbtns; // MFD: mfd * 0x100 + key or button
static std::vector<std::pair<long, std::string>> g_focus;
static std::vector<std::string> g_bodies;
static bool g_camera;
static std::map<std::string, std::string> g_last;

static void V (const char *tag, const char *k, const VECTOR3 &v)
{
	fprintf (g_out, "%s %s %.17g %.17g %.17g\n", tag, k, v.x, v.y, v.z);
}

static void Props (VESSEL *v)
{
	const char *t = v->GetName ();
	VECTOR3 a, b, c;
	fprintf (g_out, "%s size %.17g\n%s emptymass %.17g\n", t, v->GetSize (), t, v->GetEmptyMass ());
	v->GetPMI (a); V (t, "pmi", a);
	v->GetCrossSections (a); V (t, "cs", a);
	v->GetRotDrag (a); V (t, "rotdrag", a);
	v->GetCameraOffset (a); V (t, "camera", a);
	fprintf (g_out, "%s meshes %u\n", t, v->GetMeshCount ());
	fprintf (g_out, "%s docks %u\n", t, v->DockCount ());
	for (UINT i = 0; i < v->DockCount (); i++) { v->GetDockParams (v->GetDockHandle (i), a, b, c); V (t, "dock.pos", a); V (t, "dock.dir", b); V (t, "dock.rot", c); }
	DWORD np = v->GetPropellantCount ();
	fprintf (g_out, "%s propellants %u\n", t, np);
	for (DWORD i = 0; i < np; i++) {
		PROPELLANT_HANDLE p = v->GetPropellantHandleByIndex (i);
		fprintf (g_out, "%s prop%u %.17g %.17g %.17g\n", t, i, v->GetPropellantMaxMass (p), v->GetPropellantMass (p), v->GetPropellantEfficiency (p));
	}
	DWORD nt = v->GetThrusterCount ();
	fprintf (g_out, "%s thrusters %u\n", t, nt);
	for (DWORD i = 0; i < nt; i++) {
		THRUSTER_HANDLE th = v->GetThrusterHandleByIndex (i);
		v->GetThrusterRef (th, a); v->GetThrusterDir (th, b);
		int res = -1;
		for (DWORD k = 0; k < np; k++) if (v->GetPropellantHandleByIndex (k) == v->GetThrusterResource (th)) res = (int)k;
		fprintf (g_out, "%s th%u %.17g %.17g %.17g | %.17g %.17g %.17g | %.17g %.17g prop%d\n", t, i, a.x, a.y, a.z, b.x, b.y, b.z,
			v->GetThrusterMax0 (th), v->GetThrusterIsp0 (th), res);
	}
	static const THGROUP_TYPE groups[] = {THGROUP_MAIN, THGROUP_RETRO, THGROUP_HOVER, THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN,
		THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_RIGHT, THGROUP_ATT_LEFT,
		THGROUP_ATT_UP, THGROUP_ATT_DOWN, THGROUP_ATT_FORWARD, THGROUP_ATT_BACK};
	for (THGROUP_TYPE g : groups) {
		fprintf (g_out, "%s group%d", t, (int)g);
		for (DWORD i = 0; i < v->GetGroupThrusterCount (g); i++) {
			THRUSTER_HANDLE th = v->GetGroupThruster (g, i);
			for (DWORD k = 0; k < nt; k++) if (v->GetThrusterHandleByIndex (k) == th) fprintf (g_out, " th%u", k);
		}
		fprintf (g_out, "\n");
	}
	DWORD ne = v->GetExhaustCount ();
	fprintf (g_out, "%s exhausts %u\n", t, ne);
	for (DWORD i = 0; i < ne; i++) {
		double ls, ws;
		SURFHANDLE tex; // the core writes every pointer it is given
		v->GetExhaustSpec (i, &ls, &ws, &a, &b, &tex);
		fprintf (g_out, "%s ex%u %.17g %.17g | %.17g %.17g %.17g | %.17g %.17g %.17g\n", t, i, ls, ws, a.x, a.y, a.z, b.x, b.y, b.z);
	}
	ANIMATION *an;
	UINT na = v->GetAnimPtr (&an);
	fprintf (g_out, "%s animations %u\n", t, na);
	for (UINT i = 0; i < na; i++) {
		fprintf (g_out, "%s anim%u %.17g %u\n", t, i, an[i].defstate, an[i].ncomp);
		for (UINT k = 0; k < an[i].ncomp; k++) {
			ANIMATIONCOMP *cp = an[i].comp[k];
			MGROUP_TRANSFORM *tr = cp->trans;
			fprintf (g_out, "%s anim%u.%u %.17g %.17g type%d mesh%u grp", t, i, k, cp->state0, cp->state1, tr ? (int)tr->Type () : -1, tr ? tr->mesh : 0);
			if (tr) fprintf (g_out, " %u%s", tr->ngrp, tr->grp ? ":" : " null");
			for (UINT j = 0; tr && tr->grp && j < tr->ngrp; j++) fprintf (g_out, " %u", tr->grp[j]);
			if (tr && tr->Type () == MGROUP_TRANSFORM::ROTATE) {
				MGROUP_ROTATE *r = (MGROUP_ROTATE *)tr;
				fprintf (g_out, " ref %.17g %.17g %.17g axis %.17g %.17g %.17g angle %.9g", r->ref.x, r->ref.y, r->ref.z, r->axis.x, r->axis.y, r->axis.z, r->angle);
			}
			fprintf (g_out, " parent%d\n", cp->parent ? 1 : 0);
		}
	}
	fprintf (g_out, "%s touchdown %u\n", t, v->GetTouchdownPointCount ());
	for (DWORD i = 0; i < v->GetTouchdownPointCount (); i++) {
		TOUCHDOWNVTX td;
		v->GetTouchdownPoint (td, i);
		fprintf (g_out, "%s td%u %.17g %.17g %.17g %.17g %.17g %.17g\n", t, i, td.pos.x, td.pos.y, td.pos.z, td.stiffness, td.damping, td.mu);
	}
}

static void Pairs (const char *s, std::vector<std::pair<long, double>> &out)
{
	while (s && *s) {
		char *e;
		long f = strtol (s, &e, 10);
		if (*e != ':') return;
		double v = strtod (e + 1, &e);
		out.push_back ({f, v});
		s = *e == ',' ? e + 1 : nullptr;
	}
}

static void Names (const char *s, std::vector<std::pair<long, std::string>> &out)
{
	while (s && *s) {
		char *e;
		long f = strtol (s, &e, 10);
		if (*e != ':') return;
		const char *n = e + 1, *c = strchr (n, ',');
		out.push_back ({f, c ? std::string (n, c - n) : std::string (n)});
		s = c ? c + 1 : nullptr;
	}
}

static std::string F (const char *fmt, ...)
{
	char b[1024];
	va_list a;
	va_start (a, fmt);
	vsnprintf (b, sizeof b, fmt, a);
	va_end (a);
	return b;
}

static std::string Vs (const VECTOR3 &v) { return F ("%.17g %.17g %.17g", v.x, v.y, v.z); }

// one line of a vessel's item, written when it changed (or always)
static void Put (const char *t, const std::string &k, const std::string &line, bool always)
{
	std::string &l = g_last[std::string (t) + ' ' + k];
	if (!always && l == line) return;
	l = line;
	fprintf (g_out, "f%ld %s %s %s\n", g_frame, t, k.c_str (), line.c_str ());
}

static std::string Name (OBJHANDLE h)
{
	char b[256] = "-";
	if (h) oapiGetObjectName (h, b, sizeof b);
	return b;
}

// what the modules keep in their own memory for Orbiter to read, and what they link
static void Live (VESSEL *v)
{
	const char *t = v->GetName ();
	for (DWORD i = 0; const BEACONLIGHTSPEC *s = v->GetBeacon (i); i++)
		Put (t, F ("bc%u", i), F ("%d %d %.17g %.17g %.17g %.17g %.17g | ", s->active ? 1 : 0, (int)s->shape, s->size, s->falloff,
			s->period, s->duration, s->tofs) + Vs (*s->pos) + " | " + Vs (*s->col), false);
	for (DWORD i = 0; i < v->GetExhaustCount (); i++) {
		double ls = 0, ws = 0;
		VECTOR3 p = {}, d = {};
		SURFHANDLE tex;
		v->GetExhaustSpec (i, &ls, &ws, &p, &d, &tex);
		Put (t, F ("ex%u", i), F ("%.17g %.17g %.17g | ", v->GetExhaustLevel (i), ls, ws) + Vs (p) + " | " + Vs (d), false);
	}
	for (DWORD i = 0; i < v->LightEmitterCount (); i++) {
		const LightEmitter *l = v->GetLightEmitter (i);
		const COLOUR4 &c = l->GetDiffuseColour ();
		std::string pt;
		if (l->GetType () == LightEmitter::LT_POINT || l->GetType () == LightEmitter::LT_SPOT) {
			const PointLight *p = static_cast<const PointLight *> (l);
			const double *a = p->GetAttenuation ();
			pt = F (" | %.17g %.17g %.17g %.17g", p->GetRange (), a[0], a[1], a[2]);
		}
		if (l->GetType () == LightEmitter::LT_SPOT)
			pt += F (" %.17g %.17g", static_cast<const SpotLight *> (l)->GetUmbra (), static_cast<const SpotLight *> (l)->GetPenumbra ());
		Put (t, F ("le%u", i), F ("%d %d %d %s %.17g | ", l->IsActive () ? 1 : 0, (int)l->GetType (), (int)l->GetVisibility (),
			Name (l->GetObjectHandle ()).c_str (), l->GetIntensity ()) + Vs (l->GetPosition ()) + " | " + Vs (l->GetDirection ()) +
			F (" | %g %g %g %g", c.r, c.g, c.b, c.a) + pt, false);
	}
	std::string dk, at;
	for (UINT i = 0; i < v->DockCount (); i++) dk += ' ' + Name (v->GetDockStatus (v->GetDockHandle (i)));
	Put (t, "dk", dk, false);
	for (int toparent = 0; toparent < 2; toparent++)
		for (DWORD i = 0; i < v->AttachmentCount (toparent); i++)
			at += ' ' + Name (v->GetAttachmentStatus (v->GetAttachmentHandle (toparent, i)));
	Put (t, "at", at, false);
}

static void State (VESSEL *v)
{
	const char *t = v->GetName ();
	OBJHANDLE g = v->GetGravityRef ();
	VECTOR3 r, u, w;
	MATRIX3 R;
	v->GetRelativePos (g, r);
	v->GetRelativeVel (g, u);
	v->GetAngularVel (w);
	v->GetRotationMatrix (R);
	Put (t, "st", Name (g) + F (" %u %.17g | ", v->GetFlightStatus (), v->GetMass ()) + Vs (r) + " | " + Vs (u) + " | " + Vs (w) +
		" | " + Vs (_V (R.m11, R.m12, R.m13)) + ' ' + Vs (_V (R.m21, R.m22, R.m23)) + ' ' + Vs (_V (R.m31, R.m32, R.m33)), true);
	std::string s;
	for (DWORD i = 0; i < v->GetThrusterCount (); i++) s += F (" %.17g", v->GetThrusterLevel (v->GetThrusterHandleByIndex (i)));
	Put (t, "th", s, true);
	s.clear ();
	for (DWORD i = 0; i < v->GetPropellantCount (); i++) s += F (" %.17g", v->GetPropellantMass (v->GetPropellantHandleByIndex (i)));
	Put (t, "pr", s, true);
	s.clear ();
	ANIMATION *an;
	UINT na = v->GetAnimPtr (&an);
	for (UINT i = 0; i < na; i++) s += F (" %.17g", v->GetAnimation (i));
	Put (t, "an", s, true);
	s.clear ();
	for (UINT i = 0; i < na; i++) // the rotations' parameters, as a module may change them
		for (UINT k = 0; k < an[i].ncomp; k++) {
			MGROUP_TRANSFORM *tr = an[i].comp[k]->trans;
			if (!tr || tr->Type () != MGROUP_TRANSFORM::ROTATE) continue;
			MGROUP_ROTATE *r = (MGROUP_ROTATE *)tr;
			s += F (" %.9g %.17g %.17g %.17g %.17g %.17g %.17g", r->angle, r->axis.x, r->axis.y, r->axis.z, r->ref.x, r->ref.y, r->ref.z);
		}
	Put (t, "tf", s, false);
	s.clear ();
	for (int c = AIRCTRL_ELEVATOR; c <= AIRCTRL_RUDDERTRIM; c++) s += F (" %.17g", v->GetControlSurfaceLevel ((AIRCTRL_TYPE)c));
	Put (t, "cs", s, true);
}

// the bodies of WB_PROBE_BODIES, and the atmosphere the focus vessel is in
static void Bodies (VESSEL *f)
{
	static bool first = true;
	for (auto &n : g_bodies) {
		OBJHANDLE h = oapiGetGbodyByName ((char *)n.c_str ());
		if (!h) { Put ("body", n, "not found", false); continue; }
		VECTOR3 p, u;
		oapiGetGlobalPos (h, &p);
		oapiGetGlobalVel (h, &u);
		Put ("body", n, F ("%.17g | ", oapiGetSimMJD ()) + Vs (p) + " | " + Vs (u), true);
		if (!first || oapiGetObjectType (h) != OBJTP_PLANET) continue;
		const ATMCONST *c = oapiGetPlanetAtmConstants (h);
		Put ("atmc", n, c ? F ("%.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g | ", c->p0, c->rho0, c->R, c->gamma, c->C, c->O2pp,
			c->altlimit, c->radlimit, c->horizonalt) + Vs (c->color0) : std::string ("none"), true);
	}
	if (f && !g_bodies.empty ()) Put (f->GetName (), "atm", F ("%.17g %.17g %.17g", f->GetAtmDensity (), f->GetAtmPressure (),
		f->GetAtmTemperature ()), true);
	first = false;
}

// the camera (WB_PROBE_CAMERA)
static void Camera (VESSEL *f)
{
	if (!g_camera || !f) return;
	VECTOR3 gd;
	MATRIX3 R;
	oapiCameraGlobalDir (&gd);
	f->GetRotationMatrix (R);
	VECTOR3 d = tmul (R, gd);
	Put ("camera", "c", F ("%d %d %s %.6f %.6f %.6f %.6f | ", oapiCameraMode (), oapiCameraInternal () ? 1 : 0, Name (oapiCameraTarget ()).c_str (),
		oapiCameraTargetDist (), oapiCameraAzimuth (), oapiCameraPolar (), oapiCameraAperture ()) + F ("%.6f %.6f %.6f", d.x, d.y, d.z), false);
}

// 10 km over the equator at longitude -40, 200 m/s east of the ground, level, nose east
static void Start (VESSEL *v)
{
	OBJHANDLE e = oapiGetGbodyByName ((char *)"Earth");
	double R = oapiGetSize (e) + 10e3, lng = -40 * RAD, d = 1e-6;
	VECTOR3 c, p, pe;
	oapiGetGlobalPos (e, &c);
	oapiEquToGlobal (e, lng, 0, R, &p);
	oapiEquToGlobal (e, lng + d, 0, R, &pe);
	VECTOR3 up = unit (p - c), east = unit (pe - p);
	VESSELSTATUS2 vs;
	memset (&vs, 0, sizeof vs);
	vs.version = 2;
	vs.rbody = e;
	vs.status = 0;
	vs.rpos = p - c;
	vs.rvel = east * (PI2 * R / oapiGetPlanetPeriod (e) + 200);
	v->DefSetStateEx (&vs);
	VECTOR3 z = east, y = up, x = crossp (y, z);
	v->SetRotationMatrix (_M (x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z));
	v->SetAngularVel (_V (0, 0, 0));
}

class Probe : public oapi::Module {
public:
	Probe (HINSTANCE h) : Module (h) {}
	void clbkPreStep (double simt, double, double) override
	{
		g_frame++;
		if (g_kill >= 0 && g_frame == g_kill) {
			fprintf (g_out, "killing the helper at frame %ld\n", g_frame);
			fflush (g_out);
			if (system ("pkill -KILL -f 'WineBridge/Orbiter.exe'")) {}
		}
		if (g_mode == "trace") {
			for (auto &w : g_warps) if (w.first == g_frame) oapiSetTimeAcceleration (w.second);
			for (auto &f : g_focus) if (f.first == g_frame) {
				OBJHANDLE h = oapiGetVesselByName ((char *)f.second.c_str ());
				OBJHANDLE old = h ? oapiSetFocusObject (h) : nullptr;
				fprintf (g_out, "f%ld focus %s %s\n", g_frame, f.second.c_str (), h ? (old ? "set" : "set, no old") : "not found");
			}
			for (auto &k : g_mfdkeys) if (k.first == g_frame) { // a key to an MFD, as Orbiter's keyboard sends it
				int mfd = (int)k.second >> 8; DWORD key = (DWORD)k.second & 0xff;
				int r = oapiSendMFDKey (mfd, key);
				fprintf (g_out, "f%ld mfd %d key 0x%02x %d mode %d\n", g_frame, mfd, (unsigned)key, r, oapiGetMFDMode (mfd));
			}
			for (auto &k : g_mfdbtns) { // a button pressed this frame, released the next
				int mfd = (int)k.second >> 8, bt = (int)k.second & 0xff;
				if (k.first == g_frame) {
					const char *l = oapiMFDButtonLabel (mfd, bt);
					bool r = oapiProcessMFDButton (mfd, bt, PANEL_MOUSE_LBDOWN);
					fprintf (g_out, "f%ld mfd %d button %d '%s' %d\n", g_frame, mfd, bt, l ? l : "", r);
				} else if (k.first + 1 == g_frame) oapiProcessMFDButton (mfd, bt, PANEL_MOUSE_LBUP);
			}
			for (auto &k : g_keys) if (k.first == g_frame) {
				VESSEL *v = oapiGetFocusInterface ();
				DWORD code = (DWORD)k.second;
				char ks[256] = {};
				if (code & 0x100) ks[OAPI_KEY_LSHIFT] = (char)0x80;
				if (code & 0x200) ks[OAPI_KEY_LCONTROL] = (char)0x80;
				if (code & 0x400) ks[OAPI_KEY_LALT] = (char)0x80;
				int r = v->SendBufferedKey (code & 0xff, true, ks);
				fprintf (g_out, "f%ld %s key 0x%03x %d\n", g_frame, v->GetName (), code, r);
				Live (v);
			}
			return;
		}
		if (g_mode != "flight") return;
		VESSEL *v = oapiGetFocusInterface ();
		if (g_t0 < 0) { g_t0 = simt; g_next = simt; Start (v); return; }
		v->SetThrusterGroupLevel (THGROUP_MAIN, simt - g_t0 < 30 ? 1.0 : 0.0);
		v->SetControlSurfaceLevel (AIRCTRL_ELEVATOR, 0.3, true);
	}
	void clbkPostStep (double simt, double, double) override
	{
		if (g_mode == "trace") {
			VESSEL *f = oapiGetFocusInterface ();
			for (DWORD i = 0; i < oapiGetVesselCount (); i++) {
				VESSEL *v = oapiGetVesselInterface (oapiGetVesselByIndex (i));
				if (g_frame == 1) Props (v);
				if (v == f || g_frame % 25 == 1) State (v);
				Live (v);
			}
			Bodies (f);
			Camera (f);
			fflush (g_out);
			return;
		}
		if (g_mode == "props") {
			if (g_frame == 3 && g_save) oapiSaveScenario (g_save, "WineBridge test: saved by the probe");
			if (g_frame != 1) return;
			for (DWORD i = 0; i < oapiGetVesselCount (); i++) Props (oapiGetVesselInterface (oapiGetVesselByIndex (i)));
			fflush (g_out);
			return;
		}
		if (g_mode != "flight" || g_t0 < 0 || simt < g_next - 1e-9) return;
		g_next += 1;
		VESSEL *v = oapiGetFocusInterface ();
		OBJHANDLE e = oapiGetGbodyByName ((char *)"Earth");
		VECTOR3 r, u;
		v->GetRelativePos (e, r);
		v->GetRelativeVel (e, u);
		fprintf (g_out, "t %.2f pos %.17g %.17g %.17g vel %.17g %.17g %.17g alt %.3f air %.3f aoa %.6f lift %.6f drag %.6f elev %.6f anim %.6f %.6f %.6f\n",
			simt - g_t0, r.x, r.y, r.z, u.x, u.y, u.z, v->GetAltitude (), v->GetAirspeed (), v->GetAOA (), v->GetLift (), v->GetDrag (),
			v->GetControlSurfaceLevel (AIRCTRL_ELEVATOR), v->GetAnimation (0), v->GetAnimation (1), v->GetAnimation (2));
		Bodies (v);
		fflush (g_out);
	}
};

DLLCLBK void InitModule (HINSTANCE h)
{
	const char *m = getenv ("WB_PROBE"), *k = getenv ("WB_PROBE_KILL");
	g_save = getenv ("WB_PROBE_SAVE");
	g_mode = m ? m : "";
	if (k) g_kill = atol (k);
	Pairs (getenv ("WB_PROBE_KEYS"), g_keys);
	Pairs (getenv ("WB_PROBE_WARP"), g_warps);
	Names (getenv ("WB_PROBE_FOCUS"), g_focus);
	Pairs (getenv ("WB_PROBE_MFDKEYS"), g_mfdkeys);
	Pairs (getenv ("WB_PROBE_MFDBTN"), g_mfdbtns);
	g_camera = getenv ("WB_PROBE_CAMERA") != nullptr;
	for (const char *b = getenv ("WB_PROBE_BODIES"); b && *b; ) {
		const char *c = strchr (b, ',');
		g_bodies.push_back (c ? std::string (b, c - b) : std::string (b));
		b = c ? c + 1 : nullptr;
	}
	g_out = fopen ("WbProbe.log", "w");
	if (g_out) oapiRegisterModule (new Probe (h));
}

DLLCLBK void ExitModule (HINSTANCE)
{
	if (g_out) fclose (g_out);
	g_out = nullptr;
}
