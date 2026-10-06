
// custom: launcher layouts; the layout of this run: the choice, the first check, the dialog hook, export and dump

#include "LayoutSkin.h"
#include "LayoutApply.h"
#include "LayoutHook.h"
#include "UiForm.h"
#include "Config.h"
#include "Log.h"
#include "ResDialog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QWidget>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

	const char *CFG_FILE = "Launcher.cfg";
	const char *SKIN_DIR = "Skins";

	struct Run {
		bool installed = false, inUse = false, undone = false;
		QString id, skinDir, uiDir;
		QString run;  // LayoutRun to store when Orbiter.cfg is written
		QSize minSize {custom::LAYOUT_MINW, custom::LAYOUT_MINH}, refSize;
		QString rootStyle, stockTitle;
		int errors = 0;
		void *hInst = nullptr;
	};

	Run &R ()
	{
		static Run *r = new Run;
		return *r;
	}

	void Log (const QString &s)
	{
		LOGOUT ("Launcher layout: %s", s.toUtf8 ().constData ());
	}

	void Warn (const QString &s)
	{
		LOGOUT_WARN ("Launcher layout: %s", s.toUtf8 ().constData ());
	}

	const RESDIALOG *FindDialog (const QString &name)
	{
		const RESTABLE *t = OrbiterResources ();
		for (size_t i = 0; t && i < t->ndlg; i++)
			if (t->dlg[i].name && name == QString::fromUtf8 (t->dlg[i].name)) return t->dlg + i;
		return nullptr;
	}

	// every form at the start, so one message covers them
	void Check (Run &r)
	{
		QDir dir (r.uiDir);
		for (const QFileInfo &fi : dir.entryInfoList ({"*.ui"}, QDir::Files, QDir::Name)) {
			const QString name = fi.completeBaseName ();
			if (!custom::IsLayoutDialog (name)) {
				Log (fi.fileName () + " is not a Launchpad dialog; ignored");
				continue;
			}
			custom::UiForm form;
			QString err;
			if (custom::ReadUiFile (fi.absoluteFilePath (), form, err)) err = custom::CheckLayoutForm (form, name);
			if (!err.isEmpty ()) {
				Warn (fi.fileName () + ": " + err + "; that dialog stays stock");
				r.errors++;
			}
		}
	}

	void Hook (QWidget *dlg, const RESDIALOG *d, void *hModule)
	{
		Run &r = R ();
		if (!r.inUse || r.undone || !dlg || !d || !d->name) return;
		if (oapiResourceTable (hModule) != OrbiterResources ()) return; // add-on dialogs stay as they are
		const QString name = QString::fromUtf8 (d->name);
		if (!custom::IsLayoutDialog (name)) return;
		const QString file = r.uiDir + "/" + name + ".ui";
		if (!QFileInfo (file).isFile ()) return; // popups read their file at every open
		custom::UiForm form;
		custom::LayoutInfo info;
		QString err;
		bool ok = custom::ReadUiFile (file, form, err) &&
			custom::ApplyLayout (dlg, d, form, r.skinDir, file, [](const QString &s) { Log (s); }, info, err);
		if (!ok) {
			Warn (name + ".ui: " + err + "; that dialog stays stock");
			return;
		}
		if (name == "IDD_MAIN") {
			r.refSize = info.refSize;
			r.minSize = info.minSize;
			r.rootStyle = info.rootStyle;
		}
	}

	bool WriteFile (const QString &path, const QByteArray &data)
	{
		QFile f (path);
		return f.open (QIODevice::WriteOnly | QIODevice::Truncate) && f.write (data) == data.size ();
	}

	const char *README =
		"Launchpad layout (custom build), made by Extra > Launchpad skin > New layout...\n"
		"\n"
		"ui/ holds one Qt Designer form per Launchpad dialog: IDD_MAIN.ui is the main window, IDD_PAGE_*.ui its pages,\n"
		"IDD_OPTIONS_*.ui the Options pages, IDD_EXTRA_*.ui the Extra dialogs, IDD_SAVESCN.ui and IDD_MSG.ui the small\n"
		"windows. Open them in Qt 6 Designer (/usr/lib/qt6/bin/designer or designer6), not Qt 5 Designer.\n"
		"\n"
		"What you can do:\n"
		"- move and resize controls, also into group boxes and frames;\n"
		"- change texts, titles, tool tips, fonts, style sheets, alignment, word wrap and flat buttons;\n"
		"- replace a picture with a file inside this folder (images/ holds the stock ones);\n"
		"- delete a control to hide it (Launch, Exit, the page buttons and page area, the Extra list and its Edit button,\n"
		"  and the Options list and page area always stay);\n"
		"- add labels, pictures, frames, lines and group boxes as decorations; they let clicks through. A dynamic string\n"
		"  property \"anchor\" with \"right\" and/or \"bottom\" keeps one at its distance from that edge;\n"
		"- change the tab order (Edit > Edit Tab Order).\n"
		"\n"
		"Rules:\n"
		"- absolute positions only, no Designer layouts (Form > Break Layout);\n"
		"- keep the dynamic properties orbiterCtl and orbiterControls: Orbiter finds its controls by them, not by name;\n"
		"- orbiterNote tells what Orbiter does with a control itself: \"Orbiter places this\" controls are moved or sized\n"
		"  by Orbiter at the start and when the window is resized; \"Orbiter sets this text\" texts are replaced;\n"
		"- framed boxes with orbiterStandIn are areas Orbiter draws itself: only their position and size are used;\n"
		"- the form's size is the starting size; IDD_MAIN can't be smaller than 550 x 350;\n"
		"- Options pages have no scroll bar: a page taller than its area is cut off;\n"
		"- ${SKIN} in a style sheet is this folder;\n"
		"- the main window, its pages and the Options pages change when Orbiter starts again, the other windows at\n"
		"  their next open;\n"
		"- Ctrl+Shift+L in the Launchpad undoes the layout's looks at once; the next start uses the stock layout;\n"
		"- problems are written to Orbiter.log (\"Launcher layout:\").\n";

}

