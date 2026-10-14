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
#include "CloudAtlas.h" // not upstream
#include "GfxPool.h"    // not upstream
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>

// not upstream: the cloud layer's seamless alpha atlas, built on the pool (design-cloud-seams rev 4)
struct CloudAtlasState {
	struct Box { std::mutex mx; bool done = false, ok = false; int lvl = 0, real = 0, total = 0; VkPixels px; };
	int id = 0;
	int state = 0;      // 0 none, 1 building, 2 ready, 3 failed
	int lvl = 0;        // file level of the source tiles
	int real = 0, total = 0; // tiles read at lvl; the rest are filled from an ancestor
	bool loose = false, tree = false, pending = false;
	int maxw = 0;
	double tcheck = 0.0; // last look for newly streamed tiles
	std::shared_ptr<Box> box;
	VkTex *tex = nullptr;
};

void CloudAtlasFree (CloudAtlasState *a)
{
	if (!a) return;
	if (GfxPool *p = GfxPool::Peek()) p->Unregister(a->id);
	delete a->tex;
	delete a;
}

VkTex *CloudAtlasTex (const CloudAtlasState *a)
{
	return (a && a->state == 2) ? a->tex : nullptr;
}

int CloudAtlasLevel (const CloudAtlasState *a)
{
	return (a && a->state == 2) ? a->lvl : 0;
}

// the cloud tile sources, as CloudTile::PreLoad reads them; ancestors kept for filling
struct CloudAtlasSrc {
	std::string root;
	bool loose = false;
	ZTreeMgr *zt = nullptr;
	std::map<long long, std::vector<BYTE>> cache;

	std::string Path (int lvl, int r, int c) const
	{
		char path[MAX_PATH];
		snprintf(path, MAX_PATH, "%s\\Cloud\\%02d\\%06d\\%06d.dds", root.c_str(), lvl, r, c);
		return oapiResolvePath(path);
	}
	bool Has (int lvl, int r, int c) const
	{
		if (loose && std::filesystem::exists(Path(lvl, r, c))) return true;
		if (!zt) return false;
		DWORD i = zt->Idx(lvl, r, c);
		return i != (DWORD)-1 && zt->NodeSizeInflated(i) > 0;
	}
	bool Read (int lvl, int r, int c, std::vector<BYTE> &a) // 512x512 alpha
	{
		using CloudAtlas::TILE;
		VkPixels src, rgba;
		bool got = loose && VkLoadPixels(Path(lvl, r, c).c_str(), src);
		if (!got && zt) {
			BYTE *buf;
			DWORD n = zt->ReadData(lvl, r, c, &buf);
			if (n) {
				got = VkLoadPixelsFromMemory(buf, n, src);
				zt->ReleaseData(buf);
			}
		}
		if (!got || !VkConvertPixels(src, rgba, VK_FORMAT_B8G8R8A8_UNORM, SWZ_NONE, 0, 0, 1) || rgba.w != UINT(TILE) || rgba.h != UINT(TILE)) return false;
		const std::vector<BYTE> &s = rgba.Level(0);
		a.resize(size_t(TILE) * TILE);
		for (size_t i = 0; i < a.size(); i++) a[i] = s[i * 4 + 3]; // alpha holds the clouds, rgb is white
		return true;
	}
	const std::vector<BYTE> *Ancestor (int lvl, int r, int c)
	{
		long long key = ((long long)lvl << 40) | ((long long)r << 20) | c;
		auto it = cache.find(key);
		if (it == cache.end()) {
			std::vector<BYTE> a;
			if (!Read(lvl, r, c, a)) a.clear();
			it = cache.emplace(key, std::move(a)).first;
		}
		return it->second.empty() ? nullptr : &it->second;
	}
	// the finest level up to MAXLVL, no wider than maxw, with at least half its tiles; 0 if none
	int Pick (int maxw, int &real, int &total) const
	{
		using namespace CloudAtlas;
		for (int l = MAXLVL; l >= MINLVL; l--) {
			if (Cols(l) * TILE > maxw) continue;
			total = Rows(l) * Cols(l); real = 0;
			for (int r = 0; r < Rows(l); r++)
				for (int c = 0; c < Cols(l); c++) real += Has(l, r, c) ? 1 : 0;
			if (2 * real >= total) return l;
		}
		real = total = 0;
		return 0;
	}
};

