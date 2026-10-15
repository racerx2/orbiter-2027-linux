// not upstream: collision addon, the streamed 3D buildings as colliders (design bldg-collision 3)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include "CollBldgA.h"
#include "CollAddonFrame.h"
#include "CollBaseA.h"
#if COLL_HAVE_BLDGGEOM
#include "BldgGeom.h"
#endif

namespace {
const int Z = 14;
const int OBJTP_PLANET_B = 4; // OBJTP_PLANET (OrbiterAPI.h:1773)
const double PI_B = 3.14159265358979323846;
typedef std::chrono::steady_clock Clock;

double Ms (Clock::duration d) { return std::chrono::duration<double, std::milli> (d).count (); }

// a point's local axes in the planet frame, as BldgMgr: x east, y up, z north of the .bld
Vector Up (double lat, double lng) { return Vector (cos (lat) * cos (lng), sin (lat), cos (lat) * sin (lng)); }
Vector East (double lng) { return Vector (-sin (lng), 0.0, cos (lng)); }
Vector North (double lat, double lng) { return Vector (-sin (lat) * cos (lng), cos (lat), -sin (lat) * sin (lng)); }

// z14 tile of a point and a point of a tile, x east from 180W, y south from the top (BldgMgr)
void TileOf (double lat, double lng, int &x, int &y)
{
	int n = 1 << Z;
	lat = std::max (-1.4844, std::min (1.4844, lat));
	x = (int)std::floor ((lng / (2.0 * PI_B) + 0.5) * n);
	y = (int)std::floor ((1.0 - std::asinh (std::tan (lat)) / PI_B) * 0.5 * n);
	y = std::max (0, std::min (n - 1, y));
}

void Merc (double fx, double fy, double &lat, double &lng)
{
	double n = (double)(1 << Z);
	lng = fx / n * 2.0 * PI_B - PI_B;
	lat = std::atan (std::sinh (PI_B * (1.0 - 2.0 * fy / n)));
}

uint64_t Mix (uint64_t x) { x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 29; x *= 0x94D049BB133111EBull; return x ^ (x >> 32); } // BldgMgr's building hash

uint64_t Key (int planet, int x, int y) { return (uint64_t)(uint32_t)planet << 40 | (uint64_t)(uint32_t)x << 20 | (uint64_t)(uint32_t)y; }

int64_t Cell (double v) { return (int64_t)std::floor (v / COLL_BLDG_CELL); }
int64_t CellKey (int64_t x, int64_t z) { return (int64_t)(((uint64_t)x << 32) ^ (uint64_t)(uint32_t)z); }
}

struct CollBldgA::Bldg {
	enum : uint8_t { IDLE, BUILT, EXCLUDED, SKIPPED };
	uint8_t state = IDLE;
	bool checked = false, wasPart = false;     // base exclusion tested; a part last frame
	double rb = 0;                             // footprint radius about the anchor [m]
	Vector c;                                  // anchor on the tile centre's ground, tile frame
	std::unique_ptr<CollGeom> geom;
	uint32_t version = 0, stamp = 0;           // stamp: frame of best
	double best = 0, g = 0, farSince = -1, tGround = 0;
};

struct CollBldgA::Tile {
	int x = 0, y = 0;
	uint32_t slot = 0;
	uint64_t key = 0;
	const Planet *p = nullptr;
	CollBldgTile tile;
	Vector rposP; Matrix rrotP;                // tile centre at its ground: x south, y up, z east (CollBaseA.cpp:157)
	double rad = 0, maxRb = 0;                 // farthest footprint point from the centre, largest footprint radius [m]
	std::vector<Bldg> b;
	std::unordered_map<int64_t, std::vector<uint32_t>> grid; // anchors by COLL_BLDG_CELL cell
	std::vector<uint32_t> active;              // built buildings
	std::vector<const CollBaseRec *> near;     // bases with a structure index within COLL_BLDG_BASES
	std::vector<std::shared_ptr<const FixedRec>> nearF; // fixed structures within COLL_BLDG_BASES (design fixed-structures)
	std::vector<const ZoneRec *> nearZ;        // base files' clear zones within COLL_BLDG_BASES + their radius (design base-clear)
};

struct CollBldgA::FixedRec {                   // a fixed structure's steep faces in the horizon frame at its origin, as a base record
	CollH h = nullptr;
	const Planet *p = nullptr;
	std::string name;
	Vector loc;                                // origin, planet frame
	Matrix Rp;                                 // its rotation in the planet frame when made
	Vector rposP; Matrix rrotP;                // record frame: x south, y up, z east (CollBaseA.cpp)
	uint64_t sig = 0;
	double clear = 0;                          // FixedStructureClear [m] (design fixed-clear)
#if COLL_HAVE_BLDGGEOM
	std::shared_ptr<const BldgGeom::BaseIndex> bidx;
#endif
};

CollBldgA::CollBldgA (CollSdk &s, const CollCfgValues &c) : sdk (s), cfg (c) {}

CollBldgA::~CollBldgA () { Release (); }

bool CollBldgA::Built ()
{
#if COLL_HAVE_BLDGGEOM
	return true;
#else
	return false;
#endif
}

void CollBldgA::Log (const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (buf, sizeof buf, fmt, ap);
	va_end (ap);
	sdk.Log (1, buf);
}

