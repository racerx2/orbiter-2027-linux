// not upstream: re-entry hull heating of the vessel being drawn (design-reentry-heat 5); zero for every other draw; must match HullHeat.h HeatPrm

struct HeatPrm
{
	vec4 vFlow;		// xyz downstream direction (render frame), w 1 on
	vec4 vStag;		// xyz stagnation point (camera-relative), w nose radius [m]
	vec4 vCoef;		// x flat-plate factor, y lee share, z visible emissivity floor, w leading-edge level
	vec4 vBin[8];	// rgb ln of the glow radiance of bin k (G_k = 2 * 0.4^k, scene units), w its temperature [K]
};
