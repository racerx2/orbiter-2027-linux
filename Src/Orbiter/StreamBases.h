// not upstream: Launchpad Earth streaming page (TabStream.cpp): the surface base list, and when Clear and Download are refused

#ifndef __STREAMBASES_H
#define __STREAMBASES_H

#include <string>
#include <vector>

struct StreamBase { std::string name; double lng = 0.0, lat = 0.0; }; // degrees

// Name and Location (lng lat) before the first BEGIN_ line; false if either is missing
bool StreamReadBase (const std::string &file, StreamBase &b);
// the bases of every *.cfg in dir, sorted by name
std::vector<StreamBase> StreamBaseList (const std::string &dir);

enum class StreamAct { Clear, Download };
struct StreamState {
	bool session = false;      // a simulation runs its own stream
	bool fetching = false;     // a region download runs
	bool clearing = false;     // a Clear runs
	bool pendingClear = false; // a Clear waits for the size count
	bool offline = false;      // Offline is ticked
	bool busy = false;         // another process holds the tile cache
};
// empty if the action may start, else the reason shown to the user
std::string StreamRefusal (StreamAct a, const StreamState &s);

#endif // !__STREAMBASES_H
