// GPSNav: the unit
#include "Unit.h"
#include "imgui.h"
#include "imgui_extras.h"

// not upstream in Orbiter (the Custom build's gpsnav-hud): weak, so the module loads in an Orbiter without it (no caret there)
OAPIFUNC void oapiSetHUDBearingTarget (OBJHANDLE hVessel, OBJHANDLE hBody, double lng, double lat) __attribute__((weak));
namespace ImGui { OAPIFUNC void SetNextWindowAspect (float aspect, float extra, float min_width) __attribute__((weak)); } // likewise (gpsnav-window)
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

using namespace gps;

namespace {

const float ASPECT = 1.45f; // the unit's width over its height
const double RANGES[] = {0.2, 0.5, 1, 2, 3, 5, 7.5, 10, 15, 20, 30, 50, 75, 100, 150, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 5000};
const int NRANGE = sizeof RANGES / sizeof RANGES[0];
const char *ALPHA = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
enum { F_GS, F_DTK, F_TRK, F_DIS, F_ETE, F_BRG, F_XTK, F_ETA, F_ALT, F_VS, F_WPT, NFIELD };
const char *FIELDS[NFIELD] = {"GS", "DTK", "TRK", "DIS", "ETE", "BRG", "XTK", "ETA", "ALT", "VS", "WPT"};
const char *NRST_PAGE[5] = {"APT", "VOR", "NDB", "BASE", "USER"};
const char *NRST_TITLE[5] = {"NEAREST AIRPORTS", "NEAREST VOR", "NEAREST NDB", "NEAREST BASES", "NEAREST USER WPTS"};
const char *GROUP[Unit::NGROUP] = {"MAP", "WPT", "NRST", "FPL", "VNAV"};
enum { M_UNITS, M_FIELD1, M_FIELD5 = M_FIELD1 + 4, M_MAP, M_COLOURS, M_DOWNLOAD, M_MARKER, M_CDI, M_GP, M_TCH, M_CANCEL, M_CLEAR, NMENU }; // the MENU's lines
const char *MAPMODE[3] = {"TOPO", "TERRAIN", "PLAIN"};
const char *CDISCALE[4] = {"AUTO", "5.0 NM", "1.0 NM", "0.3 NM"};

const ImU32 C_MAG = IM_COL32 (255, 70, 255, 255), C_CYAN = IM_COL32 (0, 225, 255, 255), C_WHITE = IM_COL32 (235, 235, 235, 255);
const ImU32 C_GREEN = IM_COL32 (40, 230, 80, 255), C_AMBER = IM_COL32 (255, 190, 0, 255), C_GREY = IM_COL32 (135, 135, 135, 255);
const ImU32 C_BLACK = IM_COL32 (0, 0, 0, 255), C_MAPBG = IM_COL32 (3, 9, 26, 255), C_LAND = IM_COL32 (160, 135, 85, 255);
const ImU32 C_GRID = IM_COL32 (60, 70, 95, 255), C_VOR = IM_COL32 (90, 190, 255, 255), C_NDB = IM_COL32 (215, 150, 60, 255);
const ImU32 C_RWY = IM_COL32 (125, 125, 125, 255), C_SEL = IM_COL32 (0, 150, 175, 255);
const ImU32 C_CITY = IM_COL32 (240, 210, 70, 255), C_MARK = IM_COL32 (190, 190, 190, 255), C_COAST = IM_COL32 (20, 40, 85, 255);

ImFont *F; // this frame's font

float TextW (float size, const std::string &s) { return F->CalcTextSizeA (size, FLT_MAX, 0.0f, s.c_str ()).x; }

// align: 0 left, 1 centre, 2 right; y is the top
void Text (ImDrawList *dl, float x, float y, float size, ImU32 col, const std::string &s, int align = 0)
{
	if (align) x -= (align == 1 ? 0.5f : 1.0f) * TextW (size, s);
	dl->AddText (F, size, ImVec2 (x, y), col, s.c_str ());
}

std::string Fit (std::string s, float size, float w) // cut to a width
{
	while (!s.empty () && TextW (size, s) > w) s.pop_back ();
	return s;
}

std::string Upper (std::string s) { for (char &c : s) c = (char)toupper ((unsigned char)c); return s; }

const char *ShortKind (const Wpt &w) // a list's kind column
{
	switch (w.kind) {
	case Kind::Airport: return "AIRPORT";
	case Kind::Heliport: return "HELI";
	case Kind::Seaplane: return "SEAPLN";
	case Kind::Base: return "BASE";
	case Kind::Pad: return "PAD";
	case Kind::Vor: return "VOR";
	case Kind::Ndb: return "NDB";
	case Kind::Dme: return "DME";
	case Kind::Ils: return "ILS";
	case Kind::Landmark: return w.size ? "CITY" : "MARK";
	default: return "USER";
	}
}

bool WantApt (const Wpt &w) { return w.kind == Kind::Airport || w.kind == Kind::Heliport || w.kind == Kind::Seaplane; }
bool WantVor (const Wpt &w) { return w.kind == Kind::Vor || w.kind == Kind::Dme || w.kind == Kind::Ils; }
bool WantNdb (const Wpt &w) { return w.kind == Kind::Ndb; }
bool WantBase (const Wpt &w) { return w.kind == Kind::Base || w.kind == Kind::Pad; }
bool WantUser (const Wpt &w) { return w.kind == Kind::User; }
bool (*const WANT[5]) (const Wpt &) = {WantApt, WantVor, WantNdb, WantBase, WantUser};

// a plan's waypoint in the body's list
int Lookup (const Body *b, const PlanWpt &p)
{
	int r = -1;
	if (b) b->Near (p.pos, 1e-5, [&] (int i) { if (r < 0 && b->wpts[i].ident == p.ident && Angle (b->wpts[i].pos, p.pos) < 1e-6) r = i; });
	return r;
}

// the map's symbols: 0 hidden, 1 the symbol, 2 with its ident; r is the range in nm
int Show (const Wpt &w, double r, int dcl)
{
	double s, l;
	switch (w.kind) {
	case Kind::Airport: s = w.size == 2 ? 1500 : w.size == 1 ? 250 : 40; l = w.size == 2 ? 300 : w.size == 1 ? 60 : 12; break;
	case Kind::Heliport: case Kind::Seaplane: s = 15, l = 5; break;
	case Kind::Base: s = 5000, l = 2000; break;
	case Kind::Pad: s = 5, l = 1.5; break;
	case Kind::Vor: case Kind::Dme: s = 400, l = 100; break;
	case Kind::Ndb: s = 60, l = 20; break;
	case Kind::Ils: s = 30, l = 10; break;
	case Kind::Landmark: s = w.size ? 400 : 1500, l = w.size ? 150 : 400; break;
	default: s = 3000, l = 500; break;
	}
	if (dcl >= 1 && (w.kind == Kind::Ndb || w.kind == Kind::Dme || w.kind == Kind::Heliport || w.kind == Kind::Seaplane)) return 0;
	if (dcl >= 2 && IsNavaid (w.kind)) return 0;
	double f = dcl == 0 ? 1.0 : dcl == 1 ? 0.4 : 0.15;
	return r > s * f ? 0 : r > l * f ? 1 : 2;
}

void Symbol (ImDrawList *dl, ImVec2 p, const Wpt &w, float u)
{
	switch (w.kind) {
	case Kind::Airport:
		if (w.size) {
			dl->AddCircle (p, u * (w.size == 2 ? 1.3f : 1.0f), C_CYAN, 16, 2.0f);
			for (int k = 0; k < 4; k++) {
				float a = k * 1.5708f, c = std::cos (a), s = std::sin (a), r = u * (w.size == 2 ? 1.3f : 1.0f);
				dl->AddLine (ImVec2 (p.x + c * r, p.y + s * r), ImVec2 (p.x + c * (r + 0.5f * u), p.y + s * (r + 0.5f * u)), C_CYAN, 2.0f);
			}
		} else dl->AddCircle (p, 0.75f * u, C_CYAN, 12, 1.5f);
		break;
	case Kind::Heliport:
		dl->AddCircle (p, 0.8f * u, C_CYAN, 12, 1.2f);
		Text (dl, p.x, p.y - 0.6f * u, 1.2f * u, C_CYAN, "H", 1);
		break;
	case Kind::Seaplane:
		dl->AddCircle (p, 0.8f * u, C_CYAN, 12, 1.2f);
		dl->AddLine (ImVec2 (p.x - 0.5f * u, p.y), ImVec2 (p.x + 0.5f * u, p.y), C_CYAN, 1.2f);
		break;
	case Kind::Base:
		dl->AddRect (ImVec2 (p.x - u, p.y - u), ImVec2 (p.x + u, p.y + u), C_WHITE, 0.0f, 0, 2.0f);
		dl->AddCircleFilled (p, 0.3f * u, C_WHITE);
		break;
	case Kind::Pad:
		dl->AddRect (ImVec2 (p.x - 0.6f * u, p.y - 0.6f * u), ImVec2 (p.x + 0.6f * u, p.y + 0.6f * u), C_GREEN, 0.0f, 0, 1.5f);
		break;
	case Kind::Vor:
		dl->AddNgon (p, u, C_VOR, 6, 1.6f);
		dl->AddCircleFilled (p, 0.22f * u, C_VOR);
		break;
	case Kind::Dme:
		dl->AddRect (ImVec2 (p.x - 0.8f * u, p.y - 0.8f * u), ImVec2 (p.x + 0.8f * u, p.y + 0.8f * u), C_VOR, 0.0f, 0, 1.4f);
		break;
	case Kind::Ndb:
		for (int k = 0; k < 10; k++) dl->AddCircleFilled (ImVec2 (p.x + u * std::cos (k * 0.6283f), p.y + u * std::sin (k * 0.6283f)), 0.12f * u + 0.5f, C_NDB);
		dl->AddCircleFilled (p, 0.3f * u, C_NDB);
		break;
	case Kind::Ils:
		dl->AddQuad (ImVec2 (p.x, p.y - 0.8f * u), ImVec2 (p.x + 0.5f * u, p.y), ImVec2 (p.x, p.y + 0.8f * u), ImVec2 (p.x - 0.5f * u, p.y), C_NDB, 1.4f);
		break;
	case Kind::Landmark: // a city a yellow spot, as on charts; another place a small grey cross
		if (w.size) dl->AddCircleFilled (p, 0.55f * u, C_CITY), dl->AddCircle (p, 0.55f * u, C_BLACK, 12, 1.0f);
		else dl->AddLine (ImVec2 (p.x - 0.5f * u, p.y), ImVec2 (p.x + 0.5f * u, p.y), C_MARK, 1.5f), dl->AddLine (ImVec2 (p.x, p.y - 0.5f * u), ImVec2 (p.x, p.y + 0.5f * u), C_MARK, 1.5f);
		break;
	default:
		dl->AddQuadFilled (ImVec2 (p.x, p.y - 0.8f * u), ImVec2 (p.x + 0.8f * u, p.y), ImVec2 (p.x, p.y + 0.8f * u), ImVec2 (p.x - 0.8f * u, p.y), C_WHITE);
		break;
	}
	if (w.tunable && w.kind != Kind::Pad) dl->AddCircleFilled (ImVec2 (p.x + u, p.y - u), 0.25f * u, C_GREEN);
}

void SizeWidth (ImGuiSizeCallbackData *d) // an Orbiter without SetNextWindowAspect: the height follows the width
{
	d->DesiredSize.y = d->DesiredSize.x / ASPECT + ImGui::GetFrameHeight ();
}

} // namespace

// ---------------------------------------------------------------------

std::string Options::Save () const
{
	char b[160];
	snprintf (b, sizeof b, "%d %d %d %d %d %d %d %d %d %.3f %.1f %d %d %d %d", metric ? 1 : 0, trackUp ? 1 : 0, dcl, range,
		fields[0], fields[1], fields[2], fields[3], fields[4], gpAngle * gps::DEG, tch, mapMode, marker ? 1 : 0, cdi, mapDownload ? 1 : 0);
	return b;
}

void Options::Load (const char *s)
{
	int m, t, d, r, f[5], c = 0, mk = 1, cd = 0, dl = 1;
	double a, h;
	if (sscanf (s, "%d%d%d%d%d%d%d%d%d%lf%lf%d%d%d%d", &m, &t, &d, &r, f, f + 1, f + 2, f + 3, f + 4, &a, &h, &c, &mk, &cd, &dl) < 11) return; // the map colours, the marker, the CDI and the map download came later
	metric = m != 0, trackUp = t != 0, dcl = std::clamp (d, 0, 2), range = std::clamp (r, 0, NRANGE - 1);
	for (int i = 0; i < 5; i++) fields[i] = std::clamp (f[i], 0, NFIELD - 1);
	gpAngle = std::clamp (a, 1.0, 30.0) * gps::RAD, tch = std::clamp (h, 0.0, 100.0), mapMode = std::clamp (c, 0, 2), marker = mk != 0, cdi = std::clamp (cd, 0, 3), mapDownload = dl != 0;
}