QString custom::LayoutSkin::StartSkin (const Config *cfg, const LauncherCfg &lc)
{
	if (cfg && cfg->CfgDemoPrm.bDemo) return QString ();
	QString id = (qEnvironmentVariableIsSet ("ORBITER_LAUNCHER_SKIN") ? QString::fromUtf8 (qgetenv ("ORBITER_LAUNCHER_SKIN")).trimmed ()
		: QString::fromStdString (lc.skin));
	if (!id.compare ("classic", Qt::CaseInsensitive)) id.clear ();
	return id;
}

void custom::LayoutSkin::Install (Config *cfg, void *hInst)
{
	Run &r = R ();
	if (r.installed) return;
	r.installed = true;
	r.hInst = hInst;
	if (const RESDIALOG *d = FindDialog ("IDD_MAIN"); d && d->caption) r.stockTitle = QString::fromUtf8 (d->caption);

	LauncherCfg lc;
	LoadLauncherCfg (CFG_FILE, lc);
	const QString id = StartSkin (cfg, lc);
	if (!id.isEmpty () && !id.contains ('/') && id != "." && id != "..") {
		SkinManifest m = ReadSkin ((fs::path (SKIN_DIR) / id.toStdString ()).string ());
		if (m.ok && !m.ui.empty ()) {
			r.inUse = true;
			r.id = id;
			r.skinDir = QString::fromStdString (m.dir);
			r.uiDir = QDir (QString::fromStdString ((fs::path (m.dir) / m.ui).string ())).canonicalPath ();
		}
	}

	// the list widths Orbiter.cfg keeps would override the layout's splits
	const QString run = (r.inUse ? r.id : QString ());
	if (QString::fromStdString (lc.layoutRun) != run) {
		if (cfg) {
			cfg->CfgWindowPos.LaunchpadScnListWidth = 0;
			cfg->CfgWindowPos.LaunchpadModListWidth = 0;
			cfg->CfgWindowPos.LaunchpadExtListWidth = 0;
		}
		Log ("now " + (run.isEmpty () ? QString ("the stock layout") : "'" + run + "'") + ": the saved list widths start again from the layout");
	}
	r.run = run;
	if (!r.inUse) return;
	Check (r);
	g_resDialogHook = Hook;
	Log ("'" + r.id + "' in use, forms in " + r.uiDir);
}

