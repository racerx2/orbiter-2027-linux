// not upstream: public API of the collision addon: notices to vessel modules, damage query, repair
#ifndef __COLLISIONAPI_H
#define __COLLISIONAPI_H
#include <stddef.h>
#include <stdint.h>
#include "OrbiterAPI.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define COLLA_VMSG    0x4F434F4C   // clbkGeneric msgid of every addon notice ("OCOL"), above VMSG_USER
#define COLLA_MAGIC   0x31414C43u  // COLLA_HDR::magic ("CLA1")
#define COLLA_VERSION 1            // payload version written by this header
#define COLLA_HANDLED 0x58464C43   // reply to DESTROYED or RESTORED: the module handles the destroyed effects ("CLFX")

enum { COLLA_KIND_STATE = 0, COLLA_KIND_CONTACT = 1, COLLA_KIND_DENT = 16, COLLA_KIND_DESTROYED = 17,
	COLLA_KIND_RESTORED = 18, COLLA_KIND_REPAIRED = 19 };

typedef struct {
	uint32_t magic;               // COLLA_MAGIC
	uint16_t version;             // COLLA_VERSION of the writer
	uint16_t kind;                // COLLA_KIND_*, equal to prm
	uint32_t size;                // bytes of the whole payload as written; read no field beyond it
} COLLA_HDR;

#define COLLA_CON_VESSEL   0x01   // the other body is a vessel
#define COLLA_CON_BUILDING 0x02   // the other body is a base object
#define COLLA_CON_FIRST    0x04   // newly touching feature; only such contacts carry damage energy
#define COLLA_CON_SLOW     0x08   // every approach below 0.1 m/s
#define COLLA_CON_WOKE     0x10   // this vessel was woken from LANDED
typedef struct {
	COLLA_HDR hdr;                // kind COLLA_KIND_CONTACT
	uint32_t flags;               // COLLA_CON_*
	OBJHANDLE hOther;             // other vessel, or the base of a building
	int32_t otherObj;             // building object index in its base, else -1
	int32_t mesh, group;          // hit mesh and group of this vessel, -1 if unknown
	int32_t reserved;             // 0
	double simt;                  // impact time [s]
	VECTOR3 pos, nml;             // impact point and outward normal, this vessel's frame
	double vn, vt;                // approach and slip speed at impact [m/s]
	double J;                     // phase-1 impulse on this vessel [N s]
	double dE;                    // damage energy of the pair: phase-1 work of its first-touch contacts [J]
} COLLA_CONTACTINFO;

#define COLLA_DMG_DESTROYED    0x01   // absorbed energy reached the threshold
#define COLLA_DMG_MODULEFX     0x02   // the module handles the destroyed effects
#define COLLA_DMG_CATASTROPHIC 0x04   // one impact above 40 kJ/kg
#define COLLA_DMG_PLAYBACK     0x08   // replayed from the recorder side file
#define COLLA_DMG_CUT          0x10   // the addon's thrust cut is active
typedef struct {
	COLLA_HDR hdr;                // kind DENT, DESTROYED, RESTORED, REPAIRED, or STATE from collaGetVesselDamage
	uint32_t flags;               // COLLA_DMG_*
	OBJHANDLE hOther;             // other vessel or base of the deepest dent, or NULL
	int32_t otherObj;             // building object index, else -1
	int32_t mesh, group;          // mesh and group of the deepest dent, -1 if none
	uint32_t ndent;               // dent records held
	double simt;                  // impact time of the deepest dent of this notice [s]
	VECTOR3 pos, nml;             // deepest dent centre and outward normal, vessel frame
	double energy;                // absorbed in this notice [J]
	double energy_total;          // absorbed so far [J]
	double depth;                 // max dent depth in this notice [m]
	double destroyEnergy;         // threshold of this vessel [J/kg]
} COLLA_DAMAGEINFO;

typedef int (*COLLA_PFN_VERSION) ();
typedef int (*COLLA_PFN_REPAIRVESSEL) (OBJHANDLE hVessel);
typedef int (*COLLA_PFN_REPAIRBUILDING) (const char *planetBase, int obj);
typedef int (*COLLA_PFN_GETVESSELDAMAGE) (OBJHANDLE hVessel, COLLA_DAMAGEINFO *info);
typedef int (*COLLA_PFN_GETBUILDINGDAMAGE) (const char *planetBase, int obj, double *eabs, uint32_t *flags);
static const char *const COLLA_EXPORTS[] = { "collaVersion", "collaRepairVessel", "collaRepairBuilding",
	"collaGetVesselDamage", "collaGetBuildingDamage" };

inline void *collaFind (const char *name)   // exported function of the loaded addon, NULL if not loaded
{
#ifdef _WIN32
	HMODULE h = GetModuleHandleA ("Collision.dll");
	return h ? (void *)GetProcAddress (h, name) : NULL;
#else
	void *h = dlopen ("Collision.so", RTLD_NOW | RTLD_NOLOAD);   // matches the SONAME
	if (!h) return NULL;
	void *f = dlsym (h, name);
	dlclose (h);                                                 // RTLD_NOLOAD took a reference
	return f;
#endif
}
#endif
