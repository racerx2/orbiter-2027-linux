
// ============================================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// licensed under LGPL v2
// Copyright (C) 2022-2026 Jarmo Nikkanen
// ============================================================================

#include "Scatter.glsl"
#include "SceneShadow.glsl" // not upstream: CsmPrm (_CSM)

struct _Light
{
	vec3   position[4];         /* position in world space */
	vec3   direction[4];        /* direction in world space */
	vec3   diffuse[4];          /* diffuse color of light */
	vec3   attenuation[4];      /* Attenuation */
	vec4   param[4];            /* range, falloff, theta, phi */
};

#define Range   0
#define Falloff 1
#define Theta   2
#define Phi     3

#define ATMNOISE 0.25
#define GLARE_SIZE 5	// Larger value -> smaller

// ----------------------------------------------------------------------------
// Vertex input layouts from Vertex buffers to vertex shader
// ----------------------------------------------------------------------------

struct TILEVERTEX					// (VERTEX_2TEX) Vertex declaration used for surface tiles and cloud layer
{
	vec3 posL;     // POSITION0
	vec3 normalL;  // NORMAL0
	vec2 tex0;     // TEXCOORD0
	float  elev;   // TEXCOORD1
};

// ----------------------------------------------------------------------------
// Vertex Shader to Pixel Shader datafeeds
// ----------------------------------------------------------------------------

struct TileVS
{
	vec4 posH;     // POSITION0
	vec2 texUV;    // TEXCOORD0  // Texture coordinate
	vec4 camW;     // TEXCOORD1  // Radius in .w
	vec3 nrmW;     // TEXCOORD2
#if defined(_SHDMAP)
	vec4 shdH;     // TEXCOORD3
#endif
};

struct CldVS
{
	vec4 posH;     // POSITION0
	vec2 texUV;    // TEXCOORD0  // Texture coordinate
	vec3 nrmW;     // TEXCOORD1
	vec3 posW;     // TEXCOORD2
};

struct HazeVS
{
	vec4 posH;     // POSITION0
	vec2 texUV;    // TEXCOORD0
	vec3 posW;     // TEXCOORD1
	float  alpha;  // COLOR0
};


// Note: "bool" is 32-bits in a shaders (max count 16) 
//
struct FlowControlPS
{
	BOOL bInSpace;				// Camera in space (not in atmosphere)
	BOOL bBelowClouds;			// Camera is below cloud layer
	BOOL bOverlay;				// Overlay on/off	
	BOOL bShadows;				// Shadow Map on/off
	BOOL bLocals;				// Local Lights on/off
	BOOL bMicroNormals;			// Micro texture has normals
	BOOL bCloudShd;				// Cloud shadow textures valid and enabled
	BOOL bMask;					// Nightlights/water mask texture is enabled
	BOOL bRipples;				// Water riples texture is enabled
	BOOL bMicroTex;				// Micro textures exists and enabled
	BOOL bPlanetShadow;			// Use spherical approximation for shadow
	BOOL bEclipse;				// Eclipse is occuring
	BOOL bTexture;				// Surface texture exists
	BOOL bClass;				// not upstream: land class weights (tClass) for micro detail, repeat in vMSc[0]
	BOOL bHorizon;				// not upstream: horizon map (tHorizon) for terrain shadows
	BOOL bWay;					// not upstream (design-ways): way texture (tWay)
	BOOL bWayN;					// not upstream: the lamps come from tWayN, not the night look's street lattice
};

struct FlowControlVS
{
	BOOL bInSpace;				// Camera in space (not in atmosphere)
	BOOL bSpherical;			// Ignore elevation, render as sphere
	BOOL bElevOvrl;				// ElevOverlay on/off			
};

struct PerObjectParams
{
	mat4 mWorld;			// World Matrix
	mat4 mLVP;				// Light-View-Projection
	vec4   vSHD;				// Shadow Map Parameters
	vec4   vMSc[3];			// Micro Texture offset-scale
	vec4	 vTexOff;			// Texture offset-scale
	vec4   vCloudOff;			// Cloud texture offset-scale
	vec4   vMicroOff;			// Micro texture offset-scale
	vec4   vOverlayOff;       // Overlay texture offset-scale
	vec4   vOverlayCtrl[4];
	vec3	 vEclipse;			// Eclipse caster position (geocentric)
	float	 fEclipse;			// Eclipse data addressing scale factor. (to access tExlipse)
	float	 fAlpha;
	float	 fBeta;
	float	 fTgtScale;
	float	 fPad0;				// not upstream: matches the client struct's padding
	vec4	 vHorOff;			// not upstream: horizon map uv offset (xy) and scale (zw), left half
	vec4	 vHorW[2];			// not upstream: sun weights of azimuths 0-3 and 4-7
	vec4	 vVolCld;			// not upstream: 2D cloud fade start, end (m), volume weight
	vec4	 vCldAtlas;			// not upstream: cloud atlas uv offset (xy) and scale (zw)
	vec4	 vCldAtlasW;		// not upstream: x 1 with the atlas, y max anisotropy, z 1 when the texture is at or below the atlas level
	vec4	 vWaterFx;			// not upstream: water effects, x foam mask mip, yz foam ring radius in uv, w spare
	vec4	 vWayOff;			// not upstream (design-ways): way and lamp texture uv offset (xy) and scale (zw)
};

#if defined(_SSR)
#include "SsrPrm.glsl" // not upstream: screen-space reflections (design-ssr)
#endif

// uniform extern: vertex shader constants at binding 0 (the pixel shaders read Prm too), pixel shader constants at 2 (Scatter.glsl's are 1)
layout(binding = 0, row_major, scalar) uniform PlanetVSBlock { PerObjectParams Prm; FlowControlVS FlowVS; };
layout(binding = 2, row_major, scalar) uniform PlanetPSBlock {
	FlowControlPS Flow;
	_Light Lights;			// Note: DX9 doesn't tolerate structure arrays outside FX framework
	BOOL Spotlight[4];
#if defined(_CSM)
	CsmPrm gCsm;			// not upstream: scene shadow cascades (design-scene-shadows Rev 2)
#endif
#if defined(_SSR)
	SsrPrm gSsr;			// not upstream: screen-space reflections of the water (design-ssr), after the shadows' gCsm
#endif
};


layout(binding = 13) uniform sampler2D tDiff;				// Diffuse texture
layout(binding = 14) uniform sampler2D tMask;				// Nightlights / Specular mask texture
layout(binding = 15) uniform sampler2D tCloud;				// 1st Cloud shadow texture
layout(binding = 16) uniform sampler2D tCloud2;			// 2nd Cloud shadow texture
layout(binding = 17) uniform sampler2D tCloudMicro;		
layout(binding = 18) uniform sampler2D tCloudMicroNorm;
layout(binding = 19) uniform sampler2D tNoise;				//
layout(binding = 20) uniform sampler2D	tOcean;				// Ocean Normal Map Texture
layout(binding = 21) uniform sampler2D	tMicroA;
layout(binding = 22) uniform sampler2D	tMicroB;
layout(binding = 23) uniform sampler2D	tMicroC;
layout(binding = 24) uniform sampler2D tGlare;
layout(binding = 25) uniform sampler2D	tShadowMap;
layout(binding = 26) uniform sampler2D	tOverlay;
layout(binding = 27) uniform sampler2D	tMskOverlay;
layout(binding = 28) uniform sampler2D	tElvOverlay;
layout(binding = 29) uniform sampler2D	tEclipse;
layout(binding = 30) uniform sampler2D	tClass;				// not upstream: R trees, G grass / crops, B bare / built-up
layout(binding = 31) uniform sampler2D	tHorizon;			// not upstream: sin of the horizon, 8 azimuths in 2 halves; the last free binding
#if defined(_WAYS) // not upstream (design-ways): on tCloudMicro/tCloudMicroNorm's bindings, which only CloudPS reads
layout(binding = 17) uniform sampler2D	tWay;				// R paved share, G luminance x R, B yellow paint x R, A water share
layout(binding = 18) uniform sampler2D	tWayN;				// sqrt (value / 4) of lamp pool, head, spill; A the imagery's share in the pavement colour
#endif
#include "SceneShadow.glsl" // not upstream: CsmVis (_CSM, pixel stage); tShadowMap carries the atlas

#if defined(_SSR) // not upstream: screen-space reflections of the water (design-ssr); on tMicroA/tMicroB's bindings, which the Earth shader never reads (no _MICROTEX), so _DEVTOOLS keeps 26-28 (r3-ssr2)
#if defined(_MICROTEX)
#error "_SSR takes the micro texture bindings 21 and 22"
#endif
layout(binding = 21) uniform sampler2D	tSsrCol;			// not upstream: last frame's scene, mip chain
layout(binding = 22) uniform sampler2D	tSsrHiZ;			// not upstream: its 1/z pyramid
#define SSR_PLANET
#include "Ssr.glsl"

