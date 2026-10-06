// custom: launcher skins; the skin host of the Launchpad dialog (QSS, QML and forms skins, modes, teardown)

#include "LauncherSkin.h"
#include "ClassicHider.h"
#include "LauncherApi.h"
#include "LauncherItem.h"
#include "LayoutSkin.h"
#include "ResetKey.h"
#include "Orbiter.h"
#include "Launchpad.h"
#include "LpadTab.h"
#include "Config.h"
#include "Log.h"
#include "ResDialog.h"
#include "resource.h"
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <dlfcn.h>
#include <filesystem>

namespace fs = std::filesystem;
using orbiter::LaunchpadDialog;

namespace {

	const int CLASSIC_MINW = 550, CLASSIC_MINH = 350; // LaunchpadDialog::OnInitDialog
	const char *CFG_FILE = "Launcher.cfg";
	const char *SKIN_DIR = "Skins";
	const char *MODULE_FILE = "Modules/Launcher/LauncherQml.so";
	const char *FORMS_MODULE_FILE = "Modules/Launcher/LauncherForms.so"; // custom: forms skins
	const size_t MAX_SKINS = 200;
	const char *TITLE_HINT = " — Ctrl+Shift+L: classic Launchpad";
	const char *UNDONE_HINT = " — restart for the stock layout";

	// custom: launcher layouts; the layout's part of the dialog's style sheet while it is shown
	QString LayoutRootStyle ()
	{
		return (custom::LayoutSkin::InUse () && !custom::LayoutSkin::Undone () ? custom::LayoutSkin::RootStyle () : QString ());
	}

	bool LayoutShown ()
	{
		return custom::LayoutSkin::InUse () && !custom::LayoutSkin::Undone ();
	}

	void LogLine (const char *line)
	{
		LOGOUT ("%s", line);
	}

}

void custom::LauncherSkin::Attach (LaunchpadDialog *lp)
{
	if (lp && lp->GetTab (0)) new LauncherSkin (lp);
}

custom::LauncherSkin::LauncherSkin (LaunchpadDialog *lp): QObject (lp->GetTab (0)->LaunchpadWnd ()), lp (lp)
{
	dlg = lp->GetTab (0)->LaunchpadWnd ();
	hWait = lp->GetWaitWindow ();
	baseTitle = dlg->windowTitle ();
	resetKey = new ResetKey (dlg, [this]() { ResetToClassic (); }, this);
	LoadLauncherCfg (CFG_FILE, cfg);
	hider = new ClassicHider (dlg, hWait);
	item = new LauncherItem (this);
	lp->RegisterExtraParam (item, nullptr);
	if (QPushButton *b = DlgItem<QPushButton> (dlg, IDLAUNCH)) {
		connect (b, &QPushButton::pressed, this, [this]() { launching = SelectedScenario (); CommitLayoutRun (); }); // click() and Enter press it too; Launch writes Orbiter.cfg
		connect (b, &QPushButton::clicked, this, [this]() { RecordLaunch (); }); // after the classic handler, which launched
	}
	dlg->installEventFilter (this);
	if (hWait) hWait->installEventFilter (this);
	connect (qApp, &QCoreApplication::aboutToQuit, this, [this]() { Teardown (); });

	const Config *c = lp->Cfg ();
	const QString id = LayoutSkin::StartSkin (c, cfg); // the same choice the layout was made by
	ScanSkins ();
	if (LayoutSkin::InUse ()) {
		ApplyMinSize (false);
		QSize ref = LayoutSkin::RefSize ();
		QScreen *s = dlg->screen ();
		QRect avail = (s ? s->availableGeometry () : QRect (0, 0, 1920, 1080));
		if (ref.isValid () && (dlg->width () < ref.width () || dlg->height () < ref.height ()))
			dlg->resize (std::min (std::max (dlg->width (), ref.width ()), std::max (dlg->minimumWidth (), avail.width ())),
				std::min (std::max (dlg->height (), ref.height ()), std::max (dlg->minimumHeight (), avail.height ())));
		if (int n = LayoutSkin::Errors ()) {
			QString msg = QString ("Layout '%1': %2 dialog%3 could not be applied and stay%4 as they are; see Orbiter.log.")
				.arg (LayoutSkin::Id ()).arg (n).arg (n == 1 ? "" : "s").arg (n == 1 ? "s" : "");
			AddPending ([this, msg]() { QMessageBox::warning (dlg, "Orbiter: Launchpad layout", msg); });
		}
	}
	if (!id.isEmpty ()) Apply (id, c->CfgCmdlinePrm.bOpenVideoTab);
	SyncEscape ();
	LayoutSkin::Dump (dlg, "start");
	AddPending ([this]() { if (api) api->RefreshAll (); });
}

