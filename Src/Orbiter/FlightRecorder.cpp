// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#define OAPI_IMPLEMENTATION

#include "Orbiter.h"
#include "Vessel.h"
#include "SuperVessel.h"
#include "Celbody.h"
#include "Psys.h"
#include "Camera.h"
#include "Vecmat.h"
#include "PlaybackEd.h"
#include "Log.h"
#include "Pane.h"
#include "State.h"
#include "MenuInfoBar.h"
#include "DlgMgr.h"
#include <fstream>
#include <string>
#include <filesystem>
#include <QWindow>
namespace fs = std::filesystem;

using namespace std;

extern Orbiter *g_pOrbiter;
extern Pane *g_pane;
extern TimeData td;
extern PlanetarySystem *g_psys;
extern Camera *g_camera;
extern char DBG_MSG[256];

const double max_step = 2.0;
const double att_step = 1.0;
const double eng_eps = 0.5;
const double EPS = 1e-8;

const int NTHGROUP = 15;
const char *THGROUPSTR[NTHGROUP] = {
	"MAIN","RETRO","HOVER",
	"RCS_PITCHUP","RCS_PITCHDOWN",
	"RCS_YAWLEFT","RCS_YAWRIGHT",
	"RCS_BANKLEFT","RCS_BANKRIGHT",
	"RCS_RIGHT","RCS_LEFT",
	"RCS_UP","RCS_DOWN",
	"RCS_FORWARD","RCS_BACK"	
};

static double Tofs = 0.0;
static double MJDofs = 0.0;
double RecordingSpeed = 1.0;
double WarpDelay = 0.0;
Vessel *vfocus = NULL;  // focus vessel as defined by playback stream

static bool ResolveInPlace (char *buf, size_t size, int len) // not upstream: buf (snprintf gave len) case-resolved in place; false and "" when either doesn't fit
{
	std::string path;
	if (len >= 0 && (size_t)len < size) path = oapiResolvePath (buf);
	if (path.empty () || path.size () >= size) {
		buf[0] = '\0';
		return false;
	}
	memcpy (buf, path.c_str (), path.size () + 1);
	return true;
}

static char *TokenValue (char *s, size_t ofs) // not upstream: the text at s+ofs as upstream read it: inside the token, the rest of the line (strtok) just past it, NULL past the line's end
{
	size_t n = strlen (s);
	if (ofs <= n) return s + ofs;
	return (ofs == n + 1 ? strtok (NULL, "") : NULL);
}

// ================================================================
// Local prototypes
// ================================================================

void Euler2Quaternion (double *a, Quaternion &q, int frm);


// ================================================================
// Flight recorder methods in class Vessel
// ================================================================

void Vessel::FRecorder_Reset ()
{
	frec_last.fstatus = FLIGHTSTATUS_UNDEFINED;
	frec_last.simt = frec_last_syst = -1e10;
	frec_last.frm = g_pOrbiter->Cfg()->CfgRecPlayPrm.RecordPosFrame;
	//frec_last.frm = 0;  // ecliptic frame by default
	frec_last.crd = 0;  // cartesian coordinates by default
	frec_last.ref = 0;
	frec = 0;
	nfrec = 0;
	frec_att = 0;
	frec_att_last.simt = frec_att_last_syst = -1e10;
	frec_att_last.frm = g_pOrbiter->Cfg()->CfgRecPlayPrm.RecordAttFrame;
	frec_att_last.ref = 0;
	nfrec_att = 0;
	nfrec_eng = 0;
	frec_eng_simt = -1e10;
	FRfname = 0;
	bFRplayback = bRequestPlayback = false;
	bFRrecord = false;
	RecordingSpeed = 1.0;
	WarpDelay = 0.0;
	vfocus = NULL;
	FRatc_stream = 0;
}

void Vessel::FRecorder_Activate (bool active, const char *fname, bool append)
{
	if (bFRrecord == active) return; // nothing to do
	if (active) {
		if (!append) FRecorder_Reset();
		bFRrecord = true;
		char cbuf[256];
		Tofs = td.SimT0; // not upstream: moved before the length check, the system recorder's events use it even when no vessel is recorded
		MJDofs = td.MJD0; // not upstream: as above
		int len = snprintf (cbuf, sizeof cbuf, "Flights/%s/%s.pos", fname, name.c_str()); // not upstream: bounded
		if (!ResolveInPlace (cbuf, sizeof cbuf, len)) { // not upstream: case-insensitive path, resolved once for all streams; N: the .att/.atc names are built from it in 256 bytes
			LOGOUT_WARN ("Flight recorder: file name too long for record %s, vessel %s; the vessel isn't recorded", fname, name.c_str());
			bFRrecord = false;
			return;
		}
		if (FRfname) delete []FRfname;
		FRfname = new char[strlen(cbuf)+1]; TRACENEW
		strcpy (FRfname, cbuf);
		//frec_last.frm = 1;  // for now, record in equatorial frame by default
		frec_last.crd = 1;  // for now, record in polar coordinates by default
	} else {
		bFRrecord = false;
		FRecorder_Save (true);
	}
}

