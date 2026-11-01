// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream (stream-rwylights): runway lights on the streamed airports, the runways WayGeom finds lit by RunwayLights

#ifndef __STREAMRWY_H
#define __STREAMRWY_H

#include "BeaconFrame.h"
#include "RwyFrame.h"
#include <memory>
#include <vector>

class RunwayLights;
class vPlanet;
class Scene;
class VkDev;

class StreamRwys {
public:
	StreamRwys (vPlanet *vp, const Scene *scn);
	~StreamRwys ();
	void Add (const std::vector<WayGeom::RunwayLL> &rw); // a runway listed again is kept once
	void Render (VkDev *dev);

private:
	class Frame : public BeaconFrame {
	public:
		RwyFrame::Axes a;
		VECTOR3 ToLocal (VECTOR3 pos, double *lng = NULL, double *lat = NULL) const override;
		void FromLocal (VECTOR3 pos, D3DXVECTOR3 *pTgt) const override;
	};
	struct Rwy {
		WayGeom::RunwayLL d;
		VECTOR3 mid;                          // unit vector to its middle, planet frame
		Frame f;
		std::unique_ptr<RunwayLights> lights; // built near the camera
		bool night = false;
		bool base = false;                    // a base's runway: its base lights it
		double tSun = -1e9;
		double h[2] = {0.0, 0.0};             // not upstream (stream-rwyflat): the ground at its ends when built
	};
	void Build (Rwy &r);
	bool Moved (const Rwy &r) const;          // not upstream (stream-rwyflat): that ground has changed since
	void ReadBases ();
	struct BaseRwy { VECTOR3 a, b; };     // a base's RUNWAY or RUNWAYLIGHTS ends, planet frame
	std::vector<BaseRwy> baseRwys;
	bool basesRead = false;
	vPlanet *vp;
	const Scene *scn;
	std::vector<std::unique_ptr<Rwy>> rw;
	double tRange = -1e9;
};

#endif // !__STREAMRWY_H
