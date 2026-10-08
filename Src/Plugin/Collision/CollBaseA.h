// not upstream: collision addon, planet and base cfg scan, base matching, placement and building colliders (design E2 10)
#ifndef __COLLBASEA_H
#define __COLLBASEA_H
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "CollMeshFile.h"
#include "CollSdk.h"
#include "CollShape.h"

struct CollBaseObjView {
	std::string planet, base, type;
	int32_t planetIdx, baseIdx; uint32_t obj;
	int cls;
	Vector size;
	double x, z;
	uint16_t mat;
	CollH hPlanet, hBase;
};

// one parsed base object (CollBaseObjA.cpp)
struct CollBaseObjDef {
	std::string type;                  // TYPE keyword as in the cfg
	uint32_t index = 0;                // index in the core's object list
	Vector pos, scale = Vector (1, 1, 1); double rot = 0;
	uint32_t specs = 0;                // OBJSPEC_* of the core class
	bool noCollide = false, collide = false; std::string collMat;
	std::vector<CollGroupData> grp;    // base frame geometry after Setup
	Vector restBox;                    // MESH: rest box after the object scale
	std::string meshFile;              // MESH: FILE value
};

// a parsed base cfg (BASE-V2.0)
struct CollBaseFile {
	std::string name;                  // Name item, "" if none
	bool haveLocation = false; double lng = 0, lat = 0;
	double objSize = 0; bool mapToSphere = false;
	std::vector<CollBaseObjDef> obj;
	bool periodOk = true, context = false;
};

struct CollBaseRec {
	int planetIdx = -1, baseIdx = -1;
	CollH hPlanet = nullptr, hBase = nullptr;
	std::string planet, base;
	double rPlanet = 0, lng = 0, lat = 0, elev = 0;
	Vector rposP; Matrix rrotP;        // base frame in the planet frame
	CollBaseShape shape;
	std::vector<CollBaseObjView> view; // included objects
	std::vector<uint32_t> objOf;       // shape slot -> core object index
	bool checkedOk = true; uint32_t nextCheck = 0;
};

// MIT base-object code (CollBaseObjA.cpp): parse the object list, build the geometry in the base frame
bool CollParseBaseFile (const std::string &text, CollBaseFile &out, std::vector<std::string> &warn);
bool CollBaseObjGeometry (CollBaseObjDef &o, CollSdk &sdk, const CollDirs &d, double rPlanet, bool mapToSphere, std::vector<std::string> &warn);
bool CollBaseObjIncluded (const CollBaseObjDef &o);
int  CollBaseObjClass (const std::string &type);   // DENTB_*, -1 for non-colliding types

class CollBaseA {
public:
	void Build (CollSdk &sdk, const CollDirs &d, bool geometry); // clbkSimulationStart (10.3)
	void Poll (CollSdk &sdk, uint32_t frame);                    // pose check once per base per 100 frames (10.4)
	const CollBaseRec *Rec (int planet, int base) const;
	const CollBaseObjView *Object (int planet, int base, int obj) const;
	void All (std::vector<const CollBaseObjView *> &all) const;
	std::vector<std::unique_ptr<CollBaseRec>> rec;
	uint32_t nObjects = 0, nIncluded = 0;
};
#endif
