// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// not upstream: glTF 2.0 reader for meshes (claude/design-gltf-loader.md 2)

#include "GltfLoader.h"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <strings.h>

namespace fs = std::filesystem;

bool GltfIsName (const char *name)
{
	if (!name) return false;
	size_t n = strlen (name);
	return (n > 4 && !strcasecmp (name + n - 4, ".glb")) || (n > 5 && !strcasecmp (name + n - 5, ".gltf"));
}

// ----------------------------------------------------------------------------------------------
// image references and base64

std::string GltfMakeImageRef (const GltfImageRef &r)
{
	char buf[160];
	snprintf (buf, sizeof buf, "@gltf:%llu:%llu:%d:%llu:%llu:", r.off, r.len, r.b64 ? 1 : 0, r.sliceOff, r.sliceLen);
	return std::string (buf) + r.file;
}

bool GltfParseImageRef (const char *name, GltfImageRef &r)
{
	if (!name || strncmp (name, "@gltf:", 6)) return false;
	int b64 = 0, used = 0;
	if (sscanf (name + 6, "%llu:%llu:%d:%llu:%llu:%n", &r.off, &r.len, &b64, &r.sliceOff, &r.sliceLen, &used) != 5 || !used) return false;
	r.b64 = (b64 != 0);
	r.file = name + 6 + used;
	return !r.file.empty();
}

bool GltfBase64 (const char *s, size_t n, std::vector<unsigned char> &out)
{
	out.clear ();
	out.reserve (n * 3 / 4);
	unsigned v = 0;
	int bits = 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		int d;
		if (c >= 'A' && c <= 'Z') d = c - 'A';
		else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
		else if (c >= '0' && c <= '9') d = c - '0' + 52;
		else if (c == '+' || c == '-') d = 62;
		else if (c == '/' || c == '_') d = 63;
		else if (c == '=') break;
		else if (c == '\\' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue; // JSON may escape '/' as "\/"
		else return false;
		v = (v << 6) | (unsigned)d;
		bits += 6;
		if (bits >= 8) { bits -= 8; out.push_back ((unsigned char)((v >> bits) & 0xFF)); }
	}
	return true;
}

// ----------------------------------------------------------------------------------------------
// reader state

namespace {

struct Buffer {
	std::vector<unsigned char> data;
	GltfImageRef src;            // where the bytes are, for image references
};

struct Ctx {
	std::string path, dir;
	std::vector<char> file;      // the .glb or .gltf as read
	std::string json;            // raw JSON text
	size_t jsonOff = 0;          // its offset in the file
	QJsonObject root;
	std::vector<Buffer> buffers;
	GltfModel *out = nullptr;             // G0 groups
	std::vector<std::string> *textures = nullptr, *warnings = nullptr;
	std::string err;
	std::set<std::string> warned;
	void Warn (const std::string &w) { if (warned.insert (w).second) warnings->push_back (w); }
	bool Fail (const std::string &e) { if (err.empty()) err = e; return false; }
};

typedef double Mat4[16]; // column-major, as glTF

void MatIdent (Mat4 m) { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.0 : 0.0; }

void MatMul (const Mat4 a, const Mat4 b, Mat4 r) // r = a * b
{
	Mat4 t;
	for (int c = 0; c < 4; c++)
		for (int rr = 0; rr < 4; rr++) {
			double s = 0;
			for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
			t[c * 4 + rr] = s;
		}
	memcpy (r, t, sizeof (Mat4));
}

double Det3 (const Mat4 m)
{
	return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
}

// inverse-transpose of the upper 3x3, row-major n[r][c]
bool NormalMat (const Mat4 m, double n[3][3])
{
	double a[3][3] = { { m[0], m[4], m[8] }, { m[1], m[5], m[9] }, { m[2], m[6], m[10] } };
	double d = Det3 (m);
	if (std::fabs (d) < 1e-30) return false;
	double inv[3][3];
	inv[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) / d;
	inv[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) / d;
	inv[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) / d;
	inv[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) / d;
	inv[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) / d;
	inv[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) / d;
	inv[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) / d;
	inv[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) / d;
	inv[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) / d;
	for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) n[r][c] = inv[c][r];
	return true;
}

} // namespace

// ----------------------------------------------------------------------------------------------
// containers and buffers

