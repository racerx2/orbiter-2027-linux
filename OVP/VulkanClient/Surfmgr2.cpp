// ==============================================================
//   ORBITER VISUALISATION PROJECT (OVP)
//   Copyright (C) 2006-2026 Martin Schweiger
//   Dual licensed under GPL v3 and LGPL v3
// ==============================================================

// ==============================================================
// surfmgr2.cpp
// class SurfaceManager2 (implementation)
//
// Planetary surface rendering engine v2, including a simple
// LOD (level-of-detail) algorithm for surface patch resolution.
// ==============================================================

#include "Surfmgr2.h"
#include "Tilemgr2.h"
#include "Cloudmgr2.h"
#include "D3D9Catalog.h"
#include "D3D9Config.h"
#include "VVessel.h"
#include "VectorHelpers.h"
#include "DebugControls.h"
#include "gcCore.h"
#include "D3D9Surface.h"
#include "VkTexFile.h"
#include "GfxPool.h" // not upstream
#include "CloudAtlas.h" // not upstream
#include "SceneFx.h" // not upstream: DepthWanted
#include "FogMath.h" // not upstream: ElevGrids' tile range
#include "SceneShadows.h" // not upstream: scene shadow casters and receivers
#include "SceneSsr.h" // not upstream: the water's reflections
#include <functional> // not upstream
#include <deque> // not upstream
#include <filesystem> // not upstream
#include <mutex> // not upstream
#include <sys/stat.h> // not upstream

// =======================================================================
extern void FilterElevationGraphics(OBJHANDLE hPlanet, int lvl, int ilat, int ilng, float *elev);

// not upstream: g++, specializations declared before their use below
template<> SurfTile *TileManager2<SurfTile>::NodeAt (int ilvl, int ilat, int ilng);
template<> void TileManager2<SurfTile>::HorizonRequest (SurfTile *o, double dist);
template<> void TileManager2<SurfTile>::HorizonSubmit ();
template<> void TileManager2<SurfTile>::HorizonUpload ();
template<> void TileManager2<SurfTile>::RenderDepth (ShaderClass *sh, const D3DXMATRIX &mVP);
template<> void TileManager2<SurfTile>::RenderCasters (ShaderClass *sh, MATRIX4 &dwmat, const vPlanet::RenderPrm &rprm, const SceneShadows &s, double minSpacing);
template<> std::shared_ptr<const Horizon::Map> TileManager2<SurfTile>::HorizonAt (double lat, double lng, double bnd[4]);

#pragma pack(push, 4)
struct LightF
{
	float3   position[4];         /* position in world space */
	float3   direction[4];        /* direction in world space */
	float3   diffuse[4];          /* diffuse color of light */
	float3   attenuation[4];      /* Attenuation */
	float4   param[4];            /* range, falloff, theta, phi */
};
#pragma pack(pop)


// =======================================================================
// Utility functions

static void VtxInterpolate (VERTEX_2TEX &res, const VERTEX_2TEX &a, const VERTEX_2TEX &b, double w)
{
	float w1 = (float)w;
	float w0 = 1.0f-w1;
	res.x = a.x*w0 + b.x*w1;
	res.y = a.y*w0 + b.y*w1;
	res.z = a.z*w0 + b.z*w1;
	res.nx = a.nx*w0 + b.nx*w1;
	res.ny = a.ny*w0 + b.ny*w1;
	res.nz = a.nz*w0 + b.nz*w1;
}


int compare_lights(const void * a, const void * b);

static bool ElevNodeOk (const BYTE *buf, DWORD ndata, int ndat) // not upstream: a .tree elevation node must hold its header and ndat samples of a known dtype, else it counts as no tile
{
	ELEVFILEHEADER hdr;
	if (ndata < sizeof(ELEVFILEHEADER)) return false;
	memcpy(&hdr, buf, sizeof(ELEVFILEHEADER));
	if (hdr.hdrsize < 0 || (DWORD)hdr.hdrsize > ndata) return false;
	if (hdr.dtype != 0 && hdr.dtype != 8 && hdr.dtype != -16) return false;
	return ndata - (DWORD)hdr.hdrsize >= (DWORD)ndat * (hdr.dtype == 8 ? 1 : hdr.dtype == -16 ? 2 : 0);
}

static uint64_t FileStamp (const char *path) // not upstream: inode, mtime and size, 0 when missing; each new stream file is renamed into place
{
	struct stat st;
	if (stat(oapiResolvePath(path).c_str(), &st) != 0) return 0;
	uint64_t t = (uint64_t)st.st_mtim.tv_sec * 1000000000u + (uint64_t)st.st_mtim.tv_nsec;
	uint64_t h = (t * 1000003u + (uint64_t)st.st_size) ^ ((uint64_t)st.st_ino * 0x9E3779B97F4A7C15ull);
	return h ? h : 1;
}


// =======================================================================
// =======================================================================

SurfTile::SurfTile (TileManager2Base *_mgr, int _lvl, int _ilat, int _ilng)
: Tile (_mgr, _lvl, _ilat, _ilng)
{
	smgr = static_cast<TileManager2<SurfTile>* > (_mgr);
	node = 0;
	streamWait = false; // not upstream
	looseTex = false; // not upstream
	texStamp = 0; // not upstream
	touchT = -1e9; // not upstream
	touchF = 0; // not upstream
	static std::atomic<unsigned> horGens{0}; // not upstream: horizon maps
	elevReady.store(false);
	horGen = ++horGens;
	htex = NULL;
	horAsk = -1.0;
	horCheck = 0.0;
	horMissing = 0;
	horRedone = false;
	elev = NULL;
	ggelev = NULL;
	elev_file = NULL;
	ltex = NULL;
	ctex = NULL; // not upstream
	has_elevfile = false;
	label = NULL;
	imicrolvl = 14;	// Water resolution level
	MaxRep = mgr->Client()->GetFramework()->GetCaps()->MaxTextureRepeat;
	if (Config->TileMipmaps == 1) bMipmaps = true;

	memset(&ehdr, 0, sizeof(ELEVFILEHEADER));
}

// -----------------------------------------------------------------------

SurfTile::~SurfTile ()
{
	smgr->GetPlanet()->TileDeleted(this);

	if (elev) g_pMemgr_f->Free(elev);
	if (elev_file) g_pMemgr_i->Free(elev_file);	
	if (tex && owntex) g_pTexmgr_tt->Free(tex);
	if (ltex && owntex) g_pTexmgr_tt->Free(ltex);
	if (ctex && owntex) g_pTexmgr_tt->Free(ctex); // not upstream
	delete htex; // not upstream
		
	DeleteLabels();
}

// -----------------------------------------------------------------------

void SurfTile::PreLoad()
{
	char fname[128];
	char path[MAX_PATH];
	
	owntex = true;

	assert(tex == nullptr);
	assert(ltex == nullptr);
	assert(ctex == nullptr); // not upstream

	VkDev *pDev = mgr->Dev();
	VkPixels *pSysSrf = nullptr; // D3DPOOL_SYSTEMMEM texture
	
	// Configure microtexture range for "Water texture" and "Cloud microtexture".
	GetParentMicroTexRange(&microrange);
	GetParentOverlayRange(&overlayrange);

	// Load surface texture

	uint64_t stamp = 0; // not upstream
	if (smgr->DoLoadIndividualFiles(0)) { // try loading from individual tile file
		snprintf(path, MAX_PATH, "%s\\Surf\\%02d\\%06d\\%06d.dds", mgr->DataRootDir().c_str(), lvl + 4, ilat, ilng);
		if (smgr->bStream) stamp = FileStamp(path); // not upstream: before the read, so a rewrite after it is not taken as read
		LoadTextureFile(path, &pSysSrf);
		if (pSysSrf && smgr->bStream && lvl >= 0) oapiTileStreamWant(mgr->Cbody(), lvl + 4, ilat, ilng, 2); // not upstream: in use, the size cap takes it later
	}
	bool loose = pSysSrf != nullptr; // not upstream
	if (!pSysSrf && smgr->ZTreeManager(0)) { // try loading from compressed archive
		BYTE *buf;
		DWORD ndata = smgr->ZTreeManager(0)->ReadData(lvl+4, ilat, ilng, &buf);
		if (ndata) {
			LoadTextureFromMemory(buf, ndata, &pSysSrf);
			smgr->ZTreeManager(0)->ReleaseData(buf);
		}
	}
	if (!pSysSrf && smgr->bStream && lvl >= 0) { // not upstream: the core's tile stream writes this tile into the tile folder
		int r = oapiTileStreamWant(mgr->Cbody(), lvl + 4, ilat, ilng, 0);
		if (r > 0) { // arrived since the read above
			snprintf(path, MAX_PATH, "%s\\Surf\\%02d\\%06d\\%06d.dds", mgr->DataRootDir().c_str(), lvl + 4, ilat, ilng);
			stamp = FileStamp(path);
			LoadTextureFile(path, &pSysSrf);
			if (!pSysSrf) r = oapiTileStreamWant(mgr->Cbody(), lvl + 4, ilat, ilng, 1);
			loose = pSysSrf != nullptr;
		}
		streamWait = (r >= 0 && !pSysSrf);
	}
	
	if (CreateTexture(pDev, pSysSrf, &tex) != true) {
		if (GetParentSubTexRange(&texrange)) {
			tex = getSurfParent()->Tex();
			owntex = false;
		}
	}
	looseTex = smgr->bStream && loose && tex && owntex; // not upstream: a rewrite of the loose file is swapped in (StreamApply)
	texStamp = looseTex ? stamp : 0; // not upstream
	
	SAFE_DELETE(pSysSrf); // Release

	if (smgr->bStream && tex) { // not upstream: the tile stream's land class weights go with the Surf tile, L >= 12
		if (owntex && lvl + 4 >= 12) {
			snprintf(path, MAX_PATH, "%s\\Class\\%02d\\%06d\\%06d.dds", mgr->DataRootDir().c_str(), lvl + 4, ilat, ilng);
			LoadTextureFile(path, &pSysSrf);
			if (pSysSrf) CreateTexture(pDev, pSysSrf, &ctex);
			SAFE_DELETE(pSysSrf);
		}
		else if (!owntex && node && node->Parent()) ctex = getSurfParent()->ctex;
	}

	// Load mask texture

	if (tex && (mgr->Cprm().bSpecular || mgr->Cprm().bLights))
	{
		if (smgr->DoLoadIndividualFiles(1)) { // try loading from individual tile file
			snprintf(path, MAX_PATH, "%s\\Mask\\%02d\\%06d\\%06d.dds", mgr->DataRootDir().c_str(), lvl + 4, ilat, ilng);
			LoadTextureFile(path, &pSysSrf);
		}
		if (!pSysSrf && smgr->ZTreeManager(1)) { // try loading from compressed archive
			BYTE* buf;
			DWORD ndata = smgr->ZTreeManager(1)->ReadData(lvl + 4, ilat, ilng, &buf);
			if (ndata) {
				LoadTextureFromMemory(buf, ndata, &pSysSrf);
				smgr->ZTreeManager(1)->ReleaseData(buf);
			}
		}
		
		if (owntex) {
			CreateTexture(pDev, pSysSrf, &ltex);			
		}
		else if (node && node->Parent()) {
			ltex = getSurfParent()->ltex;
		}		
		
		SAFE_DELETE(pSysSrf); // Release
	}
}

// -----------------------------------------------------------------------

void SurfTile::Load ()
{
	// Load elevation data
	float *elev = ElevationData ();

	bool shift_origin = (lvl >= 4);
	int res = mgr->GridRes();

	if (lvl <= 0)
	{
		if (!lvl) { // create hemisphere mesh for western or eastern hemispheres
			mesh = CreateMesh_hemisphere(res, elev, 0.0);
	//  } else {    // create full sphere mesh
	//	  // TODO
		}
	} else {
		// create rectangular patch
		mesh = CreateMesh_quadpatch (res, res, elev, 1.0, 0.0, &texrange, shift_origin, &vtxshift, mgr->GetPlanet()->prm.tilebb_excess);
	}

	CreateLabels();
}

// -----------------------------------------------------------------------

// not upstream: the stream's Elev of the parent (past its last level, of the ancestor there) hides the archive's surface model tile; as elevmgr's StreamCovers
static bool StreamCovers (const TileManager2Base *mgr, int lvl, int ilat, int ilng)
{
	int m = mgr->streamElevMax;
	if (!m || lvl <= 4) return false;
	int d = lvl > m ? lvl - m : 1;
	char path[MAX_PATH];
	snprintf(path, MAX_PATH, "%s\\Elev\\%02d\\%06d\\%06d.elv", mgr->DataRootDir().c_str(), lvl - d, ilat >> d, ilng >> d);
	return FileExists(path);
}