custom::LauncherSkin::~LauncherSkin ()
{
	// the classic tabs and Config may be gone already: nothing here touches them (the teardown ran on aboutToQuit)
}

void custom::LauncherSkin::Log (const QString &line) const
{
	LOGOUT ("Launcher skin: %s", line.toUtf8 ().constData ());
}

// ---------------------------------------------------------------------------------------------------------
// skins and Launcher.cfg

void custom::LauncherSkin::ScanSkins ()
{
	skins.clear ();
	std::vector<fs::path> dirs;
	std::error_code ec;
	for (fs::directory_iterator it (SKIN_DIR, ec), end; !ec && it != end; it.increment (ec)) {
		std::error_code ec2;
		if (!it->is_directory (ec2)) continue;
		std::string name = it->path ().filename ().string ();
		if (name.empty () || name[0] == '.') continue;
		dirs.push_back (it->path ());
		if (dirs.size () >= MAX_SKINS) break;
	}
	std::sort (dirs.begin (), dirs.end ());
	for (const auto &d : dirs)
		skins.push_back (ReadSkin (d.string ()));
	if (api) api->SkinsRescanned ();
}

const custom::SkinManifest *custom::LauncherSkin::ActiveManifest () const
{
	return activeId.isEmpty () ? nullptr : &active;
}

void custom::LauncherSkin::SaveCfg ()
{
	if (!SaveLauncherCfg (CFG_FILE, cfg))
		LOGOUT_WARN ("Launcher skin: can't write %s", CFG_FILE);
}

void custom::LauncherSkin::RequestSkin (const QString &id)
{
	if (torn) return;
	QString t = (id.compare ("classic", Qt::CaseInsensitive) ? id.trimmed () : QString ());
	cfg.skin = t.toStdString ();
	SaveCfg ();
	switchTarget = t;
	if (switchPending) return; // the pending switch takes the latest target
	switchPending = true;
	AddPending ([this]() {
		switchPending = false;
		ScanSkins ();
		Unapply ();
		resetting = false; // only now: the old skin can call setSkin until its engine is gone
		if (!switchTarget.isEmpty ()) Apply (switchTarget, false);
		if (api) api->SkinSwitched ();
		SyncEscape ();
		if (!InSkinView ()) FocusClassic (); // the focused QML view may be gone
		const QString note = RestartNote (switchTarget); // custom: launcher layouts
		if (!note.isEmpty ()) QMessageBox::information (dlg, "Orbiter: Launchpad skin", note);
	});
}

void custom::LauncherSkin::ResetToClassic ()
{
	const bool layout = LayoutShown ();
	if (torn || resetting || (activeId.isEmpty () && !layout)) return;
	Log ("Ctrl+Shift+L: back to Classic");
	if (layout) { // custom: launcher layouts; its looks go at once, its places at the next start
		LayoutSkin::Undo ();
		if (activeId.isEmpty () || active.qss.empty ()) dlg->setStyleSheet (QString ());
		baseTitle = LayoutSkin::StockTitle () + QString::fromUtf8 (UNDONE_HINT);
	}
	if (activeId.isEmpty ()) {
		cfg.skin.clear ();
		SaveCfg ();
		SyncEscape ();
		return;
	}
	resetting = true;
	RequestSkin (QString ()); // queued: the view that got the key must not go away under it
}

