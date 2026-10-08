// not upstream: collision addon, base-object parsers and geometry copied from Src/Orbiter/Baseobj.cpp for the building colliders (design E2 10.2)
// Copyright (c) Martin Schweiger
// Licensed under the MIT License
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <strings.h>
#include "CollBaseA.h"
#include "DentMath.h"

using namespace std;

namespace collbo {
typedef uint32_t DWORD; typedef uint16_t WORD; typedef int64_t LONGLONG;
typedef CollVtx NTVERTEX; // same layout
#define TRACENEW
const double RAD = 3.14159265358979323846 / 180.0;
#define OBJSPEC_EXPORTMESH         0x0001
#define OBJSPEC_EXPORTVERTEX       0x0002
#define OBJSPEC_RENDERSHADOW       0x0020
#define OBJSPEC_UPDATEVERTEX       0x0040
#define OBJSPEC_LPAD               0x0080
#define OBJSPEC_RWY                0x0100
#define OBJSPEC_UNDERSHADOW        0x0200
#define OBJSPEC_EXPORTSHADOWMESH   0x0400
#define OBJSPEC_OWNSHADOW          0x0800
#define OBJSPEC_WRAPTOSURFACE      0x1000
class Base {};
static LONGLONG NameToId (const char *) { return 0; } // textures do not matter for colliders

// trim_string (Config.cpp): cut the comment, strip trailing and skip leading white space
static char *trim_string (char *cbuf)
{
	char *c;
	for (c = cbuf; *c && *c != ';'; c++);
	*c = '\0';
	for (--c; c >= cbuf; c--) {
		if (*c == ' ' || *c == '\t' || *c == '\r') *c = '\0';
		else break;
	}
	for (c = cbuf; *c; c++)
		if (*c != ' ' && *c != '\t') return c;
	return c;
}

class BaseObject {
public:
	BaseObject (const Base *_base): base (_base) { relpos.x = relpos.y = relpos.z = 0.0; scale.x = scale.y = scale.z = 1.0; rot = 0.0; elev = yofs = 0.0; }
	virtual ~BaseObject () = default;
	virtual int Read (std::istream &is);
	virtual int ParseLine (const char *label, const char *value) { (void)label; (void)value; return 0; }
	virtual DWORD GetSpecs () const { return 0; }
	virtual void Activate () {}
	virtual void Deactivate () {}
	int nGroup () { return ngrp; }
	virtual bool GetGroupSpec (int, DWORD &, DWORD &, LONGLONG &, bool &, bool &) { return false; }
	virtual void ExportGroup (int, NTVERTEX *, WORD *, DWORD &) {}
	void ParseError (const char *msg) const { if (err) err->push_back (msg); }
	const Base *base;
	int ngrp = 0;
	Vector relpos, equpos, scale;
	double rot, elev, yofs;
	std::vector<std::string> *err = nullptr;
	bool noCollide = false, collide = false; std::string collMat; // not upstream: NOCOLLIDE, COLLIDE, COLLMAT
};

// BaseObject::Read (Baseobj.cpp:94-133) with the collider keys read before ParseLine
int BaseObject::Read (istream &is)
{
	char cbuf[256], label[256] = "", *value, *cp;
	int r, res = 0;
	do {
		if (!is.getline (cbuf, 256)) return 1;
		cp = trim_string (cbuf);
		if (!*cp) continue;
		sscanf (cp, "%s", label);
		value = trim_string (cp + strlen (label));
		if (!strcasecmp (label, "POS")) {
			if (sscanf (value, "%lf%lf%lf", &relpos.x, &relpos.y, &relpos.z) != 3) { ParseError ("POS: expected 3 scalar values"); res = 2; }
		} else if (!strcasecmp (label, "SCALE")) {
			int nv = sscanf (value, "%lf%lf%lf", &scale.x, &scale.y, &scale.z);
			if (nv < 3) {
				if (nv == 1) scale.y = scale.z = scale.x;
				else { ParseError ("SCALE: expected 1 or 3 scalar values"); res = 2; }
			}
		} else if (!strcasecmp (label, "ROT")) {
			if (sscanf (value, "%lf", &rot) != 1) { ParseError ("ROT: expected a scalar value"); res = 2; }
			else rot *= RAD;
		} else if (!strcasecmp (label, "NOCOLLIDE")) {
			noCollide = true;
		} else if (!strcasecmp (label, "COLLIDE")) {
			collide = true;
		} else if (!strcasecmp (label, "COLLMAT")) {
			char m[256] = "";
			if (sscanf (value, "%255s", m) == 1) collMat = m;
		} else {
			r = ParseLine (label, value);
			if (!res) res = r;
		}
	} while (strcasecmp (label, "END"));
	return res;
}

// MeshObject parse part (Baseobj.cpp:187-250); geometry through the own parser
class MeshObject: public BaseObject {
public:
	MeshObject (const Base *b): BaseObject (b) {}
	int ParseLine (const char *label, const char *value) {
		if (!strcasecmp (label, "FILE")) fname = value;
		else if (!strcasecmp (label, "WRAPTOSURFACE")) specs |= OBJSPEC_WRAPTOSURFACE;
		else if (!strcasecmp (label, "SHADOW")) specs |= OBJSPEC_RENDERSHADOW;
		else if (!strcasecmp (label, "OWNSHADOW")) specs |= OBJSPEC_OWNSHADOW;
		else if (!strcasecmp (label, "UNDERSHADOWS")) undersh = true;
		else if (!strcasecmp (label, "LPAD")) specs |= OBJSPEC_LPAD;
		else if (!strcasecmp (label, "OWNMATERIAL")) ownmat = true;
		return 0;
	}
	int Read (istream &is) {
		fname.clear (); specs = 0; ownmat = undersh = false;
		BaseObject::Read (is);
		if (fname.empty ()) return 2;
		specs |= ownmat ? OBJSPEC_EXPORTMESH : OBJSPEC_EXPORTVERTEX;
		if (undersh) specs |= OBJSPEC_UNDERSHADOW;
		return 0;
	}
	DWORD GetSpecs () const { return specs; }
	DWORD specs = 0; std::string fname; bool ownmat = false, undersh = false;
};

// objects with their own Read or no export: read to END, fixed specs
class OtherObject: public BaseObject {
public:
	OtherObject (const Base *b, DWORD s): BaseObject (b), specs (s) {}
	DWORD GetSpecs () const { return specs; }
	DWORD specs;
};

struct FVEC3 { float x = 0, y = 0, z = 0; }; // oapi::FVECTOR3 fields as the per-type Read fills them

// Lpad01/02/02a::ParseLine (Baseobj.cpp:1625-1637, :1724-1736, :1842-1854): TEX, NAV
class Lpad: public OtherObject {
public:
	Lpad (const Base *b, const char *n): OtherObject (b, OBJSPEC_EXPORTVERTEX | OBJSPEC_LPAD), nm (n) {}
	int ParseLine (const char *label, const char *value) {
		int res = 0;
		if (!strcasecmp (label, "TEX")) {
			texid = NameToId (value);
		} else if (!strcasecmp (label, "NAV")) {
			if (sscanf (value, "%f", &ILSfreq) != 1) {
				ParseError ((std::string (nm) + ": NAV: expected scalar value").c_str ());
				res = 2;
			}
		}
		return res;
	}
	const char *nm; LONGLONG texid = 0; float ILSfreq = 0;
};

// Runway::Read (Baseobj.cpp:1964-2020); the segment table is not kept
class Runway: public OtherObject {
public:
	Runway (const Base *b): OtherObject (b, OBJSPEC_EXPORTVERTEX | OBJSPEC_RWY) {}
	int Read (istream &is) {
		char cbuf[256], *cp, label[256] = "";
		int i;
		do {
			if (!is.getline (cbuf, 256)) return 1;
			cp = trim_string (cbuf);
			sscanf (cp, "%s", label);
			if (!strcasecmp (label, "END1"))
				sscanf (cp+4, "%f%f%f", &end1.x, &end1.y, &end1.z);
			else if (!strcasecmp (label, "END2"))
				sscanf (cp+4, "%f%f%f", &end2.x, &end2.y, &end2.z);
			else if (!strcasecmp (label, "WIDTH")) {
				sscanf (cp+5, "%f", &width);
				width *= 0.5f;
			} else if (!strncasecmp (label, "ILS", 3)) {
				float freq;
				if (sscanf (cp+3, "%d%f", &i, &freq) == 2 && i >= 1 && i <= 2)
					ILSfreq[i-1] = freq;
			} else if (!strcasecmp (label, "NRWSEG")) {
				sscanf (cp+6, "%d", &nrwseg);
			} else if (!strncasecmp (label, "RWSEG", 5)) {
				float seglen, tu0, tu1, tv0, tv1;
				int subseg;
				sscanf (cp+5, "%d%d%f%f%f%f%f", &i, &subseg, &seglen, &tu0, &tu1, &tv0, &tv1);
			} else if (!strcasecmp (label, "RWTEX")) {
				sscanf (cp+5, "%s", label);
				texid = NameToId (label);
			}
		} while (strcasecmp (label, "END"));
		return 0;
	}
	FVEC3 end1, end2; float ILSfreq[2] = { 0, 0 }, width = 0; int nrwseg = 0; LONGLONG texid = 0;
};

// RunwayLights::Read (Baseobj.cpp:2121-2156)
class RunwayLights: public OtherObject {
public:
	RunwayLights (const Base *b): OtherObject (b, 0) {}
	int Read (istream &is) {
		char cbuf[256], *cp, label[256] = "";
		do {
			if (!is.getline (cbuf, 256)) return 1;
			cp = trim_string (cbuf);
			sscanf (cp, "%s", label);
			if (!strcasecmp (label, "END1"))
				sscanf (cp+4, "%f%f%f", &end1.x, &end1.y, &end1.z);
			else if (!strcasecmp (label, "END2"))
				sscanf (cp+4, "%f%f%f", &end2.x, &end2.y, &end2.z);
			else if (!strcasecmp (label, "COUNT1"))
				sscanf (cp+6, "%d", &count1);
			else if (!strcasecmp (label, "WIDTH")) {
				sscanf (cp+5, "%f", &width);
				width *= 0.5f;
			} else if (!strcasecmp (label, "PAPI")) {
				float p[3];
				sscanf (cp+4, "%f%f%f", p, p+1, p+2);
			} else if (!strcasecmp (label, "VASI")) {
				float v[3];
				sscanf (cp+4, "%f%f%f", v, v+1, v+2);
			}
		} while (strcasecmp (label, "END"));
		return 0;
	}
	FVEC3 end1, end2; int count1 = 0; float width = 0;
};

// BeaconArray::Read (Baseobj.cpp:2452-2471)
class BeaconArray: public OtherObject {
public:
	BeaconArray (const Base *b): OtherObject (b, 0) {}
	int Read (istream &is) {
		char cbuf[256], *cp, label[256] = "";
		do {
			if (!is.getline (cbuf, 256)) return 1;
			cp = trim_string (cbuf);
			sscanf (cp, "%s", label);
			if (!strcasecmp (label, "END1"))
				sscanf (cp+4, "%f%f%f", &end1.x, &end1.y, &end1.z);
			else if (!strcasecmp (label, "END2"))
				sscanf (cp+4, "%f%f%f", &end2.x, &end2.y, &end2.z);
			else if (!strcasecmp (label, "COUNT"))
				sscanf (cp+5, "%d", &count);
			else if (!strcasecmp (label, "SIZE"))
				sscanf (cp+4, "%lf", &size);
			else if (!strcasecmp (label, "COL"))
				sscanf (cp+3, "%f%f%f", &col_r, &col_g, &col_b);
		} while (strcasecmp (label, "END"));
		return 0;
	}
	FVEC3 end1, end2; int count = 0; double size = 0; float col_r = 0, col_g = 0, col_b = 0;
};

// Train1::Read and Train2::Read (Baseobj.cpp:2714-2738, :2989-3015); Init left out (no geometry)
class Train: public OtherObject {
public:
	Train (const Base *b, bool t2): OtherObject (b, OBJSPEC_EXPORTVERTEX | OBJSPEC_UPDATEVERTEX), two (t2) {}
	int Read (istream &is) {
		char cbuf[256], *cp, label[256] = "";
		do {
			if (!is.getline (cbuf, 256)) return 1;
			cp = trim_string (cbuf);
			sscanf (cp, "%s", label);
			if (!strcasecmp (label, "END1"))
				sscanf (cp+4, "%f%f%f", &end1.x, &end1.y, &end1.z);
			else if (!strcasecmp (label, "END2"))
				sscanf (cp+4, "%f%f%f", &end2.x, &end2.y, &end2.z);
			else if (two && !strcasecmp (label, "HEIGHT"))
				sscanf (cp+6, "%f", &height);
			else if (!strcasecmp (label, "MAXSPEED"))
				sscanf (cp+8, "%f", &maxspeed);
			else if (!strcasecmp (label, "SLOWZONE"))
				sscanf (cp+8, "%f", &slowzone);
			else if (!strcasecmp (label, "TEX")) {
				sscanf (cp+3, "%s%f", label, &tuscale_track);
				texid = NameToId (label);
			}
		} while (strcasecmp (label, "END"));
		return 0;
	}
	bool two; FVEC3 end1, end2; float height = 0, maxspeed = 0, slowzone = 0, tuscale_track = 0; LONGLONG texid = 0;
};

// SolarPlant::Read (Baseobj.cpp:3279-3313)
class SolarPlant: public OtherObject {
public:
	SolarPlant (const Base *b): OtherObject (b, 0) {}
	int Read (istream &is) {
		char cbuf[256], *cp, label[256] = "";
		do {
			if (!is.getline (cbuf, 256)) return 1;
			cp = trim_string (cbuf);
			sscanf (cp, "%s", label);
			if (!strcasecmp (label, "POS"))
				sscanf (cp+3, "%f%f%f", &pos.x, &pos.y, &pos.z);
			else if (!strcasecmp (label, "SCALE"))
				sscanf (cp+5, "%f", &fscale);
			else if (!strcasecmp (label, "SPACING"))
				sscanf (cp+7, "%f%f", &sepx, &sepz);
			else if (!strcasecmp (label, "GRID")) {
				int nr, nc;
				if (sscanf (cp+4, "%d%d", &nr, &nc) == 2 && nr >= 1 && nc >= 1 && (long long)nr*nc*21 <= INT32_MAX)
					nrow = nr, ncol = nc;
			}
			else if (!strcasecmp (label, "ROT")) {
				sscanf (cp+3, "%f", &frot);
				frot *= (float)RAD;
			} else if (!strcasecmp (label, "TEX")) {
				float su, sv;
				sscanf (cp+3, "%s%f%f", label, &su, &sv);
				texid = NameToId (label);
			}
		} while (strcasecmp (label, "END"));
		return 0;
	}
	FVEC3 pos; float fscale = 1, sepx = 0, sepz = 0, frot = 0; int nrow = 2, ncol = 2; LONGLONG texid = 0;
};

class Block: public BaseObject {
public:
	Block (const Base *_base);
	~Block ();
	int ParseLine (const char *label, const char *value);
	DWORD GetSpecs() const { return OBJSPEC_EXPORTVERTEX | OBJSPEC_EXPORTSHADOWMESH; }
	bool GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &texid,
		bool &undershadow, bool &groundshadow);
	void ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs);
	void Activate ();
	void Deactivate ();