INT16 *SurfTile::ReadElevationFile (const char *name, int lvl, int ilat, int ilng)
{
	const int ndat = TILE_ELEVSTRIDE*TILE_ELEVSTRIDE;
	INT16 *e = NULL;

	// Elevation resolution used for "rounding" due to INT16 elevation. 
	// Technically, should not apply to float based elevation but required due to rounding in physics.
	double tgt_res = mgr->ElevRes();

	char path[MAX_PATH];
	char fname[128];
	FILE *f;
	int i;

	// Elevation data
	if (smgr->DoLoadIndividualFiles(2)) { // try loading from individual tile file
		snprintf(path, MAX_PATH, "%s\\Elev\\%02d\\%06d\\%06d.elv", mgr->DataRootDir().c_str(), lvl, ilat, ilng);
		// read the elevation file header
		ELEVFILEHEADER hdr0 = ehdr; // not upstream: put back if the file is short
		if ((f = fopen(oapiResolvePath(path).c_str(), "rb")) && fread (&ehdr, sizeof(ELEVFILEHEADER), 1, f) != 1) { ehdr = hdr0; fclose (f); f = NULL; } // not upstream: a short header counts as no file
		if (f) {
			bool rd = true; // not upstream: false after a short data read
			e = g_pMemgr_i->New(ndat);
			elev = g_pMemgr_f->New(ndat);
			if (ehdr.hdrsize != sizeof(ELEVFILEHEADER)) fseek (f, ehdr.hdrsize, SEEK_SET);
			LogClr("Teal", "NewTile[%s]: Lvl=%d, Scale=%g, Offset=%g", name, lvl-4, ehdr.scale, ehdr.offset);

#ifdef ORBITER2016
			ehdr.scale = 1.0;
#endif
			switch (ehdr.dtype) {
			case 0: // flat tile, defined by offset
				for (i = 0; i < ndat; i++) e[i] = 0;
				break;
			case 8: {
				UINT8 *tmp = g_pMemgr_u->New(ndat);
				rd = fread (tmp, sizeof(UINT8), ndat, f) == (size_t)ndat;
				for (i = 0; rd && i < ndat; i++)
					e[i] = (INT16)tmp[i];
				g_pMemgr_u->Free(tmp);
				tmp = NULL;
				}
				break;
			case -16:
				rd = fread (e, sizeof(INT16), ndat, f) == (size_t)ndat;
				break;
			default: rd = false; break; // not upstream: an unknown dtype counts as missing (e holds a recycled buffer's data)
			}
			fclose (f);
			if (!rd) { g_pMemgr_i->Free(e); e = NULL; g_pMemgr_f->Free(elev); elev = NULL; ehdr = hdr0; } // not upstream: a short file counts as missing, the archive is tried next
		}
	}
	bool looseElev = e != NULL; // not upstream
	if (!looseElev && smgr->streamElevMax && lvl >= 4 && lvl <= smgr->streamElevMax) oapiTileStreamWant(mgr->Cbody(), lvl, ilat, ilng, 4); // not upstream: the stream makes it, used when the tile is made again
	if (!e && smgr->ZTreeManager(2) && !StreamCovers(mgr, lvl, ilat, ilng)) { // try loading from compressed archive; not upstream: StreamCovers
		BYTE *buf;
		DWORD ndata = smgr->ZTreeManager(2)->ReadData(lvl, ilat, ilng, &buf);
		if (ndata && !ElevNodeOk(buf, ndata, ndat)) { smgr->ZTreeManager(2)->ReleaseData(buf); ndata = 0; } // not upstream: a short or corrupt node is no tile, ehdr keeps its value
		if (ndata) {
			BYTE *p = buf;
			e = g_pMemgr_i->New(ndat);
			elev = g_pMemgr_f->New(ndat);
			memcpy(&ehdr, p, sizeof(ELEVFILEHEADER));
			LogClr("Teal", "NewTileA[%s]: Lvl=%d, Scale=%g, Offset=%g", name, lvl-4, ehdr.scale, ehdr.offset);

#ifdef ORBITER2016
			ehdr.scale = 1.0;
#endif
			p += ehdr.hdrsize;
			switch (ehdr.dtype) {
			case 0:
				for (i = 0; i < ndat; i++) e[i] = 0;
				break;
			case 8:
				for (i = 0; i < ndat; i++)
					e[i] = (INT16)(*p++);
				break;
			case -16:
				memcpy(e, p, ndat*sizeof(INT16));
				p += ndat*sizeof(INT16);
				break;
			}
			smgr->ZTreeManager(2)->ReleaseData(buf);
		}
	}

	if (e) {
	
		if (ehdr.scale != tgt_res) { // rescale the data
			double rescale = ehdr.scale / tgt_res;
			for (i = 0; i < ndat; i++)
				e[i] = (INT16)(e[i] * rescale);
		}
		if (ehdr.offset) {
			INT16 sofs = (INT16)(ehdr.offset / tgt_res);
			for (i = 0; i < ndat; i++)
				e[i] += sofs;
		}

		// Convert to float
		for (i = 0; i < ndat; i++) elev[i] = float(e[i]) * float(tgt_res);	
	}

	// Elevation mod data
	if (e) {
		bool ok = false;
		double rescale;
		INT16 offset;
		bool do_rescale, do_shift;
		ELEVFILEHEADER hdr;
		if (smgr->DoLoadIndividualFiles(3)) { // try loading from individual tile file
			snprintf (path, MAX_PATH, "%s\\Elev_mod\\%02d\\%06d\\%06d.elv", mgr->DataRootDir().c_str(), lvl, ilat, ilng);
			if ((f = fopen(oapiResolvePath(path).c_str(), "rb")) && fread (&hdr, sizeof(ELEVFILEHEADER), 1, f) != 1) { fclose (f); f = NULL; } // not upstream: a short header counts as no file
			if (f) {
				bool rd = true; // not upstream: false after a short data read
				if (hdr.hdrsize != sizeof(ELEVFILEHEADER)) fseek (f, hdr.hdrsize, SEEK_SET);
				LogClr("Teal", "NewElevMod[%s]: Lvl=%d, Scale=%g, Offset=%g", name, lvl - 4, hdr.scale, hdr.offset);

#ifdef ORBITER2016
				hdr.scale = 1.0;
#endif
				rescale = (do_rescale = (hdr.scale != tgt_res)) ? hdr.scale/tgt_res : 1.0;
				offset = (do_shift = (hdr.offset != 0.0)) ? INT16(hdr.offset/tgt_res) : 0;

				switch (hdr.dtype) {
				case 0: // overwrite the entire tile with a flat offset
					for (i = 0; i < ndat; i++) e[i] = offset, elev[i] = float(hdr.offset);
					break;
				case 8: {
					const UINT8 mask = UCHAR_MAX;
					UINT8 *tmp = g_pMemgr_u->New(ndat);
					rd = fread (tmp, sizeof(UINT8), ndat, f) == (size_t)ndat;
					for (i = 0; rd && i < ndat; i++) {
						if (tmp[i] != mask) {
							e[i] = (INT16)(do_rescale ? (INT16)(tmp[i] * rescale) : (INT16)tmp[i]);
							if (do_shift) e[i] += offset;
							elev[i] = float(e[i]) * float(tgt_res);
						}
					}
					g_pMemgr_u->Free(tmp);
					tmp = NULL;
					}
					break;
				case -16: {
					const INT16 mask = SHRT_MAX;
					INT16* tmp = g_pMemgr_i->New(ndat);
					rd = fread (tmp, sizeof(INT16), ndat, f) == (size_t)ndat;
					for (i = 0; rd && i < ndat; i++) {
						if (tmp[i] != mask) {
							e[i] = (do_rescale ? (INT16)(tmp[i] * rescale) : tmp[i]);
							if (do_shift) e[i] += offset;
							elev[i] = float(e[i]) * float(tgt_res);
						}
					}
					g_pMemgr_i->Free(tmp);
					tmp = NULL;
					}
					break;
				}
				fclose(f);
				ok = rd; // not upstream: a short file is not applied, the archive is tried
			}
		}
		if (!ok && smgr->ZTreeManager(3) && !(looseElev && smgr->streamElevMax)) { // try loading from compressed archive; not upstream: the archive's Elev_mod only over the archive's Elev
			BYTE *buf;
			DWORD ndata = smgr->ZTreeManager(3)->ReadData(lvl, ilat, ilng, &buf);
			if (ndata && !ElevNodeOk(buf, ndata, ndat)) { smgr->ZTreeManager(3)->ReleaseData(buf); ndata = 0; } // not upstream: a short or corrupt node is no mod
			if (ndata) {
				BYTE *p = buf;
				ELEVFILEHEADER *phdr = (ELEVFILEHEADER*)p;
				LogClr("Teal", "NewElevModA[%s]: Lvl=%d, Scale=%g, Offset=%g", name, lvl - 4, phdr->scale, phdr->offset);

#ifdef ORBITER2016
				phdr->scale = 1.0;
#endif
				p += phdr->hdrsize;
				rescale = (do_rescale = (phdr->scale != tgt_res)) ? phdr->scale/tgt_res : 1.0;
				offset = (do_shift = (phdr->offset != 0.0)) ? INT16(phdr->offset/tgt_res) : 0;

				switch(phdr->dtype) {
				case 0:
					for (i = 0; i < ndat; i++) e[i] = offset, elev[i] = float(phdr->offset);
					break;
				case 8: {
					const UINT8 mask = UCHAR_MAX;
					for (i = 0; i < ndat; i++) {
						if (p[i] != mask) {
							e[i] = (INT16)(do_rescale ? p[i] * rescale : p[i]);
							if (do_shift) e[i] += offset;
							elev[i] = float(e[i]) * float(tgt_res);
						}
					}
					} break;
				case -16: {
					const INT16 mask = SHRT_MAX;
					INT16 v; // not upstream: each sample copied, p is odd after an odd hdrsize
					for (i = 0; i < ndat; i++) {
						memcpy(&v, p + i * sizeof(INT16), sizeof(INT16)); // not upstream: as above
						if (v != mask) { // not upstream: v
							e[i] = (do_rescale ? (INT16)(v * rescale) : v); // not upstream: v
							if (do_shift) e[i] += offset;
							elev[i] = float(e[i]) * float(tgt_res);
						}
					}
					} break;
				}
				smgr->ZTreeManager(3)->ReleaseData(buf);
			}
		}
		if (Config->bFlats) FilterElevationGraphics(mgr->GetPlanet()->Object(), lvl - 4, ilat, ilng, elev);
	}
	return e;
}

// -----------------------------------------------------------------------

VkTex *SurfTile::SetOverlay(VkTex *pOverlay, bool bOwn)
{
	VkTex *pRet = NULL;
	if (bOwn && ownoverlay && overlay) pRet = overlay;

	overlay = pOverlay;
	ownoverlay = bOwn;

	for (int i = 0; i < 4; i++) {
		auto x = node->Child(i);
		if (x) if (x->Entry()) {
			VkTex *pOld = x->Entry()->overlay;
			if (pOld == NULL || pOld == pRet) {
				x->Entry()->GetParentOverlayRange(&overlayrange);
				x->Entry()->SetOverlay(pOverlay, false);
			}
		}
	}
	return pRet;
}

// -----------------------------------------------------------------------

bool SurfTile::DeleteOverlay(VkTex *pOverlay)
{
	bool bReturn = false;
	if (pOverlay == NULL) pOverlay = overlay;

	if (overlay) {
		if (pOverlay == overlay) {
			if (ownoverlay) {
				delete overlay; // Release
				bReturn = true;
			}
			for (int i = 0; i < 4; i++) {
				auto x = node->Child(i);
				if (x) if (x->Entry()) x->Entry()->DeleteOverlay(pOverlay);
			}
			overlay = NULL;
			ownoverlay = false;
		}
	}
	return bReturn;
}


// -----------------------------------------------------------------------

float SurfTile::Interpolate(FMATRIX4 &in, float t, float u)
{
	return 0.0f;
}

// -----------------------------------------------------------------------

bool SurfTile::InterpolateElevationGrid(const float *in, float *out)
{
	int q0 = 0, c = 129;

	if (!(ilat & 1)) q0 = TILE_ELEVSTRIDE * 128;
	if ((ilng & 1)) q0 += 128;

	for (int i = 0; i <= c; i++)
	{
		int q1 = q0 + 1;
		int q2 = q0 + TILE_ELEVSTRIDE;
		int q3 = q1 + TILE_ELEVSTRIDE;
		int x = (TILE_ELEVSTRIDE << 1) * i;

		for (int k = 0; k <= c; k++)
		{
			float f0 = in[k + q0];	float f1 = in[k + q1];
			float f2 = in[k + q2];	float f3 = in[k + q3];

			out[x + 0] = (f0 + f1 + f2 + f3) * 0.25f;
			if (k != c) out[x + 1] = (f1 + f3) * 0.5f;

			if (i != c) {
				out[x + TILE_ELEVSTRIDE] = (f2 + f3) * 0.5f;
				if (k != c) out[x + TILE_ELEVSTRIDE + 1] = f3;
			}
			x += 2;
		}
		q0 += TILE_ELEVSTRIDE;
	}
	return true;
}


// -----------------------------------------------------------------------

bool SurfTile::LoadElevationData ()
{
	// Note: a tile's elevation data are retrieved from its great-grandparent tile. Each tile stores the elevation
	// data for its area at 8x higher resolution than required by itself, so they can be used by the grandchildren.
	// This reduces the number of elevation files, by storing more information in each. It also has a caching effect:
	// Once a grandparent has loaded its elevation data on request of a grandchild, any other grandchildren's requests
	// can be served directly without further disk I/O.

	if (elev) return true; // already present

	has_elevfile = false;
	int mode = mgr->Cprm().elevMode;
	if (!mode) return false;

	DWORD phy_lvl = mgr->GetPlanet()->GetPhysicsPatchRes();
	int ndat = TILE_ELEVSTRIDE*TILE_ELEVSTRIDE;

	elev_file = ReadElevationFile (mgr->CbodyName(), lvl + 4, ilat, ilng);
	double tgt_res = mgr->ElevRes();

	if (elev_file) has_elevfile = true;
	else if (lvl > 0) {

		// Acquire elev header data from a parent
		QuadTreeNode<SurfTile> *parent = node->Parent();
		if (parent &&  parent->Entry()) memcpy(&ehdr, &parent->Entry()->ehdr, sizeof(ELEVFILEHEADER));

		// construct elevation grid by interpolating ancestor data
		ELEVHANDLE hElev = mgr->ElevMgr();
		if (!hElev) return false;

		// Cubic Interpolation
		if (mode == 2) {

			int plvl = lvl-1;
			int pilat = ilat >> 1;
			int pilng = ilng >> 1;
			INT16 *pelev_file = 0;
			QuadTreeNode<SurfTile> *parent = node->Parent();
			for (; plvl >= 0; plvl--) { // find ancestor with elevation data
				if (parent && parent->Entry()->has_elevfile) {
					pelev_file = parent->Entry()->elev_file;
					break;
				}
				if (parent) parent = parent->Parent();
				pilat >>= 1;
				pilng >>= 1;
			}

			if (!pelev_file) return false;

			elev = g_pMemgr_f->New(ndat);

			// submit ancestor data to elevation manager for interpolation
			mgr->GetClient()->ElevationGrid(hElev, ilat, ilng, lvl, pilat, pilng, plvl, pelev_file, elev);

			// Convert to float
			for (int i = 0; i < ndat; i++) elev[i] *= float(tgt_res);
		}

		// Experimental Linear Interpolation
		else {
			QuadTreeNode<SurfTile> *parent = node->Parent();
			if (parent &&  parent->Entry()->elev) {
				elev = g_pMemgr_f->New(ndat);
				InterpolateElevationGrid(parent->Entry()->elev, elev);
			}
		}
	}

	if (has_elevfile) LogClr("Teal", "TileCreatedFromFile: Level=%d, ilat=%d, ilng=%d", lvl, ilat, ilng);
	else LogClr("Teal", "TileInterpolatedFromParent: Level=%d, ilat=%d, ilng=%d", lvl, ilat, ilng);

	if (elev) elevReady.store(true, std::memory_order_release); // not upstream: horizon jobs copy it from the render thread
	return (elev != 0);
}

// -----------------------------------------------------------------------