// not upstream: Scatter.glsl HDR() inverted at this frame's exposure: a captured display value back to the terrain's radiance (clamped, design-ssr Rev 2 S2)
vec3 SsrPlanetDecode(vec3 y)
{
	y = clamp(y, 0.0, 0.985);
#if defined(_FILMIC)
	vec3 a = 2.43 * y - 2.51, b = 0.59 * y - 0.03, c = 0.14 * y;
	vec3 t = (-b - sqrt(max(b * b - 4.0 * a * c, vec3(0.0)))) / (2.0 * a);
	return t / (0.568 * Const.Expo) * gSsr.vDecode.x;
#else
	return -log(1.0 - y) / Const.Expo * gSsr.vDecode.x;
#endif
}
#endif

#if defined(_CLOUDATLAS)
layout(binding = 24) uniform sampler2D	tCloudAtlas;		// not upstream: R8 cloud alpha; shares 24 with tGlare, which only HorizonPS reads

// not upstream: anisotropic filtering's LOD of uv on a texture of s texels
float AnisoLod(vec2 uv, vec2 s)
{
#ifdef STAGE_PS
	float a = length(dFdx(uv) * s), b = length(dFdy(uv) * s);
	float pmax = max(max(a, b), 1e-12), pmin = max(min(a, b), 1e-12);
	return log2(pmax / min(pmax / pmin, Prm.vCldAtlasW.y));
#else
	return -16.0;
#endif
}

// not upstream: atlas weight, 0 where the atlas is magnified 2x, 1 at 1.19x or when the tile is no finer than the atlas
float AtlasW(vec2 uv)
{
	return max(smoothstep(-1.0, -0.25, AnisoLod(uv, Prm.vCldAtlas.zw * vec2(textureSize(tCloudAtlas, 0)))), Prm.vCldAtlasW.z);
}

// not upstream: 1 at a 512 px tile texture's edge, 0 from 3 texels of its mip lt in; 0 where the tile is magnified
float AtlasEdge(vec2 uvT, float lt)
{
	vec2 e = min(uvT, 1.0 - uvT) * 512.0;
	float b = exp2(max(lt, 0.0));
	return (1.0 - smoothstep(0.5 * b, 3.0 * b, min(e.x, e.y))) * smoothstep(0.25, 1.0, lt);
}
#endif



// ---------------------------------------------------------------------------------------------------
//
float SampleShadows(vec2 sp, float pd)
{
	if (sp.x < 0 || sp.y < 0) return 0.0f;	// If a sample is outside border -> fully lit
	if (sp.x > 1 || sp.y > 1) return 0.0f;

	if (pd < 0) pd = 0;
	if (pd > 2) pd = 2;

	vec2 dx = vec2(Prm.vSHD[1], 0) * 1.5f;
	vec2 dy = vec2(0, Prm.vSHD[1]) * 1.5f;
	float  va = 0;

	sp -= dy;
	if ((texture(tShadowMap, sp - dx).r) > pd) va++;
	if ((texture(tShadowMap, sp).r) > pd) va++;
	if ((texture(tShadowMap, sp + dx).r) > pd) va++;
	sp += dy;
	if ((texture(tShadowMap, sp - dx).r) > pd) va++;
	if ((texture(tShadowMap, sp).r) > pd) va++;
	if ((texture(tShadowMap, sp + dx).r) > pd) va++;
	sp += dy;
	if ((texture(tShadowMap, sp - dx).r) > pd) va++;
	if ((texture(tShadowMap, sp).r) > pd) va++;
	if ((texture(tShadowMap, sp + dx).r) > pd) va++;

	return va * 0.1111111f;
}

// -------------------------------------------------------------------------------------------------------------
// Local light sources
//
void LocalLights(
	out vec3 diff_out,
	in vec3 nrmW,
	in vec3 posW)
{
	diff_out = vec3(0);

	if (!Flow.bLocals) return;
	int i;

	// Relative positions
	vec3 p[4];
	for (i = 0; i < 4; i++) p[i] = posW - Lights.position[i];

	// Square distances
	vec4 sd;
	for (i = 0; i < 4; i++) sd[i] = dot(p[i], p[i]);

	// Normalize
	sd = inversesqrt(sd);
	for (i = 0; i < 4; i++) p[i] *= sd[i];

	// Distances
	vec4 dst = 1.0 / sd;

	// Attennuation factors
	vec4 att;
	for (i = 0; i < 4; i++) att[i] = dot(Lights.attenuation[i].xyz, vec3(1.0, dst[i], dst[i] * dst[i]));

	att = 1.0 / att;

	// Spotlight factors
	vec4 spt = vec4(1);
	
	for (i = 0; i < 4; i++) {
		spt[i] = (dot(p[i], Lights.direction[i]) - Lights.param[i][Phi]) * Lights.param[i][Theta];
		if (!Spotlight[i]) spt[i] = 1.0f;
	}

	spt = clamp(spt, 0.0, 1.0);

	// Diffuse light factors
	vec4 dif;
	for (i = 0; i < 4; i++) dif[i] = dot(-p[i], nrmW);

	dif = clamp(dif, 0.0, 1.0);
	dif *= (att * spt);

	for (i = 0; i < 4; i++) diff_out += Lights.diffuse[i].rgb * dif[i];
}


// Render Eclipse ------------------------------------------------------------
//
float GetEclipse(vec3 vVrt)
{
	if (Flow.bEclipse)
	{
		vec3 b = vVrt - Const.toSun * dot(vVrt, Const.toSun); // Flatten
		float  x = length(Prm.vEclipse - b) * Prm.fEclipse;
		return texture(tEclipse, vec2(clamp(x, 0.0, 1.0), 0.5)).r;	// tex1D: the 512x1 table is a 2D texture here
	}
	return 1.0;
}
	


// ============================================================================
// Render SkyDome and Horizon
// ============================================================================

HazeVS HorizonVS(vec3 posL)	// posL : POSITION0
{
	// Zero output.
	HazeVS outVS = HazeVS(vec4(0), vec2(0), vec3(0), 0.0);

	outVS.texUV = posL.xy*10.0;

	posL.xz *= mix(Prm.vTexOff[0], Prm.vTexOff[1], posL.y);
	posL.y   = mix(Prm.vTexOff[2], Prm.vTexOff[3], posL.y);

	outVS.posW = (vec4(posL, 1.0f) * Prm.mWorld).xyz;
	outVS.posH = vec4(outVS.posW, 1.0f) * Const.mVP;

	return outVS;
}

#ifdef VS_HorizonVS
layout(location = 0) in vec3 iPosL;
layout(location = 0) out HazeVS oVS;
void main() { oVS = HorizonVS(iPosL); gl_Position = oVS.posH; }
#endif


// SkyDome Shader, Renders the sky from with-in atmosphere
//
vec4 HorizonPS(HazeVS frg)
{
	float fNoise = (textureLod(tNoise, frg.texUV, 0.0).r - 0.5f) * 0.03;

	vec3 uDir = normalize(frg.posW);

	SkyOut sky = GetSkyColor(uDir);
	
	float ph = dot(uDir, Const.toSun);

	vec2  guv = vec2(dot(uDir, Const.ZeroAz), dot(uDir, Const.Up)) * GLARE_SIZE + 0.5f;
	float  cGlr = texture(tGlare, guv).r * clamp(ph, 0.0, 1.0) * Const.SunVis;
	
	vec3 color = HDR(sky.ray.rgb * RayPhase(ph) + (sky.mie.rgb + 0.0008f) * MiePhase(ph) * (0.75f + cGlr * Const.cGlare));

	return vec4(color + fNoise, sky.ray.a);
}

#ifdef PS_HorizonPS
layout(location = 0) in HazeVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = HorizonPS(frg); }
#endif


// Renders the horizon "ring" from space
//
vec4 HorizonRingPS(HazeVS frg)
{
	vec3 uDir = normalize(frg.posW);
	vec3 uOrt = normalize(uDir - Const.toCam * dot(uDir, Const.toCam));
	vec3 vVrt = Const.CamPos + frg.posW;
	float d = dot(uDir, frg.posW);
	float x = dot(uOrt, Const.SunAz) * 0.5 + 0.5;
	float r = length(vVrt);
	float q = (r - Const.PlanetRad) / Const.AtmoAlt;

	vec2 uv = vec2(x, q > 0 ? sqrt(q) : 0.0);

	vec4 cRay = texture(tSkyRayColor, uv).rgba;
	vec3 cMie = texture(tSkyMieColor, uv).rgb;

	float ph = dot(uDir, Const.toSun);

	vec3 color = HDR(cRay.rgb * RayPhase(ph) + cMie * MiePhase(ph));

	color *= GetEclipse(vVrt);

	return vec4(color, cRay.a);
}

#ifdef PS_HorizonRingPS
layout(location = 0) in HazeVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = HorizonRingPS(frg); }
#endif




// ============================================================================
// Planet Surface Renderer
// ============================================================================

#define AUX_DIST		0	// Vertex distance
#define AUX_NIGHT		1	// Night lights intensity
#define AUX_SLOPE		2   // Terrain slope factor 0.0=flat, 1.0=sloped
#define AUX_RAYDEPTH	3   // Optical depth of a ray

