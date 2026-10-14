// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the legacy particle billboards faded by SceneFx's tDepth in the main pass (r3 trail, compiled only with VolPlumes = 1)
// Lorach 2007 (soft particles), Umenhoffer et al. 2006 (spherical billboards)

#if defined(VS_SoftPtclVS) || defined(PS_SoftPtclPS)
layout(binding = 0, row_major, scalar) uniform SoftPtclVSB	// uniform extern: the vertex shader
{
mat4	mVP;		// camera-relative world to clip, the effect's gVP
};
layout(binding = 1, row_major, scalar) uniform SoftPtclPSB	// the pixel shader
{
vec4	pColor;		// texture tint (1 diffuse, gColor emissive), opacity of the batch (gMix)
vec4	pFade;		// x: 1 for a stream born inside its own hull, y: shortest fade in radii
};
layout(binding = 4) uniform sampler2D tPtcl;	// the stream's texture, as WrapS
layout(binding = 5) uniform sampler2D tDepthS;	// SceneFx camera distance, 0 none, full res
#endif

#if defined(VS_SoftPtclVS)
layout(location = 0) in vec3 iPosL;	// camera-relative corner
layout(location = 1) in vec3 iNrmL;	// corner in radii (x, y), radius m
layout(location = 5) in vec2 iTex0;
layout(location = 0) out vec2 oTex;
layout(location = 1) out vec3 oPos;
layout(location = 2) out vec3 oLoc;
void main()
{
	oTex = iTex0;
	oPos = iPosL;
	oLoc = iNrmL;
	gl_Position = vec4(iPosL, 1.0) * mVP;
}
#endif

#if defined(PS_SoftPtclPS)
layout(location = 0) in vec2 iTex;
layout(location = 1) in vec3 iPos;
layout(location = 2) in vec3 iLoc;
layout(location = 0) out vec4 oColor;

float Fade(ivec2 p, float d0, float il)	// opacity factor against one tDepth texel
{
	float ds = texelFetch(tDepthS, clamp(p, ivec2(0), textureSize(tDepthS, 0) - 1), 0).r;
	return ds > 0.0 ? clamp((ds - d0) * il, 0.0, 1.0) : 1.0;
}

void main()
{
	vec4 c = texture(tPtcl, iTex);
	float R = max(iLoc.z, 1e-3);
	float h = R * sqrt(max(1.0 - dot(iLoc.xy, iLoc.xy), 0.0));	// half chord of the particle's sphere behind this pixel
	float d0 = length(iPos) + pFade.x * h, il = 1.0 / max(h, pFade.y * R);	// the back half in front of the scene; a hull inside the sphere stays clear
	ivec2 q = ivec2(gl_FragCoord.xy);	// with TAA tDepth was drawn jittered and these billboards after jitter-off, up to half a pixel apart: the tent absorbs it, keep it
	float f = 0.5 * Fade(q, d0, il) + 0.125 * (Fade(q + ivec2(1, 0), d0, il) + Fade(q - ivec2(1, 0), d0, il) + Fade(q + ivec2(0, 1), d0, il) + Fade(q - ivec2(0, 1), d0, il));	// tent over the pixel's neighbours: depth edges blend like the anti-aliased colour
	oColor = vec4(c.rgb * pColor.rgb, c.a * pColor.a * f);
}
#endif
