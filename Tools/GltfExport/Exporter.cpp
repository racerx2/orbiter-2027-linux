// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: the worker part, snapshot to .glb file without any Orbiter API call (claude/design-gltf-export.md 8)

#include "Exporter.h"
#include "AnimModel.h"
#include "DdsDecode.h"
#include "GlbWriter.h"
#include "MshText.h"
#include <QBuffer>
#include <QImage>
#include <QImageWriter>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>

namespace fs = std::filesystem;

namespace {

bool ReadBytes (const std::string &path, std::vector<uint8_t> &out)
{
	std::ifstream f (path, std::ios::binary);
	if (!f) return false;
	f.seekg (0, std::ios::end);
	std::streamoff n = f.tellg ();
	if (n < 0) return false;
	f.seekg (0);
	out.resize ((size_t)n);
	return n == 0 || (bool)f.read ((char*)out.data (), n);
}

std::string Lower (std::string s)
{
	for (char &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
	return s;
}

bool EndsWith (const std::string &s, const char *e)
{
	size_t n = strlen (e);
	return s.size () >= n && Lower (s.substr (s.size () - n)) == e;
}

std::string Variant (const std::string &name, const char *id) // NatCreateName: <stem>_<id>.<ext>
{
	size_t p = name.rfind ('.');
	if (p == std::string::npos) return "";
	return name.substr (0, p) + "_" + id + name.substr (p);
}

bool LoadRgba (const std::string &path, GxImage &img, std::string &err)
{
	if (EndsWith (path, ".dds")) {
		std::vector<uint8_t> b;
		if (!ReadBytes (path, b)) { err = "cannot read"; return false; }
		return GxDecodeDds (b.data (), b.size (), img, err);
	}
	QImage q (QString::fromStdString (path));
	if (q.isNull ()) { err = "cannot read the image"; return false; }
	q = q.convertToFormat (QImage::Format_RGBA8888);
	img = GxImage ();
	img.w = q.width (); img.h = q.height ();
	img.rgba.resize ((size_t)img.w * img.h * 4);
	for (int y = 0; y < img.h; y++) memcpy (&img.rgba[(size_t)y * img.w * 4], q.constScanLine (y), (size_t)img.w * 4);
	return true;
}

bool Png (const GxImage &img, std::vector<uint8_t> &out)
{
	QImage q (img.rgba.data (), img.w, img.h, img.w * 4, QImage::Format_RGBA8888);
	QByteArray ba;
	QBuffer buf (&ba);
	buf.open (QIODevice::WriteOnly);
	QImageWriter w (&buf, "png");
	w.setQuality (50);
	if (!w.write (q)) return false;
	out.assign (ba.begin (), ba.end ());
	return true;
}

enum Kind { kBase, kNormal, kEmissive, kMr, kGlow };

struct Job {
	Kind kind = kBase;
	std::string a, b;            // file paths; kMr: a the _rghn, b the _metal (either may be ""); kGlow: a the base, b the _emis
	float e[3] = {0, 0, 0};      // kGlow: the material's emissive colour
	GxPng out;
	bool ok = false;
	std::string err, hash;   // kBase: GxPixelHash of the image as written
};

void Bilinear (const GxImage &im, double u, double v, double out[3]) // wrapped, as the client's samplers
{
	double x = u * im.w - 0.5, y = v * im.h - 0.5;
	int x0 = (int)std::floor (x), y0 = (int)std::floor (y);
	double fx = x - x0, fy = y - y0;
	auto at = [&](int xx, int yy, int c) { xx = ((xx % im.w) + im.w) % im.w; yy = ((yy % im.h) + im.h) % im.h; return (double)im.rgba[((size_t)yy * im.w + xx) * 4 + c]; };
	for (int c = 0; c < 3; c++)
		out[c] = (at (x0, y0, c) * (1 - fx) + at (x0 + 1, y0, c) * fx) * (1 - fy) + (at (x0, y0 + 1, c) * (1 - fx) + at (x0 + 1, y0 + 1, c) * fx) * fy;
}

void Run (Job &j)
{
	GxImage img;
	if (j.kind == kGlow) { // max (emis, base * emissive) in display values, as PBR_PS and Metalness.fx (design-gltf-simpose-glow 2)
		GxImage b, e;
		if (!LoadRgba (j.a, b, j.err) || !LoadRgba (j.b, e, j.err)) return;
		if (b.w <= 0 || b.h <= 0 || e.w <= 0 || e.h <= 0) { j.err = "empty image"; return; }
		img.w = std::max (b.w, e.w); img.h = std::max (b.h, e.h);
		img.rgba.resize ((size_t)img.w * img.h * 4);
		for (int y = 0; y < img.h; y++)
			for (int x = 0; x < img.w; x++) {
				double u = (x + 0.5) / img.w, v = (y + 0.5) / img.h, pb[3], pe[3];
				Bilinear (b, u, v, pb);
				Bilinear (e, u, v, pe);
				uint8_t *o = &img.rgba[((size_t)y * img.w + x) * 4];
				for (int c = 0; c < 3; c++) o[c] = (uint8_t)std::lround (std::clamp (std::max (pe[c], pb[c] * j.e[c]), 0.0, 255.0));
				o[3] = 255;
			}
		j.out.alpha = GxAlpha::Opaque;
	} else if (j.kind != kMr) {
		if (!LoadRgba (j.a, img, j.err)) return;
		// a normal map stays as it is: the client's bitangent cross(T, N)*w points up the image (Metalness.fx, Vessel.fx), glTF's green too
		j.out.alpha = j.kind == kBase ? GxImageAlpha (img) : GxAlpha::Opaque;
		if (j.kind == kBase) j.hash = GxPixelHash (img.w, img.h, img.rgba.data ());
	} else {
		GxImage r, m;
		if (!j.a.empty () && !LoadRgba (j.a, r, j.err)) return;
		if (!j.b.empty () && !LoadRgba (j.b, m, j.err)) return;
		const GxImage &sz = j.a.empty () ? m : r;
		img.w = sz.w; img.h = sz.h;
		img.rgba.assign ((size_t)img.w * img.h * 4, 255);
		for (int y = 0; y < img.h; y++)
			for (int x = 0; x < img.w; x++) {
				uint8_t *o = &img.rgba[((size_t)y * img.w + x) * 4];
				if (!j.a.empty ()) o[1] = (uint8_t)(255 - r.rgba[((size_t)y * r.w + x) * 4 + 1]); // smoothness in G
				if (!j.b.empty ()) { int mx = x * m.w / img.w, my = y * m.h / img.h; o[2] = m.rgba[((size_t)my * m.w + mx) * 4 + 1]; } // metalness in G
			}
	}
	j.ok = Png (img, j.out.png);
	if (!j.ok) j.err = "PNG encoding failed";
}

}

GxResult GxExportFile (const GxSnapshot &snap, const std::string &outPath, const GxResolver &resolve, int threads)
{
	GxResult res;
	res.warnings = snap.warnings;
	auto exists = [](const std::string &p) { std::error_code ec; return !p.empty () && fs::is_regular_file (p, ec); };
	auto findTex = [&](const std::string &name) -> std::string {
		if (name.empty () || name == "0") return "";
		for (const std::string &d : snap.texDirs) { std::string p = resolve (d + name); if (exists (p)) return p; }
		return "";
	};

	// .msh text: labels, material names, texture names
	std::vector<std::vector<std::string>> labels (snap.meshes.size ()), mtrlNames (snap.meshes.size ()), texNames (snap.meshes.size ());
	std::vector<std::vector<bool>> texDyn (snap.meshes.size ());
	for (size_t mi = 0; mi < snap.meshes.size (); mi++) {
		const GxMesh &M = snap.meshes[mi];
		if (M.file.empty () || M.gltf) continue;
		std::ifstream f (resolve (M.file));
		GxMshText t;
		if (!f || !GxReadMshText (f, t)) { res.warnings.push_back (M.name + ": mesh file not read, no labels or texture names"); continue; }
		if (t.labels.size () == M.groups.size ()) labels[mi] = t.labels;
		else res.warnings.push_back (M.name + ": the file has " + std::to_string (t.labels.size ()) + " groups, the mesh " + std::to_string (M.groups.size ()) + ": no labels");
		mtrlNames[mi] = t.materials;
		if ((int)t.textures.size () != M.ntex) res.warnings.push_back (M.name + ": the file lists " + std::to_string (t.textures.size ()) + " textures, the mesh has " + std::to_string (M.ntex));
		size_t n = std::min (t.textures.size (), (size_t)std::max (0, M.ntex));
		texNames[mi].assign (t.textures.begin (), t.textures.begin () + n);
		texDyn[mi].assign (t.dynamic.begin (), t.dynamic.begin () + std::min (n, t.dynamic.size ()));
	}

	// images: each file (or map pair) once
	std::vector<Job> jobs;
	std::map<std::tuple<int, std::string, std::string>, int> jobOf;
	auto job = [&](Kind k, const std::string &a, const std::string &b) -> int {
		auto key = std::make_tuple ((int)k, a, b);
		auto it = jobOf.find (key);
		if (it != jobOf.end ()) return it->second;
		Job j; j.kind = k; j.a = a; j.b = b;
		jobs.push_back (j);
		return jobOf[key] = (int)jobs.size () - 1;
	};
	std::vector<std::vector<GxTexSet>> tex (snap.meshes.size ());
	for (size_t mi = 0; mi < snap.meshes.size (); mi++) {
		const GxMesh &M = snap.meshes[mi];
		tex[mi].resize (std::max (0, M.ntex));
		std::vector<bool> used (tex[mi].size ());
		for (const GxGroup &G : M.groups) if (!(G.usrFlag & 0x2) && G.tex >= 0 && G.tex < (int)used.size ()) used[G.tex] = true;
		bool missingNames = false;
		for (size_t t = 0; t < used.size (); t++) {
			if (!used[t]) continue;
			if (t >= texNames[mi].size ()) { missingNames = true; continue; }
			const std::string &name = texNames[mi][t];
			std::string base = findTex (name);
			if (base.empty ()) { if (name != "0") res.warnings.push_back (M.name + ": texture " + name + " not found"); continue; }
			GxTexSet &ts = tex[mi][t];
			ts.base = job (kBase, base, "");
			std::string n = findTex (Variant (name, "norm")), e = findTex (Variant (name, "emis"));
			std::string r = findTex (Variant (name, "rghn")), m = findTex (Variant (name, "metal"));
			if (!n.empty ()) ts.normal = job (kNormal, n, "");
			if (!e.empty ()) ts.emissive = job (kEmissive, e, "");
			if (!r.empty () || !m.empty ()) { ts.mr = job (kMr, r, m); ts.rough = !r.empty (); ts.metal = !m.empty (); }
		}
		if (missingNames) res.warnings.push_back (M.name + (M.gltf ? ": a glTF mesh, its images are not exported" : ": textures without names in the file, left out"));
	}

	// glow images for lit groups with an emissive colour, a base image and an _emis map (design-gltf-simpose-glow 2)
	std::map<std::tuple<std::string, std::string, float, float, float>, int> glowOf;
	std::map<std::tuple<int, int, int>, int> glowJob;
	for (size_t mi = 0; mi < snap.meshes.size (); mi++) {
		const GxMesh &M = snap.meshes[mi];
		for (const GxGroup &G : M.groups) {
			if (G.vtx.empty () || G.idx.size () < 3 || (G.usrFlag & (0x2 | 0x4 | 0x10))) continue;
			if (G.tex < 0 || G.tex >= (int)tex[mi].size () || G.mtrl < 0 || G.mtrl >= (int)M.mtrls.size ()) continue;
			const GxTexSet &ts = tex[mi][G.tex];
			const float *e = M.mtrls[G.mtrl].emissive;
			if (ts.base < 0 || ts.emissive < 0 || !(e[0] > 0 || e[1] > 0 || e[2] > 0)) continue;
			auto gk = std::make_tuple ((int)mi, G.mtrl, G.tex);
			if (glowJob.count (gk)) continue;
			float c[3];
			for (int i = 0; i < 3; i++) c[i] = std::clamp (e[i], 0.0f, 1.0f);
			auto jk = std::make_tuple (jobs[ts.base].a, jobs[ts.emissive].a, c[0], c[1], c[2]);
			auto it = glowOf.find (jk);
			if (it == glowOf.end ()) {
				Job j;
				j.kind = kGlow; j.a = jobs[ts.base].a; j.b = jobs[ts.emissive].a;
				for (int i = 0; i < 3; i++) j.e[i] = c[i];
				jobs.push_back (j);
				it = glowOf.emplace (jk, (int)jobs.size () - 1).first;
			}
			glowJob[gk] = it->second;
		}
	}

	int nt = std::max (1, std::min ((int)jobs.size (), threads));
	std::atomic<size_t> next{0};
	std::vector<std::thread> pool;
	for (int i = 0; i < nt; i++) pool.emplace_back ([&]() { for (size_t k; (k = next++) < jobs.size ();) Run (jobs[k]); });
	for (auto &t : pool) t.join ();

	// base image hashes per texture slot, before the indices change
	std::vector<std::vector<std::string>> texHash (snap.meshes.size ());
	for (size_t mi = 0; mi < tex.size (); mi++) {
		texHash[mi].resize (tex[mi].size ());
		for (size_t t = 0; t < tex[mi].size (); t++) if (tex[mi][t].base >= 0 && jobs[tex[mi][t].base].ok) texHash[mi][t] = jobs[tex[mi][t].base].hash;
	}

	// failed images drop out; the rest keep their order
	std::vector<int> imgOf (jobs.size (), -1);
	GxGlbInput in;
	for (size_t k = 0; k < jobs.size (); k++) {
		if (!jobs[k].ok) { res.warnings.push_back ("image " + jobs[k].a + (jobs[k].b.empty () ? "" : " / " + jobs[k].b) + ": " + jobs[k].err); continue; }
		imgOf[k] = (int)in.images.size ();
		in.images.push_back (std::move (jobs[k].out));
	}
	for (auto &m : tex) for (GxTexSet &ts : m) {
		auto map = [&](int &i) { if (i >= 0) i = imgOf[i]; };
		map (ts.base); map (ts.normal); map (ts.emissive); map (ts.mr);
		if (ts.mr < 0) ts.rough = ts.metal = false;
	}
	for (const auto &g : glowJob) { // a failed glow, base or _emis image: the _emis map as before
		const GxTexSet &ts = tex[std::get<0> (g.first)][std::get<2> (g.first)];
		if (imgOf[g.second] >= 0 && ts.base >= 0 && ts.emissive >= 0) in.glow[g.first] = imgOf[g.second];
	}

	GxModel model;
	GxBuildModel (snap, labels, model, res.warnings);
	in.snap = &snap;
	in.model = &model;
	in.tex = tex;
	in.mtrlNames = mtrlNames;
	in.texNames = texNames;
	in.texDynamic = texDyn;
	in.texHash = texHash;
	std::vector<uint8_t> glb;
	if (!GxWriteGlb (in, glb, res.error)) return res;

	std::error_code ec;
	fs::path out (outPath);
	if (out.has_parent_path ()) fs::create_directories (out.parent_path (), ec);
	std::string part = outPath + ".part";
	{
		std::ofstream f (part, std::ios::binary | std::ios::trunc);
		if (!f || !f.write ((const char*)glb.data (), glb.size ()) || !(f.close (), f)) { res.error = "cannot write " + part; fs::remove (part, ec); return res; }
	}
	fs::rename (part, out, ec);
	if (ec) { res.error = "cannot rename " + part + ": " + ec.message (); fs::remove (part, ec); return res; }
	for (const GxNode &n : model.nodes) if (n.group >= 0) res.parts++;
	res.anims = (int)model.anims.size ();
	res.images = (int)in.images.size ();
	res.path = outPath;
	res.ok = true;
	return res;
}