TileVS TerrainVS(TILEVERTEX vrt)
{
	// Zero output.
	TileVS outVS = TileVS(vec4(0), vec2(0), vec4(0), vec3(0)
#if defined(_SHDMAP)
		, vec4(0)
#endif
	);
	vec4 vElev = vec4(0);
	vec3 vNrmW;
	
	// Apply a world transformation matrix
	vec3 vPosW = (vec4(vrt.posL, 1.0f) * Prm.mWorld).xyz;
	vec3 vVrt = Const.CamPos + vPosW;
	vec3 vPlN = normalize(vVrt);

	if (FlowVS.bElevOvrl)
	{
		// ----------------------------------------------------------
		// Elevation Overlay
		//
		vec2 vUVOvl = vrt.tex0.xy * Prm.vOverlayOff.zw + Prm.vOverlayOff.xy;

		// Sample Elevation Map
		vElev = textureLod(tElvOverlay, vUVOvl, 0.0);

		// Construct world space normal
		vNrmW = vec3(vElev.xy, sqrt(clamp(1.0f - dot(vElev.xy, vElev.xy), 0.0, 1.0)));
		vNrmW = (vec4(vNrmW, 0.0f) * Prm.mWorld).xyz;

		// Reconstruct Elevation
		vPosW += normalize(Const.CamPos + vPosW) * (vElev.z - vrt.elev) * vElev.w;	
	}
	else {
		vNrmW = (vec4(vrt.normalL, 0.0f) * Prm.mWorld).xyz;
	}

	// Disrecard elevation and make the surface spherical
	if (FlowVS.bSpherical) {
		vPosW = (normalize(Const.CamPos + vPosW) * Const.PlanetRad) - Const.CamPos;
		vNrmW = vPlN;
	}

	outVS.posH = vec4(vPosW, 1.0f) * Const.mVP;

#if defined(_SHDMAP)
	outVS.shdH = vec4(vPosW, 1.0f) * Prm.mLVP;
#endif

	outVS.texUV.xy = vrt.tex0.xy;
	outVS.camW = vec4(-vPosW, dot(vVrt, vPlN));
	outVS.nrmW = vNrmW;
	
	return outVS;
}

#ifdef VS_TerrainVS
layout(location = 0) in vec3 iPosL;
layout(location = 1) in vec3 iNrmL;
layout(location = 5) in vec2 iTex0;
layout(location = 6) in float iElev;
layout(location = 0) out TileVS oVS;
void main() { oVS = TerrainVS(TILEVERTEX(iPosL, iNrmL, iTex0, iElev)); gl_Position = oVS.posH; }
#endif



bool InRange(vec2 a)
{
	return (a.x > 0.0f && a.x < 1.0f) && (a.y > 0.0f && a.y < 1.0f);
}

float GGX_NDF(float dHN, float rgh)
{
	float r2 = rgh * rgh;
	float dHN2 = dHN * dHN;
	float d = (r2 * dHN2) + (1.0f - dHN2);
	return r2 / (3.14f * d * d);
}


// not upstream: periodic value noise for the land class detail; period P keeps it seamless over tiles of any level
float ClsHash(vec2 p) // hash12 of "Hash without Sine", Dave Hoskins, MIT (CREDITS)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float ClsNoise(vec2 p, float P)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	vec2 a = mod(i, P), b = mod(i + 1.0, P);
	return mix(mix(ClsHash(a), ClsHash(vec2(b.x, a.y)), f.x), mix(ClsHash(vec2(a.x, b.y)), ClsHash(b), f.x), f.y);
}

#if defined(_NIGHTLOOK)
// not upstream: night look street lamps (design-night-look 5, Rev 2 M4); profile exp(-r^2/s^2); qw warped 128 m blocks, fq its footprint a pixel; AutoExpo.cpp NightLook::LampField is the tested copy
const vec3 NL_PAL0 = vec3(1.0, 0.54, 0.07), NL_PAL1 = vec3(1.0, 0.71, 0.42), NL_PAL2 = vec3(1.0, 0.82, 0.64); // sodium 2000 K, LED 3000 K, LED 4000 K
const vec3 NL_PALM = NL_PAL0 * 0.5 + NL_PAL1 * 0.3 + NL_PAL2 * 0.2;
const float NL_KAMB = 0.06, NL_KPOOL = 2.0, NL_KSPILL = 0.24, NL_KHEAD = 4.0; // with the planet's light factor 0.5 in fNgt
vec3 NightPal(float t) { return t < 0.5 ? NL_PAL0 : (t < 0.8 ? NL_PAL1 : NL_PAL2); }
vec2 NightWarp(vec2 q) { return q + 0.5 * (vec2(ClsNoise(q * 0.125, 128.0), ClsNoise(q * 0.125 + vec2(37.0, 71.0), 128.0)) - 0.5); } // streets bend by up to ~30 m over ~1 km

// per street family: lamp points, a band along the line once they are under 2 px apart, the area mean once lines are 3 px apart; each feature no thinner than a pixel, energy kept
void NightLamps(vec2 qw, vec2 fq, float pSeg, out vec3 cPool, out vec3 cHead, out vec3 cSpill)
{
	const float Sp = 0.07, Sh = 0.012, Hw = 0.04, E = 0.85, PB = 4.0, PI = 3.14159265, RPI = 1.7724539;
	float n = PB * pSeg * E; // a family's lamps a block^2
	vec3 mPool = NL_PALM * (n * PI * Sp * Sp), mHead = NL_PALM * (n * PI * Sh * Sh), mSpill = NL_PALM * (3.0 * Hw * pSeg);
	cPool = vec3(0.0); cHead = vec3(0.0); cSpill = vec3(0.0);
	for (int k = 0; k < 2; k++) {
		float u = k == 0 ? qw.x : qw.y, v = k == 0 ? qw.y : qw.x, fu = k == 0 ? fq.x : fq.y, fv = k == 0 ? fq.y : fq.x;
		float tA = smoothstep(0.2, 0.35, fu), tL = smoothstep(0.0625, 0.125, fv);
		vec3 p = vec3(0.0), h = vec3(0.0), s = vec3(0.0);
		if (tA < 1.0) {
			float fk = float(k), li = floor(u + 0.5), dp = u - li;
			li = mod(li, 1024.0);
			float hs = ClsHash(vec2(li + 1031.0 * fk + 0.5, mod(floor(v), 1024.0) + 0.5));
			float sxp = min(max(Sp, fu), 0.25), sxh = min(max(Sh, fu), 0.25), hw = min(max(Hw, fu), 0.25); // half a block at most: only the nearest line is evaluated
			if (hs < pSeg) {
				vec3 c = NightPal(hs / pSeg);
				s = c * ((1.0 - smoothstep(hw, 2.0 * hw, abs(dp))) * (Hw / hw));
				if (tL > 0.0) { // the segment's lamps as a band across the line, the same energy a unit length
					p += c * (tL * PB * E * PI * Sp * Sp / (RPI * sxp) * exp(-dp * dp / (sxp * sxp)));
					h += c * (tL * PB * E * PI * Sh * Sh / (RPI * sxh) * exp(-dp * dp / (sxh * sxh)));
				}
			}
			if (tL < 1.0) {
				float syp = max(Sp, fv), syh = max(Sh, fv);
				float a = v * PB, j = floor(a), f = a - j;
				for (int i = 0; i < 2; i++) { // the lamp and the nearer neighbour
					float jj = j + (i == 0 ? 0.0 : (f > 0.5 ? 1.0 : -1.0)), jm = mod(jj, 4096.0);
					float hl = ClsHash(vec2(li + 1031.0 * fk + 0.5, floor(jm * 0.25) + 0.5)); // the lamp's own segment
					if (hl >= pSeg || ClsHash(vec2(jm + 4099.0 * fk + 0.25, li + 0.25)) >= E) continue;
					float dx = dp - (mod(jm, 2.0) < 0.5 ? 0.035 : -0.035), dy = (a - jj - 0.5) / PB;
					vec3 c = NightPal(hl / pSeg) * ((1.0 - tL) * (0.8 + 0.4 * ClsHash(vec2(li + 0.75, jm + 8209.0 * fk + 0.75))));
					p += c * (exp(-dx * dx / (sxp * sxp) - dy * dy / (syp * syp)) * Sp * Sp / (sxp * syp));
					h += c * (exp(-dx * dx / (sxh * sxh) - dy * dy / (syh * syh)) * Sh * Sh / (sxh * syh));
				}
			}
		}
		cPool += mix(p, mPool, tA); cHead += mix(h, mHead, tA); cSpill += mix(s, mSpill, tA);
	}
}
#endif

