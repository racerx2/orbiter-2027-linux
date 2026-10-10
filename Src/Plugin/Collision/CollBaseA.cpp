// not upstream: collision addon, planet and base cfg scan, matching, placement and building colliders (design E2 10.3, 10.4)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include "CollBaseA.h"
#include "DentMath.h"

namespace {
const int OBJTP_PLANET_ = 4; // OBJTP_PLANET (OrbiterAPI.h:1773)

struct Cand { std::string file, ctx; CollBaseFile f; }; // ctx: CONTEXT value of the file's DIR line, "" without one

bool EndsCfg (const std::string &s) { return s.size () > 4 && s.compare (s.size () - 4, 4, ".cfg") == 0; }

// trim_string (Config.cpp:290-311): cut the ';' comment, strip trailing white space and CR, skip leading white space
char *Trim (char *s)
{
	size_t n = strcspn (s, ";");
	s[n] = '\0';
	while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) s[--n] = '\0';
	return s + strspn (s, " \t");
}

// the header test of Planet::ScanBases (Planet.cpp:504-508): first non-empty trimmed line, lines up to 255 characters, 9-character prefix in any case
bool BaseHeader (const std::string &text)
{
	std::istringstream is (text);
	char cbuf[256], *pc;
	do {
		if (!is.getline (cbuf, 256)) return false;
		pc = Trim (cbuf);
	} while (!pc[0]);
	return !strncasecmp (pc, "BASE-V2.0", 9);
}

// AddBase (Planet.cpp:520-523) drops a name already taken, in any case; a CONTEXT dir may be off in the core, so it never shadows a dir without CONTEXT
bool Taken (const std::vector<Cand> &out, const Cand &c)
{
	for (auto &o : out)
		if (!strcasecmp (o.f.name.c_str (), c.f.name.c_str ()) && (o.ctx.empty () || !strcasecmp (o.ctx.c_str (), c.ctx.c_str ()))) return true;
	return false;
}

// Planet::ScanBases (Planet.cpp:465-518): PERIOD and CONTEXT limiters of the DIR value, then the .cfg files of the dir; path is edited in place
void ScanBases (CollSdk &sdk, const CollDirs &d, char *path, std::vector<Cand> &out, bool &ctxLogged)
{
	char cbuf[256], *pc, *cut = nullptr;
	std::string ctx;
	if ((pc = strstr (path, "PERIOD")) != nullptr) {
		if (sscanf (pc + 6, "%127s%127s", cbuf, cbuf + 128) == 2) {
			double dt, ref = sdk.RefMJD ();
			if (sscanf (cbuf, "%lf", &dt) == 1 && dt > ref) return;
			if (sscanf (cbuf + 128, "%lf", &dt) == 1 && dt < ref) return;
		}
		cut = pc;
	}
	if ((pc = strstr (path, "CONTEXT")) != nullptr) {
		if (sscanf (pc + 7, "%255s", cbuf) == 1) ctx = cbuf; // the SDK has no scenario context: kept as low-priority candidates
		cut = cut ? std::min (cut, pc) : pc;
	}
	if (cut) {
		*cut = '\0';
		Trim (path);
	}
	if (!ctx.empty () && !ctxLogged) {
		ctxLogged = true;
		sdk.Log (1, ("Collision bases: DIR " + std::string (path) + " has CONTEXT " + ctx + ", which the SDK does not expose: its bases match by location or when no other cfg has the name").c_str ());
	}
	std::string dir = CollCfgPath (d, path, "");
	for (auto &f : sdk.ListDir (sdk.Resolve (dir))) {
		if (!EndsCfg (f)) continue;
		std::string text;
		if (!sdk.ReadText (sdk.Resolve (dir + "\\" + f), text) || !BaseHeader (text)) continue;
		Cand c; c.file = dir + "\\" + f; c.ctx = ctx;
		std::vector<std::string> warn;
		CollParseBaseFile (text, c.f, warn);
		if (!Taken (out, c)) out.push_back (std::move (c));
	}
}
} // namespace

