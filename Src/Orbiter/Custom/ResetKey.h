// custom: launcher skins; Ctrl+Shift+L back to the classic Launchpad, as an application event filter

#ifndef __CUSTOM_RESETKEY_H
#define __CUSTOM_RESETKEY_H

#include <QObject>
#include <QPointer>
#include <functional>

class QWidget;

namespace custom {

	inline bool IsResetKey (int key, Qt::KeyboardModifiers m)
	{
		const Qt::KeyboardModifiers mask = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier;
		return key == Qt::Key_L && (m & mask) == (Qt::ControlModifier | Qt::ShiftModifier);
	}

	class ResetKey: public QObject {
	public:
		ResetKey (QWidget *dlg, std::function<void ()> onReset, QObject *parent = nullptr);
		~ResetKey ();
		void Install ();
		void Remove ();
		bool Installed () const { return installed; }
		void SetQml (bool on) { qml = on; } // QML windows of the skin are watched too

	protected:
		bool eventFilter (QObject *obj, QEvent *event) override;

	private:
		bool Ours (QObject *obj) const;

		QPointer<QWidget> dlg;
		std::function<void ()> onReset;
		bool installed = false;
		bool qml = false;
		bool releaseOwed = false; // the release of L after a reset press is taken too
	};

}

#endif // !__CUSTOM_RESETKEY_H
