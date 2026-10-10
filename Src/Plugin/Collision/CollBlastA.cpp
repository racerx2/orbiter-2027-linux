// not upstream: collision addon, blast: Voronoi cells of one vessel slot, NVIDIA Blast asset, family, actors and stress solver (design-CA-blast 2)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include "CollBlastA.h"
#if !defined (NDEBUG) && !defined (_DEBUG)
#define NDEBUG // NvPreprocessor.h needs exactly one of them
#endif
#include "NvBlast.h"
#include "NvBlastExtDamageShaders.h"
#include "NvBlastExtStressSolver.h"

uint64_t CollBlastA::logErrors = 0;

namespace {

double Q9 (double v)
{
	char b[40];
	snprintf (b, sizeof b, "%.9g", v);
	return strtod (b, nullptr);
}

Vector Unit (const Vector &v) { double l = v.length (); return l > 0 ? v / l : v; }

NvcVec3 N3 (const Vector &v) { return NvcVec3 { (float)v.x, (float)v.y, (float)v.z }; }

void BlastLog (int type, const char *, const char *, int)
{
	if (type <= NvBlastMessage::Warning) CollBlastA::logErrors++;
}

Vector Corner (const CollBlastInput &in, size_t t, int k) { return in.v[3 * t + k]; }

}

void *CollBlastA::Buf::Get (size_t n)
{
	b.assign (n + 16, 0);
	uintptr_t p = (uintptr_t)b.data ();
	return (void *)((p + 15) & ~(uintptr_t)15);
}

CollBlastA::~CollBlastA () { Release (); }

void CollBlastA::Release ()
{
	if (solver) solver->release ();
	if (accel) accel->release ();
	solver = nullptr; accel = nullptr; main = nullptr; family = nullptr; asset = nullptr;
	assetMem.b.clear (); familyMem.b.clear (); scratch.b.clear ();
}

size_t CollBlastA::SiteCount (const CollBlastInput &in)
{
	size_t nt = in.v.size () / 3, ns = 0;
	double A = 0, Ac = (BLAST_CELL * in.size) * (BLAST_CELL * in.size);
	for (size_t t = 0; t < nt; t++) if (in.piece[t] < 0) {
		A += 0.5 * crossp (Corner (in, t, 1) - Corner (in, t, 0), Corner (in, t, 2) - Corner (in, t, 0)).length ();
		ns++;
	}
	if (!ns || !(Ac > 0)) return 0;
	long n = std::lround (A / Ac);
	n = std::max<long> (BLAST_MIN_CELLS, std::min<long> (n, std::max (BLAST_MIN_CELLS, std::min (in.maxCells, BLAST_MAX_CELLS))));
	return std::min ((size_t)n, ns);
}

std::vector<Vector> CollBlastA::Sites (const CollBlastInput &in)
{
	size_t nt = in.v.size () / 3, n = SiteCount (in);
	std::vector<Vector> cen;
	Vector m; double A = 0;
	for (size_t t = 0; t < nt; t++) if (in.piece[t] < 0) {
		Vector a = Corner (in, t, 0), b = Corner (in, t, 1), c = Corner (in, t, 2);
		double ar = 0.5 * crossp (b - a, c - a).length ();
		Vector x = (a + b + c) / 3.0;
		cen.push_back (x); m += x * ar; A += ar;
	}
	std::vector<Vector> s;
	if (!n || cen.empty ()) return s;
	if (A > 0) m /= A;
	size_t first = 0; double best = -1;
	for (size_t i = 0; i < cen.size (); i++) { double d = (cen[i] - m).length2 (); if (d > best) best = d, first = i; }
	std::vector<double> dmin (cen.size (), 1e300);
	size_t k = first;
	while (s.size () < n) {
		s.push_back (cen[k]);
		for (size_t i = 0; i < cen.size (); i++) dmin[i] = std::min (dmin[i], (cen[i] - cen[k]).length2 ());
		best = -1;
		for (size_t i = 0; i < cen.size (); i++) if (dmin[i] > best) best = dmin[i], k = i;
		if (!(best > 0)) break;
	}
	for (auto &x : s) x = Vector (Q9 (x.x), Q9 (x.y), Q9 (x.z));
	return s;
}

