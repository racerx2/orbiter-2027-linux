// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: camera distance of the terrain tiles drawn this frame, for SSAO and the cloud march (design-ground-graphics 3)

struct DepthPrm
{
	mat4 mW;	// tile to camera-relative world
	mat4 mVP;	// view projection of the SceneDepth pass
};

layout(binding = 0, row_major, scalar) uniform DepthVSBlock { DepthPrm Prm; };

#ifdef VS_TerrainDepthVS
layout(location = 0) in vec3 iPosL;
layout(location = 0) out vec3 oPosW;
void main()
{
	oPosW = (vec4(iPosL, 1.0) * Prm.mW).xyz;
	gl_Position = vec4(oPosW, 1.0) * Prm.mVP;
}
#endif

#ifdef PS_TerrainDepthPS
layout(location = 0) in vec3 iPosW;
layout(location = 0) out vec4 oColor;
void main() { oColor = vec4(length(iPosW)); }
#endif