float *SurfTile::ElevationData () const
{
	if (!ggelev) {
		int ancestor_dlvl = 3;
		while (1 << (8-ancestor_dlvl) < mgr->GridRes()) ancestor_dlvl--;
		if (lvl >= ancestor_dlvl) { // traverse quadtree back to great-grandparent
			QuadTreeNode<SurfTile> *ancestor = node; // start at my own node
			int blockRes = TILE_FILERES;
			while (blockRes > mgr->GridRes() && ancestor) {
				ancestor = ancestor->Parent();
				blockRes >>= 1;
			}
			if (ancestor && ancestor->Entry()->LoadElevationData()) {
				// compute pixel offset into great-grandparent tile set
				int nblock = TILE_FILERES/blockRes;
				int mask = nblock-1;
				int ofs = ((mask - ilat & mask) * TILE_ELEVSTRIDE + (ilng & mask)) * blockRes;
				ggelev = ancestor->Entry()->elev + ofs;
			}
		} else {
			SurfTile *ggp = smgr->GlobalTile(lvl - ancestor_dlvl);// +3);
			if (ggp && ggp->LoadElevationData ()) {
				int blockRes = mgr->GridRes();
				int nblock = TILE_FILERES/blockRes;
				int mask = nblock-1;
				int ofs = ((mask - ilat & mask) * TILE_ELEVSTRIDE + (ilng & mask)) * blockRes;
				ggelev = ggp->elev + ofs;
			}
		}
		if (ggelev) ComputeElevationData(ggelev);
	}
	return ggelev;
}

// -----------------------------------------------------------------------

void SurfTile::ComputeElevationData(const float *elev) const
{
	if (has_elevfile) return;
	int i, j;
	int res = mgr->GridRes();
	ehdr.emax = -1e30;
	ehdr.emin = 1e30;
	ehdr.emean = 0.0;
	for (j = 0; j <= res; j++) {
		for (i = 0; i <= res; i++) {
			ehdr.emean += elev[i];
			ehdr.emax = max(ehdr.emax, (double)elev[i]);
			ehdr.emin = min(ehdr.emin, (double)elev[i]);
		}
		elev += TILE_ELEVSTRIDE;
	}
	ehdr.emean /= double((res + 1)*(res + 1));
}

// ------------------------------------------------------------------------------
// bGet(true) = Get the data specifically from this tile recardless of it's state
//
int SurfTile::GetElevation(double lng, double lat, double *elev, FVECTOR3 *nrm, SurfTile **cache, bool bFilter, bool bGet) const
{
	static int ndat = TILE_ELEVSTRIDE*TILE_ELEVSTRIDE;
	if (cache) *cache = (SurfTile *)this;

	if (lat<bnd.minlat || lat>bnd.maxlat) return -1;
	if (lng<bnd.minlng || lng>bnd.maxlng) return -1;

	if (state == ForRender || bGet)
	{
		if (!ggelev) { *elev = 0.0; return 2; }
		else {
			double fRes = double(mgr->GridRes());
			
			if (!bFilter) {
				int i = int((lat - bnd.minlat) * fRes / (bnd.maxlat - bnd.minlat)) + 1;
				int j = int((lng - bnd.minlng) * fRes / (bnd.maxlng - bnd.minlng)) + 1;
				*elev = double(ggelev[j+i*TILE_ELEVSTRIDE]);
			}
			else {

				float x = float((lat - bnd.minlat) * fRes / (bnd.maxlat - bnd.minlat)) + 1.0f; // 0.5f
				float y = float((lng - bnd.minlng) * fRes / (bnd.maxlng - bnd.minlng)) + 1.0f; // 0.5f
				float fx = (x - floor(x));
				float fy = (y - floor(y));

				int i0 = int(x) * TILE_ELEVSTRIDE;
				int i1 = i0 + TILE_ELEVSTRIDE;
				int j0 = int(y);

				assert(i0 > 0);
				assert(j0 > 0);
				assert((j0 + i1) < ndat);

				float q = lerp(ggelev[j0+i0], ggelev[j0+i1], fx); j0++;
				float w = lerp(ggelev[j0+i0], ggelev[j0+i1], fx);

				*elev = double(lerp(q,w,fy));
			}

			return 1;
		}
	}

	if (state == Invisible) return 0;

	if (state == Active) {
		int i = 0;
		if (lng > (bnd.minlng + bnd.maxlng)*0.5) i++;
		if (lat < (bnd.minlat + bnd.maxlat)*0.5) i += 2;
		if (node->Child(i))
			return node->Child(i)->Entry()->GetElevation(lng, lat, elev, nrm, cache);
	}

	if (state == Inactive) {
		// Calling elevation without tree being fully initialized.
		// Force data acquision using current level due to lack of render level. 
		return GetElevation(lng, lat, elev, nrm, cache, bFilter, true);
	}

	return -3;
}

// -----------------------------------------------------------------------

SurfTile *SurfTile::getTextureOwner()
{
	if (owntex) return this;
	SurfTile *parent = getSurfParent();
	while (parent) {
		if (parent->owntex) return parent;
		else parent = parent->getSurfParent();
	}
	return NULL;
}

// -----------------------------------------------------------------------

float SurfTile::fixinput(double a, int x)
{
	switch(x) {
		case 0: return float((1.0+floor(a*0.0625))*16.0);
		case 1: return float((1.0+floor(a*0.125))*8.0);
		case 2: return float((1.0+floor(a*0.5))*2.0);
	}
	return 0.0f;
}

// -----------------------------------------------------------------------

double SurfTile::GetCameraDistance()
{
	VECTOR3 cnt = Centre() * (mgr->CbodySize() + GetMeanElev());
	cnt = mgr->prm.cpos + mul(mgr->prm.grot, cnt);
	return length(cnt);
}

// -----------------------------------------------------------------------
// not upstream: horizon maps (design-ground-graphics 2)

SurfTile *SurfTile::ElevOwner ()
{
	int dl = 3;
	while (1 << (8 - dl) < mgr->GridRes()) dl--;
	if (lvl < dl) return NULL; // GlobalTile path, not in the tree
	QuadTreeNode<SurfTile> *a = node;
	for (int blockRes = TILE_FILERES; blockRes > mgr->GridRes() && a; blockRes >>= 1) a = a->Parent();
	return a ? a->Entry() : NULL;
}

void SurfTile::HorizonParams ()
{
	vPlanet *vp = mgr->GetPlanet();
	FlowControlPS *fc = vp->GetFlowControl();
	fc->bHorizon = false;
	if (!Config->HorizonShadows || !mgr->Cprm().elevMode) return;
	SurfTile *o = ElevOwner();
	if (!o || o->lvl + 4 < 8) return;
	if (mgr->GetScene()->GetRenderPass() == RENDERPASS_MAINSCENE) {
		double d = GetCameraDistance();
		if (d < 400e3) smgr->HorizonRequest(o, d);
	}
	if (!o->htex) return;

	ShaderParams *sp = vp->GetTerrainParams();
	int nb = 1 << (lvl - o->lvl), mask = nb - 1;
	double sc = 1.0 / nb, u0 = (ilng & mask) * sc, v0 = (ilat & mask) * sc; // v0 from the north
	const double W = Horizon::W, N = Horizon::N;
	sp->vHorOff = FVECTOR4(float((u0 * 256.0 + 0.5) / W), float((v0 * 256.0 + 0.5) / N), float(sc * 256.0 / W), float(sc * 256.0 / N));

	double lat = 0.5 * (bnd.minlat + bnd.maxlat), lng = 0.5 * (bnd.minlng + bnd.maxlng);
	double sl = sin(lat), cl = cos(lat), sg = sin(lng), cg = cos(lng);
	VECTOR3 E = _V(-sg, 0.0, cg), Nr = _V(-sl * cg, cl, -sl * sg); // planet frame, as Tile::Centre
	const VECTOR3 &s = mgr->prm.sdir;
	double k = atan2(dotp(s, Nr), dotp(s, E)) / (PI / 4.0);
	if (k < 0.0) k += 8.0;
	int k0 = int(k) % 8, k1 = (k0 + 1) % 8;
	float f = float(k - floor(k)), w[8] = {};
	w[k0] += 1.0f - f;
	w[k1] += f;
	sp->vHorW[0] = FVECTOR4(w[0], w[1], w[2], w[3]);
	sp->vHorW[1] = FVECTOR4(w[4], w[5], w[6], w[7]);
	mgr->GetShader()->SetTexture("tHorizon", o->htex, IPF_CLAMP | IPF_LINEAR);
	fc->bHorizon = true;
}

// -----------------------------------------------------------------------

FVECTOR4 SurfTile::MicroTexRange(SurfTile *pT, int ml) const
{
	float rs = 1.0f / float( 1 << (lvl-pT->Level()) );	// Range subdivision
	float xo = pT->MicroRep[ml].x * texrange.tumin;
	float yo = pT->MicroRep[ml].y * texrange.tvmin;
	xo -= floor(xo); // Micro texture offset for current tile
	yo -= floor(yo); // Micro texture offset for current tile
	return FVECTOR4(xo, yo, pT->MicroRep[ml].x * rs, pT->MicroRep[ml].y * rs);
}

// -----------------------------------------------------------------------
// Called during rendering even if this tile is not rendered but children are
// i.e. called for every RENDERED and ACTIVE tile
//
void SurfTile::StepIn ()
{
	VkDev *pDev = mgr->Dev();
	const vPlanet *vPlanet = mgr->GetPlanet();

	if (vPlanet != mgr->GetScene()->GetCameraProxyVisual()) return;

	// Compute micro texture repeat counts -------------------------------------
	//
	if (owntex && vPlanet->MicroCfg.bEnabled) {
		double s = vPlanet->GetSize();
		for (int i = 0; i < (int)std::size(vPlanet->MicroCfg.Level); ++i) {
			double f = s / vPlanet->MicroCfg.Level[i].size;
			MicroRep[i] = D3DXVECTOR2(fixinput(width*f, i), fixinput(height*f, i));
		}
	}
}


// -----------------------------------------------------------------------

