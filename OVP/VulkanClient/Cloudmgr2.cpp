// ==============================================================
//   ORBITER VISUALISATION PROJECT (OVP)
//   Copyright (C) 2006-2026 Martin Schweiger
//   Dual licensed under GPL v3 and LGPL v3
// ==============================================================

// ==============================================================
// cloudmgr2.cpp
// Rendering of planetary cloud layers, engine v2, including a simple
// LOD (level-of-detail) algorithm for cloud patch resolution.
// ==============================================================

#include "Cloudmgr2.h"
#include "D3D9Catalog.h"
#include "D3D9Config.h"
#include "VkTexFile.h"
#include "VolCloudMap.h"
#include <algorithm>

// not upstream: the tile's alpha, box-filtered to 256x256, for the volume clouds
static std::shared_ptr<const std::vector<uint8_t>> CloudDensity (const VkPixels &px)
{
	VkPixels rgba;
	if (!VkConvertPixels(px, rgba, VK_FORMAT_B8G8R8A8_UNORM, SWZ_NONE, 0, 0, 1) || rgba.w < 1 || rgba.h < 1 || rgba.data.empty()) return nullptr;
	const int n = VolCloud::DENSN;
	const std::vector<BYTE> &s = rgba.Level(0);
	if (s.size() < size_t(rgba.w) * rgba.h * 4) return nullptr;
	auto d = std::make_shared<std::vector<uint8_t>>(size_t(n) * n);
	for (int j = 0; j < n; j++) {
		UINT y0 = j * rgba.h / n, y1 = std::max(y0 + 1, (j + 1) * rgba.h / n);
		for (int i = 0; i < n; i++) {
			UINT x0 = i * rgba.w / n, x1 = std::max(x0 + 1, (i + 1) * rgba.w / n);
			UINT sum = 0, cnt = 0;
			for (UINT y = y0; y < y1; y++)
				for (UINT x = x0; x < x1; x++, cnt++) sum += s[(size_t(y) * rgba.w + x) * 4 + 3];
			(*d)[size_t(j) * n + i] = uint8_t(sum / cnt);
		}
	}
	return d;
}

// =======================================================================
// =======================================================================

CloudTile::CloudTile (TileManager2Base *_mgr, int _lvl, int _ilat, int _ilng)
: Tile (_mgr, _lvl, _ilat, _ilng)
{
	cmgr = static_cast<TileManager2<CloudTile>* > (_mgr);
	node = 0;
	imicrolvl = 6;	// Cloud micro resolution level
	cloudalt = mgr->GetPlanet()->prm.cloudalt;
	if (Config->TileMipmaps == 2) bMipmaps = true;
	if (Config->TileMipmaps == 1 && _lvl < 10) bMipmaps = true;
}

// -----------------------------------------------------------------------

CloudTile::~CloudTile ()
{
	if (tex && owntex) g_pTexmgr_tt->Free(tex);
}

// -----------------------------------------------------------------------

void CloudTile::PreLoad()
{
	assert(tex == nullptr);

	// Configure microtexture range for "Water texture" and "Cloud microtexture".
	GetParentMicroTexRange(&microrange);
	
	VkDev *pDev = mgr->Dev();
	VkPixels *pSysSrf = nullptr; // D3DPOOL_SYSTEMMEM texture

	if (cmgr->DoLoadIndividualFiles(0)) { // try loading from individual tile file
		char path[MAX_PATH];
		snprintf (path, MAX_PATH, "%s\\Cloud\\%02d\\%06d\\%06d.dds", mgr->DataRootDir().c_str(), lvl+4, ilat, ilng);
		LoadTextureFile(path, &pSysSrf);
	}
	if (!pSysSrf && cmgr->ZTreeManager(0)) { // try loading from compressed archive
		BYTE *buf;
		DWORD ndata = cmgr->ZTreeManager(0)->ReadData(lvl+4, ilat, ilng, &buf);
		if (ndata) {
			LoadTextureFromMemory(buf, ndata, &pSysSrf);
			cmgr->ZTreeManager(0)->ReleaseData(buf);
		}
	}

	owntex = true;

	if (pSysSrf && Config->VolClouds && lvl + 4 >= 5) { // not upstream: before the pixels go; none from level 4 and below, 40 km pixels and coarser
		dens = CloudDensity(*pSysSrf);
		densReady.store(true, std::memory_order_release);
	}

	if (CreateTexture(pDev, pSysSrf, &tex) != true) {
		if (GetParentSubTexRange(&texrange)) {
			tex = getParent()->Tex();
			owntex = false;
		}
		else tex = nullptr;
	}
	
	SAFE_DELETE(pSysSrf); // Release
}


