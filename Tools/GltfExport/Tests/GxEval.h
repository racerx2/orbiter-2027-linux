// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport tests: glTF 2.0 node and channel evaluation (LINEAR, slerp) of a GxModel

#pragma once
#include "AnimModel.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

struct EvMat { double m[4][4]; }; // column vectors

static inline EvMat EvMul (const EvMat &a, const EvMat &b)
{
	EvMat r{};
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) r.m[i][j] += a.m[i][k] * b.m[k][j];
	return r;
}

static inline EvMat EvTRS (const double t[3], const double q[4], const double s[3])
{
	double x = q[0], y = q[1], z = q[2], w = q[3];
	EvMat r{};
	r.m[0][0] = (1 - 2 * (y * y + z * z)) * s[0]; r.m[0][1] = 2 * (x * y - w * z) * s[1]; r.m[0][2] = 2 * (x * z + w * y) * s[2];
	r.m[1][0] = 2 * (x * y + w * z) * s[0]; r.m[1][1] = (1 - 2 * (x * x + z * z)) * s[1]; r.m[1][2] = 2 * (y * z - w * x) * s[2];
	r.m[2][0] = 2 * (x * z - w * y) * s[0]; r.m[2][1] = 2 * (y * z + w * x) * s[1]; r.m[2][2] = (1 - 2 * (x * x + y * y)) * s[2];
	r.m[0][3] = t[0]; r.m[1][3] = t[1]; r.m[2][3] = t[2]; r.m[3][3] = 1;
	return r;
}

static inline void EvSample (const GxChannel &ch, double t, double *out)
{
	int nc = ch.path == GxPath::Rotation ? 4 : 3;
	size_t n = ch.times.size ();
	if (t <= ch.times[0]) { for (int i = 0; i < nc; i++) out[i] = ch.values[i]; return; }
	if (t >= ch.times[n - 1]) { for (int i = 0; i < nc; i++) out[i] = ch.values[(n - 1) * nc + i]; return; }
	size_t k = 0;
	while (k + 1 < n && ch.times[k + 1] < t) k++;
	double u = (t - ch.times[k]) / (ch.times[k + 1] - ch.times[k]);
	const float *a = &ch.values[k * nc], *b = &ch.values[(k + 1) * nc];
	if (nc == 3) { for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * u; return; }
	double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	double bs = d < 0 ? -1 : 1; // glTF slerp takes the short way
	d = std::fabs (d);
	double wa, wb;
	if (d > 0.9999999) { wa = 1 - u; wb = u; }
	else { double th = std::acos (d); wa = std::sin ((1 - u) * th) / std::sin (th); wb = std::sin (u * th) / std::sin (th); }
	double l = 0;
	for (int i = 0; i < 4; i++) { out[i] = wa * a[i] + wb * bs * b[i]; l += out[i] * out[i]; }
	l = std::sqrt (l);
	for (int i = 0; i < 4; i++) out[i] /= l;
}

// world matrices with each glTF animation at its time (absent: rest)
static inline std::vector<EvMat> EvWorld (const GxModel &m, const std::map<int, double> &animTime)
{
	std::vector<double> T (m.nodes.size () * 3), R (m.nodes.size () * 4, 0), S (m.nodes.size () * 3, 1);
	for (size_t i = 0; i < m.nodes.size (); i++) { T[i * 3] = m.nodes[i].t.x; T[i * 3 + 1] = m.nodes[i].t.y; T[i * 3 + 2] = m.nodes[i].t.z; R[i * 4 + 3] = 1; }
	for (const GxGltfAnim &a : m.anims) {
		auto it = animTime.find (a.anim);
		if (it == animTime.end ()) continue;
		for (const GxChannel &c : a.channels) {
			double *dst = c.path == GxPath::Rotation ? &R[c.node * 4] : c.path == GxPath::Translation ? &T[c.node * 3] : &S[c.node * 3];
			EvSample (c, it->second, dst);
		}
	}
	std::vector<EvMat> W (m.nodes.size ());
	std::vector<int> done (m.nodes.size (), 0);
	std::function<void (int)> eval = [&](int i) {
		if (done[i]) return;
		EvMat L = EvTRS (&T[i * 3], &R[i * 4], &S[i * 3]);
		int p = m.nodes[i].parent;
		if (p >= 0) { eval (p); W[i] = EvMul (W[p], L); } else W[i] = L;
		done[i] = 1;
	};
	for (size_t i = 0; i < m.nodes.size (); i++) eval ((int)i);
	return W;
}

static inline GxVec3 EvApply (const EvMat &W, const GxVec3 &p)
{
	return {W.m[0][0] * p.x + W.m[0][1] * p.y + W.m[0][2] * p.z + W.m[0][3],
		W.m[1][0] * p.x + W.m[1][1] * p.y + W.m[1][2] * p.z + W.m[1][3],
		W.m[2][0] * p.x + W.m[2][1] * p.y + W.m[2][2] * p.z + W.m[2][3]};
}