void Vessel::FRecorder_Save (bool force)
{
	int i, iter = 0, niter = 1;
	DWORD j;
	double dt, alim;
	bool isfirst   = (frec_last.fstatus == FLIGHTSTATUS_UNDEFINED);
	bool newstatus = (frec_last.fstatus != fstatus);
	force = force || isfirst || newstatus;
	bool attforce  = force;

	const CelestialBody *ref = frec_last.ref;
	if (cbody != ref) force = true, niter++;
	if (frec_last.fstatus == FLIGHTSTATUS_LANDED && fstatus == FLIGHTSTATUS_FREEFLIGHT)
		FRecorder_SaveEvent ("TAKEOFF", cbody->Name());

	for (iter = 0; iter < niter; iter++) {
		if (ref) {
			Vector pos = s0->pos-ref->GPos();
			Vector vel = s0->vel-ref->GVel();
			if (frec_last.frm == 1) { // map to equatorial
				//vel = tmul (cbody->GRot(), vel);
				double lng, lat, rad, vref;
				ref->LocalToEquatorial (tmul (ref->GRot(), pos), lng, lat, rad);
				vref = Pi2/ref->RotT() * rad*cos(lat);
				vel = tmul (ref->GRot(), vel) - Vector(-vref*sin(lng),0,vref*cos(lng));
			}
			double cddir = dotp (vel.unit(), frec_last.rvel.unit());

			bool nextstep = (fstatus == FLIGHTSTATUS_FREEFLIGHT &&
				(g_pOrbiter->Cfg()->CfgRecPlayPrm.bSysInterval ? td.SysT1 - frec_last_syst : td.SimT1 - frec_last.simt) > max_step);

			if (nextstep || (cddir < 0.995) || force) {
				frec_last.simt    = td.SimT1;
				frec_last_syst    = td.SysT1;
				frec_last.fstatus = fstatus;
				//frec_last.ref     = cbody;
				//frec_last.rpos    = *gpos-cbody->GPos();
				switch (frec_last.frm) {
					case 0:  // ecliptic frame
						frec_last.rpos = s0->pos-ref->GPos();
						break;
					case 1:  // equatorial frame
						frec_last.rpos = ref->GlobalToLocal (s0->pos);
						break;
				}
				frec_last.rvel    = vel;

				ofstream ofs (FRfname, ios::app);
				ofs << setprecision(10)  << (frec_last.simt-Tofs) << ' ';
				if (frec_last.crd == 1) { // store in polar coords
					double r = frec_last.rpos.length();
					double phi = atan2 (frec_last.rpos.z, frec_last.rpos.x);
					double tht = asin (frec_last.rpos.y/r);
					ofs << setprecision(12) << r << ' ' << phi << ' ' << tht << ' ';
					double sphi = sin(phi), cphi = cos(phi), stht = sin(tht), ctht = cos(tht);
					double arg  = cphi*frec_last.rvel.x + sphi*frec_last.rvel.z;
					double vr   = stht*frec_last.rvel.y + ctht*arg;
					double vphi = (cphi*frec_last.rvel.z - sphi*frec_last.rvel.x) / (r*ctht);
					double vtht = (ctht*frec_last.rvel.y - stht*arg)/r;
					ofs << setprecision(10) << vr << ' ' << vphi << ' ' << vtht << endl;
				} else {
					ofs << setprecision(12) << frec_last.rpos.x << ' ' << frec_last.rpos.y << ' ' << frec_last.rpos.z << ' ';
					ofs << setprecision(10) << frec_last.rvel.x << ' ' << frec_last.rvel.y << ' ' << frec_last.rvel.z << endl;
				}
			}
		}
		if (cbody != ref) {
			ofstream ofs(FRfname, isfirst ? ios::trunc : ios::app);
			ofs << "STARTMJD " << setprecision(12) << MJDofs << endl;
			ofs << "REF " << cbody->Name() << endl;
			ofs << "FRM " << (frec_last.frm == 0 ? "ECLIPTIC" : "EQUATORIAL") << endl;
			ofs << "CRD " << (frec_last.crd == 0 ? "CARTESIAN" : "POLAR") << endl;
			frec_last.ref = ref = cbody;
		}
	} 

	// attitude data
	ref = frec_att_last.ref;
	niter = 1;
	if (ref != sp.ref) attforce = true, niter++;
	for (iter = 0; iter < niter; iter++) {
		if (ref) {
			double a[3];
			Quaternion q;
			switch (frec_att_last.frm) {
			case 0:
				a[0] = atan2 (s0->R.m23, s0->R.m33);
				a[1] = -asin (s0->R.m13);
				a[2] = atan2 (s0->R.m12, s0->R.m11);
				q.Set (s0->Q);
				break;
			case 1: {
				double lng, lat, rad, slng, slat, clng, clat;
				ref->GlobalToEquatorial (s0->pos, lng, lat, rad);
				slng = sin(lng), clng = cos(lng), slat = sin(lat), clat = cos(lat);
				Matrix rot (-slng, clat*clng, -slat*clng, // horizon->local
					         0   , slat,       clat,
							 clng, clat*slng, -slat*slng);
				//Matrix rot (sp.Hor2Local());
				rot.premul (ref->GRot());
				rot.tpremul (s0->R);

				if (fabs(rot.m32) > 1.0-EPS) { // apply tiny tilt when pitch=+/-90 to avoid instability
					static const double sineps=sin(1e-6), coseps=cos(1e-6);
					rot.premul (Matrix (1,0,0, 0,coseps,sineps, 0,-sineps,coseps));
				}
				a[0] = atan2 (rot.m12, rot.m22);     // bank
				a[1] = asin  (rot.m32);              // pitch
				a[2] = atan2 (rot.m31, rot.m33);     // yaw
				q.Set (rot);
				} break;
			}
			if (fstatus == FLIGHTSTATUS_FREEFLIGHT) {
				bool finestep = ((g_pOrbiter->Cfg()->CfgRecPlayPrm.bSysInterval ? td.SysT1 - frec_att_last_syst : td.SimT1 - frec_att_last.simt) > att_step);
				alim = (finestep ? 1e-3 : 1e-2);
				// the difference between two quaternions may have to be
				// defined a bit more cleverly ...
				double dvx = q.qvx - frec_att_last.q.qvx;
				double dvy = q.qvy - frec_att_last.q.qvy;
				double dvz = q.qvz - frec_att_last.q.qvz;
				double ds  = q.qs  - frec_att_last.q.qs;
				double diff = sqrt (dvx*dvx + dvy*dvy + dvz*dvz + ds*ds);
				if (diff > alim) attforce = true;
			}
			if (attforce) {
				char cbuf[256];
				strcpy (cbuf, FRfname); strcpy (cbuf+strlen(cbuf)-3, "att");
				ofstream ofs (cbuf, ios::app);
				ofs << setprecision(10) << (td.SimT1-Tofs) << setprecision(6);
				for (i = 0; i < 3; i++)
					ofs << ' ' << (/*frec_att_last.att[i] =*/ a[i]);
				ofs << endl;
				frec_att_last.q.Set (q);
				frec_att_last_syst = td.SysT1;
				frec_att_last.simt = td.SimT1;
			}
		
		}
		if (ref != sp.ref) {
			char cbuf[256];
			strcpy (cbuf, FRfname); strcpy (cbuf+strlen(cbuf)-3, "att");
			ofstream ofs (cbuf, isfirst ? ios::trunc : ios::app);
			if (isfirst)
				ofs << "STARTMJD " << setprecision(12) << MJDofs << endl;
			switch (frec_att_last.frm) {
			case 0:
				ofs << "FRM ECLIPTIC" << endl;
				break;
			case 1:
				ofs << "REF " << sp.ref->Name() << endl;
				ofs << "FRM HORIZON" << endl;
				break;
			}
			frec_att_last.ref = ref = sp.ref;
		}
	}

	// engine attributes
	if (nfrec_eng != m_thruster.size()) {
		if (nfrec_eng) {
			delete []frec_eng;
			frec_eng = NULL;
		}
		if (m_thruster.size()) {
			frec_eng = new double[nfrec_eng = m_thruster.size()]; TRACENEW
			for (j = 0; j < m_thruster.size(); j++) frec_eng[j] = -1;
			frec_eng_simt = -1e10; // force output
		}
		else frec_eng = 0;
	}
	bool bfopen = false;
	dt = td.SimT1-frec_eng_simt;
	alim = min (0.2, 0.1/dt);
	ofstream ofs;
	for (j = 0; j < m_thruster.size(); j++) {
		if (fabs(frec_eng[j]-m_thruster[j]->level) > alim || force) {
			if (!bfopen) {
				frec_eng_simt = td.SimT1;
				char cbuf[256];
				strcpy (cbuf, FRfname); strcpy (cbuf+strlen(cbuf)-3, "atc");
				ofs.open (cbuf, isfirst ? ios::trunc : ios::app);
				ofs << setprecision(10) << (frec_eng_simt-Tofs) << " ENG";
				bfopen = true;
			}
			ofs << ' ' << j << ':' << setprecision(2) << (frec_eng[j] = m_thruster[j]->level);
		}
	}
	if (bfopen) {
		ofs << endl;
		ofs.close();
	}
}