// ---------------------------------------------------------------------

struct Unit::Ctx {
	VESSEL *v = nullptr;
	std::string vname, body;
	OBJHANDLE hRef = nullptr;
	const Body *b = nullptr;          // null while the database is being made
	double R = 1.0;
	LatLon pos;
	double alt = 0.0, hdg = 0.0, gE = 0.0, gN = 0.0, gU = 0.0;
	bool ground = false;               // on the ground: no terrain warning colours
	Plan *plan = nullptr;              // the vessel's, when it is on this body
	Guidance g;
	double mjd = 0.0, sys = 0.0;
};

Unit::Unit (Library &l, std::map<std::string, Plan> &p, Options &o)
	: ImGuiDialog ("GPS Navigator", {620.0f, 620.0f / ASPECT + 20.0f}), lib (l), plans (p), opt (o)
{
	SetHelp ("html/plugin/GPSNav.chm", "/GPSNav.htm");
}

void Unit::Reset ()
{
	ov = O_NONE, cursor = false, row = 0, found.clear (), wptBody = nullptr, wptIdx = -1, nrst.clear (), nrstFor = -1, seen = nullptr, rwySel = -1;
	active = false; // the dialog manager went with the simulation
	gpss = false, gpssPending.clear ();
	if (oapiSetHUDBearingTarget) oapiSetHUDBearingTarget (nullptr, nullptr, 0.0, 0.0);
	terrain.Reset ();
}

void Unit::Display ()
{
	ImGui::SetNextWindowSize (ImVec2 (defaultSize.width, defaultSize.height), ImGuiCond_FirstUseEver);
	if (ImGui::SetNextWindowAspect) ImGui::SetNextWindowAspect (ASPECT, ImGui::GetFrameHeight (), 360.0f); // the unit keeps its shape
	else ImGui::SetNextWindowSizeConstraints (ImVec2 (360.0f, 0.0f), ImVec2 (FLT_MAX, FLT_MAX), SizeWidth);
	ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (0.0f, 0.0f));
	bool open = ImGui::Begin (name.c_str (), &active, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs);
	ImGui::PopStyleVar ();
	if (open) {
		HandleHelpButton ();
		OnDraw ();
	}
	ImGui::End ();
	if (!active) OnClose ();
}

bool Unit::Frame (Ctx &c)
{
	c.v = oapiGetFocusInterface ();
	if (!c.v) return false;
	c.hRef = c.v->GetSurfaceRef ();
	if (!c.hRef) return false;
	c.vname = c.v->GetName ();
	char n[256];
	oapiGetObjectName (c.hRef, n, sizeof n);
	c.body = n;
	c.R = oapiGetSize (c.hRef);
	c.b = lib.Get (c.hRef);
	double lng, lat, rad;
	c.v->GetEquPos (lng, lat, rad);
	c.pos = {lat, lng};
	c.alt = c.v->GetAltitude ();
	c.hdg = c.v->GetYaw ();
	c.ground = c.v->GroundContact ();
	VECTOR3 gv;
	if (c.v->GetGroundspeedVector (FRAME_HORIZON, gv)) c.gE = gv.x, c.gU = gv.y, c.gN = gv.z;
	auto it = plans.find (c.vname);
	c.plan = it != plans.end () && it->second.body == c.body ? &it->second : nullptr;
	c.g = Guide (c.plan ? *c.plan : Plan (), c.pos, c.R, c.gE, c.gN, c.gU);
	c.mjd = oapiGetSimMJD ();
	c.sys = oapiGetSysTime ();
	if (c.b != seen) { // another body: its lists
		seen = c.b;
		found.clear (), nrst.clear (), nrstFor = -1, rwySel = -1;
		if (ov != O_MENU) ov = O_NONE;
		if (cursor && group != G_VNAV) cursor = false;
	}
	return true;
}

void Unit::Step (double dt)
{
	VESSEL *v = oapiGetFocusInterface ();
	auto it = v ? plans.find (v->GetName ()) : plans.end ();
	char n[256] = "";
	if (v && v->GetSurfaceRef ()) oapiGetObjectName (v->GetSurfaceRef (), n, sizeof n);
	Plan *p = it != plans.end () && it->second.body == n && it->second.HasLeg () ? &it->second : nullptr;
	if (p) {
		double lng, lat, rad;
		v->GetEquPos (lng, lat, rad);
		if (Sequence (*p, {lat, lng}, oapiGetSize (v->GetSurfaceRef ()), v->GetGroundspeed ()) && p->active >= 0 && p->active < (int)p->wpts.size ())
			Msg ("Next leg to " + p->wpts[p->active].ident);
	}
	if (!gpss && !gpssPending.empty () && v && gpssPending == v->GetName ()) { // engaged when the scenario was saved
		gpssPending.clear ();
		if (p) GpssOn (v);
	}
	if (gpss) Steer (v, p, dt);
	if (oapiSetHUDBearingTarget) { // the HUD's heading-tape caret toward the active waypoint
		if (v && p && opt.marker) {
			const PlanWpt &w = p->direct ? p->dto : p->wpts[p->active];
			oapiSetHUDBearingTarget (v->GetHandle (), v->GetSurfaceRef (), w.pos.lng, w.pos.lat);
		} else oapiSetHUDBearingTarget (nullptr, nullptr, 0.0, 0.0);
	}
}

// GPS roll steering: bank the focus vessel onto the active leg with its ailerons (the permanent setting: the pilot's stick adds to it)
void Unit::Steer (VESSEL *v, const Plan *p, double dt)
{
	std::string why;
	if (!v || v->GetName () != gpssVessel) why = "the focus changed";
	else if (!p) why = "no active leg";
	else if (v->GroundContact ()) why = "on the ground";
	else if (v->GetDynPressure () < 200.0) why = "too little air for the ailerons";
	else if (oapiGetTimeAcceleration () > 4.0) why = "time acceleration";
	else if (v->GetGroundspeed () < 20.0) why = "too slow";
	if (!why.empty ()) { GpssOff ("GPSS off: " + why); return; }
	if (dt <= 0.0) return;
	OBJHANDLE h = v->GetSurfaceRef ();
	double lng, lat, rad;
	v->GetEquPos (lng, lat, rad);
	VECTOR3 gv;
	v->GetGroundspeedVector (FRAME_HORIZON, gv);
	Guidance g = Guide (*p, {lat, lng}, oapiGetSize (h), gv.x, gv.z, gv.y);
	double tgt = GpssBank (GpssTrack (g.dtk, g.xtk, g.gs), g.trk), step = 5.0 * gps::RAD * dt;
	bankTgt += std::clamp (tgt - bankTgt, -step, step); // a new leg's turn eased in
	double bank = v->GetBank (), rate = WrapPi (bank - bank0) / dt;
	bank0 = bank;
	ail = std::clamp (ail + (-WrapPi (bankTgt - bank) * 0.1 + rate * 0.3) * dt * 5.0, -1.0, 1.0); // the DG autopilot's bank loop (Script/DG/aap.lua)
	v->SetControlSurfaceLevel (AIRCTRL_AILERON, ail);
	if ((gpssLog += dt) >= 60.0) { // a line a minute in the log
		gpssLog = 0.0;
		oapiWriteLogV ("GPSNav: GPSS %s at %.4f %.4f, %.0f m, %.0f Pa, track %.0f, desired %.0f, cross-track %.0f m, bank %.1f", gpssVessel.c_str (), lat * gps::DEG, lng * gps::DEG,
			v->GetAltitude (), v->GetDynPressure (), g.trk * gps::DEG, g.dtk * gps::DEG, g.xtk, bank * gps::DEG);
	}
}

void Unit::GpssOn (VESSEL *v)
{
	gpss = true, gpssVessel = v->GetName ();
	ail = 0.0, bank0 = bankTgt = v->GetBank (), gpssLog = 0.0;
	double lng, lat, rad;
	v->GetEquPos (lng, lat, rad);
	oapiWriteLogV ("GPSNav: GPSS on %s at %.4f %.4f, heading %.0f", gpssVessel.c_str (), lat * gps::DEG, lng * gps::DEG, v->GetYaw () * gps::DEG);
	Msg ("GPSS on: steering the active leg");
}

void Unit::GpssOff (const std::string &why)
{
	if (!gpss) return;
	gpss = false;
	std::vector<char> name (gpssVessel.begin (), gpssVessel.end ());
	name.push_back (0);
	if (OBJHANDLE h = oapiGetVesselByName (name.data ()))
		if (VESSEL *v = oapiGetVesselInterface (h)) v->SetControlSurfaceLevel (AIRCTRL_AILERON, 0.0);
	oapiWriteLogV ("GPSNav: %s (%s)", why.c_str (), gpssVessel.c_str ());
	Msg (why);
}

Plan &Unit::PlanOf (Ctx &c)
{
	Plan &p = plans[c.vname];
	if (p.body != c.body) {
		if (!p.wpts.empty () || p.direct) Msg ("New flight plan on " + c.body);
		p = Plan ();
		p.body = c.body;
	}
	c.plan = &p;
	return p;
}

void Unit::Msg (const std::string &s)
{
	msg = s;
	msgTime = oapiGetSysTime ();
}

// ---------------------------------------------------------------------
// formatting

std::string Unit::Dist (double m) const
{
	double d = m / (opt.metric ? 1000.0 : NM);
	char b[32];
	if (d < 10.0) snprintf (b, sizeof b, "%.2f", d);
	else if (d < 100.0) snprintf (b, sizeof b, "%.1f", d);
	else if (d < 100000.0) snprintf (b, sizeof b, "%.0f", d);
	else snprintf (b, sizeof b, "%.0fk", d / 1000.0);
	return b;
}
const char *Unit::DistUnit () const { return opt.metric ? "km" : "nm"; }

std::string Unit::Speed (double ms) const
{
	char b[32];
	snprintf (b, sizeof b, "%.0f", opt.metric ? ms : ms / KT);
	return b;
}
const char *Unit::SpeedUnit () const { return opt.metric ? "m/s" : "kt"; }

std::string Unit::Alt (double m) const
{
	char b[32];
	double a = opt.metric ? m : m / FT;
	if (std::fabs (a) < 1e6) snprintf (b, sizeof b, "%.0f", a);
	else snprintf (b, sizeof b, "%.0fk", a / 1000.0);
	return b;
}
const char *Unit::AltUnit () const { return opt.metric ? "m" : "ft"; }

std::string Unit::Vs (double ms) const
{
	char b[32];
	if (opt.metric) snprintf (b, sizeof b, "%+.1f", ms);
	else snprintf (b, sizeof b, "%+.0f", ms / FT * 60.0);
	return b;
}
const char *Unit::VsUnit () const { return opt.metric ? "m/s" : "fpm"; }

std::string Unit::Deg (double rad)
{
	char b[16];
	int d = (int)std::lround (Wrap2Pi (rad) * gps::DEG);
	snprintf (b, sizeof b, "%03d\xc2\xb0", d == 0 ? 360 : d);
	return b;
}

std::string Unit::Time (double s)
{
	char b[32];
	if (s < 0.0) return "__:__";
	if (s < 3600.0) snprintf (b, sizeof b, "%02d:%02d", (int)(s / 60.0), (int)std::fmod (s, 60.0));
	else if (s < 100.0 * 3600.0) snprintf (b, sizeof b, "%d:%02d", (int)(s / 3600.0), (int)std::fmod (s / 60.0, 60.0));
	else snprintf (b, sizeof b, "%.0fh", s / 3600.0);
	return b;
}

static std::string Dm (double a, char pos, char neg, int w)
{
	char b[32];
	double d = std::fabs (a) * gps::DEG;
	int deg = (int)d;
	double min = (d - deg) * 60.0;
	if (min >= 59.995) deg++, min = 0.0;
	snprintf (b, sizeof b, "%c%0*d\xc2\xb0%05.2f'", a < 0.0 ? neg : pos, w, deg, min);
	return b;
}
std::string Unit::LatStr (double lat) { return Dm (lat, 'N', 'S', 2); }
std::string Unit::LngStr (double lng) { return Dm (lng, 'E', 'W', 3); }

std::string Unit::Freq (const Wpt &w)
{
	char b[32];
	if (w.freq <= 0.0f) return "";
	if (w.kind == Kind::Ndb) snprintf (b, sizeof b, "%.0f kHz", w.freq);
	else snprintf (b, sizeof b, "%.2f MHz", w.freq);
	return b;
}

