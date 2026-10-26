// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: Custom functions, "Export vessel to glTF" (claude/design-gltf-export.md 2, 8)

#include "Exporter.h"
#include "Snapshot.h"
#include "Orbitersdk.h"
#include <QMetaObject>
#include <QObject>
#include <algorithm>
#include <cstdlib>
#include <thread>

namespace {

std::string FileName (const std::string &vessel)
{
	std::string n;
	for (char c : vessel) n += (isalnum ((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == ' ') ? c : '_';
	if (n.empty () || n[0] == '.') n = "vessel" + n;
	return n + ".glb";
}

int Threads () { return std::max (1, (int)std::thread::hardware_concurrency () / 2); } // design 8: at most half the cores

class GltfExport : public oapi::Module {
public:
	GltfExport (void *hDLL) : Module (hDLL)
	{
		cmd = oapiRegisterCustomCmd ("Export vessel to glTF", "Saves the focus vessel with its meshes, textures and animations as a .glb file in the Exports folder.", Cmd, this);
	}

	~GltfExport ()
	{
		Join ();
		delete ctx;
		oapiUnregisterCustomCmd ((int)cmd);
	}

	void clbkSimulationStart (RenderMode mode) override
	{
		hasClient = mode != RENDER_NONE;
		frames = 0;
		const char *e = getenv ("ORBITER_GLTF_EXPORT");
		autoExport = e && *e && strcmp (e, "0");
		autoThread = autoExport && !strcmp (e, "thread"); // through the worker, as the menu does
		if (autoExport) oapiWriteLog ("GltfExport: ORBITER_GLTF_EXPORT set, the focus vessel is exported on frame 3");
		session++;
		if (!ctx) ctx = new QObject;
	}

	void clbkSimulationEnd () override
	{
		Join ();
		session++; // a result still queued is dropped
	}

	void clbkPreStep (double, double, double) override
	{
		if (autoExport && ++frames == 3) { autoExport = false; Export (autoThread); } // two renders done (design R1)
	}

private:
	static void Cmd (void *context) { ((GltfExport*)context)->Export (true); }

	void Join ()
	{
		if (worker.joinable ()) worker.join ();
		busy = false;
	}

	void Export (bool threaded)
	{
		if (busy) { oapiAddNotification (OAPINOTIF_INFO, "glTF export", "Export already running"); return; }
		VESSEL *v = oapiGetFocusInterface ();
		if (!v) return;
		GxSnapshot snap;
		std::string err;
		if (!GxTakeSnapshot (v, hasClient, snap, err)) {
			oapiWriteLogV ("GltfExport: %s: %s", v->GetName (), err.c_str ());
			oapiAddNotification (OAPINOTIF_WARNING, "glTF export", err.c_str ());
			return;
		}
		std::string out = oapiResolvePath ((std::string ("Exports\\") + FileName (snap.vesselName)).c_str ());
		oapiWriteLogV ("GltfExport: exporting %s to %s", snap.vesselName.c_str (), out.c_str ());
		GxResolver resolve = [](const std::string &p) { return oapiResolvePath (p.c_str ()); }; // thread safe: PathResolve.cpp locks its cache
		if (!threaded) { Done (GxExportFile (snap, out, resolve, Threads ())); return; }
		Join ();
		busy = true;
		int s = session;
		QObject *c = ctx;
		worker = std::thread ([this, snap = std::move (snap), out, resolve, s, c]() {
			GxResult r = GxExportFile (snap, out, resolve, Threads ());
			QMetaObject::invokeMethod (c, [this, r, s]() { if (s == session) { Done (r); Join (); } }, Qt::QueuedConnection);
		});
	}

	void Done (const GxResult &r)
	{
		for (const std::string &w : r.warnings) oapiWriteLogV ("GltfExport: warning: %s", w.c_str ());
		if (!r.ok) {
			oapiWriteLogV ("GltfExport: failed: %s", r.error.c_str ());
			oapiAddNotification (OAPINOTIF_ERROR, "glTF export failed", r.error.c_str ());
			return;
		}
		std::string name = r.path.substr (r.path.find_last_of ("/\\") + 1);
		char msg[512];
		snprintf (msg, sizeof msg, "Exports/%s: %d parts, %d animations, %d images%s", name.c_str (), r.parts, r.anims, r.images,
			r.warnings.empty () ? "" : (", " + std::to_string (r.warnings.size ()) + (r.warnings.size () == 1 ? " warning" : " warnings") + " in Orbiter.log").c_str ());
		oapiWriteLogV ("GltfExport: done: %s", msg);
		oapiAddNotification (r.warnings.empty () ? OAPINOTIF_SUCCESS : OAPINOTIF_WARNING, "glTF export", msg);
	}

	DWORD cmd = 0;
	QObject *ctx = nullptr;
	std::thread worker;
	bool busy = false, hasClient = true, autoExport = false, autoThread = false;
	int frames = 0, session = 0;
};

GltfExport *g_module = nullptr;

}

DLLCLBK void InitModule (void *hDLL)
{
	g_module = new GltfExport (hDLL);
	oapiRegisterModule (g_module);
}

DLLCLBK void ExitModule (void *hDLL)
{
	delete g_module;
	g_module = nullptr;
}
