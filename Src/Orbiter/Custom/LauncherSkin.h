// custom: launcher skins; the skin host of the Launchpad dialog (QSS and QML skins, modes, teardown)

#ifndef __CUSTOM_LAUNCHERSKIN_H
#define __CUSTOM_LAUNCHERSKIN_H

#include "LauncherFacts.h"
#include "LauncherQmlAbi.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <vector>

class QPushButton;
class QWidget;

namespace orbiter { class LaunchpadDialog; }

namespace custom {

	class ClassicHider;
	class LauncherApi;
	class LauncherItem;
	class ResetKey;

	class LauncherSkin: public QObject {
	public:
		// called by LaunchpadDialog::Create after the tabs are set up, before the first Show
		static void Attach (orbiter::LaunchpadDialog *lp);
		~LauncherSkin ();

		orbiter::LaunchpadDialog *Launchpad () const { return lp; }
		QWidget *Dialog () const { return dlg; }

		void ScanSkins (const QString &next = QString ()); // next: the skin an Apply right after needs, kept past the cap
		const std::vector<SkinManifest> &Skins () const { return skins; }
		const SkinManifest *ActiveManifest () const;
		QString ActiveSkin () const { return activeId; }
		QString StoredSkin () const { return QString::fromStdString (cfg.skin); }
		void RequestSkin (const QString &id);   // the user's choice: stored, then switched when safe
		bool SwitchPending () const { return switchPending; }
		bool Resetting () const { return resetting; } // Ctrl+Shift+L pressed, switch to Classic not run yet

		LauncherCfg &Cfg () { return cfg; }
		void SaveCfg ();

		bool InSkinView () const;               // QML or forms view shown
		bool CanAct () const;                   // classic actions allowed now (7.2 of the design)
		void EnterClassicView ();
		void Log (const QString &line) const;
		QString NextLayout (const QString &id) const; // the layout a skin brings at the next start, "" for none
		QString RestartNote (const QString &id) const; // what choosing that skin means for the layout, "" if nothing
		void CommitLayoutRun ();

	protected:
		bool eventFilter (QObject *obj, QEvent *event) override;

	private:
		LauncherSkin (orbiter::LaunchpadDialog *lp);

		enum View { NONE, SKIN, CLASSIC };

		void Apply (const QString &id, bool startClassic);
		void Unapply ();
		bool ApplyQss (const SkinManifest &m);
		bool LoadModule (bool forms, QString &err);
		bool CreateView (QString &err);
		void DestroyView ();
		void EnterSkinView ();
		void FocusSkin ();
		void BackToSkin ();
		void ApplyMinSize (bool skin);
		void PlaceBack ();
		void Fail (const QString &id, const QString &reason, const QString &hint);
		void RecordLaunch ();
		QString SelectedScenario () const;
		void AddPending (std::function<void ()> fn);
		void ScheduleTry (int delayMs = -1);
		void TryPending ();
		void OnWaitHidden ();
		void OnDialogHidden ();
		void Teardown ();
		void UpdateActive ();
		void SyncEscape ();
		void ResetToClassic ();
		void FocusClassic ();

		orbiter::LaunchpadDialog *lp;
		QWidget *dlg;
		QWidget *hWait;
		ClassicHider *hider = nullptr;
		LauncherApi *api = nullptr;
		LauncherItem *item = nullptr;
		QPushButton *back = nullptr;
		ResetKey *resetKey = nullptr;
		QString baseTitle;
		QPointer<QWidget> view;

		LauncherCfg cfg;
		std::vector<SkinManifest> skins;
		QString activeId;
		SkinManifest active;
		bool skinView = false;   // the active skin has a view (QML or forms)
		bool formsView = false;  // custom: forms skins; that view is a form
		View mode = NONE;
		bool waiting = false;
		bool torn = false;

		std::vector<std::function<void ()>> pending;
		bool tryQueued = false;
		bool switchPending = false;
		bool resetting = false;
		QString switchTarget;
		QString launching;       // the scenario selected when Launch was pressed

		struct ViewModule {
			void *handle = nullptr;
			LauncherQmlCreateFn create = nullptr;
			LauncherQmlDestroyFn destroy = nullptr;
			LauncherQmlFocusFn focus = nullptr;
		};
		ViewModule qmlModule, formsModule; // each loaded once, kept until exit
		ViewModule &Module () { return formsView ? formsModule : qmlModule; }
	};

}

#endif // !__CUSTOM_LAUNCHERSKIN_H
