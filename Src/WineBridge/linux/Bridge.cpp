// WineBridge: the Linux side of the channel to the helper (Orbiter.exe under Wine)
#include "Orbitersdk.h"
#include "Bridge.h"
#include "WbVessel.h"
#include "Manual.h"
#include "gen_calls.h"
#include <QTimer>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <immintrin.h>
#include <poll.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
void WbServe (uint32_t fn, WbReader &r, WbWriter &w); // Serve.cpp

using Clock = std::chrono::steady_clock;
static double Seconds (Clock::duration d) { return std::chrono::duration<double> (d).count (); }

WbBridge &WbBridge::Get ()
{
	static WbBridge *b = new WbBridge; // never destroyed: ExitModule may run at exit, after static objects are gone
	return *b;
}

static std::string WinPath (const std::string &p)
{
	std::string s = p[0] == '/' ? "Z:" + p : p;
	for (char &c : s) if (c == '/') c = '\\';
	return s;
}

// Modules/WineBridge/Orbiter.exe, next to this module
static std::string HelperExe ()
{
	Dl_info info;
	if (!dladdr ((void *)&HelperExe, &info) || !info.dli_fname) return "";
	std::string so = info.dli_fname;
	size_t slash = so.rfind ('/');
	return (slash == std::string::npos ? std::string (".") : so.substr (0, slash)) + "/WineBridge/Orbiter.exe";
}

// a helper that ended before it answered: Wine's reasons from its log (a missing 32-bit Wine shows only there), and what it needs
static void WineSaid (const std::string &log)
{
	if (FILE *f = fopen (log.c_str (), "r")) {
		char line[512];
		for (int n = 0; n < 3 && fgets (line, sizeof line, f);) {
			line[strcspn (line, "\r\n")] = 0;
			if (strstr (line, "fail") || strstr (line, "missing") || strstr (line, "could not")) { oapiWriteLogV ("WineBridge: Wine: %s", line); n++; }
		}
		fclose (f);
	}
	oapiWriteLog ((char *)"WineBridge: the helper is a 32-bit Windows program: Wine has to run 32-bit programs (Doc/WineBridge/README.txt)");
}

bool WbBridge::Ready ()
{
	if (m_alive) return true;
	if (m_tried) return false; // one try per session: a failure is logged once
	bool ok = Start ();
	m_tried = true; // a failed start stopped what it had made, and stays failed until a stop that ends a running helper
	return ok;
}