QString custom::LauncherSkin::RestartNote (const QString &id) const
{
	if (NextLayout (id) == (LayoutShown () ? LayoutSkin::Id () : QString ())) return QString ();
	if (qEnvironmentVariableIsSet ("ORBITER_LAUNCHER_SKIN"))
		return "ORBITER_LAUNCHER_SKIN is set, so the next start uses its skin and layout, not this choice.";
	return "The layout changes when Orbiter starts again.";
}

// custom: launcher layouts; Launcher.cfg's LayoutRun is stored when Orbiter.cfg gets this run's list widths (launch, exit)
void custom::LauncherSkin::CommitLayoutRun ()
{
	const std::string run = LayoutSkin::RunId ().toStdString ();
	if (torn || cfg.layoutRun == run) return;
	cfg.layoutRun = run;
	SaveCfg ();
}

QString custom::LauncherSkin::NextLayout (const QString &id) const
{
	for (const auto &m : skins)
		if (QString::fromStdString (m.id) == id) return (m.ok && !m.ui.empty () ? id : QString ());
	return QString ();
}

void custom::LauncherSkin::SyncEscape ()
{
	const bool on = !torn && (!activeId.isEmpty () || LayoutShown ());
	if (resetKey) {
		resetKey->SetQml (on && skinView && !formsView);
		if (on) resetKey->Install ();
		else resetKey->Remove ();
	}
	const QString title = (on ? baseTitle + QString::fromUtf8 (TITLE_HINT) : baseTitle);
	if (dlg->windowTitle () != title) dlg->setWindowTitle (title);
}

void custom::LauncherSkin::FocusClassic ()
{
	QWidget *f = dlg->focusWidget ();
	if (torn || (f && f->isVisible ())) return;
	orbiter::LaunchpadTab *tab = lp->GetTab (PG_SCN);
	QTreeWidget *t = (tab && tab->TabWnd () ? DlgItem<QTreeWidget> (tab->TabWnd (), IDC_SCN_LIST) : nullptr);
	if (t && t->isVisible ()) {
		t->setFocus (Qt::OtherFocusReason);
		return;
	}
	for (QWidget *w = dlg->nextInFocusChain (); w && w != dlg; w = w->nextInFocusChain ())
		if (w->window () == dlg && w->isVisible () && w->isEnabled () && (w->focusPolicy () & Qt::TabFocus)) {
			w->setFocus (Qt::TabFocusReason);
			return;
		}
}

QString custom::LauncherSkin::SelectedScenario () const
{
	orbiter::LaunchpadTab *tab = lp->GetTab (PG_SCN);
	QTreeWidget *t = (tab && tab->TabWnd () ? DlgItem<QTreeWidget> (tab->TabWnd (), IDC_SCN_LIST) : nullptr);
	QTreeWidgetItem *it = (t ? t->currentItem () : nullptr);
	if (!it || it->childIndicatorPolicy () == QTreeWidgetItem::ShowIndicator) return QString ();
	QString p = it->text (0);
	for (QTreeWidgetItem *q = it->parent (); q; q = q->parent ())
		p = q->text (0) + '/' + p;
	return p;
}

void custom::LauncherSkin::RecordLaunch ()
{
	QString p = launching;
	launching.clear ();
	if (torn || p.isEmpty () || lp->Visible ()) return; // Launchpad still shown: no session started
	AddRecent (cfg, p.toStdString ());
	SaveCfg ();
	if (api) api->RecentChanged ();
}

// ---------------------------------------------------------------------------------------------------------
// applying skins

