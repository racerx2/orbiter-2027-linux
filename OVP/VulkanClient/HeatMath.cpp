// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: re-entry hull heating maths (design-reentry-heat 2, 3, Rev 3 real edges)

#include "HeatMath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace HeatMath {

// Sutton and Graves, NASA TR R-376 (1971): k for air, CO2 and H2-He mixtures
double SuttonGravesK (double R)
{
	if (R < 230.0) return 1.9027e-4;
	if (R > 1000.0) return 0.6556e-4;
	return 1.7415e-4;
}

double SuttonGraves (double rho, double Rn, double V, double k)
{
	if (!(rho > 0.0) || !(V > 0.0) || !(Rn > 0.0)) return 0.0;
	double q = k * std::sqrt(rho / Rn) * V * V * V;
	return std::min(q, 0.5 * rho * V * V * V); // free-molecular limit, full accommodation
}

double HotWallFactor (double T, double cp, double h0)
{
	if (!(h0 > 0.0)) return 0.0;
	return std::min(1.0, 1.0 - cp * T / h0);
}

// Tauber, Menees and Adelman (1987): laminar flat plate 2.53e-5 rho^0.5 V^3.2 x^-0.5 over Sutton-Graves
double KFlatPlate (double V)
{
	return (2.53e-5 / 1.7415e-4) * std::pow(std::max(V, 0.0), 0.2);
}

// Lees (1956) hemisphere distribution as 0.55 + 0.45 cos 2theta; continued past 90 deg to 0 at c = -0.25
double AngleFactor (double c)
{
	if (c >= 0.0) return 0.1 + 0.9 * c * c;
	double u = std::max(0.0, 1.0 + 4.0 * c);
	return 0.1 * u * u;
}

static double Smooth (double e0, double e1, double x)
{
	double u = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
	return u * u * (3.0 - 2.0 * u);
}