void SurfTile::Render ()
{
	Tile::Render();

	if (smgr->bStream) { // not upstream: keep the tile stream's wants fresh for this tile and the ancestors whose texture it borrows
		DWORD pass = mgr->GetScene()->GetRenderPass();
		if (pass == RENDERPASS_MAINSCENE || pass == RENDERPASS_CUSTOMCAM) {
			for (SurfTile *t = this; t; t = t->getSurfParent()) {
				if (t->touchF == smgr->streamFrame) break; // walked this frame from another descendant, and its ancestors with it
				t->touchF = smgr->streamFrame;
				if (t->streamWait && smgr->streamNow - t->touchT >= 0.25) { // well inside the core's 2 s stale time
					t->touchT = smgr->streamNow;
					int r = oapiTileStreamWant(mgr->Cbody(), t->lvl + 4, t->ilat, t->ilng, 0);
					if (r > 0) smgr->StreamQueue(t->lvl + 4, t->ilat, t->ilng);
					else if (r < 0) t->streamWait = false;
				}
				if (t->owntex) break;
			}
		}
	}

	if (!mesh) return; // DEBUG : TEMPORARY

	VkDev *pDev = mgr->Dev();
	vPlanet *vPlanet = mgr->GetPlanet();
	const Scene *scene = mgr->GetScene();
	const D3D9Client *pClient = mgr->GetClient();

	static const double rad0 = sqrt(2.0)*PI05;
	double sdist, rad;
	bool has_specular = false;
	bool has_shadows = false;
	bool has_lights = false;
	bool has_microtex = false;
	bool has_atmosphere = vPlanet->HasAtmosphere();
	bool has_ripples = vPlanet->HasRipples();
	bool bUseZBuf = mgr->IsUsingZBuf();

	if (vPlanet->CameraAltitude()>20e3) has_ripples = false;

	double ca = 1.0 + saturate(vPlanet->CameraAltitude() / 150e3) * Config->OrbitalShadowMult;

	PlanetShader* pShader = mgr->GetShader();
	ShaderParams* sp = vPlanet->GetTerrainParams();
	FlowControlPS* fc = vPlanet->GetFlowControl();
	FlowControlVS* fcv = vPlanet->GetFlowControlVS();

	bool render_lights = pShader->bNightlights;
	bool render_shadows = (mgr->GetPlanet()->CloudMgr2() != NULL) && mgr->GetClient()->GetConfigParam(CFGPRM_CLOUDSHADOWS) && pShader->bCloudShd;

	if (ltex) {
		sdist = acos(dotp(mgr->prm.sdir, cnt));
		rad = rad0 / (double)(2 << lvl); // tile radius
		has_specular = (ltex != NULL) && sdist < (1.75 + rad);
		has_lights = (render_lights && ltex && sdist > 1.35);
		has_shadows = (render_shadows && sdist < (PI05 + rad));
	}
	
	has_specular &= pShader->bWater;
	has_ripples &= pShader->bRipples & has_specular;
	has_lights &= pShader->bNightlights;
	has_atmosphere &= pShader->bAtmosphere;

	sp->vCloudOff = FVECTOR4(0, 0, 1, 1);
	sp->vCldAtlasW = FVECTOR4(0, 0, 0, 0); // not upstream: set again below when the shadow uses the cloud atlas

	D3DXVECTOR3 bs_pos;
	D3DXVec3TransformCoord(&bs_pos, &mesh->bsCnt, &mWorld);

	// ----------------------------------------------------------------------
	// Assign micro texture range information to shaders 
	// ----------------------------------------------------------------------

	SurfTile *pT = getTextureOwner();

	if (pT && vPlanet->MicroCfg.bEnabled && pShader->bMicrotex)
	{
		sp->vMSc[0] = MicroTexRange(pT, 0);
		sp->vMSc[1] = MicroTexRange(pT, 1);
		sp->vMSc[2] = MicroTexRange(pT, 2);
		has_microtex = true;
	}



	// ----------------------------------------------------------------------
	// Setup cloud shadows 
	// ----------------------------------------------------------------------

	has_shadows = false;

	if (render_shadows)
	{
		VkTex *pCloud = NULL, *pCloud2 = NULL;
		CloudTile *pCloudTile = NULL; // not upstream: the tile the shadow uses, for its file level

		const TileManager2<CloudTile> *cmgr = vPlanet->CloudMgr2();
		int maxlvl = min(lvl,9);
		double rot = mgr->prm.rprm->cloudrot;
		double edglat = (bnd.minlat + bnd.maxlat)*0.5;	// latitude of tile center
		double edglng = wrap(rot + bnd.minlng);		// surface tile minlng-edge position on cloud-layer

		for (int attempt=0;attempt<2;attempt++) {

			CloudTile *ctile = (CloudTile *)cmgr->SearchTile(edglng, edglat, maxlvl, true);

			if (ctile) {

				double icsize = double( 1 << ctile->Level() ) / PI;	// inverse of cloud tile size in radians

				// Compute surface tile uv origin on a selected claud tile
				// Note: edglng exists always within tile i.e. no wrap from PI to -PI
				double u0 = (edglng - ctile->bnd.minlng) * icsize;
				double v0 = (ctile->bnd.maxlat - bnd.maxlat) * icsize;		// Note: Tile corner is lower-left, texture corner is upper-left
				double u1 = u0 + (bnd.maxlng - bnd.minlng) * icsize;
				double v1 = v0 + (bnd.maxlat - bnd.minlat) * icsize;

				// Feed uv-offset and uv-range to the shaders
				sp->vCloudOff = FVECTOR4(float(u0), float(v0), float(u1-u0), float(v1-v0));
				sp->fAlpha = float(ca);
				pCloud = ctile->Tex();
			

				// Texture uv range extends to another tile
				if (u1 > 1.0) {

					double csize = PI / double(1<<ctile->Level());			// cloud tile size in radians
					double ctr = (ctile->bnd.minlng + ctile->bnd.maxlng) * 0.5;		// cloud tile center
					double lng  = wrap(ctr + csize*sign(rot));				// center of the next tile

					// Request an other tile from the same level as the first one
					CloudTile *ctile2 = (CloudTile *)cmgr->SearchTile(lng, edglat, ctile->Level(), true);

					if (ctile2) {
						if (ctile2->Level() == ctile->Level()) {
							// Rendering with dual texture
							pCloud2 = ctile2->Tex();
							has_shadows = true;
							pCloudTile = ctile; // not upstream
							break;
						}
						else {
							// Failed to match a dual texture render requirements (due to no-valid tile available)
							// Try again and request a lower level texture data for the first texture
							maxlvl = ctile2->Level();
							if (attempt==1) LogErr("CloudShadows mapping failed");
						}
					}
				}
				else {
					// Just one texture in use
					has_shadows = true;
					pCloudTile = ctile; // not upstream
					break;
				}
			}
		}

		pShader->SetTexture(pShader->tCloud, pCloud, IPF_CLAMP | IPF_ANISOTROPIC, Config->Anisotrophy);
		pShader->SetTexture(pShader->tCloud2, pCloud2, IPF_CLAMP | IPF_ANISOTROPIC, Config->Anisotrophy);

		VkTex *atl = CloudAtlasTex(cmgr->atlas); // not upstream: seamless cloud shadows (design-cloud-seams)
		if (atl && has_shadows && pCloudTile) {
			CloudAtlas::Range ar = lvl ? CloudAtlas::Quad(bnd.minlat, bnd.maxlat, bnd.minlng, bnd.maxlng, rot) : CloudAtlas::Hemi(ilng, rot);
			sp->vCldAtlas = FVECTOR4(ar.x, ar.y, ar.z, ar.w);
			sp->vCldAtlasW = FVECTOR4(1.0f, float(max(1, Config->Anisotrophy)), pCloudTile->Level() + 4 <= CloudAtlasLevel(cmgr->atlas) ? 1.0f : 0.0f, 0.0f);
			pShader->SetTexture("tCloudAtlas", atl, IPF_ANISOTROPIC | IPF_CLAMP_V, Config->Anisotrophy);
		}
	}

	
	fc->bCloudShd = has_shadows;
	fc->bMicroTex = has_microtex;
	fc->bLocals = false;
	fc->bOverlay = false;
	fc->bMask = false;
	fc->bShadows = false;
	fc->bInSpace = !vPlanet->CameraInAtmosphere();
	fc->bPlanetShadow = vPlanet->SphericalShadow();
	fcv->bElevOvrl = false;


	// ----------------------------------------------------------------------
	// DevTools: Render with overlay image
	// ----------------------------------------------------------------------

	if (pShader->bDevtools && scene->GetRenderPass() == RENDERPASS_MAINSCENE)
	{
		FVECTOR4 texcoord;
		const vPlanet::sOverlay* oLay = vPlanet->IntersectOverlay(bnd.vec, &texcoord);

		if (oLay)
		{
			bool bOlayEnable = false; for (auto x : oLay->pSurf) if (x) bOlayEnable = true;

			if (bOlayEnable)
			{
				// Global large-scale overlay
				pShader->SetTexture("tOverlay", oLay->pSurf[0]);
				pShader->SetTexture("tMskOverlay", oLay->pSurf[1]);
				pShader->SetTexture("tElvOverlay", oLay->pSurf[2]);

				memcpy(&sp->vOverlayCtrl, &oLay->Blend, sizeof(FVECTOR4) * 4);
				sp->vOverlayOff = texcoord;

				if (oLay->pSurf[0] || oLay->pSurf[1]) fc->bOverlay = true;
				if (oLay->pSurf[2]) fcv->bElevOvrl = true;
			}
		}
		else if (overlay) {
			// Local tile specific overlay
			pShader->SetTexture("tOverlay", overlay);
			sp->vOverlayOff = GetTexRangeDX(&overlayrange);
			fc->bOverlay = true;
		}
	}


	// ----------------------------------------------------------------------
	// Setup Main Texture
	// ----------------------------------------------------------------------

	pShader->SetTexture(pShader->tDiff, tex, IPF_CLAMP | IPF_ANISOTROPIC, Config->Anisotrophy);
	fc->bTexture = (tex != nullptr);

	// ----------------------------------------------------------------------
	// Night Lights and Water Specular
	// ----------------------------------------------------------------------
	
	if (pShader->bNightlights || pShader->bWater)
	{
		if ((has_specular || has_lights) && ltex) {
			pShader->SetTexture(pShader->tMask, ltex, IPF_CLAMP | IPF_ANISOTROPIC, Config->Anisotrophy);
			fc->bMask = true;
		}
		else pShader->SetTexture(pShader->tMask, NULL, IPF_CLAMP | IPF_ANISOTROPIC, Config->Anisotrophy);
	}

	fc->bClass = false; // not upstream: land class detail where the stream made a Class tile and no micro texture is set
	if (ctex && tex && !has_microtex && vPlanet->HasTextures() && vPlanet->CameraAltitude() < 20e3) {
		double rep = height * vPlanet->GetSize() / 8.0; // 8 m cells, a power of two per tile: the same lattice on every level
		rep = std::max(1.0, std::exp2(std::round(std::log2(std::max(rep, 1.0)))));
		double P = 16384.0;
		sp->vMSc[0] = FVECTOR4(float(rep), float(rep), float(std::fmod(ilng * rep, P)), float(std::fmod(ilat * rep, P)));
		pShader->SetTexture("tClass", ctex, IPF_CLAMP | IPF_LINEAR);
		fc->bClass = true;
	}

	HorizonParams(); // not upstream: terrain shadows from the owner's horizon map

	if (tex == nullptr || !vPlanet->HasTextures())
	{
		fc->bTexture = false;
		fc->bMask = false;
		fc->bCloudShd = false;
		fc->bMicroTex = false;
		fc->bClass = false; // not upstream
	}

	sp->vTexOff = GetTexRangeDX(&texrange);
	sp->vMicroOff = GetTexRangeDX(&microrange);
	sp->mWorld = mWorld;
	sp->fTgtScale = tgtscale;

	{ // not upstream: foam mask mip with ~20 m texels and a 40 m tap ring in mask uv (texel size from the tile, its texrange and 512 texels), on every tile
		double R = vPlanet->GetSize();
		double tv = std::max(1e-3, (bnd.maxlat - bnd.minlat) * R / (512.0 * std::max(1e-9, double(texrange.tvmax - texrange.tvmin))));
		double tu = std::max(1e-3, (bnd.maxlng - bnd.minlng) * R * std::cos(0.5 * (bnd.minlat + bnd.maxlat)) / (512.0 * std::max(1e-9, double(texrange.tumax - texrange.tumin))));
		double mip = std::min(9.0, std::max(0.0, std::log2(20.0 / std::max(tu, tv))));
		sp->vWaterFx = FVECTOR4(float(mip), float(std::min(0.05, 40.0 / (512.0 * tu))), float(std::min(0.05, 40.0 / (512.0 * tv))), 0.0f);
	}
	bool bVolShd = scene->GetRenderPass() == RENDERPASS_MAINSCENE && vPlanet == scene->GetCameraProxyVisual(); // not upstream: the 2D cloud shadow fades where the volume shadow drew last frame
	sp->vVolCld = FVECTOR4(35e3f, 60e3f, bVolShd ? scene->GetVolShadowFade() : 0.0f, 0.0f);

	if (has_lights) sp->fBeta = float(mgr->Cprm().lightfac);
	else sp->fBeta = 0.0f;




	// ---------------------------------------------------------------------
	// Setup shadow maps
	// ---------------------------------------------------------------------

	bool bCsm = SceneShadows::Get() && SceneShadows::Get()->ReceiversOn(vPlanet); // not upstream: the cascades carry the focus vessel's shadow on binding 25 (design-scene-shadows 7.2)

	if (pShader->bShdMap && !bCsm)
	{
		const Scene::SHADOWMAPPARAM* shd = scene->GetSMapData();

		D3DXVECTOR3 bc = bs_pos - shd->pos;

		double alt = scene->GetCameraAltitude() - scene->GetTargetElevation();

		if ((alt < 10e3) && (scene->GetCameraProxyVisual() == mgr->GetPlanet())) {

			if (shd->pShadowMap && (Config->ShadowMapMode != 0) && (Config->TerrainShadowing == 2)) {

				float x = D3DXVec3Dot(&bc, &(shd->ld));

				if (sqrt(D3DXVec3Dot(&bc, &bc) - x * x) < (shd->rad + mesh->bsRad)) {
					float s = float(shd->size);
					float sr = 2.0f * shd->rad / s;
					sp->mLVP = shd->mViewProj;
					sp->vSHD = FVECTOR4(sr, 1.0f / s, 0.0f, 1.0f / shd->depth);
					fc->bShadows = true;
				}
			}
		}

		pShader->SetTexture(pShader->tShadowMap, shd->pShadowMap);
	}

	// ---------------------------------------------------------------------
	// Setup local light sources
	//---------------------------------------------------------------------

	int cfg = vPlanet->GetShaderID();

	LightF Locals;
	BOOL Spots[4];

	if (cfg != PLT_GIANT)
	{
		for (int i = 0; i < 4; i++)
		{
			Locals.attenuation[i] = FVECTOR3(1.0f, 1.0f, 1.0f);
			Locals.diffuse[i] = FVECTOR3(0.0f, 0.0f, 0.0f);
			Locals.direction[i] = FVECTOR3(1.0f, 0.0f, 0.0f);
			Locals.param[i] = FVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
			Locals.position[i] = FVECTOR3(0.0f, 0.0f, 0.0f);
			Spots[i] = false;
		}

		if (scene->GetRenderPass() == RENDERPASS_MAINSCENE)
		{
			const D3D9Light* pLights = scene->GetLights();
			int nSceneLights = min(scene->GetLightCount(), MAX_SCENE_LIGHTS);

			if (pLights && nSceneLights > 0 && pShader->bLocals)
			{
				int nMeshLights = 0;

				_LightList LightList[MAX_SCENE_LIGHTS];

				// Find all local lights effecting this mesh ------------------------------------------
				//
				for (int i = 0; i < nSceneLights; i++) {
					float il = pLights[i].GetIlluminance(bs_pos, mesh->bsRad);
					if (il > 0.005f) {
						LightList[nMeshLights].illuminace = il;
						LightList[nMeshLights++].idx = i;
					}
				}

				if (nMeshLights > 0) {

					// If any, Sort the list based on illuminance -------------------------------------------
					qsort(LightList, nMeshLights, sizeof(_LightList), compare_lights);

					nMeshLights = min(nMeshLights, 4);

					// Create a list of N most effective lights ---------------------------------------------
					for (int i = 0; i < nMeshLights; i++)
					{
						auto pL = pLights[LightList[i].idx];
						Locals.attenuation[i] = pL.Attenuation;
						Locals.diffuse[i] = FVECTOR4(pL.Diffuse).rgb;
						Locals.direction[i] = pL.Direction;
						Locals.param[i] = pL.Param;
						Locals.position[i] = pL.Position;
						Spots[i] = (pL.Type == 1);
					}

					// Enable local lights and feed data to shader
					fc->bLocals = true;
				}
			}
		}
	}



	// -------------------------------------------------------------------
	// render surface mesh
	//
	pShader->SetVSConstants(pShader->PrmVS, sp, sizeof(ShaderParams));
	pShader->SetPSConstants(pShader->Prm, sp, sizeof(ShaderParams));


	pShader->SetVSConstants(pShader->FlowVS, fcv, sizeof(FlowControlVS));
	pShader->SetPSConstants(pShader->Flow, fc, sizeof(FlowControlPS));

	if (fc->bLocals && (cfg != PLT_GIANT))
	{
		pShader->SetPSConstants(pShader->Lights, &Locals, sizeof(Locals));
		pShader->SetPSConstants(pShader->Spotlight, Spots, sizeof(Spots));
	}

	pShader->UpdateTextures();

	pDev->SetStreamSource(0, mesh->pVB, 0, sizeof(VERTEX_2TEX));
	pDev->SetIndices(mesh->pIB);
	pDev->DrawIndexedPrimitive(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 0, VkPrimVerts(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, mesh->nf));

	// not upstream: kept for the SceneFx distance pass, within 150 km
	if (smgr->depthRec && D3DXVec3Length(&bs_pos) - mesh->bsRad < 150e3f) smgr->depthTiles.push_back({this, mWorld});
	
	// Render tile bounding box
	//
	/*
	if (DebugControls::IsActive()) {
		DWORD flags  = *(DWORD*)mgr->GetClient()->GetConfigParam(CFGPRM_GETDEBUGFLAGS);
		if (flags&DBG_FLAGS_TILEBOXES) {
			D3D9Effect::RenderTileBoundingBox(&mWorld, mesh->Box, &D3DXVECTOR4(1,0,0,1));
		}
	}*/
}

// -----------------------------------------------------------------------

