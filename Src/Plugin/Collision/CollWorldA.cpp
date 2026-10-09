// not upstream: collision addon E1 1, 6.3-6.7, 9-11: snapshot, solver bodies, write-back sequence, notices, warp guard through CollSdk
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "CollWorldA.h"
#include "CollisionAPI.h"

namespace {
constexpr double G_GRAV = 6.67259e-11;                    // GGRAV (OrbiterAPI.h:60)
constexpr int OBJTP_PLANET_W = 4;                         // OBJTP_PLANET (OrbiterAPI.h:1773)
Matrix QM (const Quaternion &q) { Matrix R; R.Set (q); return R; }
Vector EulerOf (const Matrix &R)                          // inverse of Vessel::SetGlobalOrientation (Vessel.cpp:887-893)
{
	return Vector (std::atan2 (R(1,2), R(2,2)), -std::asin (std::max (-1.0, std::min (1.0, R(0,2)))), std::atan2 (R(0,1), R(0,0)));
}
VECTOR3 V3 (const Vector &v) { VECTOR3 r; r.x = v.x; r.y = v.y; r.z = v.z; return r; }
}

Vector CollSurfaceVel (const Vector &pp, const Vector &pv, const Vector &w, const Vector &x) { return pv + crossp (x - pp, w); }

Vector CollSurfaceAcc (const Vector &pp, const Vector &ap, const Vector &w, const Vector &x) { return ap + crossp (crossp (x - pp, w), w); }

void CollKinMotion (CollABody &B, const Vector &a, const Vector &w, double h)
{
	CollMotion &k = B.kin;
	k = CollMotion {};
	k.c0 = B.x; k.v0 = B.v; k.a0 = a;
	k.c1 = B.x + B.v*h + a*(0.5*h*h); k.v1 = B.v + a*h; k.a1 = a;
	k.q0 = B.q; k.q1 = B.q;
	Matrix R; R.Set (B.q);
	CollRotate (k.q1, tmul (R, w)*h);
	k.w0g = k.w1g = w; k.h = h; k.ta = 0; k.tb = 1; k.a0ok = true;
}

CollPhysSession::CollPhysSession (CollSdk &s, CollPhysGeom &g, const CollCfgValues &c) : sdk (s), geom (g), cfg (c)
{
	frame.check = cfg.check;
}

void CollPhysSession::Log (int level, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (buf, sizeof (buf), fmt, ap);
	va_end (ap);
	sdk.Log (level, buf);
}

void CollPhysSession::Start (const std::function<bool (const char *, std::string &)> &orbCfg)
{
	if (orbCfg) mirror.ReadCfg (orbCfg);
	frame.hRest = mirror.HRest ();
	started = true;
	Log (1, "Collision prop: %s", mirror.Describe ().c_str ());
}

void CollPhysSession::OnNewVessel (uint32_t id) { queuedNew.push_back (id); }

void CollPhysSession::OnDeleteVessel (uint32_t id)
{
	frame.OnDelete (id);
	fwd.Pairs ().PurgeVessel (id); ver.Pairs ().PurgeVessel (id);
	snap.erase (std::remove_if (snap.begin (), snap.end (), [id] (const Snap &s) { return s.id == id; }), snap.end ());
	last.erase (std::remove_if (last.begin (), last.end (), [id] (const Last &l) { return l.id == id; }), last.end ());
	for (CollImpactEvent &e : ev) for (CollImpactSide &s : e.s) if (s.owner.vesselId == id) s.owner.vesselId = 0xFFFFFFFFu; // no notice to it
	fxc.erase (std::remove_if (fxc.begin (), fxc.end (), [id] (const CollFxContact &c) { return c.id == id; }), fxc.end ());
}

void CollPhysSession::OnTimeJump ()
{
	frame.OnTimeJump ();
	fwd.Pairs ().FlushFront ();
	ver.Pairs ().Clear ();
	for (const Snap &s : snap) jumped.push_back (s.id);
}

void CollPhysSession::OnVesselJump (uint32_t id) { jumped.push_back (id); }

void CollPhysSession::OnTimeAccChanged (double newWarp)
{
	if (!started || inClamp || !(newWarp > lastW)) return;
	inClamp = true;
	sdk.SetWarp (lastW);
	inClamp = false;
	Log (1, "Collision warp: %g -> %g (clamp)", newWarp, lastW);
}

const CollPhysSession::Snap *CollPhysSession::SnapOf (uint32_t id) const
{
	for (const Snap &s : snap) if (s.id == id) return &s;
	return nullptr;
}