void Unit::Field (int f, const Ctx &c, std::string &l, std::string &v, std::string &u) const
{
	const Guidance &g = c.g;
	l = FIELDS[f], v = "---", u.clear ();
	switch (f) {
	case F_GS: v = Speed (g.gs), u = SpeedUnit (); break;
	case F_DTK: if (g.leg) v = Deg (g.dtk); break;
	case F_TRK: if (g.moving) v = Deg (g.trk); break;
	case F_DIS: if (g.leg) v = Dist (g.dis), u = DistUnit (); break;
	case F_ETE: if (g.leg && g.ete >= 0.0) v = Time (g.ete); break;
	case F_BRG: if (g.leg) v = Deg (g.brg); break;
	case F_XTK: if (g.leg) v = Dist (std::fabs (g.xtk)) + (g.xtk >= 0.0 ? "R" : "L"), u = DistUnit (); break;
	case F_ETA:
		if (g.leg && g.ete >= 0.0) {
			double t = std::fmod (c.mjd + g.ete / 86400.0, 1.0) * 1440.0;
			char b[16];
			snprintf (b, sizeof b, "%02d:%02d", (int)(t / 60.0) % 24, (int)std::fmod (t, 60.0));
			v = b, u = "UTC";
		}
		break;
	case F_ALT: v = Alt (c.alt), u = AltUnit (); break;
	case F_VS: v = Vs (g.vs), u = VsUnit (); break;
	case F_WPT: if (g.leg) v = g.toIdent; break;
	}
}

// ---------------------------------------------------------------------
// the frame

void Unit::OnDraw ()
{
	ImGui::PushFont (ImGuiFont::MONO);
	F = ImGui::GetFont ();
	ImGuiIO &io = ImGui::GetIO ();
	ImVec2 st = ImGui::GetCursorStartPos (), wp = ImGui::GetWindowPos (), ws = ImGui::GetWindowSize (); // below the title bar (the help button moved the cursor)
	ImVec2 o (wp.x + st.x, wp.y + st.y), sz (std::max (ws.x - st.x, 50.0f), std::max (ws.y - st.y, 30.0f));
	ImGui::SetCursorScreenPos (o);
	ImGui::InvisibleButton ("unit", sz);
	bool hov = ImGui::IsItemHovered (), click = ImGui::IsItemClicked (ImGuiMouseButton_Left);
	float wheel = hov ? io.MouseWheel : 0.0f;
	ImDrawList *dl = ImGui::GetWindowDrawList ();
	float W = sz.x, H = sz.y;

	// layout
	sx0 = o.x + 0.035f * W, sy0 = o.y + 0.06f * H, sx1 = o.x + 0.735f * W, sy1 = o.y + 0.80f * H;
	float sh = sy1 - sy0;
	fs = 0.052f * sh;
	cy0 = sy0 + 0.095f * sh, cy1 = sy1 - 0.085f * sh;
	keys.clear (), rows.clear ();
	float kw = (sx1 - sx0) / 6.0f;
	for (int i = 0; i < 6; i++) keys.push_back ({sx0 + i * kw + 0.12f * kw, o.y + 0.845f * H, sx0 + (i + 1) * kw - 0.12f * kw, o.y + 0.915f * H, K_SOFT + i});
	float rx0 = o.x + 0.775f * W, rx1 = o.x + 0.965f * W, gap = 0.012f * W, bw = 0.5f * (rx1 - rx0 - gap);
	keys.push_back ({rx0 + 0.15f * (rx1 - rx0), o.y + 0.06f * H, rx1 - 0.15f * (rx1 - rx0), o.y + 0.135f * H, K_RNGUP});
	keys.push_back ({rx0 + 0.15f * (rx1 - rx0), o.y + 0.135f * H, rx1 - 0.15f * (rx1 - rx0), o.y + 0.21f * H, K_RNGDN});
	const int grid[3][2] = {{K_DTO, K_MENU}, {K_FPL, K_NRST}, {K_CLR, K_ENT}};
	for (int r = 0; r < 3; r++)
		for (int k = 0; k < 2; k++) {
			float y = o.y + (0.25f + 0.125f * r) * H;
			keys.push_back ({rx0 + k * (bw + gap), y, rx0 + k * (bw + gap) + bw, y + 0.095f * H, grid[r][k]});
		}
	kx = 0.5f * (rx0 + rx1), ky = o.y + 0.785f * H, kr0 = std::min (0.09f * W, 0.17f * H), kr1 = 0.55f * kr0;

	Ctx c;
	bool ok = Frame (c);

	DrawBezel (dl, o.x, o.y, W, H);
	dl->PushClipRect (ImVec2 (sx0, sy0), ImVec2 (sx1, sy1), true);
	dl->AddRectFilled (ImVec2 (sx0, sy0), ImVec2 (sx1, sy1), C_BLACK);
	if (!ok) Text (dl, 0.5f * (sx0 + sx1), 0.5f * (sy0 + sy1), fs, C_AMBER, "NO VESSEL", 1);
	else {
		dl->PushClipRect (ImVec2 (sx0, cy0), ImVec2 (sx1, cy1), true);
		if (!c.b) Text (dl, 0.5f * (sx0 + sx1), 0.5f * (cy0 + cy1) - fs, fs, C_CYAN, "LOADING DATABASE " + std::string ("|/-\\").substr ((int)(c.sys * 6.0) % 4, 1), 1);
		else switch (group) {
			case G_MAP: DrawMap (dl, c); break;
			case G_WPT: DrawWpt (dl, c); break;
			case G_NRST: DrawNrst (dl, c); break;
			case G_FPL: DrawFpl (dl, c); break;
			case G_VNAV: DrawVnav (dl, c); break;
		}
		if (c.b && (ov == O_DTO || ov == O_INSERT || ov == O_IDENT)) DrawEntry (dl, c);
		if (ov == O_MENU) DrawMenu (dl, c);
		// the page group: its name and its pages
		{
			std::string t = GROUP[group];
			float w = TextW (0.8f * fs, t) + (group == G_NRST ? 5 * 0.55f * fs : 0.0f) + 0.6f * fs;
			ImVec2 a (sx1 - w - 0.2f * fs, cy1 - 1.2f * fs), b (sx1 - 0.2f * fs, cy1 - 0.15f * fs);
			dl->AddRectFilled (a, b, IM_COL32 (0, 0, 0, 200));
			dl->AddRect (a, b, C_GREY);
			Text (dl, a.x + 0.3f * fs, a.y + 0.12f * fs, 0.8f * fs, C_CYAN, t);
			if (group == G_NRST)
				for (int i = 0; i < 5; i++) {
					ImVec2 p (b.x - 0.3f * fs - (4 - i) * 0.55f * fs - 0.2f * fs, 0.5f * (a.y + b.y));
					if (i == nrstPage) dl->AddCircleFilled (p, 0.18f * fs, C_CYAN);
					else dl->AddCircle (p, 0.18f * fs, C_GREY);
				}
		}
		if (c.sys - msgTime < 4.0 && !msg.empty ()) {
			float w = TextW (0.85f * fs, msg) + fs;
			ImVec2 a (0.5f * (sx0 + sx1) - 0.5f * w, cy1 - 2.6f * fs), b (0.5f * (sx0 + sx1) + 0.5f * w, cy1 - 1.5f * fs);
			dl->AddRectFilled (a, b, C_BLACK);
			dl->AddRect (a, b, C_AMBER);
			Text (dl, 0.5f * (sx0 + sx1), a.y + 0.1f * fs, 0.85f * fs, C_AMBER, msg, 1);
		}
		dl->PopClipRect ();
		DrawStrip (dl, c);
		DrawSoft (dl, c);
	}
	dl->PopClipRect ();

	// input
	if (ok) {
		ImVec2 m = io.MousePos;
		float dx = m.x - kx, dy = m.y - ky, dk = std::sqrt (dx * dx + dy * dy);
		auto in = [&] (const Hit &h) { return m.x >= h.x0 && m.x < h.x1 && m.y >= h.y0 && m.y < h.y1; };
		if (click) {
			int k = K_NONE;
			if (dk <= kr1) k = K_PUSH;
			else if (dk <= kr0) k = dx < 0.0f ? K_OUTL : K_OUTR;
			else for (const Hit &h : keys) if (in (h)) k = h.id;
			if (k != K_NONE) { lit = k, litTime = c.sys; Press (k, c); }
			else for (const Hit &h : rows) if (in (h)) { Row (h.id, c); break; }
		}
		if (wheel != 0.0f) {
			int d = wheel > 0.0f ? 1 : -1;
			if (dk <= kr1) Inner (d, c);
			else if (dk <= kr0) Outer (d, c);
			else if (in (keys[6]) || in (keys[7])) opt.range = std::clamp (opt.range - d, 0, NRANGE - 1);
			else if (m.x >= sx0 && m.x < sx1 && m.y >= cy0 && m.y < cy1) {
				if (ov == O_NONE && group == G_MAP) opt.range = std::clamp (opt.range - d, 0, NRANGE - 1);
				else if (ov == O_MENU) Inner (-d, c);
				else if (ov != O_NONE) fsel = std::clamp (fsel - d, 0, std::max (0, (int)found.size () - 1)); // the matches, not the letter
				else if (group == G_NRST || group == G_FPL) cursor = true, Inner (-d, c);
			}
		}
		if (ImGui::IsWindowFocused () && (ov == O_DTO || ov == O_INSERT || ov == O_IDENT)) {
			ImGui::SetNextFrameWantCaptureKeyboard (true); // the letters are the ident's, not Orbiter's keys
			bool changed = false;
			for (ImWchar ch : io.InputQueueCharacters) {
				if (ch >= 'a' && ch <= 'z') ch = (ImWchar)(ch - 32);
				if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == ' ')) continue;
				if (fresh) entry.clear (), epos = 0, fresh = false;
				epos = std::min (epos, (int)entry.size ());
				entry = entry.substr (0, epos) + (char)ch;
				epos++;
				changed = true;
			}
			if (ImGui::IsKeyPressed (ImGuiKey_Backspace) && fresh) entry.clear (), epos = 0, fresh = false, changed = true;
			else if (ImGui::IsKeyPressed (ImGuiKey_Backspace) && epos > 0) entry = entry.substr (0, --epos), changed = true;
			if (changed) Matches (c);
			if (ImGui::IsKeyPressed (ImGuiKey_DownArrow)) fsel = std::min (fsel + 1, std::max (0, (int)found.size () - 1));
			if (ImGui::IsKeyPressed (ImGuiKey_UpArrow)) fsel = std::max (fsel - 1, 0);
			if (ImGui::IsKeyPressed (ImGuiKey_Enter) || ImGui::IsKeyPressed (ImGuiKey_KeypadEnter)) Press (K_ENT, c);
			if (ImGui::IsKeyPressed (ImGuiKey_Escape)) ov = O_NONE;
		}
	}
	ImGui::PopFont ();
}

// ---------------------------------------------------------------------
// keys and knobs

void Unit::Press (int k, Ctx &c)
{
	bool entryOpen = ov == O_DTO || ov == O_INSERT || ov == O_IDENT;
	if (k >= K_SOFT && k < K_SOFT + 6) { Softkey (k - K_SOFT, c); return; }
	switch (k) {
	case K_RNGUP: opt.range = std::min (opt.range + 1, NRANGE - 1); break;
	case K_RNGDN: opt.range = std::max (opt.range - 1, 0); break;
	case K_OUTL: Outer (-1, c); break;
	case K_OUTR: Outer (1, c); break;
	case K_DTO:
		if (!c.b) { Msg ("Database loading"); break; }
		if (ov == O_DTO) { ov = O_NONE; break; }
		Open (O_DTO, c, Seed (c));
		break;
	case K_MENU: ov = ov == O_MENU ? O_NONE : O_MENU; break;
	case K_FPL: ov = O_NONE, cursor = false, group = group == G_FPL ? G_MAP : G_FPL, row = 0; break;
	case K_NRST: ov = O_NONE, cursor = false, group = group == G_NRST ? G_MAP : G_NRST, row = 0; break;
	case K_CLR:
		if (entryOpen && fresh) entry.clear (), epos = 0, fresh = false, Matches (c);
		else if (entryOpen && !entry.empty () && epos > 0) entry = entry.substr (0, --epos), Matches (c);
		else if (ov != O_NONE) ov = O_NONE;
		else if (cursor) cursor = false;
		else group = G_MAP;
		break;
	case K_PUSH:
		if (ov != O_NONE) break;
		if (group == G_WPT) { if (c.b) Open (O_IDENT, c, wptBody == c.b ? wptIdx : -1); }
		else if (group == G_NRST || group == G_FPL || (group == G_VNAV && altMode)) cursor = !cursor;
		break;
	case K_ENT:
		if (entryOpen) {
			if (found.empty () || !c.b) break;
			int i = found[std::clamp (fsel, 0, (int)found.size () - 1)];
			const Wpt &w = c.b->wpts[i];
			if (ov == O_DTO) { PlanOf (c).DirectTo (ToPlan (w), c.pos); Msg ("Direct to " + w.ident); cursor = false, rwySel = -1; }
			else if (ov == O_INSERT) {
				Plan &p = PlanOf (c);
				int at = std::clamp (row, 0, (int)p.wpts.size ());
				p.Insert (at, ToPlan (w));
				row = at + 1;
			} else wptBody = c.b, wptIdx = i;
			ov = O_NONE;
		} else if (ov == O_MENU) {
			if (menuSel == M_CANCEL) { if (c.plan && c.plan->direct) c.plan->direct = false, Msg ("Direct-To cancelled"); }
			else if (menuSel == M_CLEAR) { plans[c.vname] = Plan (), c.plan = nullptr; Msg ("Flight plan cleared"); }
			else if (menuSel == M_UNITS || menuSel == M_MAP || menuSel == M_COLOURS || menuSel == M_DOWNLOAD || menuSel == M_MARKER || menuSel == M_CDI) Outer (1, c);
			else ov = O_NONE;
		} else if (cursor && group == G_NRST && row < (int)nrst.size ()) {
			wptBody = c.b, wptIdx = nrst[row], group = G_WPT, cursor = false;
		} else if (cursor && group == G_FPL && c.plan) {
			if (row < (int)c.plan->wpts.size ()) c.plan->Activate (row), Msg ("Leg to " + c.plan->wpts[row].ident);
			else Open (O_INSERT, c, -1);
		} else if (cursor) cursor = false;
		break;
	}
}

