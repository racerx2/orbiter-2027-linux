// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// custom: BRDF terms of the glTF 2.0 metal-rough model and the KHR material extensions (design-pbr 6), written from the specs

#ifndef PBR_BRDF_GLSL
#define PBR_BRDF_GLSL

#define PBR_PI 3.14159265359

float Max3 (vec3 v) { return max(max(v.r, v.g), v.b); }
float Sq1 (float x) { return x * x; }

// sRGB transfer, values above 1 kept on the curve's extension
vec3 SrgbToLinear (vec3 c)
{
	c = max(c, vec3(0));
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 LinearToSrgb (vec3 c)
{
	c = max(c, vec3(0));
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// Fresnel (Schlick)
vec3 FSchlick (vec3 f0, vec3 f90, float c)
{
	float x = clamp(1.0 - c, 0.0, 1.0);
	float x2 = x * x;
	return f0 + (f90 - f0) * (x2 * x2 * x);
}

float FSchlick1 (float f0, float f90, float c)
{
	float x = clamp(1.0 - c, 0.0, 1.0);
	float x2 = x * x;
	return f0 + (f90 - f0) * (x2 * x2 * x);
}

// GGX / Trowbridge-Reitz distribution, a = perceptual roughness squared
float DGgx (float NdotH, float a)
{
	float a2 = a * a;
	float f = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / (PBR_PI * f * f);
}

// height-correlated Smith visibility, G / (4 N.L N.V)
float VGgx (float NdotL, float NdotV, float a)
{
	float a2 = a * a;
	float l = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
	float v = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
	float s = l + v;
	return s > 0.0 ? 0.5 / s : 0.0;
}

// anisotropic GGX (KHR_materials_anisotropy): at along the anisotropy direction, ab across it
float DGgxAniso (float NdotH, float TdotH, float BdotH, float at, float ab)
{
	float a2 = at * ab;
	vec3 f = vec3(ab * TdotH, at * BdotH, a2 * NdotH);
	float w2 = a2 / dot(f, f);
	return a2 * w2 * w2 / PBR_PI;
}

float VGgxAniso (float NdotL, float NdotV, float TdotV, float BdotV, float TdotL, float BdotL, float at, float ab)
{
	float v = NdotL * length(vec3(at * TdotV, ab * BdotV, NdotV));
	float l = NdotV * length(vec3(at * TdotL, ab * BdotL, NdotL));
	float s = v + l;
	return s > 0.0 ? clamp(0.5 / s, 0.0, 1.0) : 0.0;
}

// Charlie sheen distribution (Estevez, Kulla 2017), r = sheen roughness
float DCharlie (float NdotH, float r)
{
	float a = max(r * r, 1e-3);
	float inv = 1.0 / a;
	float sin2 = max(1.0 - NdotH * NdotH, 0.0);
	return (2.0 + inv) * pow(sin2, inv * 0.5) / (2.0 * PBR_PI);
}

float CharlieL (float x, float a)
{
	float t = Sq1(1.0 - a);
	float ca = mix(21.5473, 25.3245, t);
	float cb = mix(3.82987, 3.32435, t);
	float cc = mix(0.19823, 0.16801, t);
	float cd = mix(-1.97760, -1.27393, t);
	float ce = mix(-4.32054, -4.85967, t);
	return ca / (1.0 + cb * pow(max(x, 1e-6), cc)) + cd * x + ce;
}

float CharlieLambda (float c, float a)
{
	return abs(c) < 0.5 ? exp(CharlieL(abs(c), a)) : exp(2.0 * CharlieL(0.5, a) - CharlieL(1.0 - abs(c), a));
}

float VCharlie (float NdotL, float NdotV, float r)
{
	float a = max(r * r, 1e-3);
	float d = (1.0 + CharlieLambda(NdotV, a) + CharlieLambda(NdotL, a)) * (4.0 * NdotV * NdotL);
	return d > 0.0 ? clamp(1.0 / d, 0.0, 1.0) : 0.0;
}

// thin-film iridescence (Belcour, Barla 2017; KHR_materials_iridescence)
vec3 IorToF0 (vec3 t, float i) { return (t - i) * (t - i) / ((t + i) * (t + i)); }
float IorToF01 (float t, float i) { return Sq1((t - i) / (t + i)); }
vec3 F0ToIor (vec3 f0) { vec3 s = sqrt(clamp(f0, 0.0, 0.9999)); return (1.0 + s) / (1.0 - s); }

vec3 FilmSensitivity (float opd, vec3 shift)
{
	float phase = 2.0 * PBR_PI * opd * 1.0e-9;
	vec3 val = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
	vec3 pos = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
	vec3 var = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);
	vec3 xyz = val * sqrt(2.0 * PBR_PI * var) * cos(pos * phase + shift) * exp(-phase * phase * var);
	xyz.x += 9.7470e-14 * sqrt(2.0 * PBR_PI * 4.5282e+09) * cos(2.2399e+06 * phase + shift.x) * exp(-4.5282e+09 * phase * phase);
	xyz /= 1.0685e-7;
	const mat3 toRgb = mat3(3.2404542, -0.9692660, 0.0556434, -1.5371385, 1.8760108, -0.2040259, -0.4985314, 0.0415560, 1.0572252);
	return toRgb * xyz;
}

vec3 ThinFilmFresnel (float filmIor, float cos1, float thickness, vec3 baseF0)
{
	float n2 = mix(1.0, filmIor, smoothstep(0.0, 0.03, thickness));
	float sin2Sq = Sq1(1.0 / n2) * (1.0 - cos1 * cos1);
	float cos2Sq = 1.0 - sin2Sq;
	if (cos2Sq < 0.0) return vec3(1.0);
	float cos2 = sqrt(cos2Sq);
	float r12 = FSchlick1(IorToF01(n2, 1.0), 1.0, cos1);
	float t121 = 1.0 - r12;
	float phi12 = n2 < 1.0 ? PBR_PI : 0.0;
	float phi21 = PBR_PI - phi12;
	vec3 n3 = F0ToIor(baseF0);
	vec3 r23 = FSchlick(IorToF0(n3, n2), vec3(1.0), cos2);
	vec3 phi23 = vec3(n3.x < n2 ? PBR_PI : 0.0, n3.y < n2 ? PBR_PI : 0.0, n3.z < n2 ? PBR_PI : 0.0);
	float opd = 2.0 * n2 * thickness * cos2;
	vec3 phi = vec3(phi21) + phi23;
	vec3 r123 = clamp(r12 * r23, 1e-5, 0.9999);
	vec3 sr123 = sqrt(r123);
	vec3 rs = t121 * t121 * r23 / (vec3(1.0) - r123);
	vec3 total = r12 + rs;
	vec3 cm = rs - t121;
	for (int m = 1; m <= 2; m++) {
		cm *= sr123;
		total += cm * 2.0 * FilmSensitivity(float(m) * opd, float(m) * phi);
	}
	return max(total, vec3(0.0));
}

// low-discrepancy samples for the prefilters
vec2 Hammersley (uint i, uint n)
{
	uint b = (i << 16u) | (i >> 16u);
	b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
	b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
	b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
	b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
	return vec2(float(i) / float(n), float(b) * 2.3283064365386963e-10);
}

// orthonormal frame around n
void Basis (vec3 n, out vec3 t, out vec3 b)
{
	vec3 up = abs(n.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
	t = normalize(cross(up, n));
	b = cross(n, t);
}

// GGX half vector around z for sample xi
vec3 SampleGgx (vec2 xi, float a)
{
	float phi = 2.0 * PBR_PI * xi.x;
	float c = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
	float s = sqrt(max(1.0 - c * c, 0.0));
	return vec3(s * cos(phi), s * sin(phi), c);
}

// cosine-weighted direction around z
vec3 SampleCosine (vec2 xi)
{
	float phi = 2.0 * PBR_PI * xi.x;
	float r = sqrt(xi.y);
	return vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - xi.y, 0.0)));
}

#endif