private:
	LONGLONG  texid[3];   // texture ids
	float  tuscale[3], tvscale[3]; // texture scaling factors
	struct DYNDATA {
		float *databuf;    // some geometry data
	} *dyndata;               // lives only during activation
};

class Hangar: public BaseObject {
public:
	Hangar (const Base *_base);
	~Hangar ();
	int ParseLine (const char *label, const char *value);
	DWORD GetSpecs() const { return OBJSPEC_EXPORTVERTEX | OBJSPEC_EXPORTSHADOWMESH; }
	bool GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &texid,
		bool &undershadow, bool &groundshadow);
	void ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs);
	void Activate ();
	void Deactivate ();

private:
	LONGLONG  texid[3];   // texture ids
	float  tuscale[3], tvscale[3]; // texture scaling factors
	struct DYNDATA {
		NTVERTEX *Vtx;       // block vertices
	} *dyndata;
};

class Hangar2: public BaseObject {
public:
	Hangar2 (const Base *_base);
	~Hangar2 ();
	int ParseLine (const char *label, const char *value);
	DWORD GetSpecs() const { return OBJSPEC_EXPORTVERTEX | OBJSPEC_EXPORTSHADOWMESH; }
	bool GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &texid,
		bool &undershadow, bool &groundshadow);
	void ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs);
	void Activate ();
	void Deactivate ();
	float RoofH () const { return roofh; } void SetRoofH (float h) { roofh = h; } // not upstream: CollBaseObjDef keeps ROOFH

private:
	float  roofh;      // roof height from base to ridge
	LONGLONG  texid[3];   // texture ids
	float  tuscale[3], tvscale[3]; // texture scaling factors
	struct DYNDATA {
		NTVERTEX *Vtx;      // block vertices
	} *dyndata;
};

class Hangar3: public BaseObject {
public:
	Hangar3 (const Base *_base);
	~Hangar3 ();
	int ParseLine (const char *label, const char *value);
	DWORD GetSpecs() const { return OBJSPEC_EXPORTVERTEX | OBJSPEC_EXPORTSHADOWMESH; }
	bool GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &texid,
		bool &undershadow, bool &groundshadow);
	void ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs);
	void Activate ();
	void Deactivate ();

private:
	LONGLONG  texid[3];   // texture ids
	float  tuscale[3], tvscale[3]; // texture scaling factors
	struct DYNDATA {
		NTVERTEX *Vtx;      // block vertices
	} *dyndata;
};

class Tank: public BaseObject {
public:
	Tank (const Base *_base);
	int ParseLine (const char *label, const char *value);
	DWORD GetSpecs() const { return OBJSPEC_EXPORTVERTEX | OBJSPEC_EXPORTSHADOWMESH; }
	bool GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &texid,
		bool &undershadow, bool &groundshadow);
	void ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs);
	void Activate ();
	void Deactivate ();
	DWORD NStep () const { return nstep; } void SetNStep (DWORD n) { nstep = n; } // not upstream: CollBaseObjDef keeps NSTEP

private:
	LONGLONG  texid[2];   // texture ids
	float  tuscale[2], tvscale[2]; // texture scaling factors (mantle and top)
	DWORD     nstep;      // segments for circle
	struct DYNDATA {
		NTVERTEX *Vtx;      // block vertices
	} *dyndata;
};

// ---- copied from Src/Orbiter/Baseobj.cpp:377-1546 (render and shadow functions left out)

Block::Block (const Base *_base): BaseObject (_base)
{
	ngrp = 3; // 3 mesh groups: x-walls, z-walls and roof
	for (int i = 0; i < 3; i++) {
		texid[i] = 0;
		tuscale[i] = tvscale[i] = 1.0f;
	}
	dyndata = 0;
}

