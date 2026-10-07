// not upstream: collision addon, the own .msh parser, Orbiter.cfg and cfg item readers, mesh paths and the per-session mesh cache (design E2 4)
#ifndef __COLLMESHFILE_H
#define __COLLMESHFILE_H
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "CollGeom.h"
#include "CollShape.h"

class CollSdk;

// Mesh.cpp:800-975 rules; returns `present` as LoadMesh (4.3 table); out holds the groups read so far
bool CollParseMsh (const std::string &text, const char *name, CollRestMesh &out);

// sscanf "%f" with the two glibc rules of 4.2 (e-tail, hex floats); p advances like glibc
bool CollScanFloat (const char *&p, const char *e, float &v);

// MeshDir and ConfigDir as the core keeps them (Config.cpp:36-37, :489-508)
struct CollDirs { std::string meshDir = ".\\Meshes\\", configDir = ".\\Config\\"; };

// replica of Config::GetString and friends on the Orbiter.cfg text (Config.cpp:1485-1538)
class CollOrbCfg {
public:
	void SetText (const std::string &t) { text = t; loaded = true; }
	bool String (const char *key, std::string &val) const;  // first line starting with key; value < 256 bytes
	bool Int (const char *key, int &val) const;             // sscanf "%d"
	bool Real (const char *key, double &val) const;         // sscanf "%lf"
	bool Bool (const char *key, bool &val) const;           // strncasecmp "true" (4), "false" (5)
	void Dirs (CollDirs &d, std::vector<std::string> *warn = nullptr) const; // GetDir rules
	bool loaded = false;
	std::string text;
};

// GetItemString replica (Config.cpp:340-365) on a cfg file text: case-insensitive label, END_PARSE ends
bool CollItemString (const std::string &text, const char *label, std::string &val);
bool CollItemReal (const std::string &text, const char *label, double &val);
bool CollItemInt (const std::string &text, const char *label, int &val);
bool CollItemBool (const std::string &text, const char *label, bool &val);

// MeshDir + name + ext, "" if it does not fit 256 bytes (Config.cpp:1393-1417)
std::string CollMeshPath (const CollDirs &d, const std::string &name, const char *ext);
std::string CollCfgPath (const CollDirs &d, const std::string &name, const char *ext);
std::string CollLower (const std::string &s);
uint64_t CollFnv (const void *data, size_t n, uint64_t h = 1469598103934665603ull);

// per-session rest meshes and sidecars (4.5, 5)
class CollMeshCache {
public:
	struct NameMesh { std::shared_ptr<const CollRestMesh> rest; bool present = false; std::string key; };
	NameMesh ByName (CollSdk &sdk, const CollDirs &d, const std::string &name);       // name slots and sidecar MESH
	std::shared_ptr<const CollRestMesh> ByTemplate (CollSdk &sdk, const void *tpl, std::string &key); // template slots
	std::shared_ptr<const CollSidecar> Sidecar (CollSdk &sdk, const CollDirs &d, const std::string &name, uint32_t ngrp);
	size_t Parsed () const { return nParsed; }
private:
	void Keep (const std::shared_ptr<const CollRestMesh> &m);
	std::unordered_map<std::string, std::weak_ptr<const CollRestMesh>> rest;
	std::unordered_map<std::string, bool> presentOf;
	std::deque<std::shared_ptr<const CollRestMesh>> lru;
	size_t lruBytes = 0, nParsed = 0;
	std::unordered_set<std::string> missing;
	std::unordered_map<std::string, std::shared_ptr<const CollSidecar>> side;
	std::unordered_set<std::string> noSide;
};
#endif
