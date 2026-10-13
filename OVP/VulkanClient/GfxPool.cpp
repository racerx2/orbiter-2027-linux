// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: background jobs for the ground graphics

#include "GfxPool.h"
#include <algorithm>

GfxPool *GfxPool::pool = nullptr;
std::mutex GfxPool::poolMx;

GfxPool &GfxPool::Get ()
{
	std::lock_guard<std::mutex> lk(poolMx);
	if (!pool) pool = new GfxPool();
	return *pool;
}

GfxPool *GfxPool::Peek ()
{
	std::lock_guard<std::mutex> lk(poolMx);
	return pool;
}

void GfxPool::Shutdown ()
{
	std::lock_guard<std::mutex> lk(poolMx);
	if (!pool) return;
	pool->Stop();
	delete pool;
	pool = nullptr;
}

int GfxPool::Threads ()
{
	int n = int(std::thread::hardware_concurrency()) / 4;
	return std::max(1, std::min(3, n));
}

GfxPool::GfxPool ()
{
	int n = Threads();
	for (int i = 0; i < n; i++) workers.emplace_back(&GfxPool::Work, this);
}

GfxPool::~GfxPool ()
{
	Stop();
}

void GfxPool::Stop ()
{
	{
		std::lock_guard<std::mutex> lk(mx);
		quit = true;
		queue.clear();
	}
	cv.notify_all();
	for (auto &t : workers) if (t.joinable()) t.join();
	workers.clear();
}

int GfxPool::Register ()
{
	std::lock_guard<std::mutex> lk(mx);
	return nextId++;
}

void GfxPool::Unregister (int id)
{
	std::vector<Job> gone; // freed outside the lock: captures may be large
	{
		std::lock_guard<std::mutex> lk(mx);
		auto it = std::stable_partition(queue.begin(), queue.end(), [id](const Job &j) { return j.owner != id; });
		for (auto k = it; k != queue.end(); ++k) gone.push_back(std::move(*k));
		queue.erase(it, queue.end());
	}
}

void GfxPool::Submit (Job &&j)
{
	std::vector<Job> dropped;
	{
		std::lock_guard<std::mutex> lk(mx);
		if (quit) return;
		int kind = j.kind;
		queue.push_back(std::move(j));
		size_t n = 0;
		for (const Job &q : queue) if (q.kind == kind) n++;
		while (n > MaxQueue) { // the farthest of that kind goes
			auto worst = queue.end();
			for (auto it = queue.begin(); it != queue.end(); ++it)
				if (it->kind == kind && (worst == queue.end() || it->prio > worst->prio)) worst = it;
			dropped.push_back(std::move(*worst));
			queue.erase(worst);
			n--;
		}
	}
	cv.notify_one();
	for (Job &d : dropped) if (d.dropped) d.dropped();
}

size_t GfxPool::Queued (int kind)
{
	std::lock_guard<std::mutex> lk(mx);
	size_t n = 0;
	for (const Job &q : queue) if (q.kind == kind) n++;
	return n;
}

bool GfxPool::Wants (int kind, double prio)
{
	std::lock_guard<std::mutex> lk(mx);
	if (quit) return false;
	size_t n = 0;
	double worst = -1e300;
	for (const Job &q : queue) if (q.kind == kind) { n++; worst = std::max(worst, q.prio); }
	return n < MaxQueue || prio < worst;
}

void GfxPool::Work ()
{
	for (;;) {
		Job job;
		{
			std::unique_lock<std::mutex> lk(mx);
			cv.wait(lk, [this] { return quit || !queue.empty(); });
			if (quit) return;
			auto best = queue.begin();
			for (auto it = queue.begin(); it != queue.end(); ++it)
				if (it->kind < best->kind || (it->kind == best->kind && it->prio < best->prio)) best = it;
			job = std::move(*best);
			queue.erase(best);
		}
		if (job.run) job.run();
	}
}
