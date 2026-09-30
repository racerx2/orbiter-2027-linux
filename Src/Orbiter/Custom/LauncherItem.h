// custom: launcher skins; "Launchpad skin" entry in the Extra tab and its skin picker

#ifndef __CUSTOM_LAUNCHERITEM_H
#define __CUSTOM_LAUNCHERITEM_H

#include "OrbiterAPI.h"

namespace custom {

	class LauncherSkin;

	class LauncherItem: public LaunchpadItem {
	public:
		LauncherItem (LauncherSkin *host): host (host) {}
		char *Name () override;
		char *Description () override;
		bool clbkOpen (QWidget *hLaunchpad) override;
		int clbkWriteConfig () override;

	private:
		LauncherSkin *host;
	};

}

#endif // !__CUSTOM_LAUNCHERITEM_H
