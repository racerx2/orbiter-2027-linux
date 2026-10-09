// not upstream: collision addon, E3's view of E4's session id map and E2's CollGeomView (Design CA E3 1.2, 1.3); the integrator adapts
#ifndef COLLDMGHOST_H
#define COLLDMGHOST_H
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "CollSdk.h"
#include "CollGeom.h"

class CollShape;
enum : uint8_t { CDMG_SLOT_REPLACED = 0, CDMG_SLOT_GONE = 1, CDMG_SLOT_REBUILT = 2 }; // E2's SLOTEV_* values
enum : uint8_t { CDMG_SRC_POLL = 0, CDMG_SRC_GCCORE = 1, CDMG_SRC_SENTINEL = 2 };     // E2's E2_POLL, E2_GCCORE, E3_SENTINEL
constexpr int COLL_GC_MATRIX_GROUP = 3;      // gcCore::MatrixId::GROUP (OVP/VulkanClient/gcCore.h:276-282)

struct CollDmgSlotEv { uint32_t mesh; uint8_t what, src; };                         // layout of E2's CollSlotEvent
struct CollDmgSlot {                         // E2's CollSlotView fields E3 reads
	bool present = false; uint32_t key = 0; uint16_t ngrp = 0; uint32_t nvtx = 0;
	std::shared_ptr<const CollRestMesh> rest; std::string name; uint32_t serial = 0;
};
struct CollDmgBaseObj {                      // E2's CollBaseObjView
	std::string planet, base, type;
	int32_t planetIdx = -1, baseIdx = -1; uint32_t obj = 0;
	int cls = 0; Vector size; double x = 0, z = 0; uint16_t mat = 0;
	CollH hPlanet = nullptr, hBase = nullptr;
};

class CollDmgHost {
public:
	virtual ~CollDmgHost () = default;
	virtual CollH    Vessel (uint32_t id) = 0;                       // E4 CollSession::Vessel; NULL once deleted
	virtual uint32_t IdOf (CollH h) = 0;                             // E4 CollSession::IdOf
	virtual CollShape *Shape (uint32_t id) = 0;                      // E2 Geom (id)->shape, null without a collider
	virtual uint32_t SlotCount (uint32_t id) = 0;                    // E2 Slots (id).size ()
	virtual bool     Slot (uint32_t id, uint32_t m, CollDmgSlot &out) = 0;
	virtual bool     SlotNow (uint32_t id, uint32_t m, uint32_t &serial) = 0;
	virtual void     ClientMeshRebuilt (uint32_t id, uint32_t m) = 0; // E2 ClientMeshRebuilt (id, m, E3_SENTINEL)
	virtual void     WantSlots (uint32_t id, bool on) = 0;
	virtual double   DestroyEnergy (uint32_t id) = 0;                // E2 Keys (id).destroyEnergy, -1 = not set
	virtual bool     BaseObject (int planet, int base, int obj, CollDmgBaseObj &out) = 0;
	virtual void     Bases (std::vector<CollDmgBaseObj> &all) = 0;
};

// adapter over E2's CollGeomView (duck-typed on its names) and E4's session id map; instantiated by the integrator
template <class GeomView, class Ids> class CollDmgHostOf : public CollDmgHost {
public:
	CollDmgHostOf (GeomView &g, Ids &i) : gv (g), ids (i) {}
	CollH Vessel (uint32_t id) override { return (CollH)ids.Vessel (id); }
	uint32_t IdOf (CollH h) override { return ids.IdOf ((decltype (ids.Vessel (0)))h); }
	CollShape *Shape (uint32_t id) override { auto *g = gv.Geom (id); return g ? const_cast<CollShape *> (g->shape) : nullptr; } // E2 owns the shape; E3 dents it
	uint32_t SlotCount (uint32_t id) override { return (uint32_t)gv.Slots (id).size (); }
	bool Slot (uint32_t id, uint32_t m, CollDmgSlot &out) override
	{
		const auto &s = gv.Slots (id);
		if (m >= s.size ()) return false;
		const auto &v = s[m];
		out.present = v.present, out.key = v.key, out.ngrp = v.ngrp, out.nvtx = v.nvtx, out.rest = v.rest, out.name = v.name, out.serial = v.serial;
		return true;
	}
	bool SlotNow (uint32_t id, uint32_t m, uint32_t &serial) override { return gv.SlotNow (id, m, serial); }
	void ClientMeshRebuilt (uint32_t id, uint32_t m) override { gv.ClientMeshRebuilt (id, m, CDMG_SRC_SENTINEL); }
	void WantSlots (uint32_t id, bool on) override { gv.WantSlots (id, on); }
	double DestroyEnergy (uint32_t id) override { return gv.Keys (id).destroyEnergy; }
	bool BaseObject (int planet, int base, int obj, CollDmgBaseObj &out) override
	{
		if (base < 0) return false; // the ground side of an impact (CA-ground)
		auto *o = gv.BaseObject (planet, base, obj);
		if (!o) return false;
		Copy (*o, out);
		return true;
	}
	void Bases (std::vector<CollDmgBaseObj> &all) override
	{
		std::vector<decltype (gv.BaseObject (0, 0, 0))> v;
		gv.Bases (v);
		all.clear ();
		for (auto *o : v) if (o) { all.emplace_back (); Copy (*o, all.back ()); }
	}
private:
	template <class O> static void Copy (const O &o, CollDmgBaseObj &out)
	{
		out.planet = o.planet, out.base = o.base, out.type = o.type, out.planetIdx = o.planetIdx, out.baseIdx = o.baseIdx, out.obj = o.obj;
		out.cls = o.cls, out.size = o.size, out.x = o.x, out.z = o.z, out.mat = o.mat, out.hPlanet = o.hPlanet, out.hBase = o.hBase;
	}
	GeomView &gv;
	Ids &ids;
};
#endif