namespace {

bool ReadFile (const std::string &p, std::vector<char> &d)
{
	std::ifstream f (p, std::ios::binary);
	if (!f) return false;
	f.seekg (0, std::ios::end);
	std::streamoff n = f.tellg ();
	if (n < 0) return false;
	f.seekg (0);
	d.resize ((size_t)n);
	return n == 0 || (bool)f.read (d.data(), n);
}

unsigned U32 (const char *p) { unsigned v; memcpy (&v, p, 4); return v; }

bool LoadContainer (Ctx &c)
{
	if (!ReadFile (c.path, c.file)) return c.Fail ("can't read the file");
	size_t n = c.file.size ();
	if (n >= 12 && U32 (c.file.data()) == 0x46546C67u) { // "glTF"
		if (U32 (c.file.data() + 4) != 2) return c.Fail ("GLB version is not 2");
		size_t len = U32 (c.file.data() + 8);
		if (len > n || len < 20) return c.Fail ("GLB length is wrong");
		size_t off = 12, chunk = 0;
		while (off + 8 <= len) {
			size_t cl = U32 (c.file.data() + off), ct = U32 (c.file.data() + off + 4);
			if (cl > len - off - 8) return c.Fail ("GLB chunk runs past the file");
			if (chunk == 0) {
				if (ct != 0x4E4F534Au) return c.Fail ("GLB first chunk is not JSON");
				c.json.assign (c.file.data() + off + 8, cl);
				c.jsonOff = off + 8;
			} else if (chunk == 1 && ct == 0x004E4942u) { // BIN
				Buffer b;
				b.data.assign (c.file.begin() + off + 8, c.file.begin() + off + 8 + cl);
				b.src.file = c.path; b.src.off = off + 8; b.src.len = cl;
				c.buffers.push_back (std::move (b)); // provisional: becomes buffer 0 if that buffer has no uri
			}
			off += 8 + ((cl + 3) & ~(size_t)3);
			chunk++;
		}
		if (c.json.empty()) return c.Fail ("GLB has no JSON chunk");
	} else {
		c.json.assign (c.file.data(), n);
		c.jsonOff = 0;
	}
	QJsonParseError pe;
	QJsonDocument doc = QJsonDocument::fromJson (QByteArray (c.json.data(), (int)c.json.size()), &pe);
	if (pe.error != QJsonParseError::NoError || !doc.isObject()) return c.Fail ("JSON: " + pe.errorString().toStdString());
	c.root = doc.object ();
	QString ver = c.root["asset"].toObject()["version"].toString ();
	if (!ver.startsWith ("2.")) return c.Fail ("asset.version is not 2.x");
	return true;
}

// the raw range of a data URI payload in the JSON text, for image references (JSON may write '/' as "\/")
bool FindRaw (Ctx &c, const std::string &payload, size_t &off, size_t &len)
{
	size_t p = c.json.find (payload);
	if (p != std::string::npos) { off = c.jsonOff + p; len = payload.size(); return true; }
	std::string esc;
	for (char ch : payload) { if (ch == '/') esc += '\\'; esc += ch; }
	p = c.json.find (esc);
	if (p != std::string::npos) { off = c.jsonOff + p; len = esc.size(); return true; }
	return false;
}

bool DataUri (Ctx &c, const std::string &uri, std::vector<unsigned char> &data, GltfImageRef &src)
{
	size_t comma = uri.find (',');
	if (comma == std::string::npos || uri.compare (0, 5, "data:") || uri.find (";base64", 0) > comma) return c.Fail ("unsupported data URI");
	std::string payload = uri.substr (comma + 1);
	if (!GltfBase64 (payload.data(), payload.size(), data)) return c.Fail ("bad base64 in a data URI");
	size_t off = 0, len = 0;
	src = GltfImageRef ();
	if (FindRaw (c, payload, off, len)) { src.file = c.path; src.off = off; src.len = len; src.b64 = true; }
	return true;
}

std::string UriPath (Ctx &c, const std::string &uri)
{
	std::string u;
	for (size_t i = 0; i < uri.size(); i++) { // percent-decoding
		if (uri[i] == '%' && i + 2 < uri.size()) { u += (char)strtol (uri.substr (i + 1, 2).c_str(), NULL, 16); i += 2; }
		else u += uri[i];
	}
	return (fs::path (c.dir) / u).lexically_normal().string();
}

bool LoadBuffers (Ctx &c)
{
	std::vector<Buffer> glbBin;
	glbBin.swap (c.buffers);
	QJsonArray bufs = c.root["buffers"].toArray ();
	for (int i = 0; i < bufs.size(); i++) {
		QJsonObject b = bufs[i].toObject ();
		Buffer buf;
		double bl = b["byteLength"].toDouble (-1);
		if (!b.contains ("uri")) {
			if (i != 0 || glbBin.empty()) return c.Fail ("buffer " + std::to_string (i) + " has no data");
			buf = std::move (glbBin[0]);
		} else {
			std::string uri = b["uri"].toString().toStdString();
			if (!uri.compare (0, 5, "data:")) { if (!DataUri (c, uri, buf.data, buf.src)) return false; }
			else {
				std::string p = UriPath (c, uri);
				std::vector<char> d;
				if (!ReadFile (p, d)) return c.Fail ("can't read buffer file " + p);
				buf.data.assign (d.begin(), d.end());
				buf.src.file = p; buf.src.off = 0; buf.src.len = d.size();
			}
		}
		if (bl < 0 || (double)buf.data.size() < bl) return c.Fail ("buffer " + std::to_string (i) + " is shorter than its byteLength");
		c.buffers.push_back (std::move (buf));
	}
	return true;
}

int CompSize (int t)
{
	switch (t) { case 5120: case 5121: return 1; case 5122: case 5123: return 2; case 5125: case 5126: return 4; default: return 0; }
}

int TypeComps (const QString &t)
{
	if (t == "SCALAR") return 1;
	if (t == "VEC2") return 2;
	if (t == "VEC3") return 3;
	if (t == "VEC4") return 4;
	if (t == "MAT2") return 4;
	if (t == "MAT3") return 9;
	if (t == "MAT4") return 16;
	return 0;
}

double ReadComp (const unsigned char *p, int t, bool norm)
{
	switch (t) {
	case 5120: { int8_t v; memcpy (&v, p, 1); return norm ? std::max (v / 127.0, -1.0) : v; }
	case 5121: { uint8_t v = *p; return norm ? v / 255.0 : v; }
	case 5122: { int16_t v; memcpy (&v, p, 2); return norm ? std::max (v / 32767.0, -1.0) : v; }
	case 5123: { uint16_t v; memcpy (&v, p, 2); return norm ? v / 65535.0 : v; }
	case 5125: { uint32_t v; memcpy (&v, p, 4); return v; }
	default: { float v; memcpy (&v, p, 4); return v; }
	}
}

// bytes of a buffer view, checked
bool ViewRange (Ctx &c, int vi, const unsigned char *&p, size_t &len, size_t &stride)
{
	QJsonArray views = c.root["bufferViews"].toArray ();
	if (vi < 0 || vi >= views.size()) return c.Fail ("bufferView index out of range");
	QJsonObject v = views[vi].toObject ();
	int bi = v["buffer"].toInt (-1);
	if (bi < 0 || bi >= (int)c.buffers.size()) return c.Fail ("buffer index out of range");
	double off = v["byteOffset"].toDouble (0), bl = v["byteLength"].toDouble (-1);
	const std::vector<unsigned char> &d = c.buffers[bi].data;
	if (off < 0 || bl < 0 || off + bl > (double)d.size()) return c.Fail ("bufferView " + std::to_string (vi) + " runs past its buffer");
	p = d.data() + (size_t)off;
	len = (size_t)bl;
	stride = (size_t)v["byteStride"].toInt (0);
	return true;
}

// an accessor as doubles, count x comps; normalized integers scaled, sparse values applied
bool ReadAccessor (Ctx &c, int ai, std::vector<double> &out, size_t &count, int &comps)
{
	QJsonArray accs = c.root["accessors"].toArray ();
	if (ai < 0 || ai >= accs.size()) return c.Fail ("accessor index out of range");
	QJsonObject a = accs[ai].toObject ();
	int ct = a["componentType"].toInt (0), cs = CompSize (ct);
	comps = TypeComps (a["type"].toString());
	double cnt = a["count"].toDouble (-1);
	bool norm = a["normalized"].toBool (false);
	if (!cs || !comps || cnt < 0 || cnt > 1e8) return c.Fail ("accessor " + std::to_string (ai) + " is malformed");
	count = (size_t)cnt;
	size_t es = (size_t)cs * comps;
	if (a.contains ("bufferView")) {
		const unsigned char *p; size_t len, stride;
		if (!ViewRange (c, a["bufferView"].toInt (-1), p, len, stride)) return false;
		if (!stride) stride = es;
		double off = a["byteOffset"].toDouble (0);
		if (off < 0 || stride < es || (count && off + (double)stride * (count - 1) + es > (double)len)) return c.Fail ("accessor " + std::to_string (ai) + " runs past its bufferView");
		out.assign (count * comps, 0.0);
		for (size_t i = 0; i < count; i++)
			for (int k = 0; k < comps; k++)
				out[i * comps + k] = ReadComp (p + (size_t)off + i * stride + (size_t)k * cs, ct, norm);
	} else {
		// zero filled; bounded by the loaded data so a bad count can't exhaust memory
		double total = 0;
		for (const auto &b : c.buffers) total += (double)b.data.size();
		if ((double)count * es > total + 1048576.0) return c.Fail ("accessor " + std::to_string (ai) + " is too large for its data");
		out.assign (count * comps, 0.0);
	}
	if (a.contains ("sparse")) {
		QJsonObject s = a["sparse"].toObject (), si = s["indices"].toObject (), sv = s["values"].toObject ();
		double n = s["count"].toDouble (-1);
		int ict = si["componentType"].toInt (0), ics = CompSize (ict);
		if (ict != 5121 && ict != 5123 && ict != 5125) ics = 0;
		const unsigned char *ip, *vp; size_t il, vl, is, vs;
		if (n < 1 || n > (double)count || !ics || !ViewRange (c, si["bufferView"].toInt (-1), ip, il, is) || !ViewRange (c, sv["bufferView"].toInt (-1), vp, vl, vs)) return c.Fail ("accessor " + std::to_string (ai) + ": bad sparse");
		double io = si["byteOffset"].toDouble (0), vo = sv["byteOffset"].toDouble (0);
		size_t ns = (size_t)n;
		if (io < 0 || vo < 0 || io + (double)ns * ics > (double)il || vo + (double)ns * es > (double)vl) return c.Fail ("accessor " + std::to_string (ai) + ": sparse runs past its bufferView");
		for (size_t i = 0; i < ns; i++) {
			double t = ReadComp (ip + (size_t)io + i * ics, ict, false);
			if (t < 0 || t >= (double)count) return c.Fail ("accessor " + std::to_string (ai) + ": sparse index out of range");
			for (int k = 0; k < comps; k++)
				out[(size_t)t * comps + k] = ReadComp (vp + (size_t)vo + i * es + (size_t)k * cs, ct, norm);
		}
	}
	return true;
}

} // namespace