void CollBldgA::Start (const CollBaseA *b)
{
	Release ();
	bases = b;
	planets.clear ();
	on = full = false;
	if (!Built () || !cfg.streamBuildings || cfg.model == 0) return; // off: no SDK call at all
	for (uint32_t i = 0, n = sdk.GbodyCount (); i < n; i++) {
		CollH h = sdk.Gbody (i);
		if (!h || sdk.ObjType (h) != OBJTP_PLANET_B || !sdk.BldgStream (h)) continue;
		planets.push_back (Planet { h, (int)i, sdk.Size (h), sdk.PlanetPeriod (h), sdk.Name (h), Vector (), Vector (), IMatrix () });
	}
	on = !planets.empty ();
	if (on) Log ("Collision bldg: streamed buildings are solid on %zu planets", planets.size ());
	for (const Planet &p : planets) // the base files' clear zones, from the core whatever cfg file matched (design base-clear)
		for (uint32_t k = 0, nb = sdk.BaseCount (p.h); k < nb; k++) {
			std::vector<CollClearZone> z;
			sdk.BaseClearZones (sdk.Base (p.h, k), z);
			for (const CollClearZone &c : z) {
				double slng = sin (c.lng), clng = cos (c.lng), slat = sin (c.lat), clat = cos (c.lat);
				ZoneRec r { &p, Up (c.lat, c.lng), Up (c.lat, c.lng) * p.R, Matrix (clng*slat, clng*clat, -slng, -clat, slat, 0, slng*slat, slng*clat, clng), c.rad }; // CollBaseA's base frame: x south, y up, z east
				zones.push_back (r);
			}
		}
	if (!zones.empty ()) Log ("Collision bldg: %zu clear zones from base files keep streamed buildings out", zones.size ());
}

void CollBldgA::Release ()
{
	for (auto &kv : tiles) Drop (*kv.second);
	tiles.clear (); slotOf.clear (); bad.clear (); pending.clear (); bodies.clear (); ves.clear (); vground.clear ();
	fixedIn.clear (); fixedRec.clear (); zones.clear (); // they point at the planets Start makes again
}

void CollBldgA::Clear () { Release (); on = false; bases = nullptr; }

void CollBldgA::TimeJump ()
{
	vground.clear ();
	for (auto &kv : tiles)
		for (uint32_t i : kv.second->active) { kv.second->b[i].farSince = -1; kv.second->b[i].tGround = -1e100; }
}

void CollBldgA::Summary ()
{
	if (st.loads)
		Log ("Collision bldg: summary tiles=%llu drops=%llu built=%llu freed=%llu rebuilt=%llu deferred=%llu excluded=%llu skipped=%llu load_ms_mean=%.3f load_ms_max=%.3f build_ms_max=%.3f build_vtx_max=%u",
			(unsigned long long)st.loads, (unsigned long long)st.drops, (unsigned long long)st.built, (unsigned long long)st.freed, (unsigned long long)st.rebuilt, (unsigned long long)st.deferred,
			(unsigned long long)st.excluded, (unsigned long long)st.skipped, st.loadMs / (double)st.loads, st.loadMsMax, st.buildMsMax, st.buildVtxMax);
	if (st.frames)
		Log ("Collision bldg: elevation frames=%llu calls=%llu calls_max=%u ms_mean=%.4f ms_max=%.3f",
			(unsigned long long)st.frames, (unsigned long long)st.elevCalls, st.elevCallsMax, st.elevMs / (double)st.frames, st.elevMsMax);
}

double CollBldgA::Elev (CollH planet, double lng, double lat)
{
	Clock::time_point a = Clock::now ();
	double g = sdk.Elevation (planet, lng, lat);
	fElevMs += Ms (Clock::now () - a);
	fElevN++;
	return g;
}

void CollBldgA::Emit (const Body &t, CollABody &B)
{
	B.kind = COLLB_BASE;
	B.id = BodyId (t.planet, t.slot);
	B.memberHash = B.id;
	B.m = 0;
	B.planet = t.planet;
	B.rmax = t.rmax + COLL_SKIN_MAX;
	if (t.entry) B.entry |= COLLE_MESH; // a building gained over a vessel: entry check, no deep impact
	B.parts.clear ();
	for (const Part &q : t.part) {
		CollPartRef p {};
		p.geom = q.geom; p.skin = q.geom->skin;
		p.owner = CollOwnerKey { COLLO_BUILDING, 0, t.planet, BaseOf (t.slot), (int32_t)q.index, 0 };
		p.partKey = 0; p.version = q.version; p.mesh = (uint16_t)q.index; p.mask = nullptr;
		B.parts.push_back (p);
	}
}

void CollBldgA::Split (const std::vector<CollVtx> &v, const std::vector<std::array<uint32_t, 3>> &tri, std::vector<CollGroupData> &grp)
{
	grp.clear ();
	std::vector<int32_t> at (v.size (), -1);
	std::vector<uint32_t> used;
	for (const auto &tr : tri) {
		if (grp.empty () || grp.back ().vtx.size () + 3 > 65535) { // a new group: its own vertex numbering
			for (uint32_t u : used) at[u] = -1;
			used.clear ();
			grp.emplace_back ();
		}
		CollGroupData &G = grp.back ();
		for (int k = 0; k < 3; k++) {
			uint32_t s = tr[k];
			if (at[s] < 0) {
				at[s] = (int32_t)G.vtx.size ();
				used.push_back (s);
				G.vtx.push_back (v[s]);
			}
			G.idx.push_back ((uint16_t)at[s]);
		}
	}
}