void SurfTile::MatchEdges ()
{
	if (edgeok) return; // done already
	edgeok = true;
	if (!mesh) return;  // sanity check

	QuadTreeNode<SurfTile>* lngnbr = smgr->FindNode(lvl, ilng + (ilng & 1 ? 1 : -1), ilat);
	QuadTreeNode<SurfTile>* latnbr = smgr->FindNode(lvl, ilng, ilat + (ilat & 1 ? 1 : -1));
	QuadTreeNode<SurfTile>* dianbr = smgr->FindNode(lvl, ilng + (ilng & 1 ? 1 : -1), ilat + (ilat & 1 ? 1 : -1));


	if (lngnbr && !(lngnbr->Entry()->state & TILE_VALID)) lngnbr = 0;
	if (latnbr && !(latnbr->Entry()->state & TILE_VALID)) latnbr = 0;
	if (dianbr && !(dianbr->Entry()->state & TILE_VALID)) dianbr = 0;

	int new_lngnbr_lvl = (lngnbr ? lngnbr->Entry()->lvl : lvl);
	int new_latnbr_lvl = (latnbr ? latnbr->Entry()->lvl : lvl);
	int new_dianbr_lvl = (dianbr ? dianbr->Entry()->lvl : lvl);

	// fix lower-level neighbours first
	bool nbr_updated = false;
	if (new_lngnbr_lvl < lvl) {
		lngnbr->Entry()->MatchEdges ();
		nbr_updated = true;
	}
	if (new_latnbr_lvl < lvl) {
		latnbr->Entry()->MatchEdges ();
		nbr_updated = true;
	}
	if (!nbr_updated && new_dianbr_lvl < lvl) {
		// now we need to check the diagonal neighbour, since this could be lower resolution
		// and thus responsible for the corner node elevation
		dianbr->Entry()->MatchEdges ();
	}

	bool lngedge_changed = (new_lngnbr_lvl != lngnbr_lvl);
	bool latedge_changed = (new_latnbr_lvl != latnbr_lvl);
	bool diaedge_changed = (new_dianbr_lvl != dianbr_lvl);

	if (lngedge_changed || latedge_changed || diaedge_changed) {
		if (new_dianbr_lvl < new_lngnbr_lvl && new_dianbr_lvl < new_latnbr_lvl) {
			FixCorner (dianbr ? dianbr->Entry() : 0);
			FixLatitudeBoundary (latnbr ? latnbr->Entry() : 0, true);
			FixLongitudeBoundary (lngnbr ? lngnbr->Entry() : 0, true);
		} else if (new_latnbr_lvl < new_lngnbr_lvl) {
			FixLatitudeBoundary (latnbr ? latnbr->Entry() : 0);
			FixLongitudeBoundary (lngnbr ? lngnbr->Entry() : 0, true);
		} else {
			FixLongitudeBoundary (lngnbr ? lngnbr->Entry() : 0);
			FixLatitudeBoundary (latnbr ? latnbr->Entry() : 0, true);
		}
		mesh->MapVertices(mgr->Dev()); // copy the updated vertices to the vertex buffer
		lngnbr_lvl = new_lngnbr_lvl;
		latnbr_lvl = new_latnbr_lvl;
		dianbr_lvl = new_dianbr_lvl;
	}
}

// -----------------------------------------------------------------------

void SurfTile::FixCorner (const SurfTile *nbr)
{
	if (!mesh) return; // sanity check

	int res = mgr->GridRes();
	int vtx_idx = (ilat & 1 ? 0 : (res+1)*res) + (ilng & 1 ? res : 0);
	VERTEX_2TEX &vtx_store = mesh->vtx[mesh->nv + (ilat & 1 ? 0 : res)];

	float *elev = ElevationData();
	if (!elev) return;

	if (nbr) {
		float *nbr_elev = nbr->ElevationData();
		if (!nbr_elev) return;

		float corner_elev = elev[TILE_ELEVSTRIDE+1 + (ilat & 1 ? 0 : TILE_ELEVSTRIDE*res) + (ilng & 1 ? res : 0)];
		float nbr_corner_elev = nbr_elev[TILE_ELEVSTRIDE+1 + (ilat & 1 ? TILE_ELEVSTRIDE*res : 0) + (ilng & 1 ? 0 : res)];

		double rad = mgr->CbodySize();
		double radfac = (rad+nbr_corner_elev)/(rad+corner_elev);
		mesh->vtx[vtx_idx].x = (float)(vtx_store.x*radfac + vtxshift.x*(radfac-1.0));
		mesh->vtx[vtx_idx].y = (float)(vtx_store.y*radfac + vtxshift.y*(radfac-1.0));
		mesh->vtx[vtx_idx].z = (float)(vtx_store.z*radfac + vtxshift.z*(radfac-1.0));
	}
}

// -----------------------------------------------------------------------

void SurfTile::FixLongitudeBoundary (const SurfTile *nbr, bool keep_corner)
{
	// Fix the left or right edge
	int i, i0, i1, j, vtx_ofs, nbrlvl, dlvl;
	int res = mgr->GridRes();
	VERTEX_2TEX *vtx_store;

	vtx_ofs = (ilng & 1 ? res : 0); // check if neighbour is at left or right edge
	if (nbr && nbr->mesh && nbr->mesh->vtx) {
		nbrlvl = min(nbr->lvl, lvl); // we don't need to worry about neigbour levels higher than ours
		vtx_store = mesh->vtx + mesh->nv;
		if (nbrlvl == lvl) { // put my own edge back
			i0 = 0;
			i1 = res;
			if (keep_corner)
				if (ilat & 1) i0++; else i1--;
			for (i = i0; i <= i1; i++)
				mesh->vtx[vtx_ofs+i*(res+1)] = vtx_store[i];
		} else {  // interpolate to neighbour's left edge
			dlvl = lvl-nbrlvl;
			int nsub = 1 << dlvl; // number of tiles fitting alongside the lowres neighbour
			if (nsub <= res) { // for larger tile level differences the interleaved sampling method doesn't work
				float *elev = ElevationData();
				float *nbr_elev = nbr->ElevationData();
				if (elev && nbr_elev) {
					elev += TILE_ELEVSTRIDE+1 + vtx_ofs; // add offset
					int nsub = 1 << dlvl;    // number of tiles fitting alongside the lowres neighbour
					int vtx_skip = nsub*(res+1);  // interleave factor for nodes attaching to neigbour nodes
					int nbr_range = res/nsub; // number of neighbour vertices attaching to us - 1
					int subidxmask = (1<<dlvl)-1;
					int subidx = ilat & subidxmask;
					int nbr_ofs = (subidxmask-subidx) * nbr_range * TILE_ELEVSTRIDE;
					nbr_elev += TILE_ELEVSTRIDE+1 + nbr_ofs + (res-vtx_ofs);
					double rad = mgr->CbodySize();
					// match nodes to neighbour elevations
					i0 = 0;
					i1 = nbr_range;
					if (keep_corner)
						if (ilat & 1) i0++; else i1--;
					for (i = i0; i <= i1; i++) {
						double radfac = (rad+nbr_elev[i*TILE_ELEVSTRIDE])/(rad+elev[i*TILE_ELEVSTRIDE*nsub]);
						mesh->vtx[vtx_ofs+i*vtx_skip].x = (float)(vtx_store[i*nsub].x*radfac + vtxshift.x*(radfac-1.0));
						mesh->vtx[vtx_ofs+i*vtx_skip].y = (float)(vtx_store[i*nsub].y*radfac + vtxshift.y*(radfac-1.0));
						mesh->vtx[vtx_ofs+i*vtx_skip].z = (float)(vtx_store[i*nsub].z*radfac + vtxshift.z*(radfac-1.0));
					}
					// interpolate the nodes that fall between neighbour nodes
					for (i = 0; i < nbr_range; i++)
						for (j = 1; j < nsub; j++)
							VtxInterpolate (mesh->vtx[vtx_ofs+j*(res+1)+i*vtx_skip], mesh->vtx[vtx_ofs+i*vtx_skip], mesh->vtx[vtx_ofs+(i+1)*vtx_skip], (double)j/double(nsub));
				}
			} else {
				// problems
			}
		}
	}
}

// -----------------------------------------------------------------------

void SurfTile::FixLatitudeBoundary (const SurfTile *nbr, bool keep_corner)
{
	// Fix the top or bottom edge
	int i, i0, i1, j, nbrlvl, dlvl;
	int res = mgr->GridRes();
	VERTEX_2TEX *vtx_store;

	int line = (ilat & 1 ? 0 : res);
	int vtx_ofs = (ilat & 1 ? 0 : line*(res+1));
	if (nbr && nbr->mesh && nbr->mesh->vtx) {
		nbrlvl = min(nbr->lvl, lvl); // we don't need to worry about neigbour levels higher than ours
		vtx_store = mesh->vtx + mesh->nv + res + 1;
		if (nbrlvl == lvl) { // put my own edge back
			i0 = 0;
			i1 = res;
			if (keep_corner)
				if (ilng & 1) i1--; else i0++;
			for (i = i0; i <= i1; i++)
				mesh->vtx[vtx_ofs+i] = vtx_store[i];
		} else {
			dlvl = lvl-nbrlvl;
			int nsub = 1 << dlvl; // number of tiles fitting alongside the lowres neighbour
			if (nsub <= res) { // for larger tile level differences the interleaved sampling method doesn't work
				float *elev = ElevationData();
				float *nbr_elev = nbr->ElevationData();
				if (elev && nbr_elev) {
					elev += (line+1)*TILE_ELEVSTRIDE + 1; // add offset
					int nsub = 1 << dlvl;    // number of tiles fitting alongside the lowres neighbour
					int vtx_skip = nsub;     // interleave factor for nodes attaching to neigbour nodes
					int nbr_range = res/nsub; // number of neighbour vertices attaching to us - 1
					int subidxmask = (1<<dlvl)-1;
					int subidx = ilng & subidxmask;
					int nbr_ofs = subidx * nbr_range;
					nbr_elev += TILE_ELEVSTRIDE+1 + nbr_ofs + (res-line)*TILE_ELEVSTRIDE;
					double rad = mgr->CbodySize();
					// match nodes to neighbour elevations
					i0 = 0;
					i1 = nbr_range;
					if (keep_corner)
						if (ilng & 1) i1--; else i0++;
					for (i = i0; i <= i1; i++) {
						double radfac = (rad+nbr_elev[i])/(rad+elev[i*nsub]);
						mesh->vtx[vtx_ofs+i*vtx_skip].x = (float)(vtx_store[i*nsub].x*radfac + vtxshift.x*(radfac-1.0));
						mesh->vtx[vtx_ofs+i*vtx_skip].y = (float)(vtx_store[i*nsub].y*radfac + vtxshift.y*(radfac-1.0));
						mesh->vtx[vtx_ofs+i*vtx_skip].z = (float)(vtx_store[i*nsub].z*radfac + vtxshift.z*(radfac-1.0));
					}
					// interpolate the nodes that fall between neighbour nodes
					for (i = 0; i < nbr_range; i++)
						for (j = 1; j < nsub; j++)
							VtxInterpolate (mesh->vtx[vtx_ofs+i*vtx_skip+j], mesh->vtx[vtx_ofs+i*vtx_skip], mesh->vtx[vtx_ofs+(i+1)*vtx_skip], (double)j/double(nsub));
				}
			} else {
				// problems
			}
		}
	}
}

// -----------------------------------------------------------------------

void SurfTile::CreateLabels()
{
	if (!label) {
		label = TileLabel::Create(this);
	}
}

// -----------------------------------------------------------------------

inline void SurfTile::DeleteLabels()
{
	SAFE_DELETE(label);
}

// -----------------------------------------------------------------------

void SurfTile::RenderLabels(D3D9Pad *skp, oapi::Font **labelfont, int *fontidx)
{
	if (!label) return;
	label->Render(skp, labelfont, fontidx);
}


// =======================================================================
// not upstream: tiles the core's tile stream has written (TileStream.cpp) replace the ancestor texture a tile shows

static QuadTreeNode<SurfTile> *StreamNode (QuadTreeNode<SurfTile> *tree, int L, int ilat, int ilng)
{
	int lvl = L - 4;
	if (lvl < 0 || (ilng >> lvl) > 1) return NULL;
	QuadTreeNode<SurfTile> *n = &tree[ilng >> lvl];
	for (int d = 1; n && d <= lvl; d++)
		n = n->Child(((ilat >> (lvl - d)) & 1) * 2 + ((ilng >> (lvl - d)) & 1)); // LoadChildNode's index
	return n && n->Entry() ? n : NULL;
}

// render thread, outside the loader mutex: the disk reads
template<>
void TileManager2<SurfTile>::StreamRead ()
{
	int k[3 * 16];
	int n = oapiTileStreamDone(Cbody(), k, 16);
	for (int i = 0; i < n; i++) StreamQueue(k[3 * i], k[3 * i + 1], k[3 * i + 2]);
	SurfTile *t0 = tiletree[0].Entry(); // a root always exists; LoadTextureFile only reads its mipmap flag
	bool mask = cprm.bSpecular || cprm.bLights;
	char path[MAX_PATH];
	int taken = 0;
	for (StreamLoad &s : streamKeys) {
		if (s.surf || !s.checked) continue; // StreamApply checks first that the tile can take it
		if (taken == 4) break;
		if (!StreamNode(tiletree, s.L, s.ilat, s.ilng)) { s.L = 0; continue; } // the tree only changes on this thread
		snprintf(path, MAX_PATH, "%s\\Surf\\%02d\\%06d\\%06d.dds", DataRootDir().c_str(), s.L, s.ilat, s.ilng);
		s.stamp = FileStamp(path);
		t0->LoadTextureFile(path, &s.surf);
		if (!s.surf) {
			oapiTileStreamWant(Cbody(), s.L, s.ilat, s.ilng, 1);
			s.L = 0;
			continue;
		}
		if (mask) {
			snprintf(path, MAX_PATH, "%s\\Mask\\%02d\\%06d\\%06d.dds", DataRootDir().c_str(), s.L, s.ilat, s.ilng);
			t0->LoadTextureFile(path, &s.mask);
			if (!s.mask) { // a streamed Surf always comes with its Mask: have the pair made again
				oapiTileStreamWant(Cbody(), s.L, s.ilat, s.ilng, 1);
				delete s.surf;
				s.surf = NULL;
				s.L = 0;
				continue;
			}
		}
		if (s.L >= 12) { // land class weights, when the stream made them
			snprintf(path, MAX_PATH, "%s\\Class\\%02d\\%06d\\%06d.dds", DataRootDir().c_str(), s.L, s.ilat, s.ilng);
			t0->LoadTextureFile(path, &s.cls);
		}
		taken++;
	}
	streamKeys.erase(std::remove_if(streamKeys.begin(), streamKeys.end(), [](const StreamLoad &s) { return s.L == 0; }), streamKeys.end());
}

