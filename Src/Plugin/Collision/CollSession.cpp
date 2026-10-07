// not upstream: collision addon, the session object (Design CA E4 11.2, 11.3); E1, E2, E3 add their parts in later phases
#include <algorithm>
#include "CollSession.h"

namespace {

double Us (CollSession::Clock::duration d) { return std::chrono::duration<double, std::micro> (d).count (); }

bool IsHeader (const std::string &l) { return l.size () > 6 && !l.compare (0, 6, "COLLA "); }

}

CollSession::CollSession (uint32_t s, const CollCfgValues &c) : serial (s), cfg (c) {}

uint32_t CollSession::IdOf (OBJHANDLE h)
{
	auto it = idOf.find (h);
	if (it != idOf.end ()) return it->second;
	uint32_t id = (uint32_t)vessel.size ();
	vessel.push_back (h);
	idOf.emplace (h, id);
	return id;
}

OBJHANDLE CollSession::Vessel (uint32_t id) const { return id < vessel.size () ? vessel[id] : nullptr; }

void CollSession::Forget (OBJHANDLE h)
{
	auto it = idOf.find (h);
	if (it == idOf.end ()) return;
	vessel[it->second] = nullptr;
	idOf.erase (it);
}

void CollSession::Load (FILEHANDLE scn)
{
	std::vector<std::string> lines;
	std::string l;
	while (CollReadLine (scn, l)) lines.push_back (l);
	if (lines.empty () || !IsHeader (lines[0])) {
		CollLogLine ("Collision: scenario block without a COLLA header ignored");
		return;
	}
	pending.swap (lines);
}

void CollSession::Save (FILEHANDLE scn)
{
	CollWriteLine (scn, "COLLA 1");
	const std::vector<std::string> &lines = started ? block : pending;
	for (size_t i = 1; i < lines.size (); i++) CollWriteLine (scn, lines[i].c_str ());
}

void CollSession::Start (int renderMode)
{
	Clock::time_point b = Clock::now ();
	uint32_t nv = CollVesselCount ();
	for (uint32_t i = 0; i < nv; i++) IdOf (CollVesselByIndex (i));
	block.swap (pending);
	pending.clear ();
	started = true;
	t.build = Us (Clock::now () - b);
	if (!block.empty ()) CollLogF ("Collision: scenario block adopted, lines=%zu", block.size ());
	CollLogF ("Collision: active version=%s session=%u model=%d response=%d check=%d damage=%d log=%d render=%d",
		COLL_ADDON_VERSION, serial, cfg.model, (int)cfg.response, (int)cfg.check, CollDamageModel (), cfg.logLevel, renderMode);
}

void CollSession::FrameBegin () { t0 = Clock::now (); }

void CollSession::FrameEnd ()
{
	double us = Us (Clock::now () - t0);
	if (!n.frames) t.firstFrame = us;
	n.frames++;
	t.prestep += us;
	t.prestepMax = std::max (t.prestepMax, us);
}

void CollSession::PostBegin () { t0 = Clock::now (); }

void CollSession::PostEnd ()
{
	t.post += Us (Clock::now () - t0);
	t.posts++;
}

void CollSession::Close ()
{
	auto u = [] (uint64_t v) { return (unsigned long long)v; };
	double f = n.frames ? (double)n.frames : 1.0, p = t.posts ? (double)t.posts : 1.0;
	CollLogF ("Collision summary: frames=%llu contacts=%llu events=%llu writes=%llu probes=%llu vtx=%llu matrix=%llu notices=%llu spec=%llu free=%llu missed=%llu",
		u (n.frames), u (n.contacts), u (n.events), u (n.writes), u (n.probes), u (n.vtx), u (n.matrix), u (n.notices), u (n.spec), u (n.free), u (n.missed));
	CollLogF ("Collision perf: prestep_mean_us=%.3f prestep_max_us=%.3f e2_us=%.3f e1_us=%.3f e3_us=%.3f post_us=%.3f build_ms=%.3f first_frame_ms=%.3f",
		t.prestep / f, t.prestepMax, t.e2 / f, t.e1 / f, t.e3 / f, t.post / p, t.build * 1e-3, t.firstFrame * 1e-3);
}
