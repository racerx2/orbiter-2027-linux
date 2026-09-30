// custom: launcher skins; the skin host of the Launchpad dialog (QSS and QML skins, modes, teardown)

#include "LauncherSkin.h"
#include "ClassicHider.h"
#include "LauncherApi.h"
#include "LauncherItem.h"
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
	const size_t MAX_SKINS = 200;

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
	LoadLauncherCfg (CFG_FILE, cfg);
	hider = new ClassicHider (dlg, hWait);
	item = new LauncherItem (this);
	lp->RegisterExtraParam (item, nullptr);
	if (QPushButton *b = DlgItem<QPushButton> (dlg, IDLAUNCH))
		connect (b, &QPushButton::clicked, this, [this]() { RecordLaunch (); }); // after the classic handler, which launched
	dlg->installEventFilter (this);
	if (hWait) hWait->installEventFilter (this);
	connect (qApp, &QCoreApplication::aboutToQuit, this, [this]() { Teardown (); });

	QString id;
	const Config *c = lp->Cfg ();
	if (!c->CfgDemoPrm.bDemo) {
		if (qEnvironmentVariableIsSet ("ORBITER_LAUNCHER_SKIN")) id = QString::fromUtf8 (qgetenv ("ORBITER_LAUNCHER_SKIN")).trimmed ();
		else id = StoredSkin ();
		if (!id.compare ("classic", Qt::CaseInsensitive)) id.clear ();
	}
	ScanSkins ();
	if (!id.isEmpty ()) Apply (id, c->CfgCmdlinePrm.bOpenVideoTab);
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
		if (!switchTarget.isEmpty ()) Apply (switchTarget, false);
		if (api) api->SkinSwitched ();
	});
}