static double Dot (const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static double Len (const double a[3]) { return std::sqrt(Dot(a, a)); }
static void Cross (const double a[3], const double b[3], double o[3])
{
	double r[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
	o[0] = r[0]; o[1] = r[1]; o[2] = r[2];
}
static bool Unit (double a[3])
{
	double l = Len(a);
	if (!(l > 1e-12)) return false;
	for (int i = 0; i < 3; i++) a[i] /= l;
	return true;
}

// stagnation region, laminar flat plate (Tauber, Menees and Adelman 1987), the edge bands, lee floor
double LocalRatio (double c, double x, double Rn, double kfp, double Ge)
{
	c = std::clamp(c, -1.0, 1.0);
	double r = x / (1.5 * Rn);
	double Gs = AngleFactor(c) * std::exp(-r * r);
	double Gf = 0.0;
	if (c > 0.0) Gf = kfp * std::sqrt(std::sqrt(std::max(0.0, 1.0 - c * c))) * c * std::sqrt(Rn / std::max(x, Rn));
	return std::clamp(std::max(Gs, std::max(Gf, Ge)) + Tune::lee, 0.0, 2.0);
}

// Lees around a cylinder of the band's half-width; the far side of a thin edge is cut by the normal
double EdgeBand (double d, double w, double level, double nDotAtt)
{
	if (!(w > 0.0) || !(level > 0.0)) return 0.0;
	return level * AngleFactor(std::cos(std::min(d / w, 3.0))) * Smooth(-0.5, 0.0, nDotAtt);
}

// swept cylinder (Fay and Riddell 1958: sqrt(R_n/2R) of the sphere's flux, Tauber's cos L), Lees by beta, off over edgeBeta when the attachment line is off the feature
double EdgeLevel (double rl, double Rn, double cL, double beta, double betaFeat)
{
	double r = std::max(rl, Tune::edgeRmin * Rn);
	double lev = std::min(Tune::edge, std::sqrt(Rn / (2.0 * r))) * std::clamp(cL, 0.0, 1.0) * AngleFactor(std::cos(beta));
	return lev * (1.0 - Smooth(0.0, Tune::edgeBeta * HPI / 180.0, betaFeat));
}

namespace {
struct VKey { int64_t x, y, z; bool operator== (const VKey &o) const { return x == o.x && y == o.y && z == o.z; } };
struct VHash { size_t operator() (const VKey &k) const { return size_t(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL); } };
struct EdgeRec { uint32_t f1 = 0, f2 = 0, o1 = 0, o2 = 0; int n = 0; };
const double QW = 1e-3;		// weld grid, m
}

void EdgeSet::Begin (const std::vector<float> &xyz, const std::vector<uint32_t> &tri)
{
	*this = EdgeSet();
	inXyz = xyz;
	inTri = tri;
	phase = 1;
}

bool EdgeSet::Step (size_t &rays)
{
	if (phase == 1) { Prepare(); phase = 2; return false; }
	if (phase == 2) { Voxelize(rays); if (next >= T.size() / 3) { next = 0; phase = 3; } return false; }
	if (phase == 3) { Expose(rays); if (next >= faces.size()) phase = 4; return false; }
	if (phase == 4) { Features(); phase = 5; }
	return phase == 5;
}

bool EdgeSet::Occ (const double p[3]) const
{
	int i[3];
	for (int k = 0; k < 3; k++) {
		double s = std::floor((p[k] - lo[k]) / vox);
		if (!(s >= 0.0) || s >= dim[k]) return false;
		i[k] = int(s);
	}
	size_t b = (size_t(i[2]) * dim[1] + i[1]) * dim[0] + i[0];
	return (occ[b >> 6] >> (b & 63)) & 1;
}

// weld, face normals, manifold edges, convex candidates, face adjacency, surface voxels
void EdgeSet::Prepare ()
{
	std::unordered_map<VKey, uint32_t, VHash> wm;
	std::vector<uint32_t> remap(inXyz.size() / 3);
	for (size_t i = 0; i < remap.size(); i++) {
		VKey k = { std::llround(inXyz[3 * i] / QW), std::llround(inXyz[3 * i + 1] / QW), std::llround(inXyz[3 * i + 2] / QW) };
		auto it = wm.find(k);
		if (it == wm.end()) {
			it = wm.emplace(k, uint32_t(P.size() / 3)).first;
			for (int j = 0; j < 3; j++) P.push_back(inXyz[3 * i + j]);
		}
		remap[i] = it->second;
	}
	double mn[3] = { 1e300, 1e300, 1e300 }, mx[3] = { -1e300, -1e300, -1e300 };
	for (size_t i = 0; i < P.size(); i += 3) for (int j = 0; j < 3; j++) { mn[j] = std::min(mn[j], P[i + j]); mx[j] = std::max(mx[j], P[i + j]); }
	size = P.empty() ? 0.0 : std::max(mx[0] - mn[0], std::max(mx[1] - mn[1], mx[2] - mn[2]));
	for (size_t t = 0; t + 2 < inTri.size(); t += 3) {
		if (inTri[t] >= remap.size() || inTri[t + 1] >= remap.size() || inTri[t + 2] >= remap.size()) continue;
		uint32_t a = remap[inTri[t]], b = remap[inTri[t + 1]], c = remap[inTri[t + 2]];
		if (a == b || b == c || a == c) continue;
		double e1[3], e2[3], n[3];
		for (int j = 0; j < 3; j++) { e1[j] = P[3 * b + j] - P[3 * a + j]; e2[j] = P[3 * c + j] - P[3 * a + j]; }
		Cross(e1, e2, n);
		if (!(Len(n) > 1e-12 * size * size) || !Unit(n)) continue;
		T.push_back(a); T.push_back(b); T.push_back(c);
		for (int j = 0; j < 3; j++) N.push_back(n[j]);
	}
	inXyz.clear(); inXyz.shrink_to_fit();
	inTri.clear(); inTri.shrink_to_fit();
	size_t nf = T.size() / 3;
	tris = nf;
	std::unordered_map<uint64_t, EdgeRec> em;
	em.reserve(nf * 2);
	for (size_t f = 0; f < nf; f++) for (int e = 0; e < 3; e++) {
		uint32_t a = T[3 * f + e], b = T[3 * f + (e + 1) % 3], o = T[3 * f + (e + 2) % 3];
		uint64_t k = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
		EdgeRec &r = em[k];
		if (r.n == 0) { r.f1 = uint32_t(f); r.o1 = o; }
		else if (r.n == 1) { r.f2 = uint32_t(f); r.o2 = o; }
		r.n++;
	}
	std::vector<uint32_t> deg(nf + 1, 0);
	for (auto &kv : em) if (kv.second.n == 2) { deg[kv.second.f1]++; deg[kv.second.f2]++; }
	adj0.assign(nf + 1, 0);
	for (size_t f = 0; f < nf; f++) adj0[f + 1] = adj0[f] + deg[f];
	adj.assign(adj0[nf], 0);
	std::vector<uint32_t> fill(adj0.begin(), adj0.end() - 1);
	const double dmin = Tune::edgeDelta * HPI / 180.0;
	std::vector<char> isFace(nf, 0);
	for (auto &kv : em) {
		const EdgeRec &r = kv.second;
		if (r.n != 2) continue;
		adj[fill[r.f1]++] = r.f2;
		adj[fill[r.f2]++] = r.f1;
		const double *n1 = &N[3 * r.f1], *n2 = &N[3 * r.f2];
		double d = std::acos(std::clamp(Dot(n1, n2), -1.0, 1.0));
		uint32_t a = uint32_t(kv.first >> 32), b = uint32_t(kv.first & 0xffffffffu);
		double w[3];
		for (int j = 0; j < 3; j++) w[j] = P[3 * r.o2 + j] - P[3 * a + j];
		if (d < dmin || !(Dot(n1, w) < -1e-9 * size)) continue;
		cand.push_back({ a, b, r.f1, r.f2, d });
		isFace[r.f1] = isFace[r.f2] = 1;
	}
	convex = cand.size();
	for (size_t f = 0; f < nf; f++) if (isFace[f]) faces.push_back(uint32_t(f));
	expo.assign(nf, -1.0f);
	vox = std::clamp(size / 250.0, 0.02, 0.15);
	for (int j = 0; j < 3; j++) {
		lo[j] = (P.empty() ? 0.0 : mn[j]) - 2.0 * vox;
		dim[j] = P.empty() ? 1 : int(std::ceil((mx[j] - mn[j]) / vox)) + 5;
	}
	occ.assign((size_t(dim[0]) * dim[1] * dim[2] + 63) / 64, 0);
	next = 0;
}

// the surface voxels, sampled at half a voxel along both triangle edges; 64 samples cost one ray of the budget
void EdgeSet::Voxelize (size_t &rays)
{
	size_t nf = T.size() / 3;
	for (; next < nf && rays > 0; next++) {
		size_t f = next;
		const double *a = &P[3 * T[3 * f]], *b = &P[3 * T[3 * f + 1]], *c = &P[3 * T[3 * f + 2]];
		double lm = 0.0;
		for (int j = 0; j < 3; j++) lm += std::max((b[j] - a[j]) * (b[j] - a[j]), (c[j] - a[j]) * (c[j] - a[j]));
		int k = std::max(2, int(std::ceil(std::sqrt(lm) / (0.5 * vox))) + 1);
		rays -= std::min(rays, size_t(k) * size_t(k + 1) / 128 + 1);
		for (int i = 0; i <= k; i++) for (int j = 0; i + j <= k; j++) {
			double p[3], ui = double(i) / k, uj = double(j) / k;
			int ix[3];
			bool in = true;
			for (int m = 0; m < 3; m++) {
				p[m] = a[m] + (b[m] - a[m]) * ui + (c[m] - a[m]) * uj;
				double s = std::floor((p[m] - lo[m]) / vox);
				if (!(s >= 0.0) || s >= dim[m]) { in = false; break; }
				ix[m] = int(s);
			}
			if (!in) continue;
			size_t bi = (size_t(ix[2]) * dim[1] + ix[1]) * dim[0] + ix[0];
			occ[bi >> 6] |= uint64_t(1) << (bi & 63);
		}
	}
}

// share of hemisphere rays from a face that leave the voxel box without a hit
void EdgeSet::Expose (size_t &rays)
{
	static double H[Tune::edgeRays][3];
	static bool hOk = false;
	if (!hOk) {
		const double ga = HPI * (3.0 - std::sqrt(5.0));
		for (int i = 0; i < Tune::edgeRays; i++) {
			double z = std::sqrt(1.0 - (i + 0.5) / Tune::edgeRays), r = std::sqrt(std::max(0.0, 1.0 - z * z)), p = ga * i;
			H[i][0] = r * std::cos(p); H[i][1] = r * std::sin(p); H[i][2] = z;
		}
		hOk = true;
	}
	int maxSteps = 2 * std::max(dim[0], std::max(dim[1], dim[2]));
	while (next < faces.size() && rays >= size_t(Tune::edgeRays)) {
		uint32_t f = faces[next++];
		const double *n = &N[3 * f];
		double c[3], t1[3], t2[3], ref[3] = { 1.0, 0.0, 0.0 };
		for (int j = 0; j < 3; j++) c[j] = (P[3 * T[3 * f] + j] + P[3 * T[3 * f + 1] + j] + P[3 * T[3 * f + 2] + j]) / 3.0;
		if (std::fabs(n[0]) > 0.9) { ref[0] = 0.0; ref[1] = 1.0; }
		Cross(n, ref, t1); Unit(t1); Cross(n, t1, t2);
		int esc = 0;
		for (int i = 0; i < Tune::edgeRays; i++) {
			double d[3], p[3];
			for (int j = 0; j < 3; j++) { d[j] = H[i][0] * t1[j] + H[i][1] * t2[j] + H[i][2] * n[j]; p[j] = c[j] + n[j] * 1.5 * vox + d[j] * 0.5 * vox; }
			bool out = false;
			for (int s = 0; s < maxSteps; s++) {
				bool inside = true;
				for (int j = 0; j < 3; j++) { double u = (p[j] - lo[j]) / vox; if (!(u >= 0.0) || u >= dim[j]) inside = false; }
				if (!inside) { out = true; break; }
				if (Occ(p)) break;
				for (int j = 0; j < 3; j++) p[j] += d[j] * 0.5 * vox;
			}
			esc += out ? 1 : 0;
		}
		expo[f] = float(esc) / Tune::edgeRays;
		rays -= Tune::edgeRays;
	}
}

// real edges: exposed convex edges whose surface-connected parallel neighbours within rho turn the normal by edgeTurn or more
void EdgeSet::Features ()
{
	const double rho = Tune::edgeRho * size, c30 = std::cos(30.0 * HPI / 180.0), turn = Tune::edgeTurn * HPI / 180.0;
	size_t nf = T.size() / 3;
	std::vector<uint32_t> ok;
	for (uint32_t i = 0; i < cand.size(); i++) if (expo[cand[i].f1] >= Tune::edgeExpose && expo[cand[i].f2] >= Tune::edgeExpose) ok.push_back(i);
	exposed = ok.size();
	std::vector<uint32_t> fc0(nf + 1, 0), fc;
	for (uint32_t k : ok) { fc0[cand[k].f1 + 1]++; fc0[cand[k].f2 + 1]++; }
	for (size_t f = 0; f < nf; f++) fc0[f + 1] += fc0[f];
	fc.assign(fc0[nf], 0);
	std::vector<uint32_t> fill(fc0.begin(), fc0.end() - 1);
	for (uint32_t m = 0; m < ok.size(); m++) { fc[fill[cand[ok[m]].f1]++] = m; fc[fill[cand[ok[m]].f2]++] = m; }
	std::vector<uint32_t> fStamp(nf, 0), eStamp(ok.size(), 0), realOf(ok.size(), UINT32_MAX);
	std::vector<std::vector<uint32_t>> nbOk(ok.size());
	std::vector<char> isReal(ok.size(), 0);
	std::vector<double> rls(ok.size(), 0.0);
	std::vector<char> plain1(ok.size(), 0), plain2(ok.size(), 0);
	uint32_t stamp = 0;
	std::vector<uint32_t> seen, front, nfront;
	std::vector<double> ang;
	for (uint32_t m = 0; m < ok.size(); m++) {
		const Cand &e = cand[ok[m]];
		const double *a = &P[3 * e.a], *b = &P[3 * e.b];
		double t[3], mid[3];
		for (int j = 0; j < 3; j++) { t[j] = b[j] - a[j]; mid[j] = 0.5 * (a[j] + b[j]); }
		if (!Unit(t)) continue;
		double sm = Dot(mid, t);
		++stamp;
		seen.assign({ e.f1, e.f2 });
		front = seen;
		fStamp[e.f1] = fStamp[e.f2] = stamp;
		for (int dep = 0; dep < 4; dep++) {
			nfront.clear();
			for (uint32_t f : front) for (uint32_t k = adj0[f]; k < adj0[f + 1]; k++) {
				uint32_t g = adj[k];
				if (fStamp[g] == stamp) continue;
				fStamp[g] = stamp;
				nfront.push_back(g); seen.push_back(g);
			}
			front.swap(nfront);
		}
		uint32_t o1 = T[3 * e.f1] != e.a && T[3 * e.f1] != e.b ? T[3 * e.f1] : (T[3 * e.f1 + 1] != e.a && T[3 * e.f1 + 1] != e.b ? T[3 * e.f1 + 1] : T[3 * e.f1 + 2]);
		double s1[3];
		for (int j = 0; j < 3; j++) s1[j] = P[3 * o1 + j] - mid[j];
		double ps = Dot(s1, t);
		for (int j = 0; j < 3; j++) s1[j] -= ps * t[j];
		Unit(s1);
		double w1 = 4.0 * rho, w2 = 4.0 * rho;
		const double *n1 = &N[3 * e.f1];
		double bu[3], bw[3];
		double pn = Dot(n1, t);
		for (int j = 0; j < 3; j++) bu[j] = n1[j] - pn * t[j];
		Unit(bu);
		Cross(t, bu, bw);
		ang.clear();
		auto addN = [&](uint32_t f) { const double *n = &N[3 * f]; ang.push_back(std::atan2(Dot(n, bw), Dot(n, bu))); };
		addN(e.f1); addN(e.f2);
		eStamp[m] = stamp;
		for (uint32_t f : seen) for (uint32_t k = fc0[f]; k < fc0[f + 1]; k++) {
			uint32_t j = fc[k];
			if (eStamp[j] == stamp) continue;
			eStamp[j] = stamp;
			const Cand &g = cand[ok[j]];
			const double *pa = &P[3 * g.a], *pb = &P[3 * g.b];
			double tj[3];
			for (int q = 0; q < 3; q++) tj[q] = pb[q] - pa[q];
			if (!Unit(tj) || std::fabs(Dot(tj, t)) < c30) continue;
			double sa = Dot(pa, t), sb = Dot(pb, t);
			if (!(std::fabs(sb - sa) > 1e-9)) continue;
			double u = (sm - sa) / (sb - sa);
			if (u < -0.05 || u > 1.05) continue;
			u = std::clamp(u, 0.0, 1.0);
			double dv[3];
			for (int q = 0; q < 3; q++) dv[q] = pa[q] + u * (pb[q] - pa[q]) - mid[q];
			double pd = Dot(dv, t);
			for (int q = 0; q < 3; q++) dv[q] -= pd * t[q];
			double dl = Len(dv);
			if (dl > rho) continue;
			nbOk[m].push_back(j);
			addN(g.f1); addN(g.f2);
			if (dl > 1e-3) { if (Dot(dv, s1) > 0.0) w1 = std::min(w1, dl); else w2 = std::min(w2, dl); }
		}
		std::sort(ang.begin(), ang.end());
		double gap = ang.front() + 2.0 * HPI - ang.back();
		for (size_t k = 1; k < ang.size(); k++) gap = std::max(gap, ang[k] - ang[k - 1]);
		double th = 2.0 * HPI - gap;
		rls[m] = 0.5 * (w1 + w2) / std::max(e.d, 1e-3);
		plain1[m] = w1 >= 4.0 * rho;
		plain2[m] = w2 >= 4.0 * rho;
		isReal[m] = th >= turn;
	}
	for (uint32_t m = 0; m < ok.size(); m++) {
		if (!isReal[m]) continue;
		const Cand &e = cand[ok[m]];
		Edge E;
		for (int j = 0; j < 3; j++) { E.a[j] = P[3 * e.a + j]; E.b[j] = P[3 * e.b + j]; E.t[j] = E.b[j] - E.a[j]; }
		if (!Unit(E.t)) continue;
		const double *n1 = &N[3 * e.f1], *n2 = &N[3 * e.f2];
		double p2[3], d1 = Dot(n1, E.t), d2 = Dot(n2, E.t);
		for (int j = 0; j < 3; j++) { E.p1[j] = n1[j] - d1 * E.t[j]; p2[j] = n2[j] - d2 * E.t[j]; }
		if (!Unit(E.p1) || !Unit(p2)) continue;
		Cross(E.t, E.p1, E.q);
		E.arc = std::atan2(Dot(p2, E.q), Dot(p2, E.p1));
		if (E.arc < 0.0) { E.arc = -E.arc; for (double &v : E.q) v = -v; }
		E.rl = rls[m];
		E.va = e.a; E.vb = e.b;
		E.nb0 = E.nb1 = 0;
		E.face1 = plain1[m] != 0;
		E.face2 = plain2[m] != 0;
		realOf[m] = uint32_t(edges.size());
		edges.push_back(E);
	}
	for (uint32_t m = 0; m < ok.size(); m++) {
		if (realOf[m] == UINT32_MAX) continue;
		Edge &E = edges[realOf[m]];
		E.nb0 = uint32_t(nbr.size());
		for (uint32_t j : nbOk[m]) if (realOf[j] != UINT32_MAX) nbr.push_back(realOf[j]);
		E.nb1 = uint32_t(nbr.size());
	}
	real = edges.size();
	P.clear(); P.shrink_to_fit(); N.clear(); N.shrink_to_fit(); T.clear(); T.shrink_to_fit();
	cand.clear(); cand.shrink_to_fit(); faces.clear(); faces.shrink_to_fit();
	adj0.clear(); adj0.shrink_to_fit(); adj.clear(); adj.shrink_to_fit(); expo.clear(); expo.shrink_to_fit();
}

// body more than shieldH upstream of p within edgeRho beside it, in the cross-section (Newtonian shadow with the shock and boundary layers' thickness)
bool EdgeSet::Shielded (const double p[3], const double u[3], const double t[3]) const
{
	if (occ.empty() || !(vox > 0.0)) return false;
	double l[3];
	Cross(t, u, l);
	if (!Unit(l)) l[0] = l[1] = l[2] = 0.0;
	double W = Tune::edgeRho * size, h0 = std::max(Tune::shieldH * size, 2.0 * vox), h1 = Tune::shieldL * size;
	for (double s : { -1.0, -0.5, 0.0, 0.5, 1.0 }) for (double h = h0; h <= h1; h += 0.5 * vox) {
		double q[3];
		for (int j = 0; j < 3; j++) q[j] = p[j] + u[j] * h + l[j] * s * W;
		if (Occ(q)) return true;
	}
	return false;
}

// the attachment lines on the real edges for the downstream direction f, merged along shared ends
void EdgeSet::Attach (const double f[3], double Rn, std::vector<EdgeSeg> &out) const
{
	if (!Done() || edges.empty()) return;
	struct B { double beta, cL, plain; double u[3], n[3]; bool ok; };
	std::vector<B> bb(edges.size());
	for (size_t i = 0; i < edges.size(); i++) {
		const Edge &E = edges[i];
		B &b = bb[i];
		double fa = Dot(f, E.t), fp[3];
		for (int j = 0; j < 3; j++) fp[j] = f[j] - fa * E.t[j];
		b.ok = Unit(fp);
		if (!b.ok) continue;
		for (int j = 0; j < 3; j++) b.u[j] = -fp[j];
		b.cL = std::sqrt(std::max(0.0, 1.0 - fa * fa));
		double au = std::atan2(Dot(b.u, E.q), Dot(b.u, E.p1)), bR = Tune::edgeBeta * HPI / 180.0;
		if (au >= 0.0 && au <= E.arc) { // on the edge; near the end of a plain face the attachment line moves onto that face
			b.beta = 0.0;
			b.plain = (E.face1 ? Smooth(0.0, bR, au) : 1.0) * (E.face2 ? Smooth(0.0, bR, E.arc - au) : 1.0);
			for (int j = 0; j < 3; j++) b.n[j] = b.u[j];
			continue;
		}
		double e0 = au < 0.0 ? -au : 2.0 * HPI - au, e1 = au < 0.0 ? au + 2.0 * HPI - E.arc : au - E.arc;
		b.beta = std::min(e0, e1);
		b.plain = (e0 <= e1 ? E.face1 : E.face2) ? 0.0 : 1.0; // past a plain face: that face's corner, not an edge
		double ae = e0 <= e1 ? 0.0 : E.arc;
		for (int j = 0; j < 3; j++) b.n[j] = std::cos(ae) * E.p1[j] + std::sin(ae) * E.q[j];
	}
	struct S { uint32_t e; double level, w; double n[3]; };
	std::vector<S> sel;
	for (size_t i = 0; i < edges.size(); i++) {
		const Edge &E = edges[i];
		const B &b = bb[i];
		if (!b.ok) continue;
		double bf = b.beta;
		for (uint32_t k = E.nb0; k < E.nb1; k++) if (bb[nbr[k]].ok) bf = std::min(bf, bb[nbr[k]].beta);
		double lev = EdgeLevel(E.rl, Rn, b.cL, b.beta, bf) * b.plain;
		if (lev < 0.02) continue;
		int sh = 0;
		for (double k : { 0.2, 0.5, 0.8 }) {
			double p[3];
			for (int j = 0; j < 3; j++) p[j] = E.a[j] + (E.b[j] - E.a[j]) * k;
			sh += Shielded(p, b.u, E.t) ? 1 : 0;
		}
		lev *= 1.0 - sh / 3.0;
		if (lev < 0.02) continue;
		S s = { uint32_t(i), lev, std::clamp(E.rl, Tune::edgeBand * Rn, Rn), { b.n[0], b.n[1], b.n[2] } };
		sel.push_back(s);
	}
	std::vector<uint32_t> par(sel.size());
	for (uint32_t i = 0; i < par.size(); i++) par[i] = i;
	auto find = [&](uint32_t i) { while (par[i] != i) { par[i] = par[par[i]]; i = par[i]; } return i; };
	std::unordered_map<uint32_t, std::vector<uint32_t>> vm;
	for (uint32_t k = 0; k < sel.size(); k++) { vm[edges[sel[k].e].va].push_back(k); vm[edges[sel[k].e].vb].push_back(k); }
	const double c15 = std::cos(15.0 * HPI / 180.0);
	for (auto &kv : vm) for (size_t x = 0; x < kv.second.size(); x++) for (size_t y = x + 1; y < kv.second.size(); y++) {
		const S &s1 = sel[kv.second[x]], &s2 = sel[kv.second[y]];
		if (std::fabs(Dot(edges[s1.e].t, edges[s2.e].t)) < c15) continue;
		if (std::fabs(s1.level - s2.level) > 0.3 * std::max(s1.level, s2.level)) continue;
		par[find(kv.second[x])] = find(kv.second[y]);
	}
	std::unordered_map<uint32_t, std::vector<uint32_t>> grp;
	for (uint32_t k = 0; k < sel.size(); k++) grp[find(k)].push_back(k);
	for (auto &kv : grp) {
		const std::vector<uint32_t> &g = kv.second;
		const double *t0 = edges[sel[g[0]].e].t;
		double ax[3] = { 0.0, 0.0, 0.0 }, c[3] = { 0.0, 0.0, 0.0 }, n[3] = { 0.0, 0.0, 0.0 }, lw = 0.0, ls = 0.0, ws = 0.0;
		for (uint32_t k : g) {
			const Edge &E = edges[sel[k].e];
			double len = 0.0;
			for (int j = 0; j < 3; j++) len += (E.b[j] - E.a[j]) * (E.b[j] - E.a[j]);
			len = std::sqrt(len);
			double sg = Dot(E.t, t0) < 0.0 ? -1.0 : 1.0;
			for (int j = 0; j < 3; j++) { ax[j] += sg * E.t[j] * len; c[j] += 0.5 * (E.a[j] + E.b[j]); n[j] += sel[k].n[j] * len; }
			lw += len; ls += sel[k].level * len; ws += sel[k].w * len;
		}
		if (!(lw > 0.0) || !Unit(ax)) continue;
		Unit(n);
		for (int j = 0; j < 3; j++) c[j] /= double(g.size());
		double s0 = 1e300, s1 = -1e300;
		for (uint32_t k : g) {
			const Edge &E = edges[sel[k].e];
			double da[3], db[3];
			for (int j = 0; j < 3; j++) { da[j] = E.a[j] - c[j]; db[j] = E.b[j] - c[j]; }
			s0 = std::min(s0, std::min(Dot(da, ax), Dot(db, ax)));
			s1 = std::max(s1, std::max(Dot(da, ax), Dot(db, ax)));
		}
		EdgeSeg o;
		for (int j = 0; j < 3; j++) { o.a[j] = c[j] + ax[j] * s0; o.b[j] = c[j] + ax[j] * s1; o.n[j] = n[j]; }
		o.level = ls / lw;
		o.w = ws / lw;
		out.push_back(o);
	}
}

double HeatCapacity (double lag, double eps)
{
	return lag * 4.0 * eps * SIGMA * Tune::Tref * Tune::Tref * Tune::Tref;
}

// C (T' - T)/dt = G q hw(T') - eps sigma (T'^4 - Ts^4): increasing in T', one root, Newton with a bracket
static double Solve (double T, double rate, double qG, double cp, double h0, const Wall &w)
{
	double es = w.eps * SIGMA, Ts4 = w.Tsink * w.Tsink * w.Tsink * w.Tsink;
	auto F = [&](double x) { return rate * (x - T) - qG * HotWallFactor(x, cp, h0) + es * (x * x * x * x - Ts4); };
	auto dF = [&](double x) {
		double d = rate + 4.0 * es * x * x * x;
		if (h0 > 0.0) d += qG * cp / h0; // the hot-wall factor is linear in T, also past the recovery temperature (review NIT 1)
		return d;
	};
	double lo = 1.0, hi = std::max(T, std::pow((qG + es * Ts4) / es, 0.25)) + 1.0;
	double x = std::clamp(T, lo, hi);
	for (int i = 0; i < 60; i++) {
		double fx = F(x);
		if (fx > 0.0) hi = x; else lo = x;
		double d = dF(x);
		double nx = d > 0.0 ? x - fx / d : 0.5 * (lo + hi);
		if (!(nx > lo && nx < hi)) nx = 0.5 * (lo + hi);
		if (std::fabs(nx - x) < 1e-5) return nx;
		x = nx;
	}
	return x;
}

double EquilibriumWall (double qcw, double G, double cp, double h0, const Wall &w)
{
	return Solve(w.Tsink, 0.0, std::max(0.0, qcw * G), cp, h0, w);
}

double StepWall (double T, double dt, double qcw, double G, double cp, double h0, const Wall &w)
{
	if (!(dt > 0.0)) return T;
	double rate = w.C / dt;
	if (!(rate > 1e-9)) return EquilibriumWall(qcw, G, cp, h0, w);
	return Solve(T, rate, std::max(0.0, qcw * G), cp, h0, w);
}

double FlowTau (double lag, double T0)
{
	double r = Tune::Tref / std::max(T0, 300.0);
	return std::clamp(lag * r * r * r / 3.0, 0.2, lag);
}

double LowPass (double dt, double tau)
{
	if (!(dt > 0.0)) return 0.0;
	if (!(tau > 0.0)) return 1.0;
	return 1.0 - std::exp(-dt / tau);
}

double BinRatio (int k)
{
	return Tune::G0 * std::pow(Tune::Gr, double(k));
}

double BinCoord (double G)
{
	if (!(G > 0.0)) return double(NBIN + 1);
	return std::clamp(std::log(G / Tune::G0) / std::log(Tune::Gr), 0.0, double(NBIN + 1));
}

// Wyman, Sloan and Shirley, JCGT 2(2) (2013): multi-lobe Gaussian fit of the CIE 1931 2 deg functions
static double Lobe (double x, double m, double s1, double s2)
{
	double t = (x - m) / (x < m ? s1 : s2);
	return std::exp(-0.5 * t * t);
}

void BlackbodyXYZ (double T, double xyz[3])
{
	const double h = 6.62607015e-34, c = 2.99792458e8, k = 1.380649e-23;
	xyz[0] = xyz[1] = xyz[2] = 0.0;
	if (!(T > 0.0)) return;
	for (int l = 360; l <= 830; l++) {
		double lm = l * 1e-9;
		double e = h * c / (lm * k * T);
		if (e > 700.0) continue;
		double B = 2.0 * h * c * c / (lm * lm * lm * lm * lm * std::expm1(e)) * 1e-9; // W/m2/sr per nm (Planck)
		double xb = 1.056 * Lobe(l, 599.8, 37.9, 31.0) + 0.362 * Lobe(l, 442.0, 16.0, 26.7) - 0.065 * Lobe(l, 501.1, 20.4, 26.2);
		double yb = 0.821 * Lobe(l, 568.8, 46.9, 40.5) + 0.286 * Lobe(l, 530.9, 16.3, 31.1);
		double zb = 1.217 * Lobe(l, 437.0, 11.8, 36.0) + 0.681 * Lobe(l, 459.0, 26.0, 13.8);
		xyz[0] += B * xb; xyz[1] += B * yb; xyz[2] += B * zb;
	}
	for (int i = 0; i < 3; i++) xyz[i] *= 683.0; // lm/W
}

// IEC 61966-2-1: XYZ to linear sRGB (D65)
void XYZToLinearSrgb (const double xyz[3], double rgb[3])
{
	rgb[0] = 3.2406 * xyz[0] - 1.5372 * xyz[1] - 0.4986 * xyz[2];
	rgb[1] = -0.9689 * xyz[0] + 1.8758 * xyz[1] + 0.0415 * xyz[2];
	rgb[2] = 0.0557 * xyz[0] - 0.2040 * xyz[1] + 1.0570 * xyz[2];
}

void Blackbody (double T, double rgb[3])
{
	double xyz[3];
	BlackbodyXYZ(T, xyz);
	XYZToLinearSrgb(xyz, rgb);
	for (int i = 0; i < 3; i++) rgb[i] = std::max(0.0, rgb[i]);
}

void ColourTable::Build (int n, double t0, double t1)
{
	T0 = t0; T1 = t1;
	n = std::max(n, 2);
	ln.assign(size_t(n) * 3, -60.0f);
	for (int i = 0; i < n; i++) {
		double u = 1.0 / T0 + (1.0 / T1 - 1.0 / T0) * double(i) / double(n - 1);
		double rgb[3];
		Blackbody(1.0 / u, rgb);
		for (int j = 0; j < 3; j++) ln[size_t(i) * 3 + j] = rgb[j] > 0.0 ? float(std::max(-60.0, std::log(rgb[j]))) : -60.0f;
	}
}

void ColourTable::Lookup (double T, double rgb[3]) const
{
	rgb[0] = rgb[1] = rgb[2] = 0.0;
	int n = int(ln.size() / 3);
	if (n < 2 || !(T >= T0)) return;
	double s = (1.0 / std::min(T, T1) - 1.0 / T0) / (1.0 / T1 - 1.0 / T0) * double(n - 1);
	int i = std::clamp(int(s), 0, n - 2);
	double fr = std::clamp(s - double(i), 0.0, 1.0);
	for (int j = 0; j < 3; j++) {
		double a = ln[size_t(i) * 3 + j], b = ln[size_t(i + 1) * 3 + j];
		double v = a + (b - a) * fr;
		rgb[j] = v > -59.0 ? std::exp(v) : 0.0;
	}
}

void SupportSet (const std::vector<std::array<double, 3>> &pts, std::vector<std::array<double, 3>> &out, int dirs)
{
	out.clear();
	if (pts.empty()) return;
	std::vector<size_t> idx;
	const double ga = HPI * (3.0 - std::sqrt(5.0)); // golden angle
	for (int i = 0; i < dirs; i++) {
		double z = 1.0 - (2.0 * i + 1.0) / dirs, r = std::sqrt(std::max(0.0, 1.0 - z * z)), p = ga * i;
		double d[3] = { r * std::cos(p), z, r * std::sin(p) };
		size_t best = 0;
		double bv = -1e300;
		for (size_t k = 0; k < pts.size(); k++) {
			double v = pts[k][0] * d[0] + pts[k][1] * d[1] + pts[k][2] * d[2];
			if (v > bv) { bv = v; best = k; }
		}
		idx.push_back(best);
	}
	std::sort(idx.begin(), idx.end());
	idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
	for (size_t k : idx) out.push_back(pts[k]);
}

std::array<double, 3> SupportQuery (const std::vector<std::array<double, 3>> &set, const double d[3])
{
	std::array<double, 3> best = { 0.0, 0.0, 0.0 };
	double bv = -1e300;
	for (const auto &p : set) {
		double v = p[0] * d[0] + p[1] * d[1] + p[2] * d[2];
		if (v > bv) { bv = v; best = p; }
	}
	return best;
}

void ShaderBins (const double lnB[NBIN][3], double G, double out[3])
{
	double t = std::clamp(std::log(std::max(G, 1e-30) * 0.5) * (1.0 / std::log(0.4)), 0.0, 9.0);
	int k = std::min(int(t), NBIN - 2);
	double f = t - double(k);
	for (int j = 0; j < 3; j++) out[j] = std::exp(std::max(lnB[k][j] + f * (lnB[k + 1][j] - lnB[k][j]), -60.0));
}

bool ParseHeatLine (const std::string &line0, HeatCfg &c)
{
	std::string line = line0.substr(0, line0.find(';'));
	std::istringstream is(line);
	std::string key;
	if (!(is >> key)) return false;
	for (char &ch : key) ch = char(std::toupper((unsigned char)ch));
	if (key != "HEAT") return false;
	std::string tok;
	while (is >> tok) {
		std::string k = tok, v;
		size_t e = tok.find('=');
		if (e != std::string::npos) { k = tok.substr(0, e); v = tok.substr(e + 1); }
		for (char &ch : k) ch = char(std::tolower((unsigned char)ch));
		char *end = nullptr;
		double x = v.empty() ? 0.0 : std::strtod(v.c_str(), &end);
		bool num = !v.empty() && end && *end == 0 && std::isfinite(x);
		if (k == "off" && v.empty()) c.off = true;
		else if (k == "on" && v.empty()) c.off = false;
		else if (k == "nose" && num) c.nose = std::clamp(x, 0.05, 20.0);
		else if (k == "emis" && num) c.emis = std::clamp(x, 0.05, 1.0);
		else if (k == "lag" && num) c.lag = std::clamp(x, 1.0, 600.0);
		else if (k == "glow" && num) c.glow = std::clamp(x, 0.0, 10.0);
		else c.bad++;
	}
	return true;
}

}