bool CollBlastA::Build (const CollBlastInput &in, const std::vector<Vector> *given, const Vector &cgIn)
{
	Release ();
	site.clear (); cellOf.clear (); chunk.clear (); bond.clear (); gone.clear (); nodeOf.clear (); force.clear (); spin = false; impD = 0;
	cg = cgIn;
	size_t nt = in.v.size () / 3;
	if (in.w.size () != in.v.size () || in.piece.size () != nt || !nt) return false;
	Acell = (BLAST_CELL * in.size) * (BLAST_CELL * in.size);
	site = given && !given->empty () ? *given : Sites (in);
	if (site.empty ()) return false;
	std::vector<double> ar (nt);
	Atotal = 0;
	for (size_t i = 0; i < nt; i++) { ar[i] = 0.5 * crossp (Corner (in, i, 1) - Corner (in, i, 0), Corner (in, i, 2) - Corner (in, i, 0)).length (); Atotal += ar[i]; }
	if (!(Atotal > 0)) return false;
	t = std::max (BLAST_T_MIN, std::min (BLAST_T_MAX, in.mass / (BLAST_RHO * Atotal)));
	cellOf.assign (nt, -1);
	std::map<int, uint32_t> cellChunk, pieceChunk;
	for (size_t i = 0; i < nt; i++) {
		if (in.piece[i] >= 0) { pieceChunk.emplace (in.piece[i], 0); continue; }
		Vector x = (Corner (in, i, 0) + Corner (in, i, 1) + Corner (in, i, 2)) / 3.0;
		int bi = 0; double bd = 1e300;
		for (size_t k = 0; k < site.size (); k++) { double d = (x - site[k]).length2 (); if (d < bd) bd = d, bi = (int)k; }
		cellOf[i] = bi;
		cellChunk.emplace (bi, 0);
	}
	for (auto &kv : cellChunk) { kv.second = (uint32_t)chunk.size (); chunk.emplace_back (); chunk.back ().cell = kv.first; }
	for (auto &kv : pieceChunk) { kv.second = (uint32_t)chunk.size (); chunk.emplace_back (); chunk.back ().piece = kv.first; }
	std::vector<uint32_t> chOf (nt);
	for (size_t i = 0; i < nt; i++) {
		chOf[i] = in.piece[i] >= 0 ? pieceChunk[in.piece[i]] : cellChunk[cellOf[i]];
		CollBlastChunk &c = chunk[chOf[i]];
		c.c += (Corner (in, i, 0) + Corner (in, i, 1) + Corner (in, i, 2)) * (ar[i] / 3.0);
		c.area += ar[i];
		for (int k = 0; k < 3; k++) c.pts.push_back (Corner (in, i, k));
	}
	for (auto &c : chunk) {
		if (c.area > 0) c.c /= c.area;
		c.mass = in.mass * c.area / Atotal;
	}
	// bonds: chunks sharing welded vertices; shared edge length
	std::map<uint32_t, Vector> wpos;
	std::map<uint32_t, std::set<uint32_t>> wch;
	std::map<std::pair<uint32_t, uint32_t>, std::pair<double, std::set<uint32_t>>> edge;
	for (size_t i = 0; i < nt; i++) for (int k = 0; k < 3; k++) {
		uint32_t a = in.w[3 * i + k], b = in.w[3 * i + (k + 1) % 3];
		wpos.emplace (a, Corner (in, i, k));
		wch[a].insert (chOf[i]);
		if (a == b) continue;
		auto &e = edge[{ std::min (a, b), std::max (a, b) }];
		e.first = (Corner (in, i, (k + 1) % 3) - Corner (in, i, k)).length ();
		e.second.insert (chOf[i]);
	}
	struct Acc { double len = 0; Vector c; uint32_t n = 0; };
	std::map<std::pair<uint32_t, uint32_t>, Acc> pair;
	for (auto &kv : wch) if (kv.second.size () > 1) {
		std::vector<uint32_t> l (kv.second.begin (), kv.second.end ());
		for (size_t i = 0; i < l.size (); i++) for (size_t j = i + 1; j < l.size (); j++) { Acc &a = pair[{ l[i], l[j] }]; a.c += wpos[kv.first]; a.n++; }
	}
	for (auto &kv : edge) if (kv.second.second.size () > 1) {
		std::vector<uint32_t> l (kv.second.second.begin (), kv.second.second.end ());
		for (size_t i = 0; i < l.size (); i++) for (size_t j = i + 1; j < l.size (); j++) pair[{ l[i], l[j] }].len += kv.second.first;
	}
	double lmin = 0.1 * std::sqrt (Acell);
	for (auto &kv : pair) {
		if (!kv.second.n) continue;
		CollBlastBond b;
		b.a = kv.first.first; b.b = kv.first.second; b.len = kv.second.len;
		b.area = std::max (kv.second.len, lmin) * t;
		b.c = kv.second.c / (double)kv.second.n;
		const CollBlastChunk &ca = chunk[b.a], &cb = chunk[b.b];
		b.n = Unit (ca.cell >= 0 && cb.cell >= 0 ? site[cb.cell] - site[ca.cell] : cb.c - ca.c);
		if (!(b.n.length () > 0)) b.n = Vector (1, 0, 0);
		bond.push_back (b);
	}
	// unwelded islands: each joins the nearest chunk of the connected set with a proximity bond
	{
		std::vector<uint32_t> comp (chunk.size ());
		for (size_t i = 0; i < comp.size (); i++) comp[i] = (uint32_t)i;
		auto F = [&] (uint32_t x) { while (comp[x] != x) x = comp[x] = comp[comp[x]]; return x; };
		for (auto &b : bond) { uint32_t x = F (b.a), y = F (b.b); if (x != y) comp[std::max (x, y)] = std::min (x, y); }
		double sp = std::sqrt (Acell);
		for (uint32_t k = 1; k < chunk.size (); k++) {
			if (F (k) == F (0) || F (k) != k) continue;
			uint32_t bi = 0, bj = 0; double bd = 1e300;
			for (uint32_t i = 0; i < chunk.size (); i++) if (F (i) == k) for (uint32_t j = 0; j < chunk.size (); j++) if (F (j) == F (0)) {
				double d = (chunk[i].c - chunk[j].c).length2 ();
				if (d < bd) bd = d, bi = i, bj = j;
			}
			CollBlastBond b;
			b.a = std::min (bi, bj); b.b = std::max (bi, bj); b.prox = true;
			double gap = std::max (0.0, std::sqrt (bd) - sp);
			b.len = std::max (lmin, sp - gap);
			b.area = b.len * t;
			b.c = (chunk[bi].c + chunk[bj].c) * 0.5;
			b.n = Unit (chunk[b.b].c - chunk[b.a].c);
			if (!(b.n.length () > 0)) b.n = Vector (1, 0, 0);
			bond.push_back (b);
			comp[k] = F (0);
		}
	}
	// asset, family, first actor
	std::vector<NvBlastChunkDesc> cd (chunk.size ());
	for (size_t i = 0; i < chunk.size (); i++) {
		cd[i].centroid[0] = (float)chunk[i].c.x; cd[i].centroid[1] = (float)chunk[i].c.y; cd[i].centroid[2] = (float)chunk[i].c.z;
		cd[i].volume = (float)std::max (chunk[i].area * t, 1e-9);
		cd[i].parentChunkDescIndex = UINT32_MAX; cd[i].flags = NvBlastChunkDesc::SupportFlag; cd[i].userData = (uint32_t)i;
	}
	std::vector<NvBlastBondDesc> bd (bond.size ());
	for (size_t i = 0; i < bond.size (); i++) {
		const CollBlastBond &b = bond[i];
		bd[i].bond.normal[0] = (float)b.n.x; bd[i].bond.normal[1] = (float)b.n.y; bd[i].bond.normal[2] = (float)b.n.z;
		bd[i].bond.area = (float)b.area;
		bd[i].bond.centroid[0] = (float)b.c.x; bd[i].bond.centroid[1] = (float)b.c.y; bd[i].bond.centroid[2] = (float)b.c.z;
		bd[i].bond.userData = (uint32_t)i;
		bd[i].chunkIndices[0] = b.a; bd[i].chunkIndices[1] = b.b;
	}
	NvBlastAssetDesc ad;
	ad.chunkCount = (uint32_t)cd.size (); ad.chunkDescs = cd.data (); ad.bondCount = (uint32_t)bd.size (); ad.bondDescs = bd.empty () ? nullptr : bd.data ();
	void *sc = scratch.Get (NvBlastGetRequiredScratchForCreateAsset (&ad, BlastLog));
	asset = NvBlastCreateAsset (assetMem.Get (NvBlastGetAssetMemorySize (&ad, BlastLog)), &ad, sc, BlastLog);
	if (!asset) { Release (); return false; }
	const uint32_t *c2n = NvBlastAssetGetChunkToGraphNodeMap (asset, BlastLog);
	nodeOf.assign (chunk.size (), UINT32_MAX);
	for (size_t i = 0; i < chunk.size (); i++) nodeOf[i] = c2n[i];
	NvBlastSupportGraph g = NvBlastAssetGetSupportGraph (asset, BlastLog);
	maxArea = 0;
	for (auto &b : bond) {
		uint32_t na = nodeOf[b.a], nb = nodeOf[b.b];
		b.asset = UINT32_MAX;
		maxArea = std::max (maxArea, b.area);
		if (na >= g.nodeCount) continue;
		for (uint32_t k = g.adjacencyPartition[na]; k < g.adjacencyPartition[na + 1]; k++) if (g.adjacentNodeIndices[k] == nb) b.asset = g.adjacentBondIndices[k];
	}
	sigY = BLAST_SIGMA_Y; sigU = BLAST_SIGMA_U;
	if (!Family ()) { Release (); return false; }
	meanMass = 0;
	for (auto &c : chunk) meanMass += c.area * t * BLAST_RHO;
	meanMass /= (double)chunk.size ();
	accel = NvBlastExtDamageAcceleratorCreate (asset, 1);
	return true;
}

