// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __LAUNCHPAD_H
#define __LAUNCHPAD_H

#include "OrbiterPlatform.h"
#include "OrbiterAPI.h"
#include "Config.h"
#include <vector>

class QTreeWidgetItem;
class QTimer;
class QObject;
class EventHook;

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class LaunchpadTab;
class ExtraTab;
class BuiltinLaunchpadItem;

//-----------------------------------------------------------------------------
// Nonmember functions
//-----------------------------------------------------------------------------
RECT GetClientPos (QWidget *hWnd, QWidget *hChild);
void SetClientPos (QWidget *hWnd, QWidget *hChild, RECT &r);

namespace orbiter {

	class LaunchpadTab;
	class ExtraTab;

	//-----------------------------------------------------------------------------
	// Name: class LaunchpadDialog
	// Desc: Handles the startup dialog ("Launchpad")
	//-----------------------------------------------------------------------------
	class LaunchpadDialog {
		friend class Orbiter;
		friend class LaunchpadTab;

	public:
		LaunchpadDialog(Orbiter* app);
		~LaunchpadDialog();

		bool Create(bool startvideotab = false);
		// create dialog window
		// If return value==false then the window could not be created

		void Show(); // Show the launchpad window
		void Hide(); // Hide the launchpad window

		inline bool Visible() const { return m_bVisible; }

		// ConsumeMessage (IsDialogMessage) left out: Qt handles the dialog's keyboard navigation itself

		QWidget *GetWaitWindow() const { return hWait; }

		inline Orbiter* App() const { return pApp; }
		inline Config* Cfg() const { return pCfg; }
		LaunchpadTab* GetTab(UINT i) const;
		QWidget *HTabContainer() const { return hTabContainer; }

		void AddTab(LaunchpadTab* tab);
		// Inserts a new tab into the list

		void EnableLaunchButton(bool enable) const;
		// Enable/disable "Launch Orbiter" button

		QTreeWidgetItem *RegisterExtraParam(LaunchpadItem* item, QTreeWidgetItem *parent = 0);
		// Register an item in the "Extra" list. If parent=0, the item is registered
		// as a root (top level) item. Otherwise it appears as a sub-item under
		// the parent item.

		bool UnregisterExtraParam(LaunchpadItem* item);
		// Unregister an item in the "Extra" list.

		QTreeWidgetItem *FindExtraParam(const char* name, QTreeWidgetItem *parent = 0);
		// Return item 'name' below parent 'parent', or NULL if not found

		void WriteExtraParams();
		// allow all externally registered "Extra" items to write their data to file
		// (internal "extra" items use the Config class to write to Orbiter.cfg)

		ExtraTab* GetExtraTab() const
		{
			return pExtra;
		}
		// tab object

		void UpdateConfig();
		// save current dialog settings in application configuration

		void ShowWaitPage(bool show, long mem_committed = 0);
		void UpdateWaitProgress();
		long mem_wait; // amount of memory to be deallocated during wait
		long mem0;     // initial memory status

	private:
		void *hInst;             // instance handle
		QWidget *hDlg;           // dialog window handle
		std::vector<LaunchpadTab*> TabList;
		LaunchpadTab* CTab;      // current tab page
		QWidget *hTabContainer;  // tab container window handle
		QWidget *hWait;          // "wait" page
		QBrush *hDlgBrush;
		QImage *hShadowImg;
		QTimer *timer;           // demo mode idle timer
		Orbiter* pApp;           // application pointer
		Config* pCfg;           // config pointer

		void SetDemoMode();
		// Set launchpad controls to demo mode

		int SelectDemoScenario();
		// Select an arbitrary scenario from the demo folder

		void InitSize(QWidget *hWnd);
		BOOL Resize(QWidget *hWnd, DWORD w, DWORD h, DWORD mode);

		void InitTabControl(QWidget *hWnd);
		// initialise the tabs

		//void InitDevicePage (D3D7Enum_DeviceInfo *devlist, DWORD ndev, D3D7Enum_DeviceInfo *dev);
		// Set dialog controls for device tab according to device list
		// and current device dev

		void SwitchTabPage(QWidget *hWnd, int pg);
		// display a new page

		void OnInitDialog(QWidget *hWnd);
		void OnCommand(int id);
		bool DlgProc(QObject *obj, QEvent *event);
		void WaitProc(QWidget *hWnd);
		// Dialog set-up and event callbacks (the dialog window and its owner-drawn controls)

		RECT client0;          // initial client window size
		RECT copyr0;           // initial copyright box size
		RECT r_launch0;        // initial position of launch button
		RECT r_help0;          // initial position of help button
		RECT r_exit0;          // initial position of exit button
		RECT r_data0;          // initial position of data area
		RECT r_wait0;          // initial position of wait dialog
		RECT r_version0;       // initial position of version string

		DWORD shadowh;         // shadow bar height
		int dy_bt;             // button separation
		bool m_bVisible;       // launchpad dialog visible?

		orbiter::ExtraTab* pExtra;      // tab object
	};

}

#endif // !__LAUNCHPAD_H
