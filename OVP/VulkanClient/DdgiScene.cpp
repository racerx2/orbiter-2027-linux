// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the scene the ray-traced GI's rays see (design-ddgi 3; Rev 2)

#include "DdgiScene.h"
#include "DdgiVolume.h"
#include "Scene.h"
#include "Tilemgr2.h"
#include "VPlanet.h"
#include "VVessel.h"
#include "VBase.h"
#include "Mesh.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static const VkImageUsageFlags AlbUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
static const VkBufferUsageFlags AsUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
static const int NQUERY = 256;		// compaction size queries in flight
static const UINT TRI_FRAME = 49152;	// triangles built per frame, nearest first
static const uint64_t KEEP = 240;	// frames an unused structure is kept

namespace {

uint64_t Mix (uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ULL; }
uint64_t MixBytes (uint64_t h, const void *p, size_t n)
{
	const unsigned char *b = (const unsigned char *)p;
	for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ULL;
	return h;
}

VECTOR3 V (double x, double y, double z) { VECTOR3 v = { x, y, z }; return v; }

void Plain (VkDev *dev) // no depth, no blend, every channel
{
	VkDev::State st = dev->GetState();
	st.depthTest = st.depthWrite = st.blend = st.stencil = false;
	st.cull = VK_CULL_MODE_NONE;
	st.fill = VK_POLYGON_MODE_FILL;
	st.writeMask = 0xF;
	dev->SetState(st);
}

VkDeviceAddress Address (VkDev *dev, VkBuffer b)
{
	VkBufferDeviceAddressInfo ai = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	ai.buffer = b;
	return vkGetBufferDeviceAddress(dev->dev, &ai);
}

void Barrier (VkCommandBuffer cmd, VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da)
{
	VkMemoryBarrier2 mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	mb.srcStageMask = ss; mb.srcAccessMask = sa;
	mb.dstStageMask = ds; mb.dstAccessMask = da;
	VkDependencyInfo di = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.memoryBarrierCount = 1;
	di.pMemoryBarriers = &mb;
	vkCmdPipelineBarrier2(cmd, &di);
}

}

DdgiScene::DdgiScene (VkDev *_dev) : dev(_dev)
{
	if (!dev->rayQuery) return;
	VkPhysicalDeviceAccelerationStructurePropertiesKHR ap = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	VkPhysicalDeviceProperties2 pp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &ap };
	vkGetPhysicalDeviceProperties2(dev->phys, &pp);
	scratchAlign = std::max<VkDeviceSize>(ap.minAccelerationStructureScratchOffsetAlignment, 1);

	VkAccelerationStructureGeometryKHR g = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	VkAccelerationStructureBuildGeometryInfoKHR bi = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	bi.geometryCount = 1;
	bi.pGeometries = &g;
	UINT n = MAXINST;
	VkAccelerationStructureBuildSizesInfoKHR sz = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkrt.GetAccelerationStructureBuildSizesKHR(dev->dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &n, &sz);
	tlasBuf = new VkBuf(dev, sz.accelerationStructureSize, AsUsage, false);
	tlasScratch = std::max(sz.buildScratchSize, sz.updateScratchSize);
	if (tlasBuf->buf) {
		VkAccelerationStructureCreateInfoKHR ci = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		ci.buffer = tlasBuf->buf;
		ci.size = sz.accelerationStructureSize;
		ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		VKCHECK(vkrt.CreateAccelerationStructureKHR(dev->dev, &ci, NULL, &tlas));
	}
	rows = new VkBuf(dev, MAXINST * sizeof(Row), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);

	VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	qi.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
	qi.queryCount = NQUERY;
	VKCHECK(vkCreateQueryPool(dev->dev, &qi, NULL, &queries));
	for (int i = NQUERY - 1; i >= 0; i--) freeQueries.push_back(i);

	tAtlas = new VkTex(dev, ATLAS, ATLAS, 1, VK_FORMAT_R8G8B8A8_UNORM, AlbUsage);
	sAtlas = new VkSurf(tAtlas, 0);
	tTable = new VkTex(dev, TABLE, TABLE, 1, VK_FORMAT_R8G8B8A8_UNORM, AlbUsage);
	sTable = new VkSurf(tTable, 0);
	for (int i = (ATLAS / CELL) * (ATLAS / CELL) - 1; i >= 0; i--) freeCells.push_back(i);
	freeSlots.push_back({ 1, TABLE * TABLE - 1 }); // texel 0: the far ground

	pTile = new DdgiPass(dev, "Modules/VulkanClient/DdgiAlbedo.glsl", "DgAlbVS", "DgTileCell", "", false);
	pMean = new DdgiPass(dev, "Modules/VulkanClient/DdgiAlbedo.glsl", "DgAlbVS", "DgMean", "", false);

	ok = tlas && rows->buf && queries && tAtlas->img && tTable->img && pTile->OK() && pMean->OK();
	if (!ok) LogErr("DdgiScene: structures, albedo targets or shaders failed");
}

