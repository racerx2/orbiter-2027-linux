// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the scene shadow casters' own shaders, depth only (design-scene-shadows Rev 2): terrain tiles and trees; meshes use NewMesh.glsl's NormalDepth

#if defined(VS_CsmTerrainVS) || defined(PS_CsmTerrainPS)
struct CsmTerrainPrm
{
	mat4 mW;	// tile to camera-relative world, this frame's
	mat4 mVP;	// the cascade's clip, its tile folded into the atlas
};
layout(binding = 0, row_major, scalar) uniform CsmTerrainBlock { CsmTerrainPrm Prm; };

#ifdef VS_CsmTerrainVS
layout(location = 0) in vec3 iPosL;
void main() { gl_Position = vec4((vec4(iPosL, 1.0) * Prm.mW).xyz, 1.0) * Prm.mVP; }
#endif

#ifdef PS_CsmTerrainPS
void main() {}
#endif
#endif

#if defined(VS_CsmTreeVS) || defined(PS_CsmTreePS)
struct CsmTreePrm
{
	mat4 mW;	// cell frame to camera-relative world
	mat4 mVP;	// the cascade's clip
	vec4 vCam;	// camera in the cell frame (xyz), time s (w)
	vec4 vBand;	// meshes below this camera distance m (x), its spread over the trees m (y), range m (z)
	vec4 vSun;	// toward the sun in the cell frame (xyz); 0 meshes, 1 cards (w)
};
layout(binding = 0, row_major, scalar) uniform CsmTreeBlock { CsmTreePrm Prm; };

#ifdef VS_CsmTreeVS
layout(location = 0) in vec3 iPos;
layout(location = 6) in vec4 iInst;		// position in the cell frame, height
layout(location = 3) in vec4 iCol;		// yaw, kind, tint r, tint g
layout(location = 0) out vec4 oImp;		// card uv (xy), kind (z), height m (w)
void main()
{
	vec3 ip = iInst.xyz;
	float s = iInst.w;
	float d = length(ip - Prm.vCam.xyz);
	bool imp = Prm.vSun.w > 0.5;
	float kind = mod(floor(iCol.y * 255.0 + 0.5), 16.0); // not upstream (design-far-forest): the level in the high bits
	float thr = Prm.vBand.x + Prm.vBand.y * fract(iCol.z * 157.0 + iCol.w * 311.0); // per tree: the two draws meet without a line
	oImp = vec4(0.0, 0.0, kind, s);
	if ((!imp && d >= thr) || (imp && (d < thr || d > Prm.vBand.z))) {
		gl_Position = vec4(0.0, 0.0, -2.0, 1.0); // the other draw's tree
		return;
	}
	vec3 p;
	if (!imp) { // Tree.glsl's mesh with its sway
		float a = iCol.x * 6.2831853, c = cos(a), sn = sin(a);
		vec3 q = iPos * s;
		float sway = 0.015 * s * iPos.y * iPos.y * sin(Prm.vCam.w * 1.3 + iCol.z * 40.0);
		p = ip + vec3(q.x * c - q.z * sn + sway, q.y, q.x * sn + q.z * c);
	}
	else { // a card facing the sun: across the light, up tilted off it, crown and trunk as the sun sees them, at the crown's depth
		vec3 L = Prm.vSun.xyz;
		vec3 r = normalize(cross(vec3(0.0, 1.0, 0.0), L) + vec3(1e-4, 0.0, 0.0));
		vec3 u = normalize(cross(L, r));
		float w = kind < 1.5 ? 0.55 : (kind > 3.5 ? 1.1 : 0.85);
		float se = clamp(L.y, 0.0, 1.0), ce = sqrt(1.0 - se * se);
		p = ip + r * (iPos.x * s * w) + u * (iPos.y * s * (ce + w * se)) + L * (0.5 * s * se);
		oImp.xy = vec2(iPos.x + 0.5, iPos.y);
	}
	gl_Position = vec4((vec4(p, 1.0) * Prm.mW).xyz, 1.0) * Prm.mVP;
}
#endif

#ifdef PS_CsmTreePS
layout(location = 0) in vec4 iImp;
void main()
{
	if (Prm.vSun.w < 0.5) return; // meshes: solid
	float x = iImp.x - 0.5, y = iImp.y, kind = iImp.z;
	float e = sin(x * iImp.w * 1.7 + y * iImp.w * 2.3) * 0.04; // Tree.glsl's outline
	bool inside;
	if (kind < 1.5) inside = abs(x) < 0.5 * (1.0 - y) * (0.9 + e) && y > 0.12;
	else if (kind > 3.5) { vec2 q = vec2(x / 0.5, (y - 0.45) / 0.45); inside = dot(q, q) < 1.0 + e; }
	else { vec2 q = vec2(x / 0.45, (y - 0.62) / 0.38); inside = dot(q, q) < 1.0 + e; }
	bool trunk = abs(x) < 0.04 && y < 0.4 && kind < 3.5;
	if (!inside && !trunk) discard;
}
#endif
#endif
