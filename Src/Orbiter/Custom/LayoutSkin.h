
// custom: launcher layouts; the layout of this run: the choice, the first check, the dialog hook, export and dump

#ifndef __CUSTOM_LAYOUTSKIN_H
#define __CUSTOM_LAYOUTSKIN_H

#include "LauncherFacts.h"
#include <QSize>
#include <QString>

class Config;
class QWidget;

namespace custom {

	class LayoutSkin {
	public:
		// LaunchpadDialog::Create, before IDD_MAIN: decides the layout for the run, resets list widths, installs the hook
		static void Install (Config *cfg, void *hInst);

		// the skin for this run by the host's rules (demo mode, ORBITER_LAUNCHER_SKIN, the stored skin); "" = Classic
		static QString StartSkin (const Config *cfg, const LauncherCfg &lc);

		static bool InUse ();           // a layout was chosen for this run
		static QString RunId ();        // what Launcher.cfg's LayoutRun says once Orbiter.cfg holds this run's list widths
		static bool Undone ();          // Ctrl+Shift+L undid it
		static QString Id ();           // its skin id
		static QSize MinSize ();        // the Launchpad minimum, 550 x 350 without a layout
		static QSize RefSize ();        // IDD_MAIN as built; invalid if its form wasn't applied
		static QString RootStyle ();    // IDD_MAIN's style sheet
		static QString StockTitle ();   // IDD_MAIN's template caption
		static int Errors ();           // forms the first check refused
		static void Undo ();
		static void SetSkinView (QWidget *mainDlg, bool on);
		static void Dump (QWidget *mainDlg, const char *when); // ORBITER_LAYOUT_DUMP=<file>

		// writes a layout skin from the stock dialogs; id: its folder; msg: what to tell the user
		static bool Export (const QString &name, const QString &coloursFrom, QString &id, QString &msg);
		static QString DesignerPath (); // Qt 6 Designer; "" if not found
	};

}

#endif // !__CUSTOM_LAYOUTSKIN_H
