// not upstream: a layered hydrostatic atmosphere (Pluto, Triton, Titan) from temperature nodes, the way MarsAtm2006 builds Mars
#pragma once
#include <cmath>

struct AtmNode { double h, T; }; // geometric altitude [m], temperature [K]

// temperature linear in geopotential altitude between nodes and constant above the last; pressure from hydrostatic balance
class LayerAtm {
public:
	// p0 surface pressure [Pa], R specific gas constant [J/(kg K)], rad reference radius [m], g0 surface gravity [m/s^2]
	LayerAtm (const AtmNode *node, int n, double p0, double R, double rad, double g0)
		: n (n < NMAX ? n : NMAX), R (R), rad (rad), g0 (g0)
	{
		for (int i = 0; i < this->n; i++) { zb[i] = Geo (node[i].h); Tb[i] = node[i].T; }
		for (int i = 0; i < this->n; i++) a[i] = (i+1 < this->n ? (Tb[i+1] - Tb[i])/(zb[i+1] - zb[i]) : 0.0);
		double T;
		pb[0] = p0;
		for (int i = 1; i < this->n; i++) pb[i] = P (i-1, zb[i], T);
	}

	// temperature [K], pressure [Pa] and density [kg/m^3] at geometric altitude alt [m]
	void Get (double alt, double &T, double &p, double &rho) const
	{
		double z = Geo (alt);
		int i = n - 1;
		while (i > 0 && z < zb[i]) i--;
		p = P (i, z, T);
		rho = p/(R*T);
	}

private:
	static const int NMAX = 8;
	int n;
	double R, rad, g0;
	double zb[NMAX], Tb[NMAX], a[NMAX], pb[NMAX]; // base geopotential altitude, temperature, lapse rate [K/m] and pressure of each layer

	double Geo (double h) const { return h*rad/(rad + h); }

	double P (int i, double z, double &T) const
	{
		double dz = z - zb[i];
		T = Tb[i] + a[i]*dz;
		if (a[i] == 0.0) return pb[i]*std::exp (-g0/(R*Tb[i])*dz);
		return pb[i]*std::pow (T/Tb[i], -g0/(R*a[i]));
	}
};
