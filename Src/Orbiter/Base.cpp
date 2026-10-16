// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "Orbiter.h"
#include "Base.h"
#include "Baseobj.h"
#include "Vessel.h"
#include "Planet.h"
#include "Psys.h"
#include "Astro.h"
#include "Mesh.h"
#include "Log.h"
#include "Util.h"
#include "GraphicsAPI.h"
#include <fstream>
#include <algorithm> // not upstream
#include <cstring> // not upstream

using namespace std;

extern Orbiter *g_pOrbiter;
extern TimeData td;
extern char DBG_MSG[256];

static int refcount = 0;

// ==========================================================
// static resources

Mesh       *Base::generic_obj_mesh = 0;
char      **Base::generic_mesh_name = 0;
int         Base::ngenericmesh = 0;
SURFHANDLE *Base::generic_dtex = 0;
SURFHANDLE *Base::generic_ntex = 0;
char      **Base::generic_tex_name = 0;
LONGLONG   *Base::generic_tex_id = 0;
int         Base::ngenerictex = 0;

// ==========================================================
// class Base

Base::Base (char *fname, Planet *_planet, double _lng, double _lat)
: Body (fname), lng (_lng), lat (_lat)
{
	char cbuf[256];
	int texid = 0, bias = 0;
	//visual = 0;
	sundir.Set(0,-1,0);
	sundir_updt = 0.0;

	InitDeviceObjects ();

	ifstream ifs (oapiResolvePath (g_pOrbiter->ConfigPath(fname)));

	// read location information from file, if available
	if (ifs && GetItemString (ifs, "LOCATION", cbuf)) {
		sscanf (cbuf, "%lf%lf", &lng, &lat);
		lng *= RAD; lat *= RAD;
	}

	Attach (_planet);

	// read list of objects
	nobj = npad = nrwy = nnav = nvor = 0;
	if (!ifs) return;

	// default base object size scale
	if (!GetItemReal (ifs, "OBJECTSIZE", objscale)) objscale = 100.0;

	// map base objects onto sphere (take into account planet curvature)?
	if (!GetItemBool (ifs, "MAPOBJECTSTOSPHERE", bObjmapsphere)) bObjmapsphere = false;

	// read list of VOR transmitters into planet data base
	if (FindLine (ifs, "BEGIN_NAVBEACON")) {
		char cbuf[256];
		for (;;) {
			if (!ifs.getline (cbuf, 256) || !strncasecmp (cbuf, "END_NAVBEACON", 13)) break;
			Nav *nv = ParseNav (cbuf, _planet);
			if (nv) {
				_planet->NavMgr().AddNav (nv);
				Nav **tmp = new Nav*[nvor+1]; TRACENEW
				if (nvor) {
					memcpy (tmp, vor, nvor*sizeof(Nav*));
					delete []vor;
					vor = NULL;
				}
				vor = tmp;
				vor[nvor++] = nv;
			}
		}
	}

	genmsh_os = genmsh_us = 0;
	objmsh_os = objmsh_us = 0;
	objmsh_sh = 0; sh_elev = 0;
	nobjmsh_os = nobjmsh_us = nobjmsh_sh = 0;
	objmsh_valid = false;

	if (FindLine (ifs, "BEGIN_OBJECTLIST")) {
		BaseObject *bo;
		while (bo = BaseObject::Create (this, ifs)) {
			BaseObject **tmp = new BaseObject*[nobj+1]; TRACENEW
			memcpy (tmp, obj, nobj*sizeof(BaseObject*));
			if (nobj) delete []obj;
			obj = tmp;
			obj[nobj++] = bo;
			// register landing pads
			if (bo->GetSpecs() & OBJSPEC_LPAD) {
				((Lpad*)bo)->SetPadno (npad);
				LpadSpec *tmp = new LpadSpec[npad+1]; TRACENEW
				if (npad) memcpy (tmp, lspec, npad*sizeof(LpadSpec)), delete []lspec;
				lspec = tmp;
				const Vector &pos = ((Lpad*)bo)->GetPos();
				lspec[npad].relpos.Set (pos.x, pos.y, pos.z);
				lspec[npad].status = 0;
				lspec[npad].vessel = 0;
				if (float freq = ((Lpad*)bo)->GetILSfreq()) {
					double plng, plat;
					Rel_EquPos (lspec[npad].relpos, plng, plat);
					navlist.AddNav (lspec[npad].nav = new Nav_VTOL (this, npad, plng, plat, freq)); TRACENEW
				} else lspec[npad].nav = 0;
				npad++;
			} else if (bo->GetSpecs() & OBJSPEC_RWY) {
				Runway *prwy = (Runway*)bo;
				RwySpec *tmp = new RwySpec[nrwy+1]; TRACENEW
				if (nrwy) memcpy (tmp, rwy, nrwy*sizeof(RwySpec)), delete []rwy;
				rwy = tmp;
				Vector p1 (Vector (prwy->end1.x, prwy->end1.y, prwy->end1.z));
				Vector p2 (Vector (prwy->end2.x, prwy->end2.y, prwy->end2.z));
				Rel_EquPos (p1, rwy[nrwy].lng1, rwy[nrwy].lat1);
				Rel_EquPos (p2, rwy[nrwy].lng2, rwy[nrwy].lat2);
				Orthodome (rwy[nrwy].lng1, rwy[nrwy].lat1, rwy[nrwy].lng2, rwy[nrwy].lat2, rwy[nrwy].length, rwy[nrwy].appr1);
				rwy[nrwy].appr1 = posangle (rwy[nrwy].appr1);
				rwy[nrwy].length *= cbody->Size();
				if (float freq = prwy->GetILSfreq(0)) {
					navlist.AddNav (rwy[nrwy].ils1 = new Nav_ILS (this, rwy[nrwy].appr1, rwy[nrwy].lng1, rwy[nrwy].lat1, freq)); TRACENEW
				}
				else rwy[nrwy].ils1 = 0;
				if (float freq = prwy->GetILSfreq(1)) {
					navlist.AddNav (rwy[nrwy].ils2 = new Nav_ILS (this, posangle (rwy[nrwy].appr1+Pi), rwy[nrwy].lng2, rwy[nrwy].lat2, freq)); TRACENEW
				}
				else rwy[nrwy].ils2 = 0;
				nrwy++;
			}
		}
	}
	nnav = navlist.nNav();
	padfree = npad;

	// read list of surface tiles
	ntile = ntilebuf = 0;
	if (FindLine (ifs, "BEGIN_SURFTILELIST")) {
		char cbuf[256];
		int res, texflag, ilng, ilat;
		for (;;) {
			if (!ifs.getline(cbuf,256) || !strncasecmp (cbuf, "END_SURFTILELIST", 16)) break;
			if (sscanf (cbuf, "%d%d%d%d", &res, &ilng, &ilat, &texflag) != 4 || res < 0 || res > 9) continue; // not upstream: a short line or a res outside nlat[10] is skipped
			if (ntile == ntilebuf) {
				SurftileSpec *tmp = new SurftileSpec[ntilebuf+32]; TRACENEW
				if (ntile) {
					memcpy (tmp, tile, ntile*sizeof(SurftileSpec));
					delete []tile;
				}
				tile = tmp;
				ntilebuf += 32;
			}
			tile[ntile].res     = res;
			tile[ntile].texflag = texflag;
			tile[ntile].ilng    = ilng;
			tile[ntile].ilat    = ilat;
			tile[ntile].mesh    = NULL;
			tile[ntile].tex     = NULL;
			ntile++;
		}
	}

	ifs.clear (); // not upstream: FindLine's rewind needs a stream without failbit (design base-clear)
	if (FindLine (ifs, "BEGIN_CLEARZONES")) { // not upstream: no streamed buildings or trees in these (design base-clear)
		std::vector<std::string> warn;
		ReadClearZones (ifs, clear, warn);
		for (const std::string &w : warn) LOGOUT_WARN ("Base %s: BEGIN_CLEARZONES: %s", Name(), w.c_str());
	}
}

