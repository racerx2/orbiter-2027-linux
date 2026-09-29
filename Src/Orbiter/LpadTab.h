// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __LPADTAB_H
#define __LPADTAB_H

#include "OrbiterPlatform.h"
#include "Config.h"
#include "Launchpad.h"

// Property page indices
#define PG_SCN  0
#define PG_OPT  1
#define PG_MOD  2
#define PG_VID  3
#define PG_EXT  4
#define PG_ABT  5
#define PG_WAIT 6

namespace orbiter {

	class LaunchpadDialog;

	//-----------------------------------------------------------------------------
	// Name: class LaunchpadTab
	// Desc: Base class for main dialog tabs
	//-----------------------------------------------------------------------------
	class LaunchpadTab {
	public:
		LaunchpadTab(const LaunchpadDialog* lp);
		virtual ~LaunchpadTab();
		virtual void Create() {}

		inline const LaunchpadDialog* Launchpad() const { return pLp; }
		inline Config* Cfg() const { return pCfg; }

		virtual void GetConfig(const Config* cfg) {}
		// Read config parameters

		virtual void SetConfig(Config* cfg) {}
		// Write config parameters back

		virtual bool OpenHelp() { return false; }

		/**
		 * \brief Indicates if a tab can resize itself to fit the available area
		 * \return true if the tab can adjust its size, false if the size is fixed.
		 * \default Returns false.
		 */
		virtual bool DynamicSize() const { return false; }

		/**
		 * \brief Notification sent to a tab window when the tab area has been resized
		 * \param w new width of tab area [pixel]
		 * \param h new height of tab area [pixel]
		 * \default Resizes the tab to fit the area if \ref DynamicSize returns true,
		 *    centers the tab in the available area otherwise.
		 */
		virtual void TabAreaResized(int w, int h);

		void OpenTabHelp(const char* topic);

		virtual void Show();
		virtual void Hide();
		virtual void LaunchpadShowing(bool show) {}
		inline bool IsActive() const { return bActive; }
		inline QWidget *TabWnd() const { return hTab; }
		inline QWidget *LaunchpadWnd() const { return pLp->hDlg; }
		inline void *AppInstance() const { return pLp->hInst; }

		virtual BOOL OnInitDialog(QWidget *hWnd) { return FALSE; }
		// WM_INITDIALOG: the tab connects its controls' signals here

		virtual BOOL OnSize(int w, int h);
		// by default, this re-centers the items if RegisterItemPositions has been called

		virtual BOOL OnMessage(QWidget *hWnd, QEvent *event) { return FALSE; }
		// events of the tab window other than its size (true if handled)

		virtual bool TabProc(QWidget *hWnd, QEvent *event);
		// generic event handler

	protected:
		QWidget *CreateTab(int resid);

		const LaunchpadDialog* pLp;
		Config* pCfg;
		QWidget *hTab;
		RECT pos0;  // initial position in Launchpad dialog
		bool bActive;

		int* item;
		POINT* itempos;  // registered dialog item postions
		int nitem;    // list length
	};

}

#endif // !__LPADTAB_H