// Save a vessel-specific event
void Vessel::FRecorder_SaveEvent (const char *event_type, const char *event)
{
	if (!bFRrecord) return;
	char cbuf[256];
	strcpy (cbuf, FRfname); strcpy (cbuf+strlen(cbuf)-3, "atc");
	ofstream ofs(cbuf, ios::app);
	ofs << setprecision(10) << (td.SimT1-Tofs) << ' ' << event_type << ' ' << event << endl;
}

void Vessel::FRecorder_SaveEventInt (const char *event_type, int event)
{
	FRecorder_SaveEvent(event_type, std::to_string(event).data());
}

void Vessel::FRecorder_SaveEventFloat (const char *event_type, double event)
{
	static char cbuf[128];
	sprintf (cbuf, "%f", event);
	FRecorder_SaveEvent (event_type, cbuf);
}

void Vessel::FRecorder_Clear ()
{
	if (nfrec) {
		delete []frec;
		frec = NULL;
		nfrec = 0;
	}
	if (nfrec_att) {
		delete []frec_att;
		frec_att = NULL;
		nfrec_att = 0;
	}
	if (nfrec_eng) {
		delete []frec_eng;
		frec_eng = NULL;
		nfrec_eng = 0;
	}
	if (FRfname) {
		delete []FRfname;
		FRfname = NULL;
	}
	if (FRatc_stream) {
		delete FRatc_stream;
		FRatc_stream = 0;
	}
	bFRplayback = false;
	bFRrecord = false;
}

