// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// custom: glTF 2.0 metal-rough material path with the KHR extensions (design-pbr 6); legacy mesh shaders are not involved

#include "PbrBRDF.glsl"
#include "SceneShadow.glsl" // not upstream: CsmPrm (_CSM, design-scene-shadows)

#define Range   0
#define Falloff 1
#define Theta   2
#define Phi     3

#define SH_SIZE		0
#define SH_INVSIZE	1

// texture slots (PbrMaterial.h PbrSlot) and flags (PBRM_*)
#define T_BASE 0
#define T_METALROUGH 1
#define T_NORMAL 2
#define T_OCCLUSION 3
#define T_EMISSIVE 4
#define T_SPECULAR 5
#define T_SPECULARCOLOR 6
#define T_TRANSMISSION 7
#define T_THICKNESS 8
#define T_CLEARCOAT 9
#define T_CLEARCOATROUGH 10
#define T_CLEARCOATNORMAL 11
#define T_SHEENCOLOR 12
#define T_SHEENROUGH 13
#define T_ANISOTROPY 14
#define T_IRIDESCENCE 15
#define T_IRIDTHICK 16

#define PBRM_BASEGAMMA 1
#define PBRM_GREENUP 2
#define PBRM_DOUBLESIDED 4
#define PBRM_FULLYLIT 8
#define PBRM_NOCOLOR 16
#define PBRM_EMISGAMMA 32
#define PBRM_SPECCOLGAMMA 64
#define PBRM_SHEENGAMMA 128

// Must match D3D9Client.fx (copied from the legacy effect by name)
struct Sun
{
	vec3 Dir;
	vec3 Color;
	vec3 Ambient;
	vec3 Transmission;
	vec3 Inscatter;
};

struct Light
{
	int      type;
	float    dst2;
	vec4     diffuse;
	vec3     position;
	vec3     direction;
	vec3     attenuation;
	vec4     param;
};

// Must match PbrMtrlGPU in PbrMaterial.h
struct PbrMtrl
{
	vec4  baseColor;
	vec3  emissive; float emissiveStrength;
	float metallic; float roughness; float normalScale; float occlusion;
	float alphaCutoff; int alphaMode; int texMask; int flags;
	float ior; float specular; vec3 specularColor;
	float transmission; float thickness; float attDistance; vec3 attColor;
	float clearcoat; float clearcoatRough; float clearcoatNormalScale;
	vec3  sheenColor; float sheenRough;
	float anisoStrength; float anisoRotation;
	float iridescence; float iridIor; float iridThickMin; float iridThickMax;
};

#if defined(_SSR)
#include "SsrPrm.glsl" // not upstream: screen-space reflections (design-ssr)
#endif

#if defined(_HEAT)
#include "HeatPrm.glsl" // not upstream: re-entry hull heating (design-reentry-heat)
#endif

layout(binding = 0, row_major, scalar) uniform PbrShared
{
vec3      kernel[KERNEL_SIZE];
mat4      gW;
mat4      gLVP;
mat4      gVP;
vec4      gColor;
vec4      gAtmColor;
vec4      gRadius;
vec4      gSHD;
vec3      gCameraPos;
vec3      gNorth;
vec3      gEast;
Sun       gSun;
Light     gLights[MAX_LIGHTS];
bool      gLightsEnabled;
bool      gShadowsEnabled;
float     gProxySize;
float     gInvProxySize;
float     gGlowConst;
float     gExposure;       // auto-exposure, 1 off the main pass (shared from D3D9Client.fx)
vec4      gScreen;         // scene copy size: w, h, 1/w, 1/h
float     gEnvMips;        // GGX cube: highest mip
float     gSheenMips;      // sheen cube: highest mip
float     gWorldScale;     // world size of one mesh unit (volume thickness)
int       gEnvMode;        // 0 none, 1 legacy cube, 2 prefiltered cubes
int       gIrrMode;        // 0 earth glow, 1 legacy irradiance map, 2 irradiance cube
bool      gSceneCopy;      // the opaque scene copy is bound
#if defined(_CSM)
CsmPrm    gCsm;            // not upstream: scene shadow cascades, shared from D3D9Client.fx (design-scene-shadows Rev 2)
#endif
#if defined(_SSR)
SsrPrm    gSsr;            // not upstream: screen-space reflections of this draw, shared from D3D9Client.fx (design-ssr), after the shadows' gCsm
#endif
#if defined(_HEAT)
HeatPrm   gHeat;           // not upstream: re-entry hull heating, shared from D3D9Client.fx (design-reentry-heat), after gSsr (INT-2)
#endif
};