Block::~Block ()
{
	Deactivate();
}

int Block::ParseLine (const char *label, const char *value)
{
	int res = 0;
	if (!strncasecmp (label, "TEX", 3)) {
		float su, sv;
		int i;
		char name[256]; // not upstream: texture names have no length limit, the line is 255
		if (sscanf (label+3, "%d", &i) != 1 || i < 1 || i > 3) {
			ParseError("Block: TEXn: Expected integer value 1-3 for n");
			res = 2;
		}
		if (sscanf (value, "%s%f%f", name, &su, &sv) != 3) {
			ParseError("Block: TEXn: expected 3 values (*char, scalar, scalar)");
			res = 2;
		} 
		if (!res) { // not upstream: only with n in range and all three values read
			texid[i-1] = NameToId (name);
			tuscale[i-1] = su;
			tvscale[i-1] = sv;
		} // not upstream: end of the TEXn guard
	}
	return res;
}

void Block::Activate ()
{
	if (dyndata) return;  // active already
	dyndata = new struct DYNDATA; TRACENEW
	dyndata->databuf = new float[13]; TRACENEW
	float dx = 0.5f*scale.x, dy = scale.y, dz = 0.5f*scale.z;
	float srot = (float)sin(rot), crot = (float)cos(rot);
	float dxcrot = dx*crot, dxsrot = dx*srot;
	float dzsrot = dz*srot, dzcrot = dz*crot;
	dyndata->databuf[0] =  dxcrot + dzsrot + relpos.x;
	dyndata->databuf[1] = -dxcrot + dzsrot + relpos.x;
	dyndata->databuf[2] = -dxcrot - dzsrot + relpos.x;
	dyndata->databuf[3] =  dxcrot - dzsrot + relpos.x;
	dyndata->databuf[4] =  relpos.y;
	dyndata->databuf[5] =  relpos.y + dy;
	dyndata->databuf[6] =  dxsrot - dzcrot + relpos.z;
	dyndata->databuf[7] = -dxsrot - dzcrot + relpos.z;
	dyndata->databuf[8] = -dxsrot + dzcrot + relpos.z;
	dyndata->databuf[9] =  dxsrot + dzcrot + relpos.z;
	dyndata->databuf[10] = srot;
	dyndata->databuf[11] = crot;
	dyndata->databuf[12] = yofs;
}

void Block::Deactivate ()
{
	if (dyndata) {
		delete []dyndata->databuf;
		dyndata->databuf = NULL;
		delete dyndata;
		dyndata = 0;
	}
}

bool Block::GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &_texid,
	bool &undershadow, bool &groundshadow)
{
	if (grp < 0 || grp >= 3) return false;

	static DWORD nv[3] = {8,8,4};
	static DWORD ni[3] = {12,12,6};
	nvtx = nv[grp];
	nidx = ni[grp];
	undershadow = false;
	groundshadow = true;
	_texid = texid[grp];
	return true;
}

void Block::ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs)
{
	static WORD sidx[12] = {0,1,2,2,3,0,4,5,6,6,7,4};
	DWORD i;
	WORD iofs = (WORD)idx_ofs;
	float *db = dyndata->databuf;

	switch (grp) {
	case 0:
		//dyndata->gv0 = vtx;
		vtx[0].x = vtx[3].x = db[0];
		vtx[1].x = vtx[2].x = db[1];
		vtx[4].x = vtx[7].x = db[2];
		vtx[5].x = vtx[6].x = db[3];
		vtx[0].y = vtx[1].y = vtx[4].y = vtx[5].y = db[4];
		vtx[2].y = vtx[3].y = vtx[6].y = vtx[7].y = db[5];
		vtx[0].z = vtx[3].z = db[6];
		vtx[1].z = vtx[2].z = db[7];
		vtx[4].z = vtx[7].z = db[8];
		vtx[5].z = vtx[6].z = db[9];
		vtx[0].nx = vtx[1].nx = vtx[2].nx = vtx[3].nx =  db[10];
		vtx[4].nx = vtx[5].nx = vtx[6].nx = vtx[7].nx = -db[10];
		vtx[0].ny = vtx[1].ny = vtx[2].ny = vtx[3].ny =  0.0;
		vtx[4].ny = vtx[5].ny = vtx[6].ny = vtx[7].ny =  0.0;
		vtx[0].nz = vtx[1].nz = vtx[2].nz = vtx[3].nz = -db[11];
		vtx[4].nz = vtx[5].nz = vtx[6].nz = vtx[7].nz =  db[11];
		vtx[0].tu = vtx[3].tu = vtx[4].tu = vtx[7].tu =  tuscale[0];
		vtx[1].tu = vtx[2].tu = vtx[5].tu = vtx[6].tu =  0.0;
		vtx[0].tv = vtx[1].tv = vtx[4].tv = vtx[5].tv =  tvscale[0];
		vtx[2].tv = vtx[3].tv = vtx[6].tv = vtx[7].tv =  0.0;
		for (i = 0; i < 12; i++) *idx++ = sidx[i] + iofs;
		return;
	case 1:
		vtx[1].x = vtx[2].x = db[0];
		vtx[4].x = vtx[7].x = db[1];
		vtx[5].x = vtx[6].x = db[2];
		vtx[0].x = vtx[3].x = db[3];
		vtx[0].y = vtx[1].y = vtx[4].y = vtx[5].y = db[4];
		vtx[2].y = vtx[3].y = vtx[6].y = vtx[7].y = db[5];
		vtx[1].z = vtx[2].z = db[6];
		vtx[4].z = vtx[7].z = db[7];
		vtx[5].z = vtx[6].z = db[8];
		vtx[0].z = vtx[3].z = db[9];
		vtx[0].nx = vtx[1].nx = vtx[2].nx = vtx[3].nx =  db[11];
		vtx[4].nx = vtx[5].nx = vtx[6].nx = vtx[7].nx = -db[11];
		vtx[0].ny = vtx[1].ny = vtx[2].ny = vtx[3].ny =  0.0;
		vtx[4].ny = vtx[5].ny = vtx[6].ny = vtx[7].ny =  0.0;
		vtx[0].nz = vtx[1].nz = vtx[2].nz = vtx[3].nz =  db[10];
		vtx[4].nz = vtx[5].nz = vtx[6].nz = vtx[7].nz = -db[10];
		vtx[0].tu = vtx[3].tu = vtx[4].tu = vtx[7].tu =  tuscale[1];
		vtx[1].tu = vtx[2].tu = vtx[5].tu = vtx[6].tu =  0.0;
		vtx[0].tv = vtx[1].tv = vtx[4].tv = vtx[5].tv =  tvscale[1];
		vtx[2].tv = vtx[3].tv = vtx[6].tv = vtx[7].tv =  0.0;
		for (i = 0; i < 12; i++) *idx++ = sidx[i] + iofs;
		return;
	case 2:
		vtx[0].x = db[0];
		vtx[1].x = db[1];
		vtx[2].x = db[2];
		vtx[3].x = db[3];
		vtx[0].y = vtx[1].y = vtx[2].y = vtx[3].y = db[5];
		vtx[0].z = db[6];
		vtx[1].z = db[7];
		vtx[2].z = db[8];
		vtx[3].z = db[9];
		vtx[0].nx = vtx[1].nx = vtx[2].nx = vtx[3].nx =  0.0;
		vtx[0].ny = vtx[1].ny = vtx[2].ny = vtx[3].ny =  1.0;
		vtx[0].nz = vtx[1].nz = vtx[2].nz = vtx[3].nz =  0.0;
		vtx[0].tu = vtx[3].tu = tuscale[2];
		vtx[1].tu = vtx[2].tu = 0.0;
		vtx[0].tv = vtx[1].tv = tvscale[2];
		vtx[2].tv = vtx[3].tv = 0.0;
		for (i = 0; i < 6; i++) *idx++ = sidx[i] + iofs;
		return;
	}

}

Hangar::Hangar (const Base *_base): BaseObject (_base)
{
	ngrp = 3;
	for (int i = 0; i < 3; i++) {
		texid[i] = 0;
		tuscale[i] = tvscale[i] = 1.0f;
	}
	dyndata = 0;
}

Hangar::~Hangar ()
{
	Deactivate ();
}

int Hangar::ParseLine (const char *label, const char *value)
{
	int res = 0;
	if (!strncasecmp (label, "TEX", 3)) {
		float su, sv;
		int i;
		char name[256]; // not upstream: texture names have no length limit, the line is 255
		if (sscanf (label+3, "%d", &i) != 1 || i < 1 || i > 3) {
			ParseError("Hangar: TEXn: Expected integer value 1-3 for n");
			res = 2;
		}
		if (sscanf (value, "%s%f%f", name, &su, &sv) != 3) {
			ParseError("Hangar: TEXn: expected 3 values (*char, scalar, scalar)");
			res = 2;
		}
		if (!res) { // not upstream: only with n in range and all three values read
			texid[i-1] = NameToId (name);
			tuscale[i-1] = su;
			tvscale[i-1] = sv;
		} // not upstream: end of the TEXn guard
	}
	return res;
}