bool Vessel::FRecorder_Read (const char *scname)
{
	int i;
	char fname[256], cbuf[256];

	for (i = strlen(scname)-1; i > 0; i--)
		if (scname[i-1] == '\\' || scname[i-1] == '/') break;
	int len = snprintf (fname, sizeof fname, "Flights/%s/%s.pos", scname+i, name.c_str()); // not upstream: bounded
	if (!ResolveInPlace (fname, sizeof fname, len)) // not upstream: case-insensitive path, resolved once for the .pos/.att/.atc streams; P: "" fails the open below
		LOGOUT_WARN ("Flight recorder: playback file name too long for record %s, vessel %s", scname+i, name.c_str()); // not upstream: one warning

	ifstream ifs (fname);
	if (!ifs) {
		bFRplayback = false;
		return false;
	}

	FRecorder_Clear();
	
	int nbuf = 0, nbuf_att = 0, frm = 0, crd = 0, attfrm = 0;
	double simt, x, y, z, vx, vy, vz;
	const CelestialBody *ref = g_psys->GetGravObj(0);

	// open position/velocity stream
	while (ifs.getline (cbuf, 256)) {
		if (!strncasecmp (cbuf, "REF", 3)) {
			ref = g_psys->GetGravObj (trim_string (cbuf+4), true);
			if (!ref) ref = g_psys->GetGravObj (0);
		} else if (!strncasecmp (cbuf, "FRM", 3)) {
			if (!strcasecmp (trim_string (cbuf+4), "EQUATORIAL")) frm = 1;
			else frm = 0;
		} else if (!strncasecmp (cbuf, "CRD", 3)) {
			if (!strcasecmp (trim_string (cbuf+4), "POLAR")) crd = 1;
			else crd = 0;
		} else if (!strncasecmp (cbuf, "STARTMJD", 8)) {
			sscanf (cbuf+9, "%lf", &MJDofs);
		} else {
			if (sscanf (cbuf, "%lf%lf%lf%lf%lf%lf%lf", &simt, &x, &y, &z, &vx, &vy, &vz) != 7)
				continue;
			if (crd == 1) { // map from polar coords
				double xz, r = x, phi = y, tht = z;
				double vr = vx, vphi = vy, vtht = vz;
				double sphi = sin(phi), cphi = cos(phi), stht = sin(tht), ctht = cos(tht);
				y = r*sin(tht); xz = r*cos(tht);
				x = xz*cos(phi); z = xz*sin(phi);
				vx = vr*cphi*ctht - r*vphi*sphi*ctht - r*vtht*cphi*stht;
				vy = vr*stht + r*vtht*ctht;
				vz = vr*sphi*ctht + r*vphi*cphi*ctht - r*vtht*sphi*stht;
				//vy = vr*sin(vtht); xz = vr*cos(vtht);
				//vx = xz*cos(vphi); vz = xz*sin(vphi);
			}
			if (nfrec == nbuf) { // re-allocate
				FRecord *tmp = new FRecord[nbuf += 1024]; TRACENEW
				if (nfrec) {
					memcpy (tmp, frec, nfrec*sizeof(FRecord));
					delete []frec;
				}
				frec = tmp;
			}
			frec[nfrec].simt = simt;
			frec[nfrec].frm  = frm;
			frec[nfrec].ref  = ref;
			frec[nfrec].rpos.Set (x, y, z);
			frec[nfrec].rvel.Set (vx, vy, vz);
			nfrec++;
		}
	}
	ifs.close();
	ifs.clear();
	cfrec = 0;
	cfrec_att = 0;

	// open attitude stream
	ref = g_psys->GetGravObj(0);
	strcpy (fname+strlen(fname)-3, "att");
	ifs.open (fname);
	while (ifs.getline (cbuf, 256)) {
		if (!strncasecmp (cbuf, "REF", 3)) {
			ref = g_psys->GetGravObj (trim_string (cbuf+4), true);
			if (!ref) ref = g_psys->GetGravObj (0);
		} else if (!strncasecmp (cbuf, "FRM", 3)) {
			if (!strcasecmp (trim_string (cbuf+4), "HORIZON")) attfrm = 1;
			else attfrm = 0;
		} else if (!strncasecmp (cbuf, "STARTMJD", 8)) {
			sscanf (cbuf+9, "%lf", &MJDofs);
			// assumes that MJDofs from all streams are the same!
		} else {
			double a[3];
			if (sscanf (cbuf, "%lf%lf%lf%lf", &simt, a+0, a+1, a+2) != 4) // not upstream: a line without its four values is skipped, as in the .pos reader (simt and a were used unset)
				continue; // not upstream: as above
			if (nfrec_att == nbuf_att) { // re-allocate
				FRecord_att *tmp = new FRecord_att[nbuf_att += 1024]; TRACENEW
				if (nfrec_att) {
					memcpy (tmp, frec_att, nfrec_att*sizeof(FRecord_att));
					delete []frec_att;
				}
				frec_att = tmp;
			}
			frec_att[nfrec_att].simt = simt;
			frec_att[nfrec_att].frm = attfrm;
			frec_att[nfrec_att].ref = ref;

			// convert Euler angles to quaternions
			Euler2Quaternion (a, frec_att[nfrec_att].q, frec_att[nfrec_att].frm);
			//for (int i = 0; i < 3; i++)
			//	frec_att[nfrec_att].att[i] = a[i];

			nfrec_att++;
		}
	}

	if (nfrec < 1) { // not upstream: no position sample (CheckEnd reads frec[nfrec-1]); as a missing file; Play reads frec_att only with 2 or more samples
		FRecorder_Clear ();
		bFRplayback = false;
		return false;
	}

	// open articulation event stream
	if (FRatc_stream) delete FRatc_stream;
	strcpy (cbuf, fname); strcpy (cbuf+strlen(cbuf)-3, "atc");
	FRatc_stream = new ifstream (cbuf); TRACENEW
	*FRatc_stream >> frec_eng_simt;
	if (!FRatc_stream->good()) {
		delete FRatc_stream;
		FRatc_stream = 0;
	}

	bFRplayback = true;
	return true;
}