void CollBaseA::Build (CollSdk &sdk, const CollDirs &d, bool geometry)
{
	rec.clear (); nObjects = nIncluded = 0;
	bool ctxLogged = false;
	for (uint32_t pi = 0, np = sdk.GbodyCount (); pi < np; pi++) {
		CollH hp = sdk.Gbody (pi);
		if (sdk.ObjType (hp) != OBJTP_PLANET_ || !sdk.BaseCount (hp)) continue;
		std::string pname = sdk.Name (hp), ptext;
		std::vector<Cand> cand;
		if (!sdk.ReadText (sdk.Resolve (CollCfgPath (d, pname, ".cfg")), ptext)) {
			sdk.Log (1, ("Collision bases: no cfg for planet " + pname + ", no buildings there").c_str ());
			continue;
		}
		std::istringstream is (ptext);
		char cbuf[1024];
		bool surf = false;
		for (;;) { // FindLine (Config.cpp:425-447): 1024 buffer, heal after a long line, prefix match at column 0
			if (!is.getline (cbuf, 1024)) {
				if (is.eof ()) break;
				is.clear ();
			}
			if (!strncasecmp (cbuf, "BEGIN_SURFBASE", 14)) { surf = true; break; }
		}
		if (surf) {
			for (;;) { // Planet.cpp:347-364: lines up to 255 characters, END_SURFBASE at column 0
				if (!is.getline (cbuf, 256) || !strncasecmp (cbuf, "END_SURFBASE", 12)) break;
				char *pc = Trim (cbuf);
				if (!pc[0]) continue;
				if (!strncasecmp (pc, "DIR", 3)) { ScanBases (sdk, d, Trim (pc + 3), cand, ctxLogged); continue; }
				char *nm = strtok (pc, ":"), *ps = strtok (nullptr, ";");
				double lng, lat;
				if (!nm || !nm[0] || !ps || sscanf (ps, "%lf%lf", &lng, &lat) != 2) continue;
				Cand cd; cd.file = CollCfgPath (d, nm, ".cfg");
				cd.f.haveLocation = true; cd.f.lng = Rad (lng); cd.f.lat = Rad (lat);
				std::string text;
				if (sdk.ReadText (sdk.Resolve (cd.file), text)) {
					double l0 = cd.f.lng, b0 = cd.f.lat;
					std::vector<std::string> warn;
					CollParseBaseFile (text, cd.f, warn);
					if (!cd.f.haveLocation) { cd.f.lng = l0; cd.f.lat = b0; cd.f.haveLocation = true; }
				}
				if (cd.f.name.empty ()) continue;
				if (!Taken (cand, cd)) cand.push_back (std::move (cd));
			}
		} else if (snprintf (cbuf, 256, "%s\\Base", pname.c_str ()) < 256) ScanBases (sdk, d, cbuf, cand, ctxLogged); // Planet.cpp:365-367
		double R = sdk.Size (hp);
		for (uint32_t bi = 0, nb = sdk.BaseCount (hp); bi < nb; bi++) {
			CollH hb = sdk.Base (hp, bi);
			std::string bname = sdk.Name (hb);
			if (bname.empty ()) continue;
			double lng, lat, rad;
			sdk.BaseEquPos (hb, lng, lat, rad);
			Cand *m = nullptr; int nm = 0;
			for (auto &c : cand) {
				if (c.f.name != bname) continue;
				if (c.f.haveLocation && c.f.lng == lng && c.f.lat == lat) { m = &c; nm = 1; break; }
				if (!m || (!m->ctx.empty () && c.ctx.empty ())) m = &c; // a CONTEXT dir's file only when no other has the name
				nm++;
			}
			if (!m) { sdk.Log (1, ("Collision bases: no cfg file matches base " + bname).c_str ()); continue; }
			if (nm > 1) sdk.Log (1, ("Collision bases: several cfg files match base " + bname + ", the first is used").c_str ());
			auto r = std::make_unique<CollBaseRec> ();
			r->planetIdx = (int)pi; r->baseIdx = (int)bi; r->hPlanet = hp; r->hBase = hb;
			r->planet = pname; r->base = bname; r->rPlanet = R; r->lng = lng; r->lat = lat;
			r->elev = geometry ? sdk.Elevation (hp, lng, lat) : 0;
			double rr = R + r->elev, slng = sin (lng), clng = cos (lng), slat = sin (lat), clat = cos (lat);
			r->rposP = Vector (rr * clat * clng, rr * slat, rr * clat * slng);
			r->rrotP = Matrix (clng*slat, clng*clat, -slng, -clat, slat, 0, slng*slat, slng*clat, clng);
			uint32_t slotI = 0;
			for (auto &o : m->f.obj) {
				nObjects++;
				if (!CollBaseObjIncluded (o)) continue;
				nIncluded++;
				CollBaseObjView v;
				v.planet = pname; v.base = bname; v.type = o.type; v.planetIdx = (int)pi; v.baseIdx = (int)bi; v.obj = o.index;
				v.cls = CollBaseObjClass (o.type); v.size = o.scale; v.x = o.pos.x; v.z = o.pos.z;
				v.mat = o.collMat.empty () ? 0 : CollInternMaterial (o.collMat.c_str ());
				v.hPlanet = hp; v.hBase = hb;
				if (geometry) {
					std::vector<std::string> warn;
					CollBaseElev el;
					el.lng = lng; el.lat = lat; el.elev = r->elev;
					el.at = [&sdk, hp] (double l, double b) { return sdk.Elevation (hp, l, b); };
					if (CollBaseObjGeometry (o, sdk, d, R, m->f.mapToSphere, warn, &el)) {
						if (!strcasecmp (o.type.c_str (), "MESH")) v.size = o.restBox;
						r->shape.SetObject (slotI, o.index, o.grp.data (), o.grp.size (), v.mat, true);
						r->shape.Follow (slotI, 1, 1);
						r->objOf.push_back (o.index);
						slotI++;
					}
					for (auto &w : warn) sdk.Log (1, ("Collision bases: " + bname + ": " + w).c_str ());
					o.grp.clear ();
				}
				r->view.push_back (v);
			}
			if (geometry && slotI) r->shape.Finish (r->rposP, r->rrotP, R);
			rec.push_back (std::move (r));
		}
	}
	char buf[160];
	snprintf (buf, sizeof buf, "Collision bases: %zu bases, %u objects, %u included", rec.size (), nObjects, nIncluded);
	sdk.Log (1, buf);
}

