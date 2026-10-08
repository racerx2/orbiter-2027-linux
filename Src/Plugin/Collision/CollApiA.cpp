// not upstream: collision addon, the exported repair and query functions of CollisionAPI.h (Design CA E3 10)
#include "CollisionAPI.h"
#include "CollApiA.h"
#include "CollDamageA.h"

namespace {
CollApiA::SessionFn g_session = nullptr;
CollDmgSession *Cur () { return g_session ? g_session () : nullptr; }
}

void CollApiA::SetSession (SessionFn fn) { g_session = fn; }

DLLCLBK int collaVersion () { return 1; }

DLLCLBK int collaRepairVessel (OBJHANDLE hVessel)
{
	return CollGuard ("collaRepairVessel", 0, [&] { CollDmgSession *s = Cur (); return s ? s->RepairVessel ((CollH)hVessel) : 0; });
}

DLLCLBK int collaRepairBuilding (const char *planetBase, int obj)
{
	return CollGuard ("collaRepairBuilding", 0, [&] { CollDmgSession *s = Cur (); return s ? s->RepairBuilding (planetBase, obj) : 0; });
}

DLLCLBK int collaGetVesselDamage (OBJHANDLE hVessel, COLLA_DAMAGEINFO *info)
{
	return CollGuard ("collaGetVesselDamage", 0, [&] { CollDmgSession *s = Cur (); return s ? s->GetVesselDamage ((CollH)hVessel, info) : 0; });
}

DLLCLBK int collaGetBuildingDamage (const char *planetBase, int obj, double *eabs, uint32_t *flags)
{
	return CollGuard ("collaGetBuildingDamage", 0, [&] { CollDmgSession *s = Cur (); return s ? s->GetBuildingDamage (planetBase, obj, eabs, flags) : 0; });
}