void Unit::Outer (int d, Ctx &c)
{
	if (ov == O_DTO || ov == O_INSERT || ov == O_IDENT) { epos = std::clamp (epos + d, 0, (int)entry.size ()), fresh = false; return; }
	if (ov == O_MENU) {
		if (menuSel >= M_FIELD1 && menuSel <= M_FIELD5) opt.fields[menuSel - M_FIELD1] = (opt.fields[menuSel - M_FIELD1] + d + NFIELD) % NFIELD;
		switch (menuSel) {
		case M_UNITS: opt.metric = !opt.metric; break;
		case M_MAP: opt.trackUp = !opt.trackUp; break;
		case M_COLOURS: opt.mapMode = (opt.mapMode + d + 3) % 3; break;
		case M_DOWNLOAD: opt.mapDownload = !opt.mapDownload; break;
		case M_MARKER: opt.marker = !opt.marker; break;
		case M_CDI: opt.cdi = (opt.cdi + d + 4) % 4; break;
		case M_GP: opt.gpAngle = std::clamp (opt.gpAngle + d * 0.5 * gps::RAD, 1.0 * gps::RAD, 30.0 * gps::RAD); break;
		case M_TCH: opt.tch = std::clamp (opt.tch + d * (opt.metric ? 1.0 : 5.0 * FT), 0.0, 100.0); break;
		}
		return;
	}
	if (cursor && group == G_VNAV && altMode) { tgtAlt += d * (opt.metric ? 1000.0 : 1000.0 * FT); return; }
	cursor = false;
	group = (group + d + NGROUP) % NGROUP;
	row = 0;
}

void Unit::Inner (int d, Ctx &c)
{
	if (ov == O_DTO || ov == O_INSERT || ov == O_IDENT) {
		int n = (int)strlen (ALPHA);
		fresh = false;
		epos = std::min (epos, (int)entry.size ());
		char ch = epos < (int)entry.size () ? entry[epos] : ' ';
		const char *p = strchr (ALPHA, toupper ((unsigned char)ch));
		int k = ((p ? (int)(p - ALPHA) : 0) + d + n) % n;
		entry = entry.substr (0, epos);
		if (ALPHA[k] != ' ') entry += ALPHA[k];
		Matches (c);
		return;
	}
	if (ov == O_MENU) { menuSel = std::clamp (menuSel + d, 0, NMENU - 1); return; }
	if (cursor) {
		if (group == G_NRST) row = std::clamp (row + d, 0, std::max (0, (int)nrst.size () - 1));
		else if (group == G_FPL) row = std::clamp (row + d, 0, c.plan ? (int)c.plan->wpts.size () : 0);
		else if (group == G_VNAV && altMode) tgtAlt += d * (opt.metric ? 100.0 : 100.0 * FT);
		return;
	}
	if (group == G_MAP) opt.range = std::clamp (opt.range - d, 0, NRANGE - 1);
	else if (group == G_NRST) nrstPage = (nrstPage + d + 5) % 5, row = 0, nrstFor = -1;
}

void Unit::Row (int id, Ctx &c)
{
	if (id >= 2000) { menuSel = id - 2000; return; }
	if (id >= 1000) {
		if (fsel == id - 1000) Press (K_ENT, c);
		else fsel = id - 1000;
		return;
	}
	if (cursor && row == id) Press (K_ENT, c);
	else cursor = true, row = id;
}

int Unit::Seed (const Ctx &c) const
{
	if (!c.b) return -1;
	if (group == G_NRST && cursor && row < (int)nrst.size ()) return nrst[row];
	if (group == G_WPT && wptBody == c.b && wptIdx >= 0) return wptIdx;
	if (group == G_FPL && cursor && c.plan && row < (int)c.plan->wpts.size ()) return Lookup (c.b, c.plan->wpts[row]);
	if (c.plan && c.plan->HasLeg ()) return Lookup (c.b, c.plan->direct ? c.plan->dto : c.plan->wpts[c.plan->active]);
	return -1;
}

void Unit::Open (Overlay o, const Ctx &c, int seed)
{
	ov = o;
	if (seed >= (int)c.b->wpts.size ()) seed = -1;
	entry = seed >= 0 ? Upper (c.b->wpts[seed].ident) : std::string ();
	epos = 0, fresh = seed >= 0;
	Matches (c);
	if (seed >= 0) {
		auto it = std::find (found.begin (), found.end (), seed);
		if (it == found.end ()) found.insert (found.begin (), seed), fsel = 0;
		else fsel = (int)(it - found.begin ());
	}
}

void Unit::Matches (const Ctx &c)
{
	found = c.b ? c.b->Find (entry, c.pos, 40) : std::vector<int> ();
	fsel = 0;
}

void Unit::Tune (const Wpt &w, Ctx &c)
{
	int ch = (int)std::lround ((w.freq - 108.0) / 0.05);
	if (!w.tunable || ch < 0 || ch > 639) { Msg ("Not an Orbiter beacon"); return; }
	if (c.v->GetNavCount () < 1) { Msg ("This vessel has no NAV receiver"); return; }
	c.v->SetNavChannel (0, (DWORD)ch);
	char b[96];
	snprintf (b, sizeof b, "NAV1 %.2f %s", w.freq, w.ident.c_str ());
	Msg (b);
}

std::vector<std::string> Unit::SoftLabels (const Ctx &c) const
{
	if (ov == O_DTO || ov == O_INSERT || ov == O_IDENT) return {"PREV", "NEXT", "", "", "", "CANCEL"};
	if (ov == O_MENU) return {"", "", "", "", "", "EXIT"};
	switch (group) {
	case G_MAP: return {MAPMODE[opt.mapMode], opt.trackUp ? "TRK UP" : "NORTH UP", "DCLTR" + (opt.dcl ? "-" + std::to_string (opt.dcl) : std::string ()), "GPSS", "", ""};
	case G_WPT: {
		bool tune = c.b && wptBody == c.b && wptIdx >= 0 && c.b->wpts[wptIdx].tunable;
		return {"IDENT", tune ? "TUNE" : "", "ADD FPL", "", "", ""};
	}
	case G_NRST: return {NRST_PAGE[0], NRST_PAGE[1], NRST_PAGE[2], NRST_PAGE[3], NRST_PAGE[4], "INFO"};
	case G_FPL: return {"INSERT", "DELETE", "ACT LEG", "INVERT", "CLR FPL", ""};
	default: return {altMode ? "ALT" : "GP", altMode ? "" : "RWY", "", "", "", ""};
	}
}

void Unit::Softkey (int i, Ctx &c)
{
	if (SoftLabels (c)[i].empty ()) return;
	if (ov == O_DTO || ov == O_INSERT || ov == O_IDENT) {
		if (i == 0) fsel = std::max (fsel - 1, 0);
		else if (i == 1) fsel = std::min (fsel + 1, std::max (0, (int)found.size () - 1));
		else ov = O_NONE;
		return;
	}
	if (ov == O_MENU) { ov = O_NONE; return; }
	switch (group) {
	case G_MAP:
		if (i == 3) { if (gpss) GpssOff ("GPSS off"); else if (!c.g.leg) Msg ("GPSS needs an active leg"); else GpssOn (c.v); }
		else if (i == 0) opt.mapMode = (opt.mapMode + 1) % 3;
		else if (i == 1) opt.trackUp = !opt.trackUp;
		else if (i == 2) opt.dcl = (opt.dcl + 1) % 3;
		break;
	case G_WPT:
		if (!c.b) break;
		if (i == 0) Open (O_IDENT, c, wptBody == c.b ? wptIdx : -1);
		else if (wptBody == c.b && wptIdx >= 0) {
			const Wpt &w = c.b->wpts[wptIdx];
			if (i == 1) Tune (w, c);
			else if (i == 2) { Plan &p = PlanOf (c); p.Insert ((int)p.wpts.size (), ToPlan (w)); Msg (w.ident + " added to the flight plan"); }
		}
		break;
	case G_NRST:
		if (i < 5) nrstPage = i, row = 0, nrstFor = -1, cursor = false;
		else if (cursor && row < (int)nrst.size ()) wptBody = c.b, wptIdx = nrst[row], group = G_WPT, cursor = false;
		else Msg ("Select one first");
		break;
	case G_FPL: {
		if (i == 0) { // before the cursor's row, else at the end
			if (!c.b) break;
			if (!cursor) row = c.plan ? (int)c.plan->wpts.size () : 0;
			Open (O_INSERT, c, -1);
			break;
		}
		if (!c.plan) break;
		Plan &p = *c.plan;
		if (i == 1 && row < (int)p.wpts.size ()) p.Remove (row);
		else if (i == 2 && row < (int)p.wpts.size ()) p.Activate (row), Msg ("Leg to " + p.wpts[row].ident);
		else if (i == 3) p.Invert (), Msg ("Flight plan inverted");
		else if (i == 4) { p = Plan (); p.body = c.body; row = 0; Msg ("Flight plan cleared"); }
		break;
	}
	case G_VNAV:
		if (i == 0) altMode = !altMode, cursor = false;
		else if (i == 1) {
			Approach a = Target (c);
			rwySel = a.runway && rwySel + 1 < a.ends ? rwySel + 1 : -1; // past the last end: back to the best aligned
		}
		break;
	}
}

// the glide path's threshold: a runway end of the active waypoint (the one chosen, or the best aligned with the course), or the waypoint itself
Unit::Approach Unit::Target (const Ctx &c) const
{
	Approach a;
	if (!c.g.leg || !c.plan || !c.b) return a;
	const PlanWpt &pw = c.plan->direct ? c.plan->dto : c.plan->wpts[c.plan->active];
	int i = Lookup (c.b, pw);
	a.ok = true, a.id = pw.ident, a.thr = pw.pos, a.elev = pw.elev;
	a.far = Destination (pw.pos, Bearing (c.pos, pw.pos), 1000.0 / c.R);
	if (i < 0 || c.b->wpts[i].runways.empty ()) return a;
	struct End { LatLon thr, far; double elev; std::string id; };
	std::vector<End> ends;
	for (int r : c.b->wpts[i].runways) {
		const Runway &w = c.b->rwys[r];
		ends.push_back ({w.e1, w.e2, w.elev1, w.id1});
		ends.push_back ({w.e2, w.e1, w.elev2, w.id2});
	}
	int k = rwySel;
	if (k < 0 || k >= (int)ends.size ()) {
		double best = 1e9;
		for (int j = 0; j < (int)ends.size (); j++) {
			double off = std::fabs (WrapPi (Bearing (ends[j].thr, ends[j].far) - c.g.dtk));
			if (off < best) best = off, k = j;
		}
	}
	a.runway = true, a.ends = (int)ends.size (), a.thr = ends[k].thr, a.far = ends[k].far, a.elev = ends[k].elev, a.id = pw.ident + " RW" + ends[k].id;
	return a;
}

// ---------------------------------------------------------------------
// drawing

