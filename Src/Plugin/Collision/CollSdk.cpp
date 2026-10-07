// not upstream: collision addon, the counted CollSdk calls: count, log line, forward (design E2 1.4)
#include <cassert>
#include <cstdio>
#include <string>
#include "CollSdk.h"

namespace {
const char *const kKindName[CSK_N] = { "state", "attitude", "spin", "force", "tank", "warp", "vtx", "matrix", "probe", "notice", "ui" };
}

void CollSdk::LogCall (CollSdkKind k, CollH v)
{
	int need = (k <= CSK_WARP || k == CSK_UI) ? 2 : (k == CSK_PROBE ? 99 : 3);
	if (logLevel < need) return;
	std::string vn = (k == CSK_WARP || k == CSK_UI || !v) ? std::string ("-") : Name (v);
	char buf[512];
	snprintf (buf, sizeof buf, "Collision write t=%.17g '%s' %s", SimTime (), vn.c_str (), kKindName[k]);
	Log (1, buf);
}

void CollSdk::SetState (CollH v, const CollStateWrite &s) { cnt.n[CSK_STATE]++; LogCall (CSK_STATE, v); DoSetState (v, s); }
void CollSdk::SetAttitude (CollH v, const Matrix &R) { cnt.n[CSK_ATTITUDE]++; LogCall (CSK_ATTITUDE, v); DoSetAttitude (v, R); }
void CollSdk::SetSpin (CollH v, const Vector &w) { cnt.n[CSK_SPIN]++; LogCall (CSK_SPIN, v); DoSetSpin (v, w); }
void CollSdk::AddForce (CollH v, const Vector &F, const Vector &r) { cnt.n[CSK_FORCE]++; LogCall (CSK_FORCE, v); DoAddForce (v, F, r); }
void CollSdk::SetTank (CollH v, CollH th, CollH tank) { cnt.n[CSK_TANK]++; LogCall (CSK_TANK, v); DoSetTank (v, th, tank); }

CollH CollSdk::CreateTank (CollH v, double maxMass, double mass)
{
	assert (mass >= 0);
	if (!(mass >= 0)) mass = 0; // a negative mass means "full" in the core
	cnt.n[CSK_TANK]++; LogCall (CSK_TANK, v);
	return DoCreateTank (v, maxMass, mass);
}

void CollSdk::DelTank (CollH v, CollH tank) { cnt.n[CSK_TANK]++; LogCall (CSK_TANK, v); DoDelTank (v, tank); }
void CollSdk::SetTankMass (CollH v, CollH tank, double m) { cnt.n[CSK_TANK]++; LogCall (CSK_TANK, v); DoSetTankMass (v, tank, m); }
void CollSdk::SetWarp (double w) { cnt.n[CSK_WARP]++; LogCall (CSK_WARP, nullptr); DoSetWarp (w); }

int CollSdk::WriteVtx (CollH v, CollH dm, uint32_t g, const uint16_t *idx, uint32_t n, const DentVtx *vtx)
{
	cnt.n[CSK_VTX]++; LogCall (CSK_VTX, v);
	return DoWriteVtx (dm, g, idx, n, vtx);
}

int CollSdk::SetClientMatrix (int id, CollH v, uint32_t mesh, uint32_t grp, const float m[16])
{
	cnt.n[CSK_MATRIX]++; LogCall (CSK_MATRIX, v);
	return DoSetClientMatrix (id, v, mesh, grp, m);
}

bool CollSdk::ProbeSlot (CollH v, uint32_t i) { cnt.n[CSK_PROBE]++; return DoProbe (v, i); }

bool CollSdk::Notify (CollH v, int prm, void *payload, int &reply)
{
	cnt.n[CSK_NOTICE]++; LogCall (CSK_NOTICE, v);
	return DoNotify (v, prm, payload, reply);
}

void CollSdk::Notification (int type, const char *title, const char *text)
{
	cnt.n[CSK_UI]++; LogCall (CSK_UI, nullptr);
	if (imgui) { DoNotification (type, title, text); return; }
	std::string s = std::string (title ? title : "") + ": " + (text ? text : "");
	Log (1, ("Collision notice: " + s).c_str ());
	Annotation (s.c_str (), 8.0);
}

void CollSdk::Annotation (const char *text, double holdSys)
{
	cnt.n[CSK_UI]++; LogCall (CSK_UI, nullptr);
	bool clear = !text || !*text;
	noteUntil = clear ? -1 : SysTime () + holdSys;
	DoAnnotation (clear ? "" : text);
}

void CollSdk::UiTick ()
{
	if (noteUntil < 0 || SysTime () <= noteUntil) return;
	noteUntil = -1;
	cnt.n[CSK_UI]++; LogCall (CSK_UI, nullptr);
	DoAnnotation ("");
}

int CollSdk::RegisterCmd (const char *label, const char *desc, CollCmdFn fn, void *ctx)
{
	cnt.n[CSK_UI]++; LogCall (CSK_UI, nullptr);
	int id = DoRegisterCmd (label, desc, fn, ctx);
	if (id) cnt.cmds++;
	return id;
}

void CollSdk::UnregisterCmd (int id)
{
	cnt.n[CSK_UI]++; LogCall (CSK_UI, nullptr);
	if (id) cnt.cmds--;
	DoUnregisterCmd (id);
}

bool CollSdk::OpenDialog (void *imguiDialog)
{
	cnt.n[CSK_UI]++; LogCall (CSK_UI, nullptr);
	if (!imgui) return false;
	return DoOpenDialog (imguiDialog);
}
