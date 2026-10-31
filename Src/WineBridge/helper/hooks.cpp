// WineBridge helper: vessel vtables a module may patch (as VesselHooking.dll does): the classes of the modules' vessel objects,
// and a copy of the Proxy vtable per Linux vessel class, so a patch reaches that class only, as a DLL's vtable on Windows
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"

namespace {
	struct Watch { void **vt; int n; void *seen[C_COUNT]; uint32_t cls; }; // cls: a Linux class's copy; 0: a module's class
	Watch *g_watch;
	int g_nwatch, g_capwatch;
	bool g_dirty; // a change is waiting for H_HOOKS

	// the copies live in the helper's image, where VirtualProtect takes WRITECOPY as on a DLL's vtable; each has the locator
	// before the slots, and room after them for a patcher that protects one entry more
	const int NCOPY = 256, COPYLEN = 1 + C_COUNT + 8;
	void *g_copy[NCOPY][COPYLEN];
	uint32_t g_copyCls[NCOPY];
	void **g_proxyVt; // the Proxy class's own

	Watch &Add ()
	{
		if (g_nwatch == g_capwatch) { g_capwatch = g_capwatch ? g_capwatch * 2 : 32; g_watch = (Watch *)realloc (g_watch, g_capwatch * sizeof *g_watch); }
		return g_watch[g_nwatch++];
	}

	void Drop (int i)
	{
		g_watch[i] = g_watch[--g_nwatch];
	}

	void ProxyMask (void **vt, uint32_t mask[2])
	{
		mask[0] = mask[1] = 0;
		for (int c = 0; c < C_COUNT; c++)
			if (vt[wb::Slot (c)] != g_proxyVt[wb::Slot (c)]) mask[c >> 5] |= 1u << (c & 31);
	}
}

bool wb::HooksProbe (void **proxyVt)
{
	g_proxyVt = proxyVt;
	DWORD old;
	if (!VirtualProtect (g_copy, sizeof g_copy, PAGE_EXECUTE_WRITECOPY, &old)) return false;
	VirtualProtect (g_copy, sizeof g_copy, old, &old);
	return true;
}

void wb::HooksTrack (void **vt, int n)
{
	for (int i = 0; i < g_nwatch; i++) if (g_watch[i].vt == vt) return;
	Watch &w = Add ();
	w.vt = vt; w.n = n; w.cls = 0;
	memcpy (w.seen, vt, n * sizeof *vt);
}

void **wb::ProxyClass (uint32_t cls)
{
	int slot = -1;
	for (int i = 0; i < NCOPY; i++) {
		if (g_copyCls[i] == cls) return g_copy[i] + 1;
		if (!g_copyCls[i] && slot < 0) slot = i;
	}
	if (slot < 0) {
		static bool said;
		if (!said) { said = true; Log ("more than %d vessel classes at once: the others share one vtable for hooks", NCOPY); }
		return 0;
	}
	g_copyCls[slot] = cls;
	memcpy (g_copy[slot], g_proxyVt - 1, (1 + C_COUNT) * sizeof (void *));
	Watch &w = Add ();
	w.vt = g_copy[slot] + 1; w.n = C_COUNT; w.cls = cls;
	memcpy (w.seen, w.vt, C_COUNT * sizeof (void *));
	return w.vt;
}

// the Linux class has no vessel left (its module may be unloaded): its copy goes, as the DLL's patched vtable would
void wb::ClassGone (uint32_t cls)
{
	for (int i = 0; i < NCOPY; i++) {
		if (g_copyCls[i] != cls) continue;
		for (int k = 0; k < g_nwatch; k++) if (g_watch[k].vt == g_copy[i] + 1) { Drop (k); break; }
		g_copyCls[i] = 0;
		memset (g_copy[i], 0, sizeof g_copy[i]);
		return;
	}
}

void wb::HooksFreed (const void *base, size_t size)
{
	const char *b = (const char *)base;
	for (int i = g_nwatch - 1; i >= 0; i--)
		if (!g_watch[i].cls && (const char *)g_watch[i].vt >= b && (const char *)g_watch[i].vt < b + size) Drop (i);
}

// at the end of each call from Linux: a vtable that differs from what Linux was told
bool wb::HooksScan ()
{
	if (g_dirty) return false;
	for (int i = 0; i < g_nwatch; i++)
		if (memcmp (g_watch[i].vt, g_watch[i].seen, g_watch[i].n * sizeof (void *))) { g_dirty = true; return true; }
	return false;
}

void wb::ServeHooks (WbWriter &w)
{
	uint8_t *at = w.p;
	uint32_t n = 0;
	w.u32 (0);
	for (int i = 0; i < g_nwatch; i++) {
		Watch &x = g_watch[i];
		if (x.cls || !memcmp (x.vt, x.seen, x.n * sizeof (void *))) continue;
		memcpy (x.seen, x.vt, x.n * sizeof (void *));
		ObjMasks (x.vt, w, &n);
	}
	if (!w.over) memcpy (at, &n, 4);
	at = w.p;
	n = 0;
	w.u32 (0);
	for (int i = 0; i < g_nwatch; i++) {
		Watch &x = g_watch[i];
		if (!x.cls || !memcmp (x.vt, x.seen, x.n * sizeof (void *))) continue;
		memcpy (x.seen, x.vt, x.n * sizeof (void *));
		uint32_t mask[2];
		ProxyMask (x.vt, mask);
		w.u32 (x.cls); w.u32 (mask[0]); w.u32 (mask[1]);
		n++;
	}
	if (!w.over) memcpy (at, &n, 4);
	g_dirty = false;
}