vec4 TerrainPS(TileVS frg)
{

	vec2 vUVSrf = frg.texUV.xy * Prm.vTexOff.zw + Prm.vTexOff.xy;
	vec2 vUVWtr = frg.texUV.xy * Prm.vMicroOff.zw + Prm.vMicroOff.xy;
	vec2 vUVCld = frg.texUV.xy * Prm.vCloudOff.zw + Prm.vCloudOff.xy;

#if defined(_WATERFX)
	vec2 vUVWt0 = vUVWtr; // not upstream: the unscrolled ocean uv, whole repeats per L14 tile
	float fFoam = 0.0; // not upstream: surf foam share, lit as white ground below
	float fFoamW = 0.0; // not upstream: the foam noise's cells per pixel, so it fades to its mean before it aliases
#ifdef STAGE_PS
	fFoamW = length(fwidth(vUVWt0 * 64.0));
#endif
#endif
	vUVWtr.x += Const.Time / 180.0f;

	vec3 cNrm = vec3(0.5, 0.5, 1.0);
	float fChA = 0.0f, fChB = 0.0f;
#if defined(_CLOUDATLAS)
	float fChAtl = 0.0, fAtlW = 0.0; // not upstream: atlas shadow and its weight
#endif

#if defined(_RIPPLES)
	if (Flow.bTexture) cNrm = texture(tOcean, vUVWtr).xyz;
#if defined(_WATERFX)
	vec3 cNrm2 = vec3(0.5, 0.5, 1.0); // not upstream: second layer on the (2u + v, 2v - u) lattice, 26.6 deg turned and sqrt5 finer, 24 and 18 repeats per 3600 s
	if (Flow.bTexture) cNrm2 = texture(tOcean, vec2(2.0 * vUVWt0.x + vUVWt0.y, 2.0 * vUVWt0.y - vUVWt0.x) + vec2(24.0, 18.0) * (Const.Time / 3600.0)).xyz;
#endif
#endif

	// Fetch Main Textures
	vec4 cTex = vec4(0.5, 0.5, 0.5, 1.0);
	if (Flow.bTexture) cTex = texture(tDiff, vUVSrf);

	vec4 cMsk = vec4(0, 0, 0, 1);
	if (Flow.bMask) cMsk = texture(tMask, vUVSrf);

#if defined(_DEVTOOLS)
	if (Flow.bOverlay) {
		vec2 vUVOvl = frg.texUV.xy * Prm.vOverlayOff.zw + Prm.vOverlayOff.xy;
		if (InRange(vUVOvl)) {
			vec4 cOvl = texture(tOverlay, vUVOvl);
			vec4 cWtr = texture(tMskOverlay, vUVOvl);
			cTex.rgb = mix(cTex.rgb, cOvl.rgb, cOvl.a * Prm.vOverlayCtrl[0].rgb);
			cMsk.rgb = mix(cMsk.rgb, cWtr.rgb, cOvl.a * Prm.vOverlayCtrl[1].rgb);
			cMsk.a = mix(cMsk.a, cWtr.a, Prm.vOverlayCtrl[1].a);
		}
	}
#endif

#if defined(_WAYS)
	// not upstream (design-ways): streamed roads, rail, airports and rivers painted on the ground
	float fWP = 0.0;
	vec2 vUVWay = frg.texUV.xy * Prm.vWayOff.zw + Prm.vWayOff.xy;
	vec4 cWN = vec4(0.0, 0.0, 0.0, 0.25);
	if (Flow.bWayN) cWN = texture(tWayN, vUVWay);
	if (Flow.bWay) {
		vec4 cW = texture(tWay, vUVWay);
		cTex.rgb = mix(cTex.rgb, vec3(0.07, 0.09, 0.08), 0.7 * cW.a); // rivers and canals: water, dark
		cMsk.a *= 1.0 - cW.a;
		fWP = cW.r;
		if (fWP > 0.5 / 255.0) {
			float fL = cW.g / fWP, fY = cW.b / fWP;
			vec3 cAlb = fL * mix(vec3(1.0), vec3(1.25, 1.02, 0.30), clamp(fY, 0.0, 1.0)); // neutral grey and white, yellow paint
			cAlb = mix(cAlb, cTex.rgb, cWN.a);
			cTex.rgb = mix(cTex.rgb, cAlb, fWP);
			cMsk.a = mix(cMsk.a, 1.0, fWP); // no water under pavement
		}
	}
#endif

#if defined(_CLOUDSHD)
	if (Flow.bCloudShd) {
		fChA = texture(tCloud, vUVCld).a;
		fChB = texture(tCloud2, vUVCld - vec2(1, 0)).a;
#if defined(_CLOUDATLAS)
		if (Prm.vCldAtlasW.x > 0.0) {
			vec2 uvT = vec2(vUVCld.x < 1.0 ? vUVCld.x : vUVCld.x - 1.0, vUVCld.y); // in the shadow's own cloud texture
			fAtlW = max(AtlasW(frg.texUV.xy), AtlasEdge(uvT, AnisoLod(vUVCld, vec2(512.0))));
			fChAtl = texture(tCloudAtlas, frg.texUV.xy * Prm.vCldAtlas.zw + Prm.vCldAtlas.xy).r;
		}
#endif
	}
#endif

	float fShadow = 1.0f;

#if defined(_SHDMAP)
	if (Flow.bShadows) {
		frg.shdH.xyz /= frg.shdH.w;
		frg.shdH.z = 1 - frg.shdH.z;
		vec2 sp = frg.shdH.xy * vec2(0.5f, -0.5f) + vec2(0.5f, 0.5f);
		float  pd = frg.shdH.z + 0.05f * Prm.vSHD[3];
		fShadow = 1.0f - SampleShadows(sp, pd);
	}
#endif

	float fH = 1.0f; // not upstream: sun above the terrain horizon
	if (Flow.bHorizon) {
		vec2 uvH = frg.texUV.xy * Prm.vHorOff.zw + Prm.vHorOff.xy;
		float s = dot(texture(tHorizon, uvH), Prm.vHorW[0]) + dot(texture(tHorizon, uvH + vec2(257.0 / 514.0, 0.0)), Prm.vHorW[1]);
		s = s * 1.25 - 0.25;
		fH = smoothstep(s - 0.012, s + 0.012, dot(normalize(Const.CamPos - frg.camW.xyz), Const.toSun));
		fShadow *= fH;
	}
#if defined(_CSM) && defined(STAGE_PS)
	fShadow = min(fShadow, CsmVis(tShadowMap, -frg.camW.xyz, normalize(frg.nrmW), 1.0, 0.0, 1.0)); // not upstream: relief, structures, buildings, trees and vessels near the camera; the horizon map beyond (design-scene-shadows 10)
#endif

	vec3 cFar, cMed, cLow;

#if defined(_MICROTEX)
	vec2 UV = frg.texUV.xy;
	// Create normals
	if (Flow.bMicroTex)
	{
		if (Flow.bMicroNormals) {
			// Normal in .ag luminance in .b
			cFar = texture(tMicroC, UV * Prm.vMSc[2].zw + Prm.vMSc[2].xy).agb;	// High altitude micro texture C
			cMed = texture(tMicroB, UV * Prm.vMSc[1].zw + Prm.vMSc[1].xy).agb;	// Medimum altitude micro texture B
			cLow = texture(tMicroA, UV * Prm.vMSc[0].zw + Prm.vMSc[0].xy).agb;	// Low altitude micro texture A
		}
		else {
			// Color in .rgb no normals
			cFar = texture(tMicroC, UV * Prm.vMSc[2].zw + Prm.vMSc[2].xy).rgb;	// High altitude micro texture C
			cMed = texture(tMicroB, UV * Prm.vMSc[1].zw + Prm.vMSc[1].xy).rgb;	// Medimum altitude micro texture B
			cLow = texture(tMicroA, UV * Prm.vMSc[0].zw + Prm.vMSc[0].xy).rgb;	// Low altitude micro texture A
		}
	}
#endif

	vec3 cRfl = vec3(0);
	vec3 nvrW = normalize(frg.nrmW);			// Per-pixel surface normal vector
	vec3 vRay = normalize(frg.camW.xyz);		// Unit viewing ray
	vec3 vVrt = Const.CamPos - frg.camW.xyz;	// Geo-centric pixel position
	vec3 vPlN = normalize(vVrt);				// Planet mean normal
	vec3 hlvW = normalize(vRay + Const.toSun);
	float   dst = dot(vRay, frg.camW.xyz);		// Pixel to camera distance
	float   rad = frg.camW.w;					// Pixel geo-distance
	float   alt = rad - Const.PlanetRad;		// Pixel altitude over mean radius
	float  fSrf = (1.0 - Const.CamSpace);		// Camera colse to surface ?
	float fMask = (1.0 - cMsk.a);				// Specular Mask
	float  fSpe = 0;
	float fAmpf = 1.0f;
	float fDRS = dot(vRay, Const.toSun);
	float fDPS = dot(vPlN, Const.toSun);		// Mean normal dot sun
#if defined(_SSR)
	SsrHit ssrH = SsrNone(); // not upstream: the water's traced reflection and its Fresnel weight (design-ssr)
	float fSsrF = 0.0;
#endif


#if defined(_WATER)
#if defined(_RIPPLES)

	// Compute world space normal for water rendering
	//
	cNrm.xy = (cNrm.xy - 0.5f) * 2.0f;
#if defined(_WATERFX)
	vec3 cN1 = normalize(cNrm), cN2 = normalize(vec3((cNrm2.xy - 0.5) * 2.0, cNrm2.z)); // not upstream: whiteout blend of the two layers as unit normals
	vec3 cWo = vec3(cN1.xy + cN2.xy, cN1.z * cN2.z);
	cNrm.xy = cWo.xy * (cNrm.z / cWo.z); // back to upstream's stored z, so wNrmStr acts as today
	cNrm.xy *= mix(1.0, 0.15, smoothstep(2e3, 30e3, dst)); // not upstream: far waves flatten, the roughness takes over
#endif
	cNrm.z *= Const.wNrmStr;
	cNrm = normalize(cNrm);

	vec3 wnrmW = (Const.vTangent * cNrm.r) + (Const.vBiTangent * cNrm.g) + (vPlN * cNrm.b);
	wnrmW = mix(nvrW, wnrmW, fMask);
	float fDWS = dot(wnrmW, Const.toSun); // Water normal dot sun

	// Render with specular ripples and fresnel water -------------------------
	//
	float fDCH = clamp(dot(vRay, hlvW), 0.0, 1.0);
	float fDCN = clamp(dot(vRay, wnrmW), 0.0, 1.0);
	float fDHN = dot(hlvW, wnrmW);

	vec3 f = 1.0 - vec3(fDCH, fDCN, fDWS);
	vec3 fFresnel4 = f * f * f;
	vec3 fF = (0.15f + fFresnel4 * 0.85f) * fMask * Const.wSpec;

	// Compute specular reflection intensity
#if defined(_WATERFX)
	vec2 fS = 1.0 - vec2(fDCH, fDCN); // not upstream: Schlick with F0 0.02, x the glint on V.H, y the sky on V.N
	vec2 fFS = (0.02 + 0.98 * fS * fS * fS * fS * fS) * fMask * Const.wSpec;
	float fRgh = min(mix(0.08, 0.35, smoothstep(1e3, 80e3, dst)), 0.2); // not upstream: the glitter path widens with distance
#ifdef STAGE_PS
	vec3 dNx = dFdx(wnrmW), dNy = dFdy(wnrmW); // not upstream: specular AA, the normal's spread over a pixel widens the lobe (Kaplanyan 2016)
	fRgh = sqrt(fRgh * fRgh + min(0.5 * (dot(dNx, dNx) + dot(dNy, dNy)), 0.18));
#endif
	fSpe = GGX_NDF(fDHN, fRgh) * fFS.x * 6.2832; // not upstream: the lit ground here has no 1/pi and a x2, so 2pi keeps the glint to water ratio physical
#else
	fSpe = GGX_NDF(fDHN, 0.1f + clamp(fDWS, 0.0, 1.0) * 0.1f) * fF.y;
#endif
	fSpe /= (4.0f * fDCH * max(fDWS, fDCN) + 1e-3);

#if defined(_WATERFX)
	vec4 cUp = AmbientApprox(vPlN); // not upstream: the sky from orbit, without fSrf
	cRfl = cUp.rgb * cUp.a * 0.6 * fFS.y;
	if (!Flow.bInSpace) cRfl = mix(cRfl, GetAmbient(reflect(-vRay, wnrmW)) * fFS.y, fSrf); // not upstream: the sky in air as upstream, Schlick on V.N; meets the orbit term at the top of the air
	cTex.rgb = mix(cTex.rgb, vec3(0.012, 0.045, 0.075), 0.25 * fMask) + vec3(0.0, 0.08, 0.07) * max(fDPS, 0.0) * fMask * 0.3; // not upstream: deep water and a sun-facing subsurface
	cTex.rgb *= 1.0 - fFS.y; // not upstream: the reflected share does not enter the water
#else
	// Apply fresnel water only if close enough to a surface
	//
	if (!Flow.bInSpace)
	{
		cRfl = GetAmbient(reflect(-vRay, wnrmW)) * fF.y * fSrf;	
		// Attennuate diffuse texture for fresnel refl.
		cTex.rgb *= clamp(1.0f - f.y * fSrf * fMask, 0.0, 1.0) * clamp(1.0f - f.z * fSrf * fMask, 0.0, 1.0);
	}

	cTex.rgb = clamp(cTex.rgb + vec3(0, 0.55, 1.0) * Const.wBrightness * fMask, 0.0, 1.0);
#endif
#if defined(_SSR) && defined(STAGE_PS)
	if (fMask > 0.0 && gSsr.vMode.x > 0.5) { // not upstream: the mirror ray traced over last frame's screen, kept above the mean plane (design-ssr)
#if defined(_WATERFX)
		fSsrF = fFS.y;
		float aSsr = 0.35 * fRgh; // the waves are in wnrmW already; this is the sub-pixel spread (design-ssr NIT 8)
#else
		fSsrF = Flow.bInSpace ? 0.0 : fF.y * fSrf;
		float aSsr = 0.35 * (0.1 + clamp(fDWS, 0.0, 1.0) * 0.1);
#endif
		if (0.75 * fSsrF * SsrFade(aSsr) >= 0.01) ssrH = SsrTrace(-frg.camW.xyz, vPlN, SsrKeepAbove(SsrDir(normalize(wnrmW), vRay, aSsr, gl_FragCoord.xy), vPlN, 0.01), aSsr);
	}
#endif

#else
	// Fallback to simple specular reflection
	float fDHN = dot(hlvW, nvrW);
	fSpe = pow(clamp(fDHN, 0.0, 1.0), 60.0f) * fMask * 5.0f;
#endif
#if defined(_WATERFX)
	if (fMask > 0.0 && dst < 8e3) { // not upstream: surf foam where a ring of 8 mask taps at ~20 m texels, 40 m out (Prm.vWaterFx), finds a little land
		float fLnd = 0.0;
		for (int i = 0; i < 8; i++) fLnd += textureLod(tMask, vUVSrf + vec2(cos(float(i) * 0.7853982), sin(float(i) * 0.7853982)) * Prm.vWaterFx.yz, Prm.vWaterFx.x).a;
		fLnd *= 0.125;
		float fTm = Const.Time / 3600.0; // whole noise periods per wrap of Time
		float fVis = 1.0 - smoothstep(0.25, 0.6, fFoamW); // the big octave resolved
		float fN1 = mix(0.5, ClsNoise(vUVWt0 * 64.0 + vec2(64.0, 128.0) * fTm, 64.0), fVis); // each octave goes to its mean below ~2 px per cell
		float fN2 = mix(0.5, ClsNoise(vUVWt0 * 256.0 - vec2(512.0, 256.0) * fTm, 256.0), 1.0 - smoothstep(0.25, 0.6, 4.0 * fFoamW));
		float fN = fN1 * 0.6 + fN2 * 0.4;
		fFoam = 0.7 * fMask * smoothstep(0.02, 0.12, fLnd) * (1.0 - smoothstep(0.25, 0.5, fLnd)) * smoothstep(0.35, 0.65, fN) * mix(0.35, 1.0, fVis) * (1.0 - smoothstep(3e3, 8e3, dst)); // a thin broken line at the waterline, faint where its breaks are too small to see
		fSpe *= 1.0 - fFoam;
		cRfl *= 1.0 - fFoam;
	}
#endif
#endif

	vec3 nrmW = nvrW; // Micro normal defaults to vertex normal

	// Render with surface microtextures --------------------------------------
	//
#if defined(_MICROTEX)

	if (Flow.bMicroTex)
	{
		float step1 = 1.0 - smoothstep(3000, 15000, dst);	// smoothstep(15000, 3000, dst): GLSL leaves edge0 > edge1 undefined
		step1 *= (step1 * step1);
		vec3 cFnl = max(vec3(0), min(vec3(2), 1.333f * (cFar + cMed + cLow) - 1));

		// Create normals
		if (Flow.bMicroNormals)
		{
			cFnl = cFnl.bbb;

#if defined(_SOFT)
			vec2 cMix = (cFar.rg + cMed.rg + cLow.rg) * 0.6666f;			// SOFT BLEND
#endif
#if defined(_MED)
			vec2 cMix = (cFar.rg + 0.5f) * (cMed.rg + 0.5f) * (cLow.rg + 0.5f);	// MEDIUM BLEND
			fAmpf = 2.0f;
#endif
#if defined(_HARD)
			vec2 cMix = cFar.rg * cMed.rg * cLow.rg * 8.0f;				// HARD BLEND
			fAmpf = 4.0f;
#endif

			vec3 cNrm = vec3((cMix - 1.0f) * 2.0f, 0) * step1;
			cNrm.z = cos(cNrm.x * cNrm.y * 1.57);

			// Approximate world space normal
			nrmW = normalize((Const.vTangent * cNrm.x) + (Const.vBiTangent * cNrm.y) + (nvrW * cNrm.z));

			// Bend the normal towards sun a bit
			nrmW = normalize(nrmW + Const.toSun * 0.06f);
		}

		// Apply luminance
		cTex.rgb *= mix(vec3(1.0f), cFnl, step1);
	}
#endif

	// not upstream: land class micro detail, 8 m and 2 m cells, fading in below 5 km
	if (Flow.bClass)
	{
		float fade = 1.0 - smoothstep(800.0, 5000.0, dst), fadeP = 0.0;
#if defined(_WAYS)
		fadeP = fade * fWP; // not upstream (design-ways): wear and patches on pavement in place of the land detail
		fade *= 1.0 - fWP;
#endif
		vec3 w = texture(tClass, vUVSrf).rgb;	// outside the per-pixel branch: defined mip level
		if (fade + fadeP > 0.0) {
			vec2 p = frg.texUV.xy * Prm.vMSc[0].xy + Prm.vMSc[0].zw;
			float n1 = ClsNoise(p, 16384.0) * 0.6 + ClsNoise(p * 2.0, 32768.0) * 0.4;
			float n2 = ClsNoise(p * 4.0, 65536.0);
			float fT = (n1 - 0.5) * 0.9 + (n2 - 0.5) * 0.2;	// canopy clumps and gaps
			float fG = (n2 - 0.5) * 0.3 + (n1 - 0.5) * 0.15;	// grass and crop texture
			float fB = (n2 - 0.5) * 0.35 + (n1 - 0.5) * 0.1;	// grit
			cTex.rgb *= 1.0 + (w.r * fT + w.g * fG + w.b * fB) * fade + ((n1 - 0.5) * 0.2 + (n2 - 0.5) * 0.08) * fadeP;
		}
	}


	// Render Eclipse ------------------------------------------------------------
	//
	float fECL = GetEclipse(vVrt);

	vec3 cDiffLocal = vec3(0);

#if defined(_LOCALLIGHTS)
	LocalLights(cDiffLocal, nrmW, -frg.camW.xyz);
#endif

#if defined(_NO_ATMOSPHERE)

	float fDNS = clamp(dot(nvrW, Const.toSun), 0.0, 1.0);
	float fDCN = clamp(dot(nvrW, Const.toCam), 0.0, 1.0);
	float fLvl = 2.0f * fDNS / (fDNS + fDCN + 0.5f);
	float fSHD = 1.0f;

	// Shadowing by planet
	if (Flow.bPlanetShadow) {
		float palt = sqrt(clamp(1.0f - fDPS * fDPS, 0.0, 1.0)) * rad - Const.PlanetRad;
		fSHD = fDPS > 0 ? 1.0f : ilerp(Const.MinAlt, Const.MaxAlt, palt);
	}

	// Amplify light and shadows
	fLvl += dot(nvrW - vPlN, Const.toSun) * fLvl * Const.trLS;

	// Add opposition surge
	fLvl += pow(clamp(fDRS, 0.0, 1.0), 4.0f) * 0.3f * fDNS;

#if defined(_MICROTEX)
	fLvl += dot(nrmW - nvrW, Const.toSun) * ilerp(0.0, 0.03, fLvl) * fAmpf;
#endif

	fLvl *= fSHD;	// Apply planet shadow
	fLvl *= fECL;	// Apply eclipse

	vec3 color = cTex.rgb * LightFX(max(fLvl, 0.0) * fShadow + cDiffLocal);
	return vec4(pow(clamp(color * Const.TrExpo, 0.0, 1.0), vec3(Const.TrGamma)), 1.0f);		// Gamma corrention
#else

	float fShd = 1.0f;

#if defined(_CLOUDSHD)
	// Do we render cloud shadows ?
	if (Flow.bCloudShd) {
		fShd = (vUVCld.x < 1.0 ? fChA : fChB);
#if defined(_CLOUDATLAS)
		if (Prm.vCldAtlasW.x > 0.0) fShd = mix(fShd, fChAtl, fAtlW);
#endif
		fShd = clamp(1.0 - fShd * Prm.fAlpha, 0.0, 1.0);
	}
#endif

	vec3 cNgt = vec3(0);
	vec3 cNgt2 = vec3(0);
	float fDNS = dot(nvrW, Const.toSun); // Vertex normal dot sun

#if defined(_NIGHTLIGHTS)

	// Night lights ?
	float fNgt = clamp(-fDPS * 4.0f + 0.05f, 0.0, 1.0) * Prm.fBeta; // Night lights intensity and 'on' time

	cMsk.b = (cMsk.b > 0.15f ? cMsk.b : 0.0f); // Blue dirt filter

#if defined(_NIGHTLOOK)
	// not upstream: night look (design-night-look 5, Rev 2): the light map as a level with contrast, street lamps light the ground and glow; the orbital term only from ~11 km up
	float fNI = max(max(cMsk.r, cMsk.g), cMsk.b);
	float fNC = pow(fNI, 2.2); // suburbs and city fringes darker, cores full
	float fNS = 0.45 + 0.5 * fNC; // the share of lit street segments
	vec3 cNH = mix(vec3(1.0), cMsk.rgb / max(fNI, 1e-3), 0.5); // the map's own hue
	float fNP = 8.0 * fNS * 0.85 * 3.14159265;
	vec3 cNPl = NL_PALM * (fNP * 0.0049), cNHd = NL_PALM * (fNP * 0.000144), cNSp = NL_PALM * (0.24 * fNS); // the area means
	float fNU = 1.0;
#if defined(_WAYS)
	if (Flow.bWayN) { // not upstream (design-ways): the lamps of the streamed roads; none where the tile has no road
		vec3 q = cWN.rgb * cWN.rgb * 4.0;
		cNPl = NL_PALM * (q.r * fNS); cNHd = NL_PALM * (q.g * fNS); cNSp = NL_PALM * (q.b * fNS);
	} else
#endif
	if (Flow.bClass) { // the 8 m lattice is set
		vec2 q = (frg.texUV.xy * Prm.vMSc[0].xy + Prm.vMSc[0].zw) * 0.0625; // 128 m blocks
		vec2 fq = vec2(1.0), gNx = vec2(0.0), gNy = vec2(0.0);
#ifdef STAGE_PS
		fq = fwidth(q); gNx = dFdx(vUVSrf); gNy = dFdy(vUVSrf); // in uniform flow; the warp's slope is under ~0.1, so q's footprint is qw's
#endif
		if (fNgt > 0.0) { // by day nothing of the night look runs
			vec3 wN = textureGrad(tClass, vUVSrf, gNx, gNy).rgb;
			fNU = 1.0 - 0.75 * clamp(max(wN.r, wN.g) - wN.b, 0.0, 1.0); // trees, grass and fields dim; built-up and bare full
			if (min(fq.x, fq.y) < 0.35) NightLamps(NightWarp(q), fq, fNS, cNPl, cNHd, cNSp);
		}
	}
	float fNL = fNgt * (1.0 - Const.CamSpace) * fNC * fNU;
	cNgt = cNH * fNL * (NL_KAMB * NL_PALM + NL_KPOOL * cNPl + NL_KSPILL * cNSp); // lamp light on the ground's albedo
	cNgt2 = cMsk.rgb * Const.CamSpace * 4.0f * fNgt * smoothstep(0.35, 0.8, Const.CamSpace); // the orbital term from ~11 km up
	vec3 cNGl = cNH * fNL * NL_KHEAD * cNHd; // lamp glare, added through the air with sct.atn after the haze
#else
	cNgt = cMsk.rgb * (1 - Const.CamSpace) * fNgt; // Nightlights surface texture illumination term
	cNgt2 = cMsk.rgb * Const.CamSpace * 4.0f * fNgt; // Nightlights orbital visibility
#endif
#endif

	float fNoise = (textureLod(tNoise, frg.texUV.xy * 4.0f * Prm.fTgtScale, 0.0).r - 0.5f) * ATMNOISE;

	// Terrain with gamma correction and attennuation
	cTex.rgb = pow(clamp(cTex.rgb, 0.0, 1.0), vec3(Const.TrGamma)) * Const.TrExpo;

	// Evaluate ambient approximation
	vec4 cAmb = AmbientApprox(vPlN, false);
	
	LandOut sct = GetLandView(rad, vPlN);

	// Get the color of sunlight and set maximum intensity to 1.0
	vec3 cSun = GetSunColor(fDPS, alt);
	vec3 cSF = cSun * Const.cSun;
	float fMx = max(max(cSF.r, cSF.g), cSF.b);
	cSF = fMx > 1.0 ? cSF / fMx : cSF;

	float  fL = Const.trLS * 0.3f;
	float  fZ = clamp(dot(nvrW - vPlN, Const.toSun) * Const.trLS, -fL, fL);
	float  fX = 1.0f - pow(1.0f - clamp(fDPS, 0.0, 1.0), 2.0f);

	fZ = fZ > 0 ? fZ * 2.0f : fZ;

#if defined(_MICROTEX)
	float  fG = dot(nrmW - nvrW, Const.toSun) * fAmpf;
#else
	float  fG = 0.0f;
#endif

	// Diffuse "lambertian" shading term
	float  fD = mix(fX + (fG + fZ) * fX, fDPS * fDPS, fMask);

	// Water masking
	float  fM = 0.5f - fMask * 0.25f;

	// Ambient light for terrain
	//					  Color					   Distance				  Altitude factor		   Particle Density		
	vec3 cA = normalize(cAmb.rgb + cSF * 4.0f) * cAmb.a * cAmb.g * fM * exp(-alt * Const.iH.r) * Const.rmI.r * 6e5 * Const.TW_Terrain;

#if defined(_VOLSHD)
	fShd = mix(fShd, 1.0, Prm.vVolCld.z * (1.0 - smoothstep(35e3, 60e3, dst))); // not upstream: the volume cloud shadow replaces the 2D one near the camera
#endif
	fShd = clamp(fShd + (1.0f - fX), 0.0, 1.0);

	// Bake light and shadow terms
	vec3 cL = cSF * fD * fShadow * fShd;

	// Lit the texture with various things
	cTex.rgb *= cL * 2.0f + (cA + cDiffLocal + Const.cAmbient * Const.Ambient) * clamp(1.0f + fG + fZ, 0.0, 1.0) + cNgt;

	cTex.rgb = max(vec3(0, 0, 0), cTex.rgb);

#if defined(_WATERFX)
	cTex.rgb = mix(cTex.rgb, vec3(pow(0.8, Const.TrGamma) * Const.TrExpo) * (cSF * fX * fShadow * fShd * 2.0f + cA + Const.cAmbient * Const.Ambient), fFoam); // not upstream: foam as white ground, not as water
#endif
#if defined(_SSR)
	if (ssrH.conf > 0.0) { // not upstream: the traced reflection over the sky term; finite hits lose the camera-to-water haze, added again below (design-ssr Rev 2 S1)
		vec3 cHit = SsrPlanetDecode(SsrFetch(ssrH));
		if (!ssrH.sky) cHit = max(cHit - (sct.ray.rgb * RayPhase(-fDRS) + sct.mie.rgb * MiePhase(-fDRS)) * (1.0 - (1.0 - fShd) * Const.CamSpace * 0.5) * (1.0 + fNoise), vec3(0.0)) / max(sct.atn.rgb, vec3(1e-3)); // fOrbShd's factor, as the haze line below
#if defined(_WATERFX)
		cHit *= 1.0 - fFoam;
#endif
		cRfl = mix(cRfl, cHit * fSsrF, ssrH.conf);
	}
#endif
	// Add Reflection
	cTex.rgb += cRfl * 0.75f;

	// Add Specular component
#if defined(_WATERFX)
	cTex.rgb += cSun * fSpe * smoothstep(-0.001f, 0.03f, fDPS) * fShadow * fShd; // not upstream: the glint under cloud, terrain (fH is in fShadow) and shadow-map shadows
#elif defined(_CSM)
	cTex.rgb += cSun * fSpe * smoothstep(-0.001f, 0.03f, fDPS) * fShadow; // not upstream: fH and the cascades are in fShadow (design-scene-shadows 10)
#else
	if (Flow.bHorizon) cTex.rgb += cSun * fSpe * smoothstep(-0.001f, 0.03f, fDPS) * fH; // not upstream: the else is upstream's line
	else cTex.rgb += cSun * fSpe * smoothstep(-0.001f, 0.03f, fDPS);
#endif

	// Amplify cloud shadows for orbital views
	float fOrbShd = 1.0f - (1.0f - fShd) * Const.CamSpace * 0.5f;

	// Add Haze and night lights
	cTex.rgb *= sct.atn.rgb;
	cTex.rgb += (sct.ray.rgb * RayPhase(-fDRS) + sct.mie.rgb * MiePhase(-fDRS)) * fOrbShd * (1.0f + fNoise);

	cTex.rgb *= fECL;	// Apply eclipse
	cTex.rgb += cNgt2;
#if defined(_NIGHTLIGHTS) && defined(_NIGHTLOOK)
	cTex.rgb += cNGl * sct.atn.rgb; // not upstream: lamp glare through the air to the camera (design-night-look 5)
#endif

	return vec4(HDR(cTex.rgb), 1.0f);
#endif
}

