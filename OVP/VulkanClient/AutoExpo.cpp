// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: auto-exposure controller (design-ground-graphics 6); night look maths (design-night-look)

#include "AutoExpo.h"
#include <algorithm>
#include <cmath>

namespace AutoExpo {

void Controller::Reset ()
{
	ev = 0.0f;
	fresh = true;
	for (int i = 0; i < NHIST; i++) { histStamp[i] = 0; histEV[i] = 0.0f; histBias[i] = 0.0f; }
	histNext = 0;
}

void Controller::SetParams (const Params &p)
{
	prm = p;
	ev = std::min(std::max(ev, prm.minEV), prm.maxEV);
}

float Controller::Exposure () const
{
	return std::exp2(ev + bias);
}

void Controller::Used (uint32_t stamp)
{
	histStamp[histNext] = stamp;
	histEV[histNext] = ev;
	histBias[histNext] = bias;
	histNext = (histNext + 1) % NHIST;
}

void Controller::SetNight (float n, bool internal)
{
	night = std::isfinite(n) ? std::min(std::max(n, 0.0f), 1.0f) : 0.0f;
	band = internal ? 0.0f : 1.0f;
}

float Controller::SatThreshold () const
{
	const float day = 254.0f / 255.0f;
	float n = Night();
	return n > 0.0f ? day + (prm.satThrNight - day) * n : day;
}

float Controller::Target (const Sample &s, float evUsed, float *tau) const
{
	float l2 = s.meanLog2;
	float n = Night();
	if (n <= 0.0f) { // by day, or the night look off: ground-graphics 6 as it was
		float toLo = std::log2(prm.lo) - l2, toHi = std::log2(prm.hi) - l2;
		if (s.sat > prm.satLimit) { // saturation only ever lowers e
			*tau = prm.tauDown;
			return evUsed + std::min(0.0f, toHi) - 2.0f * (s.sat - prm.satLimit);
		}
		if (toLo > 0.0f) { *tau = prm.tauUp; return evUsed + toLo; }
		if (toHi < 0.0f) { *tau = prm.tauDown; return evUsed + toHi; }
		*tau = prm.tauDrift; // in the band: back toward 0, as far as the band allows
		return std::min(std::max(0.0f, evUsed + toLo), evUsed + toHi);
	}
	float toLo = std::log2(prm.lo) - prm.nightDropEV * n * band - l2, toHi = std::log2(prm.hi) - l2; // a dark night scene is not lifted to the day band
	if (s.sat > prm.satLimit) {
		*tau = prm.tauDown;
		return evUsed + std::min(0.0f, toHi) - 2.0f * (s.sat - prm.satLimit);
	}
	float t;
	if (toLo > 0.0f) { *tau = prm.tauUp; t = evUsed + toLo; }
	else if (toHi < 0.0f) { *tau = prm.tauDown; t = evUsed + toHi; }
	else { *tau = prm.tauDrift; t = std::min(std::max(0.0f, evUsed + toLo), evUsed + toHi); }
	float bw = band > 0.0f ? 1.0f : 3.0f; // cockpit: MFDs, HUD and panel lamps are near-saturated already, the window is 3x wider
	if (t > evUsed) t = evUsed + (t - evUsed) * (1.0f - n * NightLook::Smooth(prm.brake0 * bw, prm.brake1 * bw, s.sat)); // near-saturated lights brake any rise
	return t;
}

bool Controller::Update (const Sample &s, uint32_t stamp, float dt, float topEV)
{
	if (stamp == 0 || !(s.weight >= prm.minWeight) || !std::isfinite(s.meanLog2) || !std::isfinite(s.sat)) return false;
	int k = -1;
	for (int i = 0; i < NHIST; i++) if (histStamp[i] == stamp) k = i;
	if (k < 0) return false;
	histStamp[k] = 0; // each measure counts once
	Sample u = s;
	u.meanLog2 -= histBias[k]; // the frame as rendered without the bias
	float tau;
	float t = Target(u, histEV[k], &tau);
	float n = Night();
	float top = std::min(prm.maxEV, topEV);
	if (n > 0.0f) top = std::min(top, prm.maxEV + (prm.nightMaxEV - prm.maxEV) * n * band);
	t = std::min(std::max(t, prm.minEV), top);
	if (s.sat > prm.satLimit) t = std::min(t, ev);
	bool snap = fresh && s.weight >= prm.snapWeight && t < ev; // first measure after Reset: darken at once
	fresh = false;
	float speed = std::isfinite(prm.speed) ? std::min(std::max(prm.speed, 0.25f), 4.0f) : 1.0f;
	float a = snap ? 1.0f : (dt > 0.0f ? 1.0f - std::exp(-dt * speed / tau) : 0.0f);
	ev += (t - ev) * a;
	ev = std::min(std::max(ev, prm.minEV), prm.maxEV);
	return true;
}

float NightWeight (float sinElev, float cosDip)
{
	if (!std::isfinite(sinElev) || !std::isfinite(cosDip)) return 0.0f;
	float se = std::min(std::max(sinElev, -1.0f), 1.0f), cd = std::min(std::max(cosDip, 0.0f), 1.0f);
	float s = se * cd + std::sqrt(1.0f - se * se) * std::sqrt(1.0f - cd * cd); // sin(elevation + dip): a high camera is in the sun longer
	if (se > 0.0f && std::sqrt(1.0f - se * se) * cd - se * std::sqrt(1.0f - cd * cd) < 0.0f) s = 1.0f; // the sum past 90 deg: the sun is up, no fold back
	return NightLook::Smooth(0.0523360f, -0.1564345f, s); // sin(+3 deg), sin(-9 deg)
}

float NightWeightAt (double cx, double cy, double cz, double sx, double sy, double sz, double R)
{
	double r = std::sqrt(cx * cx + cy * cy + cz * cz);
	if (!(r > 0.0) || !(R > 0.0)) return 0.0f;
	return NightWeight(float((cx * sx + cy * sy + cz * sz) / r), float(std::min(1.0, R / r)));
}

} // namespace AutoExpo