void Vessel::FRecorder_Play ()
{
	dCHECK(s1, "Update state not available.")
	StateVectors *sv = s1;

	if (fstatus == FLIGHTSTATUS_FREEFLIGHT) {

		double dT, dt, w0, w1;
		double r0, r1, v0, v1, a0, b, lng, lat, rad, vref;
		int i;
		static Vector s;

		if (nfrec < 2) { // not upstream: one sample has nothing to interpolate (frec[1] was read unset); the vessel holds its state until CheckEnd ends the playback
			sv->Set (*s0); // not upstream: the vessel keeps its current state for this frame (s1 held the state from two frames back)
			return;
		}
		while (cfrec+2 < nfrec && frec[cfrec+1].simt < td.SimT1) cfrec++;
		dT = frec[cfrec+1].simt - frec[cfrec].simt;
		dt = td.SimT1 - frec[cfrec].simt;

		Vector P0 = frec[cfrec].rpos, P1 = frec[cfrec+1].rpos;
		Vector V0 = frec[cfrec].rvel, V1 = frec[cfrec+1].rvel;
		if (frec[cfrec].frm == 1) { // map from equatorial frame
			// propagate from current rotation state to rotation state at last sample
			double dlng = Pi2*dt/frec[cfrec].ref->RotT(), sind = sin(dlng), cosd = cos(dlng);
			s.x =  P0.x*cosd + P0.z*sind;
			s.z = -P0.x*sind + P0.z*cosd;
			s.y =  P0.y;
			P0.Set (mul (frec[cfrec].ref->s1->R, s));

			// Needs to be fixed!
			frec[cfrec].ref->LocalToEquatorial (s, lng, lat, rad);
			vref = Pi2/frec[cfrec].ref->RotT() * rad * cos(lat);
			s.x =  V0.x*cosd + V0.z*sind;
			s.z = -V0.x*sind + V0.z*cosd;
			s.y =  V0.y;
			V0.Set (mul (frec[cfrec].ref->s1->R, s + Vector (-vref*sin(lng),0,vref*cos(lng))));
		}
		if (frec[cfrec+1].frm == 1) { // map from equatorial frame
			double dlng = Pi2*(dt-dT)/frec[cfrec+1].ref->RotT(), sind = sin(dlng), cosd = cos(dlng);
			s.x =  P1.x*cosd + P1.z*sind;
			s.z = -P1.x*sind + P1.z*cosd;
			s.y =  P1.y;
			P1.Set (mul (frec[cfrec+1].ref->s1->R, s));
			frec[cfrec+1].ref->LocalToEquatorial (s, lng, lat, rad);
			vref = Pi2/frec[cfrec+1].ref->RotT() * rad * cos(lat);
			s.x =  V1.x*cosd + V1.z*sind;
			s.z = -V1.x*sind + V1.z*cosd;
			s.y =  V1.y;
			V1.Set (mul (frec[cfrec].ref->s1->R, s + Vector (-vref*sin(lng),0,vref*cos(lng))));
		}

		for (i = 0; i < 3; i++) {
			r0 = P0.data[i]; r1 = P1.data[i];
			v0 = V0.data[i]; v1 = V1.data[i];
			a0 = 2.0*(3.0*(r1-r0) - dT*(2.0*v0+v1)) / (dT*dT);
			b  = 6.0*(2.0*(r0-r1) + dT*(v0+v1)) / (dT*dT*dT);
			sv->vel.data[i] = v0 + a0*dt + 0.5*b*dt*dt;
			sv->pos.data[i] = r0 + v0*dt + 0.5*a0*dt*dt + b*dt*dt*dt/6.0;
		}

		sv->pos += frec[cfrec].ref->s1->pos;
		sv->vel += frec[cfrec].ref->s1->vel;
	
		// attitude
		if (nfrec_att > 1 && td.SimT1 < frec_att[nfrec_att-1].simt) { // not upstream: fewer than two attitude samples have nothing to interpolate (frec_att[1], or frec_att[-1] with none, was read unset); skipped as past the last sample

			// store old orientation for calculating angular velocities
			Vector r1 (sv->R.m11, sv->R.m21, sv->R.m31);
			Vector r2 (sv->R.m12, sv->R.m22, sv->R.m32);
			Vector r3 (sv->R.m13, sv->R.m23, sv->R.m33);

			while (cfrec_att+2 < nfrec_att && frec_att[cfrec_att+1].simt < td.SimT1) cfrec_att++;
			dt = frec_att[cfrec_att+1].simt - frec_att[cfrec_att].simt;
			w1 = (td.SimT1-frec_att[cfrec_att].simt)/dt;
			w0 = 1.0-w1;

			// Orientation at intermediate time point by interpolating endpoint quaternions
			if (frec_att[cfrec_att].frm == 0) {
				Quaternion Q;
				Q.interp (frec_att[cfrec_att].q, frec_att[cfrec_att+1].q, w1);
				sv->SetRot (Q);
			} else {
				Quaternion Q;
				Q.interp (frec_att[cfrec_att].q, frec_att[cfrec_att+1].q, w1);
				sv->R.Set (Q);
				double lng, lat, rad, slng, clng, slat, clat;
				Vector loc = tmul (frec_att[cfrec_att].ref->s1->R, sv->pos - frec_att[cfrec_att].ref->s1->pos);
				frec_att[cfrec_att].ref->LocalToEquatorial (loc, lng, lat, rad);
				slng = sin(lng), clng = cos(lng), slat = sin(lat), clat = cos(lat);
				sv->R.postmul (Matrix (-slng,      0,     clng,
					                    clat*clng, slat,  clat*slng,
									   -slat*clng, clat, -slat*slng));
				sv->R.tpostmul (frec_att[cfrec_att].ref->s1->R);
				//rrot.postmul (sp.Local2Hor());
				//rrot.tpostmul (cbody->GRot());
				sv->SetRot (transp (sv->R));
			}

			// recover angular velocities from change in rotation matrix
			// this may need more thought. The current algorithm may work
			// in a differential sense, but there should be something more
			// intelligent in the case of large changes in orientation
			Vector dx = tmul (sv->R,r1);
			Vector dy = tmul (sv->R,r2);
			Vector dz = tmul (sv->R,r3);
			sv->omega.x =  atan2 (dy.z, dz.z) * td.iSimDT;
			sv->omega.y = -atan2 (dx.z, dx.x) * td.iSimDT;
			sv->omega.z =  atan2 (dx.y, dx.x) * td.iSimDT;
		}
		el_valid = false;

		if (supervessel && supervessel->GetVessel(0) == this)
			supervessel->SetStateFromComponent (sv, 0);

	} // end freeflight
}

