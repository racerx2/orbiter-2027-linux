// not upstream: Launchpad Earth streaming page helpers (StreamBases.h)

#include "StreamBases.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <strings.h>

namespace fs = std::filesystem;

bool StreamReadBase (const std::string &file, StreamBase &b)
{
	std::ifstream f (file);
	std::string line, name;
	double lng = 0.0, lat = 0.0;
	bool loc = false;
	while (std::getline (f, line) && line.compare (0, 6, "BEGIN_")) {
		size_t eq = line.find ('=');
		if (eq == std::string::npos) continue;
		std::string key = line.substr (0, eq), val = line.substr (eq + 1);
		key.erase (key.find_last_not_of (" \t") + 1);
		key.erase (0, key.find_first_not_of (" \t"));
		val.erase (0, val.find_first_not_of (" \t"));
		val.erase (val.find_last_not_of (" \t\r") + 1);
		if (!strcasecmp (key.c_str(), "Name")) name = val;
		else if (!strcasecmp (key.c_str(), "Location")) loc = (bool)(std::istringstream (val) >> lng >> lat);
	}
	if (name.empty() || !loc) return false;
	b.name = name;
	b.lng = lng;
	b.lat = lat;
	return true;
}

std::vector<StreamBase> StreamBaseList (const std::string &dir)
{
	std::vector<StreamBase> bases;
	std::error_code ec;
	for (fs::directory_iterator it (dir, ec), end; !ec && it != end; it.increment (ec)) {
		if (it->path().extension() != ".cfg") continue;
		StreamBase b;
		if (StreamReadBase (it->path().string(), b)) bases.push_back (b);
	}
	std::sort (bases.begin(), bases.end(), [] (const StreamBase &a, const StreamBase &b) { return a.name < b.name; });
	return bases;
}

std::string StreamRefusal (StreamAct a, const StreamState &s)
{
	if (s.session) return "A simulation session is running. Try again once it has ended.";
	if (a == StreamAct::Clear) {
		if (s.fetching) return "A region download is running. Cancel it or wait until it has finished.";
		if (s.clearing || s.pendingClear) return "The streamed tiles are being cleared already.";
	} else {
		if (s.clearing || s.pendingClear) return "The streamed tiles are being cleared. Try again once that has finished.";
		if (s.fetching) return "A region download is running already.";
		if (s.offline) return "Offline is ticked, so no tiles are requested. Untick it to download a region.";
	}
	if (s.busy) return "Another Orbiter is using the Earth tile cache. Try again once it has ended.";
	return std::string();
}

PendingClear StreamPendingClear (bool pending, bool counting, const std::function<bool ()> &busy)
{
	if (!pending) return PendingClear::None;
	if (counting) return PendingClear::Wait;
	return busy && busy() ? PendingClear::Refuse : PendingClear::Start;
}

std::string StreamClearText (long cleared, bool refused, const std::string &error)
{
	if (!error.empty()) return "Clearing stopped: " + error;
	if (refused) return "Not cleared: another Orbiter is using the Earth tile cache.";
	return "Cleared " + std::to_string (cleared) + " streamed tiles.";
}

std::string StreamFetchText (int result, const std::string &error, const std::string &progress)
{
	switch (result) {
	case 0: return "Done. " + progress;
	case 1: return "Cancelled. " + progress;
	case 2: return "Stopped: no answer from the tile servers for two minutes. " + progress;
	case 3: return "Config/TileStream.cfg has no Earth: nothing to download.";
	case 4: return "Not started: another Orbiter or a Clear is using the Earth tile folder.";
	case 5: return "Stopped by an error, see Orbiter.log. " + progress;
	default: return "Stopped: " + (error.empty() ? std::string ("unknown error") : error) + ". " + progress;
	}
}