// ----------------------------------------------------------------------------------------------
// images and materials

namespace {

std::string ImageRef (Ctx &c, int ii);

// image of texture ti, -1 if it has none
int TextureImage (Ctx &c, int ti)
{
	QJsonArray texs = c.root["textures"].toArray (), imgs = c.root["images"].toArray ();
	if (ti < 0 || ti >= texs.size()) { c.Warn ("texture index out of range"); return -1; }
	QJsonObject t = texs[ti].toObject ();
	int ii = t["source"].toInt (-1);
	if (ii < 0) { c.Warn ("texture " + std::to_string (ti) + " has no PNG/JPEG source (KTX2/WebP not supported)"); return -1; }
	if (ii >= imgs.size()) { c.Warn ("image index out of range"); return -1; }
	return ii;
}

// image reference of texture ti, "" if it has none
std::string TextureRef (Ctx &c, int ti)
{
	int ii = TextureImage (c, ti);
	return ii < 0 ? "" : ImageRef (c, ii);
}

// image reference of image ii, "" if it can't be referenced
std::string ImageRef (Ctx &c, int ii)
{
	QJsonObject im = c.root["images"].toArray().at (ii).toObject ();
	GltfImageRef r;
	if (im.contains ("bufferView")) {
		QJsonObject v = c.root["bufferViews"].toArray().at (im["bufferView"].toInt (-1)).toObject ();
		int bi = v["buffer"].toInt (-1);
		double off = v["byteOffset"].toDouble (0), bl = v["byteLength"].toDouble (-1);
		if (bi < 0 || bi >= (int)c.buffers.size() || off < 0 || bl <= 0 || off + bl > (double)c.buffers[bi].data.size()) { c.Warn ("image " + std::to_string (ii) + ": bad bufferView"); return ""; }
		const GltfImageRef &s = c.buffers[bi].src;
		if (s.file.empty()) { c.Warn ("image " + std::to_string (ii) + ": its buffer can't be referenced"); return ""; }
		r = s;
		if (s.b64) { r.sliceOff = (unsigned long long)off; r.sliceLen = (unsigned long long)bl; }
		else { r.off = s.off + (unsigned long long)off; r.len = (unsigned long long)bl; r.sliceOff = 0; r.sliceLen = 0; }
	} else {
		std::string uri = im["uri"].toString().toStdString();
		if (uri.empty()) { c.Warn ("image " + std::to_string (ii) + " has no data"); return ""; }
		if (!uri.compare (0, 5, "data:")) {
			std::vector<unsigned char> d;
			std::string e = c.err;
			if (!DataUri (c, uri, d, r) || r.file.empty()) { c.err = e; c.Warn ("image " + std::to_string (ii) + ": bad data URI"); return ""; }
		} else {
			r.file = UriPath (c, uri);
			std::error_code ec;
			auto sz = fs::file_size (r.file, ec);
			if (ec) { c.Warn ("image file not found: " + r.file); return ""; }
			r.off = 0; r.len = sz;
		}
	}
	return GltfMakeImageRef (r);
}

int TextureIndex (Ctx &c, const QJsonObject &info, const char *what)
{
	if (info.isEmpty()) return -1;
	if (info["texCoord"].toInt (0) != 0) c.Warn (std::string (what) + ": texCoord > 0 not supported, UV 0 used");
	if (info["extensions"].toObject().contains ("KHR_texture_transform")) c.Warn (std::string (what) + ": KHR_texture_transform not supported, ignored");
	return info["index"].toInt (-1);
}

std::string F (double v) { char b[32]; snprintf (b, sizeof b, "%.9g", v); return b; }

double Num (const QJsonObject &o, const char *k, double d) { return o.contains (k) ? o[k].toDouble (d) : d; }

void Vec (const QJsonObject &o, const char *k, double *v, int n)
{
	QJsonArray a = o[k].toArray ();
	if (a.size() == n) for (int i = 0; i < n; i++) v[i] = a[i].toDouble (v[i]);
}

int AddTexture (Ctx &c, std::map<std::string, int> &index, const std::string &ref)
{
	auto it = index.find (ref);
	if (it != index.end()) return it->second;
	int n = (int)c.textures->size ();
	c.textures->push_back (ref);
	index[ref] = n;
	return n;
}

GltfMaterial MakeMaterial (Ctx &c, const QJsonObject &m, const std::string &label, std::map<std::string, int> &texIndex)
{
	GltfMaterial g;
	std::string &t = g.pbr;
	QJsonObject pmr = m["pbrMetallicRoughness"].toObject (), ext = m["extensions"].toObject ();
	auto tex = [&](const char *slot, const QJsonObject &info) {
		int ti = TextureIndex (c, info, (label + " " + slot).c_str());
		if (ti < 0) return;
		std::string r = TextureRef (c, ti);
		if (!r.empty()) t += std::string ("PBR_TEX ") + slot + " " + r + "\n";
	};
	double base[4] = { 1, 1, 1, 1 };
	Vec (pmr, "baseColorFactor", base, 4);
	double metal = Num (pmr, "metallicFactor", 1), rough = Num (pmr, "roughnessFactor", 1);
	t += "PBR\n";
	t += "PBR_BASECOLOR " + F (base[0]) + " " + F (base[1]) + " " + F (base[2]) + " " + F (base[3]) + "\n";
	t += "PBR_METALLIC " + F (metal) + "\nPBR_ROUGHNESS " + F (rough) + "\n";
	t += "PBR_NORMAL_GREENUP 1\n";

	int bti = TextureIndex (c, pmr["baseColorTexture"].toObject(), (label + " BASECOLOR").c_str());
	if (bti >= 0) {
		std::string r = TextureRef (c, bti);
		if (!r.empty()) g.tex = AddTexture (c, texIndex, r); // the group's legacy texture, PBR reads it from there
	}
	tex ("METALROUGH", pmr["metallicRoughnessTexture"].toObject());
	QJsonObject nt = m["normalTexture"].toObject ();
	if (!nt.isEmpty()) { t += "PBR_NORMALSCALE " + F (Num (nt, "scale", 1)) + "\n"; tex ("NORMAL", nt); }
	QJsonObject ot = m["occlusionTexture"].toObject ();
	if (!ot.isEmpty()) { t += "PBR_OCCLUSION " + F (Num (ot, "strength", 1)) + "\n"; tex ("OCCLUSION", ot); }
	double em[3] = { 0, 0, 0 };
	Vec (m, "emissiveFactor", em, 3);
	double es = Num (ext["KHR_materials_emissive_strength"].toObject(), "emissiveStrength", 1);
	if (em[0] > 0 || em[1] > 0 || em[2] > 0) t += "PBR_EMISSIVE " + F (em[0]) + " " + F (em[1]) + " " + F (em[2]) + " " + F (es) + "\n";
	tex ("EMISSIVE", m["emissiveTexture"].toObject());
	QString am = m["alphaMode"].toString ("OPAQUE");
	if (am == "MASK") t += "PBR_ALPHA MASK " + F (Num (m, "alphaCutoff", 0.5)) + "\n";
	else if (am == "BLEND") t += "PBR_ALPHA BLEND\n";
	if (m["doubleSided"].toBool (false)) t += "PBR_DOUBLESIDED 1\n";

	if (ext.contains ("KHR_materials_ior")) {
		double ior = Num (ext["KHR_materials_ior"].toObject(), "ior", 1.5);
		if (ior >= 1) t += "PBR_IOR " + F (ior) + "\n";
		else c.Warn (label + ": ior below 1 not supported");
	}
	if (ext.contains ("KHR_materials_specular")) {
		QJsonObject s = ext["KHR_materials_specular"].toObject ();
		double sc[3] = { 1, 1, 1 };
		Vec (s, "specularColorFactor", sc, 3);
		t += "PBR_SPECULAR " + F (Num (s, "specularFactor", 1)) + "\nPBR_SPECULARCOLOR " + F (sc[0]) + " " + F (sc[1]) + " " + F (sc[2]) + "\n";
		tex ("SPECULAR", s["specularTexture"].toObject());
		tex ("SPECULARCOLOR", s["specularColorTexture"].toObject());
	}
	if (ext.contains ("KHR_materials_transmission")) {
		QJsonObject s = ext["KHR_materials_transmission"].toObject ();
		t += "PBR_TRANSMISSION " + F (Num (s, "transmissionFactor", 0)) + "\n";
		tex ("TRANSMISSION", s["transmissionTexture"].toObject());
	}
	if (ext.contains ("KHR_materials_volume")) {
		QJsonObject s = ext["KHR_materials_volume"].toObject ();
		double ac[3] = { 1, 1, 1 };
		Vec (s, "attenuationColor", ac, 3);
		double ad = Num (s, "attenuationDistance", 0);
		if (!(ad > 0) || !std::isfinite (ad)) ad = 0; // 0: infinite
		t += "PBR_VOLUME " + F (Num (s, "thicknessFactor", 0)) + " " + F (ad) + " " + F (ac[0]) + " " + F (ac[1]) + " " + F (ac[2]) + "\n";
		tex ("THICKNESS", s["thicknessTexture"].toObject());
	}
	if (ext.contains ("KHR_materials_clearcoat")) {
		QJsonObject s = ext["KHR_materials_clearcoat"].toObject ();
		QJsonObject cn = s["clearcoatNormalTexture"].toObject ();
		t += "PBR_CLEARCOAT " + F (Num (s, "clearcoatFactor", 0)) + " " + F (Num (s, "clearcoatRoughnessFactor", 0)) + " " + F (Num (cn, "scale", 1)) + "\n";
		tex ("CLEARCOAT", s["clearcoatTexture"].toObject());
		tex ("CLEARCOATROUGH", s["clearcoatRoughnessTexture"].toObject());
		tex ("CLEARCOATNORMAL", cn);
	}
	if (ext.contains ("KHR_materials_sheen")) {
		QJsonObject s = ext["KHR_materials_sheen"].toObject ();
		double sc[3] = { 0, 0, 0 };
		Vec (s, "sheenColorFactor", sc, 3);
		t += "PBR_SHEEN " + F (sc[0]) + " " + F (sc[1]) + " " + F (sc[2]) + " " + F (Num (s, "sheenRoughnessFactor", 0)) + "\n";
		tex ("SHEENCOLOR", s["sheenColorTexture"].toObject());
		tex ("SHEENROUGH", s["sheenRoughnessTexture"].toObject());
	}
	if (ext.contains ("KHR_materials_anisotropy")) {
		QJsonObject s = ext["KHR_materials_anisotropy"].toObject ();
		t += "PBR_ANISOTROPY " + F (Num (s, "anisotropyStrength", 0)) + " " + F (Num (s, "anisotropyRotation", 0)) + "\n";
		tex ("ANISOTROPY", s["anisotropyTexture"].toObject());
	}
	if (ext.contains ("KHR_materials_iridescence")) {
		QJsonObject s = ext["KHR_materials_iridescence"].toObject ();
		double ior = Num (s, "iridescenceIor", 1.3);
		if (ior < 1) { c.Warn (label + ": iridescence ior below 1 not supported, 1 used"); ior = 1; }
		t += "PBR_IRIDESCENCE " + F (Num (s, "iridescenceFactor", 0)) + " " + F (ior) + " " + F (Num (s, "iridescenceThicknessMinimum", 100)) + " " + F (Num (s, "iridescenceThicknessMaximum", 400)) + "\n";
		tex ("IRIDESCENCE", s["iridescenceTexture"].toObject());
		tex ("IRIDESCENCETHICK", s["iridescenceThicknessTexture"].toObject());
	}

	memset (&g.mat, 0, sizeof g.mat);
	float alpha = (am == "MASK" || am == "BLEND") ? (float)base[3] : 1.0f; // OPAQUE ignores alpha
	g.mat.diffuse = { (float)base[0], (float)base[1], (float)base[2], alpha };
	g.mat.ambient = { (float)base[0], (float)base[1], (float)base[2], alpha };
	g.mat.specular = { 0.0f, 0.0f, 0.0f, 1.0f };
	g.mat.emissive = { (float)std::min (1.0, em[0] * es), (float)std::min (1.0, em[1] * es), (float)std::min (1.0, em[2] * es), 1.0f };
	g.mat.power = 0.0f;
	return g;
}

} // namespace