void custom::LauncherSkin::Apply (const QString &id, bool startClassic)
{
	const SkinManifest *found = nullptr;
	for (const auto &m : skins)
		if (QString::fromStdString (m.id) == id) found = &m;
	if (!found) {
		Fail (id, "there is no folder of that name in Skins", QString ());
		return;
	}
	if (!found->ok) {
		Fail (id, QString::fromStdString (found->reason), QString ());
		return;
	}
	const SkinManifest m = *found;
	const bool hasView = (!m.qml.empty () || !m.forms.empty ()), forms = !m.forms.empty (); // custom: forms skins
	if (hasView) {
		QString err;
		if (!LoadModule (forms, err)) {
			Fail (id, err, forms ? "The Qt QML library may be missing (Ubuntu: libqt6qml6)." : "The Qt Quick libraries may be missing (Ubuntu: libqt6quickwidgets6).");
			return;
		}
	}
	activeId = id;
	active = m;
	if (!m.qss.empty () && !ApplyQss (m)) {
		activeId.clear ();
		active = SkinManifest ();
		Fail (id, "the style sheet can't be read", QString ());
		return;
	}
	if (hasView) {
		if (!api) api = new LauncherApi (lp, this);
		api->SkinSwitched ();
		formsView = forms;
		QString err;
		if (!CreateView (err)) {
			formsView = false;
			dlg->setStyleSheet (LayoutRootStyle ());
			activeId.clear ();
			active = SkinManifest ();
			api->SkinSwitched ();
			Fail (id, err, err.contains ("\"QtQuick\" is not installed") ? "Install the Qt Quick QML modules (Ubuntu: qml6-module-qtquick)." : QString ());
			return;
		}
		skinView = true;
		if (!back) {
			back = new QPushButton (dlg);
			back->setObjectName ("customSkinBack");
			back->setAutoDefault (false);
			back->setDefault (false);
			back->hide ();
			connect (back, &QPushButton::clicked, this, [this]() { BackToSkin (); });
		}
		back->setText (QString::fromUtf8 ("◂  Back to ") + QString::fromStdString (m.name));
		if (startClassic) EnterClassicView ();
		else EnterSkinView ();
		api->RefreshAll ();
	}
	LOGOUT ("Launcher skin: '%s' active", id.toUtf8 ().constData ());
}

void custom::LauncherSkin::Unapply ()
{
	if (skinView) {
		if (mode == SKIN) hider->Restore ();
		LayoutSkin::SetSkinView (dlg, false);
		DestroyView ();
		if (back) back->hide ();
		ApplyMinSize (false);
		skinView = false;
		formsView = false;
	}
	mode = NONE;
	if (!activeId.isEmpty () && !active.qss.empty ()) dlg->setStyleSheet (LayoutRootStyle ());
	activeId.clear ();
	active = SkinManifest ();
	if (api) api->SkinSwitched ();
	UpdateActive ();
}

bool custom::LauncherSkin::ApplyQss (const SkinManifest &m)
{
	QFile f (QString::fromStdString (m.dir) + '/' + QString::fromStdString (m.qss));
	if (!f.open (QIODevice::ReadOnly) || f.size () > 4 * 1024 * 1024) return false;
	QString text = QString::fromUtf8 (f.readAll ());
	QString dir = QString::fromStdString (m.dir);
	dir.replace ("\\", "\\\\");
	dir.replace ("\"", "\\\"");
	text.replace ("${SKIN}", dir);
	const QString root = LayoutRootStyle (); // custom: launcher layouts; the layout's root style first, the skin's after it
	dlg->setStyleSheet (root.isEmpty () ? text : root + "\n" + text);
	return true;
}

void custom::LauncherSkin::Fail (const QString &id, const QString &reason, const QString &hint)
{
	LOGOUT_WARN ("Launcher skin '%s': %s", id.toUtf8 ().constData (), reason.toUtf8 ().constData ());
	QString msg = "The Launchpad skin '" + id + "' could not be used:\n" + reason.left (600) + "\n\n";
	if (!hint.isEmpty ()) msg += hint + "\n\n";
	if (LayoutShown () && id == LayoutSkin::Id ()) msg += "The layout stays until the next start.\n\n"; // custom: launcher layouts
	msg += "Details are in Orbiter.log. Choose Classic in Extra > Launchpad skin, or start Orbiter with ORBITER_LAUNCHER_SKIN=classic.";
	AddPending ([this, msg]() { QMessageBox::warning (dlg, "Orbiter: Launchpad skin", msg); });
}