void CollBaseA::Poll (CollSdk &sdk, uint32_t frame)
{
	for (size_t i = 0; i < rec.size (); i++) {
		CollBaseRec &r = *rec[i];
		if (!r.checkedOk || (frame + i) % 100 != 0) continue;
		Vector gp, gv, pp, pv; Matrix gR, pR;
		sdk.GlobalState (r.hBase, gp, gv, gR);
		sdk.GlobalState (r.hPlanet, pp, pv, pR);
		Vector rel = tmul (pR, gp - pp) - r.rposP;
		Matrix exp = pR * r.rrotP;
		bool ok = rel.length () < 1e-3;
		for (int k = 0; k < 9; k++) if (std::fabs (gR.data[k] - exp.data[k]) > 1e-12) ok = false;
		if (!ok) {
			r.checkedOk = false;
			char buf[256];
			snprintf (buf, sizeof buf, "Collision bases: pose check failed for %s (%.3g m)", r.base.c_str (), rel.length ());
			sdk.Log (1, buf);
		}
	}
}

const CollBaseRec *CollBaseA::Rec (int planet, int base) const
{
	for (auto &r : rec) if (r->planetIdx == planet && r->baseIdx == base) return r.get ();
	return nullptr;
}

const CollBaseObjView *CollBaseA::Object (int planet, int base, int obj) const
{
	const CollBaseRec *r = Rec (planet, base);
	if (!r) return nullptr;
	for (auto &v : r->view) if ((int)v.obj == obj) return &v;
	return nullptr;
}

void CollBaseA::All (std::vector<const CollBaseObjView *> &all) const
{
	all.clear ();
	for (auto &r : rec) for (auto &v : r->view) all.push_back (&v);
}