layout(binding = 1, row_major, scalar) uniform PbrMaterialBlock
{
PbrMtrl   gPbr;
};

sampler2D ShadowS = sampler_state { MinFilter = POINT; MagFilter = POINT; MipFilter = POINT; AddressU = CLAMP; AddressV = CLAMP; };
sampler2D IrradS = sampler_state { MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR; AddressU = CLAMP; AddressV = CLAMP; };
samplerCube EnvIrrS = sampler_state { MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR; AddressU = CLAMP; AddressV = CLAMP; AddressW = CLAMP; };
samplerCube EnvGgxS = sampler_state { MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR; AddressU = CLAMP; AddressV = CLAMP; AddressW = CLAMP; };
samplerCube EnvSheenS = sampler_state { MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR; AddressU = CLAMP; AddressV = CLAMP; AddressW = CLAMP; };
samplerCube EnvLegacyS = sampler_state { MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR; AddressU = CLAMP; AddressV = CLAMP; AddressW = CLAMP; };
sampler2D LutS = sampler_state { MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = NONE; AddressU = CLAMP; AddressV = CLAMP; };
sampler2D SceneS = sampler_state { MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR; AddressU = CLAMP; AddressV = CLAMP; };
sampler2D BaseS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D MetalRoughS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D NormalS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D OcclusionS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D EmissiveS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D SpecularS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D SpecularColorS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D TransmissionS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D ThicknessS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D ClearcoatS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D ClearcoatRoughS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D ClearcoatNormalS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D SheenColorS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D SheenRoughS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D AnisoS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D IridescenceS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };
sampler2D IridThickS = sampler_state { MinFilter = ANISOTROPIC; MagFilter = LINEAR; MipFilter = LINEAR; MaxAnisotropy = ANISOTROPY_MACRO; AddressU = WRAP; AddressV = WRAP; };

#if defined(_CSM) // not upstream: the scene shadow atlas, binding 29, before SSR's
sampler2D CsmS = sampler_state { MinFilter = POINT; MagFilter = POINT; MipFilter = NONE; AddressU = CLAMP; AddressV = CLAMP; };
#endif

// Light and shadow functions of the legacy shaders (ComputeShadow, Paraboloidal_LVLH)
#include "Common.glsl"
#include "SceneShadow.glsl" // not upstream: CsmVis (_CSM, pixel stage)

#if defined(_SSR) // not upstream: screen-space reflections (design-ssr); after the shadows' CsmS in the text, so 30 and 31
sampler2D tSsrCol = sampler_state { MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR; AddressU = CLAMP; AddressV = CLAMP; };
sampler2D tSsrHiZ = sampler_state { MinFilter = POINT; MagFilter = POINT; MipFilter = POINT; AddressU = CLAMP; AddressV = CLAMP; };
#include "Ssr.glsl"
#endif

#if defined(_HEAT)
#include "HullHeat.glsl" // not upstream: re-entry hull heating, pixel stage (design-reentry-heat)
#endif


// ============================================================================
// Vertex shader
// ============================================================================

struct PbrVS
{
	vec4 posH;
	vec3 camW;     // camera-relative position, negated
	vec2 tex0;
	vec3 nrmW;
	vec4 tanW;     // handedness in w
	vec4 shdH;
};

