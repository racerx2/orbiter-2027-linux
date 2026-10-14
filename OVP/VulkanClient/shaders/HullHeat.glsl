// not upstream: re-entry hull heating, the glow of the hull's own skin (design-reentry-heat 2.2, 2.4, 3, 5, Rev 2); pixel stage only; HeatMath.cpp has the same formulas

#ifdef STAGE_PS

struct HeatGeo { float k1; float k2; vec3 d2; };	// principal curvatures [1/m] (>= 0) and the low-curvature direction

// call at the top of the pixel shader, before any discard: screen derivatives in uniform control flow (design Rev 2 M1)
HeatGeo HeatGeom (vec3 nrmW, vec3 camW)
{
	HeatGeo g = HeatGeo(0.0, 0.0, vec3(0.0));
	if (gHeat.vFlow.w <= 0.5) return g;
	vec3 N = normalize(nrmW), P = -camW;
	vec3 Nx = dFdx(N), Ny = dFdy(N), Px = dFdx(P), Py = dFdy(P);
	float lx = length(Px), ly = length(Py);
	vec3 e1 = Px - N * dot(N, Px);
	if (length(e1) < 0.1 * lx) e1 = Py - N * dot(N, Py);	// dP/dx along the normal: edge-on
	float l1 = length(e1);
	if (!(lx > 0.0 && ly > 0.0 && l1 > 0.0)) return g;
	e1 /= l1;
	vec3 e2 = cross(N, e1);
	mat2 JP = mat2(dot(e1, Px), dot(e2, Px), dot(e1, Py), dot(e2, Py));	// screen step to tangent coordinates
	mat2 JN = mat2(dot(e1, Nx), dot(e2, Nx), dot(e1, Ny), dot(e2, Ny));
	float det = determinant(JP);
	if (!(abs(det) > 1e-4 * lx * ly)) return g;
	mat2 W = JN * inverse(JP);	// Weingarten map, symmetrised below
	float a = W[0][0], d = W[1][1], b = 0.5 * (W[1][0] + W[0][1]);
	float m = 0.5 * (a + d), r = sqrt(0.25 * (a - d) * (a - d) + b * b);
	g.k1 = max(0.0, m + r);
	g.k2 = max(0.0, m - r);
	float lam = m - r;
	vec2 v = abs(b) > 1e-12 * (abs(a) + abs(d) + 1e-30) ? (abs(lam - d) > abs(lam - a) ? vec2(lam - d, b) : vec2(b, lam - a)) : (a <= d ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
	float lv = length(v);
	if (lv > 0.0) g.d2 = (v.x * e1 + v.y * e2) / lv;
	return g;
}

float HeatAngle (float c)	// Lees (1956) hemisphere distribution as 0.55 + 0.45 cos 2theta, 0 by c = -0.25
{
	float u = max(0.0, 1.0 + 4.0 * c);
	return c >= 0.0 ? 0.1 + 0.9 * c * c : 0.1 * u * u;
}

// q_local / q_stagnation (design 2.2, Rev 2 M1, M2): stagnation region, swept attachment lines, laminar flat plate, lee floor
float HeatRatio (vec3 N, vec3 P, HeatGeo g)
{
	vec3 f = gHeat.vFlow.xyz;
	float c = clamp(-dot(N, f), -1.0, 1.0);
	float Rn = gHeat.vStag.w;
	float x = length(P - gHeat.vStag.xyz);
	float r = x / (1.5 * Rn);
	float Gs = HeatAngle(c) * exp(-r * r);
	float Gf = c > 0.0 ? gHeat.vCoef.x * sqrt(sqrt(max(0.0, 1.0 - c * c))) * c * sqrt(Rn / max(x, Rn)) : 0.0;	// Tauber et al. (1987) laminar flat plate
	float an = (g.k1 + g.k2) > 0.0 ? (g.k1 - g.k2) / (g.k1 + g.k2) : 0.0;
	float sL = clamp(an * abs(dot(f, g.d2)), 0.0, 1.0), cL = sqrt(1.0 - sL * sL);
	float cn = clamp(c / max(cL, 1e-3), -1.0, 1.0);
	float Ge = gHeat.vCoef.w * HeatAngle(cn) * cL;
	float Gc = smoothstep(2.0, 4.0, Rn * g.k1) * smoothstep(0.85, 0.97, cn) * sqrt(Ge * Ge + Gf * Gf * sL * sL);	// swept cylinder (Tauber et al. 1987) on real edges holding an attachment line; HeatMath::Tune gate0/1, attach0/1
	return clamp(max(Gs, max(Gf, Gc)) + gHeat.vCoef.y, 0.0, 2.0);
}

// linear scene radiance of the hull's glow; albLin the linear albedo (Kirchhoff: visible emissivity 1 - albedo)
vec3 HeatGlow (vec3 nrmW, vec3 camW, HeatGeo g, vec3 albLin)
{
	float G = HeatRatio(normalize(nrmW), -camW, g);
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