size_t CollBldgA::BuiltCount () const
{
	size_t n = 0;
	for (auto &kv : tiles) n += kv.second->active.size ();
	return n;
}

bool CollBldgA::Name (int planet, int base, int obj, std::string &out) const
{
	if (base > -2) return false;
	auto it = tiles.find ((uint32_t)(-2 - (int64_t)base));
	if (it == tiles.end () || it->second->p->idx != planet) return false;
	const Tile &t = *it->second;
	out = "building " + t.p->name + " z14 " + std::to_string (t.x) + "/" + std::to_string (t.y) + " #" + std::to_string (obj);
	return true;
}

void CollBldgA::Free (Tile &t, uint32_t i)
{
	Bldg &b = t.b[i];
	if (b.geom) st.freed++;
	b.geom.reset ();
	b.state = Bldg::IDLE;
	b.wasPart = false;
	b.farSince = -1;
}

void CollBldgA::Drop (Tile &t)
{
	for (uint32_t i : t.active) Free (t, i);
	t.active.clear ();
	sdk.BldgFree (t.tile);
	st.drops++;
}

// footprint circle and height range against a vessel bound: horizontal in the tile frame, vertical over the vessel's ground
double CollBldgA::Gap (const Tile &t, uint32_t i, const Vector &q, double alt, double r) const
{
	const Bldg &b = t.b[i];
	const CollBldgRec &R = t.tile.b[i];
	double dh = std::max (0.0, std::hypot (q.x - b.c.x, q.z - b.c.z) - b.rb);
	double lo = R.hmin > 0 ? R.hmin : -4.0, hi = R.hmax + COLL_BLDG_ROOF; // walls from -4 m (TileStreamMvt.cpp), pitched roofs above hmax
	double dv = std::max (0.0, std::max (alt - hi, lo - alt));
	return std::hypot (dh, dv) - r;
}

void CollBldgA::Want (const Ves &v)
{
	const Planet &P = *v.p;
	double rl = v.loc.length (), lat = std::asin (std::max (-1.0, std::min (1.0, v.loc.y / rl))), lng = std::atan2 (v.loc.z, v.loc.x);
	double range = std::max (COLL_BLDG_TILES, COLL_BLDG_TILES_S * v.speed);
	double dlat = range / P.R, dlng = range / (P.R * std::max (0.05, std::cos (lat)));
	int x0, y0, x1, y1, vx, vy;
	TileOf (lat + dlat, lng - dlng, x0, y0);
	TileOf (lat - dlat, lng + dlng, x1, y1);
	TileOf (lat, lng, vx, vy);
	if (x1 < x0) x1 += 1 << Z; // across 180
	if (vx < x0) vx += 1 << Z;
	x0 = std::max (x0, vx - 16); x1 = std::min (x1, vx + 16); // at most 33 x 33 tiles about the vessel's own
	y0 = std::max (y0, vy - 16); y1 = std::min (y1, vy + 16);
	for (int y = y0; y <= y1; y++)
		for (int xx = x0; xx <= x1; xx++) {
			int x = xx & ((1 << Z) - 1);
			uint64_t key = Key (P.idx, x, y);
			if (slotOf.count (key) || bad.count (key) || std::find (pending.begin (), pending.end (), key) != pending.end ()) continue;
			if (sdk.BldgWant (P.h, x, y) == 1) pending.push_back (key); // 0: the stream fetches it; -1 is not cached (offline and not on disk)
		}
}

// one tile from the stream's file a frame, on the sim thread
void CollBldgA::Load ()
{
	while (!pending.empty () && !full) {
		uint64_t key = pending.front ();
		pending.erase (pending.begin ());
		if (slotOf.count (key) || bad.count (key)) continue;
		int pi = (int)(key >> 40), x = (int)((key >> 20) & 0xFFFFF), y = (int)(key & 0xFFFFF);
		const Planet *P = nullptr;
		for (const Planet &p : planets) if (p.idx == pi) P = &p;
		if (!P) continue;
		double lat, lng;
		Merc (x + 0.5, y + 0.5, lat, lng);
		Vector c = Up (lat, lng) * P->R;
		bool near = false; // still near a vessel: wanted half a second ago at most
		for (const Ves &v : ves) near = near || (v.p == P && (v.loc - c).length () < COLL_BLDG_KEEP + 3000.0);
		if (!near) continue;
		if (nextSlot >= COLL_BLDG_SLOTS) {
			full = true;
			Log ("Collision bldg: %u tiles loaded this session, no new tiles load; the kept ones stay solid", nextSlot);
			return;
		}
		Clock::time_point a = Clock::now ();
		auto t = std::make_unique<Tile> ();
		if (!sdk.BldgLoad (P->h, x, y, t->tile)) {
			bad.insert (key);
			Log ("Collision bldg: %s z14 %d/%d did not load, skipped this session", P->name.c_str (), x, y);
			return;
		}
		t->x = x; t->y = y; t->key = key; t->p = P; t->slot = nextSlot++;
		try { Setup (*t); } catch (...) { sdk.BldgFree (t->tile); throw; }
		double ms = Ms (Clock::now () - a);
		st.loads++; st.loadMs += ms; st.loadMsMax = std::max (st.loadMsMax, ms);
		Log ("Collision bldg: %s z14 %d/%d slot %u: %zu buildings, %u vertices, loaded in %.3f ms", P->name.c_str (), x, y, t->slot, t->tile.b.size (), t->tile.nv, ms);
		slotOf[key] = t->slot;
		tiles[t->slot] = std::move (t);
		return;
	}
}