// ---------------------------------------------------------------------------------------------------------
// the view modules (QML, custom: forms skins) and the view

bool custom::LauncherSkin::LoadModule (bool forms, QString &err)
{
	ViewModule &mod = (forms ? formsModule : qmlModule);
	if (mod.handle) return true;
	std::error_code ec;
	std::string path = fs::absolute (forms ? FORMS_MODULE_FILE : MODULE_FILE, ec).string ();
	void *h = dlopen (path.c_str (), RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		const char *e = dlerror ();
		err = "can't load " + QString::fromStdString (path) + ": " + QString::fromUtf8 (e ? e : "unknown error");
		return false;
	}
	auto c = (LauncherQmlCreateFn)dlsym (h, forms ? LAUNCHERFORMS_CREATE : LAUNCHERQML_CREATE);
	auto d = (LauncherQmlDestroyFn)dlsym (h, forms ? LAUNCHERFORMS_DESTROY : LAUNCHERQML_DESTROY);
	if (!c || !d) {
		err = forms ? "LauncherForms.so has no " LAUNCHERFORMS_CREATE "/" LAUNCHERFORMS_DESTROY : "LauncherQml.so has no " LAUNCHERQML_CREATE "/" LAUNCHERQML_DESTROY;
		dlclose (h);
		return false;
	}
	mod.handle = h; // stays loaded until exit
	mod.create = c;
	mod.destroy = d;
	mod.focus = (LauncherQmlFocusFn)dlsym (h, forms ? LAUNCHERFORMS_FOCUS : LAUNCHERQML_FOCUS);
	return true;
}

bool custom::LauncherSkin::CreateView (QString &err)
{
	ViewModule &mod = Module ();
	if (!mod.create || !api) {
		err = "the launcher module isn't loaded";
		return false;
	}
	std::string dir = active.dir, entry = (formsView ? active.forms : active.qml);
	LauncherQmlInit init;
	init.abi = LAUNCHERQML_ABI;
	init.qtVersion = QT_VERSION_STR;
	init.parent = dlg;
	init.api = api;
	init.skinDir = dir.c_str ();
	init.entry = entry.c_str ();
	init.log = LogLine;
	char buf[2048] = "";
	QWidget *w = mod.create (&init, buf, sizeof (buf));
	if (!w) {
		err = QString::fromUtf8 (buf);
		if (err.isEmpty ()) err = "the skin's view could not be created";
		return false;
	}
	view = w;
	view->setGeometry (dlg->rect ());
	return true;
}

void custom::LauncherSkin::DestroyView ()
{
	if (!view) return;
	QWidget *w = view;
	view = nullptr;
	ViewModule &mod = Module ();
	if (mod.destroy) mod.destroy (w);
	else delete w;
	UpdateActive ();
}

// ---------------------------------------------------------------------------------------------------------
// views

bool custom::LauncherSkin::InSkinView () const
{
	return skinView && mode == SKIN && view && view->isVisible ();
}

void custom::LauncherSkin::EnterSkinView ()
{
	if (!skinView || torn) return;
	hider->Hide (); // before a (re)load of the view, so the classic controls don't show meanwhile
	LayoutSkin::SetSkinView (dlg, true);
	if (back) back->hide ();
	ApplyMinSize (true);
	if (!view) {
		QString err;
		if (!CreateView (err)) {
			QString id = activeId;
			hider->Restore ();
			LayoutSkin::SetSkinView (dlg, false);
			mode = CLASSIC;
			Unapply ();
			SyncEscape ();
			Fail (id, err, QString ());
			return;
		}
	}
	view->setGeometry (dlg->rect ());
	view->show ();
	view->raise ();
	if (!view->isAncestorOf (QApplication::focusWidget ())) view->setFocus (); // custom: forms skins; a line edit inside keeps it
	mode = SKIN;
	UpdateActive ();
	QMetaObject::invokeMethod (this, [this]() { FocusSkin (); }, Qt::QueuedConnection);
}