bool CollBlastA::Family ()
{
	family = NvBlastAssetCreateFamily (familyMem.Get (NvBlastAssetGetFamilyMemorySize (asset, BlastLog)), asset, BlastLog);
	if (!family) return false;
	std::vector<float> health (std::max<size_t> (1, NvBlastAssetGetBondCount (asset, BlastLog)), 1.0f); // the stress solver reads bond health as the remaining area [m^2]
	for (auto &b : bond) if (b.asset != UINT32_MAX && b.asset < health.size ()) health[b.asset] = (float)b.area;
	NvBlastActorDesc acd;
	acd.uniformInitialBondHealth = 1.0f; acd.initialBondHealths = health.data (); acd.uniformInitialLowerSupportChunkHealth = 1.0f; acd.initialSupportChunkHealths = nullptr;
	void *sc = scratch.Get (NvBlastFamilyGetRequiredScratchForCreateFirstActor (family, BlastLog));
	main = NvBlastFamilyCreateFirstActor (family, &acd, sc, BlastLog);
	if (!main) return false;
	Nv::Blast::ExtStressSolverSettings st;
	st.maxSolverIterationsPerFrame = BLAST_ITER; st.graphReductionLevel = 0;
	solver = Nv::Blast::ExtStressSolver::create (*family, st);
	if (!solver) return false;
	solver->setAllNodesInfoFromLL ((float)BLAST_RHO);           // chunk volume = area * t: node mass = the skin mass
	Material (sigY, sigU);
	solver->notifyActorCreated (*main);
	gone.assign (chunk.size (), 0);
	return true;
}