// -----------------------------------------------------------------------

void CloudTile::Load ()
{
	
	bool shift_origin = (lvl >= 4);
	int res = mgr->GridRes();

	if (!lvl) {
		// create hemisphere mesh for western or eastern hemispheres
		mesh = CreateMesh_hemisphere (res, 0, cloudalt);
	//} else if (ilat == 0 || ilat == (1<<lvl)-1) {
		// create triangular patch for north/south pole region
	//	mesh = CreateMesh_tripatch (TILE_PATCHRES, elev, shift_origin, &vtxshift);
	} else {
		// create rectangular patch
		mesh = CreateMesh_quadpatch (res, res, 0, 1.0, cloudalt, &texrange, shift_origin, &vtxshift);
	}
}

// -----------------------------------------------------------------------

void CloudTile::Render()
{
	Tile::Render();

	VkDev *pDev = mgr->Dev();
	vPlanet* vPlanet = mgr->GetPlanet();
	PlanetShader* pShader = mgr->GetShader();
	ShaderParams* sp = vPlanet->GetTerrainParams();

	int cfg = vPlanet->GetShaderID();

	// ---------------------------------------------------------------------
	// Feed tile specific data to shaders
	//
	// ----------------------------------------------------------------------

	bool bTexture = (tex && vPlanet->HasTextures());

	if (bTexture)
	{
		pShader->SetTexture(pShader->tDiff, tex, IPF_ANISOTROPIC | IPF_CLAMP, Config->Anisotrophy);

		sp->vCloudOff = GetTexRangeDX(&texrange);
		sp->vMicroOff = GetTexRangeDX(&microrange);
		sp->fAlpha = 1.0f;
		sp->fBeta = 1.0f;
		sp->mWorld = mWorld;
		Scene *scene = mgr->GetScene(); // not upstream: the volume replaces the layer near the camera, main pass only
		bool bVol = scene->GetRenderPass() == RENDERPASS_MAINSCENE && vPlanet == scene->GetCameraProxyVisual();
		sp->vVolCld = FVECTOR4(35e3f, 60e3f, bVol ? scene->GetVolCloudFade() : 0.0f, 0.0f);

		// -------------------------------------------------------------------
		// render surface mesh

		pShader->SetPSConstants(pShader->Prm, sp, sizeof(ShaderParams));
		pShader->SetVSConstants(pShader->PrmVS, sp, sizeof(ShaderParams));

		pShader->UpdateTextures();

		pDev->SetStreamSource(0, mesh->pVB, 0, sizeof(VERTEX_2TEX));
		pDev->SetIndices(mesh->pIB);
		pDev->DrawIndexedPrimitive(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 0, VkPrimVerts(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, mesh->nf));
	}
}


// =======================================================================
// =======================================================================