// ----------------------------------------------------------------------------------------------
// geometry

namespace {

void LocalMatrix (const QJsonObject &n, Mat4 m)
{
	MatIdent (m);
	QJsonArray mm = n["matrix"].toArray ();
	if (mm.size() == 16) { for (int i = 0; i < 16; i++) m[i] = mm[i].toDouble (m[i]); return; }
	double t[3] = { 0, 0, 0 }, r[4] = { 0, 0, 0, 1 }, s[3] = { 1, 1, 1 };
	Vec (n, "translation", t, 3);
	Vec (n, "rotation", r, 4);
	Vec (n, "scale", s, 3);
	double x = r[0], y = r[1], z = r[2], w = r[3];
	double q = std::sqrt (x * x + y * y + z * z + w * w);
	if (q > 0) { x /= q; y /= q; z /= q; w /= q; }
	double R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
	                2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
	                2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y) }; // column-major columns
	for (int col = 0; col < 3; col++)
		for (int row = 0; row < 3; row++) m[col * 4 + row] = R[col * 3 + row] * s[col];
	m[12] = t[0]; m[13] = t[1]; m[14] = t[2];
}

// a primitive's triangles and attributes in node space; prim.tri stays empty when there is nothing to draw
bool ReadPrim (Ctx &c, const QJsonObject &p, const std::string &label, GltfPrim &prim)
{
	prim = GltfPrim ();
	int mode = p["mode"].toInt (4);
	if (mode < 4 || mode > 6) { c.Warn (label + ": points and lines are not drawn"); return true; }
	QJsonObject at = p["attributes"].toObject ();
	if (!at.contains ("POSITION")) { c.Warn (label + ": no POSITION, skipped"); return true; }
	std::vector<double> pos, nrm, uv, ind;
	size_t np = 0, nn = 0, nu = 0, ni = 0;
	int cp = 0, cn = 0, cu = 0, ci = 0;
	if (!ReadAccessor (c, at["POSITION"].toInt (-1), pos, np, cp)) return false;
	if (cp != 3) return c.Fail (label + ": POSITION is not VEC3");
	bool hasN = at.contains ("NORMAL");
	if (hasN && (!ReadAccessor (c, at["NORMAL"].toInt (-1), nrm, nn, cn) || cn != 3 || nn != np)) return c.Fail (label + ": bad NORMAL");
	bool hasUV = at.contains ("TEXCOORD_0");
	if (hasUV && (!ReadAccessor (c, at["TEXCOORD_0"].toInt (-1), uv, nu, cu) || cu != 2 || nu != np)) return c.Fail (label + ": bad TEXCOORD_0");
	std::vector<uint32_t> idx;
	if (p.contains ("indices")) {
		int ia = p["indices"].toInt (-1);
		if (!ReadAccessor (c, ia, ind, ni, ci) || ci != 1) return c.Fail (label + ": bad indices");
		QJsonObject ao = c.root["accessors"].toArray ().at (ia).toObject ();
		int ict = ao["componentType"].toInt (0);
		if ((ict != 5121 && ict != 5123 && ict != 5125) || ao["normalized"].toBool (false)) return c.Fail (label + ": indices are not unsigned integers");
		idx.resize (ni);
		for (size_t i = 0; i < ni; i++) idx[i] = (uint32_t)ind[i];
	} else {
		idx.resize (np);
		for (size_t i = 0; i < np; i++) idx[i] = (uint32_t)i;
	}
	std::vector<uint32_t> tri; // triangle list
	if (mode == 4) tri.assign (idx.begin(), idx.begin() + (idx.size() / 3) * 3);
	else if (mode == 5) {
		for (size_t i = 0; i + 2 < idx.size(); i++) {
			tri.push_back (idx[i]);
			tri.push_back (idx[i + 1 + i % 2]);
			tri.push_back (idx[i + 2 - i % 2]);
		}
	} else {
		for (size_t i = 1; i + 1 < idx.size(); i++) { tri.push_back (idx[i]); tri.push_back (idx[i + 1]); tri.push_back (idx[0]); }
	}
	size_t bad = 0;
	std::vector<uint32_t> ok;
	ok.reserve (tri.size());
	for (size_t i = 0; i < tri.size(); i += 3) {
		if (tri[i] >= np || tri[i + 1] >= np || tri[i + 2] >= np) { bad++; continue; }
		ok.insert (ok.end(), tri.begin() + i, tri.begin() + i + 3);
	}
	if (bad) c.Warn (label + ": " + std::to_string (bad) + " triangles with an index past the vertices dropped");
	if (ok.empty()) return true;
	if (p.contains ("material")) {
		prim.material = p["material"].toInt (0);
		if (prim.material < 0) return c.Fail (label + ": material index out of range");
	}
	prim.pos.swap (pos);
	if (hasN) prim.nrm.swap (nrm);
	if (hasUV) prim.uv.swap (uv);
	prim.tri.swap (ok);
	return true;
}