void CollBldgA::Setup (Tile &t)
{
	const Planet &P = *t.p;
	double lat0, lng0;
	Merc (t.x + 0.5, t.y + 0.5, lat0, lng0);
	double g0 = Elev (P.h, lng0, lat0);
	double slat = sin (lat0), clat = cos (lat0), slng = sin (lng0), clng = cos (lng0);
	t.rposP = Up (lat0, lng0) * (P.R + g0);
	t.rrotP = Matrix (clng*slat, clng*clat, -slng, -clat, slat, 0, slng*slat, slng*clat, clng);
	size_t nb = t.tile.b.size ();
	if (nb > 65536) { // a part's mesh field holds the building index
		Log ("Collision bldg: %s z14 %d/%d has %zu buildings, those past 65535 are not solid", P.name.c_str (), t.x, t.y, nb);
		nb = 65536;
	}
	t.b.resize (nb);
	for (uint32_t i = 0; i < nb; i++) {
		const CollBldgRec &R = t.tile.b[i];
		Bldg &b = t.b[i];
		if ((uint64_t)R.v0 + R.nv > t.tile.nv || (uint64_t)R.i0 + R.ni > t.tile.ni || !std::isfinite (R.lat) || !std::isfinite (R.lng) || !R.nv) {
			b.state = Bldg::SKIPPED;
			st.skipped++;
			continue;
		}
		const float *v = t.tile.vtx + (size_t)R.v0 * 6;
		double rb = 0;
		for (uint32_t k = 0; k < R.nv; k++) rb = std::max (rb, std::hypot ((double)v[(size_t)k * 6], (double)v[(size_t)k * 6 + 2]));
		if (!std::isfinite (rb)) { b.state = Bldg::SKIPPED; st.skipped++; continue; }
		b.rb = rb;
		b.c = tmul (t.rrotP, Up (R.lat, R.lng) * (P.R + g0) - t.rposP);
		t.rad = std::max (t.rad, std::hypot (b.c.x, b.c.z) + rb);
		t.maxRb = std::max (t.maxRb, rb);
		t.grid[CellKey (Cell (b.c.x), Cell (b.c.z))].push_back (i);
	}
	if (bases) // bases near the tile whose structures keep its buildings out (BldgMgr::Make)
		for (const auto &r : bases->rec)
			if (r->bidx && r->hPlanet == P.h && P.R * std::acos (std::min (1.0, dotp (Up (r->lat, r->lng), Up (lat0, lng0)))) <= COLL_BLDG_BASES) t.near.push_back (r.get ());
	for (const ZoneRec &z : zones) // the base files' clear zones near the tile, along the ground (BldgMgr, ClearZones::Near)
		if (z.p == t.p && 2.0 * P.R * std::asin (std::min (1.0, 0.5 * (Up (lat0, lng0) - z.u).length ())) <= COLL_BLDG_BASES + z.r) t.nearZ.push_back (&z);
	NearFixed (t);
}

void CollBldgA::NearFixed (Tile &t) const
{
	t.nearF.clear ();
	double lat0, lng0;
	Merc (t.x + 0.5, t.y + 0.5, lat0, lng0);
	for (const auto &r : fixedRec) {
		double rl = r->loc.length ();
		if (r->p != t.p || !(rl > 0)) continue;
		if (t.p->R * std::acos (std::min (1.0, dotp (r->loc / rl, Up (lat0, lng0)))) <= COLL_BLDG_BASES) t.nearF.push_back (r);
	}
}

// the client's rule (BldgMgr::Make): footprint corners, edge midpoints or centroid within COLL_BLDG_MARGIN of a base structure, or a structure inside the footprint
bool CollBldgA::Excluded (const Tile &t, uint32_t i, double g, const std::vector<std::array<double, 2>> &ring) const
{
#if COLL_HAVE_BLDGGEOM
	if ((t.near.empty () && t.nearF.empty () && t.nearZ.empty ()) || ring.size () < 3) return false;
	const CollBldgRec &R = t.tile.b[i];
	Vector ub = Up (R.lat, R.lng), eb = East (R.lng), nb = North (R.lat, R.lng), base = ub * (t.p->R + g);
	std::vector<std::array<double, 2>> foot;
	auto place = [&] (const Vector &rposP, const Matrix &rrotP) {
		foot.clear ();
		for (const auto &q : ring) {
			Vector l = tmul (rrotP, base + eb * q[0] + nb * q[1] - rposP); // the base frame: x south, z east
			foot.push_back ({ l.x, l.z });
		}
	};
	auto hits = [&] (const Vector &rposP, const Matrix &rrotP, const BldgGeom::BaseIndex &bi) {
		place (rposP, rrotP);
		bool hit = false;
		double cx = 0, cz = 0;
		for (size_t k = 0; k < foot.size () && !hit; k++) {
			const auto &a = foot[k], &c = foot[(k + 1) % foot.size ()];
			hit = bi.Near (a[0], a[1], COLL_BLDG_MARGIN) || bi.Near (0.5 * (a[0] + c[0]), 0.5 * (a[1] + c[1]), COLL_BLDG_MARGIN);
			cx += a[0]; cz += a[1];
		}
		return hit || bi.Near (cx / foot.size (), cz / foot.size (), COLL_BLDG_MARGIN) || bi.Inside (foot);
	};
	for (const ZoneRec *z : t.nearZ) { // the client's base file clear zone test (design base-clear)
		place (z->rposP, z->rrotP);
		if (BldgGeom::Reaches (foot, z->r)) return true;
	}
	for (const CollBaseRec *r : t.near) if (hits (r->rposP, r->rrotP, *r->bidx)) return true;
	for (const auto &r : t.nearF) {
		if (r->clear > 0) { // the client's clear zone test (design fixed-clear), faces or not
			place (r->rposP, r->rrotP);
			if (BldgGeom::Reaches (foot, r->clear)) return true;
		}
		if (r->bidx && hits (r->rposP, r->rrotP, *r->bidx)) return true;
	}
	return false;
#else
	(void)t; (void)i; (void)g; (void)ring;
	return false;
#endif
}