void Hangar::Activate ()
{
	if (dyndata) return; // active already
	dyndata = new struct DYNDATA; TRACENEW
	dyndata->Vtx = new NTVERTEX[44]; TRACENEW
	NTVERTEX *Vtx = dyndata->Vtx;
	float dx = 0.5f*scale.x, dy = scale.y, dz = 0.5f*scale.z;
	float dy1 = 0.5f*dy; // side wall height
	float dy2 = dy-dy1;  // roof height
	float srot = (float)sin(rot), crot = (float)cos(rot);
	float dxcrot = dx*crot, dxsrot = dx*srot;
	float dzsrot = dz*srot, dzcrot = dz*crot;
	float dxcrot1 = 0.72f*dxcrot, dxcrot2 = 0.28f*dxcrot;
	float dxsrot1 = 0.72f*dxsrot, dxsrot2 = 0.28f*dxsrot;
	float tufac = tuscale[0]*dz/dx;

	Vtx[0].x  = Vtx[7].x  = Vtx[17].x = Vtx[18].x = Vtx[29].x =  dxcrot  + dzsrot + relpos.x;
	Vtx[1].x  = Vtx[2].x  = Vtx[20].x = Vtx[23].x = Vtx[39].x = -dxcrot  + dzsrot + relpos.x;
	Vtx[3].x  = Vtx[37].x = Vtx[25].x = Vtx[26].x = Vtx[41].x = Vtx[42].x = -dxcrot1 + dzsrot + relpos.x;
	Vtx[4].x  = Vtx[35].x =                                     -dxcrot2 + dzsrot + relpos.x;
	Vtx[5].x  = Vtx[33].x =                                      dxcrot2 + dzsrot + relpos.x;
	Vtx[6].x  = Vtx[31].x = Vtx[24].x = Vtx[27].x = Vtx[40].x = Vtx[43].x =  dxcrot1 + dzsrot + relpos.x;
	Vtx[8].x  = Vtx[15].x = Vtx[21].x = Vtx[22].x = Vtx[38].x = -dxcrot  - dzsrot + relpos.x;
	Vtx[9].x  = Vtx[10].x = Vtx[16].x = Vtx[19].x = Vtx[28].x =  dxcrot  - dzsrot + relpos.x;
	Vtx[11].x = Vtx[30].x =                                      dxcrot1 - dzsrot + relpos.x;
	Vtx[12].x = Vtx[32].x =                                      dxcrot2 - dzsrot + relpos.x;
	Vtx[13].x = Vtx[34].x =                                     -dxcrot2 - dzsrot + relpos.x;
	Vtx[14].x = Vtx[36].x =                                     -dxcrot1 - dzsrot + relpos.x;
	Vtx[0].z  = Vtx[7].z  = Vtx[17].z = Vtx[18].z = Vtx[29].z =  dxsrot  - dzcrot + relpos.z;
	Vtx[1].z  = Vtx[2].z  = Vtx[20].z = Vtx[23].z = Vtx[39].z = -dxsrot  - dzcrot + relpos.z;
	Vtx[3].z  = Vtx[37].z = Vtx[25].z = Vtx[26].z = Vtx[41].z = Vtx[42].z = -dxsrot1 - dzcrot + relpos.z;
	Vtx[4].z  = Vtx[35].z =                                     -dxsrot2 - dzcrot + relpos.z;
	Vtx[5].z  = Vtx[33].z =                                      dxsrot2 - dzcrot + relpos.z;
	Vtx[6].z  = Vtx[31].z = Vtx[24].z = Vtx[27].z = Vtx[40].z = Vtx[43].z =  dxsrot1 - dzcrot + relpos.z;
	Vtx[8].z  = Vtx[15].z = Vtx[21].z = Vtx[22].z = Vtx[38].z = -dxsrot  + dzcrot + relpos.z;
	Vtx[9].z  = Vtx[10].z = Vtx[16].z = Vtx[19].z = Vtx[28].z =  dxsrot  + dzcrot + relpos.z;
	Vtx[11].z = Vtx[30].z =                                      dxsrot1 + dzcrot + relpos.z;
	Vtx[12].z = Vtx[32].z =                                      dxsrot2 + dzcrot + relpos.z;
	Vtx[13].z = Vtx[34].z =                                     -dxsrot2 + dzcrot + relpos.z;
	Vtx[14].z = Vtx[36].z =                                     -dxsrot1 + dzcrot + relpos.z;
	Vtx[0].y = Vtx[1].y = Vtx[8].y = Vtx[9].y = Vtx[16].y = Vtx[17].y = Vtx[20].y = Vtx[21].y =
		Vtx[24].y = Vtx[25].y = Vtx[40].y = Vtx[41].y = relpos.y;
	Vtx[2].y = Vtx[7].y = Vtx[10].y = Vtx[15].y = Vtx[18].y = Vtx[19].y = Vtx[22].y = Vtx[23].y =
		Vtx[28].y = Vtx[29].y = Vtx[38].y = Vtx[39].y = Vtx[26].y = Vtx[27].y = Vtx[42].y = Vtx[43].y = dy1 + relpos.y;
	Vtx[3].y = Vtx[6].y = Vtx[11].y = Vtx[14].y = Vtx[30].y = Vtx[31].y = Vtx[36].y = Vtx[37].y = dy1 + 0.55f*dy2 + relpos.y;
	Vtx[4].y = Vtx[5].y = Vtx[12].y = Vtx[13].y = Vtx[32].y = Vtx[33].y = Vtx[34].y = Vtx[35].y = dy1 + 0.95f*dy2 + relpos.y;

	int i;
	for (i = 0; i < 8; i++) Vtx[i].nx =  srot, Vtx[i].ny = 0.0, Vtx[i].nz = -crot;
	for (; i < 16; i++)     Vtx[i].nx = -srot, Vtx[i].ny = 0.0, Vtx[i].nz =  crot;
	for (; i < 20; i++)     Vtx[i].nx =  crot, Vtx[i].ny = 0.0, Vtx[i].nz =  srot;
	for (; i < 24; i++)     Vtx[i].nx = -crot, Vtx[i].ny = 0.0, Vtx[i].nz = -srot;
	for (; i < 28; i++)     Vtx[i].nx =  srot, Vtx[i].ny = 0.0, Vtx[i].nz = -crot;
	for (i = 40; i < 44; i++) Vtx[i].nx =  srot, Vtx[i].ny = 0.0, Vtx[i].nz = -crot;
	Vtx[38].nx = Vtx[39].nx = -(Vtx[28].nx = Vtx[29].nx = 0.707f*crot);
	Vtx[38].ny = Vtx[39].ny = Vtx[28].ny = Vtx[29].ny = 0.707f;
	Vtx[38].nz = Vtx[39].nz = -(Vtx[28].nz = Vtx[29].nz = 0.707f*srot);
	Vtx[36].nx = Vtx[37].nx = -(Vtx[30].nx = Vtx[31].nx = 0.5f*crot);
	Vtx[36].ny = Vtx[37].ny = Vtx[30].ny = Vtx[31].ny = 0.82f;
	Vtx[36].nz = Vtx[37].nz = -(Vtx[30].nz = Vtx[31].nz = 0.5f*srot);
	Vtx[34].nx = Vtx[35].nx = -(Vtx[32].nx = Vtx[33].nx = 0.18f*crot);
	Vtx[34].ny = Vtx[35].ny = Vtx[32].ny = Vtx[33].ny = 0.96f;
	Vtx[34].nz = Vtx[35].nz = -(Vtx[32].nz = Vtx[33].nz = 0.18f*srot);

	Vtx[0].tu = Vtx[7].tu = Vtx[8].tu = Vtx[15].tu = tuscale[0];
	Vtx[1].tu = Vtx[2].tu = Vtx[9].tu = Vtx[10].tu = Vtx[17].tu = Vtx[18].tu = Vtx[21].tu = Vtx[22].tu = 0.0;
	Vtx[3].tu = Vtx[11].tu = Vtx[25].tu = Vtx[26].tu = 0.14f*tuscale[0];
	Vtx[4].tu = Vtx[12].tu = 0.36f*tuscale[0];
	Vtx[5].tu = Vtx[13].tu = 0.64f*tuscale[0];
	Vtx[6].tu = Vtx[14].tu = Vtx[24].tu = Vtx[27].tu = 0.86f*tuscale[0];
	Vtx[16].tu = Vtx[19].tu = Vtx[20].tu = Vtx[23].tu = tufac;
	Vtx[40].tu = Vtx[43].tu = tuscale[1];
	Vtx[41].tu = Vtx[42].tu = 0.0;
	Vtx[0].tv = Vtx[1].tv = Vtx[8].tv = Vtx[9].tv = Vtx[16].tv = Vtx[17].tv =
		Vtx[20].tv = Vtx[21].tv = Vtx[24].tv = Vtx[25].tv = 0.0;
	Vtx[2].tv = Vtx[7].tv = Vtx[10].tv = Vtx[15].tv = Vtx[18].tv = Vtx[19].tv =
		Vtx[22].tv = Vtx[23].tv = Vtx[26].tv = Vtx[27].tv = 0.5f*tvscale[0];
	Vtx[3].tv = Vtx[6].tv = Vtx[11].tv = Vtx[14].tv = 0.75f*tvscale[0];
	Vtx[4].tv = Vtx[5].tv = Vtx[12].tv = Vtx[13].tv = tvscale[0];
	Vtx[40].tv = Vtx[41].tv = 0.0;
	Vtx[42].tv = Vtx[43].tv = tvscale[1];
	for (i = 0; i < 6; i++) {
		Vtx[i*2+28].tu = Vtx[i*2+29].tu = i*tuscale[2]*0.2f;
		Vtx[i*2+28].tv = 0.0f;
		Vtx[i*2+29].tv = tvscale[2];
	}	
}

void Hangar::Deactivate ()
{
	if (dyndata) {
		delete []dyndata->Vtx;
		dyndata->Vtx = NULL;
		delete dyndata;
		dyndata = 0;
	}
}