void Vessel::FRecorder_PlayEvent ()
{
	// articulation (also scanned when landed)
	while (FRatc_stream && td.SimT1 > frec_eng_simt) {
		char cbuf[1024], *s, *e, c;
		double lvl;
		int i;
		DWORD id;
		FRatc_stream->getline (cbuf, 1024);
		if (size_t n = strlen (cbuf); n && cbuf[n-1] == '\r') cbuf[n-1] = '\0'; // not upstream: CRLF files (Windows text mode dropped the CR)
		s = strtok (cbuf, " \t");
		if (s) {
			if (!strcasecmp (s, "ENG")) {
				while (s = strtok (NULL, " \t\n")) {
					if (sscanf (s, "%d%c%lf", &id, &c, &lvl) == 3 && c == ':') {
						if (id < m_thruster.size()) SetThrusterLevel_playback (m_thruster[id], lvl);
					} else {
						for (i = 0; i < NTHGROUP; i++)
							if (!strncasecmp (s, THGROUPSTR[i], strlen (THGROUPSTR[i]))) break;
						if (i < NTHGROUP && strlen (s) > strlen (THGROUPSTR[i]) && sscanf (s+strlen(THGROUPSTR[i])+1, "%lf", &lvl) == 1) { // not upstream: a group token needs its ':' value (the offset read the next token or past the line; EOF passed)
							ThrustGroupSpec* tgs = GetThrusterGroup((THGROUP_TYPE)i);
							if (tgs) {
								for (auto it = tgs->ts.begin(); it != tgs->ts.end(); it++)
									SetThrusterLevel_playback(*it, lvl);
							}
						}
					}
				}
			} else if (!strncasecmp (s, "LANDED", 6)) {
#ifdef UNDEF
				if (fstatus != FLIGHTSTATUS_LANDED) {
					Planet *p = g_psys->GetPlanet (s+7, true);
					if (supervessel) {
						double alt = supervessel->Altitude(); //rad - proxybody->Size();
						Matrix lrot (supervessel->s0->R);
						lrot.tpremul (p->s0->R);
						supervessel->InitLanded (p, supervessel->sp.lng, supervessel->sp.lat, supervessel->sp.dir, &lrot, alt);
					} else
						InitLanded (g_psys->GetPlanet (s+7, true), sp.lng, sp.lat, sp.dir);
				}
#endif
			} else if (!strncasecmp (s, "TAKEOFF", 7)) {
				if (fstatus == FLIGHTSTATUS_LANDED)
					bForceActive = true;
			} else if (!strncasecmp (s, "NAVMODE", 7)) {
				if (!strcmp (s+7, "CLR")) {
					if ((e = TokenValue (s, 11)) && sscanf (e, "%d", &i) == 1 && i >= 1 && i <= 7) // not upstream: a line without its mode, or with one outside 1..7, is skipped (i was used unset; 1 << (i-1) is undefined outside 1..32)
						ClrNavMode (i, false, true); // not upstream: as above
				} else {
					if ((e = TokenValue (s, 8)) && sscanf (e, "%d", &i) == 1 && i >= 0 && i <= 7) // not upstream: as above, 0 (clear all) to 7
						SetNavMode (i, true); // not upstream: as above
				}
			} else if (!strcasecmp (s, "RCSMODE")) {
				if ((e = TokenValue (s, 8)) && sscanf (e, "%d", &i) == 1 && i >= RCS_NONE && i <= RCS_LIN) // not upstream: a line without its mode, or with one SetAttMode would store unchecked (revmode[attmode] is [3]), is skipped
					SetAttMode (i, true); // not upstream: as above
			} else if (!strcasecmp (s, "ADCMODE")) {
				if ((e = TokenValue (s, 8)) && sscanf (e, "%d", &i) == 1 && i >= 0 && i <= 7) // not upstream: a line without its mode, or with one outside the SDK's bit flags 0..7, is skipped
					SetADCtrlMode (i, true); // not upstream: as above
			} else if (!strcasecmp (s, "UNDOCK")) {
				while (s = strtok (NULL, " \t\n")) {
					int dock;
					if (sscanf (s, "%d", &dock) == 1) // not upstream: a non-numeric token is skipped (dock was used unset)
						Undock (dock); // not upstream: as above
				}
			} else if (!strcasecmp (s, "DETACH")) {
				double v;
				int res = ((e = TokenValue (s, 7)) ? sscanf (e, "%d%lf", &id, &v) : 0); // not upstream: s+7 was past the line on a bare DETACH
				if (res < 2) v = 0.0;
				AttachmentSpec *as = (res >= 1 ? GetAttachmentFromIndex (false, id) : 0); // not upstream: a line without the id is skipped (id was used unset)
				if (as) DetachChild (as, v);
			} else if (!strcasecmp (s, "ATTACH")) {
				DWORD pidx, cidx;
				char cname[256] = "", modestr[32] = ""; // not upstream: cname [128]; a name of the line fits
				int res = ((e = TokenValue (s, 7)) ? sscanf (e, "%255s%d%d%31s", cname, &pidx, &cidx, modestr) : 0); // not upstream: widths; s+7 was past the line on a bare ATTACH
				Vessel *child = (res >= 3 ? g_psys->GetVessel (cname, true) : 0); // not upstream: a line without the name and both indices is skipped (they were used unset)
				bool loose = (res > 3 && !strcasecmp (modestr,"LOOSE") ? true : false);
				if (child) {
					AttachmentSpec *asp = GetAttachmentFromIndex (false, pidx);
					AttachmentSpec *asc = child->GetAttachmentFromIndex (true, cidx);
					if (asp && asc)
						AttachChild (child, asp, asc, loose);
				}
			} else if (!strncasecmp (s, "LIGHTSOURCE", 11)) { // light emitter event
				s = strtok (NULL, " \t\n");
				DWORD idx;
				if (s && sscanf (s, "%d", &idx) == 1 && idx < nemitter) { // not upstream: a bare LIGHTSOURCE line has no token
					s = strtok (NULL, " \t\n");
					if (s && !strcasecmp (s, "ACTIVATE")) { // not upstream: as above
						DWORD flag;
						if ((e = TokenValue (s, 9)) && sscanf (e, "%d", &flag) == 1) // not upstream: s+9 was past the line when ACTIVATE ended it
							emitter[idx]->Activate (flag != 0);
					}
				}
			} else if (!strncasecmp (s, "TACC", 4)) { // DEPRECATED - now stored in system stream
				int res = ((e = TokenValue (s, 5)) ? sscanf (e, "%lf%lf", &RecordingSpeed, &WarpDelay) : 0); // not upstream: s+5 was past the line on a bare TACC
				if (res == 1) // not upstream: the delay is optional; a line without the factor is skipped
					WarpDelay = 0.0;
				if (res >= 1 && g_pOrbiter->Cfg()->CfgRecPlayPrm.bReplayWarp) // not upstream: as above
						g_pOrbiter->SetWarpFactor (RecordingSpeed, true, WarpDelay);
			} else if (!strncasecmp (s, "CAMERA", 6)) { // DEPRECATED - now stored in system stream
				s = strtok (NULL, " \t\n");
				if (s && !strncasecmp (s, "PRESET", 6)) { // not upstream: a bare CAMERA line has no token
					if ((e = TokenValue (s, 7)) && sscanf (e, "%d", &i) == 1) // not upstream: a line without the index is skipped (i was used unset)
						g_camera->RecallPreset (i); // not upstream: as above
				}
			} else if (!strncasecmp (s, "NOTE", 4)) { // DEPRECATED - now stored in system stream
				oapi::ScreenAnnotation *sa = g_pOrbiter->SNotePB();
				if (sa) {
					if (!strcmp (s+4, "COL")) {
						double r, g, b;
						if ((e = TokenValue (s, 8)) && sscanf (e, "%lf%lf%lf", &r, &g, &b) == 3) { // not upstream: a line without its three values is skipped (they were used unset)
							VECTOR3 col = {r,g,b}; // not upstream: as above
							sa->SetColour (col); // not upstream: as above
						} // not upstream: as above
					} else if (!strcmp (s+4, "SIZE")) {
						double scale;
						if ((e = TokenValue (s, 9)) && sscanf (e, "%lf", &scale) == 1) // not upstream: a line without its value is skipped
							sa->SetSize (scale); // not upstream: as above
					} else if (!strcmp (s+4, "POS")) {
						double x1, y1, x2, y2;
						if ((e = TokenValue (s, 8)) && sscanf (e, "%lf%lf%lf%lf", &x1, &y1, &x2, &y2) == 4) // not upstream: a line without its four values is skipped
							sa->SetPosition (x1, y1, x2, y2); // not upstream: as above
					} else if (!strcmp (s+4, "OFF")) {
						sa->ClearText();
					} else {
						e = TokenValue (s, 5); // not upstream: s+5 was past the line on a bare NOTE
						sa->SetText (e ? e : (char*)""); // not upstream: an emptied note plays as SetText (""), as upstream
					}
				}
			} else if (modIntf.v->Version() >= 1) { // pass event to vessel
				e = strtok (NULL, ""); // not upstream: the rest of the line; s+strlen(s)+1 was past the line when the tag ended it
				if (!e) e = (char*)""; // not upstream: an add-on event with an empty payload is still delivered
				//e = strtok (NULL, " \t");
				((VESSEL2*)modIntf.v)->clbkPlaybackEvent (td.SimT1, frec_eng_simt, s, e);
			}
		}
		*FRatc_stream >> frec_eng_simt;
		if (!FRatc_stream->good()) {
			delete FRatc_stream;
			FRatc_stream = 0;
		}
	}

	FRecorder_CheckEnd ();
}


