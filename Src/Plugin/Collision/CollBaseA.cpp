// not upstream: collision addon, planet and base cfg scan, matching, placement and building colliders (design E2 10.3, 10.4)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "CollBaseA.h"
#include "DentMath.h"

namespace {
const int OBJTP_PLANET_ = 4; // OBJTP_PLANET (OrbiterAPI.h:1773)

struct Cand { std::string file; CollBaseFile f; };

bool EndsCfg (const std::string &s) { return s.size () > 4 && s.compare (s.size () - 4, 4, ".cfg") == 0; }

// Planet::ScanBases (Planet.cpp:465-535): .cfg files, BASE-V2.0 first line, PERIOD, CONTEXT, duplicates dropped
void ScanBases (CollSdk &sdk, const CollDirs &d, const std::string &dir, std::vector<Cand> &out)
{
	std::string path = CollCfgPath (d, dir, "");
	std::vector<std::string> ls = sdk.ListDir (sdk.Resolve (path));
	for (auto &f : ls) {
		if (!EndsCfg (f)) continue;
		std::string text;
		if (!sdk.ReadText (sdk.Resolve (path + "\\" + f), text)) continue;
		if (text.compare (0, 9, "BASE-V2.0") != 0) continue;
		Cand c; c.file = path + "\\" + f;
		std::vector<std::string> warn;
		CollParseBaseFile (text, c.f, warn);
		std::string per;
		if (CollItemString (text, "PERIOD", per)) {
			double m0 = -1e10, m1 = 1e10;
			sscanf (per.c_str (), "%lf%lf", &m0, &m1);
			double mjd = sdk.SimMJD ();
			if (mjd < m0 || mjd > m1) continue;
		}
		std::string ctx;
		if (CollItemString (text, "CONTEXT", ctx)) c.f.context = true;
		bool dup = false;
		for (auto &o : out) if (o.f.name == c.f.name) dup = true;
		if (dup) continue;
		out.push_back (std::move (c));
	}
}
} // namespace

void CollBaseA::Build (CollSdk &sdk, const CollDirs &d, bool geometry)
{
	rec.clear (); nObjects = nIncluded = 0;
	for (uint32_t pi = 0, np = sdk.GbodyCount (); pi < np; pi++) {
		CollH hp = sdk.Gbody (pi);
		if (sdk.ObjType (hp) != OBJTP_PLANET_ || !sdk.BaseCount (hp)) continue;
		std::string pname = sdk.Name (hp), ptext;
		std::vector<Cand> cand;
		if (!sdk.ReadText (sdk.Resolve (CollCfgPath (d, pname, ".cfg")), ptext)) {
			sdk.Log (1, ("Collision bases: no cfg for planet " + pname + ", no buildings there").c_str ());
			continue;
		}
		bool surf = false;
		{
			size_t p = 0;
			while (p < ptext.size ()) {
				size_t e = ptext.find ('\n', p);
				std::string l = ptext.substr (p, e == std::string::npos ? std::string::npos : e - p);
				p = e == std::string::npos ? ptext.size () : e + 1;
				while (!l.empty () && (l.back () == '\r' || l.back () == ' ' || l.back () == '\t')) l.pop_back ();
				size_t b = l.find_first_not_of (" \t");
				if (b == std::string::npos) continue;
				l = l.substr (b);
				if (!surf) { if (!strncasecmp (l.c_str (), "BEGIN_SURFBASE", 14)) surf = true; continue; }
				if (!strncasecmp (l.c_str (), "END_SURFBASE", 12)) break;
				if (!strncasecmp (l.c_str (), "DIR", 3)) { std::string dir = l.substr (3); dir.erase (0, dir.find_first_not_of (" \t")); ScanBases (sdk, d, dir, cand); continue; }
				size_t c = l.find (':');
				if (c == std::string::npos) continue;
				std::string nm = l.substr (0, c), text;
				Cand cd; cd.file = CollCfgPath (d, nm, ".cfg");
				double lng = 0, lat = 0;
				if (sscanf (l.c_str () + c + 1, "%lf%lf", &lng, &lat) == 2) { cd.f.haveLocation = true; cd.f.lng = lng * Pi / 180; cd.f.lat = lat * Pi / 180; }
				if (sdk.ReadText (sdk.Resolve (cd.file), text)) {
					double l0 = cd.f.lng, b0 = cd.f.lat; bool hl = cd.f.haveLocation;
					std::vector<std::string> warn;
					CollParseBaseFile (text, cd.f, warn);
					if (!cd.f.haveLocation) { cd.f.lng = l0; cd.f.lat = b0; cd.f.haveLocation = hl; }
				}
				if (cd.f.name.empty ()) continue;
				bool dup = false;
				for (auto &o : cand) if (o.f.name == cd.f.name) dup = true;
				if (!dup) cand.push_back (std::move (cd));
			}
		}
		if (!surf) ScanBases (sdk, d, pname + "\\Base", cand);
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
				if (!m) m = &c;
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
					if (CollBaseObjGeometry (o, sdk, d, R, m->f.mapToSphere, warn)) {
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