Base::~Base ()
{
	if (npad) {	delete []lspec; lspec = NULL; }
	if (nrwy) { delete []rwy; rwy = NULL; }
	if (nobj) { 
		for (int i = 0; i < nobj; i++) {
			delete obj[i];
		}
		delete []obj;
		obj = NULL;
	}
	if (nvor) { delete []vor; vor = NULL; }
	//if (genmsh_os) delete genmsh_os;
	//if (genmsh_us) delete genmsh_us;
	// Need to think about this! Destructor tries to release mesh textures,
	// but in this case they are not managed by the mesh.

	if (objmsh_os) { delete []objmsh_os; objmsh_os = NULL; }
	if (objmsh_us) { delete []objmsh_us; objmsh_us = NULL; }
	if (objmsh_sh) { delete []objmsh_sh; objmsh_sh = NULL; }
	if (sh_elev) { delete []sh_elev; sh_elev = NULL; }
}

void Base::CreateStaticDeviceObjects ()
{
	ngenericmesh = 0;
	ngenerictex  = 0;
	ifstream ifs (oapiResolvePath (g_pOrbiter->ConfigPath ("Base")));
	std::vector<std::string> parts = ConfigFragments (g_pOrbiter->ConfigPath ("Base")); // not upstream: drop-in parts' textures after Base.cfg's (design dropin-addons)
	for (size_t part = 0; part <= parts.size(); part++) { // not upstream: as above; 0 is Base.cfg
		ifstream pfs; // not upstream: as above
		if (part) pfs.open (parts[part-1]); // not upstream: as above
		std::istream &ifs2 = part ? (std::istream&)pfs : (std::istream&)ifs; // not upstream: as above
		if (!ifs2) continue; // not upstream: as above
		char cbuf[256], **tmp_list;
		LONGLONG *tmp_id;
		// load list of generic texture names
		if (FindLine (ifs2, "begin_textures")) { // not upstream: ifs2
			for (;;) {
				if (!ifs2.getline (cbuf, 256) || !strncasecmp (cbuf, "end_textures", 12)) break; // not upstream: ifs2
				char *str = trim_string (cbuf);
				bool known = false; // not upstream: a name listed already (Base.cfg or another part) is not added again
				for (int i = 0; i < ngenerictex && *str; i++) known |= !strcasecmp (generic_tex_name[i], str); // not upstream: as above
				if (*str && !known) { // not upstream: as above
					tmp_list = new char*[ngenerictex+1]; TRACENEW
					tmp_id   = new LONGLONG[ngenerictex+1]; TRACENEW
					if (ngenerictex) {
						memcpy (tmp_list, generic_tex_name, ngenerictex*sizeof(char*));
						delete []generic_tex_name;
						generic_tex_name = NULL;
						memcpy (tmp_id, generic_tex_id, ngenerictex*sizeof(LONGLONG));
						delete []generic_tex_id;
						generic_tex_id = NULL;
					}
					generic_tex_name = tmp_list;
					generic_tex_id = tmp_id;
					int len = strlen(str);
					generic_tex_name[ngenerictex] = new char[len+1]; TRACENEW
					strcpy (generic_tex_name[ngenerictex], str);
					generic_tex_id[ngenerictex] = NameToId (str);
				    ngenerictex++;
				}
			}
		}
	} // not upstream: the parts' loop
	// load generic textures
	if (ngenerictex) {
		generic_dtex = new SURFHANDLE[ngenerictex]; TRACENEW
		generic_ntex = new SURFHANDLE[ngenerictex]; TRACENEW
		// the actual load is performed by InitStaticDeviceObjects
	}

	oapi::GraphicsClient *gclient = g_pOrbiter->GetGraphicsClient();
	char fname[256];

	for (int i = 0; i < ngenerictex; i++) {
		if (gclient) {
			if (snprintf (fname, sizeof fname, "%s.dds", generic_tex_name[i]) >= (int)sizeof fname) { // not upstream: bounded; a name that doesn't fit: no texture, as a missing one
				LOGOUT_WARN ("Path too long: %s.dds", generic_tex_name[i]);
				generic_dtex[i] = NULL;
			} else generic_dtex[i] = gclient->clbkLoadTexture (fname); // not upstream: only when the name fit
			if (snprintf (fname, sizeof fname, "%s_n.dds", generic_tex_name[i]) >= (int)sizeof fname) { // not upstream: bounded; a name that doesn't fit: no texture, as a missing one
				LOGOUT_WARN ("Path too long: %s_n.dds", generic_tex_name[i]);
				generic_ntex[i] = NULL;
			} else generic_ntex[i] = gclient->clbkLoadTexture (fname); // not upstream: only when the name fit
		} else {
			generic_dtex[i] = NULL;
			generic_ntex[i] = NULL;
		}
	}
}
	
