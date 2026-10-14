// not upstream: re-entry hull heating of the vessel being drawn (design-reentry-heat 5, Rev 3); zero for every other draw; must match HullHeat.h HeatPrm

#define HEAT_NSEG 16

struct HeatPrm
{
	vec4 vFlow;		// xyz downstream direction (render frame), w 1 on
	vec4 vStag;		// xyz stagnation point (camera-relative), w nose radius [m]
	vec4 vCoef;		// x flat-plate factor, y lee share, z visible emissivity floor, w spare
	vec4 vBin[8];	// rgb ln of the glow radiance of bin k (G_k = 2 * 0.4^k, scene units), w its temperature [K]
	vec4 vEdgeN;	// x number of attachment segments, yzw spare
	vec4 vEdge[3 * HEAT_NSEG];	// per segment: end a (camera-relative) and band half-width [m], end b and level G_e, attachment normal
};