PbrVS PbrMeshVS (vec3 posL, vec3 nrmL, vec3 tanL, vec3 tex0)
{
	PbrVS o;
	vec3 posW = (vec4(posL, 1.0) * gW).xyz;
	o.nrmW = (vec4(nrmL, 0.0) * gW).xyz;
	o.tanW = vec4((vec4(tanL, 0.0) * gW).xyz, tex0.z);
	o.posH = vec4(posW, 1.0) * gVP;
	o.camW = -posW;
	o.tex0 = tex0.xy;
	o.shdH = vec4(posW, 1.0) * gLVP;
	return o;
}

#ifdef VS_PbrMeshVS
layout(location = 0) in vec3 iPosL;
layout(location = 1) in vec3 iNrmL;
layout(location = 2) in vec3 iTanL;
layout(location = 5) in vec3 iTex0;
layout(location = 0) out PbrVS oVS;
void main() { oVS = PbrMeshVS(iPosL, iNrmL, iTanL, iTex0); gl_Position = oVS.posH; }
#endif


// ============================================================================
// Pixel shader
// ============================================================================

#ifdef STAGE_PS

struct Surf
{
	vec3  posW, V, N, Ng, T, B;
	vec3  base;
	float metallic, perceptual, a, NdotV;
	float tRough;	// material roughness, no floor (blur of the scene seen through)
	vec3  f0d;
	float specW;
	float trans, thick;
	vec3  irD, irM;
	float irid;
	vec3  At, Ab, Nbent;
	float at, ab;
	float cc, ccPerc, cca, NcV;
	vec3  Nc;
	vec3  sheen;
	float sheenR, sheenEv;
#if defined(_SSR)
	vec3  Nf;		// not upstream: the drawn face toward the camera, where reflection rays start (r3-ssr2)
#endif
};

bool HasMap (int s) { return (gPbr.texMask & (1 << s)) != 0; }
bool Flag (int f) { return (gPbr.flags & f) != 0; }

// legacy colours (sun, lights, ambient, legacy maps, scene) are display values
vec3 Decode (vec3 c) { return SrgbToLinear(c); }

vec3 EarthGlow (vec3 d)
{
	float angl = clamp((-dot(gCameraPos, d) - gProxySize) * gInvProxySize, 0.0, 1.0);
	return gAtmColor.rgb * max(0.0, angl * gGlowConst) + gSun.Ambient;
}

vec3 Prefiltered (vec3 R, float perc)
{
	if (gEnvMode == 2) return textureLod(EnvGgxS, R, perc * gEnvMips).rgb;
	if (gEnvMode == 1) return Decode(textureLod(EnvLegacyS, R, perc * 4.0).rgb);
	return Decode(EarthGlow(R));
}

vec3 Irradiance (vec3 N)
{
	if (gIrrMode == 2) return max(texture(EnvIrrS, N).rgb, Decode(gSun.Ambient));
	if (gIrrMode == 1) {
		vec3 c = Paraboloidal_LVLH(IrradS, N).rgb;
		return Decode(max(c * c, gSun.Ambient));
	}
	return Decode(EarthGlow(N));
}

vec3 SheenEnv (vec3 N, float r)
{
	if (gEnvMode == 2) return textureLod(EnvSheenS, N, r * gSheenMips).rgb;
	return Irradiance(N);
}

vec3 VolumeAtt (Surf s)
{
	if (s.thick <= 0.0 || gPbr.attDistance <= 0.0) return vec3(1.0);
	return pow(max(gPbr.attColor, vec3(1e-4)), vec3(s.thick * gWorldScale / gPbr.attDistance));
}

void Fresnel (Surf s, float c, out vec3 Fd, out vec3 Fm)
{
	Fd = s.specW * FSchlick(s.f0d, vec3(1.0), c);
	Fm = FSchlick(s.base, vec3(1.0), c);
#ifdef PBR_IRIDESCENCE
	Fd = mix(Fd, s.specW * s.irD, s.irid);
	Fm = mix(Fm, s.irM, s.irid);
#endif
}