void Unit::DrawBezel (ImDrawList *dl, float x, float y, float w, float h)
{
	const ImU32 face = IM_COL32 (62, 64, 70, 255), faceLit = IM_COL32 (105, 108, 118, 255), edge = IM_COL32 (25, 25, 28, 255);
	dl->AddRectFilled (ImVec2 (x, y), ImVec2 (x + w, y + h), IM_COL32 (38, 40, 44, 255), 0.03f * h);
	dl->AddRect (ImVec2 (x + 1, y + 1), ImVec2 (x + w - 1, y + h - 1), IM_COL32 (78, 80, 88, 255), 0.03f * h, 0, 2.0f);
	dl->AddRectFilled (ImVec2 (sx0 - 0.008f * w, sy0 - 0.008f * w), ImVec2 (sx1 + 0.008f * w, sy1 + 0.008f * w), IM_COL32 (18, 18, 20, 255), 0.01f * w);
	double t = oapiGetSysTime ();
	float ts = 0.05f * h;
	for (const Hit &k : keys) {
		bool on = k.id == lit && t - litTime < 0.15;
		dl->AddRectFilled (ImVec2 (k.x0, k.y0), ImVec2 (k.x1, k.y1), on ? faceLit : face, 0.15f * (k.y1 - k.y0));
		dl->AddRect (ImVec2 (k.x0, k.y0), ImVec2 (k.x1, k.y1), edge, 0.15f * (k.y1 - k.y0), 0, 1.5f);
		dl->AddLine (ImVec2 (k.x0 + 0.2f * (k.x1 - k.x0), k.y0 + 2), ImVec2 (k.x1 - 0.2f * (k.x1 - k.x0), k.y0 + 2), IM_COL32 (120, 122, 130, 255));
		float mx = 0.5f * (k.x0 + k.x1), my = 0.5f * (k.y0 + k.y1);
		switch (k.id) {
		case K_DTO: { // a D crossed by an arrow
			float s = 0.6f * (k.y1 - k.y0);
			Text (dl, mx - 0.1f * s, my - 0.5f * s, s, C_WHITE, "D", 1);
			dl->AddLine (ImVec2 (mx - 0.55f * s, my), ImVec2 (mx + 0.45f * s, my), C_WHITE, 2.0f);
			dl->AddTriangleFilled (ImVec2 (mx + 0.65f * s, my), ImVec2 (mx + 0.4f * s, my - 0.16f * s), ImVec2 (mx + 0.4f * s, my + 0.16f * s), C_WHITE);
			break;
		}
		case K_MENU: Text (dl, mx, my - 0.5f * ts, ts, C_WHITE, "MENU", 1); break;
		case K_FPL: Text (dl, mx, my - 0.5f * ts, ts, C_WHITE, "FPL", 1); break;
		case K_NRST: Text (dl, mx, my - 0.5f * ts, ts, C_WHITE, "NRST", 1); break;
		case K_CLR: Text (dl, mx, my - 0.5f * ts, ts, C_WHITE, "CLR", 1); break;
		case K_ENT: Text (dl, mx, my - 0.5f * ts, ts, C_WHITE, "ENT", 1); break;
		case K_RNGUP: case K_RNGDN: {
			float s = 0.25f * (k.y1 - k.y0), d = k.id == K_RNGUP ? -1.0f : 1.0f;
			dl->AddTriangleFilled (ImVec2 (mx, my + d * s), ImVec2 (mx - s, my - d * s), ImVec2 (mx + s, my - d * s), C_WHITE);
			break;
		}
		}
	}
	Text (dl, 0.5f * (keys[6].x0 + keys[6].x1), keys[6].y0 - 0.85f * ts, 0.8f * ts, C_GREY, "RNG", 1);
	// the dual knob: a ridged outer ring, the inner knob, PUSH CRSR
	dl->AddCircleFilled (ImVec2 (kx, ky), kr0, IM_COL32 (30, 30, 33, 255), 48);
	for (int i = 0; i < 36; i++) {
		float a = i * 0.17453f;
		dl->AddLine (ImVec2 (kx + std::cos (a) * kr0 * 0.8f, ky + std::sin (a) * kr0 * 0.8f), ImVec2 (kx + std::cos (a) * kr0, ky + std::sin (a) * kr0), IM_COL32 (70, 70, 76, 255), 1.5f);
	}
	dl->AddCircle (ImVec2 (kx, ky), kr0, edge, 48, 2.0f);
	dl->AddCircleFilled (ImVec2 (kx, ky), kr1, IM_COL32 (56, 57, 62, 255), 40);
	for (int i = 0; i < 24; i++) {
		float a = i * 0.2618f;
		dl->AddLine (ImVec2 (kx + std::cos (a) * kr1 * 0.75f, ky + std::sin (a) * kr1 * 0.75f), ImVec2 (kx + std::cos (a) * kr1, ky + std::sin (a) * kr1), IM_COL32 (88, 90, 96, 255), 1.2f);
	}
	dl->AddCircle (ImVec2 (kx, ky), kr1, edge, 40, 1.5f);
	if (lit == K_PUSH && t - litTime < 0.15) dl->AddCircleFilled (ImVec2 (kx, ky), 0.7f * kr1, IM_COL32 (100, 102, 110, 255), 32);
	Text (dl, kx, ky - kr0 - 1.1f * ts, 0.8f * ts, C_GREY, "PUSH CRSR", 1);
	Text (dl, x + 0.035f * w, y + 0.94f * h, 0.8f * ts, C_GREY, "GPSNAV");
}

void Unit::DrawStrip (ImDrawList *dl, const Ctx &c)
{
	float h = cy0 - sy0, w = (sx1 - sx0) / 5.0f;
	dl->AddRectFilled (ImVec2 (sx0, sy0), ImVec2 (sx1, cy0), IM_COL32 (14, 14, 18, 255));
	dl->AddLine (ImVec2 (sx0, cy0), ImVec2 (sx1, cy0), C_GREY);
	for (int i = 0; i < 5; i++) {
		std::string l, v, u;
		int f = opt.fields[i];
		Field (f, c, l, v, u);
		float x = sx0 + i * w;
		if (i) dl->AddLine (ImVec2 (x, sy0 + 0.15f * h), ImVec2 (x, cy0 - 0.15f * h), C_GREY);
		bool nav = f != F_GS && f != F_TRK && f != F_ALT && f != F_VS;
		Text (dl, x + 0.12f * fs, sy0 + 0.08f * h, 0.62f * fs, C_WHITE, l);
		float uw = u.empty () ? 0.0f : TextW (0.6f * fs, u) + 0.1f * fs;
		Text (dl, x + w - 0.15f * fs - uw, cy0 - 1.12f * fs, fs, nav ? C_MAG : C_CYAN, Fit (v, fs, w - 1.6f * fs - uw), 2);
		if (!u.empty ()) Text (dl, x + w - 0.1f * fs - uw, cy0 - 0.75f * fs, 0.6f * fs, nav ? C_MAG : C_CYAN, u);
	}
}

void Unit::DrawSoft (ImDrawList *dl, const Ctx &c)
{
	std::vector<std::string> l = SoftLabels (c);
	float w = (sx1 - sx0) / 6.0f;
	dl->AddRectFilled (ImVec2 (sx0, cy1), ImVec2 (sx1, sy1), IM_COL32 (14, 14, 18, 255));
	for (int i = 0; i < 6; i++) {
		if (l[i].empty ()) continue;
		ImVec2 a (sx0 + i * w + 0.06f * w, cy1 + 0.12f * fs), b (sx0 + (i + 1) * w - 0.06f * w, sy1 - 0.12f * fs);
		bool on = (group == G_NRST && ov == O_NONE && i == nrstPage) || (group == G_VNAV && ov == O_NONE && i == 0) || (group == G_MAP && ov == O_NONE && i == 3 && gpss);
		dl->AddRectFilled (a, b, on ? C_SEL : IM_COL32 (45, 47, 55, 255), 3.0f);
		Text (dl, 0.5f * (a.x + b.x), 0.5f * (a.y + b.y) - 0.38f * fs, 0.76f * fs, on ? C_BLACK : C_WHITE, Fit (l[i], 0.76f * fs, b.x - a.x), 1);
	}
}

void Unit::DrawTitle (ImDrawList *dl, const char *title)
{
	dl->AddRectFilled (ImVec2 (sx0, cy0), ImVec2 (sx1, cy0 + 1.3f * fs), IM_COL32 (20, 30, 45, 255));
	Text (dl, 0.5f * (sx0 + sx1), cy0 + 0.15f * fs, fs, C_CYAN, title, 1);
}

