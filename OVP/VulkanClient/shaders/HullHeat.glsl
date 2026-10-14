// not upstream: re-entry hull heating, the glow of the hull's own skin (design-reentry-heat 2.2, 2.4, 3, 5, Rev 2, Rev 3); pixel stage only; HeatMath.cpp has the same formulas

#ifdef STAGE_PS

float HeatAngle (float c)	// Lees (1956) hemisphere distribution as 0.55 + 0.45 cos 2theta, 0 by c = -0.25
{
	float u = max(0.0, 1.0 + 4.0 * c);
	return c >= 0.0 ? 0.1 + 0.9 * c * c : 0.1 * u * u;
}

// the attachment lines the CPU found on the mesh's real edges (Rev 3): Lees around a cylinder of the band's half-width, the far side of a thin edge cut by the normal
float HeatEdges (vec3 N, vec3 P)
{
	float G = 0.0;
	int n = min(int(gHeat.vEdgeN.x), HEAT_NSEG);
	for (int k = 0; k < n; k++) {
		vec4 A = gHeat.vEdge[3 * k], B = gHeat.vEdge[3 * k + 1], C = gHeat.vEdge[3 * k + 2];
		vec3 ab = B.xyz - A.xyz;
		float t = clamp(dot(P - A.xyz, ab) / max(dot(ab, ab), 1e-12), 0.0, 1.0);
		float th = length(P - A.xyz - t * ab) / max(A.w, 1e-6);
		G = max(G, B.w * HeatAngle(cos(min(th, 3.0))) * smoothstep(-0.5, 0.0, dot(N, C.xyz)));
	}
	return G;
}

// q_local / q_stagnation (design 2.2, Rev 3): stagnation region, laminar flat plate, the edge bands, lee floor
float HeatRatio (vec3 N, vec3 P)
{
	vec3 f = gHeat.vFlow.xyz;
	float c = clamp(-dot(N, f), -1.0, 1.0);
	float Rn = gHeat.vStag.w;
	float x = length(P - gHeat.vStag.xyz);
	float r = x / (1.5 * Rn);
	float Gs = HeatAngle(c) * exp(-r * r);
	float Gf = c > 0.0 ? gHeat.vCoef.x * sqrt(sqrt(max(0.0, 1.0 - c * c))) * c * sqrt(Rn / max(x, Rn)) : 0.0;	// Tauber et al. (1987) laminar flat plate
	return clamp(max(max(Gs, Gf), HeatEdges(N, P)) + gHeat.vCoef.y, 0.0, 2.0);
}

// linear scene radiance of the hull's glow; albLin the linear albedo (Kirchhoff: visible emissivity 1 - albedo)
vec3 HeatGlow (vec3 nrmW, vec3 camW, vec3 albLin)
{
	float G = HeatRatio(normalize(nrmW), -camW);
	float t = clamp(log(max(G, 1e-30) * 0.5) * (1.0 / log(0.4)), 0.0, 9.0);	// bins G_k = 2 * 0.4^k
	int k = min(int(t), 6);
	vec3 a = gHeat.vBin[k].rgb, b = gHeat.vBin[k + 1].rgb;
	vec3 lnE = max(a + (t - float(k)) * (b - a), vec3(-60.0));
	float ev = clamp(1.0 - dot(clamp(albLin, 0.0, 1.0), vec3(0.2126, 0.7152, 0.0722)), gHeat.vCoef.z, 1.0);
	return ev * exp(lnE);
}

// sRGB transfer (IEC 61966-2-1), valid above 1 for the HDR target
vec3 HeatToLinear (vec3 c)
{
	c = max(c, vec3(0.0));
	return mix(c * (1.0 / 12.92), pow((c + 0.055) * (1.0 / 1.055), vec3(2.4)), step(vec3(0.04045), c));
}

vec3 HeatToSrgb (vec3 c)
{
	c = max(c, vec3(0.0));
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// the legacy shaders light in display values: the glow is added in linear (call inside gHeat.vFlow.w > 0.5)
vec3 HeatAddDisplay (vec3 c, vec3 E)
{
	return HeatToSrgb(HeatToLinear(c) + E);
}

#endif