// background seen through the surface (KHR_materials_transmission, KHR_materials_volume)
vec3 Transmitted (Surf s)
{
	vec3 dir = refract(-s.V, s.N, 1.0 / gPbr.ior);
	if (dot(dir, dir) < 1e-6) dir = -s.V;
	vec3 bg;
	if (gSceneCopy) {
		vec3 p = s.posW + dir * (s.thick * gWorldScale);
		vec4 h = vec4(p, 1.0) * gVP;
		vec2 uv = vec2(0.5 + 0.5 * h.x / h.w, 0.5 - 0.5 * h.y / h.w);
		uv = clamp(uv, gScreen.zw * 0.5, vec2(1.0) - gScreen.zw * 0.5);
		vec3 c = textureLod(SceneS, uv, s.tRough * log2(max(gScreen.x, gScreen.y))).rgb;
		c = (c - gSun.Inscatter) / max(gSun.Transmission, vec3(1e-3));
#if !defined(_LIGHTGLOW)
		c = clamp(c, 0.0, 0.99);
		c = c / pow(vec3(1.0) - c * c * c * c, vec3(0.25));
#endif
		c /= max(gExposure, 1e-3); // the copy holds e already: not twice behind glass
		bg = Decode(c);
	}
	else bg = Prefiltered(dir, s.perceptual);
	return bg * s.base * VolumeAtt(s);
}

// one light: direction L to the light, col its radiance-scaled colour
vec3 EvalLight (Surf s, vec3 L, vec3 col)
{
	vec3 c = vec3(0.0);
	float NdotL = dot(s.N, L);
	vec3 H = normalize(L + s.V);
	float VdotH = clamp(dot(s.V, H), 0.0, 1.0);
	if (NdotL > 0.0) {
		float NdotH = clamp(dot(s.N, H), 0.0, 1.0);
		vec3 Fd, Fm;
		Fresnel(s, VdotH, Fd, Fm);
#ifdef PBR_ANISOTROPY
		float D = DGgxAniso(NdotH, dot(s.At, H), dot(s.Ab, H), s.at, s.ab);
		float Vis = VGgxAniso(NdotL, s.NdotV, dot(s.At, s.V), dot(s.Ab, s.V), dot(s.At, L), dot(s.Ab, L), s.at, s.ab);
#else
		float D = DGgx(NdotH, s.a);
		float Vis = VGgx(NdotL, s.NdotV, s.a);
#endif
		vec3 layer = mix(Fd, Fm, s.metallic) * (D * Vis);
		layer += (1.0 - s.metallic) * (1.0 - Max3(Fd)) * (1.0 - s.trans) * s.base / PBR_PI;
#ifdef PBR_SHEEN
		float El = textureLod(LutS, vec2(NdotL, s.sheenR), 0.0).b;
		float scale = min(1.0 - Max3(s.sheen) * s.sheenEv, 1.0 - Max3(s.sheen) * El);
		layer = layer * scale + s.sheen * (DCharlie(NdotH, s.sheenR) * VCharlie(NdotL, s.NdotV, s.sheenR));
#endif
		c = layer * NdotL;
	}
#ifdef PBR_TRANSMISSION
	if (NdotL < 0.0 && s.trans > 0.0) {
		vec3 Lm = L - 2.0 * s.N * NdotL;
		vec3 Ht = normalize(Lm + s.V);
		float NdotLm = clamp(dot(s.N, Lm), 0.0, 1.0);
		float NdotHt = clamp(dot(s.N, Ht), 0.0, 1.0);
		vec3 Ft = s.specW * FSchlick(s.f0d, vec3(1.0), clamp(dot(s.V, Ht), 0.0, 1.0));
		vec3 bt = (vec3(1.0) - Ft) * s.base * (DGgx(NdotHt, s.a) * VGgx(NdotLm, s.NdotV, s.a) * NdotLm);
		c += bt * s.trans * (1.0 - s.metallic) * VolumeAtt(s);
	}
#endif
#ifdef PBR_CLEARCOAT
	float Fc = FSchlick1(0.04, 1.0, s.NcV);
	c *= 1.0 - s.cc * Fc;
	float NcL = dot(s.Nc, L);
	if (NcL > 0.0) {
		float NcH = clamp(dot(s.Nc, H), 0.0, 1.0);
		c += vec3(s.cc * Fc * DGgx(NcH, s.cca) * VGgx(NcL, s.NcV, s.cca) * NcL);
	}
#endif
	return PBR_PI * c * col;
}

