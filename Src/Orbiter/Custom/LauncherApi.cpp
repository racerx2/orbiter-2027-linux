// custom: launcher skins; the Launcher object QML skins use (API v1); it reads and drives the classic controls

#include "LauncherApi.h"
#include "LauncherSkin.h"
#include "Orbiter.h"
#include "Launchpad.h"
#include "LpadTab.h"
#include "Config.h"
#include "Astro.h"
#include "ResDialog.h"
#include "resource.h"
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QTreeWidget>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

using orbiter::LaunchpadDialog;

namespace {

	template<class T> T *Item (QWidget *parent, int id)
	{
		return parent ? qobject_cast<T*> (oapiResDlgItem (parent, id)) : nullptr;
	}

	QString WidgetText (QWidget *w)
	{
		if (!w) return QString ();
		if (auto *e = qobject_cast<QPlainTextEdit*> (w)) return e->toPlainText ();
		if (auto *e = qobject_cast<QTextEdit*> (w)) return e->toPlainText ();
		if (auto *e = qobject_cast<QLineEdit*> (w)) return e->text ();
		return w->property ("text").toString ();
	}

	QString CleanText (QString s)
	{
		s.replace ("\r\n", "\n");
		s.remove ('\r');
		s.remove (QChar (0xFFFC)); // images of the HTML description, as QTextDocument's plain text marks them
		return s.trimmed ();
	}

	// the classic block order (TabScenario.cpp); HTML through Qt, as the classic Html2Text loops on a lone '<' or '&'
	QString DescriptionOf (const std::string &file, bool html)
	{
		std::ifstream is (file);
		if (!is) return QString ();
		std::string text;
		bool isHtml = false;
		if (html) {
			if (custom::ReadBlock (is, "URLDESC", text)) return QString ();
			if (custom::ReadBlock (is, "HYPERDESC", text)) isHtml = true;
			else custom::ReadBlock (is, "DESC", text);
		} else {
			if (!custom::ReadBlock (is, "DESC", text) && custom::ReadBlock (is, "HYPERDESC", text)) isHtml = true;
		}
		QString q = QString::fromUtf8 (text);
		if (isHtml) q = QTextDocumentFragment::fromHtml (q).toPlainText ();
		return CleanText (q);
	}

	int PageButton (const QString &page)
	{
		if (page == "scenarios") return IDC_MNU_SCN;
		if (page == "parameters") return IDC_MNU_OPT;
		if (page == "modules") return IDC_MNU_MOD;
		if (page == "video") return IDC_MNU_VID;
		if (page == "extra") return IDC_MNU_EXT;
		if (page == "about") return IDC_MNU_ABT;
		return 0;
	}

}

custom::LauncherApi::LauncherApi (LaunchpadDialog *lp, LauncherSkin *host): QObject (host), lp (lp), host (host)
{
	if (QTreeWidget *t = ScnTree ()) {
		QAbstractItemModel *m = t->model ();
		conns << connect (m, &QAbstractItemModel::modelReset, this, [this]() { Schedule (R_SCN | R_CUR); });
		conns << connect (m, &QAbstractItemModel::rowsInserted, this, [this]() { Schedule (R_SCN); });
		conns << connect (m, &QAbstractItemModel::rowsRemoved, this, [this]() { Schedule (R_SCN); });
		conns << connect (t, &QTreeWidget::currentItemChanged, this, [this]() { Schedule (R_CUR); });
	}
	if (QTreeWidget *t = ModTree ()) {
		QAbstractItemModel *m = t->model ();
		conns << connect (m, &QAbstractItemModel::dataChanged, this, [this]() { Schedule (R_MOD | R_SETUP); });
		conns << connect (m, &QAbstractItemModel::modelReset, this, [this]() { Schedule (R_MOD | R_SETUP); });
		conns << connect (m, &QAbstractItemModel::rowsInserted, this, [this]() { Schedule (R_MOD | R_SETUP); });
		conns << connect (m, &QAbstractItemModel::rowsRemoved, this, [this]() { Schedule (R_MOD | R_SETUP); });
	}
	if (QCheckBox *cb = Item<QCheckBox> (TabWnd (PG_SCN), IDC_SCN_PAUSED))
		conns << connect (cb, &QCheckBox::toggled, this, [this]() { if (!dead) emit startPausedChanged (); });
	if (QComboBox *cb = Item<QComboBox> (TabWnd (PG_VID), IDC_VID_COMBO_MODULE)) // SelectClientIndex never disconnects this one
		conns << connect (cb, &QComboBox::currentIndexChanged, this, [this]() { Schedule (R_SETUP); });
	launchBt = Item<QPushButton> (Dlg (), IDLAUNCH);
	if (launchBt) launchBt->installEventFilter (this);
}