// PS1: every vessel read once before any write (1.2); no weight read here
void CollPhysSession::PS1Snapshot (double t, double dt, const std::vector<CollPhysVessel> &v)
{
	simt = t; simdt = dt;
	snap.clear ();
	for (const CollPhysVessel &x : v) {
		if (!x.h || !sdk.IsVessel (x.h)) continue;
		Snap s { x.id, x.h, CollVesselRead {} };
		sdk.ReadVessel (x.h, s.rd, CVR_NOWEIGHT);
		snap.push_back (s);
	}
}

// PS3: solver bodies (1.4), entry bits (1.5), the frame driver (5.3), the write sequence (6.4)
void CollPhysSession::PS3Physics (CollSolveHost &host)
{
	ev.clear (); wr.clear (); fxc.clear ();
	if (!started || cfg.model == 0 || !(simdt > 0.0)) return;    // simdt == 0: nothing (1.2)
	bool anyLive = false;
	for (const Snap &s : snap) anyLive = anyLive || !s.rd.playback;
	if (!anyLive) return;                                          // global playback: no physics pass (6.6)
	std::vector<CollPhysAsm> asmb;
	geom.Assemblies (asmb);
	std::vector<char> used (snap.size (), 0);
	for (const CollPhysAsm &a : asmb) for (uint32_t id : a.member) for (size_t i = 0; i < snap.size (); i++) if (snap[i].id == id) used[i] = 1;
	for (size_t i = 0; i < snap.size (); i++) if (!used[i]) { CollPhysAsm a; a.member = { snap[i].id }; a.root = snap[i].id; a.memberHash = snap[i].id; asmb.push_back (a); }
	body.clear ();
	std::vector<std::vector<uint32_t>> owners;
	for (const CollPhysAsm &a : asmb) {
		const Snap *r = SnapOf (a.root);
		if (!r) continue;
		CollABody B;
		B.member = a.member;
		std::stable_partition (B.member.begin (), B.member.end (), [&] (uint32_t id) { return id == a.root; });
		B.id = *std::min_element (a.member.begin (), a.member.end ());
		B.memberHash = a.memberHash ? a.memberHash : a.root;
		B.stack = a.stack;
		const CollVesselRead &rd = r->rd;
		Matrix R = rd.R;
		if (a.stack) {                                             // stack CG state from the reference component (1.4)
			B.x = rd.x + mul (R, rd.svcg);
			B.v = rd.v + mul (R, crossp (rd.svcg, rd.w));
			B.m = 0;
			for (uint32_t id : a.member) if (const Snap *s = SnapOf (id)) B.m += s->rd.m;
		} else { B.x = rd.x; B.v = rd.v; B.m = rd.m; }
		B.q.Set (R); B.wb = rd.w;
		B.arot = rd.arot;
		B.aTot = rd.m > 0 ? mul (R, rd.aTot)/rd.m : Vector ();
		B.ground = rd.ground; B.thrust = rd.thrust;
		// inertia: members' diagonal PMI moved to the body CG in the reference frame, diagonal kept (6.5)
		Vector I;
		for (uint32_t id : a.member) {
			const Snap *s = SnapOf (id);
			if (!s) continue;
			Matrix Rr = transp (R)*s->rd.R;
			Vector d = tmul (R, s->rd.x - B.x), pi = s->rd.pmi*s->rd.m;
			for (int k = 0; k < 3; k++) {
				double ik = Rr(k,0)*Rr(k,0)*pi.x + Rr(k,1)*Rr(k,1)*pi.y + Rr(k,2)*Rr(k,2)*pi.z;
				double dd = d.length2 () - d.data[k]*d.data[k];
				I.data[k] += ik + (a.member.size () > 1 ? s->rd.m*dd : 0.0);
			}
		}
		B.pmi = B.m > 0 ? I/B.m : Vector (1, 1, 1);
		if (!a.stack) B.pmi = rd.pmi;                             // attachment tree: Orbiter turns the root alone with its own PMI (6.6)
		B.kind = a.mixed ? COLLB_FROZEN : rd.playback ? COLLB_PLAYBACK : (rd.status & 1) ? COLLB_LANDED : COLLB_DYNAMIC;
		// gravity estimate of a state write (6.3): point mass of the gravity reference
		if (rd.gref) {
			Vector pr, vr; Matrix Rr;
			sdk.GlobalState (rd.gref, pr, vr, Rr);
			Vector d = B.x - pr;
			double l = d.length ();
			if (l > 0) B.gEst = d*(-G_GRAV*sdk.Mass (rd.gref)/(l*l*l));
		}
		// parts of every member in the body frame
		B.rmax = 0;
		for (uint32_t id : a.member) {
			const Snap *s = SnapOf (id);
			if (!s) continue;
			std::vector<CollPartRef> pf, pp;
			double rm = 0;
			if (!geom.Parts (id, pf, pp, rm)) continue;
			CollAffine T { transp (R)*s->rd.R, tmul (R, s->rd.x - B.x) };
			for (CollPartRef &p : pf) { p.P0 = CollCompose (T, p.P0); p.P1 = CollCompose (T, p.P1); B.parts.push_back (p); }
			for (CollPartRef &p : pp) { p.P0 = CollCompose (T, p.P0); p.P1 = CollCompose (T, p.P1); B.pastParts.push_back (p); }
			B.rmax = std::max (B.rmax, T.t.length () + rm);
		}
		if (B.parts.empty ()) continue;
		// kinematic motion over the step (4.3)
		if (B.kind != COLLB_DYNAMIC) {
			Vector wg = mul (R, B.wb), a = B.kind == COLLB_PLAYBACK && rd.gref && !(rd.status & 1) ? B.gEst : B.aTot; // playback in flight: FRecorder_Play leaves acc alone (Vessel.cpp:4742), gravity of gref; landed playback and frozen: the cache
			if (B.kind == COLLB_LANDED && rd.gref) {               // planet-fixed: rotation about the planet axis, the landed cache acc (Vessel.cpp:4759)
				double T = sdk.PlanetPeriod (rd.gref);
				Vector pr, vr; Matrix Rp;
				sdk.GlobalState (rd.gref, pr, vr, Rp);
				wg = std::fabs (T) > 0 ? mul (Rp, Vector (0, 1, 0))*(2.0*3.14159265358979323846/T) : Vector ();
				a = B.aTot;
				B.wakeable = true; B.wakeV = B.v; B.wakeWb = B.wb;
			}
			CollKinMotion (B, a, wg, simdt);
		}
		// entry bits (1.5)
		const Last *l = nullptr;
		for (const Last &x : last) if (x.id == B.id) l = &x;
		if (!l) B.entry |= COLLE_NEW;
		else {
			B.groundNew = B.ground && !l->ground;                  // terrain re-run of the last step (2.5)
			if (l->hash != B.memberHash) B.entry |= COLLE_MEMBERS;
			if (l->kind != COLLB_DYNAMIC && B.kind == COLLB_DYNAMIC) B.entry |= COLLE_ACTIVATED;
		}
		for (uint32_t id : a.member) {
			if (std::find (queuedNew.begin (), queuedNew.end (), id) != queuedNew.end ()) B.entry |= COLLE_NEW;
			if (std::find (jumped.begin (), jumped.end (), id) != jumped.end ()) B.entry |= COLLE_JUMP;
		}
		body.push_back (B);
	}
	std::vector<CollABody> bases;
	geom.Bases (simdt, bases);
	body.insert (body.begin (), bases.begin (), bases.end ());
	std::vector<CollZone> zones;
	if (cfg.dockZone || cfg.attachZone) geom.Zones (body, simdt, zones);
	std::vector<std::pair<uint32_t, uint32_t>> np;                 // dmg3 L5: vessel ids to body ids (a docked vessel's body)
	for (const auto &x : noPairV) {
		uint32_t ba = 0, bb = 0; bool fa = false, fb = false;
		for (const CollABody &B : body) {
			if (B.kind == COLLB_BASE) continue;
			if (std::find (B.member.begin (), B.member.end (), x.first) != B.member.end ()) ba = B.id, fa = true;
			if (std::find (B.member.begin (), B.member.end (), x.second) != B.member.end ()) bb = B.id, fb = true;
		}
		if (fa && fb && ba != bb) np.push_back ({ ba, bb });
	}
	fwd.SetNoPair (np); ver.SetNoPair (np);
	frame.Run (fwd, ver, mirror, body, zones, simdt, simt, host, wr, ev);
	for (const CollContactRec &r : frame.contacts)                 // dmg3 L4: vessel sides in the vessel frame
		for (int side = 0; side < 2; side++) {
			const CollImpactSide &s = r.s[side], &o = r.s[1 - side];
			const Snap *x = s.owner.vesselId ? SnapOf (s.owner.vesselId) : nullptr;
			if (!x) continue;
			CollFxContact c;
			c.id = x->id; c.h = x->h;
			c.c = s.c; c.n = s.n;
			geom.PartToVessel (c.id, s.mesh, s.grp, c.c, c.n);
			if (s.tdir.length () > 0.0) {
				Vector p = s.c, t = s.tdir;
				geom.PartToVessel (c.id, s.mesh, s.grp, p, t);
				t -= c.n*dotp (t, c.n);
				double l = t.length ();
				c.tdir = l > 1e-12 ? t/l : Vector ();
			}
			c.vn = r.vn; c.vt = r.vt; c.Jn = r.Jn; c.Jt = r.Jt; c.dt = r.dt; c.flags = r.flags;
			c.building = !o.owner.vesselId;
			if (c.building) c.nOther = o.n;
			c.playback = x->rd.playback;
			fxc.push_back (c);
		}
	last.clear ();
	for (const CollABody &B : body) if (B.kind != COLLB_BASE) last.push_back (Last { B.id, B.kind, B.memberHash, B.ground });
	queuedNew.clear (); jumped.clear ();
	if (!cfg.response) { frame.OnTimeJump (); return; }       // detect and log only (1.1)
	WriteBack (wr);
}

