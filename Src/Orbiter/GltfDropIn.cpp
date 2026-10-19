// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// not upstream: a .glb from Blender in a vessel's mesh folder stands in for its .msh meshes (claude/design-gltf-roundtrip.md 2, 3)

#include "GltfDropIn.h"
#include "GltfRoundTrip.h"
#include "Mesh.h"
#include "Orbiter.h"
#include "Log.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <sys/stat.h>

extern Orbiter *g_pOrbiter;
namespace fs = std::filesystem;

namespace {

struct FileInfo {
	std::string path;
	long long mtime = 0;
	std::vector<std::string> names;
};

struct Built {
	Mesh *mesh = nullptr;
	std::shared_ptr<const RtFile> file;
	int entry = -1, addedBase = 0;
};

std::mutex lock;
std::map<std::string, std::vector<FileInfo>> folders;   // resolved folder -> files with Orbiter data
std::map<std::string, std::shared_ptr<RtFile>> files;    // path -> parsed, null when it failed
std::map<std::string, std::string> hashes;               // path#image -> pixel hash
std::map<std::string, Built> built;                      // mesh name key -> its mesh
std::set<std::string> logged;

std::string Key (const std::string &n)
{
	std::string k = n;
	for (char &c : k) { if (c == '/') c = '\\'; else if (c >= 'A' && c <= 'Z') c += 32; }
	return k;
}

long long MTime (const std::string &p)
{
	struct stat st;
	return stat (p.c_str(), &st) ? 0 : (long long)st.st_mtim.tv_sec * 1000000000ll + st.st_mtim.tv_nsec;
}

const std::vector<FileInfo> &Folder (const std::string &dir)
{
	auto it = folders.find (dir);
	if (it != folders.end()) return it->second;
	std::vector<FileInfo> v;
	std::error_code ec;
	for (fs::directory_iterator d (dir, ec), end; !ec && d != end; d.increment (ec)) {
		if (!d->is_regular_file (ec)) continue;
		std::string n = d->path().filename().string();
		if (!GltfIsName (n.c_str())) continue;
		FileInfo fi;
		fi.path = d->path().string();
		fi.mtime = MTime (fi.path);
		std::string json, err;
		if (!GltfReadJson (fi.path, json, err) || !RtListedMeshes (json, fi.names)) {
			if (logged.insert (fi.path).second) LOGOUT ("glTF file %s: no Orbiter data, not used for meshes (from Blender: export with Include > Custom Properties)", fi.path.c_str());
			continue;
		}
		v.push_back (std::move (fi));
	}
	std::sort (v.begin(), v.end(), [](const FileInfo &a, const FileInfo &b) { return a.path < b.path; });
	return folders[dir] = std::move (v);
}

// the file that records mesh N: its folder first, then the folders above it up to the mesh directory
const FileInfo *Find (const std::string &name)
{
	std::string mp = g_pOrbiter->MeshPath (name.c_str());
	std::string rel = name + ".msh";
	if (mp.size() < rel.size()) return nullptr;
	std::string root = mp.substr (0, mp.size() - rel.size());
	std::string n = name;
	for (char &c : n) if (c == '/') c = '\\';
	std::vector<std::string> dirs;
	for (size_t p = n.find ('\\'); p != std::string::npos; p = n.find ('\\', p + 1)) dirs.push_back (n.substr (0, p + 1));
	std::reverse (dirs.begin(), dirs.end());
	dirs.push_back ("");
	for (const std::string &d : dirs) {
		std::string dir = oapiResolvePath ((root + d).c_str());
		if (dir.empty()) dir = "./";
		const FileInfo *best = nullptr;
		std::vector<const FileInfo*> all;
		for (const FileInfo &fi : Folder (dir))
			for (const std::string &m : fi.names)
				if (RtSameMeshName (m, name)) { all.push_back (&fi); if (!best || fi.mtime > best->mtime) best = &fi; break; }
		if (!best) continue;
		if (all.size() > 1 && logged.insert ("several " + Key (name)).second)
			for (const FileInfo *o : all) if (o != best) LOGOUT_WARN ("Mesh %s: %s is newer than %s, used", name.c_str(), best->path.c_str(), o->path.c_str());
		return best;
	}
	return nullptr;
}

std::shared_ptr<RtFile> Parsed (const std::string &path)
{
	auto it = files.find (path);
	if (it != files.end()) return it->second;
	auto f = std::make_shared<RtFile> ();
	std::string err;
	if (!RtLoad (path, *f, err)) {
		LOGOUT_ERR ("glTF file %s: %s", path.c_str(), err.c_str());
		f.reset ();
	} else {
		for (const std::string &w : f->scene.warnings) LOGOUT_WARN ("glTF file %s: %s", path.c_str(), w.c_str());
		for (const std::string &w : f->warnings) LOGOUT_WARN ("glTF file %s: %s", path.c_str(), w.c_str());
	}
	return files[path] = f;
}

// pixel hashes of a file's images, in parallel on half the cores
void Hash (const RtFile &f, const std::vector<int> &imgs)
{
	std::vector<int> todo;
	for (int i : imgs) if (!hashes.count (f.path + "#" + std::to_string (i))) todo.push_back (i);
	std::vector<std::string> out (todo.size());
	std::atomic<size_t> next{0};
	int nt = std::max (1, std::min ((int)todo.size(), (int)std::thread::hardware_concurrency() / 2));
	std::vector<std::thread> pool;
	for (int t = 0; t < nt; t++)
		pool.emplace_back ([&]() {
			for (size_t k; (k = next++) < todo.size();) {
				std::vector<unsigned char> b;
				const std::string &ref = f.scene.images[todo[k]];
				if (!ref.empty() && GltfImageBytes (ref, b)) out[k] = RtImageHash (b);
			}
		});
	for (auto &t : pool) t.join ();
	for (size_t k = 0; k < todo.size(); k++) hashes[f.path + "#" + std::to_string (todo[k])] = out[k];
}

std::string Synthetic (const RtRecMesh &M)
{
	std::ostringstream s;
	s << "MSHX1\nGROUPS 0\nMATERIALS " << M.mtrls.size() << "\n";
	for (size_t i = 0; i < M.mtrls.size(); i++) s << "m" << i << "\n";
	for (size_t i = 0; i < M.mtrls.size(); i++) {
		const MATERIAL &x = M.mtrls[i];
		s << "MATERIAL m" << i << "\n" << x.diffuse.r << " " << x.diffuse.g << " " << x.diffuse.b << " " << x.diffuse.a << "\n"
		  << x.ambient.r << " " << x.ambient.g << " " << x.ambient.b << " " << x.ambient.a << "\n"
		  << x.specular.r << " " << x.specular.g << " " << x.specular.b << " " << x.specular.a << " " << x.power << "\n"
		  << x.emissive.r << " " << x.emissive.g << " " << x.emissive.b << " " << x.emissive.a << "\n";
	}
	s << "TEXTURES " << M.tex.size() << "\n";
	for (const RtRecTex &t : M.tex) s << (t.name.empty() ? std::string ("0") : t.name) << (t.dynamic ? " D" : "") << "\n";
	return s.str();
}

Built Build (const std::string &name, std::shared_ptr<RtFile> file, int e)
{
	const RtFile &f = *file;
	const RtRecMesh &M = f.rec.meshes[e];
	const RtMeshPlan &P = f.plans[e];
	oapi::GraphicsClient *gc = g_pOrbiter->GetGraphicsClient ();
	Mesh *m = new Mesh; TRACENEW
	{
		std::ifstream ifs (oapiResolvePath (g_pOrbiter->MeshPath (name.c_str())));
		if (ifs) ifs >> *m;
	}
	bool base = m->nGroup() > 0;
	if (!base) {
		m->Clear ();
		std::istringstream is (Synthetic (M));
		is >> *m;
	}
	DWORD nBase = m->nGroup ();
	if (base && (int)nBase != M.groups) LOGOUT_WARN ("Mesh %s: the .msh has %d groups, the file recorded %d; matched by index", name.c_str(), (int)nBase, M.groups);
	int total = std::max ((int)nBase, M.groups);
	std::set<int> skipped (M.skipped.begin(), M.skipped.end());

	// which groups change
	enum { KEEP, FILEGEO, EMPTIED, EMPTY };
	std::vector<int> kind (total, KEEP);
	std::vector<std::vector<const RtOut*>> parts (total);
	for (int g = 0; g < total; g++) {
		auto it = P.primary.find (g);
		if (it == P.primary.end()) {
			if (g < (int)nBase) kind[g] = skipped.count (g) ? KEEP : EMPTIED;
			else kind[g] = EMPTY;
			continue;
		}
		const RtOut &o0 = P.outs[it->second];
		for (const RtOut &o : P.outs) if (o.node == o0.node && o.prim == o0.prim) parts[g].push_back (&o);
		if (o0.idx.empty()) { kind[g] = g < (int)nBase ? EMPTIED : EMPTY; continue; } // the node is there, its triangles are gone
		if (g >= (int)nBase) { kind[g] = FILEGEO; continue; }
		GroupSpec *G = m->GetGroup (g);
		RtBaseGroup b{ G->Vtx, G->nVtx, G->Idx, G->nIdx };
		kind[g] = RtSameGeometry (parts[g], b) ? KEEP : FILEGEO;
	}
	std::set<int> keptParts; // the other parts of a kept group's primitive: empty new groups
	for (int g = 0; g < total; g++) if (kind[g] == KEEP) for (const RtOut *o : parts[g]) if (o->added >= 0) keptParts.insert (o->added);
	bool anyFile = std::count (kind.begin(), kind.end(), (int)FILEGEO) > 0 || P.nadded > 0;

	// materials and textures (design 3.2)
	std::vector<RtMatTag> tags;
	for (const std::string &j : f.scene.materialJson) tags.push_back (RtMaterialTag (j));
	std::vector<int> imgs;
	for (size_t j = 0; j < tags.size(); j++)
		if (tags[j].tagged && tags[j].mesh == M.index && tags[j].tex >= 0 && f.scene.materialImage[j] >= 0) imgs.push_back (f.scene.materialImage[j]);
	Hash (f, imgs);
	int nMat = 0, nTex = 0;
	std::set<int> doneMtrl, doneTex;
	for (size_t j = 0; j < tags.size(); j++) {
		const RtMatTag &t = tags[j];
		if (!t.tagged || t.mesh != M.index) continue;
		if (t.edited && t.mtrl >= 0 && t.mtrl < (int)m->nMaterial()) {
			if (doneMtrl.insert (t.mtrl).second) {
				*m->GetMaterial (t.mtrl) = f.scene.materials[j].mat;
				m->SetPbr (t.mtrl, f.scene.materials[j].pbr);
				nMat++;
			} else LOGOUT_WARN ("Mesh %s: material %d edited twice in the file, the first used", name.c_str(), t.mtrl);
		}
		int ii = f.scene.materialImage[j];
		if (t.tex >= 0 && ii >= 0 && t.tex < (int)M.tex.size()) {
			const std::string &h = hashes[f.path + "#" + std::to_string (ii)];
			if (h != M.tex[t.tex].hash && !h.empty()) {
				if (t.tex >= (int)m->nTexture()) LOGOUT_WARN ("Mesh %s: texture %d not in the .msh, the file's image not used", name.c_str(), t.tex);
				else if (doneTex.insert (t.tex).second) {
					SURFHANDLE s = gc ? gc->clbkLoadTexture (f.scene.images[ii].c_str(), 8 | (M.tex[t.tex].dynamic ? 2 : 0)) : 0;
					if (s) { m->SetTexture (t.tex, s, true); nTex++; }
					else if (gc) LOGOUT_WARN ("Mesh %s: image for texture %d not loaded", name.c_str(), t.tex);
				}
			}
		}
	}
	std::map<int, DWORD> matOf, texOf;
	std::map<std::string, DWORD> texRef;
	auto material = [&](int j, DWORD &mt, DWORD &tx) {
		if (j < 0 || j >= (int)f.scene.materials.size()) j = (int)f.scene.materials.size() - 1; // the default material
		if (j < (int)tags.size() && tags[j].tagged && tags[j].mesh == M.index) {
			mt = tags[j].mtrl >= 0 ? (DWORD)tags[j].mtrl : SPEC_DEFAULT;
			tx = tags[j].tex >= 0 ? (DWORD)tags[j].tex : SPEC_DEFAULT;
			return;
		}
		if (!matOf.count (j)) {
			MATERIAL x = f.scene.materials[j].mat;
			int k = m->AddMaterial (x);
			m->SetPbr (k, f.scene.materials[j].pbr);
			matOf[j] = (DWORD)k;
			DWORD t = SPEC_DEFAULT;
			int ti = f.scene.materials[j].tex;
			if (ti >= 0 && gc) {
				const std::string &ref = f.scene.textures[ti];
				auto r = texRef.find (ref);
				if (r != texRef.end()) t = r->second;
				else if (SURFHANDLE s = gc->clbkLoadTexture (ref.c_str(), 8)) t = texRef[ref] = (DWORD)m->AddTexture (s);
			}
			texOf[j] = t;
		}
		mt = matOf[j];
		tx = texOf[j];
	};

	// groups
	if (anyFile) { // a changed group's own material must not reach the groups that inherit from it
		DWORD pm = SPEC_DEFAULT, pt = SPEC_DEFAULT;
		for (DWORD g = 0; g < m->nGroup(); g++) {
			GroupSpec *G = m->GetGroup (g);
			if (G->MtrlIdx == SPEC_INHERIT) G->MtrlIdx = pm;
			if (G->TexIdx == SPEC_INHERIT) G->TexIdx = pt;
			pm = G->MtrlIdx; pt = G->TexIdx;
		}
	}
	int changed = 0, emptied = 0, added = 0;
	auto arrays = [](const RtOut &o, NTVERTEX *&v, WORD *&i) {
		v = new NTVERTEX[std::max<size_t> (1, o.vtx.size())]; TRACENEW
		if (!o.vtx.empty()) memcpy (v, o.vtx.data(), o.vtx.size() * sizeof (NTVERTEX));
		i = new WORD[std::max<size_t> (1, o.idx.size())]; TRACENEW
		if (!o.idx.empty()) memcpy (i, o.idx.data(), o.idx.size() * sizeof (WORD));
	};
	for (int g = 0; g < total; g++) {
		if (g < (int)nBase) {
			GroupSpec *G = m->GetGroup (g);
			if (kind[g] == FILEGEO) {
				const RtOut &o = *parts[g][0];
				delete []G->Vtx;
				delete []G->Idx;
				arrays (o, G->Vtx, G->Idx);
				G->nVtx = (DWORD)o.vtx.size ();
				G->nIdx = (DWORD)o.idx.size ();
				material (o.material, G->MtrlIdx, G->TexIdx);
				changed++;
			} else if (kind[g] == EMPTIED) {
				G->nIdx = 0;
				emptied++;
			}
		} else {
			NTVERTEX *v; WORD *i;
			DWORD mt = SPEC_DEFAULT, tx = SPEC_DEFAULT, nv = 0, ni = 0;
			if (kind[g] == FILEGEO) {
				const RtOut &o = *parts[g][0];
				arrays (o, v, i);
				nv = (DWORD)o.vtx.size (); ni = (DWORD)o.idx.size ();
				material (o.material, mt, tx);
				changed++;
			} else {
				v = new NTVERTEX[1]; TRACENEW
				memset (v, 0, sizeof (NTVERTEX));
				i = new WORD[1]; TRACENEW
				nv = 1; // one vertex, no triangles: Mesh::SetupGroup divides by the vertex count
				if (base || skipped.count (g)) LOGOUT_WARN ("Mesh %s: group %d is in neither the .msh nor the file, left empty", name.c_str(), g);
			}
			m->AddGroup (v, nv, i, ni, mt, tx, 0, g < (int)M.flags.size() ? M.flags[g] : 0);
		}
	}
	std::vector<const RtOut*> extra (P.nadded, nullptr);
	for (const RtOut &o : P.outs) if (o.added >= 0) extra[o.added] = &o;
	for (int j = 0; j < P.nadded; j++) {
		const RtOut &o = *extra[j];
		NTVERTEX *v; WORD *i;
		DWORD mt, tx;
		arrays (o, v, i);
		material (o.material, mt, tx);
		bool keep = !keptParts.count (j);
		m->AddGroup (v, keep ? (DWORD)o.vtx.size () : std::min<DWORD> (1, (DWORD)o.vtx.size ()), i, keep ? (DWORD)o.idx.size () : 0, mt, tx); // a kept group's other part: no triangles
		if (keep) added++;
	}
	m->Setup ();
	m->SetName (name.c_str());
	LOGOUT ("Mesh %s from %s: %d groups, %d changed, %d new, %d emptied; %d materials and %d textures from the file",
		name.c_str(), f.path.c_str(), (int)m->nGroup(), changed, added, emptied, nMat, nTex);
	Built b;
	b.mesh = m;
	b.file = file;
	b.entry = e;
	b.addedBase = total;
	return b;
}

const Built *Get (const char *meshname)
{
	if (!meshname || !meshname[0] || GltfIsName (meshname) || !g_pOrbiter) return nullptr;
	std::string name = meshname, key = Key (name);
	auto it = built.find (key);
	if (it != built.end()) return it->second.mesh ? &it->second : nullptr;
	Built &b = built[key];
	const FileInfo *fi = Find (name);
	if (!fi) return nullptr;
	std::shared_ptr<RtFile> f = Parsed (fi->path);
	if (!f) return nullptr;
	int e = -1;
	for (size_t i = 0; i < f->rec.meshes.size(); i++) if (RtSameMeshName (f->rec.meshes[i].name, name)) e = (int)i;
	if (e < 0) return nullptr;
	b = Build (name, f, e);
	return &b;
}

} // namespace

bool GltfDropInMesh (const char *meshname, Mesh &mesh)
{
	std::lock_guard<std::mutex> g (lock);
	const Built *b = Get (meshname);
	if (!b) return false;
	mesh.Set (*b->mesh);
	if (oapi::GraphicsClient *gc = g_pOrbiter->GetGraphicsClient ())
		for (DWORD i = 0; i < mesh.nTexture(); i++) if (mesh.GetTexture (i)) gc->clbkIncrSurfaceRef (mesh.GetTexture (i)); // the copy releases its own reference
	return true;
}

bool GltfDropInSlot (const char *meshname, GltfDropSlot &slot)
{
	std::lock_guard<std::mutex> g (lock);
	const Built *b = Get (meshname);
	if (!b) return false;
	slot.file = b->file;
	slot.entry = b->entry;
	slot.addedBase = b->addedBase;
	return true;
}

void GltfDropInFlush ()
{
	std::lock_guard<std::mutex> g (lock);
	for (auto &kv : built) delete kv.second.mesh;
	built.clear ();
	files.clear ();
	folders.clear ();
	hashes.clear ();
	logged.clear ();
}