template<>
void TileManager2<CloudTile>::Render (MATRIX4 &dwmat, bool use_zbuf, const vPlanet::RenderPrm &rprm)
{
	// set generic parameters
	SetRenderPrm (dwmat, rprm.cloudrot, use_zbuf, rprm);

	int i;
	class Scene *scene = GetClient()->GetScene();

	// adjust scaling parameters (can only be done if no z-buffering is in use)
	if (!use_zbuf) {
		double R = obj_size;
		double Rc = R+rprm.cloudalt;
		double D = prm.cdist*R;
		double zmin, zmax;
		if (D > Rc) {
			zmax = sqrt(D*D-Rc*Rc);
			zmin = D-Rc;
		} else {
			zmax = sqrt(D*D-R*R) + sqrt(Rc*Rc-R*R);
			zmin = Rc-D;
		}
		zmin = max (2.0, min (zmax*1e-4, zmin));

		vp->GetScatterConst()->mVP = scene->PushCameraFrustumLimits(zmin, zmax);
	}

	// build a transformation matrix for frustum testing
	MATRIX4 Mproj = _MATRIX4(scene->GetProjectionMatrix());
	Mproj.m33 = 1.0; Mproj.m43 = -1.0;  // adjust near plane to 1, far plane to infinity
	MATRIX4 Mview = _MATRIX4(scene->GetViewMatrix());
	prm.dviewproj = mul(Mview,Mproj);

	// ---------------------------------------------------------------------
	// Initialize shading technique and feed planet specific data to shaders
	//

	ElevMode = eElevMode::Spherical;
	ElevModeLvl = 0;

	ShaderParams* sp = vp->GetTerrainParams();
	FlowControlPS* fc = vp->GetFlowControl();
	fc->bBelowClouds = vp->CameraAltitude() < rprm.cloudalt;

	int cfg = vp->GetShaderID();

	// Select cloud layer shader
	pShader = (cfg == PLT_GIANT ? vp->GetShader(PLT_G_CLOUDS) : vp->GetShader(PLT_CLOUDS));
	
	pShader->Setup(pPatchVertexDecl, false, 1);
	pShader->ClearTextures();


	// Check Eclipse conditions -------------------------------------------
	//

	vp->InitEclipse(pShader);

	pShader->SetPSConstants("Const", vp->GetScatterConst(), sizeof(ConstParams));
	pShader->SetVSConstants("Const", vp->GetScatterConst(), sizeof(ConstParams));
	pShader->SetPSConstants("Flow", fc, sizeof(FlowControlPS));

	if (cfg != PLT_GIANT)
	{
		if (Config->CloudMicro) {
			pShader->SetTexture("tCloudMicro", hCloudMicro, IPF_ANISOTROPIC | IPF_WRAP, Config->Anisotrophy);
			if (Config->bCloudNormals)
				pShader->SetTexture("tCloudMicroNorm", hCloudMicroNorm, IPF_ANISOTROPIC | IPF_WRAP, Config->Anisotrophy);
		}
		pShader->SetTexture("tSun", vp->GetScatterTable(SUN_COLOR), IPF_LINEAR | IPF_CLAMP);
		pShader->SetTexture("tLndRay", vp->GetScatterTable(RAY_LAND), IPF_LINEAR | IPF_CLAMP);
		pShader->SetTexture("tLndAtn", vp->GetScatterTable(ATN_LAND), IPF_LINEAR | IPF_CLAMP);
		//pShader->SetTexture("tSkyRayColor", vp->GetScatterTable(RAY_COLOR), IPF_LINEAR | IPF_CLAMP);
		//pShader->SetTexture("tSkyMieColor", vp->GetScatterTable(MIE_COLOR), IPF_LINEAR | IPF_CLAMP);
	}

	// TODO: render full sphere for levels < 4

	loader->WaitForMutex();

	// update the tree
	for (i = 0; i < 2; i++)
		ProcessNode (tiletree+i);

	// render the tree
	for (i = 0; i < 2; i++)
		RenderNode (tiletree+i);
	
	loader->ReleaseMutex ();

	// Pop previous frustum configuration, must initialize mVP
	if (!use_zbuf)	vp->GetScatterConst()->mVP = scene->PopCameraFrustumLimits();
}

// -----------------------------------------------------------------------

template<>
int TileManager2<CloudTile>::Coverage (double latmin, double latmax, double lngmin, double lngmax, int maxlvl, const Tile **tbuf, int nt) const
{
	double crot = GetPlanet()->prm.cloudrot;
	//double crot = prm.rprm->cloudrot;
	lngmin = lngmin + crot;
	lngmax = lngmax + crot;
	if (lngmin > PI) {
		lngmin -= PI2;
		lngmax -= PI2;
	}
	int nfound = 0;
	for (int i = 0; i < 2; i++) {
		CheckCoverage (tiletree+i, latmin, latmax, lngmin, lngmax, maxlvl, tbuf, nt, &nfound);
	}
	return nfound;
}