void CollBldgA::Shape (const Tile &t, uint32_t i, BldgGeom::Out &o) const
{
#if COLL_HAVE_BLDGGEOM
	const CollBldgRec &R = t.tile.b[i];
	uint64_t la, ln;
	memcpy (&la, &R.lat, 8);
	memcpy (&ln, &R.lng, 8);
	BldgGeom::Build (t.tile.vtx + (size_t)R.v0 * 6, R.nv, t.tile.idx + R.i0, R.ni, R.hmin, R.hmax, (uint32_t)Mix (la ^ (ln * 0x9E3779B97F4A7C15ull)), o); // the client's hash: its gable roofs and skips
#else
	(void)t; (void)i; (void)o;
#endif
}

// fixed structures (design fixed-structures): a record per landed FixedStructure vessel, made again when it moved, turned or its meshes changed
void CollBldgA::FixedPass ()
{
#if COLL_HAVE_BLDGGEOM
	std::vector<std::shared_ptr<const FixedRec>> now;
	std::vector<std::pair<const Planet *, Vector>> changed; // planet frame places whose tiles check their buildings again
	for (const FixedIn &f : fixedIn) {
		const Planet *P = nullptr;
		Vector loc;
		for (const Planet &p : planets) {
			Vector l = tmul (p.pR, f.x - p.pp);
			if (l.length () > 0 && l.length () - p.R < COLL_BLDG_ALT + 12e3) { P = &p; loc = l; break; }
		}
		if (!P) continue;
		Matrix Rp = transp (P->pR) * f.R;
		std::shared_ptr<const FixedRec> old;
		for (const auto &r : fixedRec) if (r->h == f.h) old = r;
		if (old && old->p == P && old->sig == f.sig && (old->loc - loc).length () <= COLL_FIXED_MOVE) {
			double turn = 0;
			for (int c = 0; c < 3; c++) turn = std::max (turn, (Vector (Rp.data[c], Rp.data[3 + c], Rp.data[6 + c]) - Vector (old->Rp.data[c], old->Rp.data[3 + c], old->Rp.data[6 + c])).length ());
			if (turn <= COLL_FIXED_TURN) { now.push_back (old); continue; }
		}
		auto r = std::make_shared<FixedRec> ();
		r->h = f.h; r->p = P; r->name = f.name; r->loc = loc; r->Rp = Rp; r->sig = f.sig; r->clear = f.clear;
		double rl = loc.length (), lat = std::asin (std::max (-1.0, std::min (1.0, loc.y / rl))), lng = std::atan2 (loc.z, loc.x);
		double slat = sin (lat), clat = cos (lat), slng = sin (lng), clng = cos (lng);
		r->rposP = loc;
		r->rrotP = Matrix (clng*slat, clng*clat, -slng, -clat, slat, 0, slng*slat, slng*clat, clng); // CollBaseA's base frame at the vessel
		auto bi = std::make_shared<BldgGeom::BaseIndex> ();
		for (const auto &m : f.mesh)
			for (const CollGroupData &g : m.first->grp)
				for (size_t k = 0; k + 2 < g.idx.size (); k += 3) {
					double v[3][3];
					bool ok = true;
					for (int c = 0; c < 3 && ok; c++) {
						uint16_t n = g.idx[k + c];
						ok = n < g.vtx.size ();
						if (!ok) break;
						Vector q = tmul (r->rrotP, mul (Rp, Vector (g.vtx[n].x, g.vtx[n].y, g.vtx[n].z) + m.second));
						v[c][0] = q.x; v[c][1] = q.y; v[c][2] = q.z;
					}
					if (ok) bi->Add (v[0], v[1], v[2]);
				}
		r->bidx = bi;
		Log ("Collision bldg: fixed structure '%s' keeps streamed buildings off %zu structure faces, clear within %.0f m", f.name.c_str (), bi->Count (), r->clear);
		if (old) changed.push_back ({ old->p, old->loc });
		changed.push_back ({ P, loc });
		now.push_back (r);
	}
	for (const auto &r : fixedRec)
		if (std::find (now.begin (), now.end (), r) == now.end () && std::none_of (now.begin (), now.end (), [&] (const std::shared_ptr<const FixedRec> &x) { return x->h == r->h; })) {
			Log ("Collision bldg: fixed structure '%s' no longer keeps streamed buildings out", r->name.c_str ());
			changed.push_back ({ r->p, r->loc });
		}
	fixedRec.swap (now);
	if (changed.empty ()) return;
	size_t n = 0;
	for (auto &kv : tiles) {
		Tile &t = *kv.second;
		double lat0, lng0;
		Merc (t.x + 0.5, t.y + 0.5, lat0, lng0);
		bool near = false;
		for (const auto &c : changed) {
			double cl = c.second.length ();
			near = near || (c.first == t.p && cl > 0 && t.p->R * std::acos (std::min (1.0, dotp (c.second / cl, Up (lat0, lng0)))) <= COLL_BLDG_BASES);
		}
		if (near) { Recheck (t); n++; }
	}
	if (n) Log ("Collision bldg: %zu tiles checked their buildings again for fixed structures", n);
#endif
}

