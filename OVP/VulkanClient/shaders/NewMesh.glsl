// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2022-2026 Jarmo Nikkanen
// ==============================================================

#define BOOL bool

// Constant Buffers --------------------------------------
//
struct VSConst {
	mat4 mVP;	// View Projection Matrix
	mat4 mW;	// World Matrix
};

struct PSConst {
	vec3 Cam_X;
	vec3 Cam_Y;
	vec3 Cam_Z;
};

struct PSBools {
	BOOL bOIT;		// Enable order independent transparency
	float fCutoff;	// custom: alpha test level (0.75 legacy, the material's for PBR mask groups)
	BOOL bClip;		// custom: below the cutoff the shadow map pixel is dropped (PBR), not written as an occluder (legacy)
};

// uniform extern struct X {...} x: the structs in blocks (vertex binding 0, pixel binding 1), set by name as the whole struct
layout(binding = 0, row_major, scalar) uniform VSConstBlock { VSConst vs_const; };
#if defined(_SSGI)
struct PSGi {		// not upstream: SceneDepth_PS's albedo for SSGI (design-ssgi 4.1)
	vec4 vMat;		// albedo factor (rgb), base texture bound (w)
	vec4 vMtl;		// metal factor (x), metal map bound (y), map channel b (z, else g), decode the base texture sRGB to linear (w)
	vec4 vCls;		// class (x: 2 bases and buildings, 3 vessels), family (y: 0 legacy, 1 metalness, 2 pbr, 3 base tile)
};
layout(binding = 1, row_major, scalar) uniform PSConstBlock { PSConst ps_const; PSBools ps_bools; PSGi ps_gi; };
layout(binding = 5) uniform sampler2D tMR;	// metalness map
#else
layout(binding = 1, row_major, scalar) uniform PSConstBlock { PSConst ps_const; PSBools ps_bools; };
#endif


layout(binding = 4) uniform sampler2D tDiff;	// s0

// Vertex data input layouts -----------------------------
//
struct MESH_VERTEX
{
	vec3 posL;	// POSITION0
	vec3 nrmL;	// NORMAL0
	vec3 tanL;	// TANGENT0
	vec3 tex0;	// TEXCOORD0	// Handiness in .z
};

struct POSTEX
{
	vec3 posL;	// POSITION0
	vec2 tex0;	// TEXCOORD0
};

struct SHADOW_VERTEX
{
	vec4 posL;	// POSITION0
};



// Internal data feeds between VS and PS ------------------
//
struct BShadowVS
{
	vec4 posH;	// POSITION0
	vec2 dstW;	// TEXCOORD0
};

struct ShadowTexVS
{
	vec4 posH;	// POSITION0
	vec4 tex0;	// TEXCOORD0	// distance in .zw
};

struct NormalTexVS
{
	vec4 posH;	// POSITION0
	vec3 posW;	// TEXCOORD0
	vec3 nrmW;	// TEXCOORD1
	vec2 tex0;	// TEXCOORD2
};

struct PBRData
{
	vec4 posH;     // POSITION0
	vec3 camW;     // TEXCOORD0
	vec2 tex0;     // TEXCOORD1
	vec3 nrmW;     // TEXCOORD2
	vec4 tanW;     // TEXCOORD3	 // Handiness in .w
#if SHDMAP > 0
	vec4 shdH;     // TEXCOORD4
#endif
};

struct BasicData
{
	vec4 posH;     // POSITION0
	vec3 camW;     // TEXCOORD0
	vec2 tex0;     // TEXCOORD1
	vec3 nrmW;     // TEXCOORD2
#if SHDMAP > 0
	vec4 shdH;     // TEXCOORD3
#endif
};



// -----------------------------------------------------------------------------------
// Shadow Map rendering with plain geometry (without texture) 
//
BShadowVS ShdMapVS(SHADOW_VERTEX vrt)
{
	// Zero output.
	BShadowVS outVS = BShadowVS(vec4(0), vec2(0));
	vec3 posW = (vec4(vrt.posL.xyz, 1.0f) * vs_const.mW).xyz;
	outVS.posH = vec4(posW, 1.0f) * vs_const.mVP;
	outVS.dstW = outVS.posH.zw;
	return outVS;
}

#ifdef VS_ShdMapVS
layout(location = 0) in vec4 iPosL;
layout(location = 0) out BShadowVS oVS;
void main() { oVS = ShdMapVS(SHADOW_VERTEX(iPosL)); gl_Position = oVS.posH; }
#endif

vec4 ShdMapPS(BShadowVS frg)
{
	return vec4(1 - (frg.dstW.x / frg.dstW.y));
}

#ifdef PS_ShdMapPS
layout(location = 0) in BShadowVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = ShdMapPS(frg); }
#endif




// -----------------------------------------------------------------------------------
// Shadow Map rendering with texture alpha included
//
ShadowTexVS ShdMapOIT_VS(POSTEX vrt)
{
	// Zero output.
	ShadowTexVS outVS = ShadowTexVS(vec4(0), vec4(0));
	vec3 posW = (vec4(vrt.posL.xyz, 1.0f) * vs_const.mW).xyz;
	outVS.posH = vec4(posW, 1.0f) * vs_const.mVP;
	outVS.tex0 = vec4(vrt.tex0.xy, outVS.posH.zw);
	return outVS;
}

