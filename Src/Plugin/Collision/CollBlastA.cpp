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
	family = NvBlastAssetCreateFamily (familyMem.Get (NvBlastAssetGetFamilyMemorySize (asset, BlastLog)), asset, BlastLog);
	if (!family) { Release (); return false; }
	std::vector<float> health (std::max<size_t> (1, NvBlastAssetGetBondCount (asset, BlastLog)), 1.0f); // the stress solver reads bond health as the remaining area [m^2]
	for (auto &b : bond) if (b.asset != UINT32_MAX && b.asset < health.size ()) health[b.asset] = (float)b.area;
	NvBlastActorDesc acd;
	acd.uniformInitialBondHealth = 1.0f; acd.initialBondHealths = health.data (); acd.uniformInitialLowerSupportChunkHealth = 1.0f; acd.initialSupportChunkHealths = nullptr;
	sc = scratch.Get (NvBlastFamilyGetRequiredScratchForCreateFirstActor (family, BlastLog));
	main = NvBlastFamilyCreateFirstActor (family, &acd, sc, BlastLog);
	if (!main) { Release (); return false; }
	Nv::Blast::ExtStressSolverSettings st;
	st.maxSolverIterationsPerFrame = BLAST_ITER; st.graphReductionLevel = 0;
	solver = Nv::Blast::ExtStressSolver::create (*family, st);
	if (!solver) { Release (); return false; }
	for (uint32_t n = 0; n < g.nodeCount; n++) {
		uint32_t ci = g.chunkIndices[n];
		if (ci >= chunk.size ()) continue;
		solver->setNodeInfo (n, (float)chunk[ci].mass, (float)(chunk[ci].area * t), N3 (chunk[ci].c));
	}
	Material (BLAST_SIGMA_Y, BLAST_SIGMA_U);
	solver->notifyActorCreated (*main);
	accel = NvBlastExtDamageAcceleratorCreate (asset, 1);
	gone.assign (chunk.size (), 0);
	return true;
}

void CollBlastA::Material (double sy, double su)
{
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
	if (!main) { force.clear (); spin = false; impD = 0; return out; }
	uint32_t nb = NvBlastAssetGetBondCount (asset, BlastLog), nc = NvBlastAssetGetChunkCount (asset, BlastLog);
	if (impD > 0 && impR > 0 && accel && nb) {
		NvBlastExtImpactSpreadDamageDesc d;
		d.damage = (float)(impD * maxArea); d.position[0] = (float)impC.x; d.position[1] = (float)impC.y; d.position[2] = (float)impC.z; // health is area: 1 breaks the strongest bond
		double rc = impR + std::sqrt (Acell);                          // graph distance to the neighbours of the crushed cells
		d.minRadius = (float)rc; d.maxRadius = (float)(rc + std::sqrt (Acell));
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
		for (auto &f : force) solver->addForce (*main, N3 (f.first), N3 (f.second), Nv::Blast::ExtForceMode::FORCE);
		if (spin) solver->addCentrifugalAcceleration (*main, N3 (com), N3 (w));
		solver->update ();
		if (solver->getOverstressedBondCount () > 0) {
			NvBlastFractureBuffers cmd { 0, 0, nullptr, nullptr };
			solver->generateFractureCommands (*main, cmd);
			if (cmd.bondFractureCount || cmd.chunkFractureCount) NvBlastActorApplyFracture (nullptr, main, &cmd, BlastLog, nullptr);
			Split (&out);
		}
	}
	force.clear (); spin = false; impD = 0;
	return out;
}

void CollBlastA::Restore (const std::vector<uint32_t> &bonds, const std::vector<uint32_t> &removed)
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