// one primitive in world space, Orbiter axes, into groups of at most 65535 vertices
bool AddPrimitive (Ctx &c, const QJsonObject &p, const Mat4 world, DWORD defMtrl, const std::vector<int> &mtrlTex, const std::string &label)
{
	GltfPrim pr;
	if (!ReadPrim (c, p, label, pr)) return false;
	if (pr.tri.empty()) return true;
	const std::vector<double> &pos = pr.pos, &nrm = pr.nrm, &uv = pr.uv;
	const std::vector<uint32_t> &ok = pr.tri;
	bool hasN = !nrm.empty(), hasUV = !uv.empty();

	double nm[3][3];
	if (!NormalMat (world, nm)) { c.Warn (label + ": node scale 0, skipped"); return true; }
	bool reverse = Det3 (world) >= 0; // glTF fronts are counterclockwise for a positive determinant; Orbiter's mirror (z) flips them once more

	// corners: one vertex per index when normals are missing (flat), else shared
	struct Corner { double p[3], n[3], uv[2]; };
	auto corner = [&](uint32_t v, const double *fn) {
		Corner k;
		const double *s = &pos[v * 3];
		for (int r = 0; r < 3; r++) k.p[r] = world[r] * s[0] + world[4 + r] * s[1] + world[8 + r] * s[2] + world[12 + r];
		const double *n0 = fn ? fn : &nrm[v * 3];
		double l = 0;
		for (int r = 0; r < 3; r++) { k.n[r] = nm[r][0] * n0[0] + nm[r][1] * n0[1] + nm[r][2] * n0[2]; l += k.n[r] * k.n[r]; }
		l = l > 0 ? 1.0 / std::sqrt (l) : 0.0;
		for (int r = 0; r < 3; r++) k.n[r] *= l;
		k.uv[0] = hasUV ? uv[v * 2] : 0.0;
		k.uv[1] = hasUV ? uv[v * 2 + 1] : 0.0;
		return k;
	};
	auto vertex = [](const Corner &k) {
		NTVERTEX v;
		v.x = (float)k.p[0]; v.y = (float)k.p[1]; v.z = (float)-k.p[2];
		v.nx = (float)k.n[0]; v.ny = (float)k.n[1]; v.nz = (float)-k.n[2];
		v.tu = (float)k.uv[0]; v.tv = (float)k.uv[1];
		return v;
	};

	GltfGroup g;
	g.mtrl = pr.material >= 0 ? (DWORD)pr.material : defMtrl;
	if (g.mtrl >= mtrlTex.size()) return c.Fail (label + ": material index out of range");
	g.tex = mtrlTex[g.mtrl];
	std::map<uint32_t, WORD> remap;
	auto flush = [&]() { if (!g.idx.empty()) c.out->groups.push_back (g); g.vtx.clear(); g.idx.clear(); remap.clear(); };
	for (size_t i = 0; i < ok.size(); i += 3) {
		uint32_t t3[3] = { ok[i], ok[i + 1], ok[i + 2] };
		if (reverse) std::swap (t3[1], t3[2]);
		if (hasN) {
			int fresh = 0;
			for (int k = 0; k < 3; k++) if (!remap.count (t3[k])) fresh++;
			if (g.vtx.size() + fresh > 65535) flush ();
			for (int k = 0; k < 3; k++) {
				auto it = remap.find (t3[k]);
				if (it == remap.end()) {
					it = remap.emplace (t3[k], (WORD)g.vtx.size()).first;
					g.vtx.push_back (vertex (corner (t3[k], nullptr)));
				}
				g.idx.push_back (it->second);
			}
		} else {
			const double *a = &pos[ok[i] * 3], *b = &pos[ok[i + 1] * 3], *d = &pos[ok[i + 2] * 3];
			double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
			double fn[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] }; // counterclockwise front
			if (g.vtx.size() + 3 > 65535) flush ();
			for (int k = 0; k < 3; k++) {
				g.idx.push_back ((WORD)g.vtx.size());
				g.vtx.push_back (vertex (corner (t3[k], fn)));
			}
		}
	}
	flush ();
	return true;
}

