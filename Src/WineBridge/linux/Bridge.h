// WineBridge: the Linux side of the channel to the helper (Orbiter.exe under Wine)
#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/types.h>
#include "../proto/wb_proto.h"

class VESSEL;
class WbVessel;

class WbBridge {
public:
	static WbBridge &Get ();

	// the helper: started on first use, once per Orbiter process
	bool Ready ();
	void Stop ();
	bool Alive () const { return m_alive; }
	uint64_t Session () const { return m_session; } // the helper's start: objects of an earlier one are gone

	// one call: lock, Begin writes the arguments, Call sends them and serves the helper's calls until the reply
	std::recursive_mutex &Lock () { return m_lock; }
	WbWriter &Begin (uint32_t fn);
	WbReader Call ();

	// handles crossing as ids (0 is null); the same pointer keeps its id for the session
	uint32_t Id (const void *p);
	void *Ptr (uint32_t id) const { return id < m_ptr.size () ? (void *)m_ptr[id] : nullptr; }
	uint32_t IdIf (const void *p) const { auto it = m_id.find (p); return p && it != m_id.end () ? it->second : 0; } // 0: none given
	uint32_t Ids () const { return (uint32_t)m_ptr.size (); } // the ids given are below
	uint32_t LuaFreeCount () const { return H ? __atomic_load_n (&H->lfree, __ATOMIC_SEQ_CST) : 0; } // the helper's, H_LUAFREE

	// bridged vessels by their id, and any vessel by id
	void AddVessel (uint32_t id, WbVessel *v) { m_vessels[id] = v; }
	void DropVessel (uint32_t id) { m_vessels.erase (id); }
	WbVessel *Bridged (uint32_t id) const;
	VESSEL *Vessel (uint32_t id) const;

	// a Windows module, opened once more: its number, 0 on failure (logged once a session); Free closes one open, the last one
	// frees it in the helper, and the helper stops when no module is open
	struct Module { int number = 0, opens = 0, version = 0; uint32_t exports = 0; std::string date, model, desc; };
	int Load (const std::string &dll);
	void Free (int module);
	const Module *Info (int module) const;
	void StopUnused (); // the helper, when no module is open
	void NoVessel (uint32_t fn, uint32_t vid); // a call naming no vessel, logged once per call (Serve.cpp)
	void BadArgs (uint32_t fn);                // a call with bad arguments, logged once per call
	void Frame (double simt);  // from each bridged vessel's PreStep
	void Pump ();              // the helper's window messages (its dialogs), from the timer when no call is under way
	void Dialogs (int what, uint32_t msg, void *data); // Orbiter's dialog manager (WbDlgWhat)
	void Report (bool final);

private:
	WbBridge () {}
	bool Start ();
	bool Wait ();
	void Pass ();
	void ServeOne ();
	void Dead (const char *why);
	void Hooks (); // a module patched a vessel vtable: what is hooked now (H_HOOKS)

	std::recursive_mutex m_lock;
	WbHeader *H = nullptr;
	WbWriter m_w {nullptr, 0};
	int m_lin = -1, m_hout = -1; // our FIFO (read), the helper's FIFO (write)
	pid_t m_pid = 0;
	bool m_tried = false, m_alive = false;
	uint64_t m_session = 0;
	uint32_t m_hooks = 0; // the helper's count of vtable changes Linux has asked about
	int m_sound = -1; // XRSound was there at the last load (-1: no load yet)
	std::string m_shm;
	std::vector<const void *> m_ptr {nullptr};
	std::unordered_map<const void *, uint32_t> m_id;
	std::unordered_map<uint32_t, WbVessel *> m_vessels;
	std::map<std::string, Module> m_modules; // by path; a failure stays as number 0 until the helper stops

	// counts
	struct Counts { uint64_t to, from, slept; double busy; };
	int m_depth = 0;
	uint64_t m_pumps = 0;    // not counted as calls to the helper
	bool m_dlgMade = false;  // Orbiter's dialog manager is there: told to a helper that starts later
	void *m_pumpTimer = nullptr; // QTimer
	uint64_t m_toHelper = 0, m_fromHelper = 0, m_slept = 0, m_frames = 0, m_frames0 = 0;
	double m_busy = 0; // seconds inside the outermost calls
	Counts m_setup {}, m_mark {}; // at the first frame, at the last report
	// ORBITER_WB_PROFILE=1: time by call, logged with each report (own: in the helper, or in Orbiter for its calls)
	struct Prof { uint64_t n = 0; double t = 0, own = 0; };
	bool m_profile = false;
	std::unordered_map<uint32_t, Prof> m_prof; // callbacks 0x10000|c, calls served 0x20000|fn, others by fn
	std::vector<double> m_nested;
	void Profile ();
	double m_lastSimt = -1e300;
	std::chrono::steady_clock::time_point m_lastReport, m_deadline;
};
