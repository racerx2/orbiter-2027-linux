// custom: launcher skins; hides and restores the classic Launchpad controls under a QML skin

#include "ClassicHider.h"
#include <QEvent>
#include <QWidget>

custom::ClassicHider::ClassicHider (QWidget *dlg, QWidget *exclude): QObject (dlg)
{
	for (QWidget *w : dlg->findChildren<QWidget*> (Qt::FindDirectChildrenOnly)) {
		if (w == exclude || w->isWindow () || !w->property ("resId").isValid ()) continue;
		controls.append (w);
		w->installEventFilter (this);
	}
}

void custom::ClassicHider::HideOne (QWidget *w)
{
	if (!w || w->isHidden ()) return;
	w->hide ();
	for (const auto &h : hidden)
		if (h == w) return;
	hidden.append (w);
}

void custom::ClassicHider::Hide ()
{
	active = true;
	for (const auto &w : controls)
		HideOne (w);
}

void custom::ClassicHider::Restore ()
{
	active = false;
	for (const auto &w : hidden)
		if (w) w->show ();
	hidden.clear ();
}

bool custom::ClassicHider::eventFilter (QObject *obj, QEvent *event)
{
	// ShowToParent comes with every show(), also while the dialog itself is hidden
	if (active && event->type () == QEvent::ShowToParent) {
		QPointer<QWidget> w = static_cast<QWidget*> (obj);
		QMetaObject::invokeMethod (this, [this, w]() { if (active) HideOne (w); }, Qt::QueuedConnection);
	}
	return false;
}