bool Hangar::GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &_texid,
	bool &undershadow, bool &groundshadow)
{
	if (grp < 0 || grp >= 3) return false;

	static DWORD nv[3] = {28,4,12};
	static DWORD ni[3] = {54,6,30};
	nvtx = nv[grp];
	nidx = ni[grp];
	_texid = texid[grp];
	undershadow = false;
	groundshadow = true;
	return true;
}

void Hangar::ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs)
{
	static WORD sidx0[54] = {0,24,7,27,7,24,25,1,26,2,26,1,6,7,2,2,3,6,5,6,3,3,4,5,
		                     8,9,10,10,15,8,15,10,11,11,14,15,14,11,12,12,13,14,
							 16,17,19,18,19,17,20,21,23,22,23,21};
	static WORD sidx1[6]  = {0,1,3,2,3,1};
	static WORD sidx2[30] = {0,1,2,3,2,1,2,3,4,5,4,3,4,5,6,7,6,5,6,7,8,9,8,7,8,9,10,11,10,9};
	DWORD i;
	WORD iofs = (WORD)idx_ofs;

	switch (grp) {
	case 0:
		memcpy (vtx, dyndata->Vtx, 28*sizeof(NTVERTEX));
		for (i = 0; i < 54; i++) *idx++ = sidx0[i] + iofs;
		return;
	case 1:
		memcpy (vtx, dyndata->Vtx+40, 4*sizeof(NTVERTEX));
		for (i = 0; i < 6; i++) *idx++ = sidx1[i] + iofs;
		return;
	case 2:
		memcpy (vtx, dyndata->Vtx+28, 12*sizeof(NTVERTEX));
		for (i = 0; i < 30; i++) *idx++ = sidx2[i] + iofs;
		return;
	}
}

Hangar2::Hangar2 (const Base *_base): BaseObject (_base)
{
	ngrp = 3;
	roofh = -1.0f; // use default
	for (int i = 0; i < 3; i++) {
		texid[i] = 0;
		tuscale[i] = tvscale[i] = 1.0f;
	}
	dyndata = 0;
}

Hangar2::~Hangar2 ()
{
	Deactivate ();
}

int Hangar2::ParseLine (const char *label, const char *value)
{
	int res = 0;
	if (!strcasecmp (label, "ROOFH")) {
		if (sscanf (value, "%f", &roofh) != 1) {
			ParseError("Hangar2: ROOFH: Expected scalar value");
			res = 2;
		}
	} else if (!strncasecmp (label, "TEX", 3)) {
		float su, sv;
		int i;
		char name[256]; // not upstream: texture names have no length limit, the line is 255
		if (sscanf (label+3, "%d", &i) != 1 || i < 1 || i > 3) {
			ParseError("Hangar2: TEXn: Expected integer value 1-3 for n");
			res = 2;
		}
		if (sscanf (value, "%s%f%f", name, &su, &sv) != 3) {
			ParseError("Hangar2: TEXn: expected 3 values (*char, scalar, scalar)");
			res = 2;
		}
		if (!res) { // not upstream: only with n in range and all three values read
			texid[i-1] = NameToId (name);
			tuscale[i-1] = su;
			tvscale[i-1] = sv;
		} // not upstream: end of the TEXn guard
	}
	return res;
}

bool Hangar2::GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &_texid,
	bool &undershadow, bool &groundshadow)
{
	if (grp < 0 || grp >= 3) return false;

	static DWORD nv[3] = {10,8,8};
	static DWORD ni[3] = {18,12,12};
	nvtx = nv[grp];
	nidx = ni[grp];
	_texid = texid[grp];
	undershadow = false;
	groundshadow = true;
	return true;
}

void Hangar2::ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs)
{
	static WORD sidx0[18] = {0,1,2,2,4,0,4,2,3,6,5,9,9,7,6,7,9,8};
	static WORD sidx1[12] = {0,1,3,2,3,1,5,4,6,7,6,4};
	DWORD i;
	WORD iofs = (WORD)idx_ofs;

	switch (grp) {
	case 0:
		memcpy (vtx, dyndata->Vtx, 10*sizeof(NTVERTEX));
		for (i = 0; i < 18; i++) *idx++ = sidx0[i] + iofs;
		return;
	case 1:
		memcpy (vtx, dyndata->Vtx+10, 8*sizeof(NTVERTEX));
		for (i = 0; i < 12; i++) *idx++ = sidx1[i] + iofs;
		return;
	case 2:
		memcpy (vtx, dyndata->Vtx+18, 8*sizeof(NTVERTEX));
		for (i = 0; i < 12; i++) *idx++ = sidx1[i] + iofs;
		return;
	}
}

void Hangar2::Activate ()
{
    if (dyndata) return; // active already
    dyndata = new struct DYNDATA; TRACENEW
    dyndata->Vtx = new NTVERTEX[26]; TRACENEW
    NTVERTEX *Vtx = dyndata->Vtx;
    float dx = 0.5f*scale.x, dy = scale.y, dz = 0.5f*scale.z;
    float dy2 = (roofh < 0.0 || roofh > dy ? 0.5f*dy : roofh); // roof height
    float dy1 = dy - dy2; // side wall height
    float srot = (float)sin(rot), crot = (float)cos(rot);
    float dxcrot = dx*crot, dxsrot = dx*srot;
    float dzsrot = dz*srot, dzcrot = dz*crot;
    float tufac = tuscale[0]*dz/dx;

    Vtx[0].x = Vtx[4].x = Vtx[11].x = Vtx[12].x = Vtx[19].x =  dxcrot + dzsrot + relpos.x;
    Vtx[1].x = Vtx[2].x = Vtx[15].x = Vtx[16].x = Vtx[23].x = -dxcrot + dzsrot + relpos.x;
    Vtx[5].x = Vtx[9].x = Vtx[10].x = Vtx[13].x = Vtx[18].x =  dxcrot - dzsrot + relpos.x;
    Vtx[6].x = Vtx[7].x = Vtx[14].x = Vtx[17].x = Vtx[22].x = -dxcrot - dzsrot + relpos.x;
    Vtx[3].x = Vtx[20].x = Vtx[24].x =                         dzsrot + relpos.x;
    Vtx[8].x = Vtx[21].x = Vtx[25].x =                        -dzsrot + relpos.x;
    Vtx[0].z = Vtx[4].z = Vtx[11].z = Vtx[12].z = Vtx[19].z =  dxsrot - dzcrot + relpos.z;
    Vtx[1].z = Vtx[2].z = Vtx[15].z = Vtx[16].z = Vtx[23].z = -dxsrot - dzcrot + relpos.z;
    Vtx[5].z = Vtx[9].z = Vtx[10].z = Vtx[13].z = Vtx[18].z =  dxsrot + dzcrot + relpos.z;
    Vtx[6].z = Vtx[7].z = Vtx[14].z = Vtx[17].z = Vtx[22].z = -dxsrot + dzcrot + relpos.z;
    Vtx[3].z = Vtx[20].z = Vtx[24].z =                        -dzcrot + relpos.z;
    Vtx[8].z = Vtx[21].z = Vtx[25].z =                         dzcrot + relpos.z;
    Vtx[0].y = Vtx[1].y = Vtx[5].y = Vtx[6].y = Vtx[10].y = Vtx[11].y = Vtx[14].y = Vtx[15].y = relpos.y;
    Vtx[2].y = Vtx[4].y = Vtx[7].y = Vtx[9].y = Vtx[12].y = Vtx[13].y = Vtx[16].y = Vtx[17].y =
    	       Vtx[18].y = Vtx[19].y = Vtx[22].y = Vtx[23].y = dy1 + relpos.y;
    Vtx[3].y = Vtx[8].y = Vtx[20].y = Vtx[21].y = Vtx[24].y = Vtx[25].y = dy + relpos.y;

    int i;
    double alpha = atan2 (dy2, dx); // roof angle
    float ny = (float)cos(alpha), nxz = (float)sin(alpha);

    for (i = 0; i < 5; i++) Vtx[i].nx =  srot, Vtx[i].ny = 0.0, Vtx[i].nz = -crot;
    for (; i < 10; i++)     Vtx[i].nx = -srot, Vtx[i].ny = 0.0, Vtx[i].nz =  crot;
    for (; i < 14; i++)     Vtx[i].nx =  crot, Vtx[i].ny = 0.0, Vtx[i].nz =  srot;
    for (; i < 18; i++)     Vtx[i].nx = -crot, Vtx[i].ny = 0.0, Vtx[i].nz = -srot;
    for (; i < 22; i++)     Vtx[i].nx =  nxz*crot, Vtx[i].ny = ny, Vtx[i].nz =  nxz*srot;
    for (; i < 26; i++)     Vtx[i].nx = -nxz*crot, Vtx[i].ny = ny, Vtx[i].nz = -nxz*srot;

    Vtx[0].tu = Vtx[4].tu = Vtx[6].tu = Vtx[7].tu = 0.0;
    Vtx[1].tu = Vtx[2].tu = Vtx[5].tu = Vtx[9].tu = tuscale[0];
    Vtx[3].tu = Vtx[8].tu = 0.5f*tuscale[0];
    Vtx[10].tu = Vtx[13].tu = Vtx[15].tu = Vtx[16].tu = 0.0;
    Vtx[11].tu = Vtx[12].tu = Vtx[14].tu = Vtx[17].tu = tuscale[1];
    Vtx[18].tu = Vtx[21].tu = Vtx[22].tu = Vtx[25].tu = tuscale[2];
    Vtx[19].tu = Vtx[20].tu = Vtx[23].tu = Vtx[24].tu = 0.0;
    Vtx[0].tv = Vtx[1].tv = Vtx[5].tv = Vtx[6].tv = tvscale[0];
    Vtx[10].tv = Vtx[11].tv = Vtx[14].tv = Vtx[15].tv = tvscale[1];
    Vtx[2].tv = Vtx[4].tv = Vtx[7].tv = Vtx[9].tv =
		Vtx[12].tv = Vtx[13].tv = Vtx[16].tv = Vtx[17].tv = 0.0;
    Vtx[3].tv = Vtx[8].tv = -tvscale[0]*dy2/dy1;
    Vtx[20].tv = Vtx[21].tv = Vtx[24].tv = Vtx[25].tv = 0.0;
    Vtx[18].tv = Vtx[19].tv = Vtx[22].tv = Vtx[23].tv = tvscale[2];
}

