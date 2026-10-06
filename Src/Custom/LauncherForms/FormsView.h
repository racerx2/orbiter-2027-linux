
// custom: forms skins; the view the host shows: the built form, its keys and focus, reloading when the files change

#ifndef __FORMS_FORMSVIEW_H
#define __FORMS_FORMSVIEW_H

#include <QFileSystemWatcher>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

namespace forms {

	class FormRuntime;

	class FormsView: public QWidget {
		Q_OBJECT
	public:
		FormsView (QWidget *parent, QObject *launcher, const QString &skinDir, const QString &entry, std::function<void (const QString &)> log);
		~FormsView ();
		bool Load (QString &err);
		void FocusView ();               // the view, unless the focus is inside it already
		bool ReloadNow (QString &err);   // builds the form again; the old one stays on failure
		FormRuntime *Runtime () const { return rt; }
		bool waitForLoopLevel1 = true;   // tests reload from their own loop

	protected:
		void resizeEvent (QResizeEvent *e) override;
		void keyPressEvent (QKeyEvent *e) override;
		bool focusNextPrevChild (bool) override { return true; } // Tab stays in the skin

	private:
		FormRuntime *Make (QString &err);
		void Watch ();
		void TryReload ();
		QPointer<QObject> api;
		QString skinDir, entry;
		std::function<void (const QString &)> log;
		FormRuntime *rt = nullptr;
		QFileSystemWatcher watcher;
		QTimer debounce;
	};

}

#endif // !__FORMS_FORMSVIEW_H