bool CollBlastA::Reset ()
{
	if (!asset) return false;
	if (solver) solver->release ();
	solver = nullptr; main = nullptr; family = nullptr;
	force.clear (); spin = false; impD = 0; crP = 0;
	if (Family ()) return true;
	Release ();
	return false;
}

void CollBlastA::Material (double sy, double su)
{
	sigY = sy; sigU = su;
	if (!solver) return;
	Nv::Blast::ExtStressSolverSettings st = solver->getSettings ();
	st.compressionElasticLimit = (float)sy; st.compressionFatalLimit = (float)su;
	st.tensionElasticLimit = (float)(BLAST_TENSION * sy); st.tensionFatalLimit = (float)(BLAST_TENSION * su);
	st.shearElasticLimit = (float)(BLAST_SHEAR * sy); st.shearFatalLimit = (float)(BLAST_SHEAR * su);
	solver->setSettings (st);
}

void CollBlastA::Force (const Vector &c, const Vector &F) { if (F.length () > 0) force.push_back ({ c, F }); }

void CollBlastA::Spin (const Vector &c, const Vector &w_) { com = c; w = w_; spin = w_.length () > 0; }

void CollBlastA::Impact (const Vector &c, double R, double d) { impC = c; impR = R; impD = std::max (impD, std::min (1.0, d)); }