void Base::DestroyStaticDeviceObjects ()
{
	// destroy common static resources
	oapi::GraphicsClient *gclient = g_pOrbiter->GetGraphicsClient();

	// destroy textures
	if (gclient && ngenerictex) {
		for (int i = 0; i < ngenerictex; i++) {
			if (generic_dtex[i]) gclient->clbkReleaseTexture (generic_dtex[i]);
			if (generic_ntex[i]) gclient->clbkReleaseTexture (generic_ntex[i]);
			delete []generic_tex_name[i];
			generic_tex_name[i] = NULL;
		}
		delete []generic_tex_name;
		generic_tex_name = NULL;
		delete []generic_dtex;
		generic_dtex = NULL;
		delete []generic_ntex;
		generic_ntex = NULL;
	}
}

void Base::InitDeviceObjects ()
{
	Body::InitDeviceObjects();
}

void Base::DestroyDeviceObjects ()
{
	DestroySurfaceTiles ();
	Body::DestroyDeviceObjects ();
}

void Base::Setup ()
{
	elev = RefPlanet()->Elevation (lng, lat); 
	if (elev) {
		rad += elev;
		cbody->EquatorialToLocal (lng, lat, rad, rpos);
	}

	for (DWORD i = 0; i < nobj; i++)
		obj[i]->Setup();
}