void Hangar2::Deactivate ()
{
	if (dyndata) {
		delete []dyndata->Vtx;
		dyndata->Vtx = NULL;
		delete dyndata;
		dyndata = 0;
	}
}

Hangar3::Hangar3 (const Base *_base): BaseObject (_base)
{
	ngrp = 3;
	for (int i = 0; i < 3; i++) {
		texid[i] = 0;
		tuscale[i] = tvscale[i] = 1.0f;
	}
	dyndata = 0;
}

Hangar3::~Hangar3 ()
{
	Deactivate ();
}

int Hangar3::ParseLine (const char *label, const char *value)
{
	int res = 0;
	if (!strncasecmp (label, "TEX", 3)) {
		float su, sv;
		int i;
		char name[256]; // not upstream: texture names have no length limit, the line is 255
		if (sscanf (label+3, "%d", &i) != 1 || i < 1 || i > 3) {
			ParseError("Hangar3: TEXn: Expected integer value 1-3 for n");
			res = 2;
		}
		if (sscanf (value, "%s%f%f", name, &su, &sv) != 3) {
			ParseError("Hangar3: TEXn: expected 3 values (*char, scalar, scalar)");
			res = 2;
		}
		if (!res) { // not upstream: only with n in range and all three values read
			texid[i-1] = NameToId (name);
			tuscale[i-1] = su;
			tvscale[i-1] = sv;
		} // not upstream: end of the TEXn guard
	}
	return res;
}

bool Hangar3::GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &_texid,
	bool &undershadow, bool &groundshadow)
{
	if (grp < 0 || grp >= 3) return false;

	static DWORD nv[3] = {18,8,14};
	static DWORD ni[3] = {36,24,36};
	nvtx = nv[grp];
	nidx = ni[grp];
	_texid = texid[grp];
	undershadow = false;
	groundshadow = true;
	return true;
}

void Hangar3::ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs)
{
	static WORD sidx0[60] = {0,6,1,1,6,5,1,5,2,2,5,4,2,4,3, // back wall
	          				 7,8,14,8,9,14,9,10,16,16,10,17,10,11,17,11,12,15,12,13,15}; // front wall
	static WORD sidx1[24] = {0,1,5,5,4,0,6,7,4,4,5,6,2,3,7,7,6,2,1,2,6,6,5,1}; // entry
	static WORD sidx2[36] = {0,1,7,8,7,1,1,2,8,9,8,2,2,3,9,10,9,3,3,4,10,4,11,10,4,5,11,12,11,5,5,6,12,13,12,6}; // roof
	DWORD i;
	WORD iofs = (WORD)idx_ofs;

	switch (grp) {
	case 0:
		memcpy (vtx, dyndata->Vtx, 18*sizeof(NTVERTEX));
		for (i = 0; i < 36; i++) *idx++ = sidx0[i] + iofs; // not upstream: sidx0 has 36 values (GetGroupSpec)
		return;
	case 1:
		memcpy (vtx, dyndata->Vtx+18, 8*sizeof(NTVERTEX));
		for (i = 0; i < 24; i++) *idx++ = sidx1[i] + iofs;
		return;
	case 2:
		memcpy (vtx, dyndata->Vtx+26, 14*sizeof(NTVERTEX));
		for (i = 0; i < 36; i++) *idx++ = sidx2[i] + iofs;
		return;
	}
}

void Hangar3::Activate ()
{
    static float recess = 2.0f; // should be configurable
    if (dyndata) return; // active already
    dyndata = new struct DYNDATA; TRACENEW
    dyndata->Vtx = new NTVERTEX[40]; TRACENEW
    NTVERTEX *Vtx = dyndata->Vtx;
    float dx = 0.5f*scale.x, dy = scale.y, dz = 0.5f*scale.z;
    float h1 = 0.543f*dy, h2 = 0.884f*dy, h3 = dy;
    float srot = (float)sin(rot), crot = (float)cos(rot);
    float dxcrot = dx*crot, dxsrot = dx*srot;
    float dzsrot = dz*srot, dzcrot = dz*crot;
    float dxcrot1 = 0.707f*dxcrot, dxcrot2 = 0.366f*dxcrot;
    float dxsrot1 = 0.707f*dxsrot, dxsrot2 = 0.366f*dxsrot;
    float dzcrot1 = (dz-recess)*crot, dzsrot1 = (dz-recess)*srot;
    Vtx[ 0].x = Vtx[26].x =  dxcrot  + dzsrot + relpos.x;
    Vtx[ 1].x = Vtx[27].x =  dxcrot1 + dzsrot + relpos.x;
    Vtx[ 2].x = Vtx[28].x =  dxcrot2 + dzsrot + relpos.x;
    Vtx[ 3].x = Vtx[29].x =            dzsrot + relpos.x;
    Vtx[ 4].x = Vtx[30].x = -dxcrot2 + dzsrot + relpos.x;
    Vtx[ 5].x = Vtx[31].x = -dxcrot1 + dzsrot + relpos.x;
    Vtx[ 6].x = Vtx[32].x = -dxcrot  + dzsrot + relpos.x;
    Vtx[ 7].x = Vtx[33].x =  dxcrot  - dzsrot + relpos.x;
    Vtx[ 8].x = Vtx[34].x =  dxcrot1 - dzsrot + relpos.x;
    Vtx[ 9].x = Vtx[35].x = Vtx[14].x = Vtx[16].x = Vtx[18].x = Vtx[19].x =  dxcrot2 - dzsrot + relpos.x;
    Vtx[10].x = Vtx[36].x =          - dzsrot + relpos.x;
    Vtx[11].x = Vtx[37].x = Vtx[15].x = Vtx[17].x = Vtx[20].x = Vtx[21].x = -dxcrot2 - dzsrot + relpos.x;
    Vtx[12].x = Vtx[38].x = -dxcrot1 - dzsrot + relpos.x;
    Vtx[13].x = Vtx[39].x = -dxcrot  - dzsrot + relpos.x;
    Vtx[22].x = Vtx[23].x =  dxcrot2 - dzsrot1 + relpos.x;
    Vtx[24].x = Vtx[25].x = -dxcrot2 - dzsrot1 + relpos.x;
    Vtx[ 0].z = Vtx[26].z =  dxsrot  - dzcrot  + relpos.z;
    Vtx[ 1].z = Vtx[27].z =  dxsrot1 - dzcrot  + relpos.z;
    Vtx[ 2].z = Vtx[28].z =  dxsrot2 - dzcrot  + relpos.z;
    Vtx[ 3].z = Vtx[29].z =          - dzcrot  + relpos.z;
    Vtx[ 4].z = Vtx[30].z = -dxsrot2 - dzcrot  + relpos.z;
    Vtx[ 5].z = Vtx[31].z = -dxsrot1 - dzcrot  + relpos.z;
    Vtx[ 6].z = Vtx[32].z = -dxsrot  - dzcrot  + relpos.z;
    Vtx[ 7].z = Vtx[33].z =  dxsrot  + dzcrot  + relpos.z;
    Vtx[ 8].z = Vtx[34].z =  dxsrot1 + dzcrot  + relpos.z;
    Vtx[ 9].z = Vtx[35].z = Vtx[14].z = Vtx[16].z = Vtx[18].z = Vtx[19].z =  dxsrot2 + dzcrot  + relpos.z;
    Vtx[10].z = Vtx[36].z =            dzcrot  + relpos.z;
    Vtx[11].z = Vtx[37].z = Vtx[15].z = Vtx[17].z = Vtx[20].z = Vtx[21].z = -dxsrot2 + dzcrot  + relpos.z;
    Vtx[12].z = Vtx[38].z = -dxsrot1 + dzcrot  + relpos.z;
    Vtx[13].z = Vtx[39].z = -dxsrot  + dzcrot  + relpos.z;
    Vtx[22].z = Vtx[23].z =  dxsrot2 + dzcrot1 + relpos.z;
    Vtx[24].z = Vtx[25].z = -dxsrot2 + dzcrot1 + relpos.z;
    Vtx[ 0].y = Vtx[ 6].y = Vtx[ 7].y = Vtx[13].y = Vtx[26].y = Vtx[32].y = Vtx[33].y = Vtx[39].y
	      = Vtx[14].y = Vtx[15].y = Vtx[18].y = Vtx[21].y = Vtx[22].y = Vtx[25].y = relpos.y;
    Vtx[ 1].y = Vtx[ 5].y = Vtx[ 8].y = Vtx[12].y = Vtx[27].y = Vtx[31].y = Vtx[34].y = Vtx[38].y
	      = Vtx[16].y = Vtx[17].y = Vtx[19].y = Vtx[20].y = Vtx[23].y = Vtx[24].y = h1 + relpos.y;
    Vtx[ 2].y = Vtx[ 4].y = Vtx[ 9].y = Vtx[11].y = Vtx[28].y = Vtx[30].y = Vtx[35].y = Vtx[37].y = h2 + relpos.y;
    Vtx[ 3].y = Vtx[10].y = Vtx[29].y = Vtx[36].y = h3 + relpos.y;

    int i;
    for (i = 0; i < 7; i++) Vtx[i].nx =  srot, Vtx[i].ny = 0.0, Vtx[i].nz = -crot;
    for (; i < 18; i++)     Vtx[i].nx = -srot, Vtx[i].ny = 0.0, Vtx[i].nz =  crot;
    Vtx[32].nx = Vtx[39].nx = -(Vtx[26].nx = Vtx[33].nx = 0.707f*crot);
    Vtx[32].ny = Vtx[39].ny =   Vtx[26].ny = Vtx[33].ny = 0.707f;
    Vtx[32].nz = Vtx[39].nz = -(Vtx[26].nz = Vtx[33].nz = 0.707f*srot);
    Vtx[31].nx = Vtx[38].nx = -(Vtx[27].nx = Vtx[34].nx = 0.5f*crot);
    Vtx[31].ny = Vtx[38].ny =   Vtx[27].ny = Vtx[34].ny = 0.866f;
    Vtx[31].nz = Vtx[38].nz = -(Vtx[27].nz = Vtx[34].nz = 0.5f*srot);
    Vtx[30].nx = Vtx[37].nx = -(Vtx[28].nx = Vtx[35].nx = 0.259f*crot);
    Vtx[30].ny = Vtx[37].ny =   Vtx[28].ny = Vtx[35].ny = 0.966f;
    Vtx[30].nz = Vtx[37].nz = -(Vtx[28].nz = Vtx[35].nz = 0.259f*srot);
    Vtx[29].nx = Vtx[36].nx = Vtx[29].nz = Vtx[36].nz = 0.0;
    Vtx[29].ny = Vtx[36].ny = 1.0;
    Vtx[20].nx = Vtx[21].nx = -(Vtx[18].nx = Vtx[19].nx = crot);
    Vtx[20].ny = Vtx[21].ny =   Vtx[18].ny = Vtx[19].ny = 0.0;
    Vtx[20].nz = Vtx[21].nz = -(Vtx[18].nz = Vtx[19].nz = srot);
    Vtx[22].nx = Vtx[23].nx = Vtx[24].nx = Vtx[25].nx = -srot;
    Vtx[22].ny = Vtx[23].ny = Vtx[24].ny = Vtx[25].ny =  0.0;
    Vtx[22].nz = Vtx[23].nz = Vtx[24].nz = Vtx[25].nz =  crot;
	
    // texture coordinates for barrel roof (vtx 26-39)
    for (i = 0; i < 7; i++) {
	Vtx[26+i].tu = 0.0;
	Vtx[33+i].tu = tuscale[2];
    }
    Vtx[29].tv = Vtx[36].tv = 0.0;
    for (i = 1; i < 4; i++) {
	Vtx[29+i].tv = Vtx[29-i].tv = Vtx[36+i].tv = Vtx[36-i].tv = tvscale[2]*i*0.3333f;
    }
    // texture coordinates for front/back and door - still need to be done
    for (i = 0; i < 26; i++) {
	Vtx[i].tu = Vtx[i].tv = 0.0;
    }
}

