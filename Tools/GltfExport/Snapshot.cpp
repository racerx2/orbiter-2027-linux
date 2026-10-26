// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: a vessel's meshes and animations copied through the SDK (claude/design-gltf-export.md 4, 5.2)

#include "Snapshot.h"
#include "AnimModel.h"
#include "Orbitersdk.h"
#include <cstring>
#include <map>
#include <strings.h>

static const DWORD kSpecDefault = (DWORD)(-1), kSpecInherit = (DWORD)(-2); // Mesh.h

static std::string CfgDir (FILEHANDLE f, const char *item, const char *def)
{
	char buf[512] = "";
	std::string d = def;
	if (f && oapiReadItem_string (f, (char*)item, buf) && buf[0]) {
		d = buf;
		if (d.back () != '\\') d += '\\'; // as Config::GetDir
	}
	return d;
}

static bool IsGltf (const std::string &n)
{
	auto ends = [&](const char *e) { size_t k = strlen (e); return n.size () >= k && !strcasecmp (n.c_str () + n.size () - k, e); };
	return ends (".glb") || ends (".gltf");
}

static void CopyMesh (MESHHANDLE h, GxMesh &M)
{
	DWORD nm = oapiMeshMaterialCount (h);
	for (DWORD i = 0; i < nm; i++) {
		MATERIAL *m = oapiMeshMaterial (h, i);
		GxMaterial g{};
		if (m) {
			const COLOUR4 *c[4] = {&m->diffuse, &m->ambient, &m->specular, &m->emissive};
			float *d[4] = {g.diffuse, g.ambient, g.specular, g.emissive};
			for (int k = 0; k < 4; k++) { d[k][0] = c[k]->r; d[k][1] = c[k]->g; d[k][2] = c[k]->b; d[k][3] = c[k]->a; }
			g.power = m->power;
		}
		M.mtrls.push_back (g);
	}
	M.ntex = (int)oapiMeshTextureCount (h);
	DWORD ng = oapiMeshGroupCount (h);
	DWORD prevM = kSpecDefault, prevT = kSpecDefault;
	for (DWORD g = 0; g < ng; g++) {
		MESHGROUPEX *G = oapiMeshGroupEx (h, g);
		GxGroup out;
		if (G) {
			out.vtx.resize (G->nVtx);
			for (DWORD i = 0; i < G->nVtx; i++) {
				const NTVERTEX &v = G->Vtx[i];
				out.vtx[i] = GxVertex{{v.x, v.y, v.z}, {v.nx, v.ny, v.nz}, {v.tu, v.tv}};
			}
			out.idx.assign (G->Idx, G->Idx + G->nIdx);
			for (WORD &x : out.idx) if (x >= G->nVtx) x = 0;
			DWORD mt = G->MtrlIdx, tx = G->TexIdx; // the client's ProcessInherit
			if (mt == kSpecInherit) mt = g ? prevM : kSpecDefault;
			if (tx == kSpecInherit) tx = g ? prevT : kSpecDefault;
			prevM = mt; prevT = tx;
			out.mtrl = (mt == kSpecDefault || mt >= nm) ? -1 : (int)mt;
			out.tex = (tx == kSpecDefault || tx >= (DWORD)M.ntex) ? -1 : (int)tx;
			out.usrFlag = G->UsrFlag;
		}
		M.groups.push_back (std::move (out));
	}
}

static GxVec3 V (const VECTOR3 &v) { return {v.x, v.y, v.z}; }

