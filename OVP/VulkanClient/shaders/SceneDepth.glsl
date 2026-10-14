
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: camera distance of the terrain tiles drawn this frame, for SSAO and the cloud march (design-ground-graphics 3)
// _SSGI: albedo and octahedral normal (Cigolle et al. 2014) as two more targets (design-ssgi 4.1)

struct DepthPrm
{
	mat4 mW;	// tile to camera-relative world
	mat4 mVP;	// view projection of the SceneDepth pass
#if defined(_SSGI)
	vec4 vTexOff;	// tile texture range: uv * zw + xy
	vec4 vAlb;		// gamma, scale, has texture, 0: albedo = pow(tex, x) * y (NewPlanet's terrain albedo)
#endif
};

layout(binding = 0, row_major, scalar) uniform DepthVSBlock { DepthPrm Prm; };

#ifdef VS_TerrainDepthVS
layout(location = 0) in vec3 iPosL;
#if defined(_SSGI)
layout(location = 1) in vec3 iNrmL;
layout(location = 5) in vec2 iTex0;
layout(location = 1) out vec3 oNrmW;
layout(location = 2) out vec2 oUV;
#endif
layout(location = 0) out vec3 oPosW;
void main()
{
	oPosW = (vec4(iPosL, 1.0) * Prm.mW).xyz;
	gl_Position = vec4(oPosW, 1.0) * Prm.mVP;
#if defined(_SSGI)
	oNrmW = (vec4(iNrmL, 0.0) * Prm.mW).xyz;
	oUV = iTex0 * Prm.vTexOff.zw + Prm.vTexOff.xy;
#endif
}
#endif

#ifdef PS_TerrainDepthPS
layout(location = 0) in vec3 iPosW;
layout(location = 0) out vec4 oColor;
#if defined(_SSGI)
layout(location = 1) in vec3 iNrmW;
layout(location = 2) in vec2 iUV;
layout(binding = 4) uniform sampler2D tDiff;
layout(location = 1) out vec4 oAlb;
layout(location = 2) out vec4 oNrm;
vec2 OctEnc(vec3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	vec2 p = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return p * 0.5 + 0.5;
}
#endif
void main()
{
	oColor = vec4(length(iPosW));
#if defined(_SSGI)
	vec3 t = Prm.vAlb.z > 0.5 ? texture(tDiff, iUV).rgb : vec3(0.5);	// NewPlanet: grey without a texture
	vec3 rho = pow(clamp(t, 0.0, 1.0), vec3(Prm.vAlb.x)) * Prm.vAlb.y;
	vec3 n = normalize(iNrmW);	// silhouette normals facing away are bent toward the view in SSGI.glsl
	oAlb = vec4(clamp(rho * 0.25, 0.0, 1.0), 0.0);
	oNrm = vec4(OctEnc(n), 0.0, 0.0);	// class 0 terrain
#endif
}
#endif