void CollBlastA::Crush (const Vector &c, const Vector &n, double P, double R, double ratio) { crC = c; crN = Unit (n); crP = P; crR = R; crRatio = ratio; }

std::vector<uint32_t> CollBlastA::Crushed (const Vector &c, const Vector &nIn, double P, double R, double ratio) const
{
	std::vector<uint32_t> r;
	Vector n = Unit (nIn);
	if (!main || !(P > 0) || !(R > 0) || !(n.length () > 0)) return r;
	for (uint32_t k = 0; k < chunk.size (); k++) {
		const CollBlastChunk &ch = chunk[k];
		if ((k < gone.size () && gone[k]) || ch.pts.empty ()) continue;
		Vector d = ch.c - c;
		double sc = -(d & n), lat2 = (d & d) - sc * sc;
		if (!(lat2 < R * R)) continue;                              // centroid inside the crush radius
		double smin = 1e300, smax = -1e300;
		for (const Vector &p : ch.pts) { double s = (c - p) & n; smin = std::min (smin, s); smax = std::max (smax, s); }
		double f = smax - smin > 1e-9 ? (P - smin) / (smax - smin) : (P >= smin ? 1.0 : 0.0); // share of the chunk's depth in front of the crush plane
		if (f >= ratio) r.push_back (k);
	}
	return r;
}

std::vector<uint32_t> CollBlastA::Chunks (const NvBlastActor *a) const
{
	std::vector<uint32_t> v (NvBlastActorGetVisibleChunkCount (a, BlastLog));
	if (!v.empty ()) v.resize (NvBlastActorGetVisibleChunkIndices (v.data (), (uint32_t)v.size (), a, BlastLog));
	std::sort (v.begin (), v.end ());
	return v;
}