void CollBldgA::Recheck (Tile &t)
{
#if COLL_HAVE_BLDGGEOM
	NearFixed (t);
	BldgGeom::Out o;
	for (uint32_t i = 0; i < t.b.size (); i++) {
		Bldg &b = t.b[i];
		if (b.state == Bldg::EXCLUDED) { b.state = Bldg::IDLE; b.checked = false; continue; }
		if (b.state == Bldg::IDLE) { b.checked = false; continue; }
		if (b.state != Bldg::BUILT) continue;
		Shape (t, i, o);
		b.checked = true;
		if (!Excluded (t, i, b.g, o.ring)) continue;
		Free (t, i); // leaves the active list in this frame's parts pass
		b.state = Bldg::EXCLUDED;
		st.excluded++;
	}
#else
	(void)t;
#endif
}

// the drawn shape in the tile frame: P = Up (R + g) + East x + Up y + North z, then v = rrotP^T (P - rposP) in double
uint32_t CollBldgA::Activate (Tile &t, uint32_t i, double simt, double g)
{
#if COLL_HAVE_BLDGGEOM
	Bldg &b = t.b[i];
	const CollBldgRec &R = t.tile.b[i];
	const float *vtx = t.tile.vtx + (size_t)R.v0 * 6;
	const uint32_t *idx = t.tile.idx + R.i0;
	BldgGeom::Out o;
	Shape (t, i, o);
	if (!b.checked) {
		b.checked = true;
		if (Excluded (t, i, g, o.ring)) {
			b.state = Bldg::EXCLUDED;
			st.excluded++;
			return 0;
		}
	}
	std::vector<Vector> lv, ln3;                // local positions and normals: x east, y up, z north
	std::vector<std::array<uint32_t, 3>> tri;
	if (o.pitched) { // walls to the eave, gables and slopes, as drawn
		if (!o.fv.empty () && o.fv.size () <= 65535)
			for (const BldgGeom::V &s : o.fv) { lv.push_back (Vector (s.x, s.y, s.z)); ln3.push_back (Vector (s.nx, s.ny, s.nz)); }
		for (size_t k = 0; !lv.empty () && k + 2 < o.fi.size (); k += 3) tri.push_back ({ o.fi[k], o.fi[k + 1], o.fi[k + 2] });
	} else { // the .bld surfaces the client draws: wall quads first, then the roof; a kind past 65535 drawn vertices is skipped (BldgMgr::Make)
		uint32_t nw = 0;
		while (nw * 4 + 3 < R.nv && vtx[(size_t)nw * 24 + 4] < 0.5f) nw++;
		uint32_t r0 = nw * 4;
		bool walls = !o.fv.empty () && o.fv.size () <= 65535, roof = !o.rv.empty () && o.rv.size () <= 65535;
		for (uint32_t k = 0; k < R.nv; k++) {
			const float *a = vtx + (size_t)k * 6;
			lv.push_back (Vector (a[0], a[1], a[2]));
			ln3.push_back (Vector (a[3], a[4], a[5]));
		}
		for (uint32_t k = 0; k + 2 < R.ni; k += 3) {
			uint32_t a = idx[k], c = idx[k + 1], d = idx[k + 2];
			if (a >= R.nv || c >= R.nv || d >= R.nv) continue;
			bool w = a < r0 && c < r0 && d < r0, r = a >= r0 && c >= r0 && d >= r0;
			if ((w && walls) || (r && roof)) tri.push_back ({ a, c, d });
		}
	}
	std::unique_ptr<CollGeom> geom;
	if (!tri.empty ()) {
		Vector ub = Up (R.lat, R.lng), eb = East (R.lng), nb = North (R.lat, R.lng), base = ub * (t.p->R + g);
		std::vector<CollVtx> cv (lv.size ());
		for (size_t s = 0; s < lv.size (); s++) {
			Vector p = tmul (t.rrotP, base + eb * lv[s].x + ub * lv[s].y + nb * lv[s].z - t.rposP);
			Vector n = tmul (t.rrotP, eb * ln3[s].x + ub * ln3[s].y + nb * ln3[s].z);
			cv[s] = CollVtx { (float)p.x, (float)p.y, (float)p.z, (float)n.x, (float)n.y, (float)n.z, 0.0f, 0.0f };
		}
		std::vector<CollGroupData> grp;
		Split (cv, tri, grp);
		std::vector<CollSrcGroup> src;
		for (size_t k = 0; k < grp.size (); k++) src.push_back (CollSrcGroup { &grp[k], CollSrc { i, (uint32_t)k, 0, 0, i } });
		geom = std::make_unique<CollGeom> ();
		CollBuildStats bs;
		if (!geom->Build (src.data (), src.size (), COLL_WELD_DEFAULT, &bs)) geom.reset ();
	}
	bool fresh = b.state != Bldg::BUILT;
	if (!geom) { // nothing the client draws, or no triangle survived
		if (!fresh) Free (t, i);
		b.state = Bldg::SKIPPED;
		st.skipped++;
		return 0;
	}
	b.geom = std::move (geom);
	b.version = ++verSerial; // its own version: the caches of the other buildings stay
	b.g = g;
	b.state = Bldg::BUILT;
	b.tGround = simt + COLL_BLDG_GROUND;
	b.wasPart = false;
	if (fresh) {
		t.active.push_back (i);
		b.farSince = -1;
		st.built++;
	}
	return R.nv;
#else
	(void)t; (void)i; (void)simt; (void)g;
	return 0;
#endif
}

