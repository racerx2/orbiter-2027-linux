
// custom: launcher layouts; applies a Qt Designer form to a dialog built from a template, and exports one (Qt only)

#ifndef __CUSTOM_LAYOUTAPPLY_H
#define __CUSTOM_LAYOUTAPPLY_H

#include "UiForm.h"
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class QWidget;
struct RESDIALOG;

namespace custom {

	const int LAYOUT_MINW = 550, LAYOUT_MINH = 350; // LaunchpadDialog::OnInitDialog

	typedef std::function<void (const QString &line)> LayoutLog;

	// the 32 Launchpad dialogs a layout may change
	const QStringList &LayoutDialogs ();
	bool IsLayoutDialog (const QString &name);

	// what the host needs from an applied IDD_MAIN form
	struct LayoutInfo {
		QSize refSize;     // the dialog's size as built (the form's size, scaled)
		QSize minSize;     // the Launchpad minimum for this layout
		QString rootStyle; // the form's style sheet, ${SKIN} substituted
	};

	// form-level checks that leave the whole dialog stock; "" if fine
	QString CheckLayoutForm (const UiForm &form, const QString &dialog);

	// applies a form to a dialog CreateResDialog has just built, before its set-up code runs;
	// false: nothing changed (err says why); per-control problems are logged and skipped
	bool ApplyLayout (QWidget *dlg, const RESDIALOG *d, const UiForm &form, const QString &skinDir,
		const QString &uiFile, const LayoutLog &log, LayoutInfo &info, QString &err);

	struct LayoutImage {
		QString path;      // relative to the .ui file
		QImage image;
	};

	// the form of a stock dialog (built with the hook off) and the pictures it refers to
	UiForm ExportLayout (QWidget *dlg, const RESDIALOG *d, void *hModule, std::vector<LayoutImage> &images);

	// visual undo of every layout applied this run (Ctrl+Shift+L)
	void UndoLayouts ();

	// hides the main window's decorations while a QML skin's view is shown
	void SetLayoutSkinView (QWidget *mainDlg, bool on);

	// copies the files a style sheet names as ${SKIN}/<path> from one skin folder to another; returns how many
	int CopyStyleFiles (const QString &qss, const QString &srcDir, const QString &dstDir, QStringList &problems);

	// unit tests: forget what was applied and undone, as at a new start
	void ResetLayoutState ();

}

#endif // !__CUSTOM_LAYOUTAPPLY_H