bool custom::LayoutSkin::InUse () { return R ().inUse; }
QString custom::LayoutSkin::RunId () { return R ().run; }
bool custom::LayoutSkin::Undone () { return R ().undone; }
QString custom::LayoutSkin::Id () { return R ().id; }
QSize custom::LayoutSkin::MinSize () { return R ().inUse ? R ().minSize : QSize (LAYOUT_MINW, LAYOUT_MINH); }
QSize custom::LayoutSkin::RefSize () { return R ().refSize; }
QString custom::LayoutSkin::RootStyle () { return R ().rootStyle; }
QString custom::LayoutSkin::StockTitle () { return R ().stockTitle; }
int custom::LayoutSkin::Errors () { return R ().errors; }

void custom::LayoutSkin::Undo ()
{
	Run &r = R ();
	if (!r.inUse || r.undone) return;
	r.undone = true;
	g_resDialogHook = nullptr; // popups open stock for the rest of the run
	UndoLayouts ();
	Log ("undone (Ctrl+Shift+L); the next start uses the stock layout");
}

void custom::LayoutSkin::SetSkinView (QWidget *mainDlg, bool on)
{
	if (R ().inUse) SetLayoutSkinView (mainDlg, on);
}

void custom::LayoutSkin::Dump (QWidget *mainDlg, const char *when)
{
	const QByteArray path = qgetenv ("ORBITER_LAYOUT_DUMP");
	if (path.isEmpty () || !mainDlg) return;
	QFile f (QString::fromLocal8Bit (path));
	if (!f.open (QIODevice::Append | QIODevice::Text)) return;
	QTextStream ts (&f);
	ts << "# " << when << " " << mainDlg->width () << "x" << mainDlg->height () << "\n";
	QList<QWidget*> dialogs = {mainDlg};
	for (QWidget *w : mainDlg->findChildren<QWidget*> ())
		if (w->property ("resBaseX").isValid ()) dialogs.append (w);
	for (QWidget *dlg : dialogs) {
		const RESDIALOG *d = FindDialog (dlg->objectName ());
		for (QObject *o : dlg->children ()) {
			QWidget *w = qobject_cast<QWidget*> (o);
			if (!w || !w->property ("resCtl").isValid ()) continue;
			const RESCONTROL *c = (const RESCONTROL*)w->property ("resCtl").value<void*> ();
			const long i = (d && c >= d->ctrl && c < d->ctrl + d->nctrl ? c - d->ctrl : -1);
			QRect g = w->geometry ();
			ts << dlg->objectName () << " " << i << " " << w->objectName () << " " << g.x () << " " << g.y () << " "
				<< g.width () << " " << g.height () << (w->isHidden () ? " hidden" : "") << "\n";
		}
	}
}

QString custom::LayoutSkin::DesignerPath ()
{
	QString p = QLibraryInfo::path (QLibraryInfo::BinariesPath) + "/designer";
	if (QFileInfo (p).isExecutable ()) return p;
	return QStandardPaths::findExecutable ("designer6"); // never a bare "designer": often Qt 5's
}

