// WineBridge helper: the shared memory channel and its FIFOs, the helper's side
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <intrin.h>
#include "wb_helper.h"

static WbHeader *H;
static HANDLE g_in, g_out; // our FIFO (read), Linux's FIFO (write)
static WbWriter g_w (0, 0);
static double g_tick; // QueryPerformanceCounter ticks per ns

bool wb::Open (const char *shm, const char *fifoIn, const char *fifoOut)
{
	HANDLE f = CreateFileA (shm, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (f == INVALID_HANDLE_VALUE) { fprintf (stderr, "WineBridge helper: can't open %s (%lu)\n", shm, GetLastError ()); return false; }
	HANDLE m = CreateFileMappingA (f, 0, PAGE_READWRITE, 0, WB_SIZE, 0);
	H = m ? (WbHeader *)MapViewOfFile (m, FILE_MAP_ALL_ACCESS, 0, 0, WB_SIZE) : 0;
	if (!H) { fprintf (stderr, "WineBridge helper: can't map %s (%lu)\n", shm, GetLastError ()); return false; }
	g_in = CreateFileA (fifoIn, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	g_out = CreateFileA (fifoOut, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (g_in == INVALID_HANDLE_VALUE || g_out == INVALID_HANDLE_VALUE) { fprintf (stderr, "WineBridge helper: can't open the FIFOs (%lu)\n", GetLastError ()); return false; }
	if (H->magic != WB_MAGIC) { fprintf (stderr, "WineBridge helper: not a WineBridge channel\n"); return false; }
	LARGE_INTEGER q;
	QueryPerformanceFrequency (&q);
	g_tick = (double)q.QuadPart / 1e9;
	return true;
}

static void Pass ()
{
	H->slen = wb::LiveSync (WbMsg (H) + H->len, WB_MSG_MAX - H->len); // whenever Orbiter's code runs, its copies are current
	WbStore (&H->turn, WB_LINUX);
	if (WbLoad (&H->sleeping[WB_LINUX])) {
		char b = 1; DWORD n;
		WriteFile (g_out, &b, 1, &n, 0);
	}
}

// until it is our turn: spin for WB_SPIN_NS, then sleep in our FIFO; Linux gone (end of file) ends the helper
static void Wait ()
{
	LARGE_INTEGER t0, t;
	QueryPerformanceCounter (&t0);
	const LONGLONG limit = (LONGLONG)(WB_SPIN_NS * g_tick);
	for (unsigned i = 1;; i++) {
		if (WbLoad (&H->turn) == WB_HELPER) return;
		_mm_pause ();
		if (!(i & 255)) {
			QueryPerformanceCounter (&t);
			if (t.QuadPart - t0.QuadPart > limit) break;
		}
	}
	for (;;) {
		WbStore (&H->sleeping[WB_HELPER], 1);
		if (WbLoad (&H->turn) == WB_HELPER) { WbStore (&H->sleeping[WB_HELPER], 0); return; }
		char b; DWORD n = 0;
		if (!ReadFile (g_in, &b, 1, &n, 0) || n == 0) ExitProcess (0);
		WbStore (&H->sleeping[WB_HELPER], 0);
		if (WbLoad (&H->turn) == WB_HELPER) return;
	}
}

// top: a call of Linux's outermost, whose reply ends what the modules did for it
static void ServeOne (bool top)
{
	uint32_t fn = H->fn;
	if (top && (wb::AnimsCopied () || wb::VisualsWanted ())) { // what a module reads of Orbiter's (animation copies, visual slots) first; their calls reuse the buffer
		uint8_t *keep = (uint8_t *)malloc (H->len ? H->len : 1);
		uint32_t len = H->len;
		memcpy (keep, WbMsg (H), len);
		if (wb::AnimsCopied ()) wb::AnimsRefresh ();
		if (wb::VisualsWanted ()) wb::VisualsRefresh ();
		memcpy (WbMsg (H), keep, len);
		H->fn = fn; H->len = len;
		free (keep);
	}
	WbReader r (WbMsg (H), H->len);
	WbWriter w (WbMsg (H), WB_MSG_MAX);
	void *frames = wb::LuaBarrier ();
	wb::Dispatch (fn, r, w); // reads all its arguments before it writes or calls
	wb::LuaRestore (frames);
	if (top) wb::LuaRelease ();
	uint32_t len = (uint32_t)(w.p - WbMsg (H));
	if (wb::MeshRecent () || wb::LightsChanged () || wb::WatchChanged ()) { // mesh copies fetched in this call and lights changed in it go first; their calls reuse the buffer
		uint8_t *keep = (uint8_t *)malloc (len ? len : 1);
		memcpy (keep, WbMsg (H), len);
		wb::MeshStoreRecent ();
		wb::LightsStore ();
		wb::WatchStore ();
		memcpy (WbMsg (H), keep, len);
		free (keep);
	}
	H->kind = WB_RET;
	H->fn = fn;
	H->len = len;
	if (top && wb::HooksScan ()) H->hooks++; // a vessel vtable a module patched: Linux asks H_HOOKS (not for a patch undone within the call)
	Pass ();
}

WbWriter &wb::Begin (uint32_t fn)
{
	H->fn = fn;
	g_w = WbWriter (WbMsg (H), WB_MSG_MAX);
	return g_w;
}

WbReader wb::Call ()
{
	H->kind = WB_CALL;
	H->len = (uint32_t)(g_w.p - WbMsg (H));
	Pass ();
	for (;;) {
		Wait ();
		if (H->kind == WB_RET) return WbReader (WbMsg (H), H->len);
		ServeOne (false);
	}
}

void wb::Serve ()
{
	for (;;) {
		Wait ();
		if (H->kind != WB_CALL) { fprintf (stderr, "WineBridge helper: a reply with no call\n"); ExitProcess (2); }
		bool quit = H->fn == H_QUIT;
		ServeOne (true);
		if (quit) return;
	}
}

void wb::CountLuaFree () { __atomic_add_fetch (&H->lfree, 1, __ATOMIC_SEQ_CST); }

void wb::OutStr (WbReader &r, char *buf, int n)
{
	uint32_t len;
	const char *s = r.str (&len);
	if (!buf || n <= 0) return;
	if (!s) { buf[0] = 0; return; }
	uint32_t k = len < (uint32_t)(n - 1) ? len : (uint32_t)(n - 1);
	memcpy (buf, s, k);
	buf[k] = 0;
}

void wb::InStr (WbWriter &w, const char *buf, int n)
{
	uint32_t k = 0;
	while (buf && (int)k < n - 1 && buf[k]) k++;
	w.u32 (buf ? k : 0xffffffffu);
	if (buf) w.raw (buf, k);
}

void wb::OutStrIf (WbReader &r, char *buf, int n)
{
	if (r.b ()) OutStr (r, buf, n);
}

void wb::Log (const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start (ap, fmt);
	_vsnprintf (buf, sizeof buf - 1, fmt, ap);
	va_end (ap);
	buf[sizeof buf - 1] = 0;
	WbWriter &w = Begin (L_LOG);
	w.str (buf);
	Call ();
}