// ---------------------------------------------------------------------------------------------------------
// classic controls

QWidget *custom::LauncherApi::Dlg () const
{
	orbiter::LaunchpadTab *t = lp->GetTab (0);
	return t ? t->LaunchpadWnd () : nullptr;
}

QWidget *custom::LauncherApi::TabWnd (int pg) const
{
	orbiter::LaunchpadTab *t = lp->GetTab (pg);
	return t ? t->TabWnd () : nullptr;
}

QTreeWidget *custom::LauncherApi::ScnTree () const
{
	return Item<QTreeWidget> (TabWnd (PG_SCN), IDC_SCN_LIST);
}

QTreeWidget *custom::LauncherApi::ModTree () const
{
	return Item<QTreeWidget> (TabWnd (PG_MOD), IDC_MOD_TREE);
}

bool custom::LauncherApi::IsFolder (QTreeWidgetItem *it) const
{
	return it->childIndicatorPolicy () == QTreeWidgetItem::ShowIndicator;
}

QString custom::LauncherApi::ItemPath (QTreeWidgetItem *it) const
{
	QString p = it->text (0);
	while ((it = it->parent ()))
		p = it->text (0) + '/' + p;
	return p;
}

QTreeWidgetItem *custom::LauncherApi::FindItem (const QString &path) const
{
	QTreeWidget *t = ScnTree ();
	if (!t || path.isEmpty ()) return nullptr;
	const QStringList seg = path.split ('/');
	QTreeWidgetItem *parent = nullptr;
	for (int i = 0; i < seg.size (); i++) {
		bool last = (i == seg.size () - 1);
		int n = (parent ? parent->childCount () : t->topLevelItemCount ());
		QTreeWidgetItem *found = nullptr;
		for (int j = 0; j < n && !found; j++) {
			QTreeWidgetItem *it = (parent ? parent->child (j) : t->topLevelItem (j));
			if (it->text (0) == seg[i] && (last || IsFolder (it))) found = it; // first match, as the classic re-selection
		}
		if (!found) return nullptr;
		parent = found;
	}
	return parent;
}

void custom::LauncherApi::Click (QWidget *parent, int id)
{
	QAbstractButton *b = Item<QAbstractButton> (parent, id);
	if (b && b->isEnabled ()) b->click ();
}

void custom::LauncherApi::ClickPage (const QString &page)
{
	if (int id = PageButton (page)) Click (Dlg (), id);
	else host->Log ("unknown classic page '" + page + "'");
}

// ---------------------------------------------------------------------------------------------------------
// change notification: once per event-loop turn

void custom::LauncherApi::Schedule (int bits)
{
	if (dead) return;
	pendingBits |= bits;
	if (refreshQueued) return;
	refreshQueued = true;
	QMetaObject::invokeMethod (this, [this]() { refreshQueued = false; DoRefresh (); }, Qt::QueuedConnection);
}

void custom::LauncherApi::DoRefresh ()
{
	if (dead) return;
	int b = pendingBits;
	pendingBits = 0;
	if (b & R_SCN) {
		BuildScenarios ();
		emit scenariosChanged ();
		emit recentChanged ();
		emit favouritesChanged ();
	}
	if (b & R_CUR) emit currentScenarioChanged ();
	if (b & R_LAUNCH) emit canLaunchChanged ();
	if (b & R_MOD) {
		BuildModules ();
		emit modulesChanged ();
	}
	if (b & R_SETUP) {
		BuildSetup ();
		emit setupChanged ();
	}
}

bool custom::LauncherApi::eventFilter (QObject *obj, QEvent *event)
{
	if (obj == launchBt && event->type () == QEvent::EnabledChange) Schedule (R_LAUNCH);
	return false;
}

