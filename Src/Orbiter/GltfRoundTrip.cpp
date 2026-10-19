// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// not upstream: a .glb made from an Orbiter vessel and edited in Blender, read back (claude/design-gltf-roundtrip.md)

#include "GltfRoundTrip.h"
#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <strings.h>

namespace {

const double kPi = 3.14159265358979323846;
const double kTolRad = 1e-5, kTolM = 1e-5, kTolScale = 1e-5;

// ----------------------------------------------------------------------------------------------
// JSON

QJsonObject Obj (const std::string &json)
{
	if (json.empty()) return QJsonObject ();
	QJsonDocument d = QJsonDocument::fromJson (QByteArray (json.data(), (int)json.size()));
	return d.isObject() ? d.object() : QJsonObject ();
}

RtVec V3 (const QJsonValue &v, RtVec d = RtVec ())
{
	QJsonArray a = v.toArray ();
	if (a.size() == 3) { d.x = a[0].toDouble (d.x); d.y = a[1].toDouble (d.y); d.z = a[2].toDouble (d.z); }
	return d;
}

COLOUR4 C4 (const QJsonValue &v)
{
	QJsonArray a = v.toArray ();
	COLOUR4 c = { 0, 0, 0, 1 };
	if (a.size() == 4) { c.r = (float)a[0].toDouble (); c.g = (float)a[1].toDouble (); c.b = (float)a[2].toDouble (); c.a = (float)a[3].toDouble (1); }
	return c;
}

int TypeOf (const QString &t)
{
	if (t == "rotate") return RT_ROTATE;
	if (t == "translate") return RT_TRANSLATE;
	if (t == "scale") return RT_SCALE;
	return RT_NULL;
}

// ----------------------------------------------------------------------------------------------
// 3D math, glTF matrices column-major 4x4

typedef std::vector<double> M4;

M4 Ident () { M4 m (16, 0.0); m[0] = m[5] = m[10] = m[15] = 1; return m; }

M4 Mul (const M4 &a, const M4 &b)
{
	M4 r (16, 0.0);
	for (int c = 0; c < 4; c++)
		for (int rr = 0; rr < 4; rr++) {
			double s = 0;
			for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
			r[c * 4 + rr] = s;
		}
	return r;
}

void QuatMat (const double q[4], double R[3][3]) // glTF (x, y, z, w), column vectors, row-major R
{
	double x = q[0], y = q[1], z = q[2], w = q[3];
	double n = std::sqrt (x * x + y * y + z * z + w * w);
	if (n > 0) { x /= n; y /= n; z /= n; w /= n; } else w = 1;
	R[0][0] = 1 - 2 * (y * y + z * z); R[0][1] = 2 * (x * y - z * w);     R[0][2] = 2 * (x * z + y * w);
	R[1][0] = 2 * (x * y + z * w);     R[1][1] = 1 - 2 * (x * x + z * z); R[1][2] = 2 * (y * z - x * w);
	R[2][0] = 2 * (x * z - y * w);     R[2][1] = 2 * (y * z + x * w);     R[2][2] = 1 - 2 * (x * x + y * y);
}

M4 Trs (const double t[3], const double q[4], const double s[3])
{
	double R[3][3];
	QuatMat (q, R);
	M4 m = Ident ();
	for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) m[c * 4 + r] = R[r][c] * s[c];
	m[12] = t[0]; m[13] = t[1]; m[14] = t[2];
	return m;
}

void Lin (const M4 &m, double L[3][3]) { for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) L[r][c] = m[c * 4 + r]; }

void MatMul3 (const double a[3][3], const double b[3][3], double r[3][3])
{
	double t[3][3];
	for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
	memcpy (r, t, sizeof t);
}

bool Inv3 (const double a[3][3], double r[3][3])
{
	double d = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
	if (std::fabs (d) < 1e-30) return false;
	r[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) / d; r[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) / d; r[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) / d;
	r[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) / d; r[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) / d; r[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) / d;
	r[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) / d; r[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) / d; r[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) / d;
	return true;
}

double Det3 (const double a[3][3])
{
	return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
}

// a matrix's columns orthogonal and of one length (a rotation, maybe mirrored, times a uniform scale)
bool Conformal (const double L[3][3], double tol = 1e-6)
{
	double c[3][3];
	for (int i = 0; i < 3; i++) for (int r = 0; r < 3; r++) c[i][r] = L[r][i];
	double l0 = std::sqrt (c[0][0] * c[0][0] + c[0][1] * c[0][1] + c[0][2] * c[0][2]);
	if (l0 < 1e-12) return false;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			double d = c[i][0] * c[j][0] + c[i][1] * c[j][1] + c[i][2] * c[j][2];
			if (std::fabs (d - (i == j ? l0 * l0 : 0.0)) > tol * l0 * l0) return false;
		}
	return true;
}

// Orbiter (x, y, z) = glTF (x, y, -z)
RtVec Mir (double x, double y, double z) { return RtVec{ x, y, -z }; }
void MirM (const double a[3][3], double r[3][3]) // M a M
{
	static const double s[3] = { 1, 1, -1 };
	for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = s[i] * a[i][j] * s[j];
}