void custom::LauncherSkin::FocusSkin ()
{
	if (torn || !InSkinView ()) return;
	if (!view->hasFocus () && !view->isAncestorOf (QApplication::focusWidget ())) view->setFocus (Qt::ActiveWindowFocusReason);
	if (Module ().focus) Module ().focus (view); // the QML root loses its focus while the view isn't shown or active
}

void custom::LauncherSkin::EnterClassicView ()
{
	if (!skinView || torn) return;
	if (view) view->hide ();
	hider->Restore ();
	LayoutSkin::SetSkinView (dlg, false);
	ApplyMinSize (false);
	if (back) {
		PlaceBack ();
		back->show ();
		back->raise ();
	}
	mode = CLASSIC;
	UpdateActive ();
}

void custom::LauncherSkin::BackToSkin ()
{
	EnterSkinView ();
	if (api) api->Returned ();
}

void custom::LauncherSkin::PlaceBack ()
{
	if (!back) return;
	back->adjustSize ();
	back->move (dlg->width () - back->width () - 8, 8);
}

void custom::LauncherSkin::ApplyMinSize (bool skin)
{
	QScreen *s = dlg->screen ();
	QRect avail = (s ? s->availableGeometry () : QRect (0, 0, 1920, 1080));
	QSize frame = (dlg->isVisible () ? dlg->frameGeometry ().size () - dlg->size () : QSize (0, 0));
	const QSize lo = LayoutSkin::MinSize ().expandedTo (QSize (CLASSIC_MINW, CLASSIC_MINH)); // custom: launcher layouts; 550 x 350 without one
	const int lw = std::min (lo.width (), std::max (CLASSIC_MINW, avail.width () - frame.width ()));
	const int lh = std::min (lo.height (), std::max (CLASSIC_MINH, avail.height () - frame.height ()));
	if (!skin) {
		dlg->setMinimumSize (lw, lh);
		return;
	}
	int aw = std::max (lw, avail.width () - frame.width ()), ah = std::max (lh, avail.height () - frame.height ());
	int mw = std::clamp (active.minWidth, lw, aw);
	int mh = std::clamp (active.minHeight, lh, ah);
	bool small = (dlg->width () < mw || dlg->height () < mh); // before setMinimumSize grows it to the minimum
	dlg->setMinimumSize (mw, mh);
	if (small) {
		int tw = std::clamp (active.width > 0 ? active.width : mw, mw, aw);
		int th = std::clamp (active.height > 0 ? active.height : mh, mh, ah);
		dlg->resize (std::max (dlg->width (), tw), std::max (dlg->height (), th));
		QRect fg = dlg->frameGeometry ();
		if (dlg->isVisible () && !avail.contains (fg)) // back onto the screen where the window manager lets us move it
			dlg->move (std::clamp (fg.left (), avail.left (), std::max (avail.left (), avail.right () - fg.width () + 1)),
				std::clamp (fg.top (), avail.top (), std::max (avail.top (), avail.bottom () - fg.height () + 1)));
	}
}

void custom::LauncherSkin::UpdateActive ()
{
	if (!api) return;
	api->SetActive (!torn && skinView && mode == SKIN && !waiting && view && view->isVisible ()
		&& dlg->isVisible () && !dlg->isMinimized () && dlg->isActiveWindow ());
}

// ---------------------------------------------------------------------------------------------------------
// events