bool Base::InitSurfaceTiles () const
{
	if (!ntile) return false;

	static DWORD nlat[10] = {128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
	DWORD i;
	float r = (float)rad;
	float cphi = (float)cos (lng), sphi = (float)sin (lng);
	float ctht = (float)cos (lat), stht = (float)sin (lat);

	oapi::FMATRIX4 R = {(float)rrot.m11, (float)rrot.m12, (float)rrot.m13, 0,
		           (float)rrot.m21, (float)rrot.m22, (float)rrot.m23, 0,
				   (float)rrot.m31, (float)rrot.m32, (float)rrot.m33, 0,
				   0,               0,               0,               1};

	for (i = 0; i < ntile; i++) {

		// 1. Create the tile meshes
		if (!tile[i].mesh) {
			Mesh *mesh = new Mesh; TRACENEW
			tile[i].mesh = (MESHHANDLE)mesh;
			int n = nlat[tile[i].res], n4 = n*4;
			int alat = tile[i].ilat;
			bool south = (alat < 0);
			if (south) alat = -alat - 1;
			CreateSpherePatch (*mesh, n4, n, alat, 4, 4, false, true);
			mesh->Scale (r, r, r);
			if (south) mesh->Rotate (Mesh::ROTATE_X, (float)Pi);
			mesh->Rotate (Mesh::ROTATE_Y, (float)(Pi05*(tile[i].ilng + (south ? 1:0)))/(float)n);
			mesh->Translate (-r*ctht*cphi, -r*stht, -r*ctht*sphi);
			mesh->Transform (R);
		}

		// 2. Load the textures
		if (!tile[i].tex && (tile[i].texflag & 1)) {
			char cbuf[256];
			if (snprintf (cbuf, sizeof cbuf, "%s_%d_%c%04d_%c%04d.dds", cbody->Name(), tile[i].res, // not upstream: bounded; a name that doesn't fit: no texture, as a missing one
				tile[i].ilng >= 0 ? 'e':'w', abs(tile[i].ilng),
				tile[i].ilat >= 0 ? 'n':'s', abs(tile[i].ilat)) >= (int)sizeof cbuf) // not upstream: result checked
				LOGOUT_WARN ("Path too long: %s", cbody->Name()); // not upstream
			else tile[i].tex = g_pOrbiter->LoadTexture (cbuf); // not upstream: only when the name fit
		}
	}

	return true;
}

void Base::DestroySurfaceTiles ()
{
	if (ntilebuf) {
		for (DWORD i = 0; i < ntile; i++) {
			if (tile[i].mesh) {
				delete (Mesh*)tile[i].mesh;
				tile[i].mesh = NULL;
			}
			if (tile[i].tex) {
				g_pOrbiter->ReleaseSurface (tile[i].tex);
				tile[i].tex = NULL;
			}
		}
		delete []tile;
		tile = NULL;
		ntile = ntilebuf = 0;
	}
}

void Base::Attach (Planet *_parent)
{
	cbody = _parent;

	// Set local parameters
	double slng = sin(lng), clng = cos(lng);
	double slat = sin(lat), clat = cos(lat);
	rad = cbody->Size(); // dist from planet centre
	elev = 0.0;
	cbody->EquatorialToLocal (slng, clng, slat, clat, rad, rpos);
	s0->vel.Set (0,0,0);                         // base is fixed to ground
	rrot.Set ( clng*slat, clng*clat, -slng,   // rotate from local
		      -clat,      slat,       0,      // base to local
			   slng*slat, slng*clat,  clng);  // planet coords

	double v = Pi2*rad*clat / cbody->RotT(); // surface velocity
	rotvel.Set (-v*slng, 0.0, v*clng);        // velocity vector in non-rotating planet coords
}

DWORD Base::GetTileList (const SurftileSpec **_tile) const
{
	if (ntile && !tile[0].mesh)
		InitSurfaceTiles();
	*_tile = tile; return ntile;
}

void Base::ExportBaseStructures (Mesh ***mesh_us, DWORD *nmesh_us, Mesh ***mesh_os, DWORD *nmesh_os) const
{
	ScanObjectMeshes();
	*mesh_os = objmsh_os;  *nmesh_os = nobjmsh_os;
	*mesh_us = objmsh_us;  *nmesh_us = nobjmsh_us;
}

void Base::ExportShadowGeometry (Mesh ***mesh_shadow, double **elev, DWORD *nmesh_shadow) const
{
	ScanObjectMeshes();
	*mesh_shadow = objmsh_sh;
	*elev = sh_elev;
	*nmesh_shadow = nobjmsh_sh;
}

void Base::ScanObjectMeshes () const
{
	if (objmsh_valid) return; // done already

	DWORD i, j, k, ng, spec, nvtx, nidx;
	LONGLONG texid;
	bool undersh, groundsh;
	GroupSpec *grp_os = 0, *grp_us = 0; // mesh groups for the meshes compiled from generic primitives (over and under shadows)
	DWORD ngrp_os = 0, ngrp_us = 0;
	nobjmsh_os = nobjmsh_us = nobjmsh_sh = 0;
	bool bshadow = g_pOrbiter->Cfg()->CfgVisualPrm.bShadows;

	for (i = 0; i < nobj; i++) {
		BaseObject *bo = obj[i];
		bo->Activate();
		spec = bo->GetSpecs();
		if (spec & OBJSPEC_EXPORTVERTEX) {
			ng = bo->nGroup();
			for (j = 0; j < ng; j++) {
				if (bo->GetGroupSpec (j, nvtx, nidx, texid, undersh, groundsh)) {
					DWORD &ngrp     = (undersh ? ngrp_us : ngrp_os);
					GroupSpec *&grp = (undersh ? grp_us : grp_os);
					DWORD texidx = GetGenericTextureIdx (texid);
					for (k = 0; k < ngrp; k++) {
						if (grp[k].TexIdx == texidx && (grp[k].UsrFlag & 0x1) != groundsh) {
							grp[k].nVtx += nvtx;
							grp[k].nIdx += nidx;
							break;
						}
					}
					if (k == ngrp) { // a new vertex group
						GroupSpec *tmp = new GroupSpec[ngrp+1]; TRACENEW
						if (ngrp) {
							memcpy (tmp, grp, ngrp*sizeof(GroupSpec));
							delete []grp;
						}
						grp = tmp;
						grp[ngrp].nVtx = nvtx;
						grp[ngrp].nIdx = nidx;
						grp[ngrp].TexIdx = texidx;
						grp[ngrp].UsrFlag = (groundsh ? 0:1);
						ngrp++;
					}
				}
			}
		}
		if (spec & OBJSPEC_EXPORTMESH) {
			if (spec & OBJSPEC_UNDERSHADOW) nobjmsh_us++;
			else                            nobjmsh_os++;
		}
		if (bshadow && (spec & OBJSPEC_EXPORTSHADOWMESH)) nobjmsh_sh++;
	}
	for (i = 0; i < 2; i++) {
		DWORD &ngrp = (i==0 ? ngrp_us : ngrp_os);
		GroupSpec *&grp = (i==0 ? grp_us : grp_os);
		for (j = 0; j < ngrp; j++) {
			grp[j].Vtx = new NTVERTEX[grp[j].nVtx]; TRACENEW
			grp[j].Idx = new WORD[grp[j].nIdx]; TRACENEW
			grp[j].nVtx = 0;
			grp[j].nIdx = 0;
		}
	}
	if (ngrp_os) nobjmsh_os++;
	if (ngrp_us) nobjmsh_us++;
	if (nobjmsh_os) { objmsh_os = new Mesh*[nobjmsh_os]; nobjmsh_os = 0; TRACENEW }
	if (nobjmsh_us) { objmsh_us = new Mesh*[nobjmsh_us]; nobjmsh_us = 0; TRACENEW }
	if (nobjmsh_sh) { objmsh_sh = new Mesh*[nobjmsh_sh]; sh_elev = new double[nobjmsh_sh]; nobjmsh_sh = 0; TRACENEW }

	dentRange.clear ();
	for (i = 0; i < nobj; i++) {
		BaseObject *bo = obj[i];
		spec = bo->GetSpecs();
		if (spec & OBJSPEC_EXPORTVERTEX) {
			ng = bo->nGroup();
			for (j = 0; j < ng; j++) {
				bo->GetGroupSpec (j, nvtx, nidx, texid, undersh, groundsh);
				DWORD &ngrp     = (undersh ? ngrp_us : ngrp_os);
				GroupSpec *&grp = (undersh ? grp_us : grp_os);
				DWORD texidx = GetGenericTextureIdx (texid);
				for (k = 0; k < ngrp; k++) {
					if (grp[k].TexIdx == texidx && (grp[k].UsrFlag & 0x1) != groundsh) break;
				}
				dentRange.push_back ({ i, !undersh, (DWORD)-1, k, grp[k].nVtx, nvtx, grp[k].nIdx, nidx }); // not upstream: the generic mesh, its index set below
				bo->ExportGroup (j, grp[k].Vtx+grp[k].nVtx, grp[k].Idx+grp[k].nIdx, grp[k].nVtx);
				grp[k].nVtx += nvtx;
				grp[k].nIdx += nidx;
			}
		}
		if (spec & OBJSPEC_EXPORTMESH) {
			Mesh *em = bo->ExportMesh();
			bool after = !(spec & OBJSPEC_UNDERSHADOW);
			DWORD mi = after ? nobjmsh_os : nobjmsh_us;
			for (j = 0; em && j < em->nGroup(); j++) dentRange.push_back ({ i, after, mi, j, 0, em->GetGroup(j)->nVtx, 0, em->GetGroup(j)->nIdx }); // not upstream: its own mesh
			if (spec & OBJSPEC_UNDERSHADOW) objmsh_us[nobjmsh_us++] = em;
			else                            objmsh_os[nobjmsh_os++] = em;
		}
		if (bshadow && (spec & OBJSPEC_EXPORTSHADOWMESH)) {
			Mesh *shmsh = bo->ExportShadowMesh (sh_elev[nobjmsh_sh]);
			if (shmsh) objmsh_sh[nobjmsh_sh++] = shmsh;
		}
	}

	for (i = 0; i < 2; i++) {
		DWORD &ngrp = (i==0 ? ngrp_us : ngrp_os);
		if (ngrp) {
			GroupSpec *&grp = (i==0 ? grp_us : grp_os);
			Mesh *mesh = new Mesh; TRACENEW
			if (i==0) genmsh_us = mesh;
			else      genmsh_os = mesh;
			for (j = 0; j < ngrp; j++) {
				DWORD texidx = grp[j].TexIdx;
				int tidx = (texidx != (DWORD)-1 ? mesh->AddTexture (generic_dtex[texidx]) : SPEC_DEFAULT);
				// note: shallow copy - don't delete vertex and index arrays!
				int gidx = mesh->AddGroup (grp[j].Vtx, grp[j].nVtx, grp[j].Idx, grp[j].nIdx, SPEC_DEFAULT, tidx);
				mesh->GetGroup(gidx)->UsrFlag = grp[j].UsrFlag;
				// add night texture
				if (texidx != (DWORD)-1 && generic_ntex[texidx]) {
					tidx = mesh->AddTexture (generic_ntex[texidx]);
					mesh->GetGroup(gidx)->TexIdxEx[0] = tidx;
				}
			}
			delete []grp;
			grp = NULL;
			DWORD mi = (i==0 ? nobjmsh_us : nobjmsh_os);
			for (DentRange &r : dentRange) if (r.mesh == (DWORD)-1 && r.after == (i != 0)) r.mesh = mi; // not upstream
			if (i==0) objmsh_us[nobjmsh_us++] = mesh;
			else      objmsh_os[nobjmsh_os++] = mesh;
		}
	}
	objmsh_valid = true;
	serial_os.assign (nobjmsh_os, 0); serial_us.assign (nobjmsh_us, 0); // not upstream (coll-bounded 5.7)
	for (auto &d : dentObj) d.second.dirty = true; // records set before the structures existed
	dentDirty = !dentObj.empty ();
}

// not upstream (coll-bounded 5.7): building dents and holes, made in the core's update from each object's rest with the add-on's DentMath

bool Base::SetDent (const BASEDENTSPEC &d)
{
	if (d.size < sizeof (BASEDENTSPEC) || d.obj >= nobj) return false;
	DentObj &o = dentObj[d.obj];
	if (d.k > o.rec.size ()) return false;
	if (d.k == o.rec.size ()) o.rec.push_back (d);
	else if (!memcmp (&o.rec[d.k], &d, sizeof (BASEDENTSPEC))) return true;
	else o.rec[d.k] = d;
	o.dirty = dentDirty = true;
	return true;
}

void Base::ClearDents (DWORD o)
{
	for (auto &d : dentObj)
		if (o == (DWORD)-1 || d.first == o) { d.second.rec.clear (); d.second.dirty = dentDirty = true; }
}

DWORD Base::StructureSerial (DWORD mesh, bool after) const
{
	const std::vector<DWORD> &s = after ? serial_os : serial_us;
	return mesh < s.size () ? s[mesh] : 0;
}

void Base::ApplyDents ()
{
	if (!dentDirty || !objmsh_valid) return;
	dentDirty = false;
	for (auto it = dentObj.begin (); it != dentObj.end (); ) {
		if (it->second.dirty) ApplyDent (it->first, it->second);
		if (it->second.rec.empty ()) it = dentObj.erase (it); // restored: nothing kept
		else ++it;
	}
}

void Base::ApplyDent (DWORD o, DentObj &d)
{
	d.dirty = false;
	std::vector<DentRange *> rg;
	for (DentRange &r : dentRange) if (r.obj == o && r.after) rg.push_back (&r); // pads, runways and aprons under the shadows stay
	if (rg.empty ()) return;
	auto meshOf = [&] (const DentRange &r) -> Mesh * {
		Mesh **l = r.after ? objmsh_os : objmsh_us;
		DWORD n = r.after ? nobjmsh_os : nobjmsh_us;
		return r.mesh < n ? l[r.mesh] : 0;
	};
	if (!d.haveRest) { // the first damage: the object's groups as the structures hold them now
		d.rest.clear ();
		for (DentRange *r : rg) {
			Mesh *m = meshOf (*r);
			GroupSpec *gs = m ? m->GetGroup (r->grp) : 0;
			BaseDentGroup g;
			if (gs && r->v0 + r->nv <= gs->nVtx && r->i0 + r->ni <= gs->nIdx) {
				g.vtx.resize (r->nv);
				memcpy (g.vtx.data (), gs->Vtx + r->v0, r->nv * sizeof (NTVERTEX));
				for (DWORD k = 0; k < r->ni; k++) g.idx.push_back ((uint16_t)(gs->Idx[r->i0 + k] - r->v0));
				g.room = gs->nVtx < 65535 ? 65535 - gs->nVtx : 0;
			}
			d.rest.push_back (g);
		}
		d.haveRest = true;
	}
	std::vector<BaseDentGroup> out;
	if (d.rec.empty ()) out = d.rest;
	else {
		std::vector<BaseDentRec> rec;
		for (const BASEDENTSPEC &s : d.rec)
			rec.push_back ({ s.mode, s.seed, s.bits, s.flags, { s.c.x, s.c.y, s.c.z }, { s.n.x, s.n.y, s.n.z }, { s.t.x, s.t.y, s.t.z }, s.R, s.h, s.T, s.P, s.hd, s.hz });
		BaseDentApply (d.rest, rec, out);
	}
	std::vector<Mesh *> touched;
	for (size_t k = 0; k < rg.size () && k < out.size (); k++) {
		DentRange &r = *rg[k];
		Mesh *m = meshOf (r);
		GroupSpec *gs = m ? m->GetGroup (r.grp) : 0;
		if (!gs || r.v0 + r.nv > gs->nVtx || r.i0 + r.ni > gs->nIdx) continue;
		const BaseDentGroup &g = out[k];
		DWORD nv = (DWORD)g.vtx.size (), ni = (DWORD)g.idx.size ();
		DWORD nV = gs->nVtx - r.nv + nv, nI = gs->nIdx - r.ni + ni;
		if (nV > 65535) continue; // WORD indices
		NTVERTEX *V = new NTVERTEX[nV ? nV : 1]; TRACENEW
		WORD *I = new WORD[nI ? nI : 1]; TRACENEW
		memcpy (V, gs->Vtx, r.v0 * sizeof (NTVERTEX));
		if (nv) memcpy (V + r.v0, g.vtx.data (), nv * sizeof (NTVERTEX));
		memcpy (V + r.v0 + nv, gs->Vtx + r.v0 + r.nv, (gs->nVtx - r.v0 - r.nv) * sizeof (NTVERTEX));
		long dv = (long)nv - (long)r.nv;
		memcpy (I, gs->Idx, r.i0 * sizeof (WORD));
		for (DWORD q = 0; q < ni; q++) I[r.i0 + q] = (WORD)(g.idx[q] + r.v0);
		for (DWORD q = r.i0 + r.ni; q < gs->nIdx; q++) I[r.i0 + ni + (q - r.i0 - r.ni)] = (WORD)((long)gs->Idx[q] + (gs->Idx[q] >= r.v0 + r.nv ? dv : 0));
		delete []gs->Vtx; delete []gs->Idx;
		gs->Vtx = V; gs->Idx = I; gs->nVtx = nV; gs->nIdx = nI;
		long di = (long)ni - (long)r.ni;
		for (DentRange &x : dentRange) // the later slices of this group move
			if (&x != &r && x.after == r.after && x.mesh == r.mesh && x.grp == r.grp && x.v0 >= r.v0 + r.nv) { x.v0 = (DWORD)((long)x.v0 + dv); x.i0 = (DWORD)((long)x.i0 + di); }
		r.nv = nv; r.ni = ni;
		std::vector<DWORD> &s = r.after ? serial_os : serial_us;
		if (r.mesh < s.size ()) s[r.mesh]++;
		if (std::find (touched.begin (), touched.end (), m) == touched.end ()) touched.push_back (m);
	}
	for (Mesh *m : touched) m->Setup ();
}

void Base::Update (bool force)
{
	s1->R.Set (rrot);
	s1->R.premul (cbody->s1->R);
	s1->Q.Set (s1->R);
	// WARNING: this should work the other way round: combine the
	// two quaternions and extract grot

	s1->pos.Set (mul (cbody->s1->R, rpos) + cbody->s1->pos);
	s1->vel.Set (mul (cbody->s1->R, rotvel) + cbody->s1->vel);

	ApplyDents (); // not upstream (coll-bounded 5.7)

	// periodically update some secondary parameters
	if (td.SimT1 > sundir_updt || force) {
		sundir = SunDirection();
		double csun = sundir.y;
		sundir_updt = td.SimT1 + (csun < -0.1 ? 50.0 : csun > 0.1 ? 10.0 : 2.0);
	}
}

void Base::Rel_EquPos (const Vector &relpos, double &_lng, double &_lat) const
{
	_lng = lng + relpos.z/(cbody->Size()*cos(lat));
	_lat = lat - relpos.x/cbody->Size();
}

void Base::Pad_EquPos (DWORD padno, double &_lng, double &_lat) const
{
	_lng = lng;
	_lat = lat;
	if (padno < npad) {
		_lng += lspec[padno].relpos.z/(cbody->Size()*cos(lat));
		_lat -= lspec[padno].relpos.x/cbody->Size();
	}
}

bool Base::GetGenericTexture (LONGLONG id, SURFHANDLE &daytex, SURFHANDLE &nighttex) const
{
	for (int i = 0; i < ngenerictex; i++)
		if (id == generic_tex_id[i]) {
			daytex   = generic_dtex[i];
			nighttex = generic_ntex[i];
			return true;
		}
	daytex = nighttex = 0;
	return false;
}

DWORD Base::GetGenericTextureIdx (LONGLONG id) const
{
	for (DWORD i = 0; i < ngenerictex; i++)
		if (id == generic_tex_id[i])
			return i;
	return (DWORD)-1;
}

int Base::OccupyPad (Vessel *vessel, int pad, bool forcepad)
{
	int pd;
	DWORD i;

	if (forcepad) {
		if (pad < 0 || (DWORD)pad >= npad) return -1; // not upstream: a pad that doesn't exist counts as no pad (lspec has npad entries, NULL for 0)
		if (lspec[pad].status == 0) padfree--;
		lspec[pad].status = 1;
		lspec[pad].vessel = vessel;
		return pad;
	}
	if (!padfree) return -1;
	if (pad >= 0 && (DWORD)pad < npad && lspec[pad].status == 0) { // not upstream: a pad that doesn't exist counts as no pad (random free pad)
		lspec[pad].status = 1;
		lspec[pad].vessel = vessel;
		padfree--;
		return pad;
	}
	// pick random free pad
	pd = (int)(((double)rand()*padfree)/((double)RAND_MAX+1.0)); // RAND_MAX is 2^31-1 in glibc (32767 in MSVC): the int expression overflowed
	for (i = 0; i < npad; i++) {
		if (lspec[i].status == 0) {
			if (!pd--) {
				lspec[i].status = 1;
				lspec[i].vessel = vessel;
				padfree--;
				return (int)i;
			}
		}
	}
	// should not get here
	return -1;
}

int Base::OccupyPad (Vessel *vessel, double _lng, double _lat)
{
	const double padrad = 20.0; // landing pad size
	double plng, plat, dist, bdir;
	for (DWORD i = 0; i < npad; i++) {
		Pad_EquPos (i, plng, plat);
		Orthodome (_lng, _lat, plng, plat, dist, bdir);
		dist *= cbody->Size();
		if (dist < padrad) {
			if (lspec[i].status == 0) {
				lspec[i].status = 1;
				lspec[i].vessel = vessel;
				padfree--;
				return (int)i;
			}
		}
	}
	return -1; // failure: pad not free
}

void Base::ClearPad (Vessel *vessel)
{
	for (DWORD i = 0; i < npad; i++)
		if (lspec[i].status && lspec[i].vessel == vessel) {
			lspec[i].status = 0;
			lspec[i].vessel = 0;
			padfree++;
		}
}

int Base::LandedAtPad (const Vessel *vessel) const
{
	for (DWORD i = 0; i < npad; i++)
		if (lspec[i].status == 1 && lspec[i].vessel == vessel)
			return (int)i;
	return -1;
}

int Base::RequestLanding (Vessel *vessel, DWORD &padno)
{
	if (rand() < RAND_MAX/2) return 2; // keep pending
	if (!padfree) return 1;     // deny
	int pd = (int)(((double)rand()*padfree)/((double)RAND_MAX+1.0)); // glibc RAND_MAX: see OccupyPad
	for (DWORD i = 0; i < npad; i++) {
		if (lspec[i].status == 0) {
			if (!pd--) {
				lspec[i].status = 2;
				lspec[i].vessel = vessel;
				padfree--;
				padno = i;
				return 4; // grant clearance
			}
		}
	}
	// should not get here
	return 2;
}

int Base::RequestTakeoff ()
{
	if (rand() < RAND_MAX/2) return 3; // keep pending
	return 5;                   // grant clearance
}

void Base::ReportTakeoff (Vessel *vessel)
{
	for (DWORD i = 0; i < npad; i++) {
		if (lspec[i].status == 1 && lspec[i].vessel == vessel) {
			lspec[i].status = 0;
			lspec[i].vessel = 0;
			padfree++;
		}
	}
}

int Base::ReportTouchdown (VesselBase *vessel, double vlng, double vlat)
{
	const double padrad = 30.0; // landing pad size
	double plng, plat, dist, bdir;
	DWORD i, padno = (DWORD)-1;
	for (i = 0; i < npad; i++) {
		Pad_EquPos (i, plng, plat);
		Orthodome (vlng, vlat, plng, plat, dist, bdir);
		dist *= cbody->Size();
		if (dist < padrad) {
			if (lspec[i].status == 0) padfree--;
			// else lspec[i].vessel->CancelClearance();
			lspec[i].status = 1;
			lspec[i].vessel = vessel;
			padno = i;
			break;
		}
	}
	if (padno != (DWORD)-1) { // cancel all other requests for this vessel
		if (g_pOrbiter->Cfg()->CfgLogicPrm.bPadRefuel)
			vessel->Refuel();
		for (i = 0; i < npad; i++) {
			if (i == padno) continue;
			if (lspec[i].status && lspec[i].vessel == vessel) {
				lspec[i].status = 0;
				lspec[i].vessel = 0;
				padfree++;
			}
		}
	}
	return (int)padno;
}

double Base::CosSunAlt () const
{
	return (s0->R.m12*s0->pos.x + s0->R.m22*s0->pos.y + s0->R.m32*s0->pos.z) / (-s0->pos.length());
}