DdgiScene::~DdgiScene ()
{
	for (auto &t : tiles) FreeBlas(t.second.b);
	for (auto &m : meshes) for (MeshPart &p : m.second.part) FreeBlas(p.b);
	VkDevice d = dev->dev;
	VkAccelerationStructureKHR a = tlas;
	VkQueryPool q = queries;
	dev->Defer([d, a, q]() {
		if (a) vkrt.DestroyAccelerationStructureKHR(d, a, NULL);
		if (q) vkDestroyQueryPool(d, q, NULL);
	});
	delete tlasBuf;
	delete scratch;
	delete rows;
	delete sAtlas; delete tAtlas;
	delete sTable; delete tTable;
	delete pTile; delete pMean;
}

void DdgiScene::FreeBlas (Blas &b)
{
	if (b.as) {
		VkDevice d = dev->dev;
		VkAccelerationStructureKHR a = b.as;
		dev->Defer([d, a]() { vkrt.DestroyAccelerationStructureKHR(d, a, NULL); });
	}
	delete b.buf;
	if (b.query >= 0) freeQueries.push_back(b.query);
	b = Blas();
}

bool DdgiScene::MakeBlas (Blas &b, VkDeviceSize size)
{
	b.buf = new VkBuf(dev, size, AsUsage, false);
	if (!b.buf->buf) { delete b.buf; b.buf = nullptr; return false; }
	VkAccelerationStructureCreateInfoKHR ci = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	ci.buffer = b.buf->buf;
	ci.size = size;
	ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	if (vkrt.CreateAccelerationStructureKHR(dev->dev, &ci, NULL, &b.as) != VK_SUCCESS) { delete b.buf; b.buf = nullptr; b.as = VK_NULL_HANDLE; return false; }
	VkAccelerationStructureDeviceAddressInfoKHR ai = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	ai.accelerationStructure = b.as;
	b.addr = vkrt.GetAccelerationStructureDeviceAddressKHR(dev->dev, &ai);
	b.frame = frame;
	return true;
}

