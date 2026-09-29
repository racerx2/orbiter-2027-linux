// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//-----------------------------------------------------------------------------
// Launchpad tab declaration: class DefVideoTab
// Tab for default video device parameters
//-----------------------------------------------------------------------------

#ifndef __TABVIDEO_H
#define __TABVIDEO_H

#include "LpadTab.h"
#include <filesystem>
namespace fs = std::filesystem;

namespace orbiter {

	class DefVideoTab : public LaunchpadTab {
	public:
		DefVideoTab(const LaunchpadDialog* lp);
		~DefVideoTab();

		void Create();

		BOOL OnInitDialog(QWidget *hWnd);

		void OnGraphicsClientLoaded(oapi::GraphicsClient* gc, const char *moduleName);

		void SetConfig(Config* cfg);

		bool OpenHelp();

	protected:
		void ShowInterface(QWidget *hTab, bool show);

		void EnumerateClients(QWidget *hTab);

		void ScanDir(QWidget *hTab, const fs::path &dir);
		// scan directory dir (relative to Orbiter root) for graphics clients
		// and enter them in the combo box

		void SelectClientIndex(UINT idx);

		void SetInfoString(PCSTR str);

		static void InfoProc(QWidget *hWnd, const char *info);

	private:
		UINT idxClient;
		char* strInfo;
	};

}

#endif // !__TABVIDEO_H
