// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream (stream-rwylights): runway lights on the streamed airports (StreamRwy.h)

#include "StreamRwy.h"
#include "RunwayLights.h"
#include "VPlanet.h"
#include "Scene.h"
#include "D3D9Config.h"
#include "D3D9Util.h"
#include "Log.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace {
const double MinLength = 800.0;  // no lights on shorter runways: the tiles can't tell a grass strip
const double LitRange = 80e3;    // lights built and drawn within this camera distance [m]
const double FreeRange = 100e3;  // and freed past this one
}

VECTOR3 StreamRwys::Frame::ToLocal (VECTOR3 pos, double *lng, double *lat) const
{
	VECTOR3 q = RwyFrame::ToPlanet (a, pos);
	if (lng && lat) RwyFrame::LatLng (q, *lat, *lng);
	return q;
}

void StreamRwys::Frame::FromLocal (VECTOR3 pos, D3DXVECTOR3 *pTgt) const
{
	VECTOR3 p = RwyFrame::FromPlanet (a, pos);
	*pTgt = D3DXVECTOR3 (float (p.x), float (p.y), float (p.z));
}

StreamRwys::StreamRwys (vPlanet *_vp, const Scene *_scn) : vp (_vp), scn (_scn)
{
}

StreamRwys::~StreamRwys ()
{
}

// every base's runways from its file, as RunwayLights::CreateRunwayLights reads them: a base draws and lights its own (a RUNWAY of NRWSEG 0 has no triangles for the ways to keep off)
void StreamRwys::ReadBases ()
{
	basesRead = true;
	OBJHANDLE hP = vp->GetObject ();
	MATRIX3 rp;
	VECTOR3 gp;
	oapiGetRotationMatrix (hP, &rp);
	oapiGetGlobalPos (hP, &gp);
	for (DWORD i = 0, n = oapiGetBaseCount (hP); i < n; i++) {
		OBJHANDLE hb = oapiGetBaseByIndex (hP, i);
		const char *file = oapiGetObjectFileName (hb);
		if (!file) continue;
		std::ifstream f (oapiResolvePath (file));
		if (!f) continue;
		MATRIX3 rb;
		VECTOR3 gb;
		oapiGetRotationMatrix (hb, &rb);
		oapiGetGlobalPos (hb, &gb);
		VECTOR3 p = tmul (rp, gb - gp), e[2];
		bool block = false;
		int have = 0;
		std::string line;
		while (std::getline (f, line)) {
			size_t s = line.find_first_not_of (" \t\r");
			if (s == std::string::npos) continue;
			const char *t = line.c_str () + s;
			if (!block) {
				if (!strncmp (t, "RUNWAY", 6)) { block = true; have = 0; } // RUNWAY and RUNWAYLIGHTS
				continue;
			}
			if (!strncmp (t, "END1", 4) || !strncmp (t, "END2", 4)) {
				int k = t[3] - '1';
				if (sscanf (t + 4, "%lf%lf%lf", &e[k].x, &e[k].y, &e[k].z) == 3) have |= 1 << k;
			}
			else if (!strncmp (t, "END", 3)) {
				if (have == 3) baseRwys.push_back ({p + tmul (rp, mul (rb, e[0])), p + tmul (rp, mul (rb, e[1]))});
				block = false;
			}
		}
	}
	LogAlw ("Streamed runways: %zu base runways kept to their bases", baseRwys.size ());
}