void CollBlastA::Split (std::vector<CollBlastSplit> *out)
{
	if (!main || !NvBlastActorIsSplitRequired (main, BlastLog)) return;
	void *sc = scratch.Get (NvBlastActorGetRequiredScratchForSplit (main, BlastLog));
	std::vector<NvBlastActor *> na (std::max<uint32_t> (1, NvBlastActorGetMaxActorCountForSplit (main, BlastLog)));
	NvBlastActorSplitEvent ev { nullptr, na.data () };
	NvBlastActor *old = main;
	uint32_t n = NvBlastActorSplit (&ev, main, (uint32_t)na.size (), sc, BlastLog, nullptr);
	if (!n) return;
	solver->notifyActorDestroyed (*old);
	std::vector<std::vector<uint32_t>> ch (n);
	std::vector<double> m (n, 0);
	int best = -1; double bestD = 1e300;
	for (uint32_t i = 0; i < n; i++) {
		ch[i] = Chunks (na[i]);
		double d = 1e300;
		for (uint32_t c : ch[i]) { if (!gone[c]) m[i] += chunk[c].mass; d = std::min (d, (chunk[c].c - cg).length2 ()); }
		if (best < 0 || m[i] > m[best] || (m[i] == m[best] && d < bestD)) best = (int)i, bestD = d;
	}
	main = na[best];
	solver->notifyActorCreated (*main);
	std::vector<uint8_t> inMain (chunk.size (), 0);
	for (uint32_t c : ch[best]) inMain[c] = 1;
	Vector cm; double mm = 0;
	for (uint32_t c : ch[best]) cm += chunk[c].c * chunk[c].mass, mm += chunk[c].mass;
	if (mm > 0) cm /= mm;
	for (uint32_t i = 0; i < n; i++) {
		if ((int)i == best) continue;
		bool fresh = false;
		for (uint32_t c : ch[i]) if (!gone[c]) fresh = true;
		for (uint32_t c : ch[i]) gone[c] = 1;
		if (!fresh || !out) continue;
		CollBlastSplit s;
		s.chunks = ch[i];
		std::vector<uint8_t> in (chunk.size (), 0);
		for (uint32_t c : ch[i]) in[c] = 1, s.c += chunk[c].c * chunk[c].mass, s.mass += chunk[c].mass;
		if (s.mass > 0) s.c /= s.mass;
		Vector nd;
		for (auto &b : bond) {
			if (in[b.a] && inMain[b.b]) nd -= b.n;
			else if (in[b.b] && inMain[b.a]) nd += b.n;
		}
		if (!(nd.length () > 1e-9)) nd = s.c - cm;
		if (!(nd.length () > 1e-9)) nd = Vector (0, 0, 1);
		s.n = Unit (nd);
		out->push_back (s);
	}
}

std::vector<CollBlastSplit> CollBlastA::Step ()
{
	std::vector<CollBlastSplit> out;
	if (!main) { force.clear (); spin = false; impD = 0; crP = 0; return out; }
	uint32_t nb = NvBlastAssetGetBondCount (asset, BlastLog), nc = NvBlastAssetGetChunkCount (asset, BlastLog);
	if (crP > 0) { // crushed chunks lose every bond: the crushed skin tears into fragments
		std::vector<uint32_t> cc = Crushed (crC, crN, crP, crR, crRatio);
		std::vector<uint8_t> in (chunk.size (), 0);
		for (uint32_t c : cc) in[c] = 1;
		std::vector<NvBlastBondFractureData> bf;
		for (size_t i = 0; i < bond.size (); i++) if ((in[bond[i].a] || in[bond[i].b]) && bond[i].asset != UINT32_MAX) {
			NvBlastBondFractureData f;
			f.userdata = (uint32_t)i; f.nodeIndex0 = nodeOf[bond[i].a]; f.nodeIndex1 = nodeOf[bond[i].b]; f.health = 1e9f;
			bf.push_back (f);
		}
		if (!bf.empty ()) {
			NvBlastFractureBuffers fb { (uint32_t)bf.size (), 0, bf.data (), nullptr };
			NvBlastActorApplyFracture (nullptr, main, &fb, BlastLog, nullptr);
			size_t k0 = out.size ();
			Split (&out);
			for (size_t k = k0; k < out.size (); k++) { // fragments spray out sideways from the crush axis
				out[k].crushed = true;
				Vector d = out[k].c - crC, lat = d - crN * (d & crN);
				if (lat.length () > 1e-6) out[k].n = Unit (lat * 2.0 + out[k].n);
			}
		}
		crP = 0;
	}
	if (impD > 0 && impR > 0 && accel && nb) {
		NvBlastExtImpactSpreadDamageDesc d;
		d.damage = (float)(impD * maxArea); d.position[0] = (float)impC.x; d.position[1] = (float)impC.y; d.position[2] = (float)impC.z; // health is area: 1 breaks the strongest bond
		d.minRadius = (float)impR; d.maxRadius = (float)(impR + std::sqrt (Acell)); // falloff over one cell spacing
		NvBlastExtProgramParams pp (&d, nullptr, accel);
		NvBlastDamageProgram prog { NvBlastExtImpactSpreadGraphShader, NvBlastExtImpactSpreadSubgraphShader };
		std::vector<NvBlastBondFractureData> bf (nb);
		std::vector<NvBlastChunkFractureData> cf (std::max<uint32_t> (nc, 1));
		NvBlastFractureBuffers fb { nb, (uint32_t)cf.size (), bf.data (), cf.data () };
		NvBlastActorGenerateFracture (&fb, main, prog, &pp, BlastLog, nullptr);
		if (fb.bondFractureCount || fb.chunkFractureCount) NvBlastActorApplyFracture (nullptr, main, &fb, BlastLog, nullptr);
		Split (&out);
	}
	if (!force.empty () || spin) {
		int nit = force.empty () ? 1 : BLAST_CONVERGE;              // impact frame: repeat until converged
		for (int it = 0; it < nit; it++) {
			for (auto &f : force) solver->addForce (*main, N3 (f.first), N3 (f.second / meanMass), Nv::Blast::ExtForceMode::ACCELERATION); // the solver equalizes node masses
			if (spin) solver->addCentrifugalAcceleration (*main, N3 (com), N3 (w));
			solver->update ();
			if (solver->converged () || solver->getOverstressedBondCount () > 0) break;
		}
		if (solver->getOverstressedBondCount () > 0) {
			NvBlastFractureBuffers cmd { 0, 0, nullptr, nullptr };
			solver->generateFractureCommands (*main, cmd);
			if (cmd.bondFractureCount || cmd.chunkFractureCount) NvBlastActorApplyFracture (nullptr, main, &cmd, BlastLog, nullptr);
			Split (&out);
		}
	}
	force.clear (); spin = false; impD = 0; crP = 0;
	return out;
}