// image-based light
vec3 EvalIBL (Surf s, float ao)
{
	vec2 lut = texture(LutS, vec2(s.NdotV, s.perceptual)).rg;
	vec3 Fd = s.specW * (s.f0d * lut.x + lut.y);
	vec3 Fm = s.base * lut.x + lut.y;
#ifdef PBR_IRIDESCENCE
	Fd = mix(Fd, s.specW * s.irD * (lut.x + lut.y), s.irid);
	Fm = mix(Fm, s.irM * (lut.x + lut.y), s.irid);
#endif
#ifdef PBR_ANISOTROPY
	vec3 R = reflect(-s.V, s.Nbent);
#else
	vec3 R = reflect(-s.V, s.N);
#endif
#if defined(_SSR)
	vec3 Lenv = Prefiltered(R, s.perceptual);
	bool ccRay = false; // not upstream: one traced ray a pixel, the coat's when it is the smoother lobe on top (design-ssr 6)
#ifdef PBR_CLEARCOAT
	ccRay = s.cc > 0.5 && s.ccPerc < s.perceptual;
#endif
	if (!ccRay && Max3(mix(Fd, Fm, s.metallic)) * ao >= 0.01) {
#ifdef PBR_ANISOTROPY
		vec3 Nr = s.Nbent;
#else
		vec3 Nr = s.N;
#endif
		SsrHit hS = SsrTrace(s.posW, s.Nf, SsrKeepAbove(SsrDir(Nr, s.V, s.a, gl_FragCoord.xy), s.Nf, 0.01), s.a);
		if (hS.conf > 0.0) Lenv = mix(Lenv, Decode(SsrMeshDecode(SsrFetch(hS), SSR_CURVE)), hS.conf);
	}
	vec3 spec = Lenv * mix(Fd, Fm, s.metallic);
#else
	vec3 spec = Prefiltered(R, s.perceptual) * mix(Fd, Fm, s.metallic);
#endif
	vec3 diffuse = Irradiance(s.N) * s.base;
#ifdef PBR_TRANSMISSION
	diffuse = mix(diffuse, Transmitted(s), s.trans);
#endif
	vec3 c = ((1.0 - s.metallic) * (1.0 - Max3(Fd)) * diffuse + spec) * ao;
#ifdef PBR_SHEEN
	c = c * (1.0 - Max3(s.sheen) * s.sheenEv) + s.sheen * SheenEnv(s.N, s.sheenR) * (s.sheenEv * ao);
#endif
#ifdef PBR_CLEARCOAT
	vec2 lc = texture(LutS, vec2(s.NcV, s.ccPerc)).rg;
	float Fcv = FSchlick1(0.04, 1.0, s.NcV);
#if defined(_SSR)
	vec3 Lc = Prefiltered(reflect(-s.V, s.Nc), s.ccPerc);
	if (ccRay) { // not upstream: the coat's traced ray (design-ssr 6)
		SsrHit hC = SsrTrace(s.posW, s.Nf, SsrKeepAbove(SsrDir(s.Nc, s.V, s.cca, gl_FragCoord.xy), s.Nf, 0.01), s.cca);
		if (hC.conf > 0.0) Lc = mix(Lc, Decode(SsrMeshDecode(SsrFetch(hC), SSR_CURVE)), hC.conf);
	}
	c = c * (1.0 - s.cc * Fcv) + s.cc * Lc * ((0.04 * lc.x + lc.y) * ao);
#else
	c = c * (1.0 - s.cc * Fcv) + s.cc * Prefiltered(reflect(-s.V, s.Nc), s.ccPerc) * ((0.04 * lc.x + lc.y) * ao);
#endif
#endif
	return c;
}