void Vessel::FRecorder_CheckEnd ()
{
	if (td.SimT1 > frec[nfrec-1].simt) { // reached end of playback list
		g_pOrbiter->EndPlayback();
		//FRecorder_EndPlayback();
		//g_pOrbiter->SNote()->ClearNote();

		// TEMPORARY
		//g_pOrbiter->ToggleRecorder (false, true);
	}
}

void Vessel::FRecorder_EndPlayback ()
{
	if (bFRplayback) {
		bFRplayback = false;
		Amom_add.Set(0,0,0);
		rvel_base.Set (s0->vel);
		rvel_add.Set(0,0,0);
		rpos_base.Set (s0->pos);
		rpos_add.Set(0,0,0);
		s0->Q.Set (s0->R);
		if (supervessel && supervessel->GetVessel(0) == this)
			supervessel->FRecorder_EndPlayback();
	}
}

// ================================================================
// System event recording/playback
// (implemented in class Orbiter)
// ================================================================

void Orbiter::FRecorder_Reset ()
{
	FRsysname = 0;
	FRsys_stream = 0;
	FReditor = 0;
	frec_sys_simt = -1e10;
	bRecord = bPlayback = false;
}

bool Orbiter::FRecorder_PrepareDir (const char *fname, bool force)
{
	fs::path dir = fs::path(oapiResolvePath((std::string("Flights/") + fname).c_str()));
	std::error_code ec;
	auto status = fs::status(dir, ec);

	// don't overwrite existing recording
	if (!ec) {
		if(fs::is_directory(status) && !force) return false;
		fs::remove_all(dir);
	}
	fs::create_directory(dir);

	return true;
}

void Orbiter::FRecorder_Activate (bool active, const char *fname, bool append)
{
	if (bRecord == active) return; // nothing to do
	if (active) {
		if (!append) FRecorder_Reset();
		bRecord = true;
		char cbuf[256];
		int len = snprintf (cbuf, sizeof cbuf, "Flights\\%s\\system.dat", fname); // not upstream: bounded
		if (!ResolveInPlace (cbuf, sizeof cbuf, len)) { // not upstream: '\' separators and case resolved once; N: a record name that doesn't fit isn't recorded
			LOGOUT_WARN ("Flight recorder: record name too long, not recorded: %s", fname);
			bRecord = false;
			return;
		}
		if (FRsysname) delete []FRsysname;
		FRsysname = new char[strlen(cbuf)+1]; TRACENEW
		strcpy (FRsysname, cbuf);
	} else {
		bRecord = false;
	}
}

// Save a system event
void Orbiter::FRecorder_SaveEvent (const char *event_type, const char *event)
{
	if (!bRecord) return;
	ofstream ofs(FRsysname, ios::app);
	ofs << setprecision(10) << (td.SimT1-Tofs) << ' ' << event_type << ' ' << event << endl;
}

void Orbiter::FRecorder_OpenPlayback (const char *scname)
{
	int i;
	char cbuf[256];

	if (FRsys_stream) delete FRsys_stream;

	for (i = strlen(scname)-1; i > 0; i--)
		if (scname[i-1] == '\\' || scname[i-1] == '/') break;
	int len = snprintf (cbuf, sizeof cbuf, "Flights\\%s\\system.dat", scname+i); // not upstream: bounded
	if (!ResolveInPlace (cbuf, sizeof cbuf, len)) // not upstream: '\' separators and case resolved once; P: "" fails the open below
		LOGOUT_WARN ("Flight recorder: playback record name too long: %s", scname+i); // not upstream: one warning
	if (FRsysname) delete []FRsysname;
	FRsysname = new char[strlen(cbuf)+1]; TRACENEW
	strcpy (FRsysname, cbuf);

	FRsys_stream = new ifstream (cbuf); TRACENEW
	*FRsys_stream >> frec_sys_simt;
	if (!FRsys_stream->good()) {
		delete FRsys_stream;
		FRsys_stream = 0;
	}
}

void Orbiter::FRecorder_SuspendPlayback ()
{
	if (FRsys_stream) {
		delete FRsys_stream;
		FRsys_stream = 0;
	}
}

void Orbiter::FRecorder_RescanPlayback ()
{
	oapi::ScreenAnnotation *sa = SNotePB();
	if (sa) sa->Reset();
	FRsys_stream = new ifstream (FRsysname); TRACENEW
	*FRsys_stream >> frec_sys_simt;
	FRecorder_Play(); // read up to current playback time
}

void Orbiter::FRecorder_ClosePlayback ()
{
	if (FRsys_stream) {
		delete FRsys_stream;
		FRsys_stream = 0;
	}
	// not upstream: FReditor delete left out, PlaybackEditor is never defined or created (DlgPlaybackEditor replaced it)
}

