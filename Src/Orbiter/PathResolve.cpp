// not upstream: Linux counterpart of Windows' case-insensitive, '\'-separated file name lookup

#define OAPI_IMPLEMENTATION
#include "OrbiterAPI.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

static bool Exists (const std::string &p)
{
	struct stat st;
	return stat (p.c_str(), &st) == 0;
}

static std::string Lower (const std::string &s) // ASCII only; strcasecmp follows the locale
{
	std::string r (s);
	for (char &c : r) if (c >= 'A' && c <= 'Z') c += 32;
	return r;
}

static std::string Join (const std::string &dir, const std::string &name)
{
	if (dir.empty()) return name;
	if (dir.back() == '/') return dir + name;
	return dir + '/' + name;
}

// directory listings (lower-case name, name) in order, read again when the directory changes
struct DirList { timespec mtime; time_t listed; std::vector<std::pair<std::string, std::string>> names; };
static std::mutex dirLock; // tile loader threads resolve paths too
static std::map<std::pair<dev_t, ino_t>, DirList> dirCache;

// the other case spellings of name in dir, in byte order; none if dir can't be listed
static std::vector<std::string> Variants (const std::string &dir, const std::string &name)
{
	std::vector<std::string> v;
	const char *d = (dir.empty() ? "." : dir.c_str());
	struct stat st;
	if (stat (d, &st) || !S_ISDIR (st.st_mode)) return v;
	std::lock_guard<std::mutex> lock (dirLock);
	auto key = std::make_pair (st.st_dev, st.st_ino);
	auto it = dirCache.find (key);
	// a listing serves while the directory is unchanged and was not changed within 2 s of it (coarse file times, FAT's 2 s steps)
	if (it == dirCache.end() || it->second.mtime.tv_sec != st.st_mtim.tv_sec || it->second.mtime.tv_nsec != st.st_mtim.tv_nsec ||
		st.st_mtim.tv_sec + 2 > it->second.listed) {
		if (dirCache.size() >= 1024) dirCache.clear();
		timespec now;
		clock_gettime (CLOCK_REALTIME_COARSE, &now);
		DirList l = { st.st_mtim, now.tv_sec, {} };
		if (DIR *dp = opendir (d)) {
			while (struct dirent *e = readdir (dp)) l.names.push_back ({Lower (e->d_name), e->d_name});
			closedir (dp);
		}
		std::sort (l.names.begin(), l.names.end());
		it = dirCache.insert_or_assign (key, std::move (l)).first;
	}
	const auto &names = it->second.names;
	auto r = std::equal_range (names.begin(), names.end(), std::make_pair (Lower (name), std::string()),
		[](const auto &a, const auto &b) { return a.first < b.first; });
	for (auto i = r.first; i != r.second; ++i)
		if (i->second != name) v.push_back (i->second);
	return v;
}

// depth-first: exact spelling by stat, then other case spellings (one folder on Windows); best = deepest folder reached
static bool Walk (const std::string &cur, const std::vector<std::string> &comp, size_t k, std::string &out, size_t &bestk, std::string &best)
{
	if (k > bestk) bestk = k, best = cur;
	if (k == comp.size()) { out = cur; return true; }
	std::string exact = Join (cur, comp[k]);
	if (comp[k] == "." || comp[k] == "..") return Walk (exact, comp, k+1, out, bestk, best);
	if (Exists (exact) && Walk (exact, comp, k+1, out, bestk, best)) return true;
	for (const std::string &v : Variants (cur, comp[k]))
		if (Walk (Join (cur, v), comp, k+1, out, bestk, best)) return true;
	return false;
}

DLLEXPORT std::string oapiResolvePath (const char *path)
{
	std::string p (path ? path : "");
	for (char &c : p) if (c == '\\') c = '/';
	if (p.empty() || Exists (p)) return p;

	bool absolute = (p[0] == '/');
	bool trailing = (p.back() == '/');
	std::vector<std::string> comp;
	for (size_t i = 0, j; i < p.size(); i = j + 1) {
		j = p.find ('/', i);
		if (j == std::string::npos) j = p.size();
		if (j > i) comp.push_back (p.substr (i, j - i));
	}

	std::string root = (absolute ? "/" : ""), cur, best = root;
	size_t bestk = 0;
	if (!Walk (root, comp, 0, cur, bestk, best)) { // no match on disk: the deepest existing folder, the rest as spelled (new file, or genuinely missing)
		cur = best;
		for (size_t k = bestk; k < comp.size(); k++) cur = Join (cur, comp[k]);
	}
	if (trailing && (cur.empty() || cur.back() != '/')) cur += '/';
	return cur;
}