void Unit::DrawMap (ImDrawList *dl, const Ctx &c)
{
	const Body &b = *c.b;
	float w = sx1 - sx0, h = cy1 - cy0;
	dl->AddRectFilled (ImVec2 (sx0, cy0), ImVec2 (sx1, cy1), C_MAPBG);
	ImVec2 own (sx0 + 0.5f * w, opt.trackUp ? cy0 + 0.68f * h : cy0 + 0.5f * h);
	double rangeM = RANGES[opt.range] * (opt.metric ? 1000.0 : NM), rnm = rangeM / NM;
	float rpx = own.y - cy0;
	double k = rpx / rangeM;  // pixels per metre
	double up = opt.trackUp ? (c.g.moving ? c.g.trk : c.hdg) : 0.0;
	double view = std::min (0.9 * gps::PI, std::hypot (w, h) / k / c.R); // the screen's reach, a central angle
	float u = 0.45f * fs;
	auto P = [&] (LatLon q, double &ang) {
		ang = Angle (c.pos, q);
		double br = Bearing (c.pos, q) - up, d = ang * c.R * k;
		return ImVec2 (own.x + (float)(std::sin (br) * d), own.y - (float)(std::cos (br) * d));
	};
	auto Poly = [&] (const std::vector<LatLon> &pts, ImU32 col, float th) { // a piece going out of view is kept up to its first point out
		std::vector<ImVec2> run;
		ImVec2 prev;
		double prevAng = 1e9;
		for (const LatLon &q : pts) {
			double ang;
			ImVec2 p = P (q, ang);
			if (ang > view) {
				if (!run.empty () && ang < 4.0 * view) run.push_back (p);
				if (run.size () > 1) dl->AddPolyline (run.data (), (int)run.size (), col, 0, th);
				run.clear ();
			} else {
				if (run.empty () && prevAng > view && prevAng < 4.0 * view) run.push_back (prev);
				run.push_back (p);
			}
			prev = p, prevAng = ang;
		}
		if (run.size () > 1) dl->AddPolyline (run.data (), (int)run.size (), col, 0, th);
	};
	auto Great = [&] (LatLon a, LatLon e, ImU32 col, float th) { // a great circle leg in pieces short against the view
		double ang = Angle (a, e), brg = Bearing (a, e);
		int n = std::clamp ((int)std::ceil (ang / std::min (gps::RAD, view / 6.0)), 1, 512);
		std::vector<LatLon> pts;
		for (int i = 0; i <= n; i++) pts.push_back (Destination (a, brg, ang * i / n));
		Poly (pts, col, th);
	};

	// the terrain's colours, coastlines or contours, else a grid of latitude and longitude
	bool relief = false, shore = false;
	if (opt.mapMode != 2) {
		Terrain::View tv {c.hRef, c.R, c.pos, up, k, own.x, own.y, sx0, cy0, sx1, cy1};
		tv.osm = b.coast && !strcasecmp (c.body.c_str (), "Earth"), tv.download = opt.mapDownload;
		relief = terrain.Draw (dl, tv, opt.mapMode == 1 && !c.ground ? Terrain::WARN : Terrain::TOPO, c.alt, b.coast, c.sys, C_COAST, &shore);
	}
	if (!(relief && shore)) // the map's shores, else the body's map lines
		for (const auto &l : b.lines) Poly (l, !relief ? C_LAND : b.coast ? C_COAST : IM_COL32 (60, 48, 38, 255), relief ? 1.2f : 1.4f);
	if (b.lines.empty () && !relief) {
		const double steps[] = {30, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01};
		double st = steps[0];
		for (double s : steps) if (s * gps::RAD * c.R * k >= 70.0) st = s;
		double span = view * gps::DEG, la0 = std::max (-90.0, c.pos.lat * gps::DEG - span), la1 = std::min (90.0, c.pos.lat * gps::DEG + span);
		double lspan = std::min (180.0, span / std::max (0.05, std::cos (c.pos.lat)));
		for (double la = std::ceil (la0 / st) * st; la <= la1; la += st) {
			std::vector<LatLon> pts;
			for (int i = 0; i <= 96; i++) pts.push_back ({la * gps::RAD, c.pos.lng + (-lspan + 2.0 * lspan * i / 96) * gps::RAD});
			Poly (pts, C_GRID, 1.0f);
		}
		for (double lo = std::ceil ((c.pos.lng * gps::DEG - lspan) / st) * st; lo <= c.pos.lng * gps::DEG + lspan; lo += st) {
			std::vector<LatLon> pts;
			for (int i = 0; i <= 96; i++) pts.push_back ({(la0 + (la1 - la0) * i / 96) * gps::RAD, lo * gps::RAD});
			Poly (pts, C_GRID, 1.0f);
		}
	}

	// what is in view
	std::vector<std::pair<int, int>> vis; // index, Show
	b.Near (c.pos, view, [&] (int i) { int s = Show (b.wpts[i], rnm, opt.dcl); if (s) vis.push_back ({i, s}); });
	// runways to scale
	if (rnm <= 15.0)
		for (auto &e : vis)
			for (int r : b.wpts[e.first].runways) {
				const Runway &rw = b.rwys[r];
				double a1, a2;
				ImVec2 p1 = P (rw.e1, a1), p2 = P (rw.e2, a2);
				if (a1 > view || a2 > view) continue;
				float dx = p2.x - p1.x, dy = p2.y - p1.y, len = std::sqrt (dx * dx + dy * dy);
				if (len < 1.0f) continue;
				float hw = std::max (1.5f, (float)(0.5 * std::max (rw.width, 20.0) * k)), nx = -dy / len * hw, ny = dx / len * hw;
				dl->AddQuadFilled (ImVec2 (p1.x + nx, p1.y + ny), ImVec2 (p2.x + nx, p2.y + ny), ImVec2 (p2.x - nx, p2.y - ny), ImVec2 (p1.x - nx, p1.y - ny), C_RWY);
				if (rnm <= 4.0) {
					float ox = dx / len * 1.2f * fs, oy = dy / len * 1.2f * fs;
					Text (dl, p1.x - ox, p1.y - oy - 0.35f * fs, 0.7f * fs, C_WHITE, rw.id1, 1);
					Text (dl, p2.x + ox, p2.y + oy - 0.35f * fs, 0.7f * fs, C_WHITE, rw.id2, 1);
				}
			}

	// the route: the flight plan white, its active leg and Direct-To magenta
	if (c.plan) {
		const Plan &p = *c.plan;
		for (int i = 1; i < (int)p.wpts.size (); i++)
			Great (p.wpts[i - 1].pos, p.wpts[i].pos, !p.direct && i == p.active ? C_MAG : C_WHITE, !p.direct && i == p.active ? 3.0f : 2.0f);
		if (p.direct) Great (p.dfrom, p.dto.pos, C_MAG, 3.0f);
		else if (p.active == 0 && !p.wpts.empty ()) Great (c.pos, p.wpts[0].pos, C_MAG, 3.0f);
	}

	// the symbols, then their idents
	for (auto &e : vis) {
		double ang;
		ImVec2 p = P (b.wpts[e.first].pos, ang);
		if (ang <= view) Symbol (dl, p, b.wpts[e.first], u);
	}
	for (auto &e : vis) {
		if (e.second < 2) continue;
		double ang;
		ImVec2 p = P (b.wpts[e.first].pos, ang);
		const Wpt &w = b.wpts[e.first];
		if (ang <= view) Text (dl, p.x + 1.2f * u, p.y - 0.2f * fs, 0.68f * fs, w.kind != Kind::Landmark ? C_WHITE : w.size ? C_CITY : C_MARK, w.ident);
	}
	if (c.plan) {
		const Plan &p = *c.plan;
		auto Mark = [&] (const PlanWpt &pw, bool act) {
			double ang;
			ImVec2 q = P (pw.pos, ang);
			if (ang > view) return;
			dl->AddQuadFilled (ImVec2 (q.x, q.y - 0.7f * u), ImVec2 (q.x + 0.7f * u, q.y), ImVec2 (q.x, q.y + 0.7f * u), ImVec2 (q.x - 0.7f * u, q.y), act ? C_MAG : C_WHITE);
			Text (dl, q.x + 1.0f * u, q.y + 0.2f * fs, 0.72f * fs, act ? C_MAG : C_WHITE, pw.ident);
		};
		for (int i = 0; i < (int)p.wpts.size (); i++) Mark (p.wpts[i], !p.direct && i == p.active);
		if (p.direct) Mark (p.dto, true);
	}

	// the track line, the range ring, own ship
	if (c.g.moving) {
		double a = c.g.trk - up;
		dl->AddLine (own, ImVec2 (own.x + (float)std::sin (a) * 2.0f * rpx, own.y - (float)std::cos (a) * 2.0f * rpx), IM_COL32 (220, 220, 220, 150), 1.2f);
	}
	dl->AddCircle (own, 0.5f * rpx, IM_COL32 (170, 170, 170, 110), 64, 1.0f);
	{
		std::string rr = Dist (0.5 * rangeM);
		while (rr.find ('.') != std::string::npos && (rr.back () == '0' || rr.back () == '.')) { bool dot = rr.back () == '.'; rr.pop_back (); if (dot) break; }
		Text (dl, own.x - 0.5f * rpx * 0.7071f - 0.3f * fs, own.y - 0.5f * rpx * 0.7071f - 0.9f * fs, 0.65f * fs, IM_COL32 (190, 190, 190, 255), rr, 2);
	}
	{
		double a = c.hdg - up;
		float s = 1.1f * fs, ca = (float)std::cos (a), sa = (float)std::sin (a);
		auto R2 = [&] (float x, float y) { return ImVec2 (own.x + x * ca - y * sa, own.y + x * sa + y * ca); };
		ImVec2 nose = R2 (0, -0.8f * s), rt = R2 (0.55f * s, 0.6f * s), notch = R2 (0, 0.3f * s), lt = R2 (-0.55f * s, 0.6f * s);
		dl->AddTriangleFilled (nose, rt, notch, C_WHITE);
		dl->AddTriangleFilled (nose, notch, lt, C_WHITE);
		ImVec2 out[4] = {nose, rt, notch, lt};
		dl->AddPolyline (out, 4, C_BLACK, ImDrawFlags_Closed, 1.0f);
	}

	// the leg, the range, the orientation
	float bh = 1.25f * fs;
	if (c.g.leg) {
		std::string from = c.plan->direct ? std::string () : c.plan->active > 0 ? c.plan->wpts[c.plan->active - 1].ident : std::string ();
		float x = sx0 + 0.3f * fs, tw = (from.empty () ? 0.0f : TextW (0.8f * fs, from) + 1.3f * fs) + TextW (0.8f * fs, c.g.toIdent) + (from.empty () ? 1.6f * fs : 0.0f);
		dl->AddRectFilled (ImVec2 (sx0, cy0), ImVec2 (x + tw + 0.4f * fs, cy0 + bh), IM_COL32 (0, 0, 0, 210));
		if (from.empty ()) {
			Text (dl, x, cy0 + 0.2f * fs, 0.8f * fs, C_MAG, "D"); // D crossed by an arrow, as on the key
			dl->AddLine (ImVec2 (x - 0.1f * fs, cy0 + 0.62f * fs), ImVec2 (x + 0.8f * fs, cy0 + 0.62f * fs), C_MAG, 1.5f);
			dl->AddTriangleFilled (ImVec2 (x + 1.05f * fs, cy0 + 0.62f * fs), ImVec2 (x + 0.75f * fs, cy0 + 0.45f * fs), ImVec2 (x + 0.75f * fs, cy0 + 0.79f * fs), C_MAG);
			x += 1.6f * fs;
		} else {
			Text (dl, x, cy0 + 0.2f * fs, 0.8f * fs, C_WHITE, from);
			x += TextW (0.8f * fs, from) + 0.3f * fs;
			dl->AddLine (ImVec2 (x, cy0 + 0.6f * fs), ImVec2 (x + 0.7f * fs, cy0 + 0.6f * fs), C_MAG, 2.0f);
			dl->AddTriangleFilled (ImVec2 (x + 0.95f * fs, cy0 + 0.6f * fs), ImVec2 (x + 0.65f * fs, cy0 + 0.4f * fs), ImVec2 (x + 0.65f * fs, cy0 + 0.8f * fs), C_MAG);
			x += 1.0f * fs;
		}
		Text (dl, x, cy0 + 0.2f * fs, 0.8f * fs, C_MAG, c.g.toIdent);
		if (gpss) { // annunciated as autopilot modes are, in green
			float gx = x + TextW (0.8f * fs, c.g.toIdent) + 0.8f * fs;
			dl->AddRectFilled (ImVec2 (gx - 0.25f * fs, cy0), ImVec2 (gx + TextW (0.8f * fs, "GPSS") + 0.25f * fs, cy0 + bh), IM_COL32 (0, 0, 0, 210));
			Text (dl, gx, cy0 + 0.2f * fs, 0.8f * fs, C_GREEN, "GPSS");
		}

		// the CDI at the bottom of the map: five dots a side, the needle where the course is, TO or FROM, the full scale
		double scale = CdiScale (opt.cdi, c.g.dis);
		float dw = 0.75f * fs, half = 5.5f * dw, cx = 0.5f * (sx0 + sx1), y = cy1 - 0.75f * fs;
		char sc[16];
		snprintf (sc, sizeof sc, "%g", scale / (opt.metric ? 1000.0 : NM));
		float lw = TextW (0.6f * fs, sc);
		dl->AddRectFilled (ImVec2 (cx - half - lw - 0.6f * fs, y - 0.65f * fs), ImVec2 (cx + half + 1.4f * fs, y + 0.62f * fs), IM_COL32 (0, 0, 0, 190), 3.0f);
		Text (dl, cx - half - 0.3f * fs, y - 0.32f * fs, 0.6f * fs, C_WHITE, sc, 2);
		for (int i = -5; i <= 5; i++) if (i) dl->AddCircle (ImVec2 (cx + i * dw, y), 0.17f * fs, C_WHITE, 10, 1.3f);
		dl->AddLine (ImVec2 (cx, y - 0.5f * fs), ImVec2 (cx, y + 0.5f * fs), C_WHITE, 1.5f);
		float nx = cx + (float)CdiNeedle (c.g.xtk, scale) * 5.0f * dw;
		dl->AddLine (ImVec2 (nx, y - 0.55f * fs), ImVec2 (nx, y + 0.55f * fs), C_MAG, 3.0f);
		float tx = cx + half + 0.7f * fs, ty = c.g.past ? 1.0f : -1.0f; // TO: up, FROM: down
		dl->AddTriangleFilled (ImVec2 (tx, y + ty * 0.45f * fs), ImVec2 (tx - 0.35f * fs, y - ty * 0.25f * fs), ImVec2 (tx + 0.35f * fs, y - ty * 0.25f * fs), C_WHITE);
	} else if (c.plan == nullptr && plans.count (c.vname) && (!plans.at (c.vname).wpts.empty () || plans.at (c.vname).direct))
		Text (dl, sx0 + 0.3f * fs, cy0 + 0.2f * fs, 0.75f * fs, C_AMBER, "PLAN ON " + Upper (plans.at (c.vname).body));
	{
		char rb[32];
		snprintf (rb, sizeof rb, "%g%s", RANGES[opt.range], DistUnit ());
		float tw = TextW (0.8f * fs, rb);
		dl->AddRectFilled (ImVec2 (sx0, cy1 - bh), ImVec2 (sx0 + tw + 0.6f * fs, cy1), IM_COL32 (0, 0, 0, 210));
		dl->AddRect (ImVec2 (sx0, cy1 - bh), ImVec2 (sx0 + tw + 0.6f * fs, cy1), C_GREY);
		Text (dl, sx0 + 0.3f * fs, cy1 - bh + 0.2f * fs, 0.8f * fs, C_CYAN, rb);
	}
	{
		const char *t = opt.trackUp ? "TRK UP" : "NORTH UP";
		float tw = TextW (0.7f * fs, t);
		dl->AddRectFilled (ImVec2 (sx1 - tw - 0.5f * fs, cy0), ImVec2 (sx1, cy0 + bh), IM_COL32 (0, 0, 0, 210));
		Text (dl, sx1 - 0.25f * fs, cy0 + 0.25f * fs, 0.7f * fs, C_WHITE, t, 2);
		if (opt.trackUp) { // a north pointer
			ImVec2 n (sx1 - 0.9f * fs, cy0 + 2.3f * fs);
			float s = 0.7f * fs, sa = (float)std::sin (-up), ca = (float)std::cos (-up);
			dl->AddTriangleFilled (ImVec2 (n.x + sa * s, n.y - ca * s), ImVec2 (n.x - ca * 0.3f * s, n.y - sa * 0.3f * s), ImVec2 (n.x + ca * 0.3f * s, n.y + sa * 0.3f * s), C_WHITE);
			Text (dl, n.x - sa * 1.5f * s, n.y + ca * 1.5f * s - 0.35f * fs, 0.7f * fs, C_WHITE, "N", 1);
		}
	}
	if (opt.mapMode == 1 && (c.ground || !relief)) Text (dl, 0.5f * (sx0 + sx1), cy0 + 1.6f * fs, 0.7f * fs, C_AMBER, c.ground ? "TERRAIN: ON GROUND" : "TERRAIN: NO DATA", 1);
	if (rnm <= 15.0 && opt.dcl == 0 && b.wpts.empty ()) Text (dl, 0.5f * (sx0 + sx1), cy1 - 2.5f * fs, 0.7f * fs, C_GREY, "NO DATABASE", 1);
}