void Orbiter::FRecorder_Play ()
{
	// scan system event stream
	while (FRsys_stream && td.SimT1 > frec_sys_simt) {
		char cbuf[1024], *s, *e; // not upstream: e, a token's value
		int i;
		FRsys_stream->getline (cbuf, 1024);
		if (size_t n = strlen (cbuf); n && cbuf[n-1] == '\r') cbuf[n-1] = '\0'; // not upstream: CRLF files (Windows text mode dropped the CR)
		s = strtok (cbuf, " \t");
		if (s) {
			if (!strncasecmp (s, "TACC", 4)) {
				int res = ((e = TokenValue (s, 5)) ? sscanf (e, "%lf%lf", &RecordingSpeed, &WarpDelay) : 0); // not upstream: s+5 was past the line on a bare TACC
				if (res == 1) // not upstream: the delay is optional; a line without the factor is skipped
					WarpDelay = 0.0;
				if (res >= 1 && Cfg()->CfgRecPlayPrm.bReplayWarp) // not upstream: as above
						SetWarpFactor (RecordingSpeed, true, WarpDelay);
			} else if (!strncasecmp (s, "CAMERA", 6)) {
				s = strtok (NULL, " \t\n");
				if (s && !strncasecmp (s, "PRESET", 6)) { // not upstream: a bare CAMERA line has no token
					if ((e = TokenValue (s, 7)) && sscanf (e, "%d", &i) == 1) // not upstream: a line without the index is skipped (i was used unset)
						g_camera->RecallPreset (i); // not upstream: as above
				} else if (s && !strncasecmp (s, "SET", 3)) { // not upstream: as above
					CameraMode *cm = ((e = TokenValue (s, 4)) ? CameraMode::Create (e) : 0); // not upstream: s+4 was past the line when SET ended it
					if (cm) g_camera->SetCMode (cm);
					delete cm;
				}
			} else if (!strncasecmp (s, "FOCUS", 5)) {
				s = strtok (NULL, " \t\n");
				if (s) vfocus = g_psys->GetVessel (s, true); // not upstream: a bare FOCUS line is skipped (GetVessel (NULL) crashed)
				if (s && vfocus && Cfg()->CfgRecPlayPrm.bReplayFocus) // not upstream: as above
					g_pOrbiter->SetFocusObject (vfocus);
			} else if (!strncasecmp (s, "NOTE", 4)) {
				oapi::ScreenAnnotation *sa = SNotePB();
				if (sa) {
					if (!strcmp (s+4, "COL")) {
						double r, g, b;
						if ((e = TokenValue (s, 8)) && sscanf (e, "%lf%lf%lf", &r, &g, &b) == 3) { // not upstream: a line without its three values is skipped (they were used unset)
							VECTOR3 col = {r,g,b}; // not upstream: as above
							sa->SetColour (col); // not upstream: as above
						} // not upstream: as above
					} else if (!strcmp (s+4, "SIZE")) {
						double scale;
						if ((e = TokenValue (s, 9)) && sscanf (e, "%lf", &scale) == 1) // not upstream: a line without its value is skipped
							sa->SetSize (scale); // not upstream: as above
					} else if (!strcmp (s+4, "POS")) {
						double x1, y1, x2, y2;
						if ((e = TokenValue (s, 8)) && sscanf (e, "%lf%lf%lf%lf", &x1, &y1, &x2, &y2) == 4) // not upstream: a line without its four values is skipped
							sa->SetPosition (x1, y1, x2, y2); // not upstream: as above
					} else if (!strcmp (s+4, "OFF")) {
						sa->ClearText();
					} else {
						e = TokenValue (s, 5); // not upstream: s+5 was past the line on a bare NOTE
						sa->SetText (e ? e : (char*)""); // not upstream: an emptied note plays as SetText (""), as upstream
					}
				}
			} else if (!strncasecmp (s, "JUMPTOTIME", 10)) {
				double jumptime;
				if ((e = TokenValue (s, 11)) && sscanf (e, "%lf", &jumptime) == 1 && jumptime > td.SimT0) { // not upstream: s+11 was past the line on a bare JUMPTOTIME; EOF passed with jumptime unset
					double tgtmjd = td.MJD0 + (jumptime-td.SimT0)/86400.0;
					g_pOrbiter->Timejump(tgtmjd, PROP_ORBITAL_FIXEDSURF);
				}
			} else if (!strncasecmp (s, "ENDSESSION", 10)) {
				if (hRenderWnd) QMetaObject::invokeMethod (hRenderWnd, "close", Qt::QueuedConnection); // PostMessage WM_CLOSE
			}
		}
		*FRsys_stream >> frec_sys_simt; // read time for next event
		if (!FRsys_stream->good()) {    // end of stream
			delete FRsys_stream;
			FRsys_stream = 0;
		}
	}
}

void Orbiter::FRecorder_ToggleEditor ()
{	
	DlgPlaybackEditor *m_DlgPlaybackEditor = g_pOrbiter->DlgMgr()->EnsureEntry<DlgPlaybackEditor>();
	const char *playbackdir = pState->PlaybackDir();
	m_DlgPlaybackEditor->Load(playbackdir);
}

// ================================================================
// helper functions

// convert Euler angles from given reference frame to quaternion
void Euler2Quaternion (double *a, Quaternion &q, int frm)
{
	double sinx = sin(a[0]), cosx = cos(a[0]);
	double siny = sin(a[1]), cosy = cos(a[1]);
	double sinz = sin(a[2]), cosz = cos(a[2]);
	if (frm == 0) { // global frame
		Matrix R (1,0,0,  0,cosx,sinx,  0,-sinx,cosx);
		R.postmul (Matrix (cosy,0,-siny,  0,1,0,  siny,0,cosy));
		R.postmul (Matrix (cosz,sinz,0,  -sinz,cosz,0,  0,0,1));
		q.Set (R);
	} else {        // local horizon frame
		Matrix R (cosx,sinx,0,  -sinx,cosx,0,  0,0,1);
		R.postmul (Matrix (1,0,0,  0,cosy,-siny,  0,siny,cosy));
		R.postmul (Matrix (cosz,0,-sinz,  0,1,0,  sinz,0,cosz));
		q.Set (R);
	}
}