// a missing tile as the tile manager shows it: its part of the nearest ancestor, bilinear and clamped to that texture
static bool CloudAtlasFill (CloudAtlasSrc &src, int lvl, int r, int c, std::vector<BYTE> &a)
{
	using CloudAtlas::TILE;
	using CloudAtlas::MINLVL;
	for (int k = 1; lvl - k >= MINLVL; k++) {
		const std::vector<BYTE> *p = src.Ancestor(lvl - k, r >> k, c >> k);
		if (!p) continue;
		int n = 1 << k, sx = (c & (n - 1)) * TILE / n, sy = (r & (n - 1)) * TILE / n;
		a.resize(size_t(TILE) * TILE);
		for (int y = 0; y < TILE; y++)
			for (int x = 0; x < TILE; x++) {
				float fx = std::clamp(sx + (x + 0.5f) / n - 0.5f, 0.0f, float(TILE - 1));
				float fy = std::clamp(sy + (y + 0.5f) / n - 0.5f, 0.0f, float(TILE - 1));
				int x0 = int(fx), y0 = int(fy), x1 = std::min(x0 + 1, TILE - 1), y1 = std::min(y0 + 1, TILE - 1);
				float tx = fx - x0, ty = fy - y0;
				const BYTE *q = p->data();
				float v = (q[y0 * TILE + x0] * (1 - tx) + q[y0 * TILE + x1] * tx) * (1 - ty) + (q[y1 * TILE + x0] * (1 - tx) + q[y1 * TILE + x1] * tx) * ty;
				a[size_t(y) * TILE + x] = BYTE(std::lround(v));
			}
		return true;
	}
	return false;
}

// the picked level's alpha in one R8 image, unseamed, with a full box mip chain; lvl 0 when nothing to build or the alpha is flat
static bool CloudAtlasBuild (const std::string &root, bool loose, bool tree, int maxw, VkPixels &out, int &lvl, int &real, int &total)
{
	using namespace CloudAtlas;
	CloudAtlasSrc src;
	src.root = root;
	src.loose = loose;
	src.zt = tree ? ZTreeMgr::CreateFromFile(root.c_str(), ZTreeMgr::LAYER_CLOUD) : NULL;
	lvl = src.Pick(maxw, real, total);
	bool ok = lvl > 0;
	int w = Cols(std::max(lvl, MINLVL)) * TILE, h = w / 2, amin = 255, amax = 0;
	std::vector<BYTE> atl, t;
	if (ok) atl.assign(size_t(w) * h, 0);
	for (int r = 0; ok && r < Rows(lvl); r++)
		for (int c = 0; ok && c < Cols(lvl); c++) {
			ok = src.Read(lvl, r, c, t) || CloudAtlasFill(src, lvl, r, c, t);
			if (!ok) break;
			int x0, y0;
			Place(r, c, x0, y0);
			for (int y = 0; y < TILE; y++) memcpy(&atl[size_t(y0 + y) * w + x0], &t[size_t(y) * TILE], TILE);
			for (BYTE v : t) { amin = std::min(amin, int(v)); amax = std::max(amax, int(v)); }
		}
	delete src.zt;
	if (!ok) { lvl = 0; return false; }
	if (amax - amin < 2) { lvl = 0; return false; } // uniform alpha: no seams to hide
	Unseam(atl.data(), w, h, 1, TILE, BAND); // the source tiles' own edge mismatch
	out = VkPixels();
	out.w = w; out.h = h; out.layers = 1;
	out.fmt = VK_FORMAT_R8_UNORM;
	out.data.push_back(std::move(atl));
	for (int lw = w, lh = h; lw > 1 || lh > 1; lw = std::max(1, lw / 2), lh = std::max(1, lh / 2)) {
		std::vector<BYTE> m(size_t(std::max(1, lw / 2)) * std::max(1, lh / 2));
		Half(out.data.back().data(), lw, lh, m.data());
		out.data.push_back(std::move(m));
	}
	out.levels = UINT(out.data.size());
	return true;
}

static void CloudAtlasSubmit (CloudAtlasState *a, const std::string &root)
{
	auto box = a->box = std::make_shared<CloudAtlasState::Box>();
	bool loose = a->loose, tree = a->tree;
	int maxw = a->maxw;
	GfxPool::Job j;
	j.owner = a->id;
	j.kind = GfxPool::CLOUDATLAS;
	j.run = [root, loose, tree, maxw, box]() {
		VkPixels px;
		int lvl = 0, real = 0, total = 0;
		bool ok = CloudAtlasBuild(root, loose, tree, maxw, px, lvl, real, total);
		std::lock_guard<std::mutex> lk(box->mx);
		box->px = std::move(px);
		box->ok = ok;
		box->lvl = lvl; box->real = real; box->total = total;
		box->done = true;
	};
	j.dropped = [box]() {
		std::lock_guard<std::mutex> lk(box->mx);
		box->done = true;
	};
	GfxPool::Get().Submit(std::move(j));
	a->pending = true;
}

