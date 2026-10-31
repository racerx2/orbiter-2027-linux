// WineBridge helper: camera modes (Camera.cpp of the Windows source) and external camera control (CamAPI.cpp). A module built with
// the 2010 SDK lays out CameraMode_Cockpit and CameraMode_Ground as the 2010 headers do: its objects are the 2010 classes here
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include "CamAPI.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "wb_helper.h"
#include "compat2010.h"

namespace wb {
	// the 2010 SDK's CameraMode_Cockpit: no members of its own, an Init that does nothing
	class CameraMode_Cockpit2010 : public CameraMode {
	public:
		void Init (char *) override {}
		void Store (char *str) override;
		Mode GetMode () const override { return CM_COCKPIT; }
		void GetDescr (char *str, int len) override;
	};
	// the 2010 SDK's CameraMode_Ground: no altitude mode
	class CameraMode_Ground2010 : public CameraMode {
	public:
		CameraMode_Ground2010 () { ref = 0; alt = 0; lng = lat = phi = theta = 0; tgtlock = true; }
		void Init (char *str) override;
		void Store (char *str) override;
		Mode GetMode () const override { return CM_GROUND; }
		void GetDescr (char *str, int len) override;
		OBJHANDLE ref;
		double lng, lat;
		double alt;
		double phi, theta;
		bool tgtlock;
	};
}

// the 2010 cockpit mode has no members of its own (the 2016 one four); the 2010 ground mode one bool less, in the padding at its end
static_assert (sizeof (wb::CameraMode_Cockpit2010) == sizeof (CameraMode) && sizeof (CameraMode_Cockpit) == sizeof (CameraMode) + 16,
	"the SDKs' cockpit mode sizes");
static_assert (sizeof (CameraMode_Ground) == sizeof (wb::CameraMode_Ground2010), "the SDKs' ground mode sizes");
static_assert (sizeof (ExternalCameraControl::VCMode) == 32 && sizeof (ExternalCameraControl::TrackMode) == 24, "as on Linux");

namespace {
	// the module the code at addr is in has the 2010 layouts
	bool Old (const void *addr) { return wb::AddrModule (addr) && wb::CallerApi (addr) < wb::API_2016; }

	// an object's layout: one of the 2010 classes here, a 2010 module's own class, or the 2016 one
	template <class V> bool Is2010 (const void *obj)
	{
		static V v;
		void *vt = *(void *const *)obj;
		return vt == *(void **)&v || Old (vt);
	}

	const char *Name (OBJHANDLE h, char *buf)
	{
		buf[0] = 0;
		if (h) oapiGetObjectName (h, buf, 256);
		return buf;
	}

	// the head of a mode's description, as Orbiter's: the target's name, then the mode
	void Descr (OBJHANDLE target, const char *mode, char *str, int len)
	{
		char cbuf[272] = "", nm[256];
		if (target) _snprintf (cbuf, 256, "%s ", Name (target, nm));
		strcat (cbuf, mode);
		if (len > 0) { strncpy (str, cbuf, len - 1); str[len - 1] = 0; }
	}

	// a stored mode, as Orbiter's: only a whole one, else ""
	void Put (char *str, const char *tmp, int n)
	{
		if (n >= 0 && n < 256) memcpy (str, tmp, n + 1);
		else str[0] = 0;
	}
}

// ---------------------------------------------------------------------
// CameraMode

CameraMode::CameraMode ()
{
	target = 0;
	fov = 0;
}

void CameraMode::SetTarget (OBJHANDLE hTgt)
{
	target = hTgt;
}

void CameraMode::SetFOV (double FOV)
{
	fov = FOV;
}

