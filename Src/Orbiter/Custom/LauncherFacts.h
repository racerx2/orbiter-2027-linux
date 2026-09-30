// custom: launcher skins; parsers for skin.cfg, Launcher.cfg and scenario facts (std only, unit-tested)

#ifndef __CUSTOM_LAUNCHERFACTS_H
#define __CUSTOM_LAUNCHERFACTS_H

#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace custom {

	const int LAUNCHER_API = 1;           // launcher API version this build provides
	const size_t MAX_RECENT = 8;
	const size_t MAX_FAVOURITES = 200;
	const size_t MAX_LISTED_VESSELS = 64;

	struct CfgEntry {
		std::string key;   // lower case
		std::string value; // trimmed
	};

	// "key = value" lines; a line whose first non-blank character is ';' or '#' is a comment; no inline comments
	std::vector<CfgEntry> ReadCfg (std::istream &is);

	struct SkinManifest {
		std::string id;          // folder name
		std::string dir;         // canonical folder path
		std::string name, author, version, description;
		int api = 1;
		std::string qml, qss;    // relative to dir
		int minWidth = 0, minHeight = 0, width = 0, height = 0;
		bool ok = false;         // usable by this build
		std::string reason;      // why not, if !ok
	};

	// reads and validates <dir>/skin.cfg
	SkinManifest ReadSkin (const std::string &dir, int supportedApi = LAUNCHER_API);

	struct LauncherCfg {
		std::string skin;                     // stored skin id, "" = classic
		std::vector<std::string> recent;      // newest first
		std::vector<std::string> favourites;
	};

	LauncherCfg ReadLauncherCfg (std::istream &is);
	void WriteLauncherCfg (std::ostream &os, const LauncherCfg &cfg);
	bool LoadLauncherCfg (const std::string &path, LauncherCfg &cfg); // false if missing or unreadable (cfg reset)
	bool SaveLauncherCfg (const std::string &path, const LauncherCfg &cfg); // via <path>.tmp and rename
	void AddRecent (LauncherCfg &cfg, const std::string &scn);
	bool ToggleFavourite (LauncherCfg &cfg, const std::string &scn); // returns the new state

	struct VesselFact {
		std::string name, cls;
	};

	struct ScenarioFacts {
		char dateKind = 0;       // 0 none, 'M' MJD, 'J' JD, 'E' Julian epoch (as State.cpp reads them)
		double dateValue = 0.0;
		std::string system;
		std::string focus;
		std::vector<VesselFact> vessels; // first MAX_LISTED_VESSELS
		int vesselCount = 0;
		std::string focusClass;
		std::string focusStatus; // "Orbiting", "Landed" or ""
		std::string focusBody;
		std::string focusBase;
		int focusPad = 0;        // as written in the file (1-based), 0 if none
	};

	// reads BEGIN_ENVIRONMENT, BEGIN_FOCUS and BEGIN_SHIPS like State.cpp and Psys.cpp; at most 4 MiB
	ScenarioFacts ReadScenario (std::istream &is);

}

#endif // !__CUSTOM_LAUNCHERFACTS_H