void CollBlastA::Restore (const std::vector<uint32_t> &bonds, const std::vector<uint32_t> &removed, const std::vector<uint32_t> &weak)
{
	if (!main) return;
	std::vector<uint8_t> rm (chunk.size (), 0), br (bond.size (), 0);
	for (uint32_t c : removed) if (c < chunk.size ()) rm[c] = 1, gone[c] = 1;
	for (uint32_t b : bonds) if (b < bond.size ()) br[b] = 1;
	for (size_t i = 0; i < bond.size (); i++) if (rm[bond[i].a] != rm[bond[i].b]) br[i] = 1;
	std::vector<NvBlastBondFractureData> bf;
	for (size_t i = 0; i < bond.size (); i++) if (br[i] && bond[i].asset != UINT32_MAX) {
		NvBlastBondFractureData f;
		f.userdata = (uint32_t)i; f.nodeIndex0 = nodeOf[bond[i].a]; f.nodeIndex1 = nodeOf[bond[i].b]; f.health = 1e9f;
		bf.push_back (f);
	}
	std::map<uint32_t, uint32_t> idx;
	for (size_t i = 0; i < bond.size (); i++) { uint32_t x = ChunkKey (bond[i].a), y = ChunkKey (bond[i].b); idx[std::min (x, y) * 65536u + std::max (x, y)] = (uint32_t)i; }
	for (size_t k = 0; k + 1 < weak.size (); k += 2) { // weakened bonds: take the lost health off
		auto it = idx.find (weak[k]);
		if (it == idx.end () || br[it->second] || bond[it->second].asset == UINT32_MAX || weak[k + 1] == 0 || weak[k + 1] >= 1000000u) continue;
		uint32_t i = it->second;
		NvBlastBondFractureData f;
		f.userdata = i; f.nodeIndex0 = nodeOf[bond[i].a]; f.nodeIndex1 = nodeOf[bond[i].b];
		f.health = (float)(bond[i].area * (1.0 - weak[k + 1] * 1e-6));
		if (f.health > 0) bf.push_back (f);
	}
	if (bf.empty ()) return;
	NvBlastFractureBuffers fb { (uint32_t)bf.size (), 0, bf.data (), nullptr };
	NvBlastActorApplyFracture (nullptr, main, &fb, BlastLog, nullptr);
	Split (nullptr);
}