bool custom::LauncherSkin::eventFilter (QObject *obj, QEvent *event)
{
	if (torn) return false;
	if (obj == dlg) {
		switch (event->type ()) {
		case QEvent::Resize:
			if (view) view->setGeometry (dlg->rect ());
			PlaceBack ();
			if (qEnvironmentVariableIsSet ("ORBITER_LAYOUT_DUMP")) // custom: launcher layouts; after the classic rules ran
				QMetaObject::invokeMethod (this, [this]() { LayoutSkin::Dump (dlg, "resize"); }, Qt::QueuedConnection);
			break;
		case QEvent::KeyPress: {
			int k = static_cast<QKeyEvent*> (event)->key ();
			if (mode == SKIN && skinView && (k == Qt::Key_Return || k == Qt::Key_Enter)) return true; // Enter belongs to the skin
			} break;
		case QEvent::Show:
			UpdateActive ();
			if (api) api->refreshSetup ();
			ScheduleTry ();
			break;
		case QEvent::Hide:
			UpdateActive ();
			QMetaObject::invokeMethod (this, [this]() { OnDialogHidden (); }, Qt::QueuedConnection);
			break;
		case QEvent::WindowActivate:
			QMetaObject::invokeMethod (this, [this]() { FocusSkin (); }, Qt::QueuedConnection); // keys go to the skin
			UpdateActive ();
			ScheduleTry ();
			break;
		case QEvent::WindowDeactivate:
		case QEvent::WindowStateChange:
			UpdateActive ();
			break;
		default:
			break;
		}
	} else if (obj == hWait) {
		if (event->type () == QEvent::Show) {
			waiting = true;
			if (view) view->hide ();
			UpdateActive ();
		} else if (event->type () == QEvent::Hide) {
			QMetaObject::invokeMethod (this, [this]() { OnWaitHidden (); }, Qt::QueuedConnection);
		}
	}
	return false;
}

void custom::LauncherSkin::OnDialogHidden ()
{
	if (torn || lp->Visible ()) return; // minimised, or shown again
	if (view) DestroyView ();          // a flight: no QML engine until the Launchpad is back
}

void custom::LauncherSkin::OnWaitHidden ()
{
	if (torn) return;
	waiting = false;
	if (skinView && mode == SKIN && !switchPending) EnterSkinView (); // recreates the view after a flight and hides Launch/Help/Exit again
	UpdateActive ();
	ScheduleTry ();
}

// ---------------------------------------------------------------------------------------------------------
// pending work: runs at loop level 1, with the Launchpad visible and no modal dialog open

void custom::LauncherSkin::AddPending (std::function<void ()> fn)
{
	if (torn) return;
	pending.push_back (std::move (fn));
	ScheduleTry ();
}

void custom::LauncherSkin::ScheduleTry (int delayMs)
{
	if (torn || pending.empty () || tryQueued) return;
	tryQueued = true;
	auto run = [this]() { tryQueued = false; TryPending (); };
	if (delayMs < 0) QMetaObject::invokeMethod (this, run, Qt::QueuedConnection);
	else QTimer::singleShot (delayMs, this, run);
}

void custom::LauncherSkin::TryPending ()
{
	if (torn || pending.empty ()) return;
	if (!lp->Visible () || waiting) return; // resumes on the dialog's Show and at the end of the wait page
	if (QThread::currentThread ()->loopLevel () != 1 || QApplication::activeModalWidget ()) {
		ScheduleTry (250); // a modal loop, or Orbiter::Create before exec()
		return;
	}
	std::vector<std::function<void ()>> list;
	list.swap (pending);
	for (auto &fn : list) {
		if (torn) break;
		fn ();
	}
}

bool custom::LauncherSkin::CanAct () const
{
	return !torn && lp->Visible () && !waiting && !switchPending && !QApplication::activeModalWidget ();
}

// ---------------------------------------------------------------------------------------------------------
// teardown on aboutToQuit, before CloseApp deletes Config and the Launchpad

void custom::LauncherSkin::Teardown ()
{
	if (torn) return;
	CommitLayoutRun (); // before CloseApp writes Orbiter.cfg
	torn = true;
	pending.clear ();
	SyncEscape (); // filter off, title back
	if (view) DestroyView ();
	if (api) api->Kill ();
	if (item) {
		lp->UnregisterExtraParam (item); // its clbkWriteConfig writes nothing
		delete item;
		item = nullptr;
	}
}