bool custom::LayoutSkin::Export (const QString &name, const QString &coloursFrom, QString &id, QString &msg)
{
	Run &r = R ();
	QString nm = name.simplified ();
	if (nm.isEmpty ()) nm = "My Launchpad";
	QString base;
	for (QChar ch : nm)
		base += ((ch.unicode () < 128 && ch.isLetterOrNumber ()) || ch == '-' || ch == '_' ? ch : QChar ('_'));
	if (base.isEmpty ()) base = "Layout";
	QDir skins (QDir::current ().absoluteFilePath (SKIN_DIR));
	if (!skins.exists () && !QDir ().mkpath (skins.absolutePath ())) {
		msg = "The Skins folder can't be made: " + skins.absolutePath ();
		return false;
	}
	QString folder = base;
	for (int k = 2; !folder.compare ("classic", Qt::CaseInsensitive) || skins.exists (folder); k++)
		folder = base + "_" + QString::number (k);
	if (!skins.mkdir (folder)) {
		msg = "The Skins folder can't be written: " + skins.absolutePath () + "\nCheck its permissions, or run Orbiter from a folder you own.";
		return false;
	}
	const QString dir = skins.absoluteFilePath (folder);
	QDir (dir).mkpath ("ui/images");
	QStringList problems;

	// the stock dialogs, built with the hook off
	ResDialogHook saved = g_resDialogHook;
	g_resDialogHook = nullptr;
	int written = 0;
	for (const QString &dn : LayoutDialogs ()) {
		const RESDIALOG *d = FindDialog (dn);
		QWidget *w = (d ? CreateResDialog (r.hInst, d->id, nullptr, nullptr, false) : nullptr);
		if (!w) {
			problems << dn + " is not in this Orbiter";
			continue;
		}
		std::vector<LayoutImage> images;
		UiForm form = ExportLayout (w, d, r.hInst, images);
		delete w;
		if (!WriteFile (dir + "/ui/" + dn + ".ui", WriteUiForm (form))) {
			problems << dn + ".ui can't be written";
			continue;
		}
		for (const LayoutImage &img : images) {
			QString p = dir + "/ui/" + img.path;
			if (!QFileInfo::exists (p) && !img.image.save (p, "PNG")) problems << img.path + " can't be written";
		}
		written++;
	}
	g_resDialogHook = saved;

	// colours of another skin: its style sheet and the files it names with ${SKIN}
	QString cfgText = "Name = " + nm + "\nUi = ui\n";
	if (!coloursFrom.isEmpty ()) {
		SkinManifest m = ReadSkin ((fs::path (SKIN_DIR) / coloursFrom.toStdString ()).string ());
		QFile q (QString::fromStdString ((fs::path (m.dir) / m.qss).string ()));
		if (!m.ok || m.qss.empty () || !q.open (QIODevice::ReadOnly) || q.size () > 4 * 1024 * 1024) {
			problems << "the colours of '" + coloursFrom + "' can't be read";
		} else {
			const QByteArray qss = q.readAll ();
			if (WriteFile (dir + "/style.qss", qss)) cfgText += "Qss = style.qss\n";
			else problems << "style.qss can't be written";
			const QString text = QString::fromUtf8 (qss);
			CopyStyleFiles (text, QString::fromStdString (m.dir), dir, problems);
			QRegularExpression url ("url\\(\\s*[\"']?([^)\"']*)");
			for (auto it = url.globalMatch (text); it.hasNext (); ) {
				const QString u = it.next ().captured (1).trimmed ();
				if (!u.startsWith ("${SKIN}") && !u.startsWith (':')) problems << "the style sheet names " + u + ", which was not copied";
			}
		}
	}
	if (!written) problems << "no form could be written, so the folder is not a skin";
	else if (!WriteFile (dir + "/skin.cfg", cfgText.toUtf8 ())) problems << "skin.cfg can't be written";
	if (!WriteFile (dir + "/README.txt", QByteArray (README))) problems << "README.txt can't be written";

	id = folder;
	msg = QString ("Made the layout '%1' in\n%2\n\n%3 Launchpad dialogs are in its ui folder. Edit them in Qt Designer, then choose "
		"'%1' in this list and press Apply: the main window and its pages change when Orbiter starts again.")
		.arg (nm, dir).arg (written);
	if (!problems.isEmpty ()) msg += "\n\nProblems:\n- " + problems.mid (0, 12).join ("\n- ");
	Log ("made '" + folder + "' (" + QString::number (written) + " forms)" + (problems.isEmpty () ? QString () : "; " + problems.join ("; ")));
	return written > 0;
}
