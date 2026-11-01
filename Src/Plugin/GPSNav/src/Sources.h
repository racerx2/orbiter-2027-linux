// GPSNav: each body's database, made from Orbiter (its bases, their pads, runways and beacons, the body's beacons) and from files
// (the add-on's OurAirports data, the user's files, the body's map lines). The files are read on a worker thread.
#pragma once
#include "Data.h"
#include "Orbitersdk.h"
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>

class Library {
public:
	~Library ();
	const gps::Body *Get (OBJHANDLE hBody); // null while it is being made (then Loading is true)
	bool Loading (OBJHANDLE hBody) const;
	std::string Summary (OBJHANDLE hBody) const; // what it holds, for the log
	void Clear ();                               // a simulation's end: its handles go

private:
	struct Entry {
		gps::Body body;         // Orbiter's part, then everything
		gps::Body files;        // the worker's part
		std::thread worker;
		std::atomic<bool> done {false};
		bool merged = false;
		int nOrbiter = 0, nFiles = 0;
	};
	void Start (OBJHANDLE hBody, Entry &e);
	static void Merge (Entry &e);
	std::map<OBJHANDLE, std::unique_ptr<Entry>> m;
};

std::string ConfigFile (const char *fileName); // a body's FileName (the cfg Orbiter read, Config/Legacy/Earth.cfg say) as a path