void CollBldgA::Frame (double simt, const std::vector<Live> &v)
{
	bodies.clear ();
	if (!on) return;
	frameNo++;
	fElevMs = 0; fElevN = 0;
	for (Planet &p : planets) sdk.GlobalState (p.h, p.pp, p.pv, p.pR);
	ves.clear ();
	std::vector<VGround> vg; // this frame's ground cache: the vessels gone drop out
	for (const Live &l : v) { // vessels near a planet's ground: planet frame position, ground speed, height over the ground
		Vector x, u;
		Matrix Rv;
		sdk.GlobalState (l.h, x, u, Rv);
		for (const Planet &p : planets) {
			Vector loc = tmul (p.pR, x - p.pp);
			double rl = loc.length ();
			if (!(rl > 0) || !(rl - p.R < COLL_BLDG_ALT + 12e3)) continue;
			Vector w = std::fabs (p.T) > 0 ? mul (p.pR, Vector (0, 1, 0)) * (2.0 * PI_B / p.T) : Vector ();
			double speed = (u - (p.pv + crossp (x - p.pp, w))).length (); // against the planet-fixed point (CollSurfaceVel)
			if (!std::isfinite (speed)) break; // a runaway state: no buildings
			speed = std::min (speed, COLL_BLDG_VMAX);
			double lat = std::asin (std::max (-1.0, std::min (1.0, loc.y / rl))), lng = std::atan2 (loc.z, loc.x);
			const VGround *c = nullptr; // the ground asked a moment ago nearby: the height only gates tiles and reach
			for (const VGround &q : vground) if (q.h == l.h && q.p == &p) c = &q;
			bool reuse = c && (loc - c->loc).length () < COLL_BLDG_VMOVE && simt >= c->t && simt - c->t < COLL_BLDG_VAGE;
			VGround now = reuse ? *c : VGround { l.h, &p, loc, Elev (p.h, lng, lat), simt };
			vg.push_back (now);
			double alt = rl - p.R - now.g;
			ves.push_back (Ves { &p, loc, alt, l.r, speed, COLL_BLDG_REACH + COLL_BLDG_REACH_S * speed });
			break;
		}
	}
	vground.swap (vg);
	double sys = sdk.SysTime ();
	if (sys >= tWant) { // every tile near a low vessel, every half second
		tWant = sys + COLL_BLDG_WANT;
		FixedPass (); // before Load: a new tile's Setup sees the records
		for (const Ves &x : ves) if (x.alt < COLL_BLDG_ALT) Want (x);
	}
	Load ();
	for (auto it = tiles.begin (); it != tiles.end (); ) { // tiles no vessel is near
		Tile &t = *it->second;
		bool keep = false;
		for (const Ves &x : ves) keep = keep || (x.p == t.p && (x.loc - t.rposP).length () - t.rad - x.r < COLL_BLDG_KEEP);
		if (keep) { ++it; continue; }
		Drop (t);
		slotOf.erase (t.key);
		it = tiles.erase (it);
	}
	// building: buildings within reach of a vessel, nearest first, a vertex budget past COLL_BLDG_NEAR
	std::vector<Cand> cand;
	for (auto &kv : tiles) {
		Tile &t = *kv.second;
		for (const Ves &x : ves) {
			if (x.p != t.p) continue;
			Vector q = tmul (t.rrotP, x.loc - t.rposP);
			if (std::hypot (q.x, q.z) - t.rad - x.r > x.reach) continue;
			double ext = x.reach + x.r + t.maxRb;
			if (!std::isfinite (q.x) || !std::isfinite (q.z) || !std::isfinite (ext)) continue;
			int64_t cx0 = Cell (q.x - ext), cx1 = Cell (q.x + ext), cz0 = Cell (q.z - ext), cz1 = Cell (q.z + ext);
			auto scan = [&] (const std::vector<uint32_t> &in) {
				for (uint32_t i : in) {
					Bldg &b = t.b[i];
					if (b.state != Bldg::IDLE) continue;
					double gap = Gap (t, i, q, x.alt, x.r);
					if (!(gap < x.reach)) continue;
					if (b.stamp != frameNo) { b.stamp = frameNo; b.best = gap; cand.push_back (Cand { &t, i }); }
					else b.best = std::min (b.best, gap);
				}
			};
			if ((double)(cx1 - cx0 + 1) * (double)(cz1 - cz0 + 1) > (double)t.grid.size ()) { // window wider than the tile's cells: walk the cells (never more work than the tile holds)
				for (const auto &kv2 : t.grid) {
					int64_t cx = kv2.first >> 32, cz = (int32_t)(kv2.first & 0xFFFFFFFF);
					if (cx >= cx0 && cx <= cx1 && cz >= cz0 && cz <= cz1) scan (kv2.second);
				}
			}
			else for (int64_t cx = cx0; cx <= cx1; cx++)
				for (int64_t cz = cz0; cz <= cz1; cz++) {
					auto f = t.grid.find (CellKey (cx, cz));
					if (f != t.grid.end ()) scan (f->second);
				}
		}
	}
	if (!cand.empty ()) {
		std::sort (cand.begin (), cand.end (), [] (const Cand &a, const Cand &c) {
			double ga = a.t->b[a.i].best, gc = c.t->b[c.i].best;
			return ga != gc ? ga < gc : (a.t->slot != c.t->slot ? a.t->slot < c.t->slot : a.i < c.i); });
		Clock::time_point a = Clock::now ();
		uint32_t used = 0;
		for (const Cand &c : cand) {
			const CollBldgRec &R = c.t->tile.b[c.i];
			if (c.t->b[c.i].best >= COLL_BLDG_NEAR && used > 0 && (used + R.nv > COLL_BLDG_BUDGET || fElevN >= COLL_BLDG_ELEVS)) break; // the rest in the next frames
			used += Activate (*c.t, c.i, simt, Elev (c.t->p->h, R.lng, R.lat));
		}
		double ms = Ms (Clock::now () - a);
		st.buildMsMax = std::max (st.buildMsMax, ms);
		st.buildVtxMax = std::max (st.buildVtxMax, used);
	}
	// this frame's parts: built buildings within reach of a vessel; ground checks; freeing those far from every vessel
	struct VQ { Vector q; double alt, r, reach; };
	std::vector<VQ> vq;
	auto touches = [] (const CollGeom &G, const VQ &w) { return (G.bsCentre - w.q).length () < G.bsRadius + w.r + 2.0 * COLL_SKIN_MAX; }; // a vessel bound on or in its bounding sphere
	for (auto &kv : tiles) {
		Tile &t = *kv.second;
		vq.clear ();
		for (const Ves &x : ves) if (x.p == t.p) vq.push_back (VQ { tmul (t.rrotP, x.loc - t.rposP), x.alt, x.r, x.reach });
		Body B { t.p->idx, t.p->h, t.slot, t.rposP, t.rrotP, 0.0, false, {} };
		for (size_t k = 0; k < t.active.size (); ) {
			uint32_t i = t.active[k];
			Bldg &b = t.b[i];
			if (b.state != Bldg::BUILT) { t.active[k] = t.active.back (); t.active.pop_back (); continue; }
			bool part = false, far = true;
			for (const VQ &w : vq) {
				double gap = Gap (t, i, w.q, w.alt, w.r);
				part = part || gap < w.reach;
				far = far && gap > 2.0 * w.reach;
			}
			if (!far) b.farSince = -1;
			else if (b.farSince < 0) b.farSince = simt;
			else if (simt - b.farSince >= COLL_BLDG_IDLE) { Free (t, i); t.active[k] = t.active.back (); t.active.pop_back (); continue; }
			if (part && simt >= b.tGround && fElevN < COLL_BLDG_ELEVS) { // the core's terrain refines: rebuilt when its ground moved (BldgMgr::Reground)
				const CollBldgRec &R = t.tile.b[i];
				double g = Elev (t.p->h, R.lng, R.lat);
				b.tGround = simt + COLL_BLDG_GROUND;
				if (std::fabs (g - b.g) > COLL_BLDG_MOVE) {
					bool under = false; // a vessel on or in it would be inside the moved shape: the rebuild waits for the next check
					for (const VQ &w : vq) under = under || touches (*b.geom, w);
					if (under) st.deferred++;
					else {
						Activate (t, i, simt, g);
						st.rebuilt++;
						if (b.state != Bldg::BUILT) { t.active[k] = t.active.back (); t.active.pop_back (); continue; }
					}
				}
			}
			if (!part) { b.wasPart = false; k++; continue; }
			const CollGeom &G = *b.geom;
			if (!b.wasPart) // a building gained this frame inside a vessel's bound: the entry check opens GRACE, no deep impact (CollDetect.cpp EntryCheck)
				for (const VQ &w : vq) B.entry = B.entry || touches (G, w);
			b.wasPart = true;
			B.part.push_back (Part { &G, b.version, i });
			B.rmax = std::max (B.rmax, G.bsCentre.length () + G.bsRadius);
			k++;
		}
		if (!B.part.empty ()) bodies.push_back (std::move (B));
	}
	st.frames++;
	st.elevCalls += fElevN;
	st.elevMs += fElevMs;
	st.elevMsMax = std::max (st.elevMsMax, fElevMs);
	st.elevCallsMax = std::max (st.elevCallsMax, fElevN);
}
