// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: background jobs for the ground graphics (horizon maps, cloud maps, noise)

#ifndef __GFXPOOL_H
#define __GFXPOOL_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class GfxPool {
public:
	enum Kind { CLOUDATLAS = 0, CLOUDMAP = 1, NOISE = 2, HORIZON = 3, NKIND = 4 }; // run in this order; the atlas is one job per planet
	struct Job {
		int owner = 0;                   // Register() id
		int kind = HORIZON;
		double prio = 0.0;               // lower runs first within a kind
		std::function<void()> run;       // touches only what it captured
		std::function<void()> dropped;   // instead of run, when the queue cap drops it
	};

	static GfxPool &Get ();
	static GfxPool *Peek ();             // NULL after Shutdown or before the first Get
	static void Shutdown ();             // joins the threads, queued jobs are discarded
	static int Threads ();               // hardware_concurrency/4, 1...3

	int Register ();                     // a new id, never reused
	void Unregister (int id);            // its queued jobs go, without dropped()
	void Submit (Job &&j);
	size_t Queued (int kind);
	bool Wants (int kind, double prio);  // false when that kind is full and this would be dropped first

	static const size_t MaxQueue = 64;   // per kind

private:
	GfxPool ();
	~GfxPool ();
	void Work ();
	void Stop ();

	std::mutex mx;
	std::condition_variable cv;
	std::vector<Job> queue;              // under mx
	std::vector<std::thread> workers;
	bool quit = false;                   // under mx
	int nextId = 1;                      // under mx
	static GfxPool *pool;
	static std::mutex poolMx;
};

#endif // !__GFXPOOL_H
