// GPSNav: the unit, a panel navigator drawn in an ImGui window: bezel, screen, soft keys, keys and a dual knob
#pragma once
#include "Nav.h"
#include "Sources.h"
#include "Terrain.h"
#include <map>
#include <string>
#include <vector>

struct ImDrawList;
struct ImVec2;

// the unit's settings, kept in the scenario
struct Options {
	bool metric = false;               // km, m/s, m in place of nm, kt, ft
	bool trackUp = false;
	int dcl = 0;                       // map declutter, 0 to 2
	int range = 7;                     // into the range table
	int fields[5] = {0, 1, 2, 3, 4};   // the data strip
	double gpAngle = 3.0 * gps::RAD, tch = 15.0;
	int mapMode = 0;                   // 0 TOPO, 1 TERRAIN (against the altitude), 2 PLAIN
	bool marker = true;                // the HUD's heading-tape caret toward the active waypoint
	int cdi = 0;                       // the CDI's full scale: 0 AUTO, 1 5 nm, 2 1 nm, 3 0.3 nm
	bool mapDownload = true;           // the map's water tiles fetched from OpenFreeMap (else the cache only)
	std::string Save () const;
	void Load (const char *s);
};

class Unit : public ImGuiDialog {
public:
	Unit (Library &lib, std::map<std::string, gps::Plan> &plans, Options &opt);
	void Display () override;
	void OnDraw () override;
	void Step (double dt); // a time step: the focus vessel's flight plan moves on to its next leg; GPS roll steering
	void Reset (); // a simulation's end: its bodies' lists go
	std::string GpssVessel () const { return gpss ? gpssVessel : std::string (); } // for the scenario
	void GpssResume (const std::string &vessel) { gpssPending = vessel; }

	enum Group { G_MAP, G_WPT, G_NRST, G_FPL, G_VNAV, NGROUP };
	enum Key { K_NONE = -1, K_SOFT = 0, K_DTO = 6, K_MENU, K_FPL, K_NRST, K_CLR, K_ENT, K_RNGUP, K_RNGDN, K_PUSH, K_OUTL, K_OUTR, NKEY };

private:
	struct Ctx;                        // what a frame knows about the focus vessel
	struct Hit { float x0, y0, x1, y1; int id; };
	enum Overlay { O_NONE, O_DTO, O_INSERT, O_IDENT, O_MENU };

	bool Frame (Ctx &c);
	void Press (int key, Ctx &c);
	void Outer (int d, Ctx &c);
	void Inner (int d, Ctx &c);
	void Row (int row, Ctx &c);        // a list row clicked

	void DrawBezel (ImDrawList *dl, float x, float y, float w, float h);
	void DrawStrip (ImDrawList *dl, const Ctx &c);
	void DrawSoft (ImDrawList *dl, const Ctx &c);
	void DrawMap (ImDrawList *dl, const Ctx &c);
	void DrawWpt (ImDrawList *dl, const Ctx &c);
	void DrawNrst (ImDrawList *dl, const Ctx &c);
	void DrawFpl (ImDrawList *dl, const Ctx &c);
	void DrawVnav (ImDrawList *dl, const Ctx &c);
	void DrawEntry (ImDrawList *dl, const Ctx &c);
	void DrawMenu (ImDrawList *dl, const Ctx &c);
	void DrawTitle (ImDrawList *dl, const char *title);

	gps::Plan &PlanOf (Ctx &c);        // the vessel's, a new one if it was on another body
	int Seed (const Ctx &c) const;     // the waypoint a Direct-To starts with: the cursor's, else the active one
	void Open (Overlay o, const Ctx &c, int seed);
	void Matches (const Ctx &c);
	void Softkey (int i, Ctx &c);
	std::vector<std::string> SoftLabels (const Ctx &c) const;
	void Tune (const gps::Wpt &w, Ctx &c);
	void Steer (VESSEL *v, const gps::Plan *p, double dt);
	void GpssOn (VESSEL *v);
	void GpssOff (const std::string &why);
	void Msg (const std::string &s);
	struct Approach { bool ok = false; std::string id; gps::LatLon thr, far; double elev = 0.0; bool runway = false; int ends = 0; };
	Approach Target (const Ctx &c) const;

	// formatting in the chosen units
	std::string Dist (double m) const;
	const char *DistUnit () const;
	std::string Speed (double ms) const;
	const char *SpeedUnit () const;
	std::string Alt (double m) const;
	const char *AltUnit () const;
	std::string Vs (double ms) const;
	const char *VsUnit () const;
	static std::string Deg (double rad);
	static std::string Time (double s);
	static std::string LatStr (double lat), LngStr (double lng);
	static std::string Freq (const gps::Wpt &w);
	void Field (int f, const Ctx &c, std::string &label, std::string &value, std::string &unit) const;

	Library &lib;
	Terrain terrain;
	std::map<std::string, gps::Plan> &plans;
	Options &opt;

	int group = G_MAP, nrstPage = 0;
	bool cursor = false;
	int row = 0;                       // the cursor's row on the list pages
	Overlay ov = O_NONE;
	std::string entry;                 // the ident being entered
	int epos = 0;                      // the character the inner knob changes
	bool fresh = false;                // the entry still holds the waypoint it opened with: typing replaces it
	std::vector<int> found;            // its matches
	int fsel = 0;
	int menuSel = 0;
	const gps::Body *seen = nullptr;   // the body of the last frame
	const gps::Body *wptBody = nullptr;
	int wptIdx = -1;                   // the WPT page's waypoint
	std::vector<int> nrst;             // the NRST page's list
	double nrstTime = -1.0;
	int nrstFor = -1;
	bool altMode = false;              // VNAV: a target altitude, not the glide path
	double tgtAlt = 3000.0 * gps::FT;
	int rwySel = -1;                   // VNAV: the runway end chosen, -1 the best aligned
	bool gpss = false;                 // GPS roll steering engaged, on this vessel
	std::string gpssVessel, gpssPending;
	double ail = 0.0, bank0 = 0.0, bankTgt = 0.0, gpssLog = 0.0;
	std::string msg;
	double msgTime = -10.0;
	int lit = K_NONE;                  // the key last pressed, lit a moment
	double litTime = 0.0;

	// this frame's layout
	float sx0, sy0, sx1, sy1;          // the screen
	float cy0, cy1;                    // its content between the data strip and the soft key labels
	float fs;                          // the text size
	float kx, ky, kr0, kr1;            // the knob: its centre, the ring's and the inner knob's radii
	std::vector<Hit> keys, rows;
};