std::vector<uint32_t> CollBlastA::Broken () const
{
	std::vector<uint32_t> r;
	if (!main) return r;
	const float *h = NvBlastActorGetBondHealths (main, BlastLog);
	if (!h) return r;
	for (size_t i = 0; i < bond.size (); i++) if (bond[i].asset != UINT32_MAX && !(h[bond[i].asset] > 0.0f)) r.push_back ((uint32_t)i);
	return r;
}

std::vector<std::vector<uint32_t>> CollBlastA::Partition () const
{
	std::vector<std::vector<uint32_t>> r;
	if (!family) return r;
	std::vector<NvBlastActor *> a (NvBlastFamilyGetActorCount (family, BlastLog));
	if (!a.empty ()) a.resize (NvBlastFamilyGetActors (a.data (), (uint32_t)a.size (), family, BlastLog));
	for (NvBlastActor *x : a) r.push_back (Chunks (x));
	std::sort (r.begin (), r.end ());
	return r;
}

std::vector<uint32_t> CollBlastA::MainChunks () const { return main ? Chunks (main) : std::vector<uint32_t> (); }

int CollBlastA::ChunkOfCell (uint32_t cell) const
{
	for (size_t i = 0; i < chunk.size (); i++) if (chunk[i].cell == (int)cell) return (int)i;
	return -1;
}

int CollBlastA::ChunkOfPiece (int piece) const
{
	for (size_t i = 0; i < chunk.size (); i++) if (chunk[i].piece == piece) return (int)i;
	return -1;
}

uint32_t CollBlastA::ChunkKey (uint32_t c) const { return chunk[c].cell >= 0 ? (uint32_t)chunk[c].cell : (uint32_t)(site.size () + chunk[c].piece); }

std::vector<uint32_t> CollBlastA::BrokenPairs () const
{
	std::vector<uint32_t> r;
	for (uint32_t b : Broken ()) { uint32_t x = ChunkKey (bond[b].a), y = ChunkKey (bond[b].b); r.push_back (std::min (x, y) * 65536u + std::max (x, y)); }
	std::sort (r.begin (), r.end ());
	r.erase (std::unique (r.begin (), r.end ()), r.end ());
	return r;
}

std::vector<uint32_t> CollBlastA::WeakPairs () const
{
	std::vector<std::pair<uint32_t, uint32_t>> w;
	if (!main) return {};
	const float *h = NvBlastActorGetBondHealths (main, BlastLog);
	if (!h) return {};
	for (size_t i = 0; i < bond.size (); i++) {
		if (bond[i].asset == UINT32_MAX || !(h[bond[i].asset] > 0.0f) || !(bond[i].area > 0)) continue;
		double f = h[bond[i].asset] / bond[i].area;
		long q = std::lround (f * 1e6);
		if (q >= 1000000 || (float)h[bond[i].asset] >= (float)bond[i].area) continue;
		uint32_t x = ChunkKey (bond[i].a), y = ChunkKey (bond[i].b);
		w.push_back ({ std::min (x, y) * 65536u + std::max (x, y), (uint32_t)std::max (1L, q) });
	}
	std::sort (w.begin (), w.end ());
	std::vector<uint32_t> r;
	for (auto &p : w) r.push_back (p.first), r.push_back (p.second);
	return r;
}

std::vector<uint32_t> CollBlastA::BondsOfPairs (const std::vector<uint32_t> &pairs) const
{
	std::map<uint32_t, uint32_t> idx;
	for (size_t i = 0; i < bond.size (); i++) { uint32_t x = ChunkKey (bond[i].a), y = ChunkKey (bond[i].b); idx[std::min (x, y) * 65536u + std::max (x, y)] = (uint32_t)i; }
	std::vector<uint32_t> r;
	for (uint32_t p : pairs) { auto it = idx.find (p); if (it != idx.end ()) r.push_back (it->second); }
	std::sort (r.begin (), r.end ());
	return r;
}

double CollBlastA::Health (uint32_t b) const
{
	if (!main || b >= bond.size () || bond[b].asset == UINT32_MAX) return 0;
	const float *h = NvBlastActorGetBondHealths (main, BlastLog);
	return h ? h[bond[b].asset] : 0;
}
