// not upstream (head-track): the tracking loop, as linuxtrack's runloop.c, tir_driver.c and tracking.c run it (MIT, LICENSE-linuxtrack)

#include "HeadTracker.h"
#include <pthread.h>
#include <chrono>

namespace ht {

const char *StateName (TrackState s)
{
	switch (s) {
	case HT_STOPPED: return "off";
	case HT_SEARCHING: return "no TrackIR found";
	case HT_NO_ACCESS: return "no access to the TrackIR";
	case HT_BUSY: return "the TrackIR is in use by another program";
	case HT_NO_FIRMWARE: return "the TrackIR's firmware is missing";
	case HT_STARTING: return "starting";
	case HT_TRACKING: return "tracking";
	default: return "failed";
	}
}

HeadTracker::~HeadTracker ()
{
	Stop ();
}

void HeadTracker::Start (const Settings &s, const std::string &dir)
{
	{
		std::lock_guard<std::mutex> lk (mx);
		set = s, setChanged = true, fwDir = dir;
	}
	if (thread.joinable ()) return;
	stop = false;
	recenter = false;
	thread = std::thread (&HeadTracker::Run, this);
}

void HeadTracker::Stop ()
{
	if (!thread.joinable ()) return;
	{
		std::lock_guard<std::mutex> lk (mx);
		stop = true;
	}
	cv.notify_all ();
	thread.join ();
	std::lock_guard<std::mutex> lk (mx);
	st = TrackStatus ();
	havePose = false;
}

void HeadTracker::Update (const Settings &s)
{
	std::lock_guard<std::mutex> lk (mx);
	if (s.IrOn () != set.IrOn () || s.tir.irBrightness != set.tir.irBrightness || s.tir.statusLed != set.tir.statusLed ||
		s.tir.statusBrightness != set.tir.statusBrightness) reopen = true; // set at the start only
	set = s, setChanged = true;
}

TrackStatus HeadTracker::Status () const
{
	std::lock_guard<std::mutex> lk (mx);
	TrackStatus r = st;
	r.on = on;
	return r;
}

bool HeadTracker::Pose (RawPose &out, uint64_t &s) const
{
	std::lock_guard<std::mutex> lk (mx);
	if (!havePose) return false;
	out = pose, s = seq;
	return true;
}

void HeadTracker::Center ()
{
	recenter = true;
}

void HeadTracker::SetOn (bool o)
{
	on = o;
}

bool HeadTracker::Wait (int ms)
{
	std::unique_lock<std::mutex> lk (mx);
	return !cv.wait_for (lk, std::chrono::milliseconds (ms), [this] { return stop; });
}

void HeadTracker::Run ()
{
	pthread_setname_np (pthread_self (), "HeadTrack");
	TirDevice dev;
	PoseSolver solver;
	PosePost post;
	Settings cur;
	std::string dir;
	bool open = false, initRecenter = true;
	int lastThreshold = -1;
	auto t0 = std::chrono::steady_clock::now ();
	uint64_t fpsFrames = 0;
	std::vector<Blob> blobs;
	for (;;) {
		bool changed, doReopen;
		{
			std::lock_guard<std::mutex> lk (mx);
			if (stop) break;
			changed = setChanged, doReopen = reopen;
			if (changed) cur = set, dir = fwDir, setChanged = false;
			reopen = false;
		}
		if (changed && !solver.Init (cur.MakeModel ())) {
			std::lock_guard<std::mutex> lk (mx);
			st.state = HT_FAILED, st.message = "the model's dimensions make no triangle";
			havePose = false;
			if (open) dev.Close (), open = false;
			if (!Wait (2000)) break;
			continue;
		}
		if (changed) recenter = true, post.Reset ();
		if (open && doReopen) dev.Close (), open = false;
		if (!open) {
			{
				std::lock_guard<std::mutex> lk (mx);
				if (st.state != HT_SEARCHING && st.state != HT_NO_ACCESS && st.state != HT_BUSY && st.state != HT_NO_FIRMWARE) st.state = HT_STARTING;
			}
			TirParams p = cur.tir;
			p.irOn = cur.IrOn ();
			TirDevice::Result r = dev.Open (p, dir);
			std::lock_guard<std::mutex> lk (mx);
			st.model = dev.Model (), st.pid = dev.ProductId ();
			st.message = dev.Error ();
			switch (r) {
			case TirDevice::OK:
				open = true, initRecenter = true, lastThreshold = -1;
				st.state = HT_TRACKING, st.message.clear ();
				st.width = dev.Width (), st.height = dev.Height ();
				st.frames = 0, st.fps = 0;
				t0 = std::chrono::steady_clock::now (), fpsFrames = 0;
				post.Reset ();
				break;
			case TirDevice::NO_DEVICE: st.state = HT_SEARCHING; break;
			case TirDevice::NO_ACCESS: st.state = HT_NO_ACCESS; break;
			case TirDevice::BUSY: st.state = HT_BUSY; break;
			case TirDevice::NO_FIRMWARE: st.state = HT_NO_FIRMWARE; break;
			default: st.state = HT_FAILED; break;
			}
			if (!open) havePose = false;
		}
		if (!open) {
			if (!Wait (2000)) break; // looked for again in 2 s
			continue;
		}
		if (cur.tir.threshold != lastThreshold) dev.SetThreshold (lastThreshold = cur.tir.threshold);
		int n = dev.ReadFrame (blobs, 10);
		if (n < 0) { // unplugged, or it stopped answering
			std::string e = dev.Error ();
			dev.Close (), open = false;
			std::lock_guard<std::mutex> lk (mx);
			st.state = HT_SEARCHING, st.message = e, havePose = false;
			continue;
		}
		if (n == 0) continue;
		solver.RemoveCameraRotation (blobs, cur.orientation);
		solver.SortBlobs (blobs);
		RawPose raw, out;
		bool ok = blobs.size () >= 3 && solver.Process (blobs, recenter, raw);
		if (ok) {
			if (cur.orientation & ORIENT_FROM_BEHIND) raw.pitch = -raw.pitch, raw.roll = -raw.roll, raw.tx = -raw.tx, raw.tz = -raw.tz;
			if (initRecenter) { // as upstream: the first good frame only lets the next one be the centre
				initRecenter = false;
				recenter = true;
				ok = false;
			} else {
				recenter = false;
				ok = post.Apply (cur.axes, raw, out, cur.align);
			}
		}
		std::lock_guard<std::mutex> lk (mx);
		st.frames++, fpsFrames++;
		double dt = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
		if (dt >= 1.0) st.fps = fpsFrames / dt, fpsFrames = 0, t0 = std::chrono::steady_clock::now ();
		st.blobs = blobs;
		st.havePose = ok;
		if (ok) {
			st.raw = raw, st.pose = out;
			pose = out, seq = st.frames, havePose = true;
		}
	}
	if (open) dev.Close ();
	std::lock_guard<std::mutex> lk (mx);
	st.state = HT_STOPPED;
}

} // namespace ht