namespace NightLook {

const float Pal[3][3] = {{1.0f, 0.54f, 0.07f}, {1.0f, 0.71f, 0.42f}, {1.0f, 0.82f, 0.64f}}; // sodium 2000 K, LED 3000 K, LED 4000 K
const float PalW[3] = {0.5f, 0.3f, 0.2f};

float Smooth (float e0, float e1, float x)
{
	float t = std::min(std::max((x - e0) / (e1 - e0), 0.0f), 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

float CellDusk (float elevDeg, float h)
{
	float d = std::min(std::max((2.0f - elevDeg) / 10.0f, 0.0f), 1.0f); // 0 at +2 deg, 1 at -8 deg
	return std::min(std::max(1.2f * d - 0.2f * h, 0.0f), 1.0f);
}

float WindowOn (float a, float D, float lod)
{
	float w = std::min(std::max(0.03f + 0.03f * std::max(lod, 0.0f), 0.03f), 0.15f);
	return Smooth(a - w, a + w, D) * Smooth(0.0f, 0.05f, D);
}

float CityLevel (float r, float g, float b)
{
	float m = std::max(std::max(r, g), b);
	return std::pow(std::min(std::max(m, 0.0f), 1.0f), 2.2f);
}

float SegShare (float city)
{
	return 0.45f + 0.5f * city;
}

static float Fract (float x) { return x - std::floor(x); }
static float Mod (float x, float p) { return x - p * std::floor(x / p); }

float ClsHash (float x, float y) // hash12 of "Hash without Sine", Dave Hoskins, MIT (CREDITS), as NewPlanet.glsl
{
	float p0 = Fract(x * 0.1031f), p1 = Fract(y * 0.1031f), p2 = p0;
	float d = p0 * (p1 + 33.33f) + p1 * (p2 + 33.33f) + p2 * (p0 + 33.33f);
	p0 += d; p1 += d; p2 += d;
	return Fract((p0 + p1) * p2);
}

float ClsNoise (float x, float y, float P)
{
	float ix = std::floor(x), iy = std::floor(y), fx = x - ix, fy = y - iy;
	fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
	float ax = Mod(ix, P), ay = Mod(iy, P), bx = Mod(ix + 1.0f, P), by = Mod(iy + 1.0f, P);
	float a = ClsHash(ax, ay), b = ClsHash(bx, ay), c = ClsHash(ax, by), d = ClsHash(bx, by);
	float u = a + (b - a) * fx, v = c + (d - c) * fx;
	return u + (v - u) * fy;
}

static int PalOf (float t) { return t < 0.5f ? 0 : (t < 0.8f ? 1 : 2); }

void Warp (float qx, float qy, float &wx, float &wy)
{
	wx = qx + 0.5f * (ClsNoise(qx * 0.125f, qy * 0.125f, 128.0f) - 0.5f);
	wy = qy + 0.5f * (ClsNoise(qx * 0.125f + 37.0f, qy * 0.125f + 71.0f, 128.0f) - 0.5f);
}

Lamps LampMeans (float pSeg)
{
	const float pi = 3.14159265f;
	Lamps m;
	float n = 2.0f * PerBlock * pSeg * Exist;
	m.pool = n * pi * Sp * Sp;
	m.head = n * pi * Sh * Sh;
	m.spill = 2.0f * pSeg * 3.0f * Hw;
	for (int c = 0; c < 3; c++) m.col[c] = PalW[0] * Pal[0][c] + PalW[1] * Pal[1][c] + PalW[2] * Pal[2][c];
	return m;
}

// per street family: lamp points (nearest lamp and the nearer neighbour), a band along the line when they are under 2 px apart, the area mean when lines are 3 px apart
Lamps LampField (float wx, float wy, float fx, float fy, float pSeg)
{
	const float pi = 3.14159265f, rpi = 1.7724539f;
	Lamps m = LampMeans(pSeg), r {0.0f, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}};
	float cw[3] = {0.0f, 0.0f, 0.0f};
	for (int k = 0; k < 2; k++) {
		float u = k == 0 ? wx : wy, v = k == 0 ? wy : wx, fu = k == 0 ? fx : fy, fv = k == 0 ? fy : fx;
		float tA = Smooth(Area0, Area1, fu), tL = Smooth(Line0, Line1, fv);
		float pool = 0.0f, head = 0.0f, spill = 0.0f, cp[3] = {0.0f, 0.0f, 0.0f};
		if (tA < 1.0f) {
			float li = std::floor(u + 0.5f), dp = u - li;
			li = Mod(li, 1024.0f);
			float hs = ClsHash(li + 1031.0f * k + 0.5f, Mod(std::floor(v), 1024.0f) + 0.5f);
			int ps = PalOf(hs / pSeg);
			float sxp = std::min(std::max(Sp, fu), 0.25f), sxh = std::min(std::max(Sh, fu), 0.25f), hw = std::min(std::max(Hw, fu), 0.25f); // half a block at most: only the nearest line is evaluated
			if (hs < pSeg) {
				spill = (1.0f - Smooth(hw, 2.0f * hw, std::fabs(dp))) * (Hw / hw);
				if (tL > 0.0f) { // the lamps of the segment as a band across the line, same energy a unit length
					float bp = PerBlock * Exist * pi * Sp * Sp / (rpi * sxp) * std::exp(-dp * dp / (sxp * sxp));
					float bh = PerBlock * Exist * pi * Sh * Sh / (rpi * sxh) * std::exp(-dp * dp / (sxh * sxh));
					pool += tL * bp; head += tL * bh;
					for (int c = 0; c < 3; c++) cp[c] += Pal[ps][c] * tL * bp;
				}
			}
			if (tL < 1.0f) {
				float syp = std::max(Sp, fv), syh = std::max(Sh, fv);
				float a = v * PerBlock, j = std::floor(a), f = a - j;
				for (int n = 0; n < 2; n++) {
					float jj = j + (n == 0 ? 0.0f : (f > 0.5f ? 1.0f : -1.0f));
					float jm = Mod(jj, 4096.0f);
					float hl = ClsHash(li + 1031.0f * k + 0.5f, std::floor(jm * 0.25f) + 0.5f); // the lamp's own segment
					if (hl >= pSeg || ClsHash(jm + 4099.0f * k + 0.25f, li + 0.25f) >= Exist) continue;
					float side = Mod(jm, 2.0f) < 0.5f ? Side : -Side;
					float dx = dp - side, dy = (a - jj - 0.5f) / PerBlock;
					float c = (1.0f - tL) * (0.8f + 0.4f * ClsHash(li + 0.75f, jm + 8209.0f * k + 0.75f));
					float gp = c * std::exp(-dx * dx / (sxp * sxp) - dy * dy / (syp * syp)) * Sp * Sp / (sxp * syp);
					float gh = c * std::exp(-dx * dx / (sxh * sxh) - dy * dy / (syh * syh)) * Sh * Sh / (sxh * syh);
					pool += gp; head += gh;
					int pl = PalOf(hl / pSeg);
					for (int ch = 0; ch < 3; ch++) cp[ch] += Pal[pl][ch] * gp;
				}
			}
		}
		float fm = 0.5f; // a family's share of the means
		r.pool += pool + (m.pool * fm - pool) * tA;
		r.head += head + (m.head * fm - head) * tA;
		r.spill += spill + (m.spill * fm - spill) * tA;
		for (int c = 0; c < 3; c++) cw[c] += cp[c] + (m.col[c] * m.pool * fm - cp[c]) * tA;
	}
	for (int c = 0; c < 3; c++) r.col[c] = r.pool > 0.0f ? cw[c] / r.pool : m.col[c];
	return r;
}

void NiteHalf (const std::vector<uint8_t> &s, int w, int h, std::vector<uint8_t> &d)
{
	int W = std::max(1, w / 2), H = std::max(1, h / 2);
	d.assign((size_t)W * H * 4, 0);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int sx = std::min(w - 1, 2 * x), sy = std::min(h - 1, 2 * y), sx1 = std::min(w - 1, sx + 1), sy1 = std::min(h - 1, sy + 1);
			const uint8_t *q[4] = {&s[((size_t)sy * w + sx) * 4], &s[((size_t)sy * w + sx1) * 4], &s[((size_t)sy1 * w + sx) * 4], &s[((size_t)sy1 * w + sx1) * 4]};
			uint8_t *o = &d[((size_t)y * W + x) * 4];
			long long l = 0, la = 0;
			int a = 0;
			for (int c = 0; c < 3; c++) o[c] = (uint8_t)((q[0][c] + q[1][c] + q[2][c] + q[3][c] + 2) / 4);
			for (int k = 0; k < 4; k++) {
				long long lk = 2126LL * q[k][0] + 7152LL * q[k][1] + 722LL * q[k][2];
				l += lk; la += lk * q[k][3]; a += q[k][3];
			}
			o[3] = (uint8_t)(l > 0 ? (la + l / 2) / l : (a + 2) / 4); // the lit children's on-time; dark ones do not move it
		}
}

static uint32_t Hm (uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

void NiteNest (const std::vector<uint8_t> &s, int w, int h, int level, std::vector<uint8_t> &d)
{
	int W = std::max(1, w / 2), H = std::max(1, h / 2);
	d.assign((size_t)W * H * 4, 0);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			uint32_t hh = Hm((uint32_t)x * 0x9E3779B1u ^ Hm((uint32_t)y * 0x85EBCA77u ^ Hm((uint32_t)level * 0xC2B2AE3Du)));
			int cx = std::min(w - 1, 2 * x + (int)((hh >> 7) & 1)), cy = std::min(h - 1, 2 * y + (int)((hh >> 13) & 1));
			for (int c = 0; c < 4; c++) d[((size_t)y * W + x) * 4 + c] = s[((size_t)cy * w + cx) * 4 + c];
		}
}

} // namespace NightLook