bool WalkNode (Ctx &c, int ni, const Mat4 parent, std::vector<char> &state, DWORD defMtrl, const std::vector<int> &mtrlTex, int depth = 0)
{
	QJsonArray nodes = c.root["nodes"].toArray ();
	if (ni < 0 || ni >= nodes.size()) return c.Fail ("node index out of range");
	if (depth > 256) return c.Fail ("node hierarchy is too deep");
	if (state[ni] == 1) return c.Fail ("node hierarchy has a cycle");
	if (state[ni] == 2) { c.Warn ("node " + std::to_string (ni) + " is used twice, drawn once"); return true; }
	state[ni] = 1;
	QJsonObject n = nodes[ni].toObject ();
	Mat4 local, world;
	LocalMatrix (n, local);
	MatMul (parent, local, world);
	if (n.contains ("skin")) c.Warn ("skins are not supported, rest pose drawn");
	if (n.contains ("mesh")) {
		QJsonArray meshes = c.root["meshes"].toArray ();
		int mi = n["mesh"].toInt (-1);
		if (mi < 0 || mi >= meshes.size()) return c.Fail ("mesh index out of range");
		QJsonObject m = meshes[mi].toObject ();
		std::string name = n["name"].toString (m["name"].toString (QString ("node %1").arg (ni))).toStdString();
		QJsonArray prims = m["primitives"].toArray ();
		for (int k = 0; k < prims.size(); k++) {
			QJsonObject p = prims[k].toObject ();
			if (p.contains ("targets")) c.Warn ("morph targets are not supported, base shape drawn");
			if (!AddPrimitive (c, p, world, defMtrl, mtrlTex, name)) return false;
		}
	}
	QJsonArray ch = n["children"].toArray ();
	for (int k = 0; k < ch.size(); k++) if (!WalkNode (c, ch[k].toInt (-1), world, state, defMtrl, mtrlTex, depth + 1)) return false;
	state[ni] = 2;
	return true;
}

} // namespace

// ----------------------------------------------------------------------------------------------