void CollPhysSession::WriteBack (std::vector<CollAWrite> &wl)
{
	// pass 1: position and attitude, then the weight of a single body written with SetState (6.4)
	std::vector<Vector> &gExact = gx;
	gExact.assign (body.size (), Vector ());
	for (size_t i = 0; i < body.size (); i++) gExact[i] = body[i].gEst;
	for (const CollAWrite &w : wl) {
		const CollABody &B = body[w.body];
		const Snap *r = SnapOf (B.member[0]);
		if (!r || r->rd.playback) continue;
		Matrix R = QM (w.q);
		CollVesselRead rw {};
		if (w.weight) sdk.ReadVessel (r->h, rw, 0);           // before the write: the core's weight is cached until its next Update, SetState2 and SetRotationMatrix keep it (Vessel.cpp:1240-1246, 4943)
		if (w.state) {
			CollStateWrite s {};
			s.rbody = r->rd.gref;
			Vector xr, vr; Matrix Rr;
			if (s.rbody) sdk.GlobalState (s.rbody, xr, vr, Rr);
			Vector xc = B.stack ? w.x - mul (r->rd.R, r->rd.svcg) : w.x; // RPlace takes the CG with the stack's current rotation (SuperVessel.cpp:309-317), SetAttitude turns about it
			s.rpos = xc - xr; s.rvel = w.v - vr; s.vrot = w.wb; s.arot = EulerOf (R);
			if (s.rbody && sdk.ObjType (s.rbody) == OBJTP_PLANET_W) {   // below the terrain the core lifts the vessel (DefSetStateEx): log once per vessel, keep the write
				Vector loc = tmul (Rr, s.rpos);
				double rl = loc.length ();
				double lng = std::atan2 (loc.z, loc.x), lat = rl > 0 ? std::asin (std::max (-1.0, std::min (1.0, loc.y/rl))) : 0.0;
				double rp = sdk.Size (s.rbody);
				bool near = rl < rp + 25e3 && std::find (belowLogged.begin (), belowLogged.end (), B.member[0]) == belowLogged.end ();  // elevation queried only within 25 km of the radius
				double rt = near ? rp + sdk.Elevation (s.rbody, lng, lat) : rp;
				if (near && rl < rt) {
					belowLogged.push_back (B.member[0]);
					Log (1, "Collision: state write of '%s' %.3f m below the terrain; Orbiter lifts it to the surface", sdk.Name (r->h).c_str (), rt - rl);
				}
			}
			sdk.SetState (r->h, s);
		}
		if (w.attitude) sdk.SetAttitude (r->h, R);
		if (w.weight && rw.m > 0) gExact[w.body] = mul (r->rd.R, rw.W)/rw.m; // the core's full weight in the frame it was cached in: R_new (R_new^T R_old W) / m
	}
	frame.Finish (mirror, body, wl, gExact);
	// pass 2: spin and force; they do not touch the weight's inputs
	for (const CollAWrite &w : wl) {
		const CollABody &B = body[w.body];
		const Snap *r = SnapOf (B.member[0]);
		if (!r || r->rd.playback) continue;
		if (w.spin) sdk.SetSpin (r->h, w.wb);
		if (w.Fb.length () > 0) sdk.AddForce (r->h, w.Fb, B.stack ? r->rd.svcg : Vector ()); // at the stack CG: no lever torque (SuperVessel.cpp:780-786)
		double ml = w.Mb.length ();
		if (ml > 0) {
			Vector a = std::fabs (w.Mb.x) < 0.6*ml ? Vector (1, 0, 0) : Vector (0, 1, 0);
			Vector r1 = Xc (w.Mb, a); r1 /= r1.length ();
			Vector u = Xc (w.Mb, r1)*0.5;
			sdk.AddForce (r->h, u, r1); sdk.AddForce (r->h, -u, -r1);
		}
	}
}