// first call submits the job, later calls upload its result; streamed loose tiles are looked for again every 30 s; render thread
static void CloudAtlasStep (TileManager2<CloudTile> *m)
{
	CloudAtlasState *&a = m->atlas;
	if (!a) {
		a = new CloudAtlasState;
		a->id = GfxPool::Get().Register();
		a->loose = m->DoLoadIndividualFiles(0); a->tree = m->ZTreeManager(0) != NULL; // PreLoad's sources
		a->maxw = int(m->GetClient()->GetHardwareCaps()->MaxTextureWidth);
		a->tcheck = oapiGetSysTime();
		if (!a->loose && !a->tree) {
			a->state = 3;
			LogWrn("Cloud atlas for %s: no cloud tiles", m->CbodyName());
			return;
		}
		CloudAtlasSubmit(a, m->DataRootDir());
		a->state = 1;
		return;
	}
	if (!a->pending) {
		if (!a->loose || a->tree || a->state == 1) return; // an archive does not change
		int finest = CloudAtlas::MAXLVL;
		while (finest > CloudAtlas::MINLVL && CloudAtlas::Cols(finest) * CloudAtlas::TILE > a->maxw) finest--;
		if (a->state == 2 && a->lvl == finest && a->real == a->total) return; // complete
		double now = oapiGetSysTime();
		if (now - a->tcheck < 30.0) return;
		a->tcheck = now;
		CloudAtlasSrc src;
		src.root = m->DataRootDir();
		src.loose = true;
		int real, total, lvl = src.Pick(a->maxw, real, total);
		if (lvl > a->lvl || (lvl == a->lvl && real > a->real)) CloudAtlasSubmit(a, src.root); // the stream has added tiles
		return;
	}
	VkPixels px;
	bool ok;
	int lvl, real, total;
	{
		std::lock_guard<std::mutex> lk(a->box->mx);
		if (!a->box->done) return;
		px = std::move(a->box->px);
		ok = a->box->ok;
		lvl = a->box->lvl; real = a->box->real; total = a->box->total;
	}
	a->pending = false;
	VkTex *tex = ok ? VkCreateTexture(m->Dev(), px, VK_IMAGE_USAGE_SAMPLED_BIT) : nullptr;
	if (tex) {
		delete a->tex;
		a->tex = tex;
		a->lvl = lvl; a->real = real; a->total = total;
		a->state = 2;
		LogAlw("Cloud atlas for %s: file level %d, %d of %d tiles read, %ux%u", m->CbodyName(), lvl, real, total, px.w, px.h);
	}
	else if (a->state == 1) {
		a->state = 3;
		a->lvl = lvl; a->real = real; a->total = total;
		if (!ok && total > 0) a->loose = false; // flat alpha or no ancestor to fill from: nothing to wait for
		if (ok) LogWrn("Cloud atlas for %s not built, the layer keeps its tile edges", m->CbodyName());
		else LogAlw("Cloud atlas for %s: none (no level with half its tiles, or uniform alpha)", m->CbodyName());
	}
	else a->loose = false; // a rebuild failed: keep the atlas there is
}

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

	if (pSysSrf && (Config->VolClouds || Config->LensFlare || Config->HeightFog) && lvl + 4 >= 5) { // not upstream: before the pixels go; none from level 4 and below, 40 km pixels and coarser; the height fog's deck and cloud shadow
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
		CloudAtlas::Range ar = lvl ? CloudAtlas::Quad(bnd.minlat, bnd.maxlat, bnd.minlng, bnd.maxlng) : CloudAtlas::Hemi(ilng); // not upstream: seamless cloud alpha
		sp->vCldAtlas = FVECTOR4(ar.x, ar.y, ar.z, ar.w);
		int tl = lvl; // internal level of the texture's owner
		for (const CloudTile *t = this; t && !t->HasOwnTex(); t = static_cast<const CloudTile *>(t->getParent())) tl--;
		sp->vCldAtlasW = FVECTOR4(CloudAtlasTex(cmgr->atlas) ? 1.0f : 0.0f, float(max(1, Config->Anisotrophy)), tl + 4 <= CloudAtlasLevel(cmgr->atlas) ? 1.0f : 0.0f, 0.0f);

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
// not upstream: the lens flare's cloud test (design-visual-batch 6, S5)

static struct { const void *mgr = nullptr; DWORD frame = 0; float alpha = 0.0f; } cloudSun; // the main camera's, render thread

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

	if (Config->CloudAtlas) { // not upstream: seamless cloud alpha (design-cloud-seams); giant layers too
		CloudAtlasStep(this);
		if (VkTex *atl = CloudAtlasTex(atlas)) pShader->SetTexture("tCloudAtlas", atl, IPF_ANISOTROPIC | IPF_CLAMP_V, Config->Anisotrophy);
	}

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

	if (Config->LensFlare && scene->GetRenderPass() == RENDERPASS_MAINSCENE && vp == scene->GetCameraProxyVisual()) { // not upstream: the flare's cloud test, under the mutex (design-visual-batch S5)
		VECTOR3 gsun;
		oapiGetGlobalPos(oapiGetObjectByIndex(0), &gsun);
		VECTOR3 cam = scene->GetCameraGPos();
		cloudSun.mgr = this;
		cloudSun.frame = scene->GetFrameId();
		cloudSun.alpha = CloudAlphaToward(this, cam, gsun - cam);
	}
	
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

// -----------------------------------------------------------------------
// not upstream: the finest loaded density over a planet-local direction; loader mutex held
static float CloudDensityAt (TileManager2<CloudTile> *m, const VECTOR3 &dl, double cloudrot)
{
	double r = length(dl);
	if (r <= 0.0) return 0.0f;
	double lat = std::clamp(asin(std::clamp(dl.y / r, -1.0, 1.0)), -PI05 + 1e-6, PI05 - 1e-6);
	double lng = VolCloud::CloudLng(atan2(dl.z, dl.x), cloudrot);
	const CloudTile *t = (const CloudTile *)m->SearchTile(lng, lat, 32, true);
	while (t && !t->Density()) t = (t->GetNode() && t->GetNode()->Parent()) ? t->GetNode()->Parent()->Entry() : nullptr;
	if (!t) return 0.0f;
	VolCloud::Src s;
	s.minlat = t->bnd.minlat; s.maxlat = t->bnd.maxlat;
	s.minlng = t->bnd.minlng; s.maxlng = t->bnd.maxlng;
	s.d = t->Density();
	double u, v;
	return s.Covers(lat, lng, &u, &v) ? s.At(u, v) : 0.0f;
}

// not upstream: the lens flare's cloud alpha (design-visual-batch S5)
float CloudAlphaToward (TileManager2<CloudTile> *m, const VECTOR3 &cameraPos, const VECTOR3 &sunDir)
{
	vPlanet *vp = m->GetPlanet();
	if (!vp || !vp->prm.bCloud) return 0.0f;
	MATRIX3 R;
	VECTOR3 P;
	oapiGetRotationMatrix(vp->Object(), &R);
	oapiGetGlobalPos(vp->Object(), &P);
	VECTOR3 p = tmul(R, cameraPos - P), d = tmul(R, unit(sunDir)); // planet-local
	double Rp = vp->GetSize(), alt = vp->prm.cloudalt, rc = Rp + alt, r = length(p), h = r - Rp;
	if (h >= alt + 1000.0) return 0.0f; // above the layer
	auto step = [](double e0, double e1, double x) { double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); };
	double wV = m->GetScene()->GetVolCloudFade(); // the volume clouds replace the layer near the camera, as CloudPS fades it
	auto fVol = [&](double dist) { return 1.0 - wV + wV * step(35e3, 60e3, dist); };
	double inside = 0.0, below = 0.0;
	if (h > alt - 1000.0) inside = 0.5 * CloudDensityAt(m, p, vp->prm.cloudrot) * fVol(0.0);
	if (h < alt) { // from below: where the ray meets the layer
		double b = dotp(p, d), c = r * r - rc * rc, t = -b + sqrt(std::max(0.0, b * b - c));
		below = CloudDensityAt(m, p + d * t, vp->prm.cloudrot) * fVol(t);
		return float(below + (inside - below) * step(alt - 1000.0, alt, h));
	}
	return float(inside * (1.0 - step(alt, alt + 1000.0, h)));
}

// not upstream: the lens flare's cloud alpha (design-visual-batch S5)
float CloudSunAlpha (const TileManager2<CloudTile> *m, DWORD frame)
{
	return (m && cloudSun.mgr == m && cloudSun.frame == frame) ? cloudSun.alpha : 0.0f;
}
