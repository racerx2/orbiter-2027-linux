// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// custom: environment prefilters and the BRDF lookup texture of the PBR material path (design-pbr 7), ImageProcessing entries

#include "PbrBRDF.glsl"

layout(binding = 1, row_major, scalar) uniform PbrIBLPS	// pixel shader constants (IPI.glsl's are binding 0)
{
vec3  vDir;
vec3  vUp;
vec3  vCp;
float fRough;      // roughness of the mip being written
float fSrcSize;    // source cube face size at mip 0
float fSrcMips;    // source cube mip count
int   iSamples;
};

layout(binding = 4) uniform samplerCube tGamma;	// legacy env cube (display values)
layout(binding = 5) uniform samplerCube tSrc;	// linear source cube with mips

// face direction as EnvMapBlur.glsl builds it
vec3 FaceDir (float x, float y)
{
	return normalize(vDir - vUp * (y * 2.0 - 1.0) + vCp * (x * 2.0 - 1.0));
}

// mip 0 of the linear source: the legacy cube decoded
vec4 PSDecode (float x, float y)
{
	return vec4(SrgbToLinear(textureLod(tGamma, FaceDir(x, y), 0.0).rgb), 1.0);
}

// GGX specular prefilter, N = V = R; source mip from the sample's pdf
vec4 PSGgx (float x, float y)
{
	vec3 N = FaceDir(x, y);
	if (fRough < 0.01) return vec4(textureLod(tSrc, N, 0.0).rgb, 1.0);
	float a = fRough * fRough;
	vec3 t, b;
	Basis(N, t, b);
	float texel = 4.0 * PBR_PI / (6.0 * fSrcSize * fSrcSize);
	uint n = uint(max(iSamples, 1));
	vec3 sum = vec3(0.0);
	float w = 0.0;
	for (uint i = 0u; i < n; i++) {
		vec3 h = SampleGgx(Hammersley(i, n), a);
		vec3 H = t * h.x + b * h.y + N * h.z;
		vec3 L = 2.0 * h.z * H - N;
		float NdotL = dot(N, L);
		if (NdotL > 0.0) {
			float pdf = max(DGgx(h.z, a) * 0.25, 1e-6);
			float lod = clamp(0.5 * log2(1.0 / (float(n) * pdf * texel)) + 1.0, 0.0, fSrcMips - 1.0);
			sum += textureLod(tSrc, L, lod).rgb * NdotL;
			w += NdotL;
		}
	}
	return vec4(w > 0.0 ? sum / w : textureLod(tSrc, N, 0.0).rgb, 1.0);
}

// Charlie sheen prefilter: cosine samples weighted by the distribution
vec4 PSCharlie (float x, float y)
{
	vec3 N = FaceDir(x, y);
	vec3 t, b;
	Basis(N, t, b);
	float lod = clamp(log2(fSrcSize / 32.0), 0.0, fSrcMips - 1.0);
	float r = max(fRough, 0.07);
	float e = 0.5 / max(r * r, 1e-3);
	uint n = uint(max(iSamples, 1));
	vec3 sum = vec3(0.0);
	float w = 0.0;
	for (uint i = 0u; i < n; i++) {
		vec3 l = SampleCosine(Hammersley(i, n));
		vec3 L = t * l.x + b * l.y + N * l.z;
		float nh = dot(N, normalize(L + N));
		float d = pow(clamp(2.0 * (1.0 - nh * nh), 0.0, 1.0), e); // D_Charlie up to a constant; sin^2 of the half angle is at most 1/2
		sum += textureLod(tSrc, L, lod).rgb * d;
		w += d;
	}
	return vec4(w > 0.0 ? sum / w : vec3(0.0), 1.0);
}

// diffuse: cosine-weighted mean radiance (irradiance / pi)
vec4 PSIrr (float x, float y)
{
	vec3 N = FaceDir(x, y);
	vec3 t, b;
	Basis(N, t, b);
	float lod = clamp(log2(fSrcSize / 16.0), 0.0, fSrcMips - 1.0);
	uint n = uint(max(iSamples, 1));
	vec3 sum = vec3(0.0);
	for (uint i = 0u; i < n; i++) {
		vec3 l = SampleCosine(Hammersley(i, n));
		sum += textureLod(tSrc, t * l.x + b * l.y + N * l.z, lod).rgb;
	}
	return vec4(sum / float(n), 1.0);
}

// x = N.V, y = roughness: r, g GGX split-sum scale and bias; b Charlie sheen directional albedo
vec4 PSLut (float x, float y)
{
	float NdotV = max(x, 1e-3);
	vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
	float r = max(y, 0.045);
	float a = r * r;
	float rs = max(y, 0.07);
	uint n = uint(max(iSamples, 1));
	float A = 0.0, B = 0.0, E = 0.0;
	for (uint i = 0u; i < n; i++) {
		vec2 xi = Hammersley(i, n);
		vec3 H = SampleGgx(xi, a);
		vec3 L = 2.0 * dot(V, H) * H - V;
		float NdotL = clamp(L.z, 0.0, 1.0);
		float NdotH = clamp(H.z, 0.0, 1.0);
		float VdotH = clamp(dot(V, H), 0.0, 1.0);
		if (NdotL > 0.0) {
			float g = VGgx(NdotL, NdotV, a) * 4.0 * NdotL * VdotH / max(NdotH, 1e-4);
			float fc = pow(1.0 - VdotH, 5.0);
			A += (1.0 - fc) * g;
			B += fc * g;
		}
		float ct = xi.y, st = sqrt(max(1.0 - ct * ct, 0.0)), ph = 2.0 * PBR_PI * xi.x;
		vec3 Ls = vec3(st * cos(ph), st * sin(ph), ct);
		vec3 Hs = normalize(Ls + V);
		E += DCharlie(Hs.z, rs) * VCharlie(Ls.z, NdotV, rs) * Ls.z;
	}
	A /= float(n);
	B /= float(n);
	E *= 2.0 * PBR_PI / float(n);
	return vec4(A, B, clamp(E, 0.0, 1.0), 1.0);
}

#if defined(PS_PSDecode) || defined(PS_PSGgx) || defined(PS_PSCharlie) || defined(PS_PSIrr) || defined(PS_PSLut)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_PSDecode
void main() { oColor = PSDecode(iX, iY); }
#endif
#ifdef PS_PSGgx
void main() { oColor = PSGgx(iX, iY); }
#endif
#ifdef PS_PSCharlie
void main() { oColor = PSCharlie(iX, iY); }
#endif
#ifdef PS_PSIrr
void main() { oColor = PSIrr(iX, iY); }
#endif
#ifdef PS_PSLut
void main() { oColor = PSLut(iX, iY); }
#endif
