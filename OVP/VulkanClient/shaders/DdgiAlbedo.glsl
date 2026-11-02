// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: albedo the ray-traced GI's hits read: a tile's texture into its atlas cell, a mesh group's mean into its table texel (design-ddgi 3)
// the group's albedo as SceneDepth_PS writes it for SSGI (NewMesh.glsl)

layout(binding = 0, row_major, scalar) uniform DgAlbBlock
{
	vec4 vRange;	// texture range: uv * zw + xy
	vec4 vMat;		// albedo factor (rgb), base texture used (w)
	vec4 vMtl;		// metal factor, metal map bound, map channel b (else g), base texture decoded to linear
	vec4 vOpt;		// PBR family: kept in the sRGB-encoded space the bounce uses (x)
};

layout(binding = 4) uniform sampler2D tSrc;
layout(binding = 5) uniform sampler2D tMR;

#define DG_TAPS 8

#if defined(VS_DgAlbVS)
layout(location = 0) out vec2 oUV;
void main()	// a triangle over the viewport
{
	vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	oUV = p;
	gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
#endif

#if defined(PS_DgTileCell)
layout(location = 0) in vec2 iUV;
layout(location = 0) out vec4 oColor;
void main()	// a 64 x 64 cell from the tile's part of its texture, at the mip that matches it
{
	vec2 sz = vec2(textureSize(tSrc, 0)) * vRange.zw;
	float lod = clamp(log2(max(sz.x, sz.y) / 64.0), 0.0, float(textureQueryLevels(tSrc) - 1));
	oColor = vec4(textureLod(tSrc, iUV * vRange.zw + vRange.xy, lod).rgb, 1.0);
}
#endif

#if defined(PS_DgMean)
layout(location = 0) out vec4 oColor;
vec3 S2L(vec3 c) { c = max(c, vec3(0.0)); return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }
vec3 L2S(vec3 c) { c = max(c, vec3(0.0)); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }
vec4 Mean(sampler2D s)	// DG_TAPS^2 taps over the range at the mip that holds about as many texels
{
	vec2 sz = vec2(textureSize(s, 0)) * vRange.zw;
	float lod = clamp(log2(max(sz.x, sz.y) / float(DG_TAPS)), 0.0, float(textureQueryLevels(s) - 1));
	vec4 m = vec4(0.0);
	for (int j = 0; j < DG_TAPS; j++)
		for (int i = 0; i < DG_TAPS; i++) m += textureLod(s, (vec2(i, j) + 0.5) / float(DG_TAPS) * vRange.zw + vRange.xy, lod);
	return m / float(DG_TAPS * DG_TAPS);
}
void main()
{
	vec3 c = vMat.w > 0.5 ? clamp(Mean(tSrc).rgb, 0.0, 1.0) : vec3(1.0);
	if (vMat.w > 0.5 && vMtl.w > 0.5) c = S2L(c);	// UNORM base colour to linear (PbrMesh's PBRM_BASEGAMMA)
	vec3 rho = c * vMat.rgb;
	float metal = vMtl.x;
	if (vMtl.y > 0.5) { vec4 m = Mean(tMR); metal *= vMtl.z > 0.5 ? m.b : m.g; }
	rho *= 1.0 - clamp(metal, 0.0, 1.0);	// metals have no diffuse part
	if (vOpt.x > 0.5) rho = L2S(rho);	// PBR albedo is linear, the bounce works on sRGB-encoded values as SSGI's senders
	oColor = vec4(clamp(rho, 0.0, 1.0), 1.0);
}
#endif
