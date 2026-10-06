
// custom: forms skins; the view the host shows: the built form, its keys and focus, reloading when the files change

#include "FormsView.h"
#include "FormRuntime.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QThread>

namespace forms {

FormsView::FormsView (QWidget *parent, QObject *launcher, const QString &dir, const QString &e, std::function<void (const QString &)> lg)
	: QWidget (parent), api (launcher), skinDir (dir), entry (e), log (std::move (lg))
{
	setFocusPolicy (Qt::TabFocus); // a click on a card keeps a line edit's focus, as QML's MouseAreas do
	debounce.setSingleShot (true);
	debounce.setInterval (400);
	connect (&debounce, &QTimer::timeout, this, [this]() { TryReload (); });
	connect (&watcher, &QFileSystemWatcher::fileChanged, this, [this]() { debounce.start (); });
	connect (&watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { debounce.start (); });
}

FormsView::~FormsView ()
{
	debounce.stop ();
	if (rt) {
		rt->Shutdown ();
		delete rt;
		rt = nullptr;
	}
}

FormRuntime *FormsView::Make (QString &err)
{
	auto *r = new FormRuntime (this, api, log);
	r->onPageChange = [this]() { if (isVisible ()) setFocus (Qt::OtherFocusReason); };
	if (!r->Load (QDir (skinDir).filePath (entry), skinDir, err)) {
		r->Shutdown ();
		delete r;
		return nullptr;
	}
	if (r->Root ()) r->Root ()->setGeometry (rect ());
	return r;
}

bool FormsView::Load (QString &err)
{
	rt = Make (err);
	if (!rt) return false;
	Watch ();
	return true;
}

void FormsView::Watch ()
{
	if (!watcher.files ().isEmpty ()) watcher.removePaths (watcher.files ());
	if (!watcher.directories ().isEmpty ()) watcher.removePaths (watcher.directories ());
	if (!rt) return;
	QStringList paths = rt->Files ();
	QStringList dirs;
	for (const QString &f : paths) {
		const QString d = QFileInfo (f).absolutePath ();
		if (!dirs.contains (d)) dirs << d;
	}
	paths.removeAll (QString ());
	if (!paths.isEmpty ()) watcher.addPaths (paths); // again after each change: Designer saves by rename
	if (!dirs.isEmpty ()) watcher.addPaths (dirs);
}

bool FormsView::ReloadNow (QString &err)
{
	FormRuntime *n = Make (err);
	if (!n) {
		Watch ();
		return false;
	}
	FormRuntime *old = rt;
	rt = n;
	if (old) {
		old->Shutdown ();
		delete old;
	}
	if (rt->Root ()) rt->Root ()->show ();
	rt->ScheduleRefresh (); // Load's refresh stopped at the hidden root
	FocusView ();
	Watch ();
	return true;
}

void FormsView::TryReload ()
{
	if (waitForLoopLevel1 && (QThread::currentThread ()->loopLevel () != 1 || QApplication::activePopupWidget () || QApplication::activeModalWidget ())) {
		debounce.start (500); // not inside a menu, a dialog or a nested loop
		return;
	}
	QString err;
	if (ReloadNow (err)) {
		if (log) log ("reloaded after a change of its files");
	} else {
		if (log) log ("not reloaded: " + err);
		if (rt) rt->Toast ("Not reloaded: " + err.left (160));
	}
}

void FormsView::FocusView ()
{
	QWidget *f = QApplication::focusWidget ();
	if (!hasFocus () && !(f && isAncestorOf (f))) setFocus (Qt::ActiveWindowFocusReason);
}

void FormsView::resizeEvent (QResizeEvent *e)
{
	QWidget::resizeEvent (e);
	if (rt && rt->Root ()) rt->Root ()->setGeometry (rect ());
	if (rt) rt->ScheduleRefresh ();
}

void FormsView::keyPressEvent (QKeyEvent *e)
{
	if (rt) rt->HandleKey (e);
	e->accept (); // as the QML view: the classic dialog gets no keys while the skin shows
}

}