#ifdef PS_TerrainPS
layout(location = 0) in TileVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = TerrainPS(frg); }
#endif







// ============================================================================
// Planet Cloud Renderer
// ============================================================================

CldVS CloudVS(TILEVERTEX vrt)
{
	// Zero output.
	CldVS outVS = CldVS(vec4(0), vec2(0), vec3(0), vec3(0));

	// Apply a world transformation matrix
	vec3 vPosW = (vec4(vrt.posL, 1.0f) * Prm.mWorld).xyz;
	vec3 vNrmW = (vec4(vrt.normalL, 0.0f) * Prm.mWorld).xyz;

	outVS.posH = vec4(vPosW, 1.0f) * Const.mVP;
	outVS.nrmW = vNrmW;
	outVS.posW = vPosW;
	outVS.texUV.xy = vrt.tex0.xy;						// Note: vrt.tex0 is un-used (hardcoded in Tile::CreateMesh and varies per tile)

	return outVS;
}

#ifdef VS_CloudVS
layout(location = 0) in vec3 iPosL;
layout(location = 1) in vec3 iNrmL;
layout(location = 5) in vec2 iTex0;
layout(location = 6) in float iElev;
layout(location = 0) out CldVS oVS;
void main() { oVS = CloudVS(TILEVERTEX(iPosL, iNrmL, iTex0, iElev)); gl_Position = oVS.posH; }
#endif