void custom::LauncherSkin::RecordLaunch ()
{
	if (torn) return;
	orbiter::LaunchpadTab *tab = lp->GetTab (PG_SCN);
	QTreeWidget *t = (tab && tab->TabWnd () ? DlgItem<QTreeWidget> (tab->TabWnd (), IDC_SCN_LIST) : nullptr);
	QTreeWidgetItem *it = (t ? t->currentItem () : nullptr);
	if (!it || it->childIndicatorPolicy () == QTreeWidgetItem::ShowIndicator) return;
	QString p = it->text (0);
	for (QTreeWidgetItem *q = it->parent (); q; q = q->parent ())
		p = q->text (0) + '/' + p;
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
	if (!m.qml.empty ()) {
		QString err;
		if (!LoadModule (err)) {
			Fail (id, err, "The Qt Quick libraries may be missing (Ubuntu: libqt6quickwidgets6).");
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
	if (!m.qml.empty ()) {
		if (!api) api = new LauncherApi (lp, this);
		api->SkinSwitched ();
		QString err;
		if (!CreateView (err)) {
			dlg->setStyleSheet (QString ());
			activeId.clear ();
			active = SkinManifest ();
			api->SkinSwitched ();
			Fail (id, err, err.contains ("\"QtQuick\" is not installed") ? "Install the Qt Quick QML modules (Ubuntu: qml6-module-qtquick)." : QString ());
			return;
		}
		qml = true;
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
	if (qml) {
		if (mode == SKIN) hider->Restore ();
		DestroyView ();
		if (back) back->hide ();
		ApplyMinSize (false);
		qml = false;
	}
	mode = NONE;
	if (!activeId.isEmpty () && !active.qss.empty ()) dlg->setStyleSheet (QString ());
	activeId.clear ();
	active = SkinManifest ();
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
	dlg->setStyleSheet (text);
	return true;
}

void custom::LauncherSkin::Fail (const QString &id, const QString &reason, const QString &hint)
{
	LOGOUT_WARN ("Launcher skin '%s': %s", id.toUtf8 ().constData (), reason.toUtf8 ().constData ());
	QString msg = "The Launchpad skin '" + id + "' could not be used:\n" + reason.left (600) + "\n\n";
	if (!hint.isEmpty ()) msg += hint + "\n\n";
	msg += "Details are in Orbiter.log. Choose Classic in Extra > Launchpad skin, or start Orbiter with ORBITER_LAUNCHER_SKIN=classic.";
	AddPending ([this, msg]() { QMessageBox::warning (dlg, "Orbiter: Launchpad skin", msg); });
}

// ---------------------------------------------------------------------------------------------------------
// the QML module and view

bool custom::LauncherSkin::LoadModule (QString &err)
{
	if (module) return true;
	std::error_code ec;
	std::string path = fs::absolute (MODULE_FILE, ec).string ();
	void *h = dlopen (path.c_str (), RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		const char *e = dlerror ();
		err = "can't load " + QString::fromStdString (path) + ": " + QString::fromUtf8 (e ? e : "unknown error");
		return false;
	}
	auto c = (LauncherQmlCreateFn)dlsym (h, LAUNCHERQML_CREATE);
	auto d = (LauncherQmlDestroyFn)dlsym (h, LAUNCHERQML_DESTROY);
	if (!c || !d) {
		err = "LauncherQml.so has no " LAUNCHERQML_CREATE "/" LAUNCHERQML_DESTROY;
		dlclose (h);
		return false;
	}
	module = h; // stays loaded until exit
	qmlCreate = c;
	qmlDestroy = d;
	return true;
}

bool custom::LauncherSkin::CreateView (QString &err)
{
	if (!qmlCreate || !api) {
		err = "the QML module isn't loaded";
		return false;
	}
	std::string dir = active.dir, entry = active.qml;
	LauncherQmlInit init;
	init.abi = LAUNCHERQML_ABI;
	init.qtVersion = QT_VERSION_STR;
	init.parent = dlg;
	init.api = api;
	init.skinDir = dir.c_str ();
	init.entry = entry.c_str ();
	init.log = LogLine;
	char buf[2048] = "";
	QWidget *w = qmlCreate (&init, buf, sizeof (buf));
	if (!w) {
		err = QString::fromUtf8 (buf);
		if (err.isEmpty ()) err = "the QML view could not be created";
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
	if (qmlDestroy) qmlDestroy (w);
	else delete w;
	UpdateActive ();
}

// ---------------------------------------------------------------------------------------------------------
// views

bool custom::LauncherSkin::InSkinView () const
{
	return qml && mode == SKIN && view && view->isVisible ();
}

void custom::LauncherSkin::EnterSkinView ()
{
	if (!qml || torn) return;
	if (!view) {
		QString err;
		if (!CreateView (err)) {
			QString id = activeId;
			Unapply ();
			Fail (id, err, QString ());
			return;
		}
	}
	hider->Hide ();
	if (back) back->hide ();
	ApplyMinSize (true);
	view->setGeometry (dlg->rect ());
	view->show ();
	view->raise ();
	view->setFocus ();
	mode = SKIN;
	UpdateActive ();
}

void custom::LauncherSkin::EnterClassicView ()
{
	if (!qml || torn) return;
	if (view) view->hide ();
	hider->Restore ();
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
	if (!skin) {
		dlg->setMinimumSize (CLASSIC_MINW, CLASSIC_MINH);
		return;
	}
	QScreen *s = dlg->screen ();
	QRect avail = (s ? s->availableGeometry () : QRect (0, 0, 1920, 1080));
	int aw = std::max (CLASSIC_MINW, avail.width ()), ah = std::max (CLASSIC_MINH, avail.height ());
	int mw = std::clamp (active.minWidth, CLASSIC_MINW, aw);
	int mh = std::clamp (active.minHeight, CLASSIC_MINH, ah);
	dlg->setMinimumSize (mw, mh);
	if (dlg->width () < mw || dlg->height () < mh) {
		int tw = std::clamp (active.width > 0 ? active.width : mw, mw, aw);
		int th = std::clamp (active.height > 0 ? active.height : mh, mh, ah);
		dlg->resize (std::max (dlg->width (), tw), std::max (dlg->height (), th));
	}
}

void custom::LauncherSkin::UpdateActive ()
{
	if (!api) return;
	api->SetActive (!torn && qml && mode == SKIN && !waiting && view && view->isVisible ()
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
			break;
		case QEvent::KeyPress: {
			int k = static_cast<QKeyEvent*> (event)->key ();
			if (mode == SKIN && qml && (k == Qt::Key_Return || k == Qt::Key_Enter)) return true; // Enter belongs to the skin
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
	if (qml && mode == SKIN) EnterSkinView (); // recreates the view after a flight and hides Launch/Help/Exit again
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
	torn = true;
	pending.clear ();
	if (view) DestroyView ();
	if (api) api->Kill ();
	if (item) {
		lp->UnregisterExtraParam (item); // writes Launcher.cfg through clbkWriteConfig once more
		delete item;
		item = nullptr;
	}
}