// under the loader mutex: swap the texture in, and point the descendants that borrowed the old one at it
template<>
void TileManager2<SurfTile>::StreamApply ()
{
	static const TEXCRDRANGE2 full = {0, 1, 0, 1};
	std::vector<StreamLoad> keep;
	for (StreamLoad &s : streamKeys) {
		QuadTreeNode<SurfTile> *node = StreamNode(tiletree, s.L, s.ilat, s.ilng);
		SurfTile *t = node ? node->Entry() : NULL;
		bool use = t && (t->state & TILE_VALID) && (t->streamWait || t->looseTex); // others read the file in their own PreLoad
		if (!s.surf) { // not read yet: only what can be applied is read
			if (use && !s.checked && !t->streamWait && t->texStamp) { // a Done key for the very file the tile read in PreLoad is dropped
				char path[MAX_PATH];
				snprintf(path, MAX_PATH, "%s\\Surf\\%02d\\%06d\\%06d.dds", DataRootDir().c_str(), s.L, s.ilat, s.ilng);
				if (FileStamp(path) == t->texStamp) use = false;
			}
			if (use) { s.checked = true; keep.push_back(s); }
			continue;
		}
		std::vector<QuadTreeNode<SurfTile>*> set;
		if (use) {
			set.push_back(node);
			for (size_t i = 0; i < set.size(); i++)
				for (int c = 0; c < 4; c++) {
					QuadTreeNode<SurfTile> *ch = set[i]->Child(c);
					SurfTile *e = ch ? ch->Entry() : NULL;
					if (e && (e->state & TILE_VALID) && !e->owntex) set.push_back(ch);
				}
			bool busy = false; // a loading child reads its parent's texture without the mutex
			for (QuadTreeNode<SurfTile> *m : set)
				for (int c = 0; c < 4; c++) {
					QuadTreeNode<SurfTile> *ch = m->Child(c);
					if (ch && ch->Entry() && ch->Entry()->state == Tile::Loading) busy = true;
				}
			if (busy) { keep.push_back(s); continue; }
		}
		VkTex *nt = NULL, *nl = NULL, *nc = NULL;
		if (use && t->CreateTexture(Dev(), s.surf, &nt)) {
			if (s.mask) t->CreateTexture(Dev(), s.mask, &nl);
			if (s.cls) t->CreateTexture(Dev(), s.cls, &nc);
			if (t->owntex) {
				if (t->tex) g_pTexmgr_tt->Free(t->tex);
				if (t->ltex) g_pTexmgr_tt->Free(t->ltex);
				if (t->ctex) g_pTexmgr_tt->Free(t->ctex);
			}
			t->tex = nt;
			t->ltex = nl;
			t->ctex = nc;
			t->owntex = true;
			t->texrange = full;
			t->streamWait = false;
			t->looseTex = true;
			t->texStamp = s.stamp;
			for (size_t i = 1; i < set.size(); i++) {
				SurfTile *e = set[i]->Entry(), *p = e->getSurfParent();
				e->tex = p->tex;
				e->ltex = p->ltex;
				e->ctex = p->ctex;
				e->GetParentSubTexRange(&e->texrange);
			}
		}
		delete s.surf;
		delete s.mask;
		delete s.cls;
	}
	streamKeys.swap(keep);
}

// =======================================================================
// not upstream: horizon map jobs (design-ground-graphics 2.3)

struct HorizonState {
	struct Result { int lvl, ilat, ilng; unsigned gen; std::shared_ptr<const Horizon::Map> map; int missing; bool redo; }; // map NULL: dropped
	struct Box { std::mutex mx; std::deque<Result> done; };
	int id = 0;
	std::shared_ptr<Box> box = std::make_shared<Box>();
	SurfTile *cand = nullptr; // this main pass's nearest owner wanting a job
	double candPrio = 0.0;
	bool candRedo = false;
};

void HorizonStateFree (HorizonState *h)
{
	if (!h) return;
	if (GfxPool *p = GfxPool::Peek()) p->Unregister(h->id);
	delete h;
}

template<>
SurfTile *TileManager2<SurfTile>::NodeAt (int ilvl, int ilat, int ilng)
{
	if (ilvl < 0 || ilat < 0 || ilat >= (1 << ilvl)) return NULL;
	int nlng = 2 << ilvl;
	ilng = ((ilng % nlng) + nlng) % nlng;
	QuadTreeNode<SurfTile> *n = &tiletree[ilng >> ilvl];
	for (int d = 1; n && d <= ilvl; d++)
		n = n->Child(((ilat >> (ilvl - d)) & 1) * 2 + ((ilng >> (ilvl - d)) & 1)); // LoadChildNode's index
	return n ? n->Entry() : NULL;
}

template<>
void TileManager2<SurfTile>::HorizonRequest (SurfTile *o, double dist)
{
	if (!horizon) {
		horizon = new HorizonState;
		horizon->id = GfxPool::Get().Register();
	}
	auto ready = [](const SurfTile *t) { return t && t->elevReady.load(std::memory_order_acquire) && t->elev; };
	double now = oapiGetSysTime();
	bool redo = false;
	if (o->hor) {
		if (!o->horMissing || o->horRedone || o->horAsk >= 0.0 || now - o->horCheck < 2.0) return;
		o->horCheck = now;
		for (int a = -1; a <= 1; a++)
			for (int b = -1; b <= 1; b++) {
				if ((!a && !b) || o->ilat + a < 0 || o->ilat + a >= (1 << o->lvl)) continue;
				if (!ready(NodeAt(o->lvl, o->ilat + a, o->ilng + b))) return;
			}
		redo = true;
	}
	else if (o->horAsk >= 0.0 && now - o->horAsk < 10.0) return; // pending; a lost job expires
	if (!ready(o)) return;
	double prio = dist + (redo ? 1e9 : 0.0);
	if (horizon->cand && prio >= horizon->candPrio) return;
	horizon->cand = o;
	horizon->candPrio = prio;
	horizon->candRedo = redo;
}

template<>
void TileManager2<SurfTile>::HorizonSubmit ()
{
	if (!horizon || !horizon->cand) return;
	SurfTile *o = horizon->cand;
	double prio = horizon->candPrio, now = oapiGetSysTime();
	bool redo = horizon->candRedo;
	horizon->cand = nullptr;
	auto ready = [](const SurfTile *t) { return t && t->elevReady.load(std::memory_order_acquire) && t->elev; };
	if (!GfxPool::Get().Wants(GfxPool::HORIZON, prio)) return; // queue full of nearer ones: no grids copied to be dropped

	auto in = std::make_shared<Horizon::Input>();
	auto grid = [](const SurfTile *t) {
		Horizon::Grid g;
		g.lvl = t->lvl;
		g.minlat = t->bnd.minlat; g.maxlat = t->bnd.maxlat;
		g.minlng = t->bnd.minlng; g.maxlng = t->bnd.maxlng;
		g.h.assign(t->elev, t->elev + Horizon::STRIDE * Horizon::STRIDE);
		return g;
	};
	int missing = 0;
	auto neighbours = [&](const SurfTile *c, bool count) {
		for (int a = -1; a <= 1; a++)
			for (int b = -1; b <= 1; b++) {
				if ((!a && !b) || c->ilat + a < 0 || c->ilat + a >= (1 << c->lvl)) continue;
				SurfTile *t = NodeAt(c->lvl, c->ilat + a, c->ilng + b);
				if (ready(t)) in->grids.push_back(grid(t));
				else if (count) missing++;
			}
	};
	in->owner = grid(o);
	in->R = obj_size;
	in->dmax = Horizon::DmaxFor((o->bnd.maxlat - o->bnd.minlat) * obj_size);
	neighbours(o, true);
	SurfTile *a3 = o, *a6;
	for (int k = 0; k < 3 && a3; k++) a3 = a3->getSurfParent();
	a6 = a3;
	for (int k = 0; k < 3 && a6; k++) a6 = a6->getSurfParent();
	if (ready(a3)) { in->grids.push_back(grid(a3)); neighbours(a3, false); }
	if (ready(a6)) in->grids.push_back(grid(a6));
	if (!ready(a3) || !ready(a6)) { // else the coarsest ready ancestor
		SurfTile *c = NULL;
		for (SurfTile *t = o->getSurfParent(); t; t = t->getSurfParent())
			if (t != a3 && t != a6 && ready(t)) c = t;
		if (c) in->grids.push_back(grid(c));
	}

	o->horAsk = now;
	auto box = horizon->box;
	int lv = o->lvl, la = o->ilat, ln = o->ilng;
	unsigned gen = o->horGen;
	GfxPool::Job j;
	j.owner = horizon->id;
	j.kind = GfxPool::HORIZON;
	j.prio = prio;
	j.run = [in, box, lv, la, ln, gen, missing, redo]() {
		auto m = std::make_shared<Horizon::Map>();
		Horizon::Compute(*in, *m);
		std::lock_guard<std::mutex> lk(box->mx);
		box->done.push_back({lv, la, ln, gen, m, missing, redo});
	};
	j.dropped = [box, lv, la, ln, gen]() {
		std::lock_guard<std::mutex> lk(box->mx);
		box->done.push_back({lv, la, ln, gen, nullptr, 0, false});
	};
	GfxPool::Get().Submit(std::move(j));
}

template<>
void TileManager2<SurfTile>::HorizonUpload ()
{
	if (!horizon) return;
	horizon->cand = nullptr;
	for (int n = 0; n < 2; n++) { // uploads per frame
		HorizonState::Result r;
		{
			std::lock_guard<std::mutex> lk(horizon->box->mx);
			if (horizon->box->done.empty()) return;
			r = std::move(horizon->box->done.front());
			horizon->box->done.pop_front();
		}
		SurfTile *t = NodeAt(r.lvl, r.ilat, r.ilng);
		if (!t || t->horGen != r.gen) { n--; continue; } // gone or made again
		t->horAsk = -1.0;
		if (!r.map) { n--; continue; } // dropped: asked again later
		VkPixels px;
		px.w = Horizon::W;
		px.h = Horizon::N;
		px.levels = 1;
		px.layers = 1;
		px.fmt = VK_FORMAT_R8G8B8A8_UNORM;
		px.data.push_back(r.map->px);
		VkTex *nt = VkCreateTexture(Dev(), px, VK_IMAGE_USAGE_SAMPLED_BIT);
		if (!nt) continue;
		delete t->htex;
		t->htex = nt;
		t->hor = r.map;
		t->horMissing = r.missing;
		if (r.redo) t->horRedone = true; // set here: a dropped redo is asked again
		t->horCheck = oapiGetSysTime();
	}
}

// not upstream: the SceneFx distance pass, the tiles this frame's main pass drew
template<>
void TileManager2<SurfTile>::RenderDepth (ShaderClass *sh, const D3DXMATRIX &mVP)
{
	if (depthTiles.empty() || depthFrame != GetScene()->GetFrameId()) return;
	struct { D3DXMATRIX mW, mVP; FVECTOR4 vTexOff, vAlb; } prm; // not upstream: vTexOff, vAlb only in the _SSGI block
	prm.mVP = mVP;
	VkConstHandle hDiff = sh->GetPSHandle("tDiff"); // not upstream: SSGI, the shader was compiled with _SSGI (design-ssgi S8)
	UINT nPrm = hDiff ? UINT(sizeof(prm)) : UINT(2 * sizeof(D3DXMATRIX));
	if (hDiff) {
		const ConstParams *cp = vp->GetScatterConst();
		bool air = vp->GetShaderID() != PLT_MOON && cp; // NewPlanet's terrain albedo: pow(tex, TrGamma) * TrExpo with air, tex without
		prm.vAlb = FVECTOR4(air ? cp->TrGamma : 1.0f, air ? cp->TrExpo : 1.0f, 0.0f, 0.0f);
	}
	VkDev *dev = Dev();
	loader->WaitForMutex();
	sh->Setup(pPatchVertexDecl, true, 0);
	dev->SetCullMode(VK_CULL_MODE_BACK_BIT);
	for (auto &r : depthTiles) {
		SurfTile *t = static_cast<SurfTile *>(r.first);
		if (!t->mesh) continue;
		prm.mW = r.second;
		if (hDiff) { // not upstream: SSGI, the tile's texture as its main pass reads it
			VkTex *tx = vp->HasTextures() ? t->Tex() : NULL;
			prm.vTexOff = t->GetTexRangeDX(t->GetTexRange());
			prm.vAlb.z = tx ? 1.0f : 0.0f;
			if (tx) sh->SetTexture(hDiff, tx, IPF_LINEAR | IPF_CLAMP);
		}
		sh->SetVSConstants("Prm", &prm, nPrm);
		sh->UpdateTextures();
		dev->SetStreamSource(0, t->mesh->pVB, 0, sizeof(VERTEX_2TEX));
		dev->SetIndices(t->mesh->pIB);
		dev->DrawIndexedPrimitive(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 0, VkPrimVerts(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, t->mesh->nf));
	}
	loader->ReleaseMutex();
}

// not upstream: scene shadow casters from the loaded tiles, not the last pass's states: down while all four children are loaded and not finer than the cut (design-scene-shadows Rev 2 MUST 3)
template<>
void TileManager2<SurfTile>::RenderCasters (ShaderClass *sh, MATRIX4 &dwmat, const vPlanet::RenderPrm &rprm, const SceneShadows &s, double minSpacing)
{
	SetRenderPrm(dwmat, 0, true, rprm); // this frame's tile matrices; the main pass sets them again
	struct { D3DXMATRIX mW, mVP; } cp;
	cp.mVP = *s.CasterVP();
	VkConstHandle hPrm = sh->GetVSHandle("Prm");
	VkDev *dev = Dev();
	double span = PI * obj_size / double(GridRes());
	loader->WaitForMutex();
	sh->Setup(pPatchVertexDecl, true, 0);
	std::function<void(QuadTreeNode<SurfTile> *)> walk = [&](QuadTreeNode<SurfTile> *node) {
		SurfTile *t = node->Entry();
		if (!t || !(t->State() & TILE_VALID) || !t->mesh) return;
		D3DXMATRIX W;
		MATRIX4toD3DMATRIX(WorldMatrix(t), W);
		D3DXVECTOR3 bc;
		D3DXVec3TransformCoord(&bc, &t->mesh->bsCnt, &W);
		if (!s.Cull(bc, t->mesh->bsRad * 1.1f + 50.0f)) return; // the margin holds the children's relief
		bool down = 0.5 * span / double(1 << t->Level()) >= minSpacing;
		for (int k = 0; k < 4 && down; k++) {
			QuadTreeNode<SurfTile> *c = node->Child(k);
			SurfTile *ct = c ? c->Entry() : NULL;
			if (!ct || !(ct->State() & TILE_VALID) || !ct->mesh) down = false;
		}
		if (down) {
			for (int k = 0; k < 4; k++) walk(node->Child(k));
			return;
		}
		if (!s.Cull(bc, t->mesh->bsRad)) return;
		cp.mW = W;
		sh->SetVSConstants(hPrm, &cp, sizeof(cp));
		dev->SetStreamSource(0, t->mesh->pVB, 0, sizeof(VERTEX_2TEX));
		dev->SetIndices(t->mesh->pIB);
		dev->DrawIndexedPrimitive(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 0, VkPrimVerts(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, t->mesh->nf));
	};
	for (int i = 0; i < 2; i++) walk(tiletree + i);
	loader->ReleaseMutex();
}

// =======================================================================

