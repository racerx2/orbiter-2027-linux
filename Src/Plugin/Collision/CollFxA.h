// not upstream: collision addon, dmg3 area F: impact effects (flakes, venting, sparks, dust) (design-CA-dmg3-F)
#ifndef COLLFXA_H
#define COLLFXA_H
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>
#include "CollCfg.h"
#include "CollDmgTypes.h"
#include "CollSdk.h"
#include "Vecmat.h"

enum : uint8_t { CFX_SPARKS = 0, CFX_DUST = 1, CFX_GLASS = 2, CFX_FLAKES = 3, CFX_VENT = 4, CFX_KINDS = 5 }; // value = eviction priority
constexpr int CFX_SLOTS = 32;                                // fixed: the client keeps &slot.lvl
constexpr int CFX_PER_FRAME = 4;                             // creations between two post-steps
constexpr double CFX_MAX_WARP = 10;                          // no new streams above this warp
constexpr double CFX_LVL_MIN = 0.1;                          // client alpha floor: streams below are deleted

struct CollFxCounters { uint64_t requests = 0, streams = 0, nulls = 0, dels = 0, stale = 0, capped = 0, skipped = 0; };
struct CollFxWorld {                                         // the session as F sees it; CollMakeFx binds CollDmgSession
	std::function<CollH (uint32_t)> handle;                  // live vessel of an id, NULL once deleted
	std::function<bool (uint32_t)> destroyed;                // XDMG_DESTROYED
	std::function<void (std::vector<std::pair<uint32_t, CollH>> &)> wrecks; // destroyed live vessels
};
struct CollFxSlot {
	bool used = false, held = false, fresh = false;          // held: refreshed by contacts; fresh: refreshed since the last post-step
	uint8_t kind = 0;
	uint32_t id = 0; CollH h = nullptr, ps = nullptr;        // ps NULL: no client (ghost slot, same timing, never deleted through the SDK)
	double lvl = 0;                                          // the client reads it until the stream is deleted
	double L0 = 0, tau = 0, hold = 0, t0 = 0, tHit = 0, power = 0;
	Vector c, dir;
};

class CollFxA : public CollDmgSink {
public:
	CollFxA (CollSdk &sdk, const CollCfgValues &cfg, CollFxWorld w);
	void Hit (const CollDamageHit &x) override;
	void Break (const CollBreakEvent &e) override;
	void Contact (const CollFxContact &x) override;
	void Destroyed (uint32_t id) override;
	void Post (double simt, double simdt) override;
	void DropVessel (uint32_t id, CollH h) override;
	void TimeJump () override;
	void End () override;
	void Quiet () override;
	const CollFxCounters &Counters () const { return n; }
	const std::array<CollFxSlot, CFX_SLOTS> &Slots () const { return slot; }
	int Live () const;
	int Find (CollH h, uint8_t kind) const;
	static double Yield (const DentMaterial *m, bool gearOk); // spark yield of a material
private:
	struct Req { uint8_t kind = 0; uint32_t id = 0; CollH h = nullptr; Vector pos, dir; CollSdk::FxSpec s; double lvl = 0, L0 = 0, tau = 0, hold = 0, t0 = 0, power = 0; bool held = false; };
	struct Ground { bool was = false; double vd = 0; };
	struct LastHit { Vector c, n; };
	bool Off (CollH h, bool playback);
	bool Debris (CollH h);
	double Inten () const;
	double Propellant (CollH h);
	int Create (const Req &r);
	void Del (int i);
	void Flakes (uint32_t id, CollH h, const Vector &pos, const Vector &dir, double N, double v0, double t, double S, bool air);
	void Glass (uint32_t id, CollH h, const Vector &pos, const Vector &dir, double t, double S, bool air);
	void Vent (uint32_t id, CollH h, const Vector &pos, const Vector &dir, double L0, double t, double S, bool air);
	void Held (uint8_t kind, uint32_t id, CollH h, const Vector &pos, const Vector &dir, double L, double t, double S, const CollSdk::FxSpec &s);
	void DustBurst (uint32_t id, CollH h, const Vector &pos, const Vector &dir, double v, double t, double S, bool air);
	static CollSdk::FxSpec DustSpec (double v, double S, bool air);
	void GroundPass (double simt);
	void LogLine (const CollFxSlot &s, bool del);
	CollSdk &sdk;
	const CollCfgValues &cfg;
	CollFxWorld w;
	std::array<CollFxSlot, CFX_SLOTS> slot;
	std::map<CollH, Ground> ground;
	std::map<CollH, bool> debris;
	std::map<uint32_t, LastHit> last;
	CollFxCounters n;
	int made = 0;
	bool quiet = false, loggedStale = false;
};
#endif