// ============================================================================
// 
vec4 CloudPS(CldVS frg)
{
	vec2 vUVTex = frg.texUV.xy * Prm.vCloudOff.zw + Prm.vCloudOff.xy; // not upstream: a tile without its own file shows its part of the parent's texture
	vec4 cTex = texture(tDiff, vUVTex);
#if defined(_CLOUDATLAS)
	vec2 vUVAtl = frg.texUV.xy * Prm.vCldAtlas.zw + Prm.vCldAtlas.xy; // not upstream: the seamless cloud alpha atlas
	float fAtlF = 0.0, fAtlW = 0.0, fLodT = 0.0;
	if (Prm.vCldAtlasW.x > 0.0) {
		fAtlF = AtlasW(frg.texUV.xy);
		fLodT = AnisoLod(vUVTex, vec2(512.0));
		fAtlW = max(fAtlF, AtlasEdge(vUVTex, fLodT));
		cTex.a = mix(cTex.a, texture(tCloudAtlas, vUVAtl).r, fAtlW);
	}
#endif
	float fVol = mix(1.0, smoothstep(Prm.vVolCld.x, Prm.vVolCld.y, length(frg.posW)), Prm.vVolCld.z); // not upstream: the volume clouds replace the layer near the camera
	vec3 vRay;
	vec3 vPxl;
	float  dRC;
	float  fNrm = 1.0f;

	if (Flow.bBelowClouds) {
		float  rRef = Const.PlanetRad + Const.smi * 0.5f;	// Reference altitude
		vec3 vRef = Const.toCam * rRef;
		vRay = normalize(Const.toCam * (Const.CamRad - rRef) + frg.posW); // Viewing ray to the pixel
		dRC = dot(vRay, Const.toCam);
		float  fEca2 = 1.0f - dRC * dRC;			// Ray horizon angle^2
		float  fD = Const.smi * inversesqrt(1.0f - Const.ecc * Const.ecc * fEca2); // Distance to ellipse threshold
		vPxl = vRef + vRay * fD;					// Pretend the pixel being closer and lower
	}
	else {
		vRay = normalize(frg.posW);					// Viewing ray to the pixel
		dRC = dot(vRay, Const.toCam);
		vPxl = Const.CamPos + frg.posW;				// Pixel's geocentric location
	}

	vec3 vPlN = normalize(vPxl);					// Mean Normal at pixel's locatin
	vec3 vVrt = Const.CamPos + frg.posW.xyz;	// Geo-centric pixel position
	vec3 nrm = vPlN;

	float dRS = dot(vRay, Const.toSun);
	float dMNus = dot(vPlN, Const.toSun);
	float dMN = clamp(dMNus, 0.0, 1.0);					// Mean normal sun angle
	float fPxR = dot(vPxl, vPlN);					// Pixel geo distance
	float fPxA = fPxR - Const.PlanetRad;			// Pixel altitude

	if (!Flow.bBelowClouds) fPxA = Const.CloudAlt;


	// -----------------------------------------------
	// Cloud layer rendering for Earth
	// -----------------------------------------------

#if defined(_CLOUDMICRO)
	vec2 vUVMic = frg.texUV.xy * Prm.vMicroOff.zw + Prm.vMicroOff.xy;
	vec4 cMic = texture(tCloudMicro, vUVMic);
#endif


#if defined(_CLOUDNORMALS)
#if defined(_CLOUDMICRO)

	vec4 cMicNorm = texture(tCloudMicroNorm, vUVMic);  // Filename "cloud1_norm.dds"

	// Extract normal from transparency (height) data
	// Filter width
	float d = 2.0 / 512.0;

	float x1 = texture(tDiff, vUVTex + vec2(-d, 0)).a;
	float x2 = texture(tDiff, vUVTex + vec2(+d, 0)).a;
	nrm.x = (x1 * x1 - x2 * x2);

	float y1 = texture(tDiff, vUVTex + vec2(0, -d)).a;
	float y2 = texture(tDiff, vUVTex + vec2(0, +d)).a;
	nrm.y = (y1 * y1 - y2 * y2);
#if defined(_CLOUDATLAS)
	if (Prm.vCldAtlasW.x > 0.0) { // not upstream: the same taps on the atlas
		vec2 da = Prm.vCldAtlasW.z > 0.0 ? 2.0 / vec2(textureSize(tCloudAtlas, 0)) : d * Prm.vCldAtlas.zw / Prm.vCloudOff.zw; // 2 texels of what is sampled
		x1 = mix(x1, texture(tCloudAtlas, vUVAtl + vec2(-da.x, 0)).r, max(fAtlF, AtlasEdge(vUVTex + vec2(-d, 0), fLodT)));
		x2 = mix(x2, texture(tCloudAtlas, vUVAtl + vec2(+da.x, 0)).r, max(fAtlF, AtlasEdge(vUVTex + vec2(+d, 0), fLodT)));
		y1 = mix(y1, texture(tCloudAtlas, vUVAtl + vec2(0, -da.y)).r, max(fAtlF, AtlasEdge(vUVTex + vec2(0, -d), fLodT)));
		y2 = mix(y2, texture(tCloudAtlas, vUVAtl + vec2(0, +da.y)).r, max(fAtlF, AtlasEdge(vUVTex + vec2(0, +d), fLodT)));
		nrm.x = (x1 * x1 - x2 * x2);
		nrm.y = (y1 * y1 - y2 * y2);
	}
#endif

	// Blend in cloud normals only on moderately thick clouds, allowing the highest cloud tops to be smooth.
	nrm.xy = (nrm.xy + clamp((cTex.a * 10.0f) - 3.0f, 0.0, 1.0) * clamp(((1.0f - cTex.a) * 10.0f) - 1.0f, 0.0, 1.0) * (cMicNorm.rg - 0.5f)); // new

	// Increase normals contrast based on sun-earth angle.
	nrm.xyz = nrm.xyz * (1.0f + (0.5f * dMN));

	nrm.z = sqrt(1.0f - clamp(nrm.x * nrm.x + nrm.y * nrm.y, 0.0, 1.0));

	// Approximate world space normal from local tangent space
	nrm = normalize((Const.vTangent * nrm.x) + (Const.vBiTangent * nrm.y) + (vPlN * nrm.z));

	float dCS = dot(nrm, Const.toSun); // Cloud normal sun angle

	// Brighten the lighting model for clouds, based on sun-earth angle. Twice is better.
	// Low sun angles = greater effect. No modulation leads to washed out normals at high sun angles.
	dCS = clamp((1.0f - dMN) * (dCS * (1.0f - dCS)) + dCS, 0.0, 1.0);
	dCS = clamp((1.0f - dMN) * (dCS * (1.0f - dCS)) + dCS, 0.0, 1.0);

	// With a high sun angle, don't let the dCS go below 0.2 to avoid unnaturally dark edges.
	dCS = mix(0.2f * dMN, 1.0f, dCS);

	// Effect of normal/sun angle to color
	// Add some brightness (borrowing red channel from sunset attenuation)
	// Adding it to the sun illumination factor, taking care to keep from saturating
	fNrm = dCS +((1.0f - dCS) * 0.2f);
#endif
#endif

#if defined(_CLOUDMICRO)
	float f = cTex.a;
	float g = mix(1.0f, cMic.a, 1.0f - abs(dot(Const.vPolarAxis, vPlN)));
	float h = (g + 4.0f) * 0.2f;
	cTex.a = clamp(mix(g, h, f) * f, 0.0, 1.0);
#endif

	// Render Eclipse ------------------------------------------------------------
	//
	float fECL = GetEclipse(vVrt);

	if (Flow.bBelowClouds)
	{
		// Get sunlight color
		vec3 cSun = GetSunColor(dMNus, fPxA);

		// Get ambient information
		vec4 cMlt = AmbientApprox(vPlN);

		cSun *= clamp(dRS + 1.3f, 0.0, 1.0);
		float fPh = pow(clamp(1.0f - dRC, 0.0, 1.0), 32.0f) * pow(clamp(dRS, 0.0, 1.0), 10.0f); // Boost near horizon and close the sun
		cSun *= 1.0f + fPh * 8.0f;

		cSun *= Const.cSun * fNrm;
		cSun *= Const.Clouds;
		cSun += cMlt.rgb * cMlt.a * 0.2f;

		LandOut sct = GetLandView(fPxA + Const.PlanetRad, vPlN);

		cTex.rgb *= cSun;
		cTex.rgb *= sct.atn.rgb;
		cTex.rgb += sct.ray.rgb * 2.0f;
		cTex.rgb *= fECL;

		return vec4(HDR(cTex.rgb), clamp(cTex.a, 0.0, 1.0) * fVol); // not upstream: fVol
	}
	else {

		// Get sunlight color
		vec3 cSun = GetSunColor(dMN, fPxA);
	
		// Get ambient information
		vec4 cAmb = AmbientApprox(dMNus);
		vec3 cMSC = Const.RayWave * Const.RayWave * Const.Clouds; // Multiscatter color

		cSun = sqrt(cMSC * cMSC + cSun * cSun * fNrm) * cAmb.a;
		
		LandOut sct = GetLandView(fPxA + Const.PlanetRad, vPlN);

		cTex.rgb *= cSun;
		cTex.rgb *= sct.atn.rgb;
		cTex.rgb += sct.ray.rgb;
		cTex.rgb *= fECL;

		return vec4(sqr(HDR(cTex.rgb * 4.0f)), cTex.a * cAmb.a * cAmb.a * fVol); // not upstream: fVol
	}	
}