void Unit::DrawWpt (ImDrawList *dl, const Ctx &c)
{
	const Body &b = *c.b;
	if (wptBody != c.b || wptIdx < 0 || wptIdx >= (int)b.wpts.size ()) { // the active waypoint, else the nearest airport
		wptBody = c.b;
		wptIdx = -1;
		wptIdx = Seed (c);
		if (wptIdx < 0) { std::vector<int> n = b.Nearest (c.pos, 1, WantApt); wptIdx = n.empty () ? -1 : n[0]; }
		if (wptIdx < 0) { std::vector<int> n = b.Nearest (c.pos, 1, WantBase); wptIdx = n.empty () ? -1 : n[0]; }
	}
	DrawTitle (dl, "WAYPOINT INFORMATION");
	if (wptIdx < 0) { Text (dl, 0.5f * (sx0 + sx1), cy0 + 3.0f * fs, fs, C_GREY, "NO WAYPOINT", 1); return; }
	const Wpt &w = b.wpts[wptIdx];
	float x = sx0 + 0.5f * fs, y = cy0 + 1.6f * fs, lh = 1.2f * fs, vx = sx0 + 5.5f * fs;
	Text (dl, x, y, 1.5f * fs, C_CYAN, w.ident);
	std::string kind = w.kind == Kind::Landmark && w.size ? "CITY" : Upper (KindName (w.kind)) + (w.kind != Kind::Airport ? "" : w.size == 2 ? " (LARGE)" : w.size == 1 ? " (MEDIUM)" : "");
	Text (dl, sx1 - 0.5f * fs, y + 0.1f * fs, 0.75f * fs, C_WHITE, kind, 2);
	Text (dl, sx1 - 0.5f * fs, y + 0.85f * fs, 0.65f * fs, C_GREY, w.source, 2);
	y += 1.8f * fs;
	if (!w.name.empty () && w.name != w.ident) { Text (dl, x, y, 0.85f * fs, C_WHITE, Fit (Upper (w.name), 0.85f * fs, sx1 - x - 0.5f * fs)); y += lh; }
	auto Line = [&] (const char *l, const std::string &v, ImU32 col) {
		Text (dl, x, y, 0.75f * fs, C_WHITE, l);
		Text (dl, vx, y - 0.05f * fs, 0.85f * fs, col, v);
		y += lh;
	};
	Line ("POSN", LatStr (w.pos.lat) + " " + LngStr (w.pos.lng), C_CYAN);
	Line ("ELEV", Alt (w.elev) + AltUnit (), C_CYAN);
	Line ("BRG", Deg (Bearing (c.pos, w.pos)) + "  DIS " + Dist (Angle (c.pos, w.pos) * c.R) + DistUnit (), C_CYAN);
	if (w.freq > 0.0f) Line ("FREQ", Freq (w) + (w.tunable ? "  NAV" : ""), w.tunable ? C_GREEN : C_CYAN);
	if (!w.runways.empty ()) {
		y += 0.2f * lh;
		Text (dl, x, y, 0.75f * fs, C_WHITE, "RUNWAYS");
		y += lh;
		for (int r : w.runways) {
			if (y > cy1 - 2.0f * lh) { Text (dl, x, y, 0.7f * fs, C_GREY, "..."); break; }
			const Runway &rw = b.rwys[r];
			char s[160];
			if (opt.metric) snprintf (s, sizeof s, "%s/%s %5.0fx%.0fm", rw.id1.c_str (), rw.id2.c_str (), rw.length, rw.width);
			else snprintf (s, sizeof s, "%s/%s %5.0fx%.0fft", rw.id1.c_str (), rw.id2.c_str (), rw.length / FT, rw.width / FT);
			std::string t = s;
			if (!rw.surface.empty ()) t += " " + Upper (rw.surface).substr (0, 6);
			if (rw.ils1 > 0.0f) { snprintf (s, sizeof s, "  ILS%s %.2f", rw.id1.c_str (), rw.ils1); t += s; }
			if (rw.ils2 > 0.0f) { snprintf (s, sizeof s, "  ILS%s %.2f", rw.id2.c_str (), rw.ils2); t += s; }
			Text (dl, x + 0.5f * fs, y, 0.72f * fs, C_CYAN, Fit (t, 0.72f * fs, sx1 - x - fs));
			y += 0.95f * lh;
		}
	}
}

void Unit::DrawNrst (ImDrawList *dl, const Ctx &c)
{
	const Body &b = *c.b;
	if (nrstFor != nrstPage || c.sys - nrstTime > 1.0) {
		int keep = cursor && row < (int)nrst.size () ? nrst[row] : -1;
		nrst = b.Nearest (c.pos, 25, WANT[nrstPage]);
		nrstFor = nrstPage, nrstTime = c.sys;
		if (keep >= 0) { auto it = std::find (nrst.begin (), nrst.end (), keep); if (it != nrst.end ()) row = (int)(it - nrst.begin ()); }
		row = std::clamp (row, 0, std::max (0, (int)nrst.size () - 1));
	}
	DrawTitle (dl, NRST_TITLE[nrstPage]);
	float y0 = cy0 + 1.5f * fs, rh = 1.15f * fs, w = sx1 - sx0;
	float xi = sx0 + 0.4f * fs, xn = sx0 + 0.24f * w, xb = sx0 + 0.70f * w, xd = sx1 - 0.4f * fs;
	Text (dl, xi, y0, 0.62f * fs, C_GREY, "IDENT");
	Text (dl, xn, y0, 0.62f * fs, C_GREY, nrstPage == 1 || nrstPage == 2 ? "FREQ" : nrstPage == 0 ? "RWY" : "NAME");
	Text (dl, xb, y0, 0.62f * fs, C_GREY, "BRG", 1);
	Text (dl, xd, y0, 0.62f * fs, C_GREY, "DIS", 2);
	y0 += 0.85f * fs;
	if (nrst.empty ()) { Text (dl, 0.5f * (sx0 + sx1), y0 + fs, 0.85f * fs, C_GREY, "NONE ON " + Upper (c.body), 1); return; }
	int n = std::max (1, (int)((cy1 - 1.4f * fs - y0) / rh));
	int first = cursor ? std::max (0, row - n + 1) : 0;
	for (int i = first; i < (int)nrst.size () && i < first + n; i++) {
		const Wpt &wp = b.wpts[nrst[i]];
		float y = y0 + (i - first) * rh;
		bool sel = cursor && i == row;
		if (sel) dl->AddRectFilled (ImVec2 (sx0 + 0.15f * fs, y - 0.05f * fs), ImVec2 (sx1 - 0.15f * fs, y + rh - 0.1f * fs), C_SEL);
		rows.push_back ({sx0, y - 0.05f * fs, sx1, y + rh - 0.1f * fs, i});
		std::string mid;
		if (nrstPage == 1 || nrstPage == 2) mid = Freq (wp) + (wp.tunable ? " NAV" : "");
		else if (nrstPage == 0) {
			double len = 0.0;
			for (int r : wp.runways) len = std::max (len, b.rwys[r].length);
			if (len > 0.0) mid = Alt (len) + AltUnit ();
		} else mid = Upper (wp.name);
		Text (dl, xi, y, 0.85f * fs, sel ? C_BLACK : C_CYAN, Fit (wp.ident, 0.85f * fs, xn - xi - 0.3f * fs));
		Text (dl, xn, y + 0.08f * fs, 0.72f * fs, sel ? C_BLACK : wp.tunable && nrstPage <= 2 ? C_GREEN : C_WHITE, Fit (mid, 0.72f * fs, xb - xn - 1.8f * fs));
		Text (dl, xb, y, 0.85f * fs, sel ? C_BLACK : C_WHITE, Deg (Bearing (c.pos, wp.pos)), 1);
		Text (dl, xd, y, 0.85f * fs, sel ? C_BLACK : C_WHITE, Dist (Angle (c.pos, wp.pos) * c.R) + DistUnit (), 2);
	}
}

void Unit::DrawFpl (ImDrawList *dl, const Ctx &c)
{
	DrawTitle (dl, "ACTIVE FLIGHT PLAN");
	float y = cy0 + 1.5f * fs, rh = 1.15f * fs, w = sx1 - sx0;
	auto it = plans.find (c.vname);
	if (!c.plan && it != plans.end () && (!it->second.wpts.empty () || it->second.direct)) {
		Text (dl, 0.5f * (sx0 + sx1), y + fs, 0.8f * fs, C_AMBER, "THE FLIGHT PLAN IS ON " + Upper (it->second.body), 1);
		Text (dl, 0.5f * (sx0 + sx1), y + 2.3f * fs, 0.7f * fs, C_GREY, "an edit starts one here", 1);
		return;
	}
	if (c.plan && c.plan->direct) {
		Text (dl, sx0 + 0.4f * fs, y, 0.75f * fs, C_MAG, "DIRECT TO " + c.plan->dto.ident);
		y += rh;
	}
	float xi = sx0 + 1.4f * fs, xt = sx0 + 0.45f * w, xd = sx0 + 0.72f * w, xe = sx1 - 0.4f * fs;
	Text (dl, xi, y, 0.62f * fs, C_GREY, "WAYPOINT");
	Text (dl, xt, y, 0.62f * fs, C_GREY, "DTK", 1);
	Text (dl, xd, y, 0.62f * fs, C_GREY, "DIS", 2);
	Text (dl, xe, y, 0.62f * fs, C_GREY, "ETE", 2);
	y += 0.85f * fs;
	const std::vector<PlanWpt> none;
	const std::vector<PlanWpt> &wp = c.plan ? c.plan->wpts : none;
	int act = c.plan && !c.plan->direct ? c.plan->active : -1;
	int n = std::max (1, (int)((cy1 - 1.4f * fs - y) / rh)), total = (int)wp.size () + 1;
	int first = std::clamp (cursor ? row - n + 1 : act - 1, 0, std::max (0, total - n));
	for (int i = first; i < total && i < first + n; i++) {
		float ry = y + (i - first) * rh;
		bool sel = cursor && i == row;
		if (sel) dl->AddRectFilled (ImVec2 (sx0 + 0.15f * fs, ry - 0.05f * fs), ImVec2 (sx1 - 0.15f * fs, ry + rh - 0.1f * fs), C_SEL);
		rows.push_back ({sx0, ry - 0.05f * fs, sx1, ry + rh - 0.1f * fs, i});
		if (i == (int)wp.size ()) { Text (dl, xi, ry, 0.85f * fs, sel ? C_BLACK : C_GREY, "_____"); continue; }
		bool a = i == act;
		ImU32 col = sel ? C_BLACK : a ? C_MAG : act >= 0 && i < act - 1 ? C_GREY : C_WHITE;
		if (a) { // the active leg's arrow
			float ax = sx0 + 0.45f * fs, ay = ry + 0.45f * fs;
			dl->AddLine (ImVec2 (ax, ay - 0.9f * rh), ImVec2 (ax, ay), sel ? C_BLACK : C_MAG, 2.0f);
			dl->AddTriangleFilled (ImVec2 (ax + 0.65f * fs, ay), ImVec2 (ax, ay - 0.3f * fs), ImVec2 (ax, ay + 0.3f * fs), sel ? C_BLACK : C_MAG);
		}
		Text (dl, xi, ry, 0.85f * fs, col, Fit (wp[i].ident, 0.85f * fs, xt - xi - 2.0f * fs));
		if (i == 0) continue;
		double dtk = Bearing (wp[i - 1].pos, wp[i].pos), dis = Angle (wp[i - 1].pos, wp[i].pos) * c.R;
		if (a && c.g.leg) dtk = c.g.dtk, dis = c.g.dis;
		Text (dl, xt, ry, 0.85f * fs, col, Deg (dtk), 1);
		Text (dl, xd, ry, 0.85f * fs, col, Dist (dis), 2);
		Text (dl, xe, ry, 0.85f * fs, col, c.g.moving ? Time (dis / c.g.gs) : "__:__", 2);
	}
}