template<>
void TileManager2<SurfTile>::Render (MATRIX4 &dwmat, bool use_zbuf, const vPlanet::RenderPrm &rprm)
{
	bUseZ = use_zbuf;
	ElevModeLvl = 0;

	// set generic parameters
	SetRenderPrm (dwmat, 0, use_zbuf, rprm);

	int i;
	class Scene *scene = GetClient()->GetScene();

	// adjust scaling parameters (can only be done if no z-buffering is in use)
	if (!use_zbuf) {
		double R = obj_size;
		double D = prm.cdist*R;
		double zmax = (D - R*R/D) * 1.5;
		double zmin = max (2.0, min (zmax*1e-4, (D-R) * 0.8));

		vp->GetScatterConst()->mVP = scene->PushCameraFrustumLimits(zmin, zmax);
	}

	// build a transformation matrix for frustum testing
	MATRIX4 Mproj = _MATRIX4(scene->GetProjectionMatrix());
	Mproj.m33 = 1.0; Mproj.m43 = -1.0;  // adjust near plane to 1, far plane to infinity
	MATRIX4 Mview = _MATRIX4(scene->GetViewMatrix());
	prm.dviewproj = mul(Mview, Mproj);

	// ---------------------------------------------------------------------

	static float spec_base = 0.7f; // 0.95f;

	bool has_ripples = vp->HasRipples();

	// Choose a proper shader for a body
	//
	pShader = vp->GetShader();
	int cfg = vp->GetShaderID();

	pShader->ClearTextures();
	pShader->Setup(pPatchVertexDecl, bUseZ, 0);
	pShader->GetDevice()->SetCullMode(VK_CULL_MODE_BACK_BIT); // D3DCULL_CCW
	if (SceneShadows *ss = SceneShadows::Get()) { // not upstream: the cascades on the proxy in the main pass, off in every other draw; the atlas on binding 25 (design-scene-shadows Rev 2)
		bool on = ss->ReceiversOn(vp);
		pShader->SetPSConstants("gCsm", (void *)ss->Block(on), sizeof(CsmGpu));
		if (on) pShader->SetTexture(pShader->tShadowMap, ss->Atlas(), IPF_CLAMP | IPF_POINT);
	}

	ShaderParams* sp = vp->GetTerrainParams();
	FlowControlPS* fc = vp->GetFlowControl();
	FlowControlVS* fcv = vp->GetFlowControlVS();
	ConstParams* cp = vp->GetScatterConst();


	pShader->SetVSConstants("Const", cp, sizeof(ConstParams));
	pShader->SetPSConstants("Const", cp, sizeof(ConstParams));

	if (pShader->bAtmosphere && cfg != PLT_GIANT)
	{
		pShader->SetTexture("tLndRay", vp->GetScatterTable(RAY_LAND), IPF_LINEAR | IPF_CLAMP);
		pShader->SetTexture("tLndMie", vp->GetScatterTable(MIE_LAND), IPF_LINEAR | IPF_CLAMP);
		pShader->SetTexture("tLndAtn", vp->GetScatterTable(ATN_LAND), IPF_LINEAR | IPF_CLAMP);
		pShader->SetTexture("tSun", vp->GetScatterTable(SUN_COLOR), IPF_LINEAR | IPF_CLAMP);
		pShader->SetTexture("tNoise", GetClient()->GetNoiseTex(), IPF_LINEAR | IPF_WRAP);

		if (pShader->bWater) {
			pShader->SetTexture("tAmbient", vp->GetScatterTable(SKY_AMBIENT), IPF_LINEAR | IPF_CLAMP);
		}
	}

	if (ElevMode == eElevMode::Spherical) {
		// Force spherical rendering at shader level
		fcv->bSpherical = true;
	}	

	if (!use_zbuf) fcv->bSpherical = true;

	// -------------------------------------------------------------------
	vVessel *vFocus = scene->GetFocusVisual();
	vPlanet *vProxy = scene->GetCameraProxyVisual();


	// Setup micro textures ---------------------------------------------
	//
	if (vp->MicroCfg.bEnabled && pShader->bMicrotex)
	{
		UINT Filter = IPF_ANISOTROPIC;
		if (Config->MicroFilter == 0) Filter = IPF_POINT;
		if (Config->MicroFilter == 1) Filter = IPF_LINEAR;
		UINT micro_aniso = (1 << (max(1, Config->MicroFilter) - 1));
		pShader->SetTexture("tMicroA", vp->MicroCfg.Level[0].pTex, IPF_WRAP | Filter, micro_aniso);
		pShader->SetTexture("tMicroB", vp->MicroCfg.Level[1].pTex, IPF_WRAP | Filter, micro_aniso);
		pShader->SetTexture("tMicroC", vp->MicroCfg.Level[2].pTex, IPF_WRAP | Filter, micro_aniso);

		fc->bMicroNormals = vp->MicroCfg.bNormals;
	}

	if (has_ripples && pShader->bRipples) {
		fc->bRipples = true;
		pShader->SetTexture("tOcean", hOcean, IPF_WRAP | IPF_ANISOTROPIC, 4);
	}

	vp->InitEclipse(pShader);

	if (Config->NoPlanetAA) pShader->GetDevice()->SetMultisampleAA(false);

	// ------------------------------------------------------------------
	// TODO: render full sphere for levels < 4

	bool bStreamPass = bStream && scene->GetRenderPass() == RENDERPASS_MAINSCENE; // not upstream
	if (bStreamPass) StreamRead(); // not upstream
	if (bStream) { streamFrame++; streamNow = oapiGetSysTime(); } // not upstream

	loader->WaitForMutex();

	if (bStreamPass) StreamApply(); // not upstream
	if (scene->GetRenderPass() == RENDERPASS_MAINSCENE) { // not upstream: horizon maps in, tiles recorded for SceneFx
		HorizonUpload();
		depthTiles.clear();
		depthRec = SceneFx::DepthWanted() && vp == scene->GetCameraProxyVisual() && use_zbuf && !fcv->bSpherical; // not upstream: every reader of tDepth (TAA included)
		depthFrame = scene->GetFrameId();
	}
	else depthRec = false; // not upstream
	SceneSsr::PlanetBind(scene, vp, pShader, depthRec); // not upstream: the water's reflection parameters, every pass (design-ssr)

	// update the tree
	for (i = 0; i < 2; i++)
		ProcessNode (tiletree+i);

	vp->tile_cache = NULL;

	// render the tree
	for (i = 0; i < 2; i++)
		RenderNode (tiletree+i);

	if (scene->GetRenderPass() == RENDERPASS_MAINSCENE) HorizonSubmit(); // not upstream

	loader->ReleaseMutex();

	if (Config->NoPlanetAA) pShader->GetDevice()->SetMultisampleAA(true);

	// Backup the stats and clear counters
	if (scene->GetRenderPass() == RENDERPASS_MAINSCENE) prevstat = elvstat;
	elvstat.Elev = elvstat.Sphe = 0;

	/*if (scene->GetRenderPass() == RENDERPASS_MAINSCENE) {
		if (GetHandle() == scene->GetCameraNearBody()) {
			D3D9DebugLog("Body=%s, Elv=%d, Sph=%d", CbodyName(), prevstat.Elev, prevstat.Sphe);
			if (ElevMode == eElevMode::Elevated) D3D9DebugLog("Elevated");
			if (ElevMode == eElevMode::Spherical) D3D9DebugLog("Spherical");
			if (ElevMode == eElevMode::ForcedElevated) D3D9DebugLog("ForcedElevated");
		}
	}*/

	// Pop previous frustum configuration, must initialize mVP
	if (!use_zbuf)	vp->GetScatterConst()->mVP = scene->PopCameraFrustumLimits();
}

// -----------------------------------------------------------------------

template<>
void TileManager2<SurfTile>::RenderLabels(D3D9Pad *skp, oapi::Font **labelfont, int *fontidx)
{
	for (int i = 0; i < 2; ++i) {
		RenderNodeLabels(tiletree + i, skp, labelfont, fontidx);
	}
}

// -----------------------------------------------------------------------

template<> void TileManager2<SurfTile>::SetSubtreeLabels(QuadTreeNode<SurfTile> *node, bool activate); // g++: specialization declared before its use below

template<>
void TileManager2<SurfTile>::CreateLabels()
{
	loader->WaitForMutex();
	for (int i = 0; i < 2; ++i) {
		SetSubtreeLabels(tiletree + i, true);
	}
	loader->ReleaseMutex();
}

// -----------------------------------------------------------------------

template<>
void TileManager2<SurfTile>::DeleteLabels()
{
	loader->WaitForMutex();
	for (int i = 0; i < 2; ++i) {
		SetSubtreeLabels(tiletree + i, false);
	}
	loader->ReleaseMutex();
}

// -----------------------------------------------------------------------

template<>
void TileManager2<SurfTile>::SetSubtreeLabels(QuadTreeNode<SurfTile> *node, bool activate)
{
	if (node->Entry()) {
		if (activate) node->Entry()->CreateLabels();
		else          node->Entry()->DeleteLabels();
	}
	for (int i = 0; i < 4; ++i) {
		if (node->Child(i)) {
			SetSubtreeLabels(node->Child(i), activate);
		}
	}
}

// -----------------------------------------------------------------------

template<>
void TileManager2<SurfTile>::LoadZTrees()
{
	treeMgr = new ZTreeMgr*[ntreeMgr = 5]();
	if (cprm.tileLoadFlags & 0x0002) {
		treeMgr[0] = ZTreeMgr::CreateFromFile(m_dataRootDir.c_str(), ZTreeMgr::LAYER_SURF);
		treeMgr[1] = ZTreeMgr::CreateFromFile(m_dataRootDir.c_str(), ZTreeMgr::LAYER_MASK);
		treeMgr[2] = ZTreeMgr::CreateFromFile(m_dataRootDir.c_str(), ZTreeMgr::LAYER_ELEV);
		treeMgr[3] = ZTreeMgr::CreateFromFile(m_dataRootDir.c_str(), ZTreeMgr::LAYER_ELEVMOD);
		treeMgr[4] = ZTreeMgr::CreateFromFile(m_dataRootDir.c_str(), ZTreeMgr::LAYER_LABEL);
	}
	else {
		for (int i = 0; i < ntreeMgr; i++)
			treeMgr[i] = 0;
	}
}

// -----------------------------------------------------------------------

template<>
void TileManager2<SurfTile>::InitHasIndividualFiles()
{
	hasIndividualFiles = new bool[ntreeMgr]();
	if (cprm.tileLoadFlags & 0x0001) {
		const char *name[] = { "Surf", "Mask", "Elev", "Elev_mod", "Label" };
		char path[MAX_PATH], dummy[MAX_PATH];
		for (int i = 0; i < (int)std::size(name); ++i) {
			snprintf(path, MAX_PATH, "%s\\%s", m_dataRootDir.c_str(), name[i]);
			hasIndividualFiles[i] = FileExists(path);
		}
	}
}

// -----------------------------------------------------------------------

template<>
Tile * TileManager2<SurfTile>::SearchTile (double lng, double lat, int maxlvl, bool bOwntex) const
{
	if (lng<0) return SearchTileSub(&tiletree[0], lng, lat, maxlvl, bOwntex);
	else	   return SearchTileSub(&tiletree[1], lng, lat, maxlvl, bOwntex);
}
// -----------------------------------------------------------------------

template<>
int TileManager2<SurfTile>::GetElevation(double lng, double lat, double *elev, FVECTOR3 *nrm, SurfTile **cache)
{
	int rv = 0;
	loader->WaitForMutex();
	if (lng<0) rv = tiletree[0].Entry()->GetElevation(lng, lat, elev, nrm, cache);
	else	   rv = tiletree[1].Entry()->GetElevation(lng, lat, elev, nrm, cache);
	loader->ReleaseMutex();
	return rv;
}

// -----------------------------------------------------------------------
// not upstream: elevation from the deepest loaded tile, also for tiles out of view (sun behind terrain)

template<>
int TileManager2<SurfTile>::GetLoadedElevation(double lng, double lat, double *elev)
{
	loader->WaitForMutex();
	const SurfTile *t = static_cast<const SurfTile *>(SearchTile(lng, lat, 32, false));
	int rv = t ? t->GetElevation(lng, lat, elev, NULL, NULL, true, true) : 0;
	loader->ReleaseMutex();
	return rv;
}

template<>
void TileManager2<SurfTile>::GetLoadedElevations(int n, const double *lng, const double *lat, double *elev, int *rc)
{
	loader->WaitForMutex();
	for (int i = 0; i < n; i++) {
		const SurfTile *t = static_cast<const SurfTile *>(SearchTile(lng[i], lat[i], 32, false));
		int r = t ? t->GetElevation(lng[i], lat[i], elev + i, NULL, NULL, true, true) : 0;
		if (r != 1) elev[i] = 0.0;
		if (rc) rc[i] = r;
	}
	loader->ReleaseMutex();
}

// -----------------------------------------------------------------------

template<>
void TileManager2<SurfTile>::Unload(int lvl)
{
	tiletree[0].DelAbove(lvl);
	tiletree[1].DelAbove(lvl);	
}

// -----------------------------------------------------------------------

template<>
void TileManager2<SurfTile>::Pick(D3DXVECTOR3 &vRay, TILEPICK *pPick)
{
	std::list<Tile *> tiles;

	pPick->d = 1e12f;

	// Give me a list of rendered tiles ----------------------------------------------
	//
	QueryTiles(&tiletree[0], tiles);
	QueryTiles(&tiletree[1], tiles);

	for (auto tile : tiles)	tile->Pick(&(tile->mWorld), &vRay, *pPick);
}

// -----------------------------------------------------------------------