#ifdef PS_CloudPS
layout(location = 0) in CldVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = CloudPS(frg); }
#endif








// ============================================================================
// Gas Giant Renderer
// ============================================================================

TileVS GiantVS(TILEVERTEX vrt)
{
	// Zero output.
	TileVS outVS = TileVS(vec4(0), vec2(0), vec4(0), vec3(0)
#if defined(_SHDMAP)
		, vec4(0)
#endif
	);
	
	// Apply a world transformation matrix
	vec3 vPosW = (vec4(vrt.posL, 1.0f) * Prm.mWorld).xyz;
	vec3 vNrmW = (vec4(vrt.normalL, 0.0f) * Prm.mWorld).xyz;
	
	outVS.posH = vec4(vPosW, 1.0f) * Const.mVP;
	outVS.texUV.xy = vrt.tex0.xy;
	outVS.camW = vec4(-vPosW, 0);
	outVS.nrmW = vNrmW;

	return outVS;
}

#ifdef VS_GiantVS
layout(location = 0) in vec3 iPosL;
layout(location = 1) in vec3 iNrmL;
layout(location = 5) in vec2 iTex0;
layout(location = 6) in float iElev;
layout(location = 0) out TileVS oVS;
void main() { oVS = GiantVS(TILEVERTEX(iPosL, iNrmL, iTex0, iElev)); gl_Position = oVS.posH; }
#endif

