// custom: launcher skins; hides and restores the classic Launchpad controls under a QML skin

#ifndef __CUSTOM_CLASSICHIDER_H
#define __CUSTOM_CLASSICHIDER_H

#include <QObject>
#include <QPointer>
#include <QList>

class QWidget;

namespace custom {

	class ClassicHider: public QObject {
	public:
		// records the dialog's direct children that are controls of its template (resId set, not windows), except 'exclude'
		ClassicHider (QWidget *dlg, QWidget *exclude);

		void Hide ();          // hides the recorded controls that aren't hidden; any of them shown later is hidden again
		void Restore ();       // shows exactly the controls it hid
		bool Active () const { return active; }
		int Recorded () const { return (int)controls.size (); }
		int HiddenCount () const { return (int)hidden.size (); }

	protected:
		bool eventFilter (QObject *obj, QEvent *event) override;

	private:
		void HideOne (QWidget *w);
		QList<QPointer<QWidget>> controls;
		QList<QPointer<QWidget>> hidden;
		bool active = false;
	};

}

#endif // !__CUSTOM_CLASSICHIDER_H