// quaternions (w, x, y, z), Orbiter frame
struct Q4 { double w = 1, x = 0, y = 0, z = 0; };
Q4 QMul (const Q4 &a, const Q4 &b)
{
	return { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
}
Q4 QConj (const Q4 &a) { return { a.w, -a.x, -a.y, -a.z }; }
Q4 QAxis (const RtVec &u, double ang) { double s = std::sin (ang / 2); return { std::cos (ang / 2), u.x * s, u.y * s, u.z * s }; }
double QAngle (const Q4 &a) // rotation angle of a, 0..pi
{
	double v = std::sqrt (a.x * a.x + a.y * a.y + a.z * a.z);
	return 2 * std::atan2 (v, std::fabs (a.w));
}
void QAxisAngle (const Q4 &a, RtVec &u, double &ang) // ang in 0..pi
{
	Q4 q = a.w < 0 ? Q4{ -a.w, -a.x, -a.y, -a.z } : a;
	double v = std::sqrt (q.x * q.x + q.y * q.y + q.z * q.z);
	ang = 2 * std::atan2 (v, q.w);
	if (v > 1e-15) u = { q.x / v, q.y / v, q.z / v }; else u = { 1, 0, 0 };
}
Q4 QFromMat (const double m[3][3]) // proper rotation
{
	Q4 q;
	double tr = m[0][0] + m[1][1] + m[2][2];
	if (tr > 0) {
		double s = std::sqrt (tr + 1.0) * 2;
		q = { 0.25 * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s };
	} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
		double s = std::sqrt (1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
		q = { (m[2][1] - m[1][2]) / s, 0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s };
	} else if (m[1][1] > m[2][2]) {
		double s = std::sqrt (1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
		q = { (m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s };
	} else {
		double s = std::sqrt (1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
		q = { (m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s };
	}
	double n = std::sqrt (q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
	return { q.w / n, q.x / n, q.y / n, q.z / n };
}

void Orthonormal (double m[3][3]) // Gram-Schmidt on the columns, right-handed
{
	double c[3][3];
	for (int i = 0; i < 3; i++) for (int r = 0; r < 3; r++) c[i][r] = m[r][i];
	auto norm = [](double *v) { double l = std::sqrt (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) for (int k = 0; k < 3; k++) v[k] /= l; };
	norm (c[0]);
	double d = c[1][0] * c[0][0] + c[1][1] * c[0][1] + c[1][2] * c[0][2];
	for (int k = 0; k < 3; k++) c[1][k] -= d * c[0][k];
	norm (c[1]);
	c[2][0] = c[0][1] * c[1][2] - c[0][2] * c[1][1];
	c[2][1] = c[0][2] * c[1][0] - c[0][0] * c[1][2];
	c[2][2] = c[0][0] * c[1][1] - c[0][1] * c[1][0];
	for (int i = 0; i < 3; i++) for (int r = 0; r < 3; r++) m[r][i] = c[i][r];
}

double Len (const RtVec &v) { return std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z); }
RtVec Unit (const RtVec &v) { double l = Len (v); return l > 0 ? RtVec{ v.x / l, v.y / l, v.z / l } : v; }

} // namespace

// ----------------------------------------------------------------------------------------------
// records, names, tags

bool RtParseRecord (const std::string &sceneExtras, RtRecord &out, std::string &err)
{
	out = RtRecord ();
	QJsonObject o = Obj (sceneExtras)["orbiter"].toObject ();
	if (o.isEmpty()) { err = "no Orbiter data"; return false; }
	if (o["version"].toInt (0) != 2) { err = "Orbiter data version " + std::to_string (o["version"].toInt (0)) + " (2 needed: export again with the current GltfExport)"; return false; }
	out.vessel = o["vessel"].toString().toStdString();
	out.cls = o["class"].toString().toStdString();
	for (const auto &mv : o["meshes"].toArray()) {
		QJsonObject m = mv.toObject ();
		RtRecMesh r;
		r.index = m["index"].toInt (0);
		r.name = m["name"].toString().toStdString();
		r.ofs = V3 (m["offset"]);
		r.vis = m["visibility"].toInt (0);
		r.groups = m["groups"].toInt (0);
		for (const auto &v : m["skipped"].toArray()) r.skipped.push_back (v.toInt (-1));
		for (const auto &v : m["flags"].toArray()) r.flags.push_back ((DWORD)v.toDouble (0));
		for (const auto &v : m["materials"].toArray()) {
			QJsonObject x = v.toObject ();
			MATERIAL mt;
			mt.diffuse = C4 (x["diffuse"]); mt.ambient = C4 (x["ambient"]); mt.specular = C4 (x["specular"]); mt.emissive = C4 (x["emissive"]);
			mt.power = (float)x["power"].toDouble (0);
			r.mtrls.push_back (mt);
		}
		for (const auto &v : m["textures"].toArray()) {
			QJsonObject x = v.toObject ();
			RtRecTex t;
			t.name = x["name"].toString ("0").toStdString();
			t.dynamic = x["dynamic"].toBool (false);
			t.hash = x["hash"].toString().toStdString();
			r.tex.push_back (t);
		}
		if (r.name.empty()) { err = "a recorded mesh has no name"; return false; }
		out.meshes.push_back (std::move (r));
	}
	for (const auto &av : o["animations"].toArray()) {
		QJsonObject a = av.toObject ();
		RtRecAnim r;
		r.defstate = a["defstate"].toDouble (0);
		r.time0 = a["time0"].toDouble (0);
		for (const auto &cv : a["components"].toArray()) {
			QJsonObject c = cv.toObject ();
			RtRecComp k;
			k.type = TypeOf (c["type"].toString ());
			k.state0 = c["state0"].toDouble (0);
			k.state1 = c["state1"].toDouble (1);
			k.mesh = c["mesh"].toInt (0);
			if (c["groups"].isString ()) k.all = true;
			else for (const auto &g : c["groups"].toArray()) k.groups.push_back (g.toInt (-1));
			k.ref = V3 (c["ref"]);
			k.axis = V3 (c["axis"]);
			k.angle = c["angle"].toDouble (0);
			k.shift = V3 (c["shift"]);
			k.scale = V3 (c["scale"], RtVec{ 1, 1, 1 });
			QJsonArray p = c["parent"].toArray ();
			if (p.size() == 2) { k.parentAnim = p[0].toInt (-1); k.parentComp = p[1].toInt (-1); }
			r.comps.push_back (std::move (k));
		}
		out.anims.push_back (std::move (r));
	}
	return true;
}

bool RtListedMeshes (const std::string &gltfJson, std::vector<std::string> &names)
{
	names.clear ();
	QJsonObject root = Obj (gltfJson);
	QJsonArray scenes = root["scenes"].toArray ();
	int si = root["scene"].toInt (0);
	if (si < 0 || si >= scenes.size()) return false;
	QJsonObject o = scenes[si].toObject()["extras"].toObject()["orbiter"].toObject ();
	if (o["version"].toInt (0) != 2) return false;
	for (const auto &mv : o["meshes"].toArray()) {
		std::string n = mv.toObject()["name"].toString().toStdString();
		if (!n.empty()) names.push_back (n);
	}
	return !names.empty();
}

bool RtSameMeshName (const std::string &a, const std::string &b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) {
		char x = a[i] == '/' ? '\\' : a[i], y = b[i] == '/' ? '\\' : b[i];
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return false;
	}
	return true;
}

int RtAnimIndex (const std::string &name)
{
	size_t i = 0;
	while (i < name.size() && name[i] >= '0' && name[i] <= '9') i++;
	if (!i || i > 6 || (i < name.size() && name[i] != ' ')) return -1;
	return atoi (name.substr (0, i).c_str());
}

double RtFrac (double state0, double state1, double s)
{
	if (state1 == state0) return 0;
	double v = s; // the client's clamp, also for state0 > state1
	if (v < state0) v = state0;
	else if (v > state1) v = state1;
	return (v - state0) / (state1 - state0);
}

void RtQuatGltf (const RtVec &axis, double angle, double q[4])
{
	RtVec a = Unit (axis);
	double s = std::sin (angle * 0.5);
	q[0] = -s * a.x; q[1] = -s * a.y; q[2] = s * a.z; q[3] = std::cos (angle * 0.5);
}

static RtTag ParseTag (const std::string &extras)
{
	RtTag t;
	QJsonObject o = Obj (extras)["orbiter"].toObject ();
	if (o.isEmpty()) return t;
	if (o.contains ("anim") && o.contains ("component")) {
		t.kind = RT_TAG_PIVOT;
		t.anim = o["anim"].toInt (-1);
		t.comp = o["component"].toInt (-1);
	} else if (o.contains ("group")) {
		t.kind = RT_TAG_GROUP;
		t.mesh = o["mesh"].toInt (-1);
		t.group = o["group"].toInt (-1);
		for (const auto &cv : o["chain"].toArray()) {
			QJsonArray p = cv.toArray ();
			if (p.size() == 2) t.chain.push_back ({ p[0].toInt (-1), p[1].toInt (-1) });
		}
	} else if (o.contains ("mesh")) {
		t.kind = RT_TAG_MESH;
		t.mesh = o["mesh"].toInt (-1);
	}
	return t;
}

// ----------------------------------------------------------------------------------------------
// channels and the default pose (design 4)

namespace {

void Slerp (const double *a, const double *b0, double u, double *r)
{
	double b[4] = { b0[0], b0[1], b0[2], b0[3] };
	double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	if (d < 0) { for (double &x : b) x = -x; d = -d; }
	double wa, wb;
	if (d > 0.9999999) { wa = 1 - u; wb = u; }
	else {
		double th = std::acos (std::min (1.0, d)), sn = std::sin (th);
		wa = std::sin ((1 - u) * th) / sn; wb = std::sin (u * th) / sn;
	}
	double n = 0;
	for (int i = 0; i < 4; i++) { r[i] = wa * a[i] + wb * b[i]; n += r[i] * r[i]; }
	n = std::sqrt (n);
	if (n > 0) for (int i = 0; i < 4; i++) r[i] /= n;
}

const double *Key (const GltfSamplerSrc &s, size_t k) { return &s.out[(s.interp == GLTF_CUBIC ? 3 * k + 1 : k) * s.comps]; }

// a sampler's value at time t; a rotation normalised
void Eval (const GltfSamplerSrc &s, double t, double *v)
{
	int c = s.comps;
	size_t n = s.in.size ();
	if (!n) { for (int i = 0; i < c; i++) v[i] = (c == 4 && i == 3) ? 1 : 0; return; }
	if (t <= s.in[0] || n == 1) { memcpy (v, Key (s, 0), c * sizeof (double)); }
	else if (t >= s.in[n - 1]) { memcpy (v, Key (s, n - 1), c * sizeof (double)); }
	else {
		size_t k = std::upper_bound (s.in.begin(), s.in.end(), t) - s.in.begin() - 1;
		double td = s.in[k + 1] - s.in[k], u = (t - s.in[k]) / td;
		const double *a = Key (s, k), *b = Key (s, k + 1);
		if (s.interp == GLTF_STEP) memcpy (v, a, c * sizeof (double));
		else if (s.interp == GLTF_CUBIC) {
			const double *bo = &s.out[(3 * k + 2) * c], *ai = &s.out[(3 * (k + 1)) * c];
			double u2 = u * u, u3 = u2 * u;
			for (int i = 0; i < c; i++)
				v[i] = (2 * u3 - 3 * u2 + 1) * a[i] + (u3 - 2 * u2 + u) * td * bo[i] + (-2 * u3 + 3 * u2) * b[i] + (u3 - u2) * td * ai[i];
		} else if (c == 4) Slerp (a, b, u, v);
		else for (int i = 0; i < c; i++) v[i] = a[i] + (b[i] - a[i]) * u;
	}
	if (c == 4) {
		double l = std::sqrt (v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
		if (l > 0) for (int i = 0; i < 4; i++) v[i] /= l; else { v[0] = v[1] = v[2] = 0; v[3] = 1; }
	}
}

struct Chan {
	int ganim = -1, orb = -1;     // glTF animation, Orbiter animation (-1 none)
	const GltfSamplerSrc *s = nullptr;
};

struct Ana {
	RtFile &f;
	GltfScene &sc;
	std::vector<std::array<std::vector<Chan>, 3>> chans; // node, path
	std::vector<char> reach;
	std::map<int, int> entry;       // vessel mesh index -> record entry
	std::set<std::string> warned;

	Ana (RtFile &file) : f (file), sc (file.scene) {}
	void Warn (const std::string &w) { if (warned.insert (w).second) f.warnings.push_back (w); }

	double TimeDef (int a) const { return f.rec.anims[a].defstate - f.rec.anims[a].time0; }
	double StateOf (int a, double t) const { return t + f.rec.anims[a].time0; }

	// the channel that drives a node's path: the lowest Orbiter animation
	const Chan *Drive (int node, int path)
	{
		const Chan *best = nullptr;
		bool unmapped = false;
		for (const Chan &c : chans[node][path]) {
			if (c.orb < 0) { unmapped = true; continue; }
			if (best && c.orb != best->orb) Warn ("node " + sc.nodes[node].name + ": one path moved by animations " + std::to_string (best->orb) + " and " + std::to_string (c.orb) + ", the lowest used");
			if (!best || c.orb < best->orb) best = &c;
		}
		if (!best && unmapped) Warn ("node " + sc.nodes[node].name + ": moved only by animations that don't map to the vessel, its static pose used");
		return best;
	}

	void Pose ()
	{
		size_t n = sc.nodes.size ();
		chans.assign (n, {});
		for (size_t ai = 0; ai < sc.anims.size(); ai++) {
			const GltfAnimSrc &a = sc.anims[ai];
			for (const GltfChannelSrc &c : a.channels) {
				Chan ch;
				ch.ganim = (int)ai;
				ch.orb = f.animIndex[ai];
				ch.s = &a.samplers[c.sampler];
				chans[c.node][c.path].push_back (ch);
			}
		}
		f.restLocal.assign (n, Ident ());
		f.restWorld.assign (n, Ident ());
		reach.assign (n, 0);
		for (size_t i = 0; i < n; i++) {
			const GltfNodeSrc &d = sc.nodes[i];
			double t[3], r[4], s[3];
			memcpy (t, d.t, sizeof t); memcpy (r, d.r, sizeof r); memcpy (s, d.s, sizeof s);
			bool anim = false;
			for (int p = 0; p < 3; p++) {
				const Chan *c = Drive ((int)i, p);
				if (!c) continue;
				anim = true;
				double v[4];
				Eval (*c->s, TimeDef (c->orb), v);
				if (p == GLTF_TRANSLATION) memcpy (t, v, sizeof t);
				else if (p == GLTF_ROTATION) memcpy (r, v, sizeof r);
				else memcpy (s, v, sizeof s);
			}
			if (d.hasMatrix && !anim) f.restLocal[i].assign (d.matrix, d.matrix + 16);
			else f.restLocal[i] = Trs (t, r, s);
		}
		std::function<void(int, const M4&)> walk = [&](int i, const M4 &p) {
			reach[i] = 1;
			f.restWorld[i] = Mul (p, f.restLocal[i]);
			for (int k : sc.nodes[i].children) walk (k, f.restWorld[i]);
		};
		for (int r : sc.roots) walk (r, Ident ());
	}

	// default-pose local translation, rotation (glTF quaternion) and scale of a node
	void RestTrs (int i, double t[3], double r[4], double s[3])
	{
		const GltfNodeSrc &d = sc.nodes[i];
		memcpy (t, d.t, 3 * sizeof (double)); memcpy (r, d.r, 4 * sizeof (double)); memcpy (s, d.s, 3 * sizeof (double));
		for (int p = 0; p < 3; p++) {
			const Chan *c = Drive (i, p);
			if (!c) continue;
			double v[4];
			Eval (*c->s, TimeDef (c->orb), v);
			if (p == GLTF_TRANSLATION) memcpy (t, v, 3 * sizeof (double));
			else if (p == GLTF_ROTATION) memcpy (r, v, 4 * sizeof (double));
			else memcpy (s, v, 3 * sizeof (double));
		}
	}
};

} // namespace

// ----------------------------------------------------------------------------------------------
// output groups (design 3.1)

namespace {

// a primitive at a world matrix, Orbiter axes, minus the mesh offset, into parts of at most 65535 vertices (the G0 rules)
void Convert (const GltfPrim &pr, const M4 &world, const RtVec &ofs, std::vector<RtOut> &parts)
{
	parts.clear ();
	if (pr.tri.empty()) return;
	double L[3][3], N[3][3];
	Lin (world, L);
	double inv[3][3];
	if (!Inv3 (L, inv)) return;
	for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) N[r][c] = inv[c][r];
	bool reverse = Det3 (L) >= 0;
	bool hasN = !pr.nrm.empty(), hasUV = !pr.uv.empty();
	auto vertex = [&](uint32_t v, const double *fn) {
		const double *p = &pr.pos[v * 3];
		double w[3], n[3], l = 0;
		for (int r = 0; r < 3; r++) w[r] = L[r][0] * p[0] + L[r][1] * p[1] + L[r][2] * p[2] + world[12 + r];
		const double *n0 = fn ? fn : &pr.nrm[v * 3];
		for (int r = 0; r < 3; r++) { n[r] = N[r][0] * n0[0] + N[r][1] * n0[1] + N[r][2] * n0[2]; l += n[r] * n[r]; }
		l = l > 0 ? 1.0 / std::sqrt (l) : 0.0;
		NTVERTEX x;
		x.x = (float)(w[0] - ofs.x); x.y = (float)(w[1] - ofs.y); x.z = (float)(-w[2] - ofs.z);
		x.nx = (float)(n[0] * l); x.ny = (float)(n[1] * l); x.nz = (float)(-n[2] * l);
		x.tu = hasUV ? (float)pr.uv[v * 2] : 0.0f; x.tv = hasUV ? (float)pr.uv[v * 2 + 1] : 0.0f;
		return x;
	};
	RtOut g;
	g.material = pr.material;
	std::map<uint32_t, WORD> remap;
	auto flush = [&]() { if (!g.idx.empty()) { parts.push_back (g); parts.back().part = (int)parts.size() - 1; } g.vtx.clear(); g.idx.clear(); remap.clear(); };
	for (size_t i = 0; i + 2 < pr.tri.size(); i += 3) {
		uint32_t t3[3] = { pr.tri[i], pr.tri[i + 1], pr.tri[i + 2] };
		if (reverse) std::swap (t3[1], t3[2]);
		if (hasN) {
			int fresh = 0;
			for (int k = 0; k < 3; k++) if (!remap.count (t3[k])) fresh++;
			if (g.vtx.size() + fresh > 65535) flush ();
			for (int k = 0; k < 3; k++) {
				auto it = remap.find (t3[k]);
				if (it == remap.end()) {
					it = remap.emplace (t3[k], (WORD)g.vtx.size()).first;
					g.vtx.push_back (vertex (t3[k], nullptr));
				}
				g.idx.push_back (it->second);
			}
		} else {
			const double *a = &pr.pos[pr.tri[i] * 3], *b = &pr.pos[pr.tri[i + 1] * 3], *d = &pr.pos[pr.tri[i + 2] * 3];
			double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
			double fn[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
			if (g.vtx.size() + 3 > 65535) flush ();
			for (int k = 0; k < 3; k++) {
				g.idx.push_back ((WORD)g.vtx.size());
				g.vtx.push_back (vertex (t3[k], fn));
			}
		}
	}
	flush ();
}

// the record entry a node with a mesh belongs to: its own group tag, else the nearest tagged ancestor, else the first exterior mesh
int Owner (Ana &A, int node)
{
	for (int k = node; k >= 0; k = A.sc.nodes[k].parent) {
		const RtTag &t = A.f.tags[k];
		if (t.kind == RT_TAG_GROUP || t.kind == RT_TAG_MESH) {
			auto it = A.entry.find (t.mesh);
			if (it != A.entry.end()) return it->second;
		}
	}
	for (size_t e = 0; e < A.f.rec.meshes.size(); e++) if (A.f.rec.meshes[e].vis & 1) return (int)e;
	return A.f.rec.meshes.empty() ? -1 : 0;
}

void Outputs (Ana &A)
{
	RtFile &f = A.f;
	f.plans.assign (f.rec.meshes.size(), RtMeshPlan ());
	std::vector<std::set<int>> seen (f.rec.meshes.size());
	for (size_t ni = 0; ni < A.sc.nodes.size(); ni++) {
		const GltfNodeSrc &n = A.sc.nodes[ni];
		if (!A.reach[ni] || n.mesh < 0) continue;
		int e = Owner (A, (int)ni);
		if (e < 0) continue;
		RtMeshPlan &P = f.plans[e];
		const RtTag &t = f.tags[ni];
		int g = -1;
		if (t.kind == RT_TAG_GROUP && A.entry.count (t.mesh) && A.entry[t.mesh] == e && t.group >= 0 && !seen[e].count (t.group)) {
			g = t.group;
			seen[e].insert (g);
		}
		const std::vector<GltfPrim> &prims = A.sc.meshes[n.mesh];
		bool first = true;
		for (size_t p = 0; p < prims.size(); p++) {
			std::vector<RtOut> parts;
			Convert (prims[p], f.restWorld[ni], f.rec.meshes[e].ofs, parts);
			for (RtOut &o : parts) {
				o.node = (int)ni;
				o.prim = (int)p;
				if (first && g >= 0) { o.group = g; P.primary[g] = (int)P.outs.size(); }
				else o.added = P.nadded++;
				first = false;
				P.outs.push_back (std::move (o));
			}
			if (parts.empty() && first && g >= 0) { // a group with nothing to draw: an empty output keeps its index
				RtOut o;
				o.node = (int)ni; o.prim = (int)p; o.group = g; o.material = prims[p].material;
				P.primary[g] = (int)P.outs.size();
				P.outs.push_back (o);
				first = false;
			}
		}
		if (first && g >= 0) { // a group node without primitives
			RtOut o;
			o.node = (int)ni; o.group = g;
			P.primary[g] = (int)P.outs.size();
			P.outs.push_back (o);
		}
	}
}

} // namespace

// ----------------------------------------------------------------------------------------------
// the unchanged test and linked units (design 5.2, 5.3)

namespace {

int PathOf (int type) { return type == RT_ROTATE ? GLTF_ROTATION : type == RT_SCALE ? GLTF_SCALE : GLTF_TRANSLATION; }

bool Degenerate (const RtRecComp &k)
{
	if (k.state0 == k.state1 || k.type == RT_NULL) return true;
	if (k.type == RT_ROTATE) return Len (k.axis) == 0 || k.angle == 0;
	if (k.type == RT_TRANSLATE) return Len (k.shift) == 0;
	return k.scale.x == 1 && k.scale.y == 1 && k.scale.z == 1;
}

// the value a recorded component gives its pivot's path at state s (exporter formulas)
void Expected (const RtRecComp &k, double def, double s, const double rest[3], double *v)
{
	double df = RtFrac (k.state0, k.state1, s) - RtFrac (k.state0, k.state1, def);
	if (k.type == RT_ROTATE) {
		if (Len (k.axis) == 0) { v[0] = v[1] = v[2] = 0; v[3] = 1; }
		else RtQuatGltf (k.axis, k.angle * df, v);
	} else if (k.type == RT_TRANSLATE) {
		v[0] = rest[0] + k.shift.x * df; v[1] = rest[1] + k.shift.y * df; v[2] = rest[2] - k.shift.z * df;
	} else {
		double fs = RtFrac (k.state0, k.state1, s), fd = RtFrac (k.state0, k.state1, def);
		const double kk[3] = { k.scale.x, k.scale.y, k.scale.z };
		for (int i = 0; i < 3; i++) { double den = fd * (kk[i] - 1) + 1; v[i] = den != 0 ? (fs * (kk[i] - 1) + 1) / den : 1; }
	}
}

struct Units {
	Ana &A;
	std::vector<std::set<int>> self;  // node -> Orbiter animations with a channel on it
	std::map<int, int> pivotOk;       // node -> 1 passes, 0 fails (design 5.2)
	std::vector<int> uf;
	size_t nE = 0, nA = 0;

	Units (Ana &a) : A (a) {}
	int Find (int x) { while (uf[x] != x) x = uf[x] = uf[uf[x]]; return x; }
	void Join (int a, int b) { a = Find (a); b = Find (b); if (a != b) uf[a] = b; }
	int Unit (int anim, int e) const { return anim * (int)nE + e; }

	bool PivotCheck (int X, const RtVec &pivot, const RtRecComp &k, int a)
	{
		const M4 &w = A.f.restWorld[X];
		for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) if (std::fabs (w[c * 4 + r] - (r == c ? 1.0 : 0.0)) > 1e-6) return false;
		RtVec g = Mir (pivot.x, pivot.y, pivot.z);
		if (std::fabs (w[12] - g.x) > kTolM || std::fabs (w[13] - g.y) > kTolM || std::fabs (w[14] - g.z) > kTolM) return false;
		double rt[3], rr[4], rs[3];
		A.RestTrs (X, rt, rr, rs);
		double def = A.f.rec.anims[a].defstate, time0 = A.f.rec.anims[a].time0;
		int own = PathOf (k.type);
		bool moved = false;
		for (int p = 0; p < 3; p++)
			for (const Chan &c : A.chans[X][p]) {
				if (c.orb < 0) continue;
				if (c.orb != a) return false;
				const GltfSamplerSrc &s = *c.s;
				int nc = p == GLTF_ROTATION ? 4 : 3;
				if (p == own) {
					moved = true;
					if (s.interp != GLTF_LINEAR && s.in.size() > 1) return false;
					for (size_t i = 0; i < s.in.size(); i++) {
						double e[4], st = s.in[i] + time0;
						Expected (k, def, st, rt, e);
						const double *v = Key (s, i);
						if (p == GLTF_ROTATION) {
							double dp = 0, dm = 0;
							for (int j = 0; j < 4; j++) { dp += (v[j] - e[j]) * (v[j] - e[j]); dm += (v[j] + e[j]) * (v[j] + e[j]); }
							if (std::sqrt (std::min (dp, dm)) > kTolRad) return false;
						} else for (int j = 0; j < 3; j++) if (std::fabs (v[j] - e[j]) > (p == GLTF_SCALE ? kTolScale : kTolM)) return false;
					}
				} else {
					const double *rest = p == GLTF_TRANSLATION ? rt : p == GLTF_ROTATION ? rr : rs;
					for (size_t i = 0; i < s.in.size(); i++) {
						const double *v = Key (s, i);
						if (p == GLTF_ROTATION) {
							double dp = 0, dm = 0;
							for (int j = 0; j < 4; j++) { dp += (v[j] - rest[j]) * (v[j] - rest[j]); dm += (v[j] + rest[j]) * (v[j] + rest[j]); }
							if (std::sqrt (std::min (dp, dm)) > kTolRad) return false;
						} else for (int j = 0; j < nc; j++) if (std::fabs (v[j] - rest[j]) > kTolM) return false;
					}
				}
			}
		return moved || Degenerate (k);
	}

	void Run ()
	{
		RtFile &f = A.f;
		nE = f.rec.meshes.size ();
		nA = f.rec.anims.size ();
		size_t n = A.sc.nodes.size ();
		self.assign (n, {});
		for (size_t i = 0; i < n; i++)
			for (int p = 0; p < 3; p++)
				for (const Chan &c : A.chans[i][p]) if (c.orb >= 0) self[i].insert (c.orb);
		uf.resize (nA * nE);
		for (size_t i = 0; i < uf.size(); i++) uf[i] = (int)i;
		std::vector<char> changed (nA * nE, 0);

		for (size_t e = 0; e < nE; e++) {
			RtMeshPlan &P = f.plans[e];
			const RtRecMesh &M = f.rec.meshes[e];
			std::set<int> nodes;
			for (const RtOut &o : P.outs) nodes.insert (o.node);
			for (int G : nodes) {
				const RtTag &t = f.tags[G];
				bool primary = false;
				for (const RtOut &o : P.outs) if (o.node == G && o.group >= 0) primary = true;
				std::set<int> all;
				std::vector<int> pivots; // pivot ancestors, outer first
				for (int X = G; X >= 0; X = A.sc.nodes[X].parent) {
					all.insert (self[X].begin(), self[X].end());
					if (X != G && f.tags[X].kind == RT_TAG_PIVOT) pivots.push_back (X);
				}
				std::reverse (pivots.begin(), pivots.end());
				if (primary) for (auto &pc : t.chain) if (pc.first >= 0 && pc.first < (int)nA) all.insert (pc.first);
				if (all.empty()) continue;
				int first = Unit (*all.begin(), (int)e);
				for (int a : all) Join (first, Unit (a, (int)e));

				bool chainOk = primary && pivots.size() == t.chain.size();
				for (size_t i = 0; chainOk && i < pivots.size(); i++)
					chainOk = f.tags[pivots[i]].anim == t.chain[i].first && f.tags[pivots[i]].comp == t.chain[i].second;
				std::vector<RtVec> expect (pivots.size());
				if (chainOk) {
					RtVec pp;
					for (size_t i = 0; i < pivots.size(); i++) {
						int a = t.chain[i].first, c = t.chain[i].second;
						if (a < 0 || a >= (int)nA || c < 0 || c >= (int)f.rec.anims[a].comps.size()) { chainOk = false; break; }
						const RtRecComp &k = f.rec.anims[a].comps[c];
						if (k.type == RT_ROTATE || k.type == RT_SCALE) pp = RtVec{ k.ref.x + M.ofs.x, k.ref.y + M.ofs.y, k.ref.z + M.ofs.z };
						expect[i] = pp;
					}
				}
				for (int a : all) {
					bool bad = !chainOk;
					for (int X = G; !bad && X >= 0; X = A.sc.nodes[X].parent)
						if (f.tags[X].kind != RT_TAG_PIVOT && self[X].count (a)) bad = true;
					for (size_t i = 0; !bad && i < pivots.size(); i++) {
						int X = pivots[i];
						const RtTag &pt = f.tags[X];
						if (pt.anim != a) { if (self[X].count (a)) bad = true; continue; }
						auto it = pivotOk.find (X);
						if (it == pivotOk.end())
							it = pivotOk.emplace (X, PivotCheck (X, expect[i], f.rec.anims[a].comps[pt.comp], a) ? 1 : 0).first;
						if (!it->second) bad = true;
					}
					if (bad) changed[Unit (a, (int)e)] = 1;
				}
			}
		}

		// recorded parents link units across meshes
		for (size_t a = 0; a < nA; a++)
			for (const RtRecComp &k : f.rec.anims[a].comps) {
				if (k.parentAnim < 0 || k.parentAnim >= (int)nA || k.parentComp < 0 || k.parentComp >= (int)f.rec.anims[k.parentAnim].comps.size()) continue;
				const RtRecComp &p = f.rec.anims[k.parentAnim].comps[k.parentComp];
				auto ec = A.entry.find (k.mesh), ep = A.entry.find (p.mesh);
				if (ec != A.entry.end() && ep != A.entry.end()) Join (Unit ((int)a, ec->second), Unit (k.parentAnim, ep->second));
			}

		std::set<int> marked;
		for (size_t u = 0; u < changed.size(); u++) if (changed[u]) marked.insert (Find ((int)u));
		for (size_t e = 0; e < nE; e++) {
			RtMeshPlan &P = f.plans[e];
			for (size_t a = 0; a < nA; a++) {
				if (marked.count (Find (Unit ((int)a, (int)e)))) P.replaced.push_back ((int)a);
				int cnt = 0;
				for (const RtRecComp &k : f.rec.anims[a].comps) if (k.mesh == f.rec.meshes[e].index) cnt++;
				if (cnt) P.recCount[(int)a] = cnt;
			}
		}
		for (size_t a = 0; a < nA; a++)
			for (size_t c = 0; c < f.rec.anims[a].comps.size(); c++) {
				const RtRecComp &k = f.rec.anims[a].comps[c];
				if (k.parentAnim < 0 || k.parentAnim >= (int)nA || k.parentComp < 0 || k.parentComp >= (int)f.rec.anims[k.parentAnim].comps.size()) continue;
				const RtRecComp &p = f.rec.anims[k.parentAnim].comps[k.parentComp];
				auto ec = A.entry.find (k.mesh), ep = A.entry.find (p.mesh);
				if (k.mesh >= 0 && ec == A.entry.end() && ep != A.entry.end()) {
					const std::vector<int> &R = f.plans[ep->second].replaced;
					if (std::binary_search (R.begin(), R.end(), k.parentAnim))
						A.Warn ("animation " + std::to_string (a) + " part " + std::to_string (c) + ": on a mesh not in the file, its parent is taken from the file; it keeps the program's motion");
				}
			}
	}
};

} // namespace

// ----------------------------------------------------------------------------------------------
// components from the file (design 5.4, 5.5)

namespace {

// the states a channel is sampled at: its keys, the default state, extra points for CUBICSPLINE and STEP
std::vector<double> Samples (const GltfSamplerSrc &s, double time0, double def)
{
	std::vector<double> t;
	for (size_t i = 0; i < s.in.size(); i++) {
		t.push_back (s.in[i] + time0);
		if (i + 1 < s.in.size()) {
			double a = s.in[i], b = s.in[i + 1];
			if (s.interp == GLTF_CUBIC) for (int k = 1; k < 8; k++) t.push_back (a + (b - a) * k / 8 + time0);
			else if (s.interp == GLTF_STEP) t.push_back (b - (b - a) * 1e-3 + time0);
		}
	}
	t.push_back (def);
	std::sort (t.begin(), t.end());
	t.erase (std::unique (t.begin(), t.end(), [](double x, double y) { return std::fabs (x - y) < 1e-12; }), t.end());
	return t;
}

// STEP holds a key's value up to the ramp just before the next key
void EvalAt (const GltfSamplerSrc &s, double t, double *v)
{
	if (s.interp == GLTF_STEP && s.in.size() > 1 && t > s.in.front() && t < s.in.back()) {
		size_t k = std::upper_bound (s.in.begin(), s.in.end(), t) - s.in.begin() - 1;
		double b = s.in[k + 1], a = s.in[k], r0 = b - (b - a) * 1e-3;
		if (t > r0) {
			const double *x = Key (s, k), *y = Key (s, k + 1);
			double u = (t - r0) / (b - r0);
			if (s.comps == 4) Slerp (x, y, u, v); else for (int i = 0; i < s.comps; i++) v[i] = x[i] + (y[i] - x[i]) * u;
			return;
		}
	}
	Eval (s, t, v);
}

// one side of the default state, nearest first: the pieces a linear motion covers
template <class Ok> void Pieces (size_t n, Ok ok, std::vector<std::pair<int, int>> &out)
{
	out.clear ();
	size_t i = 0;
	while (i + 1 < n) {
		size_t e = i + 1;
		while (e + 1 < n && ok (i, e + 1)) e++;
		out.push_back ({ (int)i, (int)e });
		i = e;
	}
}

struct Fit {
	Ana &A;
	int e;
	RtMeshPlan &P;
	Fit (Ana &a, int entry) : A (a), e (entry), P (a.f.plans[entry]) {}

	int Add (RtCompSpec s, int parent)
	{
		s.parent = parent;
		P.comps.push_back (s);
		return (int)P.comps.size() - 1;
	}

	// rotations about a fixed point, Orbiter frame; q[0] is the default state
	void Rot (int anim, const std::vector<double> &st, const std::vector<Q4> &q, size_t d, const RtVec &ref, int &parent)
	{
		for (int side = 0; side < 2; side++) {
			std::vector<double> s; std::vector<Q4> v;
			if (side == 0) for (size_t i = d; i < st.size(); i++) { s.push_back (st[i]); v.push_back (q[i]); }
			else for (size_t i = d + 1; i-- > 0;) { s.push_back (st[i]); v.push_back (q[i]); }
			if (s.size() < 2) continue;
			// accumulated angle about one axis per step
			auto piece = [&](size_t i, size_t j, RtVec &u, double &ang) {
				ang = 0;
				bool have = false;
				for (size_t k = i + 1; k <= j; k++) {
					Q4 step = QMul (QConj (v[k - 1]), v[k]);
					RtVec a; double phi;
					QAxisAngle (step, a, phi);
					if (phi < 1e-12) continue;
					if (!have) { u = a; have = true; ang += phi; }
					else ang += (a.x * u.x + a.y * u.y + a.z * u.z) >= 0 ? phi : -phi;
				}
				if (!have) { u = { 1, 0, 0 }; ang = 0; }
			};
			auto ok = [&](size_t i, size_t j) {
				RtVec u; double ang;
				piece (i, j, u, ang);
				for (size_t k = i + 1; k <= j; k++) {
					double fr = (s[k] - s[i]) / (s[j] - s[i]);
					Q4 pr = QMul (v[i], QAxis (u, ang * fr));
					if (QAngle (QMul (QConj (pr), v[k])) > kTolRad) return false;
				}
				return true;
			};
			std::vector<std::pair<int, int>> pcs;
			Pieces (s.size(), ok, pcs);
			for (auto &pc : pcs) {
				RtVec u; double ang;
				piece (pc.first, pc.second, u, ang);
				if (std::fabs (ang) < 1e-12) continue;
				RtCompSpec c;
				c.anim = anim; c.type = RT_ROTATE; c.ref = ref; c.axis = u;
				if (side == 0) { c.state0 = s[pc.first]; c.state1 = s[pc.second]; c.angle = ang; }
				else { c.state0 = s[pc.second]; c.state1 = s[pc.first]; c.angle = -ang; }
				parent = Add (c, parent);
			}
		}
	}

	void Lin (int anim, const std::vector<double> &st, const std::vector<RtVec> &dv, size_t d, int &parent)
	{
		for (int side = 0; side < 2; side++) {
			std::vector<double> s; std::vector<RtVec> v;
			if (side == 0) for (size_t i = d; i < st.size(); i++) { s.push_back (st[i]); v.push_back (dv[i]); }
			else for (size_t i = d + 1; i-- > 0;) { s.push_back (st[i]); v.push_back (dv[i]); }
			if (s.size() < 2) continue;
			auto ok = [&](size_t i, size_t j) {
				RtVec D{ v[j].x - v[i].x, v[j].y - v[i].y, v[j].z - v[i].z };
				for (size_t k = i + 1; k < j; k++) {
					double fr = (s[k] - s[i]) / (s[j] - s[i]);
					RtVec p{ v[i].x + D.x * fr - v[k].x, v[i].y + D.y * fr - v[k].y, v[i].z + D.z * fr - v[k].z };
					if (Len (p) > kTolM) return false;
				}
				return true;
			};
			std::vector<std::pair<int, int>> pcs;
			Pieces (s.size(), ok, pcs);
			for (auto &pc : pcs) {
				RtVec D{ v[pc.second].x - v[pc.first].x, v[pc.second].y - v[pc.first].y, v[pc.second].z - v[pc.first].z };
				if (Len (D) < 1e-12) continue;
				RtCompSpec c;
				c.anim = anim; c.type = RT_TRANSLATE;
				if (side == 0) { c.state0 = s[pc.first]; c.state1 = s[pc.second]; c.shift = D; }
				else { c.state0 = s[pc.second]; c.state1 = s[pc.first]; c.shift = RtVec{ -D.x, -D.y, -D.z }; }
				parent = Add (c, parent);
			}
		}
	}

	void Scl (int anim, const std::vector<double> &st, const std::vector<RtVec> &sv, size_t d, const RtVec &ref, int &parent)
	{
		for (int side = 0; side < 2; side++) {
			std::vector<double> s; std::vector<RtVec> v;
			if (side == 0) for (size_t i = d; i < st.size(); i++) { s.push_back (st[i]); v.push_back (sv[i]); }
			else for (size_t i = d + 1; i-- > 0;) { s.push_back (st[i]); v.push_back (sv[i]); }
			if (s.size() < 2) continue;
			auto rel = [&](size_t i, size_t k) {
				auto q = [](double a, double b) { return std::fabs (b) > 1e-12 ? a / b : 1.0; };
				return RtVec{ q (v[k].x, v[i].x), q (v[k].y, v[i].y), q (v[k].z, v[i].z) };
			};
			auto ok = [&](size_t i, size_t j) {
				RtVec E = rel (i, j);
				for (size_t k = i + 1; k < j; k++) {
					double fr = (s[k] - s[i]) / (s[j] - s[i]);
					RtVec r = rel (i, k);
					if (std::fabs (1 + (E.x - 1) * fr - r.x) > kTolScale || std::fabs (1 + (E.y - 1) * fr - r.y) > kTolScale || std::fabs (1 + (E.z - 1) * fr - r.z) > kTolScale) return false;
				}
				return true;
			};
			std::vector<std::pair<int, int>> pcs;
			Pieces (s.size(), ok, pcs);
			for (auto &pc : pcs) {
				RtVec E = rel (pc.first, pc.second);
				if (std::fabs (E.x - 1) < 1e-12 && std::fabs (E.y - 1) < 1e-12 && std::fabs (E.z - 1) < 1e-12) continue;
				RtCompSpec c;
				c.anim = anim; c.type = RT_SCALE; c.ref = ref;
				if (side == 0) { c.state0 = s[pc.first]; c.state1 = s[pc.second]; c.scale = E; }
				else {
					auto inv = [](double x) { return std::fabs (x) > 1e-12 ? 1 / x : 1.0; };
					c.state0 = s[pc.second]; c.state1 = s[pc.first]; c.scale = RtVec{ inv (E.x), inv (E.y), inv (E.z) };
				}
				parent = Add (c, parent);
			}
		}
	}
};

} // namespace

namespace {

void Components (Ana &A, Units &U, int e)
{
	RtFile &f = A.f;
	RtMeshPlan &P = f.plans[e];
	if (P.replaced.empty()) return;
	const std::vector<int> &R = P.replaced;
	auto inR = [&](int a) { return std::binary_search (R.begin(), R.end(), a); };
	const RtVec &ofs = f.rec.meshes[e].ofs;
	std::map<int, int> inner;
	std::set<int> fitted;
	Fit F (A, e);

	std::function<int(int)> ensure = [&](int X) -> int {
		auto it = inner.find (X);
		if (it != inner.end()) return it->second;
		int up = A.sc.nodes[X].parent;
		int parent = up >= 0 ? ensure (up) : -1, last = parent;
		const Chan *dr[3];
		bool any = false;
		for (int p = 0; p < 3; p++) {
			dr[p] = A.Drive (X, p);
			if (dr[p] && !inR (dr[p]->orb)) { A.Warn ("node " + A.sc.nodes[X].name + ": animation " + std::to_string (dr[p]->orb) + " is not taken from the file here, ignored"); dr[p] = nullptr; }
			any |= dr[p] != nullptr;
		}
		const RtTag &t = f.tags[X];
		auto ok = U.pivotOk.find (X);
		if (any && t.kind == RT_TAG_PIVOT && inR (t.anim) && ok != U.pivotOk.end() && ok->second) {
			const RtRecComp &k = f.rec.anims[t.anim].comps[t.comp];
			RtCompSpec c;
			c.anim = t.anim; c.type = k.type == RT_NULL ? RT_TRANSLATE : k.type;
			c.state0 = k.state0; c.state1 = k.state1;
			c.ref = k.ref; c.axis = k.axis; c.angle = k.angle; c.shift = k.shift; c.scale = k.scale;
			c.pivotAnim = t.anim; c.pivotComp = t.comp;
			last = F.Add (c, last);
		} else if (any) {
			M4 P0 = up >= 0 ? f.restWorld[up] : Ident ();
			double rt[3], rr[4], rs[3];
			A.RestTrs (X, rt, rr, rs);
			double PL[3][3], R0[3][3], L[3][3], Li[3][3];
			Lin (P0, PL);
			QuatMat (rr, R0);
			MatMul3 (PL, R0, L);
			const M4 &W = f.restWorld[X];
			RtVec piv = Mir (W[12], W[13], W[14]);
			RtVec ref{ piv.x - ofs.x, piv.y - ofs.y, piv.z - ofs.z };
			auto defIndex = [](const std::vector<double> &st, double d) { size_t i = 0; while (i < st.size() && std::fabs (st[i] - d) >= 1e-12) i++; return i; };
			if (dr[GLTF_TRANSLATION]) {
				int a = dr[GLTF_TRANSLATION]->orb;
				const GltfSamplerSrc &s = *dr[GLTF_TRANSLATION]->s;
				double def = f.rec.anims[a].defstate, t0 = f.rec.anims[a].time0;
				std::vector<double> st = Samples (s, t0, def);
				std::vector<RtVec> d;
				for (double x : st) {
					double v[4];
					EvalAt (s, x - t0, v);
					double dl[3] = { v[0] - rt[0], v[1] - rt[1], v[2] - rt[2] };
					d.push_back (Mir (PL[0][0] * dl[0] + PL[0][1] * dl[1] + PL[0][2] * dl[2], PL[1][0] * dl[0] + PL[1][1] * dl[1] + PL[1][2] * dl[2], PL[2][0] * dl[0] + PL[2][1] * dl[1] + PL[2][2] * dl[2]));
				}
				size_t before = P.comps.size ();
				F.Lin (a, st, d, defIndex (st, def), last);
				if (P.comps.size() > before) fitted.insert (a);
			}
			bool rotates = false;
			if (dr[GLTF_ROTATION]) {
				int a = dr[GLTF_ROTATION]->orb;
				const GltfSamplerSrc &s = *dr[GLTF_ROTATION]->s;
				double def = f.rec.anims[a].defstate, t0 = f.rec.anims[a].time0;
				bool conf = Conformal (L);
				if (!conf) A.Warn ("node " + A.sc.nodes[X].name + ": rotation under an uneven scale, made rigid");
				if (Inv3 (L, Li)) {
					std::vector<double> st = Samples (s, t0, def);
					std::vector<Q4> q;
					double R0t[3][3];
					for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) R0t[i][j] = R0[j][i];
					for (double x : st) {
						double v[4], Rs[3][3], D[3][3], Wm[3][3], Om[3][3];
						EvalAt (s, x - t0, v);
						QuatMat (v, Rs);
						MatMul3 (R0t, Rs, D);
						MatMul3 (L, D, Wm);
						MatMul3 (Wm, Li, Wm);
						MirM (Wm, Om);
						if (!conf) Orthonormal (Om);
						Q4 qq = QFromMat (Om);
						if (!q.empty() && qq.w * q.back().w + qq.x * q.back().x + qq.y * q.back().y + qq.z * q.back().z < 0) qq = { -qq.w, -qq.x, -qq.y, -qq.z };
						q.push_back (qq);
					}
					size_t before = P.comps.size ();
					F.Rot (a, st, q, defIndex (st, def), ref, last);
					if (P.comps.size() > before) { fitted.insert (a); rotates = true; }
				}
			}
			if (dr[GLTF_SCALE]) {
				int a = dr[GLTF_SCALE]->orb;
				const GltfSamplerSrc &s = *dr[GLTF_SCALE]->s;
				double def = f.rec.anims[a].defstate, t0 = f.rec.anims[a].time0;
				std::vector<double> st = Samples (s, t0, def);
				int axisOf[3] = { -1, -1, -1 };
				bool perm = true;
				for (int i = 0; i < 3 && perm; i++) {
					double c[3] = { L[0][i], L[1][i], L[2][i] }, l = std::sqrt (c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
					perm = false;
					for (int j = 0; j < 3; j++) if (l > 0 && std::fabs (c[j]) / l >= 1 - 1e-6) { axisOf[i] = j; perm = true; }
				}
				if (perm && (axisOf[0] == axisOf[1] || axisOf[1] == axisOf[2] || axisOf[0] == axisOf[2])) perm = false;
				std::vector<RtVec> sv;
				bool uneven = false;
				for (double x : st) {
					double v[4];
					EvalAt (s, x - t0, v);
					double k[3];
					for (int i = 0; i < 3; i++) k[i] = std::fabs (rs[i]) > 1e-12 ? v[i] / rs[i] : 1;
					if (std::fabs (k[0] - k[1]) > kTolScale || std::fabs (k[1] - k[2]) > kTolScale) uneven = true;
					double w[3];
					if (perm) for (int i = 0; i < 3; i++) w[axisOf[i]] = k[i];
					else { double g = std::cbrt (std::fabs (k[0] * k[1] * k[2])); w[0] = w[1] = w[2] = g; }
					sv.push_back (RtVec{ w[0], w[1], w[2] });
				}
				if (uneven && !perm) A.Warn ("node " + A.sc.nodes[X].name + ": uneven scale off the vessel axes, its uniform part used");
				bool underRot = rotates;
				for (int j = last; j >= 0 && !underRot; j = P.comps[j].parent) if (P.comps[j].type == RT_ROTATE) underRot = true;
				if (uneven && perm && underRot) A.Warn ("node " + A.sc.nodes[X].name + ": uneven scale under a rotation, Orbiter keeps its axes fixed");
				size_t before = P.comps.size ();
				F.Scl (a, st, sv, defIndex (st, def), ref, last);
				if (P.comps.size() > before) fitted.insert (a);
			}
			if (t.kind == RT_TAG_PIVOT && last != parent) { P.comps[last].pivotAnim = t.anim; P.comps[last].pivotComp = t.comp; }
		}
		inner[X] = last;
		return last;
	};

	std::set<int> nodes;
	for (const RtOut &o : P.outs) nodes.insert (o.node);
	for (int G : nodes) {
		bool moved = false;
		for (int X = G; X >= 0 && !moved; X = A.sc.nodes[X].parent)
			for (int a : U.self[X]) if (inR (a)) { moved = true; break; }
		const RtTag &t = f.tags[G];
		if (!moved && t.kind == RT_TAG_GROUP) for (auto &pc : t.chain) if (inR (pc.first)) moved = true;
		if (!moved) continue;
		int spec = ensure (G);
		if (spec < 0) continue;
		for (const RtOut &o : P.outs) if (o.node == G) P.comps[spec].groups.push_back (o.group >= 0 ? o.group : -1 - o.added);
	}
	P.fitted.assign (fitted.begin(), fitted.end());
}

} // namespace

bool RtAnalyse (RtFile &f, std::string &err)
{
	f.warnings.clear ();
	f.plans.clear ();
	f.tags.clear ();
	for (const GltfNodeSrc &n : f.scene.nodes) f.tags.push_back (ParseTag (n.extras));
	f.animIndex.assign (f.scene.anims.size(), -1);
	Ana A (f);
	for (size_t i = 0; i < f.scene.anims.size(); i++) {
		int n = RtAnimIndex (f.scene.anims[i].name);
		if (n >= 0 && n < (int)f.rec.anims.size()) f.animIndex[i] = n;
		else if (!f.scene.anims[i].channels.empty()) A.Warn ("animation \"" + f.scene.anims[i].name + "\" doesn't name a recorded animation, ignored");
	}
	for (size_t e = 0; e < f.rec.meshes.size(); e++) {
		if (A.entry.count (f.rec.meshes[e].index)) { err = "two recorded meshes with index " + std::to_string (f.rec.meshes[e].index); return false; }
		A.entry[f.rec.meshes[e].index] = (int)e;
	}
	A.Pose ();
	Outputs (A);
	Units U (A);
	U.Run ();
	for (size_t e = 0; e < f.rec.meshes.size(); e++) Components (A, U, (int)e);
	return true;
}

bool RtLoad (const std::string &path, RtFile &f, std::string &err)
{
	f = RtFile ();
	f.path = path;
	if (!GltfReadScene (path, f.scene, err)) return false;
	if (!RtParseRecord (f.scene.sceneExtras, f.rec, err)) return false;
	try { return RtAnalyse (f, err); }
	catch (const std::bad_alloc &) { err = "out of memory"; return false; }
}

// ----------------------------------------------------------------------------------------------
// geometry, materials, images

bool RtSameGeometry (const std::vector<const RtOut*> &parts, const RtBaseGroup &base)
{
	struct T { const NTVERTEX *v[3]; double c[3]; };
	auto make = [](const NTVERTEX *a, const NTVERTEX *b, const NTVERTEX *d) {
		T t{ { a, b, d }, { (a->x + b->x + d->x) / 3.0, (a->y + b->y + d->y) / 3.0, (a->z + b->z + d->z) / 3.0 } };
		return t;
	};
	std::vector<T> bt, ft;
	for (DWORD i = 0; i + 2 < base.nidx; i += 3) {
		if (base.idx[i] >= base.nvtx || base.idx[i + 1] >= base.nvtx || base.idx[i + 2] >= base.nvtx) continue;
		bt.push_back (make (base.vtx + base.idx[i], base.vtx + base.idx[i + 1], base.vtx + base.idx[i + 2]));
	}
	for (const RtOut *p : parts)
		for (size_t i = 0; i + 2 < p->idx.size(); i += 3)
			ft.push_back (make (&p->vtx[p->idx[i]], &p->vtx[p->idx[i + 1]], &p->vtx[p->idx[i + 2]]));
	if (ft.size() > bt.size()) return false;
	const double h = 1e-3, tp = 5e-5, tu = 1e-4; // normals not compared: Blender's import changes some by up to 90 degrees on untouched parts (DG cockpit levers)
	auto cell = [&](const double c[3], int dx, int dy, int dz) {
		long long x = (long long)std::floor (c[0] / h) + dx, y = (long long)std::floor (c[1] / h) + dy, z = (long long)std::floor (c[2] / h) + dz;
		return (x * 73856093ll) ^ (y * 19349663ll) ^ (z * 83492791ll);
	};
	std::multimap<long long, size_t> grid;
	for (size_t i = 0; i < bt.size(); i++) grid.emplace (cell (bt[i].c, 0, 0, 0), i);
	auto vsame = [&](const NTVERTEX *a, const NTVERTEX *b) {
		if (std::fabs (a->x - b->x) > tp || std::fabs (a->y - b->y) > tp || std::fabs (a->z - b->z) > tp) return false;
		return std::fabs (a->tu - b->tu) <= tu && std::fabs (a->tv - b->tv) <= tu;
	};
	auto tsame = [&](const T &a, const T &b) {
		for (int r = 0; r < 3; r++)
			if (vsame (a.v[0], b.v[r]) && vsame (a.v[1], b.v[(r + 1) % 3]) && vsame (a.v[2], b.v[(r + 2) % 3])) return true;
		return false;
	};
	std::vector<char> used (bt.size(), 0);
	for (const T &t : ft) {
		bool found = false;
		for (int dx = -1; dx <= 1 && !found; dx++) for (int dy = -1; dy <= 1 && !found; dy++) for (int dz = -1; dz <= 1 && !found; dz++) {
			auto r = grid.equal_range (cell (t.c, dx, dy, dz));
			for (auto it = r.first; it != r.second; ++it)
				if (!used[it->second] && tsame (t, bt[it->second])) { used[it->second] = 1; found = true; break; }
		}
		if (!found) return false;
	}
	for (size_t i = 0; i < bt.size(); i++) {
		if (used[i]) continue;
		const T &b = bt[i];
		double e1[3] = { b.v[1]->x - b.v[0]->x, b.v[1]->y - b.v[0]->y, b.v[1]->z - b.v[0]->z }, e2[3] = { b.v[2]->x - b.v[0]->x, b.v[2]->y - b.v[0]->y, b.v[2]->z - b.v[0]->z };
		double cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		if (std::sqrt (cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]) < 1e-12) continue; // zero area
		bool dup = false;
		auto r = grid.equal_range (cell (b.c, 0, 0, 0));
		for (auto it = r.first; it != r.second && !dup; ++it) {
			if (it->second == i || !used[it->second]) continue;
			const T &o = bt[it->second];
			for (int k = 0; k < 3 && !dup; k++) {
				bool all = true;
				for (int j = 0; j < 3 && all; j++) {
					const NTVERTEX *p = b.v[j], *q = o.v[(j + k) % 3];
					all = std::fabs (p->x - q->x) <= tp && std::fabs (p->y - q->y) <= tp && std::fabs (p->z - q->z) <= tp;
				}
				dup = all;
			}
		}
		if (!dup) return false;
	}
	return true;
}

RtMatTag RtMaterialTag (const std::string &materialJson)
{
	RtMatTag r;
	QJsonObject m = Obj (materialJson);
	QJsonObject o = m["extras"].toObject()["orbiter"].toObject ();
	if (!o.contains ("mesh")) return r;
	r.tagged = true;
	r.mesh = o["mesh"].toInt (-1);
	r.mtrl = o["mtrl"].toInt (-1);
	r.tex = o["tex"].toInt (-1);
	r.unlit = o["unlit"].toBool (false);
	QJsonObject x = o["values"].toObject (), pmr = m["pbrMetallicRoughness"].toObject (), ext = m["extensions"].toObject ();
	auto num = [](const QJsonObject &j, const char *k, double d) { return j.contains (k) ? j[k].toDouble (d) : d; };
	auto arr = [](const QJsonObject &j, const char *k, int n, double d) {
		std::vector<double> v (n, d);
		QJsonArray a = j[k].toArray ();
		if (a.size() == n) for (int i = 0; i < n; i++) v[i] = a[i].toDouble (d);
		return v;
	};
	auto near = [](const std::vector<double> &a, const std::vector<double> &b) {
		for (size_t i = 0; i < a.size(); i++) if (std::fabs (a[i] - b[i]) > 1e-5) return false;
		return true;
	};
	bool unlit = ext.contains ("KHR_materials_unlit");
	bool edited = unlit != r.unlit;
	for (auto it = ext.begin(); it != ext.end(); ++it) if (it.key() != "KHR_materials_unlit") edited = true;
	edited |= !near (arr (pmr, "baseColorFactor", 4, 1), arr (x, "base", 4, 1));
	if (!r.unlit) {
		edited |= std::fabs (num (pmr, "metallicFactor", 1) - num (x, "metallic", 1)) > 1e-5;
		edited |= std::fabs (num (pmr, "roughnessFactor", 1) - num (x, "roughness", 1)) > 1e-5;
	}
	edited |= !near (arr (m, "emissiveFactor", 3, 0), arr (x, "emissive", 3, 0));
	QString am = m["alphaMode"].toString ("OPAQUE"), ar = x["alphaMode"].toString ("OPAQUE");
	edited |= (am == "OPAQUE") != (ar == "OPAQUE"); // BLEND and MASK swap through Blender on untouched materials (DG cockpit)
	if (am == "MASK" && ar == "MASK") edited |= std::fabs (num (m, "alphaCutoff", 0.5) - num (x, "alphaCutoff", 0.5)) > 1e-5;
	edited |= m["doubleSided"].toBool (false);
	int maps = (pmr.contains ("baseColorTexture") ? 1 : 0) | (m.contains ("normalTexture") ? 2 : 0) | (m.contains ("emissiveTexture") ? 4 : 0) |
		(pmr.contains ("metallicRoughnessTexture") ? 8 : 0) | (m.contains ("occlusionTexture") ? 16 : 0);
	edited |= maps != o["maps"].toInt (-1);
	r.edited = edited;
	return r;
}

std::string RtPixelHash (int w, int h, const unsigned char *rgba)
{
	unsigned long long x = 1469598103934665603ull;
	auto feed = [&](const unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) { x ^= p[i]; x *= 1099511628211ull; } };
	unsigned char hdr[8];
	for (int i = 0; i < 4; i++) { hdr[i] = (unsigned char)((unsigned)w >> (8 * i)); hdr[4 + i] = (unsigned char)((unsigned)h >> (8 * i)); }
	feed (hdr, 8);
	feed (rgba, (size_t)w * h * 4);
	char b[24];
	snprintf (b, sizeof b, "%016llx", x);
	return b;
}

std::string RtImageHash (const std::vector<unsigned char> &encoded)
{
	QImage q;
	if (encoded.empty() || !q.loadFromData (encoded.data(), (int)encoded.size())) return "";
	q = q.convertToFormat (QImage::Format_RGBA8888);
	std::vector<unsigned char> px ((size_t)q.width() * q.height() * 4);
	for (int y = 0; y < q.height(); y++) memcpy (&px[(size_t)y * q.width() * 4], q.constScanLine (y), (size_t)q.width() * 4);
	return RtPixelHash (q.width(), q.height(), px.data());
}

void RtLocalAt (const RtFile &f, int node, const std::vector<double> &states, std::vector<double> &m)
{
	const GltfNodeSrc &d = f.scene.nodes[node];
	double t[3], r[4], s[3];
	memcpy (t, d.t, sizeof t); memcpy (r, d.r, sizeof r); memcpy (s, d.s, sizeof s);
	bool anim = false;
	for (int p = 0; p < 3; p++) {
		const GltfSamplerSrc *best = nullptr;
		int bo = -1;
		for (size_t ai = 0; ai < f.scene.anims.size(); ai++) {
			int o = f.animIndex[ai];
			if (o < 0 || o >= (int)states.size()) continue;
			for (const GltfChannelSrc &c : f.scene.anims[ai].channels)
				if (c.node == node && c.path == p && (bo < 0 || o < bo)) { bo = o; best = &f.scene.anims[ai].samplers[c.sampler]; }
		}
		if (!best) continue;
		anim = true;
		double v[4];
		EvalAt (*best, states[bo] - f.rec.anims[bo].time0, v);
		if (p == GLTF_TRANSLATION) memcpy (t, v, sizeof t);
		else if (p == GLTF_ROTATION) memcpy (r, v, sizeof r);
		else memcpy (s, v, sizeof s);
	}
	if (d.hasMatrix && !anim) m.assign (d.matrix, d.matrix + 16);
	else m = Trs (t, r, s);
}
