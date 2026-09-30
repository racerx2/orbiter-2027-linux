// not upstream: WM_POWERBROADCAST counterpart, logind's PrepareForSleep signal on the system bus

#include "SleepWatch.h"
#include "Orbiter.h"
#include "Log.h"
#include <QDBusConnection>

extern Orbiter *g_pOrbiter;

SleepWatch::SleepWatch (QObject *parent) : QObject (parent)
{
	if (!QDBusConnection::systemBus().connect ("org.freedesktop.login1", "/org/freedesktop/login1",
		"org.freedesktop.login1.Manager", "PrepareForSleep", this, SLOT(PrepareForSleep(bool))))
		LOGOUT("logind not reachable: the simulation is not frozen across suspend");
}

void SleepWatch::PrepareForSleep (bool start)
{
	// PBT_APMQUERYSUSPEND / PBT_APMRESUMESUSPEND, which only reach the render window
	if (!g_pOrbiter || !g_pOrbiter->GetRenderWnd()) { frozen = false; return; }
	if (start) { // a paused simulation stays paused, and Freeze's Resume needs its Suspend
		if (!frozen && g_pOrbiter->IsRunning()) { frozen = true; g_pOrbiter->Freeze (true); }
	} else if (frozen) {
		frozen = false;
		g_pOrbiter->Freeze (false);
	}
}