bool WbBridge::Start ()
{
	std::string exe = HelperExe ();
	if (exe.empty () || access (exe.c_str (), R_OK)) {
		oapiWriteLogV ("WineBridge: no helper at %s", exe.c_str ());
		return false;
	}
	char cwd[4096];
	if (!getcwd (cwd, sizeof cwd)) return false;
	const char *pf = getenv ("ORBITER_WB_PREFIX");
	std::string prefix = pf && *pf ? pf : std::string (cwd) + "/WineBridge/prefix";
	const char *wv = getenv ("ORBITER_WB_WINE");
	std::string wine = wv && *wv ? wv : "wine";
	bool fresh = access (prefix.c_str (), F_OK) != 0;
	if (fresh) mkdir ((std::string (cwd) + "/WineBridge").c_str (), 0755);
	// a prefix made by a Wine without its 32-bit part keeps an empty syswow64 after that part is installed: without the timestamp of
	// its last update, Wine updates it and fills it
	std::string wow = prefix + "/drive_c/windows/syswow64";
	if (!fresh && access (wow.c_str (), F_OK) == 0 && access ((wow + "/kernel32.dll").c_str (), F_OK) != 0
		&& unlink ((prefix + "/.update-timestamp").c_str ()) == 0)
		oapiWriteLog ((char *)"WineBridge: the Wine prefix has no 32-bit part: Wine updates it");

	m_shm = "/dev/shm/orbiter-wb-" + std::to_string (getpid ());
	std::string hf = m_shm + ".h", lf = m_shm + ".l";
	unlink (m_shm.c_str ()); unlink (hf.c_str ()); unlink (lf.c_str ());
	int fd = open (m_shm.c_str (), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0 || ftruncate (fd, WB_SIZE)) { oapiWriteLogV ("WineBridge: can't make %s: %s", m_shm.c_str (), strerror (errno)); if (fd >= 0) close (fd); return false; }
	void *m = mmap (nullptr, WB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close (fd);
	if (m == MAP_FAILED) { oapiWriteLogV ("WineBridge: can't map %s: %s", m_shm.c_str (), strerror (errno)); unlink (m_shm.c_str ()); return false; }
	H = (WbHeader *)m;
	H->magic = WB_MAGIC;
	H->version = WB_VERSION;
	H->turn = WB_LINUX;
	if (mkfifo (hf.c_str (), 0600) || mkfifo (lf.c_str (), 0600)
		|| (m_lin = open (lf.c_str (), O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0
		|| (m_hout = open (hf.c_str (), O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0) {
		oapiWriteLogV ("WineBridge: can't make the FIFOs: %s", strerror (errno));
		Stop ();
		return false;
	}

	// the helper's environment: Orbiter's, without what is meant for Orbiter's own libraries; the display for the modules' dialogs, but
	// for an Orbiter that runs offscreen
	std::vector<std::string> env;
	const char *qpa = getenv ("QT_QPA_PLATFORM");
	bool offscreen = qpa && !strcmp (qpa, "offscreen");
	static const char *drop[] = {"LD_LIBRARY_PATH=", "LD_PRELOAD=", "WINEPREFIX=", "WINEDEBUG=", "WINEDLLOVERRIDES=", "DISPLAY=", "WAYLAND_DISPLAY="};
	for (char **e = environ; *e; e++) {
		bool keep = true;
		for (size_t i = 0; i < sizeof drop / sizeof *drop; i++)
			if ((i < 5 || offscreen) && !strncmp (*e, drop[i], strlen (drop[i]))) keep = false;
		if (keep) env.push_back (*e);
	}
	env.push_back ("WINEPREFIX=" + prefix);
	const char *wd = getenv ("ORBITER_WB_WINEDEBUG"); // Wine's channels for the helper, to look into a Windows module (README)
	env.push_back (std::string ("WINEDEBUG=") + (wd && *wd ? wd : "-all,err+module"));
	env.push_back ("WINEDLLOVERRIDES=mscoree=;mshtml=");
	std::vector<char *> envp;
	for (auto &s : env) envp.push_back ((char *)s.c_str ());
	envp.push_back (nullptr);
	std::string a1 = WinPath (m_shm), a2 = WinPath (hf), a3 = WinPath (lf);
	char *argv[] = {(char *)wine.c_str (), (char *)exe.c_str (), (char *)a1.c_str (), (char *)a2.c_str (), (char *)a3.c_str (), nullptr};
	std::string log = std::string (cwd) + "/WineBridge.log";

	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_addopen (&fa, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen (&fa, 1, log.c_str (), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	posix_spawn_file_actions_adddup2 (&fa, 1, 2);
	posix_spawnattr_init (&at);
	posix_spawnattr_setflags (&at, POSIX_SPAWN_SETSID);
	int err = posix_spawnp (&m_pid, wine.c_str (), &fa, &at, argv, envp.data ());
	posix_spawn_file_actions_destroy (&fa);
	posix_spawnattr_destroy (&at);
	if (err) {
		m_pid = 0;
		oapiWriteLogV ("WineBridge: can't start %s: %s (is Wine installed?)", wine.c_str (), strerror (err));
		Stop ();
		return false;
	}
	oapiWriteLogV ("WineBridge: helper started (pid %d), Wine prefix %s%s", (int)m_pid, prefix.c_str (),
		fresh ? ", being created: the first start takes a while" : "");

	auto t0 = Clock::now ();
	m_alive = true;
	m_session++;
	m_sound = -1;
	m_hooks = 0;
	m_deadline = t0 + std::chrono::seconds (120);
	WbWriter &w = Begin (H_HELLO);
	w.u32 (WB_VERSION); w.u32 (WB_GEN_HASH);
	WbReader r = Call ();
	m_deadline = {};
	bool ok = m_alive && !r.bad && r.i32 () == 1;
	if (!ok) {
		bool early = !m_alive;
		if (m_alive) Dead ("it is another version");
		Stop ();
		if (early) WineSaid (log);
		return false;
	}
	unlink (m_shm.c_str ()); unlink (hf.c_str ()); unlink (lf.c_str ()); // both sides have them open: nothing stays in /dev/shm
	m_lastReport = Clock::now ();
	const char *prof = getenv ("ORBITER_WB_PROFILE");
	m_profile = prof && *prof && strcmp (prof, "0");
	oapiWriteLogV ("WineBridge: helper ready after %.0f ms%s", Seconds (Clock::now () - t0) * 1e3, m_profile ? ", calls profiled" : "");
	if (m_dlgMade) Dialogs (WD_MADE, 0, nullptr); // a simulation runs: its modules' dialogs may open
	QTimer *pump = new QTimer;
	QObject::connect (pump, &QTimer::timeout, [] { WbBridge::Get ().Pump (); });
	pump->start (15);
	m_pumpTimer = pump;
	return true;
}

// the helper's messages: its dialogs answer between Orbiter's frames, in the Launchpad and paused too; never inside a call
void WbBridge::Pump ()
{
	std::lock_guard<std::recursive_mutex> g (m_lock);
	if (!m_alive || m_depth) return;
	uint64_t to = m_toHelper;
	double busy = m_busy;
	Begin (H_PUMP);
	Call ();
	m_toHelper = to; // apart from the calls a frame
	m_busy = busy;
	m_pumps++;
}

void WbBridge::Dialogs (int what, uint32_t msg, void *data)
{
	std::lock_guard<std::recursive_mutex> g (m_lock);
	if (what == WD_MADE) m_dlgMade = true;
	if (what == WD_DELETED) { m_dlgMade = false; WbXrsSessionEnd (); } // the session closes: XRSound's simulation end follows
	if (!m_alive) return;
	uint32_t d = 0;
	if (what == WD_MESSAGE) d = data && oapiIsVessel ((OBJHANDLE)data) ? Id (data) : (uint32_t)(uintptr_t)data; // a vessel, or MSG_PAUSE's flag
	WbWriter &w = Begin (H_DIALOGS);
	w.i32 (what); w.u32 (msg); w.u32 (d);
	Call ();
}

// from the core's dialog manager (not upstream: dll-modules)
DLLCLBK void wbDialogs (int what, unsigned msg, void *data)
{
	WbBridge::Get ().Dialogs (what, msg, data);
}

void WbBridge::Dead (const char *why)
{
	if (!m_alive) return;
	m_alive = false;
	oapiWriteLogV ("WineBridge: the helper stopped (%s); bridged vessels go on without their module. See WineBridge.log", why);
	if (m_pid > 0) {
		kill (-m_pid, SIGKILL);
		waitpid (m_pid, nullptr, 0);
		m_pid = 0;
	}
}

void WbBridge::Pass ()
{
	H->slen = 0;
	WbStore (&H->turn, WB_HELPER);
	if (WbLoad (&H->sleeping[WB_HELPER])) {
		char b = 1;
		if (write (m_hout, &b, 1) < 0) {} // a full FIFO already wakes it
	}
}

// until it is our turn: spin for WB_SPIN_NS, then sleep in our FIFO, looking at the helper every 100 ms
bool WbBridge::Wait ()
{
	auto t0 = Clock::now ();
	for (unsigned i = 1;; i++) {
		if (WbLoad (&H->turn) == WB_LINUX) return true;
		_mm_pause ();
		if (!(i & 255) && Clock::now () - t0 > std::chrono::nanoseconds (WB_SPIN_NS)) break;
	}
	m_slept++;
	for (;;) {
		WbStore (&H->sleeping[WB_LINUX], 1);
		if (WbLoad (&H->turn) == WB_LINUX) { WbStore (&H->sleeping[WB_LINUX], 0); return true; }
		pollfd p = {m_lin, POLLIN, 0};
		if (poll (&p, 1, 100) > 0) {
			char buf[64];
			while (read (m_lin, buf, sizeof buf) > 0) {}
		}
		WbStore (&H->sleeping[WB_LINUX], 0);
		if (WbLoad (&H->turn) == WB_LINUX) return true;
		int st;
		if (m_pid > 0 && waitpid (m_pid, &st, WNOHANG) == m_pid) {
			m_pid = 0;
			char why[64];
			if (WIFEXITED (st)) snprintf (why, sizeof why, "exit code %d", WEXITSTATUS (st));
			else snprintf (why, sizeof why, "signal %d", WTERMSIG (st));
			Dead (why);
			return false;
		}
		if (m_deadline != Clock::time_point () && Clock::now () > m_deadline) { Dead ("no answer within 120 s"); return false; }
	}
}

WbWriter &WbBridge::Begin (uint32_t fn)
{
	m_w = H ? WbWriter (WbMsg (H), WB_MSG_MAX) : WbWriter (nullptr, 0);
	if (H) H->fn = fn;
	return m_w;
}

WbReader WbBridge::Call ()
{
	if (!m_alive) return WbReader ();
	if (m_w.over) { Dead ("a call larger than the channel"); return WbReader (); }
	bool outer = m_depth++ == 0;
	auto t0 = Clock::now ();
	H->kind = WB_CALL;
	H->len = (uint32_t)(m_w.p - WbMsg (H));
	uint32_t key = 0;
	if (m_profile) { // a callback by its number
		key = H->fn;
		if (H->fn == H_CLBK && H->len >= 8) { uint32_t c; memcpy (&c, WbMsg (H) + 4, 4); key = 0x10000u | c; }
		m_nested.push_back (0);
	}
	m_toHelper++;
	Pass ();
	WbReader res;
	while (Wait ()) {
		if (H->len > WB_MSG_MAX || H->slen > WB_MSG_MAX - H->len) { Dead ("a message larger than the channel"); break; }
		WbApplySync (WbMsg (H) + H->len, H->slen); // the modules' memory Orbiter reads, as it is now
		if (H->kind == WB_RET) { res = WbReader (WbMsg (H), H->len); break; }
		ServeOne ();
		if (!m_alive) break;
	}
	m_depth--;
	double t = Seconds (Clock::now () - t0);
	if (outer) m_busy += t;
	if (outer && m_alive && __atomic_load_n (&H->hooks, __ATOMIC_SEQ_CST) != m_hooks) { // the reply is kept aside meanwhile, as the helper does for meshes
		std::vector<uint8_t> keep (res.bad ? res.end : res.p, res.end);
		bool bad = res.bad;
		Hooks ();
		if (!bad && m_alive) { memcpy (WbMsg (H), keep.data (), keep.size ()); res = WbReader (WbMsg (H), (uint32_t)keep.size ()); }
		else res = WbReader ();
	}
	if (m_profile) {
		Prof &p = m_prof[key];
		p.n++; p.t += t; p.own += t - m_nested.back ();
		m_nested.pop_back ();
		if (!m_nested.empty ()) m_nested.back () += t;
	}
	return res;
}

void WbBridge::Hooks ()
{
	m_hooks = __atomic_load_n (&H->hooks, __ATOMIC_SEQ_CST);
	Begin (H_HOOKS);
	WbReader r = Call ();
	if (m_alive) WbHooksApply (r);
}

void WbBridge::ServeOne ()
{
	uint32_t fn = H->fn;
	WbReader r (WbMsg (H), H->len);
	WbWriter w (WbMsg (H), WB_MSG_MAX);
	m_fromHelper++;
	auto t0 = m_profile ? Clock::now () : Clock::time_point ();
	if (m_profile) m_nested.push_back (0);
	void *frames = WbLuaRemoteBarrier ();
	WbServe (fn, r, w); // reads all its arguments before it writes or calls
	WbLuaRemoteRestore (frames);
	if (m_profile) {
		double t = Seconds (Clock::now () - t0);
		Prof &p = m_prof[0x20000u | fn];
		p.n++; p.t += t; p.own += t - m_nested.back ();
		m_nested.pop_back ();
		if (!m_nested.empty ()) m_nested.back () += t; // time in Orbiter, not in the helper
	}
	if (!m_alive) return;
	if (w.over) { Dead ("a reply larger than the channel"); return; }
	H->kind = WB_RET;
	H->fn = fn;
	H->len = (uint32_t)(w.p - WbMsg (H));
	Pass ();
}

void WbBridge::Stop ()
{
	std::lock_guard<std::recursive_mutex> g (m_lock);
	if (m_frames || m_toHelper) Report (true);
	if (m_alive) {
		Begin (H_QUIT);
		Call ();
		m_alive = false;
	}
	if (m_pid > 0) {
		for (int i = 0; i < 100 && waitpid (m_pid, nullptr, WNOHANG) == 0; i++) usleep (20000);
		if (kill (m_pid, 0) == 0) {
			kill (-m_pid, SIGTERM);
			usleep (500000);
			kill (-m_pid, SIGKILL);
			waitpid (m_pid, nullptr, 0);
		}
		m_pid = 0;
	}
	if (m_pumpTimer) { QTimer *t = (QTimer *)m_pumpTimer; t->stop (); t->deleteLater (); m_pumpTimer = nullptr; } // Stop may run inside its timeout
	if (H) { munmap (H, WB_SIZE); H = nullptr; }
	if (m_lin >= 0) close (m_lin);
	if (m_hout >= 0) close (m_hout);
	m_lin = m_hout = -1;
	if (!m_shm.empty ()) { unlink (m_shm.c_str ()); unlink ((m_shm + ".h").c_str ()); unlink ((m_shm + ".l").c_str ()); }
	m_tried = false; // a new session may start it again
	m_modules.clear ();
	m_vessels.clear ();
	WbHooksStopped ();
	WbExtraStopped ();
	WbManualClear ();
	WbDrawClear ();
	WbSoundClear ();
	m_ptr.assign (1, nullptr);
	m_id.clear ();
	m_toHelper = m_fromHelper = m_slept = m_frames = m_frames0 = m_pumps = 0;
	m_busy = 0;
	m_setup = m_mark = {};
	m_lastSimt = -1e300;
}

uint32_t WbBridge::Id (const void *p)
{
	if (!p) return 0;
	auto it = m_id.find (p);
	if (it != m_id.end ()) return it->second;
	uint32_t id = (uint32_t)m_ptr.size ();
	m_ptr.push_back (p);
	m_id[p] = id;
	return id;
}

WbVessel *WbBridge::Bridged (uint32_t id) const
{
	auto it = m_vessels.find (id);
	return it == m_vessels.end () ? nullptr : it->second;
}

VESSEL *WbBridge::Vessel (uint32_t id) const
{
	if (WbVessel *v = Bridged (id)) return v;
	OBJHANDLE h = (OBJHANDLE)Ptr (id);
	return h && oapiIsVessel (h) ? oapiGetVesselInterface (h) : nullptr;
}

int WbBridge::Load (const std::string &dll)
{
	auto it = m_modules.find (dll);
	if (it != m_modules.end ()) { // a failure is remembered as 0 and logged once
		if (it->second.number) it->second.opens++;
		return it->second.number;
	}
	if (access (dll.c_str (), R_OK)) {
		oapiWriteLogV ("WineBridge: %s: %s", dll.c_str (), strerror (errno));
		m_modules[dll] = Module ();
		return 0;
	}
	if (!Ready ()) return 0; // the helper starts when a module is there to load
	bool sound = WbSoundPresent (); // XRSound may load after the first Windows module (the plugins' order)
	if ((int)sound != m_sound) oapiWriteLogV ("WineBridge: %s", sound ? "OrbiterSound on XRSound" : "no XRSound: no OrbiterSound");
	m_sound = sound;
	WbWriter &w = Begin (H_LOAD);
	w.str (WinPath (dll).c_str ()); w.b (sound); // with XRSound the helper has loaded its OrbiterSound.dll before the module
	WbReader r = Call ();
	Module m;
	m.number = r.i32 ();
	uint32_t n;
	const char *e = r.str (&n);
	std::string err = e ? std::string (e, n) : "";
	m.exports = r.u32 ();
	m.version = r.i32 ();
	const char *d = r.str (&n);
	if (d) m.date.assign (d, n);
	if ((d = r.str (&n))) m.model.assign (d, n); // an atmosphere module's ModelName and ModelDesc
	if ((d = r.str (&n))) m.desc.assign (d, n);
	if (r.bad || !m_alive) return 0; // Dead already logged it
	if (!m.number) oapiWriteLogV ("WineBridge: %s not loaded: %s", dll.c_str (), err.c_str ());
	m.opens = m.number ? 1 : 0;
	m_modules[dll] = m;
	return m.number;
}

const WbBridge::Module *WbBridge::Info (int module) const
{
	for (auto &m : m_modules) if (module && m.second.number == module) return &m.second;
	return nullptr;
}

void WbBridge::Free (int module)
{
	for (auto it = m_modules.begin (); module && it != m_modules.end (); ++it) {
		if (it->second.number != module) continue;
		if (--it->second.opens > 0) return;
		if (m_alive) { WbWriter &w = Begin (H_FREE); w.i32 (module); Call (); }
		WbModuleObjectsFreed (module); // Orbiter holds them no more
		WbCelbodyFreed (module);
		WbExtraFreed (module);
		m_modules.erase (it);
		break;
	}
	StopUnused ();
}

void WbBridge::StopUnused ()
{
	for (auto &m : m_modules) if (m.second.opens > 0) return;
	if (m_alive || H) Stop (); // a helper that died is cleaned up too
}

void WbBridge::Frame (double simt)
{
	if (simt == m_lastSimt) return;
	m_lastSimt = simt;
	if (!m_frames) { // what came before the first frame is setup: the helper, the modules, the class caps
		m_setup = {m_toHelper, m_fromHelper, m_slept, m_busy};
		m_mark = m_setup;
		m_lastReport = Clock::now ();
	}
	m_frames++;
	if (m_alive && WbMeshCopies ()) { // the modules' changed mesh copies go back
		Begin (H_FRAME);
		Call ();
	}
	if (m_alive) WbLuaFlush (); // the blocks of the modules' userdata Lua collected
	if (Clock::now () - m_lastReport > std::chrono::seconds (10)) Report (false);
}

// the frames since the last report, or for the session total all frames, setup apart
void WbBridge::Report (bool final)
{
	if (!m_frames) m_setup = {m_toHelper, m_fromHelper, m_slept, m_busy};
	Counts &from = final ? m_setup : m_mark;
	if (final) oapiWriteLogV ("WineBridge: setup: %llu calls to the helper and %llu from it, %.0f ms",
		(unsigned long long)m_setup.to, (unsigned long long)m_setup.from, m_setup.busy * 1e3);
	if (final && m_pumps) oapiWriteLogV ("WineBridge: %llu message pumps for the modules' windows", (unsigned long long)m_pumps);
	uint64_t f = m_frames - (final ? 0 : m_frames0);
	double pf = f ? 1.0 / f : 0;
	if (f) oapiWriteLogV ("WineBridge: %s%llu frames: %.2f calls to the helper and %.2f from it a frame, %.1f us a frame in bridged calls, %llu waits slept",
		final ? "session total, " : "", (unsigned long long)f, (m_toHelper - from.to) * pf, (m_fromHelper - from.from) * pf,
		(m_busy - from.busy) * 1e6 * pf, (unsigned long long)(m_slept - from.slept));
	m_mark = {m_toHelper, m_fromHelper, m_slept, m_busy};
	m_frames0 = m_frames;
	m_lastReport = Clock::now ();
	if (m_profile && f) Profile ();
}

const char *WbGenName (uint32_t fn); // gen_serve.cpp

const char *WbClbkName (int c)
{
	static const char *cname[] = {"SetClassCaps", "SaveState", "LoadStateEx", "SetStateEx", "PostCreation", "FocusChanged", "PreStep",
		"PostStep", "PlaybackEvent", "VisualCreated", "VisualDestroyed", "DrawHUD2", "RCSMode", "ADCtrlMode", "HUDMode", "MFDMode",
		"NavMode", "DockEvent", "Animate", "ConsumeDirectKey", "ConsumeBufferedKey", "LoadGenericCockpit", "LoadPanel",
		"PanelMouseEvent2", "PanelRedrawEvent2", "LoadVC", "VCMouseEvent", "VCRedrawEvent", "PanelMouseEvent3", "PanelRedrawEvent3",
		"Generic", "LoadPanel2D", "DrawHUD3", "RenderHUD", "GetRadiationForce", "NavProcess"};
	static_assert (sizeof cname / sizeof *cname == C_COUNT, "a name per callback");
	return c >= 0 && c < C_COUNT ? cname[c] : "?";
}

// the calls that took the most time since the last report
void WbBridge::Profile ()
{
	static const char *hname[] = {"", "HELLO", "LOAD", "OVCINIT", "OVCEXIT", "CLBK", "AIRFOIL", "QUIT", "LIFT", "INPUTBOX", "FRAME", "FREE", "OPC",
		"MFDOPEN", "MFDCLOSED", "MFD", "MODULE", "EMFD", "HOOKS", "PROXY", "VESSELGONE", "CELBODY", "LPAD", "CUSTOMCMD", "ECC", "ECCGONE",
		"MESHLOADED", "LUACALL", "LUAHOOK", "LUAFREE", "PUMP", "DIALOGS"};
	static_assert (sizeof hname / sizeof *hname == H_DIALOGS + 1, "a name per helper call");
	std::vector<std::pair<uint32_t, Prof>> v (m_prof.begin (), m_prof.end ());
	std::sort (v.begin (), v.end (), [] (auto &a, auto &b) { return a.second.t > b.second.t; });
	for (size_t i = 0; i < v.size () && i < 12; i++) {
		uint32_t k = v[i].first, fn = k & 0xffff;
		char name[96];
		if (k & 0x20000u) {
			const char *g = WbGenName (fn);
			if (g) snprintf (name, sizeof name, "Orbiter %s", g); else snprintf (name, sizeof name, "Orbiter call %u", fn);
		} else if (k & 0x10000u) snprintf (name, sizeof name, "helper clbk%s", WbClbkName ((int)fn));
		else snprintf (name, sizeof name, "helper %s", fn < sizeof hname / sizeof *hname ? hname[fn] : "?");
		const Prof &p = v[i].second;
		oapiWriteLogV ("WineBridge profile: %-40s %8llu calls %9.2f ms, own %9.2f ms, %.1f us a call", name, (unsigned long long)p.n,
			p.t * 1e3, p.own * 1e3, p.n ? p.t * 1e6 / p.n : 0.0);
	}
	m_prof.clear ();
}
