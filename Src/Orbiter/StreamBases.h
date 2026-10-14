// not upstream: Launchpad Earth streaming page logic (TabStream.cpp): base list, Clear/Download refusals, end texts

#ifndef __STREAMBASES_H
#define __STREAMBASES_H

#include <functional>
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
	bool streamOff = false;    // streaming is off, so the Offline box is greyed
	bool busy = false;         // another process holds the tile cache
};
// empty if the action may start, else the reason shown to the user
std::string StreamRefusal (StreamAct a, const StreamState &s);

// a Clear confirmed while a size count ran: what happens to it now; busy asked only once the count has ended
enum class PendingClear { None, Wait, Start, Refuse };
PendingClear StreamPendingClear (bool pending, bool counting, const std::function<bool ()> &busy);
// the page's line for a Clear that has ended
std::string StreamClearText (long cleared, bool refused, const std::string &error);
// the page's line for an ended region download (Prefetch's result, 6 an exception); progress: its counts
std::string StreamFetchText (int result, const std::string &error, const std::string &progress);

#endif // !__STREAMBASES_H