static bool ReadModel (const std::string &path, GltfModel &out, std::string &err)
{
	static const char *Supported[] = { "KHR_materials_emissive_strength", "KHR_materials_ior", "KHR_materials_specular", "KHR_materials_transmission",
		"KHR_materials_volume", "KHR_materials_clearcoat", "KHR_materials_sheen", "KHR_materials_anisotropy", "KHR_materials_iridescence" };
	out = GltfModel ();
	Ctx c;
	c.out = &out;
	c.textures = &out.textures;
	c.warnings = &out.warnings;
	std::error_code ec;
	c.path = fs::absolute (fs::path (path), ec).lexically_normal().string();
	if (ec) c.path = path;
	c.dir = fs::path (c.path).parent_path().string();
	auto fail = [&]() { err = c.err.empty() ? "unknown error" : c.err; out = GltfModel (); return false; };
	if (!LoadContainer (c)) return fail ();
	for (const auto &v : c.root["extensionsRequired"].toArray()) {
		std::string e = v.toString().toStdString();
		if (std::find_if (std::begin (Supported), std::end (Supported), [&](const char *s) { return e == s; }) == std::end (Supported)) { c.Fail ("required extension " + e + " is not supported"); return fail (); }
	}
	for (const auto &v : c.root["extensionsUsed"].toArray()) {
		std::string e = v.toString().toStdString();
		if (std::find_if (std::begin (Supported), std::end (Supported), [&](const char *s) { return e == s; }) == std::end (Supported)) c.Warn ("extension " + e + " is not supported, ignored");
	}
	if (!LoadBuffers (c)) return fail ();
	if (c.root.contains ("animations") && !c.root["animations"].toArray().isEmpty()) c.Warn ("animations are not supported, ignored");

	// materials; the glTF default material last, only if a primitive has none (GC MATERIAL n stays glTF material n)
	std::map<std::string, int> texIndex;
	QJsonArray mats = c.root["materials"].toArray ();
	std::vector<int> mtrlTex;
	for (int i = 0; i < mats.size(); i++) {
		QJsonObject m = mats[i].toObject ();
		out.materials.push_back (MakeMaterial (c, m, "material " + std::to_string (i) + " (" + m["name"].toString().toStdString() + ")", texIndex));
		mtrlTex.push_back (out.materials.back().tex);
	}
	bool needDefault = false;
	for (const auto &mv : c.root["meshes"].toArray())
		for (const auto &pv : mv.toObject()["primitives"].toArray())
			if (!pv.toObject().contains ("material")) needDefault = true;
	DWORD defMtrl = (DWORD)out.materials.size ();
	if (needDefault) {
		out.materials.push_back (MakeMaterial (c, QJsonObject (), "default material", texIndex));
		mtrlTex.push_back (-1);
	}
	if (!c.err.empty()) return fail ();

	// scene
	QJsonArray scenes = c.root["scenes"].toArray (), nodes = c.root["nodes"].toArray ();
	std::vector<char> state (nodes.size(), 0);
	Mat4 id;
	MatIdent (id);
	if (scenes.isEmpty()) {
		c.Warn ("no scene, every root node drawn");
		std::vector<char> child (nodes.size(), 0);
		for (const auto &nv : nodes) for (const auto &cv : nv.toObject()["children"].toArray()) { int k = cv.toInt (-1); if (k >= 0 && k < (int)child.size()) child[k] = 1; }
		for (int i = 0; i < nodes.size(); i++) if (!child[i] && !WalkNode (c, i, id, state, defMtrl, mtrlTex)) return fail ();
	} else {
		int si = c.root["scene"].toInt (0);
		if (si < 0 || si >= scenes.size()) { c.Fail ("scene index out of range"); return fail (); }
		for (const auto &nv : scenes[si].toObject()["nodes"].toArray())
			if (!WalkNode (c, nv.toInt (-1), id, state, defMtrl, mtrlTex)) return fail ();
	}
	if (out.groups.empty()) { c.Fail ("no triangles in the default scene"); return fail (); }
	return true;
}

bool GltfRead (const std::string &path, GltfModel &out, std::string &err)
{
	try { return ReadModel (path, out, err); }
	catch (const std::bad_alloc &) { out = GltfModel (); err = "out of memory"; return false; }
}

// ----------------------------------------------------------------------------------------------
// not upstream: the scene for the round trip (claude/design-gltf-roundtrip.md 6)