// PS5: one CONTACT notice per event and vessel side (11); the list is built before any module code runs
void CollPhysSession::PS5Notices ()
{
	struct N { uint32_t id; COLLA_CONTACTINFO info; };
	std::vector<N> list;
	for (const CollImpactEvent &e : ev)
		for (int side = 0; side < 2; side++) {
			const CollImpactSide &s = e.s[side], &o = e.s[1 - side];
			if (!s.owner.vesselId || s.owner.vesselId == 0xFFFFFFFFu) continue;
			N n {};
			n.id = s.owner.vesselId;
			COLLA_CONTACTINFO &c = n.info;
			c.hdr.magic = COLLA_MAGIC; c.hdr.version = COLLA_VERSION; c.hdr.kind = COLLA_KIND_CONTACT; c.hdr.size = sizeof (COLLA_CONTACTINFO);
			c.flags = (o.owner.vesselId ? COLLA_CON_VESSEL : COLLA_CON_BUILDING) | COLLA_CON_FIRST;
			if (e.flags & COLLEV_SLOW) c.flags |= COLLA_CON_SLOW;
			if (e.flags & COLLEV_WOKE_LANDED)                    // only the side that was woken (11)
				for (const CollABody &B : body)
					if (B.woke && std::find (B.member.begin (), B.member.end (), s.owner.vesselId) != B.member.end ()) c.flags |= COLLA_CON_WOKE;
			if (o.owner.vesselId) { const Snap *x = SnapOf (o.owner.vesselId); c.hOther = x ? (OBJHANDLE)x->h : nullptr; c.otherObj = -1; }
			else { c.hOther = (OBJHANDLE)geom.BaseHandle (o.owner.planet, o.owner.base); c.otherObj = o.owner.obj; }
			c.mesh = s.mesh; c.group = s.grp; c.reserved = 0;
			c.simt = e.t;
			Vector p = s.c, nn = s.n;
			geom.PartToVessel (n.id, s.mesh, s.grp, p, nn);
			c.pos = V3 (p); c.nml = V3 (nn);
			c.vn = e.vn; c.vt = e.vt; c.J = e.Jn; c.dE = std::max (0.0, e.dKE);
			list.push_back (n);
		}
	for (N &n : list) {
		const Snap *x = SnapOf (n.id);
		if (!x || !sdk.IsVessel (x->h)) continue;                 // resolved right before the call
		int reply = 0;
		if (sdk.Notify (x->h, COLLA_KIND_CONTACT, &n.info, reply)) nNotices++;
	}
}