void Hangar3::Deactivate ()
{
	if (dyndata) {
		delete []dyndata->Vtx;
		dyndata->Vtx = NULL;
		delete dyndata;
		dyndata = 0;
	}
}

Tank::Tank (const Base *_base): BaseObject (_base)
{
	ngrp = 2; // 2 mesh groups: mantle and top
	for (int i = 0; i < 2; i++) {
		texid[i] = 0;
		tuscale[i] = tvscale[i] = 1.0f;
	}
	nstep = 12;
	dyndata = 0;
}

int Tank::ParseLine (const char *label, const char *value)
{
	int res = 0;
	if (!strcasecmp (label, "NSTEP")) {
		int n; // not upstream: read as int, clamped to 3..16383 (WORD indices and loops)
		if (sscanf (value, "%d", &n) != 1) {
			ParseError("Tank: NSTEP: Expected integer value");
			res = 2;
		} else nstep = (DWORD)(n < 3 ? 3 : n > 16383 ? 16383 : n); // not upstream: the clamp
	} else if (!strncasecmp (label, "TEX", 3)) {
		float su, sv;
		int i;
		char name[256]; // not upstream: texture names have no length limit, the line is 255
		if (sscanf (label+3, "%d", &i) != 1 || i < 1 || i > 2) {
			ParseError("Tank: TEXn: Expected integer value 1-2 for n");
			res = 2;
		}
		if (sscanf (value, "%s%f%f", name, &su, &sv) != 3) {
			ParseError("Tank: TEXn: expected 3 values (*char, scalar, scalar)");
			res = 2;
		}
		if (!res) { // not upstream: only with n in range and all three values read
			texid[i-1] = NameToId (name);
			tuscale[i-1] = su;
			tvscale[i-1] = sv;
		} // not upstream: end of the TEXn guard
	}
	return res;
}

void Tank::Activate ()
{
	if (dyndata) return; // active already
	dyndata = new struct DYNDATA; TRACENEW
	dyndata->Vtx = new NTVERTEX[nstep*3+3]; TRACENEW
	NTVERTEX *Vtx = dyndata->Vtx;
	float dx, dz, dnx, dnz, fac, ifac = 1.0f/(float)nstep;
	float srot = (float)sin(rot), crot = (float)cos(rot);
	DWORD i, ofs1 = nstep+1, ofs2 = 2*nstep+2;
	double alpha;

	for (i = 0; i < nstep; i++) {
		fac = (float)i*ifac;
		alpha = Pi2*fac;
		dx = (dnx = (float)cos(alpha)) * scale.x;
		dz = (dnz = (float)sin(alpha)) * scale.z;
		Vtx[i].x = Vtx[ofs1+i].x = Vtx[ofs2+i].x = crot*dx - srot*dz + relpos.x;
		Vtx[i].z = Vtx[ofs1+i].z = Vtx[ofs2+i].z = srot*dx + crot*dz + relpos.z;
		Vtx[i].y = relpos.y;
		Vtx[ofs1+i].y = Vtx[ofs2+i].y = scale.y + relpos.y;
		Vtx[i].nx = Vtx[ofs1+i].nx = crot*dnx - srot*dnz;
		Vtx[i].ny = Vtx[ofs1+i].ny = 0.0f;
		Vtx[i].nz = Vtx[ofs1+i].nz = srot*dnx + crot*dnz;
		Vtx[ofs2+i].nx = Vtx[ofs2+i].nz = 0.0f;
		Vtx[ofs2+i].ny = 1.0f;
		Vtx[i].tu = Vtx[ofs1+i].tu = fac*tuscale[0];
		Vtx[i].tv = 0.0f;
		Vtx[ofs1+i].tv = tvscale[0];
		Vtx[ofs2+i].tu = (0.5f+dnx)*tuscale[1];
		Vtx[ofs2+i].tv = (0.5f+dnz)*tvscale[1];
	}
	Vtx[nstep].x = Vtx[ofs2-1].x = Vtx[0].x;
	Vtx[nstep].z = Vtx[ofs2-1].z = Vtx[0].z;
	Vtx[nstep].y = relpos.y; Vtx[ofs2-1].y = scale.y + relpos.y;
	Vtx[nstep].nx = Vtx[ofs2-1].nx = Vtx[0].nx;
	Vtx[nstep].ny = Vtx[ofs2-1].ny = 0.0f;
	Vtx[nstep].nz = Vtx[ofs2-1].nz = Vtx[0].nz;
	Vtx[nstep].tu = Vtx[ofs2-1].tu = tuscale[0];
	Vtx[nstep].tv = 0.0f; Vtx[ofs2-1].tv = tvscale[0];
}

void Tank::Deactivate ()
{
	if (dyndata) {
		delete []dyndata->Vtx;
		dyndata->Vtx = NULL;
		delete dyndata;
		dyndata = 0;
	}
}

bool Tank::GetGroupSpec (int grp, DWORD &nvtx, DWORD &nidx, LONGLONG &_texid,
	bool &undershadow, bool &groundshadow)
{
	switch (grp) {
	case 0:
		nvtx = (nstep+1)*2;
		nidx = nstep*6;
		_texid = texid[0];
		undershadow = false;
		groundshadow = true;
		return true;
	case 1:
		nvtx = nstep;
		nidx = (nstep-2)*3;
		_texid = texid[1];
		undershadow = false;
		groundshadow = true;
		return true;
	default:
		return false;
	}
}

void Tank::ExportGroup (int grp, NTVERTEX *vtx, WORD *idx, DWORD &idx_ofs)
{
	WORD i, ofs = (WORD)(nstep+1), iofs = (WORD)idx_ofs;

	switch (grp) {
	case 0:
		memcpy (vtx, dyndata->Vtx, (nstep+1)*2*sizeof(NTVERTEX));
		for (i = 0; i < nstep; i++) {
			*idx++ = iofs + i;
			*idx++ = iofs + ofs+i;
			*idx++ = iofs + i+1;
			*idx++ = iofs + ofs+1+i;
			*idx++ = iofs + i+1;
			*idx++ = iofs + ofs+i;
		}
		return;
	case 1:
		memcpy (vtx, dyndata->Vtx+((nstep+1)*2), nstep*sizeof(NTVERTEX));
		for (i = 1; i < nstep-1; i++) {
			*idx++ = iofs;
			*idx++ = iofs + i+1;
			*idx++ = iofs + i;
		}
		return;
	}
}

