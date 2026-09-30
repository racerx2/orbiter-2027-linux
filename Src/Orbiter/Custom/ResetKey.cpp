// custom: launcher skins; Ctrl+Shift+L back to the classic Launchpad, as an application event filter

#include "ResetKey.h"
#include <QCoreApplication>
#include <QKeyEvent>
#include <QWidget>

custom::ResetKey::ResetKey (QWidget *dlg, std::function<void ()> onReset, QObject *parent)
	: QObject (parent), dlg (dlg), onReset (std::move (onReset))
{
}

custom::ResetKey::~ResetKey ()
{
	Remove ();
}

void custom::ResetKey::Install ()
{
	QCoreApplication *app = QCoreApplication::instance ();
	if (installed || !app) return;
	app->installEventFilter (this);
	installed = true;
}

void custom::ResetKey::Remove ()
{
	if (!installed) return;
	if (QCoreApplication *app = QCoreApplication::instance ()) app->removeEventFilter (this);
	installed = false;
	releaseOwed = false;
}

bool custom::ResetKey::Ours (QObject *obj) const
{
	if (!obj || !dlg) return false;
	if (obj->isWidgetType ()) return static_cast<QWidget*> (obj)->window () == dlg;
	return qml && dlg->isVisible () && obj->inherits ("QQuickWindow"); // not during a flight
}

bool custom::ResetKey::eventFilter (QObject *obj, QEvent *event)
{
	const QEvent::Type t = event->type ();
	if (t != QEvent::ShortcutOverride && t != QEvent::KeyPress && t != QEvent::KeyRelease) return false;
	const QKeyEvent *k = static_cast<QKeyEvent*> (event);
	if (t == QEvent::KeyRelease) {
		if (!releaseOwed || k->key () != Qt::Key_L || !Ours (obj)) return false;
		if (!k->isAutoRepeat ()) releaseOwed = false;
		return true;
	}
	if (!IsResetKey (k->key (), k->modifiers ()) || !Ours (obj)) return false;
	if (t == QEvent::ShortcutOverride) {
		event->accept (); // no QShortcut or QML Shortcut fires on it
		return true;
	}
	if (!k->isAutoRepeat ()) {
		releaseOwed = true;
		if (onReset) onReset ();
	}
	return true;
}