#ifdef VS_ShdMapOIT_VS
layout(location = 0) in vec3 iPosL;
layout(location = 5) in vec2 iTex0;
layout(location = 0) out ShadowTexVS oVS;
void main() { oVS = ShdMapOIT_VS(POSTEX(iPosL, iTex0)); gl_Position = oVS.posH; }
#endif

vec4 ShdMapOIT_PS(ShadowTexVS frg)
{
	if (ps_bools.bOIT) {
		float alpha = texture(tDiff, frg.tex0.xy).a;
		if (alpha < ps_bools.fCutoff) {
#ifdef STAGE_PS
			if (ps_bools.bClip) discard; // custom
#endif
			return vec4(1.0f);
		}
	}
	return vec4(1 - (frg.tex0.z / frg.tex0.w));
}

#ifdef PS_ShdMapOIT_PS
layout(location = 0) in ShadowTexVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = ShdMapOIT_PS(frg); }
#endif




// -----------------------------------------------------------------------------------
// Render Normal and depth buffer
//
NormalTexVS NormalDepth_VS(MESH_VERTEX vrt)
{
	// Zero output.
	NormalTexVS outVS = NormalTexVS(vec4(0), vec3(0), vec3(0), vec2(0));

	outVS.posW = (vec4(vrt.posL.xyz, 1.0f) * vs_const.mW).xyz;
	outVS.nrmW = (vec4(vrt.nrmL, 0.0f) * vs_const.mW).xyz;
	outVS.posH = vec4(outVS.posW, 1.0f) * vs_const.mVP;
	outVS.tex0 = vrt.tex0.xy;
	return outVS;
}

#ifdef VS_NormalDepth_VS
layout(location = 0) in vec3 iPosL;
layout(location = 1) in vec3 iNrmL;
layout(location = 2) in vec3 iTanL;
layout(location = 5) in vec3 iTex0;
layout(location = 0) out NormalTexVS oVS;
void main() { oVS = NormalDepth_VS(MESH_VERTEX(iPosL, iNrmL, iTanL, iTex0)); gl_Position = oVS.posH; }
#endif

#ifdef STAGE_PS
vec4 NormalDepth_PS(NormalTexVS frg)
{
	if (ps_bools.bOIT) {
		if (texture(tDiff, frg.tex0.xy).a < ps_bools.fCutoff) discard; // clip(-1)
	}
	//if (dot(frg.nrmW, ps_const.Cam_Z) > 0) clip(-1);

	float D = length(frg.posW);
	float x = dot(frg.nrmW, ps_const.Cam_X);
	float y = dot(frg.nrmW, ps_const.Cam_Y);
	float z = sqrt(clamp(1.0 - (x * x + y * y), 0.0, 1.0));
	return vec4(x, y, z, D);
}
#endif

#ifdef PS_NormalDepth_PS
layout(location = 0) in NormalTexVS frg;
layout(location = 0) out vec4 oColor;
void main() { oColor = NormalDepth_PS(frg); }
#endif

// not upstream: camera distance for the SceneDepth pass (SSAO, cloud march); textured groups alpha tested
#ifdef PS_SceneDepth_PS
layout(location = 0) in NormalTexVS frg;
layout(location = 0) out vec4 oColor;
#if defined(_SSGI)
layout(location = 1) out vec4 oAlb;	// not upstream: SSGI albedo / 4, family
layout(location = 2) out vec4 oNrm;	// not upstream: SSGI octahedral world normal, class
vec2 OctEnc(vec3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	vec2 p = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return p * 0.5 + 0.5;
}
#endif
void main()
{
#if defined(_SSGI)
	vec4 t = texture(tDiff, frg.tex0.xy);
	if (ps_bools.bOIT && t.a < ps_bools.fCutoff) discard;
#else
	if (ps_bools.bOIT && texture(tDiff, frg.tex0.xy).a < ps_bools.fCutoff) discard; // custom: 0.75 legacy, the material's for PBR
#endif
	oColor = vec4(length(frg.posW));
#if defined(_SSGI)
	vec3 c = clamp(t.rgb, 0.0, 1.0);
	if (ps_gi.vMtl.w > 0.5) c = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); // UNORM base colour to linear (PbrMesh's PBRM_BASEGAMMA)
	vec3 rho = (ps_gi.vMat.w > 0.5 ? c : vec3(1.0)) * ps_gi.vMat.rgb;
	float metal = ps_gi.vMtl.x;
	if (ps_gi.vMtl.y > 0.5) { vec4 m = texture(tMR, frg.tex0.xy); metal *= ps_gi.vMtl.z > 0.5 ? m.b : m.g; }
	rho *= 1.0 - clamp(metal, 0.0, 1.0);	// metals have no diffuse part
	vec3 n = normalize(frg.nrmW);
	if (dot(n, frg.posW) > 0.25 * length(frg.posW)) n = -n;	// inverted mesh normals; slight silhouette ones are bent in SSGI.glsl
	oAlb = vec4(clamp(rho * 0.25, 0.0, 1.0), ps_gi.vCls.y / 3.0);
	oNrm = vec4(OctEnc(n), 0.0, ps_gi.vCls.x / 3.0);
#endif
}
#endif