// as Orbiter's; the object is the calling module's, made with its operator new, which its delete frees
CameraMode *CameraMode::Create (char *str)
{
	const void *ret = __builtin_return_address (0);
	bool old = Old (ret);
	int mod = wb::AddrModule (ret);
	char *pc, *tc;
	if (!(pc = strtok (str, ":"))) return 0;
	while (*pc == ' ' || *pc == '\t') pc++;
	tc = pc;
	for (char *e = tc + strlen (tc); e > tc && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
	int kind = !_stricmp (tc, "Track") ? 1 : !_stricmp (tc, "Ground") ? 2 : 0;
	size_t size = kind == 1 ? sizeof (CameraMode_Track) : kind == 2 ? sizeof (CameraMode_Ground) : old ? sizeof (wb::CameraMode_Cockpit2010) : sizeof (CameraMode_Cockpit);
	void *mem = wb::ModuleAlloc (mod, size);
	if (!mem) return 0;
	CameraMode *cm;
	if (kind == 1) cm = new (mem) CameraMode_Track;
	else if (kind == 2) cm = old ? (CameraMode *)new (mem) wb::CameraMode_Ground2010 : new (mem) CameraMode_Ground;
	else cm = old ? (CameraMode *)new (mem) wb::CameraMode_Cockpit2010 : new (mem) CameraMode_Cockpit;
	char nm[256] = "";
	if ((pc = strtok (NULL, ":"))) {
		while (*pc == ' ' || *pc == '\t') pc++;
		strncpy (nm, pc, 255);
		for (char *e = nm + strlen (nm); e > nm && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
	}
	if (!pc || !(cm->target = oapiGetObjectByName (nm))) {
		wb::ModuleRelease (mod, mem);
		return 0;
	}
	if ((pc = strtok (NULL, ":"))) sscanf (pc, "%lf", &cm->fov);
	if ((pc = strtok (NULL, ""))) {
		while (*pc == ' ' || *pc == '\t') pc++;
		for (char *e = pc + strlen (pc); e > pc && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
		cm->Init (pc);
	}
	return cm;
}

// ---------------------------------------------------------------------
// CameraMode_Cockpit

CameraMode_Cockpit::CameraMode_Cockpit (): CameraMode ()
{
	if (Old (__builtin_return_address (0))) { new (this) wb::CameraMode_Cockpit2010; return; } // a 2010 module's object: no members here
	cmode = CM_CURRENT;
	pos = -1;
	lean = -1;
	lean_smooth = false;
}

void CameraMode_Cockpit::Init (char *str)
{
	if (!str || str[0] == '\0') return;
	if (!_strnicmp (str, "generic", 7)) {
		cmode = CM_GENERIC;
		str += 7;
	} else if (!_strnicmp (str, "panel2d", 7)) {
		cmode = CM_PANEL2D;
		str += 7;
		if (str[0] == ':' && sscanf (++str, "%d", &pos))
			while (*str != ' ' && *str != '\0') str++;
	} else if (!_strnicmp (str, "vc", 2)) {
		cmode = CM_VC;
		str += 2;
		if (str[0] == ':' && sscanf (++str, "%d", &pos)) {
			while (*str != ' ' && *str != '\0' && *str != ':') str++;
			if (*str == ':' && sscanf (++str, "%d", &lean)) {
				lean_smooth = false;
				while (*str != ' ' && *str != '\0' && toupper (*str) != 'S') str++;
				if (toupper (*str) == 'S') {
					lean_smooth = true;
					str++;
				}
			}
		}
	} else if (!_strnicmp (str, "current", 7)) {
		cmode = CM_CURRENT;
		str += 7;
	}
}

void CameraMode_Cockpit::GetDescr (char *str, int len)
{
	Descr (target, "Cockpit ", str, len);
}

void CameraMode_Cockpit::Store (char *str)
{
	char tmp[256], nm[256];
	Put (str, tmp, _snprintf (tmp, sizeof tmp, "Cockpit:%s:%0.2f", target ? Name (target, nm) : "-", fov));
}

void wb::CameraMode_Cockpit2010::GetDescr (char *str, int len)
{
	Descr (target, "Cockpit ", str, len);
}

void wb::CameraMode_Cockpit2010::Store (char *str)
{
	char tmp[256], nm[256];
	Put (str, tmp, _snprintf (tmp, sizeof tmp, "Cockpit:%s:%0.2f", target ? Name (target, nm) : "-", fov));
}

// ---------------------------------------------------------------------
// CameraMode_Track (the same in both SDKs)

CameraMode_Track::CameraMode_Track (): CameraMode ()
{
	reldist = 0;
	tmode = TM_CURRENT;
	phi = theta = 0;
	ref = 0;
}

void CameraMode_Track::Init (char *str)
{
	char tm[64] = "", rf[256] = "";
	sscanf (str, "%63s%lf%lf%lf%255s", tm, &reldist, &phi, &theta, rf);
	if (!_stricmp (tm, "RELATIVE"))
		tmode = TM_RELATIVE;
	else if (!_stricmp (tm, "ABSDIR"))
		tmode = TM_ABSDIR;
	else if (!_stricmp (tm, "GLOBAL"))
		tmode = TM_GLOBAL;
	else if (!_stricmp (tm, "TARGETTOREF") || !_stricmp (tm, "TARGETFROMREF")) {
		tmode = !_stricmp (tm, "TARGETTOREF") ? TM_TARGETTOREF : TM_TARGETFROMREF;
		OBJHANDLE r = oapiGetObjectByName (rf);
		if (r) ref = r;
		else tmode = TM_CURRENT;
	}
}

void CameraMode_Track::Store (char *str)
{
	static const char *tmstr[6] = {"CURRENT","RELATIVE", "ABSDIR", "GLOBAL", "TARGETTOREF", "TARGETFROMREF"};
	char tmp[256], nm[256], rn[256];
	int n = _snprintf (tmp, sizeof tmp, "Track:%s:%0.2f:%s %0.3f %0.3f %0.3f", target ? Name (target, nm) : "-", fov,
		tmstr[tmode >= 0 && tmode < 6 ? tmode : 0], reldist, phi, theta);
	if (n >= 0 && n < 256 && (tmode == TM_TARGETTOREF || tmode == TM_TARGETFROMREF)) {
		int k = _snprintf (tmp + n, sizeof tmp - n, " %s", Name (ref, rn));
		n = k < 0 ? -1 : n + k;
	}
	Put (str, tmp, n);
}

void CameraMode_Track::GetDescr (char *str, int len)
{
	const char *m = tmode == TM_RELATIVE ? "Track relative " : tmode == TM_ABSDIR ? "Track fixed " : tmode == TM_GLOBAL ? "Track global " : "Track ";
	Descr (target, m, str, len);
}

void CameraMode_Track::SetTrackMode (TrackMode trackmode, OBJHANDLE refobj)
{
	tmode = trackmode;
	ref = refobj;
}

void CameraMode_Track::SetPosition (double rd, double ph, double th)
{
	reldist = rd;
	phi = ph;
	theta = th;
}

void CameraMode_Track::GetPosition (double *rd, double *ph, double *th) const
{
	*rd = reldist;
	*ph = phi;
	*th = theta;
}

// ---------------------------------------------------------------------
// CameraMode_Ground

CameraMode_Ground::CameraMode_Ground (): CameraMode ()
{
	if (Old (__builtin_return_address (0))) { new (this) wb::CameraMode_Ground2010; return; } // a 2010 module's object
	ref = 0;
	alt = 0;
	lng = lat = phi = theta = 0;
	alt_above_ground = true;
	tgtlock = true;
}

// Init and Store are reached through a 2010 module's own vftable too (its inline copies set it): its objects go to the 2010 code
void CameraMode_Ground::Init (char *str)
{
	if (Is2010<wb::CameraMode_Ground2010> (this)) { ((wb::CameraMode_Ground2010 *)(CameraMode *)this)->wb::CameraMode_Ground2010::Init (str); return; }
	char rf[256] = "", alt_mode = 0;
	int i = sscanf (str, "%255s%lf%lf%lf%c%lf%lf", rf, &lng, &lat, &alt, &alt_mode, &phi, &theta);
	lng *= RAD, lat *= RAD;
	alt_above_ground = (toupper (alt_mode) != 'M');
	tgtlock = (i < 7);
	ref = oapiGetObjectByName (rf);
}

void CameraMode_Ground::Store (char *str)
{
	if (Is2010<wb::CameraMode_Ground2010> (this)) { ((wb::CameraMode_Ground2010 *)(CameraMode *)this)->wb::CameraMode_Ground2010::Store (str); return; }
	char tmp[256], nm[256], rn[256];
	int n = _snprintf (tmp, sizeof tmp, "Ground:%s:%0.2f:%s %0.5f %0.5f %0.2f%s", target ? Name (target, nm) : "-", fov,
		ref ? Name (ref, rn) : "-", lng*DEG, lat*DEG, alt, alt_above_ground ? "" : "M");
	if (!tgtlock && n >= 0 && n < 256) {
		int k = _snprintf (tmp + n, sizeof tmp - n, " %0.2f %0.2f", phi, theta);
		n = k < 0 ? -1 : n + k;
	}
	Put (str, tmp, n);
}

void CameraMode_Ground::GetDescr (char *str, int len)
{
	Descr (target, "Ground ", str, len);
}

void wb::CameraMode_Ground2010::Init (char *str)
{
	char rf[256] = "";
	int i = sscanf (str, "%255s%lf%lf%lf%lf%lf", rf, &lng, &lat, &alt, &phi, &theta);
	lng *= RAD, lat *= RAD;
	tgtlock = (i < 6);
	ref = oapiGetObjectByName (rf);
}

void wb::CameraMode_Ground2010::Store (char *str)
{
	char tmp[256], nm[256], rn[256];
	int n = _snprintf (tmp, sizeof tmp, "Ground:%s:%0.2f:%s %0.5f %0.5f %0.2f", target ? Name (target, nm) : "-", fov,
		ref ? Name (ref, rn) : "-", lng*DEG, lat*DEG, alt);
	if (!tgtlock && n >= 0 && n < 256) {
		int k = _snprintf (tmp + n, sizeof tmp - n, " %0.2f %0.2f", phi, theta);
		n = k < 0 ? -1 : n + k;
	}
	Put (str, tmp, n);
}

void wb::CameraMode_Ground2010::GetDescr (char *str, int len)
{
	Descr (target, "Ground ", str, len);
}

// the non-virtual members: the 2010 layout for a 2010 module's object
void CameraMode_Ground::SetPosition (double longitude, double latitude, double altitude, OBJHANDLE hRef)
{
	if (Is2010<wb::CameraMode_Ground2010> (this)) {
		wb::CameraMode_Ground2010 *g = (wb::CameraMode_Ground2010 *)(void *)this;
		g->lng = longitude, g->lat = latitude, g->alt = altitude;
		if (hRef) g->ref = hRef;
		return;
	}
	lng = longitude;
	lat = latitude;
	alt = altitude;
	if (hRef) ref = hRef;
}

void CameraMode_Ground::GetPosition (double *longitude, double *latitude, double *altitude, OBJHANDLE *hRef) const
{
	if (Is2010<wb::CameraMode_Ground2010> (this)) {
		const wb::CameraMode_Ground2010 *g = (const wb::CameraMode_Ground2010 *)(const void *)this;
		*longitude = g->lng, *latitude = g->lat, *altitude = g->alt, *hRef = g->ref;
		return;
	}
	*longitude = lng;
	*latitude = lat;
	*altitude = alt;
	*hRef = ref;
}

void CameraMode_Ground::SetOrientation (double ph, double th)
{
	if (Is2010<wb::CameraMode_Ground2010> (this)) {
		wb::CameraMode_Ground2010 *g = (wb::CameraMode_Ground2010 *)(void *)this;
		g->phi = ph, g->theta = th, g->tgtlock = false;
		return;
	}
	phi = ph;
	theta = th;
	tgtlock = false;
}

void CameraMode_Ground::GetOrientation (double *ph, double *th) const
{
	if (Is2010<wb::CameraMode_Ground2010> (this)) {
		const wb::CameraMode_Ground2010 *g = (const wb::CameraMode_Ground2010 *)(const void *)this;
		*ph = g->phi, *th = g->theta;
		return;
	}
	*ph = phi;
	*th = theta;
}

// ---------------------------------------------------------------------
// the mode Orbiter's camera takes: its fields in the object's layout

bool oapiSetCameraMode (const CameraMode &mode)
{
	struct Base : CameraMode { using CameraMode::target; using CameraMode::fov; };
	const Base &b = (const Base &)mode;
	CameraMode::Mode m = mode.GetMode ();
	WbWriter &w = wb::Begin (L_SetCameraMode);
	w.i32 (m); w.u32 (wb::Id (b.target)); w.f64 (b.fov);
	switch (m) {
	case CameraMode::CM_COCKPIT:
		if (Is2010<wb::CameraMode_Cockpit2010> (&mode)) { w.i32 (CameraMode_Cockpit::CM_CURRENT); w.i32 (-1); w.i32 (-1); w.b (false); }
		else {
			const CameraMode_Cockpit &c = (const CameraMode_Cockpit &)mode;
			w.i32 (c.GetCockpitMode ()); w.i32 (c.GetPosition ()); w.i32 (c.GetLeaning ()); w.b (c.GetLeaningSmooth ());
		}
		break;
	case CameraMode::CM_TRACK: {
		const CameraMode_Track &t = (const CameraMode_Track &)mode;
		double rd, ph, th;
		t.GetPosition (&rd, &ph, &th);
		w.i32 (t.GetTrackMode ()); w.f64 (rd); w.f64 (ph); w.f64 (th); w.u32 (wb::Id (t.GetRef ()));
		break; }
	case CameraMode::CM_GROUND: {
		const CameraMode_Ground &g = (const CameraMode_Ground &)mode;
		double lng, lat, alt, ph, th;
		OBJHANDLE ref;
		g.GetPosition (&lng, &lat, &alt, &ref);
		g.GetOrientation (&ph, &th);
		bool old = Is2010<wb::CameraMode_Ground2010> (&mode);
		w.u32 (wb::Id (ref)); w.f64 (lng); w.f64 (lat); w.f64 (alt); w.f64 (ph); w.f64 (th);
		w.b (old ? true : g.GetAltMode ()); w.b (old ? ((const wb::CameraMode_Ground2010 &)(const CameraMode &)mode).tgtlock : g.GetTgtLock ());
		break; }
	}
	WbReader r = wb::Call ();
	bool ok = r.b ();
	return ok && !r.bad;
}

// ---------------------------------------------------------------------
// ExternalCameraControl: Orbiter's proxy polls the object; its modes follow the object's members (live items)

ExternalCameraControl::ExternalCameraControl (DWORD dmode, DWORD cmode)
{
	SetDataMode (dmode);
	SetCameraMode (cmode);
	vcmode.trackrotation = true;
	vcmode.trackposition = true;
	vcmode.freezeonmouse = false;
	vcmode.rotationrange = PI;
	vcmode.positionrange = 0.5;
	vcmode.freeze_t = 1.0;
	trkmode.trackrotation = true;
	trkmode.trackzoom = true;
	trkmode.rotationdata = TrackMode::BYROTATION;
	trkmode.deadzone = 0.2;
	trkmode.speed = 2.0;
}

void ExternalCameraControl::SetDataMode (DWORD mode)
{
	datamode = mode & 0x2F;
}

DWORD ExternalCameraControl::GetDataMode () const
{
	return datamode;
}

void ExternalCameraControl::SetCameraMode (DWORD mode)
{
	cameramode = mode & 0x1F;
}

DWORD ExternalCameraControl::GetCameraMode () const
{
	return cameramode;
}

namespace {
	ExternalCameraControl *g_ecc; // the one Orbiter's camera holds through its proxy (the camera takes one at a time)
	int g_eccModule;

	struct EccAccess : ExternalCameraControl {
		static void Live (WbWriter &w, ExternalCameraControl *e)
		{
			EccAccess *a = static_cast<EccAccess *> (e);
			const void *addr[4] = {&a->datamode, &a->cameramode, &a->vcmode, &a->trkmode};
			const uint32_t n[4] = {4, 4, sizeof (VCMode), sizeof (TrackMode)};
			for (int i = 0; i < 4; i++) {
				uint32_t id = wb::Live (0, WB_LIVE_BYTES, addr[i], n[i]);
				wb::LiveTag (id, wb::TAG_ECC, wb::Id (e));
				uint32_t len;
				const uint8_t *v = wb::LiveValue (id, &len);
				w.u32 (id); w.u32 (len); w.raw (v, len);
			}
		}
	};

	void EccDrop ()
	{
		if (g_ecc) wb::LiveDropTag (0, wb::TAG_ECC, wb::Id (g_ecc));
		g_ecc = 0;
		g_eccModule = 0;
	}
}

// Orbiter's camera decides (it holds one, none outside a session); the one it holds already is refused here, as there
bool oapiRegisterExternalCameraControl (ExternalCameraControl *ecc)
{
	if (ecc && ecc == g_ecc) return false;
	int module = ecc ? wb::AddrModule (*(void **)ecc) : 0;
	WbWriter &w = wb::Begin (L_EccRegister);
	w.u32 (wb::Id (ecc)); w.i32 (module);
	if (ecc) EccAccess::Live (w, ecc);
	WbReader r = wb::Call ();
	bool ok = r.b () && !r.bad;
	if (!ecc) return ok;
	if (!ok) { wb::LiveDropTag (0, wb::TAG_ECC, wb::Id (ecc)); return false; }
	EccDrop (); // none here: Linux tells when the camera that held one goes
	g_ecc = ecc;
	g_eccModule = module;
	return true;
}

// as Orbiter's: the control the camera holds, whichever module's
bool oapiUnregisterExternalCameraControl ()
{
	wb::Begin (L_EccUnregister);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	EccDrop (); // the camera holds none now
	return ok && !r.bad;
}

void wb::ServeEcc (WbReader &r, WbWriter &w)
{
	uint32_t id = r.u32 ();
	ExternalCameraControl::CamData d = {};
	bool ok = !r.bad && g_ecc && Id (g_ecc) == id && g_ecc->clbkPoll (&d);
	w.b (ok); w.f64 (d.x); w.f64 (d.y); w.f64 (d.z); w.f64 (d.yaw); w.f64 (d.pitch); w.f64 (d.roll);
}

void wb::ServeEccGone (WbReader &r)
{
	uint32_t id = r.u32 ();
	if (!r.bad && g_ecc && Id (g_ecc) == id) EccDrop ();
}

// the module is freed: Linux unregistered its control; its memory is read no more
void wb::CameraFreed (int module)
{
	if (g_ecc && g_eccModule == module) EccDrop ();
}

// ---------------------------------------------------------------------
// CameraMode_Cockpit's and CameraMode_Ground's copy constructor and assignment, as the calling module lays the objects out; exported
// under their names (api/inline.def)

namespace wb {
	struct CockpitCopy : CameraMode_Cockpit {
		CockpitCopy *Construct (const CockpitCopy &o);
		CockpitCopy &Assign (const CockpitCopy &o);
	};
	struct GroundCopy : CameraMode_Ground {
		GroundCopy *Construct (const GroundCopy &o);
		GroundCopy &Assign (const GroundCopy &o);
	};
}

static void CopyFields (void *to, const void *from, size_t size)
{
	memcpy ((char *)to + sizeof (void *), (const char *)from + sizeof (void *), size - sizeof (void *));
}

wb::CockpitCopy *wb::CockpitCopy::Construct (const CockpitCopy &o)
{
	bool old = Old (__builtin_return_address (0));
	if (old) new (this) CameraMode_Cockpit2010;
	else new (this) CameraMode_Cockpit;
	CopyFields (this, &o, old ? sizeof (CameraMode_Cockpit2010) : sizeof (CameraMode_Cockpit));
	return this;
}

wb::CockpitCopy &wb::CockpitCopy::Assign (const CockpitCopy &o)
{
	CopyFields (this, &o, Old (__builtin_return_address (0)) ? sizeof (CameraMode_Cockpit2010) : sizeof (CameraMode_Cockpit));
	return *this;
}

wb::GroundCopy *wb::GroundCopy::Construct (const GroundCopy &o)
{
	if (Old (__builtin_return_address (0))) new (this) CameraMode_Ground2010;
	else new (this) CameraMode_Ground;
	CopyFields (this, &o, sizeof (CameraMode_Ground));
	return this;
}

wb::GroundCopy &wb::GroundCopy::Assign (const GroundCopy &o)
{
	CopyFields (this, &o, sizeof (CameraMode_Ground));
	return *this;
}

// ---------------------------------------------------------------------
// the inline and compiler-made members Orbiter.exe exports (api/inline.def): made here (this file is built without inlining)

namespace {
	struct EccMade : ExternalCameraControl {
		EccMade () : ExternalCameraControl (0, 0) {}
		bool clbkPoll (CamData *) override { return false; }
	};
}

__attribute__ ((used)) static void Made (CameraMode_Cockpit *c, CameraMode_Track *t, CameraMode_Ground *g, EccMade *e)
{
	CameraMode_Track ct (*t);
	*t = ct;
	EccMade ce (*e);
	*e = ce;
	CameraMode &m = *c;
	OBJHANDLE h = m.GetTarget ();
	double f = m.GetFOV ();
	int v = c->GetCockpitMode () + c->GetPosition () + c->GetLeaning () + c->GetLeaningSmooth () + t->GetTrackMode () +
		g->GetTgtLock () + g->GetAltMode ();
	OBJHANDLE r = t->GetRef () ? t->GetRef () : g->GetRef ();
	const ExternalCameraControl::VCMode *vm = e->GetVCMode ();
	const ExternalCameraControl::TrackMode *tm = e->GetTrackMode ();
	CameraMode::Mode m1 = c->CameraMode_Cockpit::GetMode (), m2 = t->CameraMode_Track::GetMode (), m3 = g->CameraMode_Ground::GetMode ();
	(void)h; (void)f; (void)v; (void)r; (void)vm; (void)tm; (void)m1; (void)m2; (void)m3;
}
