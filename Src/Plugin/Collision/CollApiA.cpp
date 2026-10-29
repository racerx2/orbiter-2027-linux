// not upstream: collision addon, the exported repair and query functions of CollisionAPI.h (Design CA E3 10)
#include "CollisionAPI.h"
#include "CollApiA.h"
#include "CollDamageA.h"

namespace {
CollApiA::SessionFn g_session = nullptr;
CollDmgSession *Cur () { return g_session ? g_session () : nullptr; }
Vector V (const VECTOR3 &v) { return Vector (v.x, v.y, v.z); }
Matrix M (const MATRIX3 &m) { return Matrix (m.m11, m.m12, m.m13, m.m21, m.m22, m.m23, m.m31, m.m32, m.m33); }
}

void CollApiA::SetSession (SessionFn fn) { g_session = fn; }

DLLCLBK int collaVersion () { return 2; }

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

DLLCLBK int collaSweepVessel (OBJHANDLE hTarget, OBJHANDLE hMover, const VECTOR3 *pos, const MATRIX3 *rot, const VECTOR3 *dir, double len, double clearance, double *dist)
{
	return CollGuard ("collaSweepVessel", -1, [&] {
		if (!pos || !rot || !dir || !dist) return -1;
		*dist = 0.0;
		CollDmgSession *s = Cur ();
		return s ? s->SweepVessel ((CollH)hTarget, (CollH)hMover, V (*pos), M (*rot), V (*dir), len, clearance, *dist) : -1;
	});
}

DLLCLBK int collaSweepSphere (OBJHANDLE hTarget, const VECTOR3 *centre, double radius, const VECTOR3 *dir, double len, double clearance, double *dist)
{
	return CollGuard ("collaSweepSphere", -1, [&] {
		if (!centre || !dir || !dist) return -1;
		*dist = 0.0;
		CollDmgSession *s = Cur ();
		return s ? s->SweepSphere ((CollH)hTarget, V (*centre), radius, V (*dir), len, clearance, *dist) : -1;
	});
}

DLLCLBK int collaVesselRadius (OBJHANDLE hVessel, double *radius)
{
	return CollGuard ("collaVesselRadius", -1, [&] {
		if (!radius) return -1;
		*radius = 0.0;
		CollDmgSession *s = Cur ();
		return s ? s->VesselRadius ((CollH)hVessel, *radius) : -1;
	});
}