VkBuf *DdgiScene::Scratch (VkDeviceSize size)
{
	size += scratchAlign;
	if (scratch && scratch->size >= size) return scratch;
	delete scratch; // deferred past the frames that use it
	scratch = new VkBuf(dev, std::max<VkDeviceSize>(size, VkDeviceSize(16) << 20), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
	if (!scratch->buf) { delete scratch; scratch = nullptr; }
	return scratch;
}

// the anchor: set at the ground under the camera, stepped in 1024 m along its own axes, set again after a jump or 200 km of steps
void DdgiScene::Anchor (Scene *scn, vPlanet *vp)
{
	OBJHANDLE hP = vp->Object();
	MATRIX3 R;
	VECTOR3 P;
	oapiGetRotationMatrix(hP, &R);
	oapiGetGlobalPos(hP, &P);
	VECTOR3 camG = scn->GetCameraGPos();
	VECTOR3 cl = tmul(R, camG - P);
	double elev = scn->GetCameraElevation();
	anc.reset = false;
	for (int pass = 0; pass < 2; pass++) {
		bool jump = anc.valid && length(cl - lastCam) > 2e3;
		double far = 1024.0 * std::sqrt(double(anc.k[0]) * anc.k[0] + double(anc.k[1]) * anc.k[1] + double(anc.k[2]) * anc.k[2]);
		if (anc.valid && anc.body == hP && !jump && far < 2e5) break;
		VECTOR3 up = unit(cl);
		anc.o0 = cl - up * elev;
		VECTOR3 e = V(-up.z, 0.0, up.x), n = V(-up.y * up.x, 1.0 - up.y * up.y, -up.y * up.z);
		if (length(e) < 1e-9 || length(n) < 1e-9) { e = V(1, 0, 0); n = crossp(e, up); } // at a pole any east will do
		e = unit(e);
		n = unit(n);
		anc.f = _M(e.x, up.x, n.x, e.y, up.y, n.y, e.z, up.z, n.z);
		anc.k[0] = anc.k[1] = anc.k[2] = 0;
		anc.body = hP;
		anc.valid = true;
		anc.reset = true;
		lastCam = cl;
	}
	lastCam = cl;
	for (int it = 0; it < 4; it++) {
		VECTOR3 al = anc.o0 + mul(anc.f, V(anc.k[0], anc.k[1], anc.k[2]) * 1024.0);
		VECTOR3 pa = tmul(anc.f, cl - al);
		double p[3] = { pa.x, pa.y, pa.z };
		bool moved = false;
		for (int i = 0; i < 3; i++) if (std::fabs(p[i]) > 1536.0) { anc.k[i] += (int)std::lround(p[i] / 1024.0); moved = true; }
		if (!moved) break;
	}
	anc.al = anc.o0 + mul(anc.f, V(anc.k[0], anc.k[1], anc.k[2]) * 1024.0);
	anc.geo = mul(R, anc.al);
	anc.ag = P + anc.geo;
	anc.ax = mul(R, V(anc.f.m11, anc.f.m21, anc.f.m31));
	anc.ay = mul(R, V(anc.f.m12, anc.f.m22, anc.f.m32));
	anc.az = mul(R, V(anc.f.m13, anc.f.m23, anc.f.m33));
	anc.cam = tmul(anc.f, cl - anc.al);
	anc.ctr = tmul(anc.f, -anc.al);
	anc.ground = anc.cam.y - elev;
	anc.radius = oapiGetSize(hP);
	const D3D9Sun *sun = scn->GetSun();
	VECTOR3 s = unit(V(-sun->Dir.x, -sun->Dir.y, -sun->Dir.z));
	anc.sun = V(dotp(anc.ax, s), dotp(anc.ay, s), dotp(anc.az, s));
}

// camera-relative global (row vectors) to the anchor frame, 3x4 rows for column vectors
void DdgiScene::Transform (const double W[4][4], float out[3][4]) const
{
	VECTOR3 t = anc.cam; // the camera in the anchor frame: the camera-relative origin
	const VECTOR3 *ax[3] = { &anc.ax, &anc.ay, &anc.az };
	double tc[3] = { t.x, t.y, t.z };
	for (int k = 0; k < 3; k++) {
		const VECTOR3 &a = *ax[k];
		for (int i = 0; i < 3; i++) out[k][i] = float(a.x * W[i][0] + a.y * W[i][1] + a.z * W[i][2]);
		out[k][3] = float(a.x * W[3][0] + a.y * W[3][1] + a.z * W[3][2] + tc[k]);
	}
}

void DdgiScene::Gather (Scene *scn, vPlanet *vp, double range, std::vector<GiTileRec> &tl, std::vector<MeshUse> &ml)
{
	vp->GiTiles(range, 1.0 / 32.0, tl);
	std::sort(tl.begin(), tl.end(), [](const GiTileRec &a, const GiTileRec &b) { return a.dist < b.dist; });
	ck.on = false;
	OBJHANDLE hF = oapiGetFocusObject();
	bool vcView = oapiCameraInternal() && oapiCockpitMode() == COCKPIT_VIRTUAL;
	for (Scene::VOBJREC *r = scn->vobjFirst; r; r = r->next) {
		if (!r->vobj || !r->vobj->IsActive() || r->type != OBJTP_VESSEL) continue;
		vVessel *v = (vVessel *)r->vobj;
		if (v->CamDist() - v->GetBoundingSphereRadius() > range) continue;
		bool focus = v->Object() == hF, vc = focus && vcView;
		double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
		for (UINT i = 0; i < v->nmesh; i++) { // design-ddgi-cockpit 3: the focus vessel's exterior 0x02, its cockpit view 0x04, the rest 0x01
			const auto &m = v->meshlist[i];
			if (!m.mesh) continue;
			MeshUse u;
			if (m.vismode & MESHVIS_EXTERNAL) u.mask = focus ? 0x02 : 0x01;
			else u.mask = 0;
			bool cv = vc && (m.vismode & (MESHVIS_COCKPIT | MESHVIS_VC));
			if (cv) u.mask |= 0x04;
			if (!u.mask) continue;
			u.vc = cv && !(m.vismode & MESHVIS_EXTPASS); // drawn in the cockpit pass, as vVessel::Render picks it
			u.mesh = m.mesh;
			if (m.trans) D3DXMatrixMultiply(&u.W, (const D3DXMATRIX *)m.trans, &v->mWorld);
			else u.W = v->mWorld;
			ml.push_back(u);
			if (!u.vc) continue;
			const D9BBox *b = m.mesh->GetAABB();
			for (int k = 0; k < 8; k++) { // the box's corners through the mesh's offset, vessel frame
				D3DXVECTOR3 p((k & 1) ? b->max.x : b->min.x, (k & 2) ? b->max.y : b->min.y, (k & 4) ? b->max.z : b->min.z), q;
				if (m.trans) D3DXVec3TransformCoord(&q, &p, (const D3DXMATRIX *)m.trans);
				else q = p;
				const float c[3] = { q.x, q.y, q.z };
				for (int a = 0; a < 3; a++) { lo[a] = std::min(lo[a], double(c[a])); hi[a] = std::max(hi[a], double(c[a])); }
			}
		}
		if (!vc || lo[0] > hi[0]) continue;
		VESSEL *ves = oapiGetVesselInterface(hF);
		if (!ves) continue;
		ck.on = true;
		ck.vessel = hF;
		double W[4][4];
		for (int a = 0; a < 4; a++) for (int c = 0; c < 4; c++) W[a][c] = v->mWorld.m[a][c];
		Transform(W, ck.M);
		VECTOR3 gp, lp;
		oapiCameraGlobalPos(&gp);
		ves->Global2Local(gp, lp);
		for (int a = 0; a < 3; a++) { ck.lo[a] = lo[a]; ck.hi[a] = hi[a]; }
		ck.eye[0] = lp.x; ck.eye[1] = lp.y; ck.eye[2] = lp.z;
	}
	for (DWORD i = 0; i < vp->GetBaseCount(); i++) {
		vBase *b = vp->GetBaseByIndex(i);
		if (!b || !b->IsActive() || b->CamDist() - b->GetBoundingSphereRadius() > range) continue;
		if (b->tilemesh) ml.push_back({ b->tilemesh, b->mWorld });
		for (DWORD j = 0; j < b->nstructure_bs; j++) if (b->structure_bs[j]) ml.push_back({ b->structure_bs[j], b->mWorld });
		for (DWORD j = 0; j < b->nstructure_as; j++) if (b->structure_as[j]) ml.push_back({ b->structure_as[j], b->mWorld });
	}
}

int DdgiScene::CellAlloc ()
{
	if (freeCells.empty()) Evict();
	if (freeCells.empty()) return -1;
	int c = freeCells.back();
	freeCells.pop_back();
	return c;
}

int DdgiScene::SlotAlloc (int n)
{
	for (int pass = 0; pass < 2; pass++) {
		for (size_t i = 0; i < freeSlots.size(); i++) {
			if (freeSlots[i].second < n) continue;
			int b = freeSlots[i].first;
			freeSlots[i].first += n;
			freeSlots[i].second -= n;
			if (!freeSlots[i].second) freeSlots.erase(freeSlots.begin() + i);
			return b;
		}
		Evict();
	}
	return -1;
}

void DdgiScene::SlotFree (int base, int n)
{
	if (base < 0 || n <= 0) return;
	freeSlots.push_back({ base, n });
	std::sort(freeSlots.begin(), freeSlots.end());
	std::vector<std::pair<int, int>> m;
	for (auto &s : freeSlots) {
		if (!m.empty() && m.back().first + m.back().second == s.first) m.back().second += s.second;
		else m.push_back(s);
	}
	freeSlots.swap(m);
}

// what wasn't used for a while goes; when space ran out, what wasn't used in the last frame either
void DdgiScene::Evict ()
{
	bool tight = freeCells.empty() || freeSlots.empty();
	uint64_t keep = tight ? (frame > 1 ? frame - 1 : 0) : (frame > KEEP ? frame - KEEP : 0);
	for (auto it = tiles.begin(); it != tiles.end(); ) {
		if (it->second.used >= keep) { ++it; continue; }
		FreeBlas(it->second.b);
		if (it->second.cell >= 0) freeCells.push_back(it->second.cell);
		it = tiles.erase(it);
	}
	for (auto it = meshes.begin(); it != meshes.end(); ) {
		if (it->second.used >= keep) { ++it; continue; }
		for (MeshPart &p : it->second.part) FreeBlas(p.b);
		SlotFree(it->second.slot, it->second.nslot);
		it = meshes.erase(it);
	}
}

// the tiles whose compacted size is back move into a structure of that size; a query is read only once the frame that reset and wrote it has run
int DdgiScene::Compact (VkCommandBuffer cmd, bool fenced)
{
	int n = 0;
	for (size_t i = 0; i < pendingCompact.size(); ) {
		int q = pendingCompact[i].query;
		auto it = tiles.find(pendingCompact[i].key);
		if (it == tiles.end() || it->second.b.query != q) { pendingCompact.erase(pendingCompact.begin() + i); continue; } // freed: its slot went back with it
		if (frame < pendingCompact[i].frame + VkDev::NFRAMES + 1) { i++; continue; } // before that its state is undefined, not unavailable
		uint64_t r[2] = {};
		VkResult res = vkGetQueryPoolResults(dev->dev, queries, q, 1, sizeof(r), r, sizeof(r), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
		if (res != VK_SUCCESS || !r[1]) { i++; continue; }
		Blas &b = it->second.b;
		Blas c;
		if (r[0] && r[0] < b.buf->size && MakeBlas(c, r[0])) {
			if (!fenced) Barrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT);
			fenced = true;
			VkCopyAccelerationStructureInfoKHR ci = { VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
			ci.src = b.as;
			ci.dst = c.as;
			ci.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
			vkrt.CmdCopyAccelerationStructureKHR(cmd, &ci);
			n++;
			c.frame = b.frame;
			FreeBlas(b); // its query goes back too
			b = c;
		}
		else {
			freeQueries.push_back(q);
			b.query = -1;
		}
		pendingCompact.erase(pendingCompact.begin() + i);
	}
	return n;
}

void DdgiScene::WriteTile (const GiTileRec &t, int cell)
{
	int x = (cell % (ATLAS / CELL)) * CELL, y = (cell / (ATLAS / CELL)) * CELL;
	if (!t.tex) { // NewPlanet: grey without a texture
		RECT r = { x, y, x + CELL, y + CELL };
		dev->ColorFill(sAtlas, &r, 0xFF808080);
		return;
	}
	dev->SetRenderTarget(sAtlas, NULL);
	for (int i = 1; i < 4; i++) dev->SetRenderTargetN(i, NULL);
	dev->SetViewport(float(x), float(y), float(CELL), float(CELL));
	Plain(dev);
	float rg[4] = { t.range.tumin, t.range.tvmin, t.range.tumax - t.range.tumin, t.range.tvmax - t.range.tvmin };
	pTile->Set("vRange", rg, sizeof(rg));
	pTile->Tex("tSrc", t.tex, true, true);
	pTile->Draw(3);
}

void DdgiScene::WriteMesh (D3D9Mesh *m, const MeshEntry &e)
{
	dev->SetRenderTarget(sTable, NULL);
	for (int i = 1; i < 4; i++) dev->SetRenderTargetN(i, NULL);
	Plain(dev);
	float rg[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	pMean->Set("vRange", rg, sizeof(rg));
	for (size_t k = 0; k < e.groups.size(); k++) {
		D3D9Mesh::GiGroupDesc d;
		if (!m->GiGroup(e.groups[k], d)) continue;
		int s = e.slot + int(k);
		dev->SetViewport(float(s % TABLE), float(s / TABLE), 1.0f, 1.0f);
		float opt[4] = { d.fam == 2 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
		pMean->Set("vMat", &d.mat, sizeof(d.mat));
		pMean->Set("vMtl", &d.mtl, sizeof(d.mtl));
		pMean->Set("vOpt", opt, sizeof(opt));
		pMean->Tex("tSrc", d.tex, true, true);
		pMean->Tex("tMR", d.mr, true, true);
		pMean->Draw(3);
	}
}

// texel 0: the mean of the tile under the camera, for the ground beyond the traced region
void DdgiScene::WriteFar (vPlanet *vp, const std::vector<GiTileRec> &tl)
{
	const GiTileRec *t = NULL;
	for (const GiTileRec &r : tl) if (r.tex) { t = &r; break; }
	if (!t || (t->tex == farTex && t->tex->writes == farWrites)) return;
	farTex = t->tex;
	farWrites = t->tex->writes;
	dev->SetRenderTarget(sTable, NULL);
	for (int i = 1; i < 4; i++) dev->SetRenderTargetN(i, NULL);
	Plain(dev);
	dev->SetViewport(0.0f, 0.0f, 1.0f, 1.0f);
	float rg[4] = { t->range.tumin, t->range.tvmin, t->range.tumax - t->range.tumin, t->range.tvmax - t->range.tvmin };
	float mat[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, zero[4] = {};
	pMean->Set("vRange", rg, sizeof(rg));
	pMean->Set("vMat", mat, sizeof(mat));
	pMean->Set("vMtl", zero, sizeof(zero));
	pMean->Set("vOpt", zero, sizeof(zero));
	pMean->Tex("tSrc", t->tex, true, true);
	pMean->Tex("tMR", NULL, true, true);
	pMean->Draw(3);
}

bool DdgiScene::Build (Scene *scn, vPlanet *vp, double range)
{
	if (!ok || !vp) return false;
	frame++;
	Anchor(scn, vp);
	const ConstParams *cp = vp->GetScatterConst();
	bool air = cp && vp->HasAtmosphere() && vp->GetShaderID() != PLT_MOON; // RenderDepth's terrain albedo
	albG = air ? cp->TrGamma : 1.0f;
	albS = air ? cp->TrExpo : 1.0f;
	if (frame == 1) {
		dev->ColorFill(sAtlas, NULL, 0xFF808080);
		dev->ColorFill(sTable, NULL, 0xFF404040);
	}

	std::vector<GiTileRec> tl;
	std::vector<MeshUse> ml;
	Gather(scn, vp, range, tl, ml);
	if (frame % 64 == 0) Evict();

	struct Job {
		Blas *b;
		bool update, compact;
		uint64_t key;
		size_t g0, ng;
		VkBuildAccelerationStructureFlagsKHR flags;
	};
	std::vector<Job> jobs;
	std::vector<VkAccelerationStructureGeometryKHR> geos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
	std::vector<VkAccelerationStructureInstanceKHR> inst;
	std::vector<Row> rws;
	UINT tri = 0;
	auto addInst = [&](const double W[4][4], Blas *b, const Row &row, uint8_t mask) { // the structure's address is set once the builds are recorded
		if ((int)inst.size() >= MAXINST) return;
		VkAccelerationStructureInstanceKHR in = {};
		Transform(W, in.transform.matrix);
		in.mask = mask;
		in.flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
		in.accelerationStructureReference = (uint64_t)(uintptr_t)b;
		inst.push_back(in);
		rws.push_back(row);
	};
	auto tri3 = [](VkAccelerationStructureGeometryKHR &g, VkDeviceAddress vb, UINT stride, UINT maxV, VkDeviceAddress ib, VkDeviceAddress tf) {
		g = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		auto &t = g.geometry.triangles;
		t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		t.vertexData.deviceAddress = vb;
		t.vertexStride = stride;
		t.maxVertex = maxV;
		t.indexType = VK_INDEX_TYPE_UINT16;
		t.indexData.deviceAddress = ib;
		t.transformData.deviceAddress = tf;
	};

	// terrain, nearest first; a tile is traced once its structure is built
	int nTiles = 0, maxTiles = std::min((int)freeCells.size() + (int)tiles.size(), MAXINST - 512);
	for (const GiTileRec &t : tl) {
		if (nTiles >= maxTiles) break;
		TileEntry &e = tiles[t.serial];
		e.used = frame;
		if (e.cell < 0) e.cell = CellAlloc();
		if (e.cell < 0) { tiles.erase(t.serial); continue; }
		nTiles++;
		float rg[4] = { t.range.tumin, t.range.tvmin, t.range.tumax - t.range.tumin, t.range.tvmax - t.range.tvmin };
		if (!e.albedo || t.tex != e.tex || (t.tex && t.tex->writes != e.texWrites) || memcmp(rg, e.range, sizeof(rg))) {
			WriteTile(t, e.cell);
			e.tex = t.tex;
			e.texWrites = t.tex ? t.tex->writes : 0;
			memcpy(e.range, rg, sizeof(rg));
			e.albedo = true;
		}
		if (!e.b.as && tri + t.nf <= TRI_FRAME) {
			Job j = { &e.b, false, true, t.serial, geos.size(), 1, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR };
			geos.emplace_back();
			tri3(geos.back(), t.vb->Address(), sizeof(VERTEX_2TEX), t.nv - 1, t.ib->Address(), 0);
			ranges.push_back({ t.nf, 0, 0, 0 });
			jobs.push_back(j);
			tri += t.nf;
		}
		if (!e.b.as && (jobs.empty() || jobs.back().b != &e.b)) continue;
		double W[4][4];
		memcpy(W, &t.W, sizeof(W));
		double latc = 0.5 * (t.ext[0] + t.ext[1]), lngc = 0.5 * (t.ext[2] + t.ext[3]);
		double dlat = std::max(t.ext[1] - t.ext[0], 1e-12), dlng = std::max(t.ext[3] - t.ext[2], 1e-12);
		VECTOR3 pc = tmul(anc.f, V(std::cos(latc) * std::cos(lngc), std::sin(latc), std::cos(latc) * std::sin(lngc)) * anc.radius - anc.al);
		VECTOR3 ea = tmul(anc.f, V(-std::sin(lngc), 0.0, std::cos(lngc)));
		VECTOR3 na = tmul(anc.f, V(-std::sin(latc) * std::cos(lngc), std::cos(latc), -std::sin(latc) * std::sin(lngc)));
		VECTOR3 U = ea * (1.0 / (anc.radius * std::max(std::cos(latc), 1e-6) * dlng)), Vv = na * (-1.0 / (anc.radius * dlat));
		Row row = { { 0.0f, float(e.cell), 0.0f, 0.0f },
			{ float(U.x), float(U.y), float(U.z), float(0.5 - dotp(pc, U)) },
			{ float(Vv.x), float(Vv.y), float(Vv.z), float(0.5 - dotp(pc, Vv)) } };
		addInst(W, &e.b, row, 0x01);
	}
	WriteFar(vp, tl);

	// vessels and bases: one geometry per traced group, its group matrix in the structure
	std::vector<std::pair<size_t, size_t>> tfRanges; // geometry index range of jobs that read transforms
	std::vector<VkTransformMatrixKHR> tfs;
	for (const MeshUse &u : ml) {
		D3D9Mesh *m = u.mesh;
		if (!m || !m->IsOK()) continue;
		m->pBuf->Map(dev);
		MeshBuffer *mb = m->pBuf;
		if (!mb->pVB || !mb->pIB || !mb->pVB->buf || !mb->pIB->buf) continue;
		std::vector<DWORD> gp[2]; // design-ddgi-cockpit Rev 4: groups an instrument moves get their own structure, refit alone
		const uint64_t seed = 0xcbf29ce484222325ULL;
		uint64_t hg[2] = { seed, seed }, ht[2] = { seed, seed }, hv[2] = { mb->geoSerial, seed }, ha = seed;
		UINT ntri[2] = { 0, 0 };
		for (DWORD g = 0; g < m->GetGroupCount(); g++) {
			D3D9Mesh::GiGroupDesc d;
			if (!m->GiGroup(g, d) || !d.used) continue;
			const D3D9Mesh::GROUPREC *gr = m->GetGroup(g);
			if (!gr || gr->VertOff + gr->nVert > mb->nVtx || gr->IdexOff + gr->nFace * 3 > mb->nIdx) continue;
			int k = gr->geoEdit ? 1 : 0;
			gp[k].push_back(g);
			hg[k] = Mix(Mix(Mix(hg[k], g), gr->nFace), gr->nVert);
			ht[k] = MixBytes(ht[k], &d.tf, sizeof(d.tf));
			if (k) hv[1] = Mix(hv[1], gr->geoEdit);
			ha = Mix(Mix(Mix(ha, (uint64_t)(uintptr_t)d.tex), (uint64_t)(uintptr_t)d.mr), d.fam);
			ha = MixBytes(MixBytes(ha, &d.mat, sizeof(d.mat)), &d.mtl, sizeof(d.mtl));
			ntri[k] += gr->nFace;
		}
		std::vector<DWORD> gl = gp[0];
		gl.insert(gl.end(), gp[1].begin(), gp[1].end());
		if (gl.empty()) continue;
		auto found = meshes.find(m);
		if (found == meshes.end() && (int)inst.size() >= MAXINST) continue;
		MeshEntry &e = meshes[m];
		e.used = frame;
		if (e.slot < 0 || e.nslot != (int)gl.size()) {
			SlotFree(e.slot, e.nslot);
			e.slot = SlotAlloc((int)gl.size());
			e.nslot = e.slot < 0 ? 0 : (int)gl.size();
			e.alb = 0;
		}
		if (e.slot < 0) continue;
		bool regroup = e.groups != gl || e.nstat != (int)gp[0].size();
		e.groups = gl;
		e.nstat = (int)gp[0].size();
		if (e.alb != ha || regroup) {
			WriteMesh(m, e);
			e.alb = ha;
		}
		double W[4][4];
		for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) W[r][c] = u.W.m[r][c];
		for (int k = 0; k < 2; k++) {
			MeshPart &pt = e.part[k];
			if (gp[k].empty()) {
				if (pt.b.as) FreeBlas(pt.b);
				pt = MeshPart();
				continue;
			}
			bool rebuild = !pt.b.as || pt.geo != hg[k] || regroup;
			bool refit = !rebuild && (pt.tf != ht[k] || pt.vtx != hv[k]); // moved groups or vertices, the same triangles: refit
			if (refit) pt.moved = frame;
			if (refit && pt.refits >= 1024) rebuild = true, refit = false; // refit for long: rebuilt to its full quality
			else if (!refit && pt.refits > 0 && frame - pt.moved >= 30) rebuild = true; // settled after refits: the same
			bool queued = pt.queued == frame; // a mesh drawn twice is built once
			if (!queued && ((rebuild && tri + ntri[k] <= TRI_FRAME) || refit)) {
				if (rebuild) FreeBlas(pt.b);
				Job j = { &pt.b, refit, false, 0, geos.size(), gp[k].size(), VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR };
				tfRanges.push_back({ geos.size(), tfs.size() });
				for (DWORD g : gp[k]) {
					D3D9Mesh::GiGroupDesc d;
					m->GiGroup(g, d);
					const D3D9Mesh::GROUPREC *gr = m->GetGroup(g);
					VkTransformMatrixKHR M;
					for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) M.matrix[r][c] = c < 3 ? ((const float *)&d.tf)[c * 4 + r] : ((const float *)&d.tf)[12 + r];
					tfs.push_back(M);
					geos.emplace_back();
					tri3(geos.back(), mb->pVB->Address() + VkDeviceSize(gr->VertOff) * sizeof(NMVERTEX), sizeof(NMVERTEX), gr->nVert - 1, mb->pIB->Address() + VkDeviceSize(gr->IdexOff) * sizeof(WORD), 1);
					ranges.push_back({ gr->nFace, 0, 0, 0 });
				}
				jobs.push_back(j);
				pt.refits = refit ? pt.refits + 1 : 0;
				pt.geo = hg[k];
				pt.tf = ht[k];
				pt.vtx = hv[k];
				pt.queued = frame;
				if (rebuild) tri += ntri[k];
				queued = true;
			}
			if (!pt.b.as && !queued) continue;
			Row row = { { 1.0f, float(e.slot + (k ? e.nstat : 0)), u.vc ? 1.0f : 0.0f, 0.0f }, {}, {} };
			if (u.vc && (u.mask & 0x03)) { // in both views: the exterior's rays shade it as the exterior pass, the cockpit's as the cockpit pass
				Row ext = row;
				ext.a[2] = 0.0f;
				addInst(W, &pt.b, ext, u.mask & 0x03);
				addInst(W, &pt.b, row, 0x04);
			}
			else addInst(W, &pt.b, row, u.mask);
		}
	}

	dev->EndRendering(); // structures are built outside a rendering pass
	VkCommandBuffer cmd = dev->Cmd();
	if (!jobs.empty()) Barrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT);
	int copies = Compact(cmd, !jobs.empty());

	// group matrices into this frame's transient memory
	VkDeviceAddress tfAddr = 0;
	if (!tfs.empty()) {
		void *p;
		VkBuffer tb;
		VkDeviceSize ofs = dev->AllocTransient(tfs.size() * sizeof(VkTransformMatrixKHR), 16, &p, &tb);
		memcpy(p, tfs.data(), tfs.size() * sizeof(VkTransformMatrixKHR));
		tfAddr = Address(dev, tb) + ofs;
	}
	size_t ti = 0;
	for (Job &j : jobs) {
		if (j.compact) continue;
		size_t t0 = tfRanges[ti++].second;
		for (size_t k = 0; k < j.ng; k++) geos[j.g0 + k].geometry.triangles.transformData.deviceAddress = tfAddr + (t0 + k) * sizeof(VkTransformMatrixKHR);
	}

	// sizes, structures and scratch
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
	std::vector<VkDeviceSize> scr;
	VkDeviceSize scrTotal = 0;
	for (size_t n = 0; n < jobs.size(); n++) {
		Job &j = jobs[n];
		VkAccelerationStructureBuildGeometryInfoKHR bi = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		bi.flags = j.flags;
		bi.mode = j.update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		bi.geometryCount = (UINT)j.ng;
		bi.pGeometries = &geos[j.g0];
		std::vector<UINT> counts(j.ng);
		for (size_t k = 0; k < j.ng; k++) counts[k] = ranges[j.g0 + k].primitiveCount;
		VkAccelerationStructureBuildSizesInfoKHR sz = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		vkrt.GetAccelerationStructureBuildSizesKHR(dev->dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, counts.data(), &sz);
		if (!j.update && !MakeBlas(*j.b, sz.accelerationStructureSize)) { j.ng = 0; continue; }
		bi.srcAccelerationStructure = j.update ? j.b->as : VK_NULL_HANDLE;
		bi.dstAccelerationStructure = j.b->as;
		VkDeviceSize s = (j.update ? sz.updateScratchSize : sz.buildScratchSize);
		scr.push_back(scrTotal);
		scrTotal += (s + scratchAlign - 1) / scratchAlign * scratchAlign;
		infos.push_back(bi);
		j.b->frame = frame;
	}
	VkDeviceSize tlasOfs = scrTotal;
	scrTotal += tlasScratch;
	VkBuf *sb = Scratch(scrTotal);
	if (!sb) { nInst = 0; return false; }
	VkDeviceAddress sbase = (sb->Address() + scratchAlign - 1) / scratchAlign * scratchAlign;

	std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> pr;
	size_t ii = 0;
	for (Job &j : jobs) {
		if (!j.ng) continue;
		infos[ii].scratchData.deviceAddress = sbase + scr[ii];
		pr.push_back(&ranges[j.g0]);
		ii++;
	}
	if (!infos.empty()) vkrt.CmdBuildAccelerationStructuresKHR(cmd, (UINT)infos.size(), infos.data(), pr.data());
	if (!infos.empty() || copies) Barrier(cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);

	// the new tiles' compacted sizes, read a few frames on
	for (Job &j : jobs) {
		if (!j.compact || !j.ng || freeQueries.empty()) continue;
		int q = freeQueries.back();
		freeQueries.pop_back();
		vkCmdResetQueryPool(cmd, queries, q, 1);
		vkrt.CmdWriteAccelerationStructuresPropertiesKHR(cmd, 1, &j.b->as, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queries, q);
		j.b->query = q;
		pendingCompact.push_back({ q, j.key, frame });
	}

	// instances that have a structure now
	size_t w = 0;
	for (size_t i = 0; i < inst.size(); i++) {
		Blas *b = (Blas *)(uintptr_t)inst[i].accelerationStructureReference;
		if (!b->as) continue;
		inst[w] = inst[i];
		inst[w].accelerationStructureReference = b->addr;
		inst[w].instanceCustomIndex = (uint32_t)w;
		rws[w] = rws[i];
		w++;
	}
	inst.resize(w);
	rws.resize(w);
	nInst = (int)w;
	if (frame % 1000 == 1) LogAlw("DdgiScene: %d instances of %d tiles and %d meshes in range, %d structures built", nInst, (int)tl.size(), (int)ml.size(), (int)infos.size());
	if (!nInst) return false;
	bool refs = inst.size() == lastInst.size() && !memcmp(rws.data(), lastRows.data(), rws.size() * sizeof(Row));
	bool moved = !refs;
	for (size_t i = 0; refs && i < inst.size(); i++) {
		const VkAccelerationStructureInstanceKHR &a = inst[i], &b = lastInst[i];
		refs = a.accelerationStructureReference == b.accelerationStructureReference && a.instanceCustomIndex == b.instanceCustomIndex && a.mask == b.mask;
		for (int r = 0; r < 3 && !moved; r++) for (int c = 0; c < 4; c++) if (std::fabs(a.transform.matrix[r][c] - b.transform.matrix[r][c]) > (c == 3 ? 1e-3f : 1e-6f)) moved = true;
	}
	if (refs && !moved && infos.empty() && !copies) return true; // nothing built and nothing moved by a millimetre: the structure and the rows hold
	bool refit = refs && tlasRefits < 64; // the same instances: their bounds again
	tlasRefits = refit ? tlasRefits + 1 : 0;
	lastInst = inst;
	if (!refit) {
		lastRows = rws;
		rows->Upload(rws.data(), rws.size() * sizeof(Row));
	}

	void *ip;
	VkBuffer ib;
	VkDeviceSize iofs = dev->AllocTransient(inst.size() * sizeof(VkAccelerationStructureInstanceKHR), 16, &ip, &ib);
	memcpy(ip, inst.data(), inst.size() * sizeof(VkAccelerationStructureInstanceKHR));
	VkAccelerationStructureGeometryKHR g = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	g.geometry.instances.data.deviceAddress = Address(dev, ib) + iofs;
	VkAccelerationStructureBuildGeometryInfoKHR tb = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	tb.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	tb.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	tb.mode = refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	tb.srcAccelerationStructure = refit ? tlas : VK_NULL_HANDLE;
	tb.dstAccelerationStructure = tlas;
	tb.geometryCount = 1;
	tb.pGeometries = &g;
	tb.scratchData.deviceAddress = sbase + tlasOfs;
	VkAccelerationStructureBuildRangeInfoKHR tr = { (UINT)nInst, 0, 0, 0 };
	const VkAccelerationStructureBuildRangeInfoKHR *ptr = &tr;
	vkrt.CmdBuildAccelerationStructuresKHR(cmd, 1, &tb, &ptr);
	Barrier(cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT);
	return true;
}
