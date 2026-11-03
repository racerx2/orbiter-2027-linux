// not upstream (head-track): the TrackIR on a thread of its own: found, started, frames to poses; the latest pose for the camera, its status for the Controllers page

#ifndef __HEADTRACKER_H
#define __HEADTRACKER_H

#include "HtSettings.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ht {

enum TrackState { HT_STOPPED, HT_SEARCHING, HT_NO_ACCESS, HT_BUSY, HT_NO_FIRMWARE, HT_STARTING, HT_TRACKING, HT_FAILED };
const char *StateName (TrackState s);

struct TrackStatus {
	TrackState state = HT_STOPPED;
	TirModel model = TIR_NONE;
	uint16_t pid = 0;
	std::string message;          // the reason when it isn't tracking
	double fps = 0;
	uint64_t frames = 0;
	int width = 0, height = 0;
	std::vector<Blob> blobs;      // the last frame's, the camera turn removed
	bool havePose = false;        // three points seen in the last frame
	RawPose raw, pose;            // the head (degrees, mm) and what the view gets
	bool on = true;
};

class HeadTracker {
public:
	~HeadTracker ();
	void Start (const Settings &s, const std::string &fwDir); // its thread; already running: the settings taken
	void Stop ();                 // the device stopped, its LEDs off, the thread ended
	bool Running () const { return thread.joinable (); }
	void Update (const Settings &s); // live; the illuminators, LEDs and threshold go to the device
	TrackStatus Status () const;
	bool Pose (RawPose &out, uint64_t &seq) const; // the latest view pose and its frame; false: none yet
	void Center ();               // the next pose is the centre
	void SetOn (bool on);
	bool On () const { return on; }

private:
	void Run ();
	bool Wait (int ms);            // false: stopping

	mutable std::mutex mx;
	std::condition_variable cv;
	std::thread thread;
	bool stop = false;
	Settings set;
	bool setChanged = false, reopen = false;
	std::string fwDir;
	TrackStatus st;
	RawPose pose;
	uint64_t seq = 0;
	bool havePose = false;
	std::atomic<bool> recenter {false}, on {true};
};

} // namespace ht

#endif // !__HEADTRACKER_H