void custom::LauncherApi::Queue (std::function<void ()> fn)
{
	if (dead) return;
	QMetaObject::invokeMethod (this, [this, fn]() {
		if (!dead && host->CanAct ()) fn ();
	}, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------------------------------------
// caches

void custom::LauncherApi::BuildScenarios ()
{
	scnCache.clear ();
	scnKnown.clear ();
	haveScenarios = true;
	QTreeWidget *t = ScnTree ();
	if (!t) return;
	std::function<void (QTreeWidgetItem*, const QString&, int)> walk = [&](QTreeWidgetItem *it, const QString &folder, int depth) {
		QString name = it->text (0);
		QString path = (folder.isEmpty () ? name : folder + '/' + name);
		bool isFolder = IsFolder (it);
		QVariantMap e;
		e["path"] = path;
		e["name"] = name;
		e["folder"] = folder;
		e["isFolder"] = isFolder;
		e["depth"] = depth;
		scnCache.append (e);
		if (!scnKnown.contains (path)) scnKnown.insert (path, isFolder);
		for (int i = 0; i < it->childCount (); i++)
			walk (it->child (i), path, depth + 1);
	};
	for (int i = 0; i < t->topLevelItemCount (); i++)
		walk (t->topLevelItem (i), QString (), 0);
}

QString custom::LauncherApi::ModuleInfo (const QString &name)
{
	auto it = modInfo.find (name);
	if (it != modInfo.end ()) return it.value ();
	fs::path file = fs::path (oapiResolvePath ("Modules/Plugin")) / (name.toStdString () + ".so"); // as RefreshLists
	char buf[1024];
	QString info;
	if (LoadModuleString (file.string ().c_str (), 1000, buf, 1024)) {
		buf[1023] = '\0';
		info = CleanText (QString::fromUtf8 (buf));
	}
	modInfo.insert (name, info);
	return info;
}

void custom::LauncherApi::BuildModules ()
{
	modCache.clear ();
	haveModules = true;
	QTreeWidget *t = ModTree ();
	if (!t) return;
	const auto &cmd = lp->Cfg ()->CfgCmdlinePrm.LoadPlugins;
	for (int i = 0; i < t->topLevelItemCount (); i++) {
		QTreeWidgetItem *cat = t->topLevelItem (i);
		for (int j = 0; j < cat->childCount (); j++) {
			QTreeWidgetItem *it = cat->child (j);
			QString name = it->text (0);
			QVariantMap e;
			e["name"] = name;
			e["category"] = cat->text (0);
			e["active"] = (it->checkState (0) != Qt::Unchecked);
			e["locked"] = (std::find (cmd.begin (), cmd.end (), name.toStdString ()) != cmd.end ()); // as RefreshLists
			e["info"] = ModuleInfo (name);
			modCache.append (e);
		}
	}
}

void custom::LauncherApi::BuildSetup ()
{
	QVariantMap s;
	QWidget *vid = TabWnd (PG_VID);
	QComboBox *cm = Item<QComboBox> (vid, IDC_VID_COMBO_MODULE);
	QComboBox *dv = Item<QComboBox> (vid, IDC_VID_DEVICE);
	QAbstractButton *full = Item<QAbstractButton> (vid, IDC_VID_FULL);
	s["graphicsClient"] = (cm ? cm->currentText () : QString ());
	s["device"] = (dv ? dv->currentText () : QString ());
	s["fullscreen"] = (full && full->isChecked ());
	s["width"] = WidgetText (Item<QWidget> (vid, IDC_VID_WIDTH));
	s["height"] = WidgetText (Item<QWidget> (vid, IDC_VID_HEIGHT));
	int n = 0;
	if (QTreeWidget *t = ModTree ())
		for (int i = 0; i < t->topLevelItemCount (); i++)
			for (int j = 0; j < t->topLevelItem (i)->childCount (); j++)
				if (t->topLevelItem (i)->child (j)->checkState (0) != Qt::Unchecked) n++;
	s["activeModules"] = n;
	const Config *c = lp->Cfg ();
	s["nonsphericalGravity"] = c->CfgPhysicsPrm.bNonsphericalGrav;
	s["radiationPressure"] = c->CfgPhysicsPrm.bRadiationPressure;
	s["distributedMass"] = c->CfgPhysicsPrm.bDistributedMass;
	s["atmWind"] = c->CfgPhysicsPrm.bAtmWind;
	setupCache = s;
	haveSetup = true;
}

QStringList custom::LauncherApi::Existing (const std::vector<std::string> &paths)
{
	if (!haveScenarios) BuildScenarios ();
	QStringList out;
	for (const auto &p : paths) {
		QString q = QString::fromStdString (p);
		auto it = scnKnown.find (q);
		if (it != scnKnown.end () && !it.value ()) out << q;
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------
// properties

int custom::LauncherApi::apiVersion () const { return LAUNCHER_API; }

QString custom::LauncherApi::version () const
{
	return dead ? QString () : WidgetText (Item<QWidget> (Dlg (), IDC_VERSION)).trimmed ();
}

QString custom::LauncherApi::build () const
{
	return dead ? QString () : CleanText (WidgetText (Item<QWidget> (Dlg (), IDC_BLACKBOX)));
}

QString custom::LauncherApi::skin () const { return host->ActiveSkin (); }

QUrl custom::LauncherApi::skinUrl () const
{
	const SkinManifest *m = host->ActiveManifest ();
	return m ? QUrl::fromLocalFile (QString::fromStdString (m->dir) + '/') : QUrl ();
}

QVariantList custom::LauncherApi::skins () const
{
	QVariantList l;
	for (const auto &m : host->Skins ()) {
		QVariantMap e;
		e["id"] = QString::fromStdString (m.id);
		e["name"] = QString::fromStdString (m.name);
		e["author"] = QString::fromStdString (m.author);
		e["version"] = QString::fromStdString (m.version);
		e["description"] = QString::fromStdString (m.description);
		const char *view = (!m.qml.empty () ? "qml" : !m.forms.empty () ? "forms" : nullptr); // custom: forms skins
		e["kind"] = (view && !m.qss.empty () ? QString (view) + "+qss" : view ? QString (view) : !m.qss.empty () ? QString ("qss") : QString ("ui"));
		e["layout"] = !m.ui.empty (); // custom: launcher layouts
		e["compatible"] = m.ok;
		e["reason"] = QString::fromStdString (m.reason);
		l.append (e);
	}
	return l;
}

QVariantList custom::LauncherApi::scenarios ()
{
	if (!dead && !haveScenarios) BuildScenarios ();
	return scnCache;
}

QString custom::LauncherApi::currentScenario () const
{
	QTreeWidget *t = (dead ? nullptr : ScnTree ());
	QTreeWidgetItem *it = (t ? t->currentItem () : nullptr);
	return it ? ItemPath (it) : QString ();
}

void custom::LauncherApi::setCurrentScenario (const QString &path)
{
	if (dead) return;
	QTreeWidget *t = ScnTree ();
	QTreeWidgetItem *it = FindItem (path);
	if (t && it && it != t->currentItem ()) t->setCurrentItem (it);
}

bool custom::LauncherApi::currentIsScenario () const
{
	QTreeWidget *t = (dead ? nullptr : ScnTree ());
	QTreeWidgetItem *it = (t ? t->currentItem () : nullptr);
	return it && !IsFolder (it);
}

QString custom::LauncherApi::currentDescription () const
{
	if (dead) return QString ();
	QWidget *tab = TabWnd (PG_SCN);
	if (lp->App ()->UseHtmlInline ()) {
		QTextBrowser *tb = Item<QTextBrowser> (tab, IDC_SCN_HTML);
		return tb ? CleanText (tb->toPlainText ()) : QString ();
	}
	return CleanText (WidgetText (Item<QWidget> (tab, IDC_SCN_DESC)));
}

bool custom::LauncherApi::canLaunch () const
{
	return !dead && launchBt && launchBt->isEnabled ();
}

bool custom::LauncherApi::startPaused () const
{
	QCheckBox *cb = (dead ? nullptr : Item<QCheckBox> (TabWnd (PG_SCN), IDC_SCN_PAUSED));
	return cb && cb->isChecked ();
}

void custom::LauncherApi::setStartPaused (bool on)
{
	QCheckBox *cb = (dead ? nullptr : Item<QCheckBox> (TabWnd (PG_SCN), IDC_SCN_PAUSED));
	if (cb && cb->isChecked () != on) cb->setChecked (on);
}

QStringList custom::LauncherApi::recent ()
{
	return dead ? QStringList () : Existing (host->Cfg ().recent);
}

QStringList custom::LauncherApi::favourites ()
{
	return dead ? QStringList () : Existing (host->Cfg ().favourites);
}

QVariantList custom::LauncherApi::modules ()
{
	if (!dead && !haveModules) BuildModules ();
	return modCache;
}

QVariantMap custom::LauncherApi::setup ()
{
	if (!dead && !haveSetup) BuildSetup ();
	return setupCache;
}

void custom::LauncherApi::setPage (const QString &p)
{
	QString v = p.left (256);
	if (v == m_page) return;
	m_page = v;
	emit pageChanged ();
}

void custom::LauncherApi::setState (const QVariantMap &s)
{
	QJsonDocument doc = QJsonDocument::fromVariant (QVariant (s));
	if (doc.toJson (QJsonDocument::Compact).size () > 65536) {
		log ("state larger than 64 KiB ignored");
		return;
	}
	m_state = doc.object ().toVariantMap (); // plain data only: object references would outlive the view
	emit stateChanged ();
}

// ---------------------------------------------------------------------------------------------------------
// methods

QVariantMap custom::LauncherApi::scenarioInfo (const QString &path)
{
	QVariantMap r;
	if (dead) return r;
	if (!haveScenarios) BuildScenarios ();
	auto known = scnKnown.find (path);
	if (known == scnKnown.end ()) return r;
	bool isFolder = known.value ();
	std::string file = (isFolder
		? oapiResolvePath ((std::string (lp->Cfg ()->CfgDirPrm.ScnDir) + path.toStdString () + "/Description.txt").c_str ()) // as ScenarioChanged
		: oapiResolvePath ((std::string (lp->Cfg ()->CfgDirPrm.ScnDir) + path.toStdString () + ".scn").c_str ())); // not Config::ScnPath: its buffer is 256 bytes
	std::error_code ec;
	fs::file_time_type mtime = fs::last_write_time (file, ec);
	if (ec && !isFolder) return r;
	auto cached = infoCache.find (path);
	if (cached != infoCache.end () && !ec && cached->mtime == mtime) return cached->map;

	int slash = path.lastIndexOf ('/');
	r["path"] = path;
	r["name"] = path.mid (slash + 1);
	r["folder"] = (slash < 0 ? QString () : path.left (slash));
	r["isFolder"] = isFolder;
	r["description"] = (ec ? QString () : DescriptionOf (file, lp->App ()->UseHtmlInline ()));
	if (!isFolder) {
		std::ifstream is (file);
		ScenarioFacts f = ReadScenario (is);
		r["system"] = QString::fromStdString (f.system);
		r["focus"] = QString::fromStdString (f.focus);
		r["focusClass"] = QString::fromStdString (f.focusClass);
		r["focusStatus"] = QString::fromStdString (f.focusStatus);
		r["focusBody"] = QString::fromStdString (f.focusBody);
		r["focusBase"] = QString::fromStdString (f.focusBase);
		r["focusPad"] = f.focusPad;
		r["vesselCount"] = f.vesselCount;
		QVariantList vl;
		for (const auto &v : f.vessels) {
			QVariantMap e;
			e["name"] = QString::fromStdString (v.name);
			e["className"] = QString::fromStdString (v.cls);
			vl.append (e);
		}
		r["vessels"] = vl;
		double mjd = 0.0;
		bool has = true;
		switch (f.dateKind) {
		case 'M': mjd = f.dateValue; break;
		case 'J': mjd = f.dateValue - 2400000.5; break; // State.cpp
		case 'E': mjd = Jepoch2MJD (f.dateValue); break;
		default: has = false; break;
		}
		if (has && mjd > -100000.0 && mjd < 200000.0) {
			struct tm *t = mjddate (mjd); // Orbiter's own conversion; its tm_mon is 1-based, as DateStr uses it
			r["mjd"] = mjd;
			r["date"] = QString::asprintf ("%04d-%02d-%02d %02d:%02d", t->tm_year + 1900, t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min);
		}
	}
	if (!ec) {
		if (infoCache.size () > 1000) infoCache.clear ();
		infoCache.insert (path, InfoEntry {mtime, r});
	}
	return r;
}

bool custom::LauncherApi::launch (const QString &path)
{
	if (dead) return false;
	QString p = (path.isEmpty () ? currentScenario () : path);
	QTreeWidgetItem *it = FindItem (p);
	if (!it || IsFolder (it)) return false;
	Queue ([this, p]() {
		QTreeWidget *t = ScnTree ();
		QTreeWidgetItem *it = FindItem (p);
		if (!t || !it || IsFolder (it)) return;
		if (it != t->currentItem ()) t->setCurrentItem (it);
		Click (Dlg (), IDLAUNCH);
	});
	return true;
}

void custom::LauncherApi::showClassic (const QString &page)
{
	Queue ([this, page]() {
		host->EnterClassicView ();
		ClickPage (page);
	});
}

void custom::LauncherApi::help (const QString &page)
{
	Queue ([this, page]() {
		ClickPage (page.isEmpty () ? QString ("scenarios") : page);
		Click (Dlg (), IDHELP);
	});
}

void custom::LauncherApi::setModuleActive (const QString &name, bool on)
{
	Queue ([this, name, on]() {
		QTreeWidget *t = ModTree ();
		if (!t) return;
		for (int i = 0; i < t->topLevelItemCount (); i++) {
			QTreeWidgetItem *cat = t->topLevelItem (i);
			for (int j = 0; j < cat->childCount (); j++) {
				QTreeWidgetItem *it = cat->child (j);
				if (it->text (0) != name) continue;
				if ((it->checkState (0) != Qt::Unchecked) != on)
					it->setCheckState (0, on ? Qt::Checked : Qt::Unchecked); // the classic itemChanged handler (de)activates it
				return;
			}
		}
	});
}

void custom::LauncherApi::deactivateAllModules ()
{
	Queue ([this]() { Click (TabWnd (PG_MOD), IDC_MOD_DEACTALL); });
}

void custom::LauncherApi::saveCurrentState ()
{
	Queue ([this]() { Click (TabWnd (PG_SCN), IDC_SCN_SAVE); });
}

void custom::LauncherApi::clearQuicksaves ()
{
	Queue ([this]() { Click (TabWnd (PG_SCN), IDC_SCN_DELQS); });
}

bool custom::LauncherApi::toggleFavourite (const QString &path)
{
	if (dead) return false;
	if (!haveScenarios) BuildScenarios ();
	auto known = scnKnown.find (path);
	if (known == scnKnown.end () || known.value ()) return false;
	bool on = ToggleFavourite (host->Cfg (), path.toStdString ());
	host->SaveCfg ();
	emit favouritesChanged ();
	return on;
}

void custom::LauncherApi::setSkin (const QString &id)
{
	if (!dead && !host->Resetting ()) host->RequestSkin (id); // Ctrl+Shift+L wins over the skin
}

void custom::LauncherApi::refreshSetup ()
{
	Schedule (R_SETUP);
}

bool custom::LauncherApi::openUrl (const QUrl &url)
{
	QString s = url.scheme ().toLower ();
	if (dead || !url.isValid () || (s != "http" && s != "https" && s != "mailto")) return false;
	return QDesktopServices::openUrl (url);
}

void custom::LauncherApi::quit ()
{
	Queue ([this]() { Click (Dlg (), IDEXIT); });
}

void custom::LauncherApi::log (const QString &text)
{
	if (dead || logCount > MAX_LOG) return;
	if (++logCount > MAX_LOG) {
		host->Log ("skin '" + host->ActiveSkin () + "': further log lines suppressed");
		return;
	}
	QString t = text.left (500);
	t.replace ('\n', ' ');
	t.replace ('\r', ' ');
	host->Log ("skin '" + host->ActiveSkin () + "': " + t);
}

// ---------------------------------------------------------------------------------------------------------
// host side

void custom::LauncherApi::SetActive (bool on)
{
	if (dead || on == isActive) return;
	isActive = on;
	emit activeChanged ();
}

void custom::LauncherApi::SkinSwitched ()
{
	if (dead) return;
	m_page.clear ();
	m_state.clear ();
	logCount = 0;
	emit skinChanged ();
	emit skinsChanged ();
	emit pageChanged ();
	emit stateChanged ();
}

void custom::LauncherApi::SkinsRescanned ()
{
	if (!dead) emit skinsChanged ();
}

void custom::LauncherApi::RecentChanged ()
{
	if (!dead) emit recentChanged ();
}

void custom::LauncherApi::Returned ()
{
	if (dead) return;
	Schedule (R_SETUP);
	emit returnedFromClassic ();
}

void custom::LauncherApi::RefreshAll ()
{
	Schedule (R_SCN | R_CUR | R_LAUNCH | R_MOD | R_SETUP);
}

void custom::LauncherApi::Kill ()
{
	dead = true;
	for (const auto &c : conns)
		disconnect (c);
	conns.clear ();
	if (launchBt) launchBt->removeEventFilter (this);
}