// PS6: W from the look-ahead and load caps against the larger of this frame's and the recent longest frame time (9.2)
void CollPhysSession::PS6Warp ()
{
	if (!started || !(simdt > 0.0)) return;
	double warp = sdk.Warp (), sys = sdk.SysTime ();
	double dt = warp > 0 ? simdt/warp : simdt;
	frameDt.push_back ({ sys, dt });
	frameDt.erase (std::remove_if (frameDt.begin (), frameDt.end (), [sys] (const std::pair<double, double> &x) { return sys - x.first > 2.0; }), frameDt.end ());
	double dtRef = dt;
	for (const auto &x : frameDt) dtRef = std::max (dtRef, x.second);
	double W = CollWarpAllowed (frame.WarpIn (), warp, dtRef);
	lastW = W;
	if (warp > W && warp > 1.0) {
		inClamp = true;
		sdk.SetWarp (W);
		inClamp = false;
		if (sys - warpMsgSys >= 2.0) {
			warpMsgSys = sys;
			char m[128];
			std::snprintf (m, sizeof (m), "Collision warp: %g -> %g (%s)", warp, W, frame.WarpIn ().hLoad < 1e99 ? "load" : "contact");
			sdk.Notification (COLLN_INFO, "Collision", m);
			sdk.Log (1, m);
		}
	}
}

void CollPhysSession::End ()
{
	snap.clear (); body.clear (); ev.clear (); wr.clear (); last.clear (); queuedNew.clear (); jumped.clear (); belowLogged.clear ();
	frame.Reset (); fwd.Reset (); ver.Reset ();
	started = false;
}