namespace {

std::string JsonText (const QJsonValue &v)
{
	if (v.isObject()) return QJsonDocument (v.toObject()).toJson (QJsonDocument::Compact).toStdString();
	return "";
}

bool ReadSampler (Ctx &c, const QJsonObject &s, GltfSamplerSrc &out, const std::string &label)
{
	std::vector<double> in, val;
	size_t ni = 0, nv = 0;
	int ci = 0, cv = 0;
	if (!ReadAccessor (c, s["input"].toInt (-1), in, ni, ci) || ci != 1) return c.Fail (label + ": bad input");
	if (!ReadAccessor (c, s["output"].toInt (-1), val, nv, cv)) return c.Fail (label + ": bad output");
	QString ip = s["interpolation"].toString ("LINEAR");
	out.interp = ip == "STEP" ? GLTF_STEP : ip == "CUBICSPLINE" ? GLTF_CUBIC : GLTF_LINEAR;
	if (ip != "LINEAR" && ip != "STEP" && ip != "CUBICSPLINE") c.Warn (label + ": interpolation " + ip.toStdString() + " read as LINEAR");
	if (nv != ni * (out.interp == GLTF_CUBIC ? 3 : 1)) return c.Fail (label + ": output count doesn't match its input");
	for (size_t i = 1; i < ni; i++) if (!(in[i] > in[i - 1])) return c.Fail (label + ": key times not increasing");
	out.comps = cv;
	out.in.swap (in);
	out.out.swap (val);
	return true;
}

bool ReadScene (const std::string &path, GltfScene &out, std::string &err)
{
	out = GltfScene ();
	Ctx c;
	GltfModel unused;
	c.out = &unused;
	c.textures = &out.textures;
	c.warnings = &out.warnings;
	std::error_code ec;
	c.path = fs::absolute (fs::path (path), ec).lexically_normal().string();
	if (ec) c.path = path;
	c.dir = fs::path (c.path).parent_path().string();
	auto fail = [&]() { err = c.err.empty() ? "unknown error" : c.err; out = GltfScene (); return false; };
	if (!LoadContainer (c)) return fail ();
	for (const auto &v : c.root["extensionsRequired"].toArray()) {
		std::string e = v.toString().toStdString();
		if (e != "KHR_materials_unlit" && e.compare (0, 14, "KHR_materials_")) { c.Fail ("required extension " + e + " is not supported"); return fail (); }
	}
	if (!LoadBuffers (c)) return fail ();

	QJsonArray imgs = c.root["images"].toArray ();
	for (int i = 0; i < imgs.size(); i++) out.images.push_back (ImageRef (c, i));
	std::map<std::string, int> texIndex;
	QJsonArray mats = c.root["materials"].toArray ();
	for (int i = 0; i < mats.size(); i++) {
		QJsonObject m = mats[i].toObject ();
		out.materials.push_back (MakeMaterial (c, m, "material " + std::to_string (i) + " (" + m["name"].toString().toStdString() + ")", texIndex));
		out.materialJson.push_back (JsonText (m));
		int ti = m["pbrMetallicRoughness"].toObject()["baseColorTexture"].toObject()["index"].toInt (-1);
		out.materialImage.push_back (ti >= 0 ? TextureImage (c, ti) : -1);
	}
	out.materials.push_back (MakeMaterial (c, QJsonObject (), "default material", texIndex));
	if (!c.err.empty()) return fail ();

	QJsonArray meshes = c.root["meshes"].toArray ();
	for (int mi = 0; mi < meshes.size(); mi++) {
		QJsonObject m = meshes[mi].toObject ();
		std::string label = "mesh " + std::to_string (mi) + " (" + m["name"].toString().toStdString() + ")";
		std::vector<GltfPrim> prims;
		QJsonArray pa = m["primitives"].toArray ();
		for (int k = 0; k < pa.size(); k++) {
			GltfPrim pr;
			if (!ReadPrim (c, pa[k].toObject(), label, pr)) return fail ();
			if (pr.material >= mats.size()) { c.Fail (label + ": material index out of range"); return fail (); }
			prims.push_back (std::move (pr));
		}
		out.meshes.push_back (std::move (prims));
	}

	QJsonArray nodes = c.root["nodes"].toArray ();
	out.nodes.resize (nodes.size());
	for (int i = 0; i < nodes.size(); i++) {
		QJsonObject n = nodes[i].toObject ();
		GltfNodeSrc &d = out.nodes[i];
		d.name = n["name"].toString().toStdString();
		d.mesh = n.contains ("mesh") ? n["mesh"].toInt (-1) : -1;
		if (d.mesh >= meshes.size()) { c.Fail ("mesh index out of range"); return fail (); }
		QJsonArray mm = n["matrix"].toArray ();
		if (mm.size() == 16) { d.hasMatrix = true; for (int k = 0; k < 16; k++) d.matrix[k] = mm[k].toDouble (d.matrix[k]); }
		Vec (n, "translation", d.t, 3);
		Vec (n, "rotation", d.r, 4);
		Vec (n, "scale", d.s, 3);
		d.extras = JsonText (n["extras"]);
		for (const auto &cv : n["children"].toArray()) {
			int k = cv.toInt (-1);
			if (k < 0 || k >= nodes.size() || k == i) { c.Fail ("node index out of range"); return fail (); }
			d.children.push_back (k);
		}
		if (n.contains ("skin")) c.Warn ("skins are not supported, rest pose used");
	}
	for (int i = 0; i < (int)out.nodes.size(); i++)
		for (int k : out.nodes[i].children) {
			if (out.nodes[k].parent >= 0) { c.Fail ("node " + std::to_string (k) + " has two parents"); return fail (); }
			out.nodes[k].parent = i;
		}
	for (int i = 0; i < (int)out.nodes.size(); i++) { // cycles
		int d = 0;
		for (int k = out.nodes[i].parent; k >= 0; k = out.nodes[k].parent)
			if (++d > 256) { c.Fail ("node hierarchy is too deep or has a cycle"); return fail (); }
	}

	QJsonArray scenes = c.root["scenes"].toArray ();
	if (scenes.isEmpty()) {
		for (int i = 0; i < (int)out.nodes.size(); i++) if (out.nodes[i].parent < 0) out.roots.push_back (i);
	} else {
		int si = c.root["scene"].toInt (0);
		if (si < 0 || si >= scenes.size()) { c.Fail ("scene index out of range"); return fail (); }
		QJsonObject sc = scenes[si].toObject ();
		for (const auto &nv : sc["nodes"].toArray()) {
			int k = nv.toInt (-1);
			if (k < 0 || k >= (int)out.nodes.size() || out.nodes[k].parent >= 0) { c.Fail ("scene node index out of range"); return fail (); }
			out.roots.push_back (k);
		}
		out.sceneExtras = JsonText (sc["extras"]);
	}

	QJsonArray anims = c.root["animations"].toArray ();
	for (int ai = 0; ai < anims.size(); ai++) {
		QJsonObject a = anims[ai].toObject ();
		GltfAnimSrc an;
		an.name = a["name"].toString().toStdString();
		std::string label = "animation " + std::to_string (ai) + " (" + an.name + ")";
		QJsonArray sa = a["samplers"].toArray ();
		for (int k = 0; k < sa.size(); k++) {
			GltfSamplerSrc sm;
			if (!ReadSampler (c, sa[k].toObject(), sm, label + " sampler " + std::to_string (k))) return fail ();
			an.samplers.push_back (std::move (sm));
		}
		for (const auto &cv : a["channels"].toArray()) {
			QJsonObject ch = cv.toObject (), tg = ch["target"].toObject ();
			QString path = tg["path"].toString ();
			GltfChannelSrc d;
			d.node = tg.contains ("node") ? tg["node"].toInt (-1) : -1;
			d.sampler = ch["sampler"].toInt (-1);
			if (path == "translation") d.path = GLTF_TRANSLATION;
			else if (path == "rotation") d.path = GLTF_ROTATION;
			else if (path == "scale") d.path = GLTF_SCALE;
			else { c.Warn (label + ": " + path.toStdString() + " channels are not supported, ignored"); continue; }
			if (d.node < 0 || d.node >= (int)out.nodes.size() || d.sampler < 0 || d.sampler >= (int)an.samplers.size()) { c.Fail (label + ": bad channel"); return fail (); }
			if (an.samplers[d.sampler].comps != (d.path == GLTF_ROTATION ? 4 : 3)) { c.Fail (label + ": channel output has the wrong type"); return fail (); }
			an.channels.push_back (d);
		}
		out.anims.push_back (std::move (an));
	}
	return true;
}

} // namespace

bool GltfReadScene (const std::string &path, GltfScene &out, std::string &err)
{
	try { return ReadScene (path, out, err); }
	catch (const std::bad_alloc &) { out = GltfScene (); err = "out of memory"; return false; }
}

bool GltfReadJson (const std::string &path, std::string &json, std::string &err)
{
	json.clear ();
	std::ifstream f (path, std::ios::binary);
	if (!f) { err = "can't read the file"; return false; }
	char h[20];
	if (f.read (h, 12) && U32 (h) == 0x46546C67u) {
		if (!f.read (h + 12, 8) || U32 (h + 16) != 0x4E4F534Au) { err = "GLB first chunk is not JSON"; return false; }
		size_t len = U32 (h + 12);
		if (len > 256u * 1024 * 1024) { err = "GLB JSON chunk is too large"; return false; }
		json.resize (len);
		if (len && !f.read (&json[0], len)) { err = "GLB JSON chunk runs past the file"; json.clear (); return false; }
		return true;
	}
	f.clear ();
	f.seekg (0, std::ios::end);
	std::streamoff n = f.tellg ();
	if (n < 0 || n > 256ll * 1024 * 1024) { err = "file is too large"; return false; }
	f.seekg (0);
	json.resize ((size_t)n);
	if (n && !f.read (&json[0], n)) { err = "can't read the file"; json.clear (); return false; }
	return true;
}

bool GltfImageBytes (const std::string &ref, std::vector<unsigned char> &out)
{
	out.clear ();
	GltfImageRef r;
	if (!GltfParseImageRef (ref.c_str(), r)) return false;
	std::ifstream f (r.file, std::ios::binary);
	if (!f) return false;
	std::vector<char> raw ((size_t)r.len);
	f.seekg ((std::streamoff)r.off);
	if (r.len && !f.read (raw.data(), (std::streamsize)r.len)) return false;
	std::vector<unsigned char> d;
	if (r.b64) { if (!GltfBase64 (raw.data(), raw.size(), d)) return false; }
	else d.assign (raw.begin(), raw.end());
	if (r.sliceOff > d.size() || (r.sliceLen && r.sliceOff + r.sliceLen > d.size())) return false;
	size_t end = r.sliceLen ? (size_t)(r.sliceOff + r.sliceLen) : d.size();
	out.assign (d.begin() + (size_t)r.sliceOff, d.begin() + end);
	return true;
}