// BaseObject::Create (Baseobj.cpp:36-92)
static BaseObject *Create (istream &is, std::string &type, std::vector<std::string> &err)
{
	char cbuf[256], *tok;
	BaseObject *bo = nullptr;
	static const Base b0;
	const Base *_base = &b0;
	for (;;) {
		if (!is.getline (cbuf, 256)) return nullptr;
		trim_string (cbuf);
		if ((tok = strtok (cbuf, " \t")) == NULL) continue;
		type = tok;
		if (!strcasecmp (tok, "END_OBJECTLIST")) return nullptr;
		else if (!strcasecmp (tok, "MESH")) bo = new MeshObject (_base);
		else if (!strcasecmp (tok, "BLOCK")) bo = new Block (_base);
		else if (!strcasecmp (tok, "HANGAR")) bo = new Hangar (_base);
		else if (!strcasecmp (tok, "HANGAR2")) bo = new Hangar2 (_base);
		else if (!strcasecmp (tok, "HANGAR3")) bo = new Hangar3 (_base);
		else if (!strcasecmp (tok, "TANK")) bo = new Tank (_base);
		else if (!strcasecmp (tok, "LPAD1")) bo = new Lpad (_base, "Lpad1");
		else if (!strcasecmp (tok, "LPAD2")) bo = new Lpad (_base, "Lpad2");
		else if (!strcasecmp (tok, "LPAD2A")) bo = new Lpad (_base, "Lpad2a");
		else if (!strcasecmp (tok, "RUNWAY")) bo = new Runway (_base);
		else if (!strcasecmp (tok, "RUNWAYLIGHTS")) bo = new RunwayLights (_base);
		else if (!strcasecmp (tok, "BEACONARRAY")) bo = new BeaconArray (_base);
		else if (!strcasecmp (tok, "TRAIN1")) bo = new Train (_base, false);
		else if (!strcasecmp (tok, "TRAIN2")) bo = new Train (_base, true);
		else if (!strcasecmp (tok, "SOLARPLANT")) bo = new SolarPlant (_base);
		else { err.push_back ("BaseObject: Parse error"); return nullptr; }
		bo->err = &err;
		if (bo->Read (is) == 0) return bo;
		delete bo;
		err.push_back ("BaseObject: Parse error in " + type);
		return nullptr;
	}
}
} // namespace collbo

using namespace collbo;

int CollBaseObjClass (const std::string &type)
{
	static const char *const t[] = { "BLOCK", "HANGAR", "HANGAR2", "HANGAR3", "TANK", "MESH" };
	for (int i = 0; i < 6; i++) if (!strcasecmp (type.c_str (), t[i])) return DENTB_BLOCK + i;
	return -1;
}

bool CollBaseObjIncluded (const CollBaseObjDef &o)
{
	if (o.noCollide) return false;
	if (o.collide) return true;
	if (!(o.specs & (OBJSPEC_EXPORTVERTEX | OBJSPEC_EXPORTMESH))) return false;
	return !(o.specs & (OBJSPEC_LPAD | OBJSPEC_RWY | OBJSPEC_UNDERSHADOW | OBJSPEC_WRAPTOSURFACE | OBJSPEC_UPDATEVERTEX));
}

// base cfg (Base.cpp:40-90): Name, LOCATION, OBJECTSIZE, MAPOBJECTSTOSPHERE, BEGIN_OBJECTLIST
bool CollParseBaseFile (const std::string &text, CollBaseFile &out, std::vector<std::string> &warn)
{
	out = CollBaseFile ();
	std::string v;
	if (CollItemString (text, "Name", v)) out.name = v;
	if (CollItemString (text, "LOCATION", v)) {
		double lng, lat;
		if (sscanf (v.c_str (), "%lf%lf", &lng, &lat) == 2) { out.haveLocation = true; out.lng = lng * RAD; out.lat = lat * RAD; }
	}
	CollItemReal (text, "OBJECTSIZE", out.objSize);
	CollItemBool (text, "MAPOBJECTSTOSPHERE", out.mapToSphere);
	std::istringstream is (text);
	char cbuf[256];
	bool list = false;
	while (is.getline (cbuf, 256)) {
		char *cp = trim_string (cbuf);
		if (!strcasecmp (cp, "BEGIN_OBJECTLIST")) { list = true; break; }
	}
	if (!list) return true;
	for (uint32_t idx = 0;; idx++) {
		std::string type;
		BaseObject *bo = Create (is, type, warn);
		if (!bo) break;
		CollBaseObjDef d;
		d.type = type; d.index = idx;
		d.pos = bo->relpos; d.scale = bo->scale; d.rot = bo->rot;
		d.specs = bo->GetSpecs ();
		d.noCollide = bo->noCollide; d.collide = bo->collide; d.collMat = bo->collMat;
		if (auto *mo = dynamic_cast<MeshObject *> (bo)) d.meshFile = mo->fname;
		if (auto *tk = dynamic_cast<Tank *> (bo)) d.nstep = tk->NStep ();
		if (auto *h2 = dynamic_cast<Hangar2 *> (bo)) d.roofh = h2->RoofH ();
		delete bo;
		out.obj.push_back (std::move (d));
	}
	return true;
}

// geometry in the base frame: primitives by the copied ExportGroup, MESH by Scale, Rotate Y, Translate (Baseobj.cpp:340-365)
bool CollBaseObjGeometry (CollBaseObjDef &o, CollSdk &sdk, const CollDirs &dirs, double rPlanet, bool mapToSphere, std::vector<std::string> &warn, const CollBaseElev *elev)
{
	o.grp.clear ();
	double yofs = 0;
	if (elev && elev->at) yofs = elev->at (elev->lng + o.pos.z/(rPlanet*cos(elev->lat)), elev->lat - o.pos.x/rPlanet) - elev->elev; // Rel_EquPos (Base.cpp:556-560)
	if (mapToSphere) yofs += (float)(rPlanet - std::sqrt (rPlanet*rPlanet + (float)o.pos.x*(float)o.pos.x + (float)o.pos.z*(float)o.pos.z));
	Vector rel (o.pos.x, o.pos.y + yofs, o.pos.z);
	if (!strcasecmp (o.type.c_str (), "MESH")) {
		CollRestMesh m;
		std::string text, path = CollMeshPath (dirs, o.meshFile, ".msh");
		if (path.empty () || !sdk.ReadText (sdk.Resolve (path), text) || !CollParseMsh (text, o.meshFile.c_str (), m)) { warn.push_back ("MESH file not found: " + o.meshFile); return false; }
		Vector mn (1e30, 1e30, 1e30), mx (-1e30, -1e30, -1e30);
		float cosa = (float)cos ((float)o.rot), sina = (float)sin ((float)o.rot);
		for (auto &g : m.grp) {
			for (auto &v : g.vtx) {
				v.x *= (float)o.scale.x; v.y *= (float)o.scale.y; v.z *= (float)o.scale.z;
				mn.x = std::min (mn.x, (double)v.x); mn.y = std::min (mn.y, (double)v.y); mn.z = std::min (mn.z, (double)v.z);
				mx.x = std::max (mx.x, (double)v.x); mx.y = std::max (mx.y, (double)v.y); mx.z = std::max (mx.z, (double)v.z);
				if (o.rot) { float x = v.x, z = v.z; v.x = cosa*x - sina*z; v.z = sina*x + cosa*z; }
				v.x += (float)rel.x; v.y += (float)rel.y; v.z += (float)rel.z;
			}
			o.grp.push_back (std::move (g));
		}
		o.restBox = mx - mn;
		return !o.grp.empty ();
	}
	static const Base b0;
	BaseObject *bo = nullptr;
	if (!strcasecmp (o.type.c_str (), "BLOCK")) bo = new Block (&b0);
	else if (!strcasecmp (o.type.c_str (), "HANGAR")) bo = new Hangar (&b0);
	else if (!strcasecmp (o.type.c_str (), "HANGAR2")) bo = new Hangar2 (&b0);
	else if (!strcasecmp (o.type.c_str (), "HANGAR3")) bo = new Hangar3 (&b0);
	else if (!strcasecmp (o.type.c_str (), "TANK")) bo = new Tank (&b0);
	if (!bo) return false;
	bo->relpos = rel; bo->scale = o.scale; bo->rot = o.rot; bo->yofs = yofs;
	if (auto *tk = dynamic_cast<Tank *> (bo)) tk->SetNStep (o.nstep < 3 ? 3 : o.nstep > 16383 ? 16383 : o.nstep);
	if (auto *h2 = dynamic_cast<Hangar2 *> (bo)) h2->SetRoofH (o.roofh);
	bo->Activate ();
	for (int g = 0; g < bo->nGroup (); g++) {
		DWORD nv = 0, ni = 0; LONGLONG tex = 0; bool us = false, gs = false;
		if (!bo->GetGroupSpec (g, nv, ni, tex, us, gs)) continue;
		CollGroupData gd;
		gd.vtx.assign (nv, CollVtx {}); gd.idx.assign (ni, 0);
		DWORD ofs = 0;
		bo->ExportGroup (g, gd.vtx.data (), gd.idx.data (), ofs);
		gd.undersh = us; gd.groundsh = gs; gd.texid = tex;
		o.grp.push_back (std::move (gd));
	}
	bo->Deactivate ();
	delete bo;
	return !o.grp.empty ();
}