// -----------------------------------------------------------------------

template<>
void TileManager2<CloudTile>::LoadZTrees()
{
	treeMgr = new ZTreeMgr*[ntreeMgr = 1]();
	if (cprm.tileLoadFlags & 0x0002) {
		treeMgr[0] = ZTreeMgr::CreateFromFile(m_dataRootDir.c_str(), ZTreeMgr::LAYER_CLOUD);
	}
}

// -----------------------------------------------------------------------

template<>
void TileManager2<CloudTile>::InitHasIndividualFiles()
{
	hasIndividualFiles = new bool[ntreeMgr]();
	if (cprm.tileLoadFlags & 0x0001) {
		char path[MAX_PATH], dummy[MAX_PATH];
		snprintf(path, MAX_PATH, "%s\\Cloud", m_dataRootDir.c_str());
		hasIndividualFiles[0] = FileExists(path);
	}
}

// -----------------------------------------------------------------------

template<>
Tile * TileManager2<CloudTile>::SearchTile (double lng, double lat, int maxlvl, bool bOwntex) const
{
	if (lng<0) return SearchTileSub(&tiletree[0], lng, lat, maxlvl, bOwntex);
	else	   return SearchTileSub(&tiletree[1], lng, lat, maxlvl, bOwntex);
}

// -----------------------------------------------------------------------

template<>
void TileManager2<CloudTile>::Unload(int lvl)
{
	tiletree[0].DelAbove(lvl);
	tiletree[1].DelAbove(lvl);
}


// -----------------------------------------------------------------------
// not upstream: the finest tiles with density under a box around (lat, lng) in the cloud frame, at most 16

template<>
int TileManager2<CloudTile>::VolTiles (double lat, double lng, double half, std::vector<VolCloud::Src> &out)
{
	out.clear();
	std::vector<const CloudTile*> seen;
	double dlng = half / std::max(0.05, cos(lat));
	loader->WaitForMutex();
	for (int j = -3; j <= 3 && out.size() < 16; j++)
		for (int i = -3; i <= 3 && out.size() < 16; i++) {
			double la = std::max(-PI05 + 1e-6, std::min(PI05 - 1e-6, lat + half * j / 3.0));
			const CloudTile *t = (const CloudTile *)SearchTile(wrap(lng + dlng * i / 3.0), la, 32, true);
			while (t && !t->Density()) t = (const CloudTile *)t->getParent();
			if (!t || std::find(seen.begin(), seen.end(), t) != seen.end()) continue;
			seen.push_back(t);
			VolCloud::Src s;
			s.lvl = t->Level();
			s.minlat = t->bnd.minlat; s.maxlat = t->bnd.maxlat;
			s.minlng = t->bnd.minlng; s.maxlng = t->bnd.maxlng;
			s.d = t->Density();
			out.push_back(s);
		}
	// the finest ancestor with density over the whole box: fine tiles the probes missed leave no holes
	double la0 = std::max(-PI05, lat - half), la1 = std::min(PI05, lat + half), lc = wrap(lng);
	auto covers = [&](const CloudTile *t) {
		if (t->bnd.minlat > la0 || t->bnd.maxlat < la1) return false;
		for (int w = -1; w <= 1; w++)
			if (t->bnd.minlng <= lc - dlng + w * 2.0 * PI && t->bnd.maxlng >= lc + dlng + w * 2.0 * PI) return true;
		return false;
	};
	const CloudTile *c = (const CloudTile *)SearchTile(lc, std::max(-PI05 + 1e-6, std::min(PI05 - 1e-6, lat)), 32, true);
	while (c && !(c->Density() && covers(c))) c = (const CloudTile *)c->getParent();
	if (c && std::find(seen.begin(), seen.end(), c) == seen.end()) {
		VolCloud::Src s;
		s.lvl = c->Level();
		s.minlat = c->bnd.minlat; s.maxlat = c->bnd.maxlat;
		s.minlng = c->bnd.minlng; s.maxlng = c->bnd.maxlng;
		s.d = c->Density();
		out.push_back(s);
	}
	loader->ReleaseMutex();
	return int(out.size());
}