// ============================================================================
//
vec4 GiantPS(TileVS frg)
{

	vec2 vUVSrf = frg.texUV.xy * Prm.vTexOff.zw + Prm.vTexOff.xy;
	
	// Fetch Main Textures
	vec4 cTex = texture(tDiff, vUVSrf);
	
	vec3 nrmW = normalize(frg.nrmW);			// Per-pixel surface normal vector
	vec3 vRay = normalize(frg.camW.xyz);		// Unit viewing ray
	vec3 vVrt = Const.CamPos - frg.camW.xyz;	// Geo-centric pixel position
	vec3 vPlN = normalize(vVrt);				// Planet mean normal
	float  fDPS = dot(vPlN, Const.toSun);
	vec3 cSun = vec3(clamp((fDPS + 0.1) * 5.0, 0.0, 1.0));


	// Render Eclipse ------------------------------------------------------------
	//
	cSun *= GetEclipse(vVrt);

	// Terrain with gamma correction and attennuation
	cTex.rgb = pow(clamp(cTex.rgb, 0.0, 1.0), vec3(Const.TrGamma)) * Const.TrExpo;

	vec3 color = cTex.rgb * LightFX(cSun + vec3(0.9, 0.9, 1.0) * Const.Ambient);

	return vec4(HDR(color), 1.0f);
}

#ifdef PS_GiantPS
layout(location = 0) in TileVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = GiantPS(frg); }
#endif


// ============================================================================
// Gas giant cloud layer renderer
// ============================================================================

vec4 GiantCloudPS(CldVS frg)
{
	vec4 cTex = texture(tDiff, frg.texUV.xy * Prm.vCloudOff.zw + Prm.vCloudOff.xy); // not upstream: parent's sub-range
#if defined(_CLOUDATLAS)
	if (Prm.vCldAtlasW.x > 0.0) { // not upstream
		vec2 uvT = frg.texUV.xy * Prm.vCloudOff.zw + Prm.vCloudOff.xy;
		cTex.a = mix(cTex.a, texture(tCloudAtlas, frg.texUV.xy * Prm.vCldAtlas.zw + Prm.vCldAtlas.xy).r, max(AtlasW(frg.texUV.xy), AtlasEdge(uvT, AnisoLod(uvT, vec2(512.0)))));
	}
#endif
	vec3 vPlN = normalize(frg.nrmW);
	vec3 vRay = normalize(frg.posW);
	vec3 vVrt = Const.CamPos + frg.posW.xyz;	// Geo-centric pixel position
	float  fDPS = dot(vPlN, Const.toSun);    // Planet mean normal sun angle

	vec3 cSun = vec3(clamp((fDPS + 0.1) * 5.0, 0.0, 1.0));

	// Render Eclipse ------------------------------------------------------------
	//
	float fECL = GetEclipse(vVrt);

	cTex.rgb *= LightFX(cSun + vec3(1.0, 1.0, 1.0) * Const.Ambient);
	cTex.rgb = pow(clamp(cTex.rgb, 0.0, 1.0), vec3(Const.TrGamma)) * Const.TrExpo;
	cTex.rgb *= fECL;

	return vec4(HDR(cTex.rgb), clamp(cTex.a, 0.0, 1.0));
}

#ifdef PS_GiantCloudPS
layout(location = 0) in CldVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = GiantCloudPS(frg); }
#endif
