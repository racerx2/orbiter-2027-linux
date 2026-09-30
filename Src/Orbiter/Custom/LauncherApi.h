// custom: launcher skins; the Launcher object QML skins use (API v1); it reads and drives the classic controls

#ifndef __CUSTOM_LAUNCHERAPI_H
#define __CUSTOM_LAUNCHERAPI_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <filesystem>
#include <functional>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

namespace orbiter { class LaunchpadDialog; }

namespace custom {

	class LauncherSkin;

	class LauncherApi: public QObject {
		Q_OBJECT
		Q_PROPERTY(int apiVersion READ apiVersion CONSTANT)
		Q_PROPERTY(QString version READ version CONSTANT)
		Q_PROPERTY(QString build READ build CONSTANT)
		Q_PROPERTY(QString skin READ skin NOTIFY skinChanged)
		Q_PROPERTY(QUrl skinUrl READ skinUrl NOTIFY skinChanged)
		Q_PROPERTY(QVariantList skins READ skins NOTIFY skinsChanged)
		Q_PROPERTY(QVariantList scenarios READ scenarios NOTIFY scenariosChanged)
		Q_PROPERTY(QString currentScenario READ currentScenario WRITE setCurrentScenario NOTIFY currentScenarioChanged)
		Q_PROPERTY(bool currentIsScenario READ currentIsScenario NOTIFY currentScenarioChanged)
		Q_PROPERTY(QString currentDescription READ currentDescription NOTIFY currentScenarioChanged)
		Q_PROPERTY(bool canLaunch READ canLaunch NOTIFY canLaunchChanged)
		Q_PROPERTY(bool startPaused READ startPaused WRITE setStartPaused NOTIFY startPausedChanged)
		Q_PROPERTY(QStringList recent READ recent NOTIFY recentChanged)
		Q_PROPERTY(QStringList favourites READ favourites NOTIFY favouritesChanged)
		Q_PROPERTY(QVariantList modules READ modules NOTIFY modulesChanged)
		Q_PROPERTY(QVariantMap setup READ setup NOTIFY setupChanged)
		Q_PROPERTY(bool active READ active NOTIFY activeChanged)
		Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
		Q_PROPERTY(QVariantMap state READ state WRITE setState NOTIFY stateChanged)

	public:
		LauncherApi (orbiter::LaunchpadDialog *lp, LauncherSkin *host);

		int apiVersion () const;
		QString version () const;
		QString build () const;
		QString skin () const;
		QUrl skinUrl () const;
		QVariantList skins () const;
		QVariantList scenarios ();
		QString currentScenario () const;
		void setCurrentScenario (const QString &path);
		bool currentIsScenario () const;
		QString currentDescription () const;
		bool canLaunch () const;
		bool startPaused () const;
		void setStartPaused (bool on);
		QStringList recent ();
		QStringList favourites ();
		QVariantList modules ();
		QVariantMap setup ();
		bool active () const { return isActive; }
		QString page () const { return m_page; }
		void setPage (const QString &p);
		QVariantMap state () const { return m_state; }
		void setState (const QVariantMap &s);

		Q_INVOKABLE QVariantMap scenarioInfo (const QString &path);
		Q_INVOKABLE bool launch (const QString &path = QString ());
		Q_INVOKABLE void showClassic (const QString &page);
		Q_INVOKABLE void help (const QString &page = QString ());
		Q_INVOKABLE void setModuleActive (const QString &name, bool on);
		Q_INVOKABLE void deactivateAllModules ();
		Q_INVOKABLE void saveCurrentState ();
		Q_INVOKABLE void clearQuicksaves ();
		Q_INVOKABLE bool toggleFavourite (const QString &path);
		Q_INVOKABLE void setSkin (const QString &id);
		Q_INVOKABLE void refreshSetup ();
		Q_INVOKABLE bool openUrl (const QUrl &url);
		Q_INVOKABLE void quit ();
		Q_INVOKABLE void log (const QString &text);

		// host side
		void SetActive (bool on);
		void SkinSwitched ();          // active skin or skin list changed; resets page and state
		void SkinsRescanned ();
		void RecentChanged ();
		void Returned ();              // Back from classic view
		void RefreshAll ();
		void Kill ();                  // teardown: every method returns at once from now on

	signals:
		void skinChanged ();
		void skinsChanged ();
		void scenariosChanged ();
		void currentScenarioChanged ();
		void canLaunchChanged ();
		void startPausedChanged ();
		void recentChanged ();
		void favouritesChanged ();
		void modulesChanged ();
		void setupChanged ();
		void activeChanged ();
		void pageChanged ();
		void stateChanged ();
		void returnedFromClassic ();

	protected:
		bool eventFilter (QObject *obj, QEvent *event) override;

	private:
		enum { R_SCN = 1, R_CUR = 2, R_LAUNCH = 4, R_MOD = 8, R_SETUP = 16 };
		void Schedule (int bits);
		void DoRefresh ();
		void Queue (std::function<void ()> fn);
		void Click (QWidget *parent, int id);
		void ClickPage (const QString &page);
		QWidget *Dlg () const;
		QWidget *TabWnd (int pg) const;
		QTreeWidget *ScnTree () const;
		QTreeWidget *ModTree () const;
		QTreeWidgetItem *FindItem (const QString &path) const;
		QString ItemPath (QTreeWidgetItem *it) const;
		bool IsFolder (QTreeWidgetItem *it) const;
		QString ModuleInfo (const QString &name);
		void BuildScenarios ();
		void BuildModules ();
		void BuildSetup ();
		QStringList Existing (const std::vector<std::string> &paths);

		orbiter::LaunchpadDialog *lp;
		LauncherSkin *host;
		bool dead = false;
		bool isActive = false;
		int pendingBits = 0;
		bool refreshQueued = false;
		QList<QMetaObject::Connection> conns;
		QPointer<QPushButton> launchBt;

		bool haveScenarios = false, haveModules = false, haveSetup = false;
		QVariantList scnCache;
		QHash<QString, bool> scnKnown; // path -> isFolder
		QVariantList modCache;
		QVariantMap setupCache;
		QHash<QString, QString> modInfo;
		struct InfoEntry { std::filesystem::file_time_type mtime; QVariantMap map; };
		QHash<QString, InfoEntry> infoCache;
		QString m_page;
		QVariantMap m_state;
	};

}

#endif // !__CUSTOM_LAUNCHERAPI_H