void Unit::DrawVnav (ImDrawList *dl, const Ctx &c)
{
	DrawTitle (dl, altMode ? "VNAV: TARGET ALTITUDE" : "VNAV: GLIDE PATH");
	float x = sx0 + 0.5f * fs, vx = sx0 + 7.0f * fs, y = cy0 + 1.7f * fs, lh = 1.2f * fs;
	auto Line = [&] (const char *l, const std::string &v, ImU32 col) {
		Text (dl, x, y, 0.72f * fs, C_WHITE, l);
		Text (dl, vx, y - 0.08f * fs, 0.9f * fs, col, v);
		y += lh;
	};
	const Guidance &g = c.g;
	if (altMode) {
		std::string at = g.leg ? " AT " + g.toIdent : "";
		Text (dl, x, y, 0.72f * fs, C_WHITE, "TGT ALT");
		if (cursor) dl->AddRectFilled (ImVec2 (vx - 0.2f * fs, y - 0.15f * fs), ImVec2 (vx + TextW (0.9f * fs, Alt (tgtAlt) + AltUnit ()) + 0.2f * fs, y + 0.95f * fs), C_SEL);
		Text (dl, vx, y - 0.08f * fs, 0.9f * fs, cursor ? C_BLACK : C_CYAN, Alt (tgtAlt) + AltUnit ());
		Text (dl, vx + TextW (0.9f * fs, Alt (tgtAlt) + AltUnit ()) + 0.3f * fs, y, 0.72f * fs, C_MAG, at);
		y += lh;
		Line ("ALT", Alt (c.alt) + AltUnit (), C_CYAN);
		Line ("DIS", g.leg ? Dist (g.dis) + DistUnit () : "---", C_MAG);
		Line ("VS REQ", g.leg && g.moving ? Vs (RequiredVs (c.alt, tgtAlt, g.dis, g.gs)) + VsUnit () : "---", C_MAG);
		Line ("VS", Vs (g.vs) + VsUnit (), C_CYAN);
		if (!cursor) Text (dl, x, y + 0.3f * fs, 0.65f * fs, C_GREY, "PUSH CRSR, then the knobs: the altitude");
		return;
	}
	Approach a = Target (c);
	if (!a.ok) { Text (dl, 0.5f * (sx0 + sx1), y + fs, 0.8f * fs, C_GREY, "NO ACTIVE WAYPOINT", 1); return; }
	double dis = Angle (c.pos, a.thr) * c.R, hat = c.alt - a.elev;
	double along = AlongTrack (a.thr, a.far, c.pos) * c.R; // negative before the threshold
	double gpH = PathHeight (dis, opt.gpAngle, opt.tch), dev = hat - gpH;
	char gp[48];
	snprintf (gp, sizeof gp, "%.1f\xc2\xb0  TCH %s%s", opt.gpAngle * gps::DEG, Alt (opt.tch).c_str (), AltUnit ());
	Line ("TARGET", a.id, C_MAG);
	Line ("PATH", gp, C_CYAN);
	Line ("THR ELEV", Alt (a.elev) + AltUnit (), C_CYAN);
	Line ("DIS THR", Dist (dis) + DistUnit (), C_MAG);
	Line ("HT ABOVE", Alt (hat) + AltUnit (), C_CYAN);
	Line ("PATH HT", Alt (gpH) + AltUnit (), C_CYAN);
	if (along > 0.0) { Line ("DEV", "PAST THRESHOLD", C_AMBER); return; }
	Line ("DEV", Alt (std::fabs (dev)) + AltUnit () + (dev >= 0.0 ? " HIGH" : " LOW"), std::fabs (dev) > std::max (30.0, 0.02 * dis) ? C_AMBER : C_GREEN);
	Line ("VS REQ", g.moving ? Vs (PathVs (g.gs, opt.gpAngle)) + VsUnit () : "---", C_MAG);
	Line ("VS", Vs (g.vs) + VsUnit (), C_CYAN);

	// the deviation scales: vertical (a dot is 0.35 degrees) and, on a runway, the centreline's (a dot is 1.25 degrees)
	float sx = sx1 - 2.0f * fs, sy = 0.5f * (cy0 + cy1) + 0.4f * fs, dot = 0.13f * (cy1 - cy0);
	dl->AddRectFilled (ImVec2 (sx - 0.8f * fs, sy - 2.8f * dot), ImVec2 (sx + 0.8f * fs, sy + 2.8f * dot), IM_COL32 (25, 25, 30, 255), 3.0f);
	for (int i = -2; i <= 2; i++) if (i) dl->AddCircle (ImVec2 (sx, sy + i * dot), 0.18f * fs, C_WHITE, 12, 1.5f);
	dl->AddLine (ImVec2 (sx - 0.7f * fs, sy), ImVec2 (sx + 0.7f * fs, sy), C_WHITE, 2.0f);
	double vd = (std::atan2 (hat - opt.tch, std::max (dis, 1.0)) - opt.gpAngle) / (0.35 * gps::RAD);
	float vy = sy + (float)std::clamp (vd, -2.4, 2.4) * dot; // the path below when high
	dl->AddQuadFilled (ImVec2 (sx, vy - 0.45f * fs), ImVec2 (sx + 0.35f * fs, vy), ImVec2 (sx, vy + 0.45f * fs), ImVec2 (sx - 0.35f * fs, vy), C_MAG);
	Text (dl, sx, sy - 2.8f * dot - 1.0f * fs, 0.6f * fs, C_WHITE, "V", 1);
	if (a.runway) {
		float hx = 0.5f * (sx0 + sx1) + 2.5f * fs, hy = cy1 - 2.4f * fs, hd = 0.11f * (sx1 - sx0);
		dl->AddRectFilled (ImVec2 (hx - 2.8f * hd, hy - 0.7f * fs), ImVec2 (hx + 2.8f * hd, hy + 0.7f * fs), IM_COL32 (25, 25, 30, 255), 3.0f);
		for (int i = -2; i <= 2; i++) if (i) dl->AddCircle (ImVec2 (hx + i * hd, hy), 0.18f * fs, C_WHITE, 12, 1.5f);
		double xt = CrossTrack (a.far, a.thr, c.pos) * c.R; // right of the inbound course
		double ld = std::atan2 (xt, std::max (dis, 1.0)) / (1.25 * gps::RAD);
		float lx = hx - (float)std::clamp (ld, -2.4, 2.4) * hd; // the centreline left when right of it
		dl->AddLine (ImVec2 (lx, hy - 0.6f * fs), ImVec2 (lx, hy + 0.6f * fs), C_MAG, 3.0f);
		dl->AddLine (ImVec2 (hx, hy - 0.7f * fs), ImVec2 (hx, hy - 0.45f * fs), C_WHITE, 2.0f);
		Text (dl, hx - 2.8f * hd - 0.4f * fs, hy - 0.35f * fs, 0.6f * fs, C_WHITE, "LOC", 2);
	}
}

void Unit::DrawEntry (ImDrawList *dl, const Ctx &c)
{
	const Body &b = *c.b;
	ImVec2 a (sx0 + 0.4f * fs, cy0 + 0.3f * fs), e (sx1 - 0.4f * fs, cy1 - 0.3f * fs);
	dl->AddRectFilled (a, e, IM_COL32 (8, 12, 22, 250), 4.0f);
	dl->AddRect (a, e, C_CYAN, 4.0f, 0, 1.5f);
	Text (dl, 0.5f * (a.x + e.x), a.y + 0.2f * fs, 0.9f * fs, C_CYAN, ov == O_DTO ? "DIRECT TO" : ov == O_INSERT ? "INSERT WAYPOINT" : "FIND WAYPOINT", 1);
	// the ident: a box per character, the one the inner knob changes lit
	float cw = 1.0f * fs, x = a.x + 0.8f * fs, y = a.y + 1.4f * fs;
	int n = std::max ((int)entry.size () + 1, 6);
	bool blink = std::fmod (c.sys, 0.8) < 0.5;
	for (int i = 0; i < n && x + cw < e.x - fs; i++, x += cw * 1.1f) {
		bool at = i == epos;
		dl->AddRectFilled (ImVec2 (x, y), ImVec2 (x + cw, y + 1.4f * fs), at && blink ? C_SEL : IM_COL32 (30, 34, 45, 255), 2.0f);
		if (i < (int)entry.size ()) Text (dl, x + 0.5f * cw, y + 0.15f * fs, 1.1f * fs, at && blink ? C_BLACK : C_CYAN, std::string (1, entry[i]), 1);
	}
	y += 1.8f * fs;
	Text (dl, a.x + 0.8f * fs, y, 0.6f * fs, C_GREY, "type, or the knobs: inner the letter, outer the place");
	y += 1.0f * fs;
	float rh = 1.05f * fs;
	int rowsFit = std::max (1, (int)((e.y - 0.3f * fs - y) / rh));
	int first = std::max (0, fsel - rowsFit + 1);
	if (found.empty () && !entry.empty ()) Text (dl, 0.5f * (a.x + e.x), y + 0.5f * fs, 0.8f * fs, C_AMBER, "NO MATCH ON " + Upper (c.body), 1);
	for (int i = first; i < (int)found.size () && i < first + rowsFit; i++) {
		const Wpt &w = b.wpts[found[i]];
		float ry = y + (i - first) * rh;
		bool sel = i == fsel;
		if (sel) dl->AddRectFilled (ImVec2 (a.x + 0.3f * fs, ry - 0.05f * fs), ImVec2 (e.x - 0.3f * fs, ry + rh - 0.1f * fs), C_SEL);
		rows.push_back ({a.x, ry - 0.05f * fs, e.x, ry + rh - 0.1f * fs, 1000 + i});
		float xi = a.x + 0.6f * fs, xk = a.x + 0.27f * (e.x - a.x), xn = a.x + 0.43f * (e.x - a.x);
		Text (dl, xi, ry, 0.8f * fs, sel ? C_BLACK : C_CYAN, Fit (w.ident, 0.8f * fs, xk - xi - 0.3f * fs));
		Text (dl, xk, ry + 0.08f * fs, 0.65f * fs, sel ? C_BLACK : C_WHITE, ShortKind (w));
		std::string d = Dist (Angle (c.pos, w.pos) * c.R) + DistUnit ();
		Text (dl, e.x - 0.6f * fs, ry + 0.05f * fs, 0.7f * fs, sel ? C_BLACK : C_WHITE, d, 2);
		if (w.name != w.ident) Text (dl, xn, ry + 0.08f * fs, 0.65f * fs, sel ? C_BLACK : C_GREY, Fit (Upper (w.name), 0.65f * fs, e.x - xn - TextW (0.7f * fs, d) - 1.2f * fs));
	}
}

void Unit::DrawMenu (ImDrawList *dl, const Ctx &c)
{
	ImVec2 a (sx0 + 1.5f * fs, cy0 + 0.3f * fs), e (sx1 - 1.5f * fs, cy1 - 0.3f * fs);
	dl->AddRectFilled (a, e, IM_COL32 (8, 12, 22, 250), 4.0f);
	dl->AddRect (a, e, C_CYAN, 4.0f, 0, 1.5f);
	Text (dl, 0.5f * (a.x + e.x), a.y + 0.2f * fs, 0.9f * fs, C_CYAN, "MENU", 1);
	char gp[32], tc[32];
	snprintf (gp, sizeof gp, "%.1f\xc2\xb0", opt.gpAngle * gps::DEG);
	snprintf (tc, sizeof tc, "%s%s", Alt (opt.tch).c_str (), AltUnit ());
	std::string items[NMENU][2] = {
		{"Units", opt.metric ? "KM M/S M" : "NM KT FT"},
		{"Data field 1", FIELDS[opt.fields[0]]}, {"Data field 2", FIELDS[opt.fields[1]]}, {"Data field 3", FIELDS[opt.fields[2]]},
		{"Data field 4", FIELDS[opt.fields[3]]}, {"Data field 5", FIELDS[opt.fields[4]]},
		{"Map", opt.trackUp ? "TRACK UP" : "NORTH UP"}, {"Map colours", MAPMODE[opt.mapMode]}, {"Map download", opt.mapDownload ? "ON" : "OFF"}, {"HUD caret", opt.marker ? "ON" : "OFF"}, {"CDI scale", CDISCALE[opt.cdi]},
		{"Glide path", gp}, {"Crossing height", tc},
		{"Cancel Direct-To", ""}, {"Clear flight plan", ""}};
	float y = a.y + 1.4f * fs, rh = std::min (1.1f * fs, (e.y - 1.6f * fs - y) / (float)NMENU);
	for (int i = 0; i < NMENU; i++, y += rh) {
		bool sel = i == menuSel;
		if (sel) dl->AddRectFilled (ImVec2 (a.x + 0.3f * fs, y - 0.05f * fs), ImVec2 (e.x - 0.3f * fs, y + rh - 0.05f * fs), C_SEL);
		rows.push_back ({a.x, y - 0.05f * fs, e.x, y + rh - 0.05f * fs, 2000 + i});
		Text (dl, a.x + 0.7f * fs, y, 0.75f * fs, sel ? C_BLACK : C_WHITE, items[i][0]);
		Text (dl, e.x - 0.7f * fs, y, 0.75f * fs, sel ? C_BLACK : C_CYAN, items[i][1], 2);
	}
	Text (dl, 0.5f * (a.x + e.x), e.y - 1.1f * fs, 0.6f * fs, C_GREY, "inner knob: the line, outer: its value, ENT: do it", 1);
}