template<>
SURFHANDLE TileManager2<SurfTile>::SeekTileTexture(int iLng, int iLat, int level, int flags)
{
	bool bOk = false;
	VkTex *pTex = NULL;
	const VkImageUsageFlags SeekUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // D3DPOOL_DEFAULT texture for a SurfNative
		
	if (flags & gcTileFlags::TEXTURE)
	{
		if (flags & gcTileFlags::CACHE)
		{
			char path[MAX_PATH];
			snprintf(path, MAX_PATH, "%s\\Surf\\%02d\\%06d\\%06d.dds", m_dataRootDir.c_str(), level + 4, iLat, iLng);
			bOk = ((pTex = VkCreateTextureFromFile(Dev(), oapiResolvePath(path).c_str(), 0, 0, 0, VK_FORMAT_UNDEFINED, SWZ_NONE, SeekUsage)) != NULL); // D3DXCreateTextureFromFileEx: full mip chain (D3DX_FILTER_BOX)
		}

		if (flags & gcTileFlags::TREE)
		{
			if (!bOk) {
				if (ZTreeManager(0)) {
					BYTE *buf;
					DWORD ndata = ZTreeManager(0)->ReadData(level + 4, iLat, iLng, &buf);
					if (ndata) {
						VkPixels px, cv; // D3DXCreateTextureFromFileInMemoryEx: mips and format from file
						if (VkLoadPixelsFromMemory(buf, ndata, px) && VkConvertPixels(px, cv, VK_FORMAT_UNDEFINED, SWZ_NONE, 0, 0, VKTEX_FROM_FILE) &&
							(pTex = VkCreateTexture(Dev(), cv, SeekUsage))) bOk = true;
						ZTreeManager(0)->ReleaseData(buf);
					}
				}
			}
		}
	}

	if (flags & gcTileFlags::MASK)
	{
		if (flags & gcTileFlags::CACHE)
		{
			char path[MAX_PATH];
			snprintf(path, MAX_PATH, "%s\\Mask\\%02d\\%06d\\%06d.dds", m_dataRootDir.c_str(), level + 4, iLat, iLng);
			bOk = ((pTex = VkCreateTextureFromFile(Dev(), oapiResolvePath(path).c_str(), 0, 0, 0, VK_FORMAT_UNDEFINED, SWZ_NONE, SeekUsage)) != NULL); // D3DXCreateTextureFromFileEx: full mip chain (D3DX_FILTER_BOX)
		}

		if (flags & gcTileFlags::TREE)
		{
			if (!bOk) {
				if (ZTreeManager(1)) {
					BYTE* buf;
					DWORD ndata = ZTreeManager(1)->ReadData(level + 4, iLat, iLng, &buf);
					if (ndata) {
						VkPixels px, cv; // D3DXCreateTextureFromFileInMemoryEx: mips and format from file
						if (VkLoadPixelsFromMemory(buf, ndata, px) && VkConvertPixels(px, cv, VK_FORMAT_UNDEFINED, SWZ_NONE, 0, 0, VKTEX_FROM_FILE) &&
							(pTex = VkCreateTexture(Dev(), cv, SeekUsage))) bOk = true;
						ZTreeManager(1)->ReleaseData(buf);
					}
				}
			}
		}
	}

	if (bOk && pTex) return new SurfNative(pTex, OAPISURFACE_TEXTURE);
	return NULL;
}

// -----------------------------------------------------------------------

template<>
bool TileManager2<SurfTile>::HasTileData(int iLng, int iLat, int level, int flags)
{
	bool bOk = false;
	
	if (flags & gcTileFlags::TEXTURE) {
		if (flags & gcTileFlags::CACHE) {
			char path[MAX_PATH];
			snprintf(path, MAX_PATH, "%s\\Surf\\%02d\\%06d\\%06d.dds", m_dataRootDir.c_str(), level + 4, iLat, iLng);
			bOk = FileExists(path);

		}
		if (flags & gcTileFlags::TREE) if (!bOk && ZTreeManager(0)) if (ZTreeManager(0)->Idx(level + 4, iLat, iLng) != ((DWORD)-1)) bOk = true;
	}

	if (flags & gcTileFlags::MASK) {
		if (flags & gcTileFlags::CACHE) {
			char path[MAX_PATH];
			snprintf(path, MAX_PATH, "%s\\Mask\\%02d\\%06d\\%06d.dds", m_dataRootDir.c_str(), level + 4, iLat, iLng);
			bOk = FileExists(path);
		}
		if (flags & gcTileFlags::TREE) if (!bOk && ZTreeManager(1)) if (ZTreeManager(1)->Idx(level + 4, iLat, iLng) != ((DWORD)-1)) bOk = true;
	}

	if (flags & gcTileFlags::ELEVATION) {
		if (flags & gcTileFlags::CACHE) {
			char path[MAX_PATH];
			snprintf(path, MAX_PATH, "%s\\Elev\\%02d\\%06d\\%06d.elv", m_dataRootDir.c_str(), level + 4, iLat, iLng);
			bOk = FileExists(path);
		}
		if (flags & gcTileFlags::TREE) if (!bOk && ZTreeManager(2)) if (ZTreeManager(2)->Idx(level + 4, iLat, iLng) != ((DWORD)-1)) bOk = true;
	}

	return bOk;
}

// -----------------------------------------------------------------------

template<>
float* TileManager2<SurfTile>::BrowseElevationData(int lvl, int ilat, int ilng, int flags, ELEVFILEHEADER* _hdr)
{
	ELEVFILEHEADER ehdr;
	const int ndat = TILE_ELEVSTRIDE * TILE_ELEVSTRIDE;
	float* elev = NULL;
	char path[MAX_PATH];
	char fname[MAX_PATH]; // not upstream: was 128, a 255-char body name and the tile path always fit
	FILE* f;
	int i;

	// Elevation data
	if (flags & gcTileFlags::CACHE) { // try loading from individual tile file
		snprintf(path, MAX_PATH, "%s\\Elev\\%02d\\%06d\\%06d.elv", m_dataRootDir.c_str(), lvl + 4, ilat, ilng);
		if ((f = fopen(oapiResolvePath(path).c_str(), "rb")) && fread(&ehdr, sizeof(ELEVFILEHEADER), 1, f) != 1) { fclose(f); f = NULL; } // not upstream: a short header counts as no file
		if (f) {
			bool rd = true; // not upstream: false after a short data read
			elev = new float[ndat];
			if (ehdr.hdrsize != sizeof(ELEVFILEHEADER)) fseek(f, ehdr.hdrsize, SEEK_SET);

			ehdr.scale = 1.0;

			switch (ehdr.dtype) {
			case 0: // flat tile, defined by offset
				for (i = 0; i < ndat; i++) elev[i] = 0.0f;
				break;
			case 8: {
				UINT8* tmp = new UINT8[ndat];
				rd = fread(tmp, sizeof(UINT8), ndat, f) == (size_t)ndat;
				for (i = 0; rd && i < ndat; i++) elev[i] = float(tmp[i]);
				delete[]tmp;
				break;
			}
			case -16: {
				INT16* tmp = new INT16[ndat];
				rd = fread(tmp, sizeof(INT16), ndat, f) == (size_t)ndat;
				for (i = 0; rd && i < ndat; i++) elev[i] = float(tmp[i]);
				delete[]tmp;
				break;
			}
			default: rd = false; break; // not upstream: an unknown dtype counts as missing (elev would stay uninitialised)
			}
			fclose(f);
			if (!rd) { delete[] elev; elev = NULL; } // not upstream: a short file counts as missing, the archive is tried next
		}
	}
	if (!elev && (flags & gcTileFlags::TREE) && ZTreeManager(2)) { // try loading from compressed archive
		BYTE* buf;
		DWORD ndata = ZTreeManager(2)->ReadData(lvl + 4, ilat, ilng, &buf);
		if (ndata && !ElevNodeOk(buf, ndata, ndat)) { ZTreeManager(2)->ReleaseData(buf); ndata = 0; } // not upstream: a short or corrupt node is no tile
		if (ndata) {
			BYTE* p = buf;
			elev = new float[ndat];
			memcpy(&ehdr, p, sizeof(ELEVFILEHEADER));
			p += ehdr.hdrsize;
			INT16 v; // not upstream: each sample copied, p is odd after an odd hdrsize
			switch (ehdr.dtype) {
			case 0:
				for (i = 0; i < ndat; i++) elev[i] = 0.0f; break;
			case 8:
				for (i = 0; i < ndat; i++) elev[i] = float(*p++); break;
			case -16:
				for (i = 0; i < ndat; i++) { memcpy(&v, p + i * sizeof(INT16), sizeof(INT16)); elev[i] = float(v); } break; // not upstream: memcpy, as above
			}
			ZTreeManager(2)->ReleaseData(buf);
		}
	}

	if (elev) {
		if (ehdr.scale != 1.0) for (i = 0; i < ndat; i++) elev[i] = trunc(elev[i] * float(ehdr.scale));
		if (ehdr.offset) for (i = 0; i < ndat; i++)	elev[i] += trunc(float(ehdr.offset));
	}


	// Elevation mod data
	if (elev && (flags & gcTileFlags::MOD)) {
		bool ok = false;
		ELEVFILEHEADER hdr;
		if (flags & gcTileFlags::CACHE) { // try loading from individual tile file
			snprintf(fname, std::size(fname), "%s\\Elev_mod\\%02d\\%06d\\%06d.elv", CbodyName(), lvl + 4, ilat, ilng);
			bool found = GetClient()->TexturePath(fname, path);
			if (found && (f = fopen(oapiResolvePath(path).c_str(), "rb")) && fread(&hdr, sizeof(ELEVFILEHEADER), 1, f) != 1) { fclose(f); f = NULL; } // not upstream: a short header counts as no file
			if (found && f) {
				bool rd = true; // not upstream: false after a short data read
				if (hdr.hdrsize != sizeof(ELEVFILEHEADER)) fseek(f, hdr.hdrsize, SEEK_SET);

				switch (hdr.dtype)
				{
				case 0: // overwrite the entire tile with a flat offset
					for (i = 0; i < ndat; i++) elev[i] = float(hdr.offset);
					break;

				case 8: {
					const UINT8 mask = UCHAR_MAX;
					UINT8* tmp = new UINT8[ndat];
					rd = fread(tmp, sizeof(UINT8), ndat, f) == (size_t)ndat;
					for (i = 0; rd && i < ndat; i++)
						if (tmp[i] != mask)
							elev[i] = float(trunc(float(tmp[i]) * hdr.scale) + trunc(hdr.offset));
					delete[]tmp;
					break;
				}
				case -16: {
					const INT16 mask = SHRT_MAX;
					INT16* tmp = new INT16[ndat];
					rd = fread(tmp, sizeof(INT16), ndat, f) == (size_t)ndat;
					for (i = 0; rd && i < ndat; i++)
						if (tmp[i] != mask)
							elev[i] = float(trunc(float(tmp[i]) * hdr.scale) + trunc(hdr.offset));
					delete[]tmp;
					break;
				}
				}
				fclose(f);
				ok = rd; // not upstream: a short file is not applied, the archive is tried
			}
		}

		if (!ok && (flags & gcTileFlags::TREE) && ZTreeManager(3)) { // try loading from compressed archive
			BYTE* buf;
			DWORD ndata = ZTreeManager(3)->ReadData(lvl + 4, ilat, ilng, &buf);
			if (ndata && !ElevNodeOk(buf, ndata, ndat)) { ZTreeManager(3)->ReleaseData(buf); ndata = 0; } // not upstream: a short or corrupt node is no mod
			if (ndata) {
				BYTE* p = buf;
				ELEVFILEHEADER* phdr = (ELEVFILEHEADER*)p;
				p += phdr->hdrsize;

				switch (phdr->dtype)
				{
				case 0:
					for (i = 0; i < ndat; i++) elev[i] = float(phdr->offset);
					break;
				case 8: {
					const UINT8 mask = UCHAR_MAX;
					for (i = 0; i < ndat; i++)
						if (p[i] != mask)
							elev[i] = float(trunc(float(p[i]) * phdr->scale) + trunc(phdr->offset));
					break;
				}
				case -16: {
					const INT16 mask = SHRT_MAX;
					INT16 v; // not upstream: each sample copied, p is odd after an odd hdrsize
					for (i = 0; i < ndat; i++) { // not upstream: memcpy, as above
						memcpy(&v, p + i * sizeof(INT16), sizeof(INT16)); // not upstream: as above
						if (v != mask) // not upstream: v
							elev[i] = float(trunc(float(v) * phdr->scale) + trunc(phdr->offset)); // not upstream: v
					} // not upstream: as above
					break;
				}
				}
				ZTreeManager(3)->ReleaseData(buf);
			}
		}
	}

	if (_hdr) memcpy(_hdr, &ehdr, sizeof(ELEVFILEHEADER));

	return elev;
}


// -----------------------------------------------------------------------
// not upstream: the horizon map over a point for the trees: the deepest loaded tile's owner, else the first ancestor with a map

template<>
std::shared_ptr<const Horizon::Map> TileManager2<SurfTile>::HorizonAt (double lat, double lng, double bnd[4])
{
	std::shared_ptr<const Horizon::Map> m;
	loader->WaitForMutex();
	SurfTile *t = static_cast<SurfTile *>(SearchTile(lng, lat, 32, false));
	SurfTile *o = t ? t->ElevOwner() : nullptr;
	while (o && !o->hor) {
		QuadTreeNode<SurfTile> *n = o->node ? o->node->Parent() : nullptr;
		o = n ? n->Entry() : nullptr;
	}
	if (o) {
		m = o->hor;
		bnd[0] = o->bnd.minlat; bnd[1] = o->bnd.maxlat;
		bnd[2] = o->bnd.minlng; bnd[3] = o->bnd.maxlng;
	}
	loader->ReleaseMutex();
	return m;
}

// -----------------------------------------------------------------------
// not upstream: height fog maps (design-height-fog 6, Rev 2): the ready elevation grids over a box at one level, else the finest ready ancestor

template<>
int TileManager2<SurfTile>::ElevGrids (double lat0, double lat1, double lng0, double lng1, int lvl, int maxn, bool heights, const std::vector<unsigned> &have, std::vector<Horizon::Grid> &out, std::vector<unsigned> &keys)
{
	out.clear();
	keys.clear();
	if (lvl < 0) return 0;
	int ia, ib, ja, jb, nlng = 2 << lvl;
	FogMath::TileRange(lat0, lat1, lng0, lng1, lvl, &ia, &ib, &ja, &jb);
	if (ib < ia || (ib - ia + 1) * (jb - ja + 1) > maxn) return -1;
	auto ready = [](const SurfTile *t) { return t && t->elevReady.load(std::memory_order_acquire) && t->elev; };
	loader->WaitForMutex();
	for (int i = ia; i <= ib; i++)
		for (int j = ja; j <= jb; j++) {
			const SurfTile *t = NULL;
			int a = i, b = ((j % nlng) + nlng) % nlng;
			for (int l = lvl; l >= 0 && !t; l--, a >>= 1, b >>= 1) {
				SurfTile *c = NodeAt(l, a, b);
				if (ready(c)) t = c;
			}
			if (!t || std::find(keys.begin(), keys.end(), t->horGen) != keys.end()) continue;
			keys.push_back(t->horGen);
			Horizon::Grid g;
			g.lvl = t->lvl;
			g.minlat = t->bnd.minlat; g.maxlat = t->bnd.maxlat;
			g.minlng = t->bnd.minlng; g.maxlng = t->bnd.maxlng;
			if (heights && std::find(have.begin(), have.end(), t->horGen) == have.end()) g.h.assign(t->elev, t->elev + Horizon::STRIDE * Horizon::STRIDE);
			out.push_back(std::move(g));
		}
	loader->ReleaseMutex();
	return int(out.size());
}
