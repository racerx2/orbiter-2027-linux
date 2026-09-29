// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//-----------------------------------------------------------------------------
// Launchpad tab definition: class AboutTab
// Tab for "about" page
//-----------------------------------------------------------------------------

#ifndef __TABABOUT_H
#define __TABABOUT_H

#include "LpadTab.h"

namespace orbiter {

	class AboutTab : public LaunchpadTab {
	public:
		AboutTab(const LaunchpadDialog* lp);

		void Create();
		bool OpenHelp();

		BOOL OnInitDialog(QWidget *hWnd);

	private:
		static void AboutProc(QWidget *hWnd, int textId);
	};

}

#endif // !__TABABOUT_H
