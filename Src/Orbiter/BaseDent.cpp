// not upstream: building dents and holes drawn by the core with the collision add-on's DentMath (design coll-bounded 5.7); built with the add-on's headers only
#ifdef __VECMAT_H
#error "BaseDent.cpp must see the collision add-on's Vecmat.h only"
#endif
#include <cstring>
#include "BaseDent.h"
#include "DentMath.h"

static_assert (sizeof (BaseDentVtx) == sizeof (DentVtx), "BaseDentVtx keeps the DentVtx layout");

static DentParams Params (const BaseDentRec &r)
{
	DentParams p {};
	p.c = Vector (r.c[0], r.c[1], r.c[2]); p.n = Vector (r.n[0], r.n[1], r.n[2]); p.t = Vector (r.t[0], r.t[1], r.t[2]);
	p.R = r.R; p.h = r.h; p.T = r.T; p.P = r.P; p.hd = r.hd; p.hz = r.hz;
	p.mode = r.mode; p.seed = r.seed; p.bits = r.bits;
	return p;
}

void BaseDentApply (const std::vector<BaseDentGroup> &rest, const std::vector<BaseDentRec> &rec, std::vector<BaseDentGroup> &out)
{
	DentObject o;
	for (const BaseDentGroup &g : rest) {
		std::vector<DentVtx> v (g.vtx.size ());
		if (!v.empty ()) std::memcpy (v.data (), g.vtx.data (), v.size () * sizeof (DentVtx));
		o.rest.push_back (v);
		o.cur.push_back (v);
		o.idx.push_back (g.idx);
	}
	o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
	std::vector<DentParams> pri;
	std::vector<DentRecord> rr;
	for (const BaseDentRec &r : rec) {
		DentParams p = Params (r);
		if (!(r.flags & DENTR_NOREFINE)) { // as the add-on's collider, within the room the merged groups leave
			std::vector<uint32_t> left (o.rest.size (), 0);
			for (size_t g = 0; g < o.rest.size (); g++) {
				size_t added = o.rest[g].size () - rest[g].vtx.size ();
				left[g] = rest[g].room > added ? rest[g].room - (uint32_t)added : 0;
			}
			DentMath::Refine (o, p.c, p.R, pri.data (), pri.size (), left.data ());
		}
		pri.push_back (p);
		DentRecord d {};
		d.p = p;
		rr.push_back (d);
	}
	std::vector<const DentParams *> pl;
	for (const DentParams &p : pri) pl.push_back (&p);
	std::vector<Vector> restSum;
	DentMath::FaceNormalSums (o.rest, o.idx, o.weld, o.nweld, restSum);
	std::vector<uint8_t> touched (o.nweld, 0);
	for (size_t g = 0; g < o.rest.size (); g++) {
		o.cur[g] = o.rest[g];
		for (size_t v = 0; v < o.rest[g].size (); v++) {
			const DentVtx &R = o.rest[g][v];
			Vector p (R.x, R.y, R.z), d = DentMath::Fold (pl.data (), pl.size (), p, true), q = p + d;
			o.cur[g][v].x = (float)q.x, o.cur[g][v].y = (float)q.y, o.cur[g][v].z = (float)q.z;
			if (d.x != 0.0 || d.y != 0.0 || d.z != 0.0) touched[o.weld[g][v]] = 1;
		}
	}
	DentMath::Normals (o.rest, restSum, o.idx, o.weld, o.nweld, touched, o.cur);
	out.assign (o.rest.size (), BaseDentGroup ());
	for (size_t g = 0; g < o.rest.size (); g++) {
		BaseDentGroup &G = out[g];
		G.room = 0;
		G.vtx.resize (o.cur[g].size ());
		if (!G.vtx.empty ()) std::memcpy (G.vtx.data (), o.cur[g].data (), G.vtx.size () * sizeof (DentVtx));
		const std::vector<uint16_t> &I = o.idx[g];
		const std::vector<DentVtx> &R = o.rest[g];
		for (size_t t = 0; t + 2 < I.size (); t += 3) {
			if (I[t] >= R.size () || I[t + 1] >= R.size () || I[t + 2] >= R.size ()) continue;
			const DentVtx &a = R[I[t]], &b = R[I[t + 1]], &c = R[I[t + 2]];
			if (!DentMath::HoleKeeps (rr, Vector (a.x, a.y, a.z), Vector (b.x, b.y, b.z), Vector (c.x, c.y, c.z))) continue;
			G.idx.insert (G.idx.end (), { I[t], I[t + 1], I[t + 2] });
		}
	}
}