bool GxTakeSnapshot (VESSEL *v, bool hasClient, GxSnapshot &s, std::string &err)
{
	s = GxSnapshot ();
	s.vesselName = v->GetName ();
	s.className = v->GetClassName () ? v->GetClassName () : "";

	VISHANDLE *vp = oapiObjectVisualPtr (v->GetHandle ());
	bool visual = vp && *vp;
	if (hasClient && !visual) { err = "Bring the camera to the vessel and export again"; return false; }

	FILEHANDLE f = oapiOpenFile ("Orbiter.cfg", FILE_IN_ZEROONFAIL, ROOT);
	std::string meshDir = CfgDir (f, "MeshDir", ".\\Meshes\\");
	s.texDirs.push_back (CfgDir (f, "HightexDir", ".\\Textures2\\"));
	s.texDirs.push_back (CfgDir (f, "TextureDir", ".\\Textures\\"));
	if (f) oapiCloseFile (f, FILE_IN_ZEROONFAIL);

	UINT nmesh = v->GetMeshCount ();
	for (UINT i = 0; i < nmesh; i++) {
		WORD vis = v->GetMeshVisibilityMode (i);
		if (vis == MESHVIS_NEVER) continue;
		GxMesh M;
		M.index = (int)i;
		M.vis = vis;
		VECTOR3 ofs = _V (0, 0, 0);
		v->GetMeshOffset (i, ofs);
		M.ofs = V (ofs);
		MESHHANDLE h = v->GetMeshTemplate (i);
		bool temp = false;
		if (h) {
			const char *fn = oapiGetMeshFilename (h);
			M.name = fn ? fn : "";
		} else {
			const char *mn = v->GetMeshName (i);
			if (!mn) { s.warnings.push_back ("mesh " + std::to_string (i) + ": no template and no name, left out"); continue; }
			M.name = mn;
			h = oapiLoadMesh (mn);
			temp = true;
			if (!h) { s.warnings.push_back ("mesh " + std::to_string (i) + " (" + M.name + "): not loaded, left out"); continue; }
		}
		M.gltf = IsGltf (M.name);
		if (!M.name.empty ()) M.file = meshDir + M.name + (M.gltf ? "" : ".msh"); // Config::MeshPath
		CopyMesh (h, M);
		if (temp) oapiDeleteMesh (h);
		s.meshes.push_back (std::move (M));
	}

	ANIMATION *anim = nullptr;
	UINT na = v->GetAnimPtr (&anim);
	std::map<const ANIMATIONCOMP*, int> idOf;
	for (UINT a = 0; a < na && anim; a++) {
		GxAnim A;
		A.defstate = anim[a].defstate;
		A.state = anim[a].state;
		for (UINT c = 0; c < anim[a].ncomp; c++) {
			const ANIMATIONCOMP *ac = anim[a].comp[c];
			if (!ac || !ac->trans) continue;
			GxComp k;
			k.anim = (int)a;
			k.index = (int)A.comps.size ();
			k.state0 = ac->state0;
			k.state1 = ac->state1;
			const MGROUP_TRANSFORM *t = ac->trans;
			k.mesh = t->mesh == LOCALVERTEXLIST ? -1 : (int)t->mesh;
			if (k.mesh >= 0) {
				if (!t->grp) k.allGroups = true;
				else for (UINT g = 0; g < t->ngrp; g++) k.groups.push_back ((int)t->grp[g]);
			}
			switch (t->Type ()) {
			case MGROUP_TRANSFORM::ROTATE: {
				const MGROUP_ROTATE *r = (const MGROUP_ROTATE*)t;
				k.type = GxType::Rotate; k.ref = V (r->ref); k.axis = V (r->axis); k.angle = r->angle;
			} break;
			case MGROUP_TRANSFORM::TRANSLATE:
				k.type = GxType::Translate; k.shift = V (((const MGROUP_TRANSLATE*)t)->shift);
				break;
			case MGROUP_TRANSFORM::SCALE: {
				const MGROUP_SCALE *sc = (const MGROUP_SCALE*)t;
				k.type = GxType::Scale; k.ref = V (sc->ref); k.scale = V (sc->scale);
			} break;
			default:
				k.type = GxType::Null;
				break;
			}
			idOf[ac] = (int)s.anims.comps.size ();
			A.comps.push_back ((int)s.anims.comps.size ());
			s.anims.comps.push_back (std::move (k));
		}
		s.anims.anims.push_back (std::move (A));
	}
	for (UINT a = 0; a < na && anim; a++)
		for (UINT c = 0; c < anim[a].ncomp; c++) {
			const ANIMATIONCOMP *ac = anim[a].comp[c];
			auto me = idOf.find (ac);
			if (me == idOf.end () || !ac->parent) continue;
			auto p = idOf.find (ac->parent);
			if (p == idOf.end ()) { s.warnings.push_back ("animation " + std::to_string (a) + " part " + std::to_string (c) + ": parent not found, taken as a root"); continue; }
			s.anims.comps[me->second].parent = p->second;
			s.anims.comps[p->second].children.push_back (me->second);
		}

	// group and mesh indices the meshes don't have (design R10)
	for (GxComp &k : s.anims.comps) {
		if (k.mesh < 0) continue;
		const GxMesh *M = nullptr;
		for (const GxMesh &m : s.meshes) if (m.index == k.mesh) M = &m;
		if (!M) continue; // a never-visible or missing mesh: nothing to move
		std::vector<int> ok;
		for (int g : k.groups) if (g >= 0 && g < (int)M->groups.size ()) ok.push_back (g);
		if (ok.size () != k.groups.size ()) s.warnings.push_back ("animation " + std::to_string (k.anim) + " part " + std::to_string (k.index) + ": group index out of range, skipped");
		k.groups = ok;
	}

	if (visual) GxRecoverRest (s.anims); // the visual has moved the descendants' parameters up to each state (design 5.2)
	return true;
}
