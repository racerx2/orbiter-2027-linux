// not upstream: collision addon, the Config/Collision.cfg keys of every section, their defaults and the value parser (Orbiter-free)
#ifndef COLLCFG_H
#define COLLCFG_H
#include <functional>
#include <string>
#include <vector>

struct CollCfgValues {
	int model = 1;                          // CollisionModel (E1): 0 no contacts and no new damage; default on (release step)
	bool response = true;                   // CollisionResponse (E1): false detects and logs only
	bool check = false;                     // CollisionCheck (E1): momentum checks fail hard (tests pin TRUE)
	bool dockZone = true;                   // CollisionDockZone (E1)
	bool attachZone = true;                 // CollisionAttachZone (E1)
	int logLevel = 1;                       // CollisionLog (E1, E2, E3): 2 write lines, 3 distance lines, 4 pose lines
	int clientCheck = 0;                    // ClientCheck (E2): gcCore check
	std::vector<std::string> meshProbeOnce; // MeshProbe (E2): classes probed ONCE, every other class ALWAYS
	double destroyEnergy = 1000;            // DestroyEnergy (E3) [J/kg]: classes without the class key
	double buildingDestroyEnergy = 1000;    // BuildingDestroyEnergy (E3) [J/kg]
	bool thrustCut = true;                  // CollisionThrustCut (E3)
	bool visuals = true;                    // CollisionVisuals (E3)
	bool keyPass = true;                    // CollisionKeyPass (E3)
	bool cullFix = true;                    // CollisionCullFix (E3)
	int notify = 1;                         // CollisionNotify (E3): 0 none, 1 destroyed, 2 also first damage
	bool recorder = true;                   // CollisionRecorder (E3)
	std::string testRecId;                  // CollisionTestRecId (E3, tests): fixed side-file id
	int testSlotCheck = 0;                  // CollisionTestSlotCheck (E2, tests): slot check in post-step stage PO3
	bool dentModes = true;                  // CollisionDentModes (dmg3 S): crush and hinge dents; false bowl only
	bool dentNoise = true;                  // CollisionDentNoise (dmg3 S)
	bool dentFacetNormals = true;           // CollisionDentFacetNormals (dmg3 S)
	bool brk = true;                        // CollisionBreak (dmg3 P): parts, glass and interior break
	int debrisMax = 8;                      // CollisionDebrisMax (dmg3 P): live debris vessels
	double debrisLife = 1800;               // CollisionDebrisLife (dmg3 P) [s]
	bool fx = true;                         // CollisionFx (dmg3 F)
	int fxMask = 15;                        // CollisionFxMask (dmg3 F): 1 flakes, 2 vent, 4 sparks, 8 dust
	double fxIntensity = 1;                 // CollisionFxIntensity (dmg3 F): 0..2
	bool fxPlayback = true;                 // CollisionFxPlayback (dmg3 F)
	double fxVentSpec = 150;                // CollisionFxVentSpec (dmg3 F) [J/kg]
	int fxMaxStreams = 24;                  // CollisionFxMaxStreams (dmg3 F): at most 32
	bool fxRollDust = false;                // CollisionFxRollDust (dmg3 F)
};

// every key in file order, with its field; Read, Keys and the template test walk this one list; CollisionCfgVersion is the build's (cmake/CollisionCfgOnce.cmake)
template <class V, class F> void CollCfgFields (V &v, F &&f)
{
	f ("CollisionModel", v.model);
	f ("CollisionResponse", v.response);
	f ("CollisionCheck", v.check);
	f ("CollisionDockZone", v.dockZone);
	f ("CollisionAttachZone", v.attachZone);
	f ("CollisionLog", v.logLevel);
	f ("ClientCheck", v.clientCheck);
	f ("MeshProbe", v.meshProbeOnce);
	f ("DestroyEnergy", v.destroyEnergy);
	f ("BuildingDestroyEnergy", v.buildingDestroyEnergy);
	f ("CollisionThrustCut", v.thrustCut);
	f ("CollisionVisuals", v.visuals);
	f ("CollisionKeyPass", v.keyPass);
	f ("CollisionCullFix", v.cullFix);
	f ("CollisionNotify", v.notify);
	f ("CollisionRecorder", v.recorder);
	f ("CollisionTestRecId", v.testRecId);
	f ("CollisionTestSlotCheck", v.testSlotCheck);
	f ("CollisionDentModes", v.dentModes);
	f ("CollisionDentNoise", v.dentNoise);
	f ("CollisionDentFacetNormals", v.dentFacetNormals);
	f ("CollisionBreak", v.brk);
	f ("CollisionDebrisMax", v.debrisMax);
	f ("CollisionDebrisLife", v.debrisLife);
	f ("CollisionFx", v.fx);
	f ("CollisionFxMask", v.fxMask);
	f ("CollisionFxIntensity", v.fxIntensity);
	f ("CollisionFxPlayback", v.fxPlayback);
	f ("CollisionFxVentSpec", v.fxVentSpec);
	f ("CollisionFxMaxStreams", v.fxMaxStreams);
	f ("CollisionFxRollDust", v.fxRollDust);
}

namespace CollCfg {
	typedef std::function<bool (const char *item, std::string &val)> Reader; // false: item missing or empty, as oapiReadItem_string
	CollCfgValues Read (const Reader &rd, std::vector<std::string> *bad = nullptr); // bad: keys whose value did not parse (default kept)
	std::vector<const char *> Keys ();
	bool Parse (const std::string &s, bool &out);  // TRUE, FALSE, 1, 0 (ASCII case-insensitive)
	bool Parse (const std::string &s, int &out);
	bool Parse (const std::string &s, double &out); // finite only
	bool Parse (const std::string &s, std::string &out);
	bool Parse (const std::string &s, std::vector<std::string> &out); // words split on spaces and tabs
}
#endif