vec4 PbrMeshPS (PbrVS f)
{
	Surf s;
	vec2 uv = f.tex0;

	// all texture reads and derivatives come before the alpha test's discard (end of the shader)
	vec4 base = gPbr.baseColor;
	if (HasMap(T_BASE)) {
		vec4 t = texture(BaseS, uv);
		if (Flag(PBRM_BASEGAMMA)) t.rgb = SrgbToLinear(t.rgb);
		base *= t;
	}
	if (Flag(PBRM_NOCOLOR)) base.rgb = gPbr.baseColor.rgb; // as legacy: the texture's colour is left out, the material's kept
	bool kill = (gPbr.alphaMode == 1) && (base.a < gPbr.alphaCutoff);
	s.base = clamp(base.rgb, 0.0, 1.0);

	s.posW = -f.camW;
	s.V = normalize(f.camW);
	vec3 Ng = normalize(f.nrmW);
	vec3 T = f.tanW.xyz - Ng * dot(Ng, f.tanW.xyz);
	vec3 B;
	if (dot(T, T) < 1e-10) Basis(Ng, T, B);
	T = normalize(T);
	B = cross(T, Ng) * (f.tanW.w < 0.0 ? -1.0 : 1.0);
	if (Flag(PBRM_DOUBLESIDED) && !gl_FrontFacing) { Ng = -Ng; T = -T; B = -B; }

	vec3 N = Ng;
	if (HasMap(T_NORMAL)) {
		vec3 n = texture(NormalS, uv).rgb * 2.0 - 1.0;
		n.xy *= gPbr.normalScale;
		if (Flag(PBRM_GREENUP)) n.y = -n.y;
		N = normalize(T * n.x + B * n.y + Ng * n.z);
	}
	s.N = N; s.Ng = Ng; s.T = T; s.B = B;
#if defined(_SSR)
	s.Nf = SsrFaceN(s.posW, Ng); // not upstream: from derivatives here, before the discard (r3-ssr2)
#endif
	s.NdotV = clamp(dot(N, s.V), 1e-4, 1.0);

	float metallic = gPbr.metallic, perc = gPbr.roughness;
	if (HasMap(T_METALROUGH)) {
		vec4 mr = texture(MetalRoughS, uv);
		perc *= mr.g;
		metallic *= mr.b;
	}
	s.metallic = clamp(metallic, 0.0, 1.0);
	s.tRough = clamp(perc, 0.0, 1.0);
	perc = clamp(perc, 0.045, 1.0);
	vec3 dnx = dFdx(N), dny = dFdy(N);
	float variance = 0.25 * (dot(dnx, dnx) + dot(dny, dny));
	float a2 = min(perc * perc * perc * perc + min(2.0 * variance, 0.18), 1.0);
	s.a = sqrt(a2);
	s.perceptual = sqrt(s.a);

	float ao = 1.0;
	if (HasMap(T_OCCLUSION)) ao = 1.0 + gPbr.occlusion * (texture(OcclusionS, uv).r - 1.0);

	vec3 emis = gPbr.emissive * gPbr.emissiveStrength;
	if (HasMap(T_EMISSIVE)) {
		vec3 t = texture(EmissiveS, uv).rgb;
		emis *= Flag(PBRM_EMISGAMMA) ? SrgbToLinear(t) : t;
	}

	s.specW = gPbr.specular;
	if (HasMap(T_SPECULAR)) s.specW *= texture(SpecularS, uv).a;
	vec3 specC = gPbr.specularColor;
	if (HasMap(T_SPECULARCOLOR)) {
		vec3 t = texture(SpecularColorS, uv).rgb;
		specC *= Flag(PBRM_SPECCOLGAMMA) ? SrgbToLinear(t) : t;
	}
	s.f0d = min(vec3(Sq1((gPbr.ior - 1.0) / (gPbr.ior + 1.0))) * specC, vec3(1.0));

	s.trans = 0.0; s.thick = 0.0;
	s.irD = vec3(0.0); s.irM = vec3(0.0); s.irid = 0.0;
	s.At = T; s.Ab = B; s.Nbent = N; s.at = s.a; s.ab = s.a;
	s.cc = 0.0; s.ccPerc = 1.0; s.cca = 1.0; s.Nc = Ng; s.NcV = s.NdotV;
	s.sheen = vec3(0.0); s.sheenR = 1.0; s.sheenEv = 0.0;

#ifdef PBR_TRANSMISSION
	s.trans = gPbr.transmission;
	if (HasMap(T_TRANSMISSION)) s.trans *= texture(TransmissionS, uv).r;
	s.thick = gPbr.thickness;
	if (HasMap(T_THICKNESS)) s.thick *= texture(ThicknessS, uv).g;
#endif

#ifdef PBR_IRIDESCENCE
	s.irid = gPbr.iridescence;
	if (HasMap(T_IRIDESCENCE)) s.irid *= texture(IridescenceS, uv).r;
	float film = gPbr.iridThickMax;
	if (HasMap(T_IRIDTHICK)) film = mix(gPbr.iridThickMin, gPbr.iridThickMax, texture(IridThickS, uv).g);
	s.irD = ThinFilmFresnel(gPbr.iridIor, s.NdotV, film, s.f0d);
	s.irM = ThinFilmFresnel(gPbr.iridIor, s.NdotV, film, s.base);
#endif

#ifdef PBR_ANISOTROPY
	vec2 dir = vec2(1.0, 0.0);
	float str = gPbr.anisoStrength;
	if (HasMap(T_ANISOTROPY)) {
		vec3 an = texture(AnisoS, uv).rgb;
		dir = an.rg * 2.0 - 1.0;
		str *= an.b;
	}
	float cr = cos(gPbr.anisoRotation), sr = sin(gPbr.anisoRotation);
	dir = vec2(cr * dir.x - sr * dir.y, sr * dir.x + cr * dir.y);
	if (Flag(PBRM_GREENUP)) dir.y = -dir.y;
	vec3 At = T * dir.x + B * dir.y;
	At -= N * dot(N, At);
	s.At = dot(At, At) > 1e-10 ? normalize(At) : T;
	s.Ab = cross(N, s.At);
	s.at = mix(s.a, 1.0, str * str);
	s.ab = s.a;
	vec3 bn = cross(cross(s.Ab, s.V), s.Ab);
	float bw = 1.0 - str * (1.0 - s.perceptual);
	bw = bw * bw * bw * bw;
	s.Nbent = dot(bn, bn) > 1e-10 ? normalize(mix(normalize(bn), N, bw)) : N;
#endif

#ifdef PBR_CLEARCOAT
	s.cc = gPbr.clearcoat;
	if (HasMap(T_CLEARCOAT)) s.cc *= texture(ClearcoatS, uv).r;
	float ccr = gPbr.clearcoatRough;
	if (HasMap(T_CLEARCOATROUGH)) ccr *= texture(ClearcoatRoughS, uv).g;
	s.ccPerc = clamp(ccr, 0.045, 1.0);
	s.cca = s.ccPerc * s.ccPerc;
	if (HasMap(T_CLEARCOATNORMAL)) {
		vec3 n = texture(ClearcoatNormalS, uv).rgb * 2.0 - 1.0;
		n.xy *= gPbr.clearcoatNormalScale;
		if (Flag(PBRM_GREENUP)) n.y = -n.y;
		s.Nc = normalize(T * n.x + B * n.y + Ng * n.z);
	}
	s.NcV = clamp(dot(s.Nc, s.V), 1e-4, 1.0);
#endif

#ifdef PBR_SHEEN
	s.sheen = gPbr.sheenColor;
	if (HasMap(T_SHEENCOLOR)) {
		vec3 t = texture(SheenColorS, uv).rgb;
		s.sheen *= Flag(PBRM_SHEENGAMMA) ? SrgbToLinear(t) : t;
	}
	s.sheenR = gPbr.sheenRough;
	if (HasMap(T_SHEENROUGH)) s.sheenR *= texture(SheenRoughS, uv).a;
	s.sheenR = clamp(s.sheenR, 0.07, 1.0);
	s.sheenEv = texture(LutS, vec2(s.NdotV, s.sheenR)).b;
#endif

	vec3 color;
	if (Flag(PBRM_FULLYLIT)) color = s.base + emis;
	else {
		vec3 L = -gSun.Dir;
		vec3 sun = Decode(gSun.Color);
#if defined(_CSM)
		{ // not upstream: the legacy map and the scene cascades, the darker of the two (design-scene-shadows 7.2)
			float fS = 1.0;
#if SHDMAP > 0
			fS = smoothstep(0.0, 0.72, ComputeShadow(f.shdH, clamp(dot(Ng, L), 0.0, 1.0), vec4(gl_FragCoord.xy - 0.5, 0, 0)));
#endif
			sun *= min(fS, CsmVis(CsmS, -f.camW, Ng, gShadowsEnabled ? 3.0 : 1.0, 0.0, 0.0));
		}
#elif SHDMAP > 0
		sun *= smoothstep(0.0, 0.72, ComputeShadow(f.shdH, clamp(dot(Ng, L), 0.0, 1.0), vec4(gl_FragCoord.xy - 0.5, 0, 0)));
#endif
		color = EvalLight(s, L, sun);
#if LMODE != 0
		if (gLightsEnabled) {
			for (int i = 0; i < MAX_LIGHTS; i++) {
				vec3 p = s.posW - gLights[i].position;
				float d2 = dot(p, p);
				if (d2 <= 0.0) continue;
				float d = sqrt(d2);
				p /= d;
				float att = 1.0 / dot(gLights[i].attenuation, vec3(1.0, d, d2));
				float spt = (gLights[i].type == 0) ? 1.0 : clamp((dot(p, gLights[i].direction) - gLights[i].param[Phi]) * gLights[i].param[Theta], 0.0, 1.0);
				vec3 lc = Decode(gLights[i].diffuse.rgb) * (att * spt);
				if (Max3(lc) > 0.0) color += EvalLight(s, -p, lc);
			}
		}
#endif
#if defined(_HEAT)
		if (gHeat.vFlow.w > 0.5) emis += HeatGlow(Ng, f.camW, s.base) * (1.0 - s.trans); // not upstream: the hull's own glow, linear, under the coat as the emission; not on fully lit groups (design-reentry-heat)
#endif
#ifdef PBR_CLEARCOAT
		emis *= 1.0 - s.cc * FSchlick1(0.04, 1.0, s.NcV); // emission is under the coat
#endif
		color += EvalIBL(s, ao) + emis;
	}

	vec3 g = LinearToSrgb(color) * gExposure; // auto-exposure on the display value, as the legacy shaders; inscatter holds e already
#if !defined(_LIGHTGLOW)
	vec3 h2 = g * g;
	g *= pow(max(vec3(0.0), 1.0 + h2 * h2), vec3(-0.25));
#endif
	g = g * gSun.Transmission + gSun.Inscatter;
	vec4 o = vec4(g, gPbr.alphaMode == 2 ? base.a : 1.0);
#if defined(_DEBUG)
	o = o * (1.0 - gColor * 0.5) + gColor;
#endif
	if (kill) discard;
	return o;
}

#endif

#ifdef PS_PbrMeshPS
layout(location = 0) in PbrVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = PbrMeshPS(frg); }
#endif


// ============================================================================
// P0: opaque, alpha mask and transmission groups; P1: alpha blend groups
// ============================================================================

technique PbrTech
{
	pass P0
	{
		vertexShader = compile vs_3_0 PbrMeshVS();
		pixelShader = compile ps_3_0 PbrMeshPS();

		AlphaBlendEnable = false;
		ZEnable = true;
		ZWriteEnable = true;
	}

	pass P1
	{
		vertexShader = compile vs_3_0 PbrMeshVS();
		pixelShader = compile ps_3_0 PbrMeshPS();

		AlphaBlendEnable = true;
		BlendOp = Add;
		SrcBlend = SrcAlpha;
		DestBlend = InvSrcAlpha;
		ZEnable = true;
		ZWriteEnable = false;
	}
}