void StreamRwys::Add (const std::vector<WayGeom::RunwayLL> &in)
{
	if (!basesRead) ReadBases ();
	double R = vp->GetSize ();
	for (const WayGeom::RunwayLL &d : in) {
		double L = RwyFrame::Length (d, R);
		if (L < MinLength) continue;
		bool have = false;
		for (const auto &r : rw)
			if (RwyFrame::Same (r->d, d, R)) { have = true; break; }
		if (have) continue;
		auto r = std::make_unique<Rwy> ();
		r->d = d;
		r->mid = unit (RwyFrame::Point (d.lat[0], d.lng[0], 1.0) + RwyFrame::Point (d.lat[1], d.lng[1], 1.0));
		VECTOR3 A = RwyFrame::Point (d.lat[0], d.lng[0], R), B = RwyFrame::Point (d.lat[1], d.lng[1], R);
		for (const BaseRwy &b : baseRwys)
			if (RwyFrame::OnBase (A, B, b.a, b.b)) { r->base = true; break; }
		rw.push_back (std::move (r));
		LogAlw ("Streamed runway %s %s: %.0f x %.0f m at %.5f %.5f", d.ref.empty () ? "-" : d.ref.c_str (), rw.back ()->base ? "left to its base" : "lit", L, d.w, d.lat[0] * DEG, d.lng[0] * DEG);
	}
}

void StreamRwys::Build (Rwy &r)
{
	OBJHANDLE hP = vp->GetObject ();
	double R = vp->GetSize (), mlat, mlng, elv = 0.0;
	RwyFrame::LatLng (r.mid, mlat, mlng);
	if (vp->GetElevation (mlng, mlat, &elv) != 1) elv = oapiSurfaceElevation (hP, mlng, mlat);
	r.f.a = RwyFrame::At (mlat, mlng, R + elv);
	VECTOR3 e[2];
	for (int i = 0; i < 2; i++) {
		e[i] = RwyFrame::FromPlanet (r.f.a, RwyFrame::Point (r.d.lat[i], r.d.lng[i], R + elv));
		e[i].y = 0.0;
	}
	r.lights = std::make_unique<RunwayLights> (&r.f, hP, scn);
	r.lights->SetEnd1 (e[0]);
	r.lights->SetEnd2 (e[1]);
	r.lights->SetWidth (r.d.w);
	r.lights->AddPAPI (_V(3.17, 0.67, 300.0), 1.0f, 0); // a 3 deg PAPI on the pilot's left, 300 m past each threshold
	r.lights->AddPAPI (_V(3.17, 0.67, 300.0), 1.0f, 1);
	r.lights->Init ();
}

void StreamRwys::Render (VkDev *dev)
{
	if (!Config->StreamRwyLights || rw.empty ()) return;
	OBJHANDLE hP = vp->GetObject ();
	VECTOR3 pg, cam = scn->GetCameraGPos ();
	MATRIX3 prot;
	oapiGetGlobalPos (hP, &pg);
	oapiGetRotationMatrix (hP, &prot);
	double R = vp->GetSize (), syst = oapiGetSysTime (), simt = oapiGetSimTime ();
	bool main = scn->GetRenderPass () == RENDERPASS_MAINSCENE;
	bool check = main && (syst - tRange > 2.0 || syst < tRange);
	if (check) tRange = syst;
	for (auto &p : rw) {
		Rwy &r = *p;
		double dist = length (pg + mul (prot, r.mid * R) - cam);
		if (r.base) continue;
		if (check) {
			if (!r.lights && dist < LitRange) Build (r);
			else if (r.lights && dist > FreeRange) r.lights.reset ();
		}
		if (!r.lights || dist > LitRange) continue;
		MATRIX3 grot = mul (prot, r.f.a.r);
		VECTOR3 gpos = pg + mul (prot, r.f.a.o);
		if (main && (std::fabs (simt - r.tSun) > 1.0 || oapiGetPause ())) { // night as vBase::Update: the sun under NightLightsAngle
			VECTOR3 sdir = tmul (grot, -unit (gpos));
			r.night = sdir.y < RAD * Config->SunAngle;
			r.tSun = simt;
		}
		VECTOR3 cpos = gpos - cam;
		D3DXMATRIX W;
		D3DXMatrixIdentity (&W);
		D3DMAT_SetInvRotation (&W, &grot);
		D3DMAT_SetTranslation (&W, &cpos);
		if (main) r.lights->Update (vp);
		r.lights->Render (dev, &W, r.night);
	}
}
