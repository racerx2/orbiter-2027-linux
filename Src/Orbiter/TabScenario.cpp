// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//=============================================================================
// ScenarioTab class
//=============================================================================

#include <unistd.h>
#include <string>
#include <strings.h>
#include "Orbiter.h"
#include "TabScenario.h"
#include "Launchpad.h"
//#include "Log.h"
#include "Help.h"
#include "htmlctrl.h"
#include "resource.h"
#include "ResDialog.h"
#include "Util.h"
#include <QCheckBox>
#include <QDialog>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>

using namespace std;

extern const char* CurrentScenario;
const char *htmlstyle = "<style type=""text/css"">body{font-family:Arial;font-size:12px} p{margin-top:0;margin-bottom:0.5em} h1{font-size:150%;font-weight:normal;margin-bottom:0.5em;color:#000080;background-color:#E6E6FF;padding:0.1em}</style>";

//-----------------------------------------------------------------------------

static QPixmap TreeIcon (void *hInst, int resId)
{
	QImage *img = oapiLoadResImage (hInst, resId);
	oapiClearImageBackground (img); // not upstream: the white surround shows on a dark desktop theme; Windows' tree is always white
	QPixmap pm = (img ? QPixmap::fromImage (*img) : QPixmap());
	delete img;
	return pm;
}

orbiter::ScenarioTab::ScenarioTab (const LaunchpadDialog *lp): LaunchpadTab (lp)
{
	// folders show the same image when selected; scenarios switch to their selected image
	treeicon[0] = new QIcon (TreeIcon (AppInstance(), IDB_TREEICON_FOLDER1));
	treeicon[1] = new QIcon (TreeIcon (AppInstance(), IDB_TREEICON_SCN1));
	treeicon[1]->addPixmap (TreeIcon (AppInstance(), IDB_TREEICON_SCN2), QIcon::Selected);
	scnhelp[0] = '\0';
	htmldesc = pLp->App()->UseHtmlInline();
	hWatch = NULL;
}

//-----------------------------------------------------------------------------

orbiter::ScenarioTab::~ScenarioTab ()
{
	delete treeicon[0];
	delete treeicon[1];
	// the watcher belongs to the tab window
}

//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::Create ()
{
	hTab = CreateTab (IDD_PAGE_SCN);

	RefreshList(false);
	DlgItem<QTreeWidget> (hTab, IDC_SCN_LIST)->setIconSize (QSize (16, 16)); // TVM_SETIMAGELIST

	r_list0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_SCN_LIST)); // REMOVE!
	r_desc0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_SCN_HTML)); // REMOVE!
	r_pane  = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_SCN_SPLIT1));
	r_save0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_SCN_SAVE));
	r_clear0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_SCN_DELQS));
	r_info0  = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_SCN_INFO));
	r_pause0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_SCN_PAUSED));

	if (pLp->App()->UseHtmlInline()) {
		oapiResDlgItem (hTab, IDC_SCN_DESC)->hide();
		oapiResDlgItem (hTab, IDC_SCN_HTML)->show();
		oapiResDlgItem (hTab, IDC_SCN_INFO)->hide();
		infoId = IDC_SCN_HTML;
	} else {
		oapiResDlgItem (hTab, IDC_SCN_HTML)->hide();
		oapiResDlgItem (hTab, IDC_SCN_DESC)->show();
		oapiResDlgItem (hTab, IDC_SCN_INFO)->show();
		infoId = IDC_SCN_DESC;
	}

	splitListDesc.SetHwnd (oapiResDlgItem (hTab, IDC_SCN_SPLIT1), oapiResDlgItem (hTab, IDC_SCN_LIST), oapiResDlgItem (hTab, infoId));

	// create a watcher to monitor changes to the scenario list
	hWatch = new QFileSystemWatcher (hTab);
	QObject::connect (hWatch, &QFileSystemWatcher::directoryChanged, hTab, [this]() {
		RefreshList (true);
		WatchScnList ();
	});
	WatchScnList ();
}

//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::GetConfig (const Config *cfg)
{
	DlgItem<QCheckBox> (hTab, IDC_SCN_PAUSED)->setChecked (cfg->CfgLogicPrm.bStartPaused);
	int listw = cfg->CfgWindowPos.LaunchpadScnListWidth;
	if (!listw) {
		listw = oapiResDlgItem (hTab, IDC_SCN_LIST)->width();
	}
	splitListDesc.SetStaticPane (SplitterCtrl::PANE1, listw);
}

//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::SetConfig (Config *cfg)
{
	cfg->CfgLogicPrm.bStartPaused = DlgItem<QCheckBox> (hTab, IDC_SCN_PAUSED)->isChecked();
	cfg->CfgWindowPos.LaunchpadScnListWidth = splitListDesc.GetPaneWidth (SplitterCtrl::PANE1);
}

//-----------------------------------------------------------------------------

bool orbiter::ScenarioTab::OpenHelp ()
{
	OpenTabHelp ("tab_scenario");
	return true;
}

//-----------------------------------------------------------------------------

BOOL orbiter::ScenarioTab::OnSize (int w, int h)
{
	int dw = w - (int)(pos0.right-pos0.left);
	int dh = h - (int)(pos0.bottom-pos0.top);
	int w0 = r_pane.right - r_pane.left; // initial splitter pane width
	int h0 = r_pane.bottom - r_pane.top; // initial splitter pane height

	// the elements below may need updating
	int wl0 = r_list0.right - r_list0.left; // initial list width
	int wd0 = r_desc0.right - r_desc0.left; // initial description width
	int wg  = r_desc0.right - r_list0.left - wl0 - wd0;  // gap width
	int bg  = r_clear0.left - r_save0.right; // button gap
	int wb1 = r_save0.right - r_save0.left;
	int wb2 = r_clear0.right - r_clear0.left;
	int wb3 = r_info0.right - r_info0.left;
	int hb  = r_save0.bottom - r_save0.top;
	int wl  = wl0 + (dw*wl0)/(wl0+wd0);
	wl = max (wl, wl0/2);
	int xr = r_list0.left+wl+wg;
	int wr = max(10,wl0+wd0+dw-wl);
	int ww = wl+wr+wg-2*bg;
	wb3 = min (wb3, ww/3);
	ww -= wb3;
	wb1 = wb2 = min (wb1, ww/2);
	int xb2 = r_save0.left+wb1+bg;
	int xb3 = xr+wr-wb3;

	oapiResDlgItem (hTab, IDC_SCN_SPLIT1)->resize (w0+dw, h0+dh);
	oapiResDlgItem (hTab, IDC_SCN_SAVE)->setGeometry (r_save0.left, r_save0.top+dh, wb1, hb);
	oapiResDlgItem (hTab, IDC_SCN_DELQS)->setGeometry (xb2, r_clear0.top+dh, wb2, hb);
	oapiResDlgItem (hTab, IDC_SCN_INFO)->setGeometry (xb3, r_info0.top+dh, wb3, hb);
	oapiResDlgItem (hTab, IDC_SCN_PAUSED)->move (r_pause0.left+dw, r_pause0.top);

	return FALSE;
}

//-----------------------------------------------------------------------------

BOOL orbiter::ScenarioTab::OnInitDialog (QWidget *hWnd)
{
	// WM_NOTIFY
	QTreeWidget *hTree = DlgItem<QTreeWidget> (hWnd, IDC_SCN_LIST);
	QObject::connect (hTree, &QTreeWidget::currentItemChanged, hWnd, [this]() {
		ScenarioChanged(); // TVN_SELCHANGED
	});
	QObject::connect (hTree, &QTreeWidget::itemDoubleClicked, hWnd, [this]() {
		// NM_DBLCLK: WM_COMMAND IDLAUNCH to the Launchpad
		QMetaObject::invokeMethod (DlgItem<QPushButton> (LaunchpadWnd(), IDLAUNCH), "click", Qt::QueuedConnection);
	});

	// WM_COMMAND
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_SCN_SAVE), &QPushButton::clicked, hWnd, [this]() { SaveCurScenario(); });
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_SCN_DELQS), &QPushButton::clicked, hWnd, [this]() { ClearQSFolder(); });
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_SCN_INFO), &QPushButton::clicked, hWnd, [this]() { OpenScenarioHelp(); });
	return FALSE;
}

// sibling after an item (TVGN_NEXT)
static QTreeWidgetItem *NextSibling (QTreeWidget *hTree, QTreeWidgetItem *it)
{
	QTreeWidgetItem *parent = it->parent();
	int idx = (parent ? parent->indexOfChild (it) : hTree->indexOfTopLevelItem (it)) + 1;
	return (parent ? parent->child (idx) : hTree->topLevelItem (idx));
}

//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::RefreshList (bool preserveSelection)
{
	if (Launchpad()->Visible()) {
		char cbuf[256], * pc, * c;
		QTreeWidget *hTree = DlgItem<QTreeWidget>(hTab, IDC_SCN_LIST);
		if (!GetSelScenario(cbuf, 256)) cbuf[0] = '\0';
		{
			// remove selection to avoid repeated TVN_SELCHANGED messages while the list is cleared
			QSignalBlocker block(hTree);
			hTree->setCurrentItem(NULL);
			hTree->clear();
		}
		ScanDirectory(oapiResolvePath(pCfg->CfgDirPrm.ScnDir), NULL);

		QTreeWidgetItem *hti = hTree->topLevelItem(0);
		if (preserveSelection) { // find the previous selection in the newly created list and re-select it
			pc = cbuf;
			while (*pc) {
				for (c = pc; *c && *c != '/'; c++);
				bool isdir = (*c == '/');
				*c = '\0';
				for (QTreeWidgetItem *it = hti; it; it = NextSibling(hTree, it)) {
					if (!strcmp(it->text(0).toUtf8().constData(), pc)) {
						hti = it;
						if (isdir)
							hti = hti->child(0);
						break;
					}
				}
				pc = c;
				if (isdir) pc++;
			}
		}
		else { // Select the "current" scenario
			for (QTreeWidgetItem *it = hti; it; it = NextSibling(hTree, it)) {
				if (!strcmp(it->text(0).toUtf8().constData(), CurrentScenario)) {
					hti = it;
					break;
				}
			}
		}
		hTree->setCurrentItem(hti);
	}
}

//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::WatchScnList ()
{
	// FindFirstChangeNotification watches the whole tree; the Qt watcher takes each directory
	if (!hWatch) return;
	QStringList dirs = hWatch->directories();
	if (!dirs.isEmpty()) hWatch->removePaths(dirs);
	std::error_code ec;
	fs::path root = oapiResolvePath(pCfg->CfgDirPrm.ScnDir);
	if (!fs::is_directory(root, ec)) return;
	dirs = {QString::fromStdString(root.string())};
	for (auto& entry : fs::recursive_directory_iterator(root, ec))
		if (entry.is_directory(ec)) dirs << QString::fromStdString(entry.path().string());
	hWatch->addPaths(dirs);
}

//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::LaunchpadShowing(bool show)
{
	if (show) {
		RefreshList(false);
	}
}
//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::ScanDirectory (const fs::path& path, QTreeWidgetItem *hti)
{
	QTreeWidget *hTree = DlgItem<QTreeWidget>(hTab, IDC_SCN_LIST);
	QTreeWidgetItem *ht, *hts0;
	std::error_code ec;
	auto count = [hTree, hti]() { return (hti ? hti->childCount() : hTree->topLevelItemCount()); };
	auto child = [hTree, hti](int i) { return (hti ? hti->child(i) : hTree->topLevelItem(i)); };
	auto insert = [hTree, hti](int i, QTreeWidgetItem *it) { if (hti) hti->insertChild(i, it); else hTree->insertTopLevelItem(i, it); };

	// subdirectories (cChildren = 1, folder image; TVI_SORT)
	for (auto& entry : fs::directory_iterator(path, ec)) {
		if (entry.is_directory()) {
			QString name = QString::fromStdString(entry.path().stem().string());
			ht = new QTreeWidgetItem();
			ht->setText(0, name);
			ht->setIcon(0, *treeicon[0]);
			ht->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
			int i;
			for (i = 0; i < count(); i++)
				if (QString::compare(name, child(i)->text(0), Qt::CaseInsensitive) < 0) break;
			insert(i, ht);
			ScanDirectory(entry.path(), ht);
		}
	}

	hts0 = child(0);
	// the first subdirectory entry in this folder

	// scan for files: they go ahead of the subdirectories, ordered by strcmp
	for (auto& entry : fs::directory_iterator(path, ec)) {
		if (entry.is_regular_file() && entry.path().extension().string() == ".scn") {
			std::string cbuf = entry.path().stem().string();
			int i;
			for (i = 0; i < count() && child(i) != hts0; i++)
				if (strcmp(child(i)->text(0).toUtf8().constData(), cbuf.c_str()) > 0) break;
			ht = new QTreeWidgetItem();
			ht->setText(0, QString::fromStdString(cbuf));
			ht->setIcon(0, *treeicon[1]);
			insert(i, ht);
		}
	}
}

//-----------------------------------------------------------------------------

char *ScanFileDesc (std::istream &is, const char *blockname)
{
	char *buf = 0;
	char blockbegin[256] = "BEGIN_";
	char blockend[256] = "END_";
	strncpy (blockbegin+6, blockname, 240);
	strncpy (blockend+4, blockname, 240);

	if (FindLine (is, blockbegin)) {
		int i, len, buflen = 0;
		const int linelen = 256;
		char line[linelen];
		for(i = 0;; i++) {
			if (!is.getline(line, linelen-2)) {
				if (is.eof()) break;
				else is.clear();
			}
			if (strncasecmp (line, blockend, strlen(blockend))) {
				len = strlen(line);
				if (len) strcat (line, " "), len++;    // convert newline to space
				else     strcpy (line, "\r\n"), len=2; // convert empty line to CR
				char *tmp = new char[buflen+len+1];
				if (buflen) {
					memcpy (tmp, buf, buflen*sizeof(char));
					delete []buf;
				}
				memcpy (tmp+buflen, line, len*sizeof(char));
				buflen += len;
				tmp[buflen] = '\0';
				buf = tmp;
			} else {
				break;
			}
		}
	}
	return buf;
}

void AppendChar (char *&line, int &linelen, char c, int pos)
{
	if (pos == linelen) {
		char *tmp = new char[linelen+256];
		memcpy (tmp, line, linelen);
		delete []line;
		line = tmp;
		linelen += 256;
	}
	line[pos] = c;
}

struct ReplacementPair {
	char *src, *tgt;
};

void Html2Text(std::string& str)
{
	std::string::size_type n0, n1;

	// 1. remove all newlines
	for (int i = str.size() - 1; i >= 0; i--)
		if (str[i] == '\r')
			str.erase(i, 1);
	for (int i = str.size() - 1; i >= 0; i--)
		if (str[i] == '\n')
			str[i] = ' ';

	// 2. substitute some html tags
	const std::string tag[4] = { "</h1> ", "</p> ", "</h1>", "</p>" };
	const std::string tag_subst[4] = { "\r\n\r\n", "\r\n\r\n", "\r\n\r\n", "\r\n\r\n" };
	for (int i = 0; i < 4; i++) {
		n0 = 0;
		while ((n0 = str.find(tag[i], n0)) != std::string::npos) {
			str.replace(n0, tag[i].size(), tag_subst[i]);
			n0 += tag_subst[i].size();
		}
	}

	// 3. remove remaining tags
	n0 = 0;
	while ((n0 = str.find("<", n0)) != std::string::npos) {
		n1 = str.find(">", n0);
		if (n1 != std::string::npos)
			str.erase(n0, n1 - n0 + 1);
	}

	// 4. substitute some symbols
	const std::string sym[5] = { "&gt;", "&lt;", "&ge;", "&le;", "&amp;" };
	const std::string sym_subst[5] = { ">", "<", ">=", "<=", "\001" };
	for (int i = 0; i < 5; i++) {
		n0 = 0;
		while ((n0 = str.find(sym[i], n0)) != std::string::npos) {
			str.replace(n0, sym[i].size(), sym_subst[i]);
			n0 += sym_subst[i].size();
		}
	}

	// 5. remove remaining symbols
	n0 = 0;
	while ((n0 = str.find("&", n0)) != std::string::npos) {
		n1 = str.find(";", n0);
		if (n1 != std::string::npos)
			str.erase(n0, n1 - n0 + 1);
	}

	// 6. restore ampersands
	for (int i = 0; i < str.size(); i++)
		if (str[i] == '\001')
			str[i] = '&';
}

void Text2Html(std::string& str)
{
	std::string::size_type n0;

	// 1. substitute some symbols
	const std::string sym[6] = { "&", ">=", "<=", ">", "<", "\r\n" }; // the order is relevant here
	const std::string sym_subst[6] = { "&amp;", "&ge;", "&le;", "&gt;", "&lt;", "<br />"};
	for (int i = 0; i < 6; i++) {
		n0 = 0;
		while ((n0 = str.find(sym[i], n0)) != std::string::npos) {
			str.replace(n0, sym[i].size(), sym_subst[i]);
			n0 += sym_subst[i].size();
		}
	}
}

//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::ScenarioChanged ()
{
	const int linelen = 256;
	bool have_info = false;
	char cbuf[256], path[256], *pc;
	ifstream ifs;
	scnhelp[0] = '\0';

	switch (GetSelScenario (cbuf, 256)) {
	case 0: // error
		return;
	case 1: // scenario file
		ifs.open (oapiResolvePath (pLp->App()->ScnPath (cbuf)));
		pLp->EnableLaunchButton (true);
		break;
	case 2: // subdirectory
		strcpy (path, pCfg->CfgDirPrm.ScnDir);
		strcat (path, cbuf);
		strcat (path, "/Description.txt");
		ifs.open (oapiResolvePath (path), ios::in);
		pLp->EnableLaunchButton (false);
		break;
	}
	if (ifs) {
		if (!have_info) {
			char *buf;
			if (htmldesc) {
				buf = ScanFileDesc(ifs, "URLDESC");
				if (buf) {
					char url_ref[256], url[512], cwd[256], *path, *topic;
					strncpy(url_ref, trim_string(buf), 255);
					url_ref[255] = '\0';
					path = strtok(url_ref, ",");
					topic = strtok(NULL, "\n");
					if (topic)
						snprintf(url, 512, "its:Html\\Scenarios\\%s.chm::%s.htm", path, topic);
					else
						snprintf(url, 512, "%s/Html/Scenarios/%s.htm", getcwd(cwd, 256), path);
					for (char *c = url; *c; c++) if (*c == '\\') *c = '/';
					DisplayHTMLPage(oapiResDlgItem(hTab, IDC_SCN_HTML), topic ? url : oapiResolvePath(url).c_str()); // "its:" URLs resolve in DisplayHTMLPage
					have_info = true;
				}
				else {
					buf = ScanFileDesc(ifs, "HYPERDESC");
					if (!buf) {
						buf = ScanFileDesc(ifs, "DESC");
						if (buf) {
							std::string str(buf);
							Text2Html(str);
							delete[]buf;
							buf = new char[str.size() + 1];
							strcpy(buf, str.c_str());
						}
					}
					if (buf) { // prepend style preamble
						char* buf2 = new char[strlen(htmlstyle) + strlen(buf) + 1];
						strcpy(buf2, htmlstyle); strcat(buf2, buf);
						delete[]buf;
						buf = buf2;
						DisplayHTMLStr(oapiResDlgItem(hTab, IDC_SCN_HTML), buf);
						have_info = true;
					}
				}
			} else {
				if ((buf = ScanFileDesc (ifs, "DESC"))) {
					oapiSetDlgItemText(hTab, IDC_SCN_DESC, buf);
					have_info = true;
				} else if ((buf = ScanFileDesc (ifs, "HYPERDESC"))) {
					std::string str(buf);
					Html2Text(str);
					oapiSetDlgItemText(hTab, IDC_SCN_DESC, str.c_str());
					have_info = true;
				}
			}
			if (buf) {
				delete []buf;
				buf = NULL;
			}
		}
	}

	if (!have_info) {
		if (htmldesc) DisplayHTMLStr (oapiResDlgItem (hTab, IDC_SCN_HTML), "");
		else          oapiSetDlgItemText (hTab, IDC_SCN_DESC, "");
	}

	if (!htmldesc) {
		bool enable_info = false;
		for (int i = 0; scnhelp[i]; i++)
			if (scnhelp[i] == ',') {
				enable_info = true;
				break;
			}
		oapiResDlgItem (hTab, IDC_SCN_INFO)->setEnabled (enable_info);
	}
}

//-----------------------------------------------------------------------------

int orbiter::ScenarioTab::GetSelScenario (char *scn, int len)
{
	char cbuf[256];
	int type;

	if (!hTab) return 0;
	QTreeWidgetItem *it = DlgItem<QTreeWidget> (hTab, IDC_SCN_LIST)->currentItem();
	if (!it) return 0;
	snprintf (scn, len, "%s", it->text (0).toUtf8().constData());
	type = (it->childIndicatorPolicy() == QTreeWidgetItem::ShowIndicator ? 2 : 1);

	// build path
	while ((it = it->parent())) {
		snprintf (cbuf, 256, "%s/%s", it->text (0).toUtf8().constData(), scn);
		snprintf (scn, len, "%s", cbuf);
	}
	return type;
}

//-----------------------------------------------------------------------------

void orbiter::ScenarioTab::SaveCurScenario ()
{
	ifstream ifs (oapiResolvePath (pLp->App()->ScnPath (CurrentScenario)), ios::in);
	if (ifs) {
		QDialog *dlg = qobject_cast<QDialog*> (oapiCreateResDialog (AppInstance(), IDD_SAVESCN, LaunchpadWnd()));
		if (dlg) {
			SaveProc (dlg, this);
			dlg->exec(); // DialogBoxParam
			delete dlg;
		}
	} else {
		QMessageBox::warning (LaunchpadWnd(), "Save Error", "No current simulation state available");
	}
}

//-----------------------------------------------------------------------------
// Name: SaveCurScenarioAs()
// Desc: copy current scenario file into 'name', replacing description with 'desc'.
//		 return value: 0=ok, 1=failed, 2=file exists (only checked if replace=false)
//-----------------------------------------------------------------------------
int orbiter::ScenarioTab::SaveCurScenarioAs (const char *name, char *desc, bool replace)
{
	string cbuf;
	bool skip = false;
	std::string path = oapiResolvePath (pLp->App()->ScnPath (name));
	if (!replace) { // check if exists
		ifstream ifs (path, ios::in);
		if (ifs) return 2;
	}
	ofstream ofs (path);
	if (!ofs) return 1;
	ifstream ifs (oapiResolvePath (pLp->App()->ScnPath (CurrentScenario)));
	if (!ifs) return 1;
	int i, len = strlen(desc);
	for (i = 0; i < len-1; i++)
		if (desc[i] == '\r' && desc[i+1] == '\n') desc[i] = '\n';
	ofs << "BEGIN_DESC" << endl;
	ofs << desc << endl;
	ofs << "END_DESC" << endl;
	while (std::getline( ifs, cbuf ))
	{
		if (cbuf == "BEGIN_DESC")
			skip = true;
		else if (cbuf == "END_DESC")
			skip = false;
		else if (!skip)
			ofs << cbuf << endl;
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Name: SaveProc()
// Desc: Scenario save dialog set-up (WM_INITDIALOG) and command handlers
//-----------------------------------------------------------------------------
void orbiter::ScenarioTab::SaveProc (QWidget *hWnd, ScenarioTab *pTab)
{
	QDialog *dlg = qobject_cast<QDialog*> (hWnd);

	// WM_COMMAND
	QObject::connect (DlgItem<QPushButton> (hWnd, IDOK), &QPushButton::clicked, dlg, [hWnd, dlg, pTab]() {
		int res, name_len, desc_len;
		char name[64], *desc;
		name_len = DlgItem<QLineEdit> (hWnd, IDC_SAVE_NAME)->text().toUtf8().size();
		desc_len = DlgItem<QPlainTextEdit> (hWnd, IDC_SAVE_DESC)->toPlainText().toUtf8().size();
		if (name_len > 63) {
			QMessageBox::warning (hWnd, "Save Error", "Scenario name too long (max 63 characters)");
			return;
		}
		desc = new char[desc_len+1];
		oapiGetDlgItemText (hWnd, IDC_SAVE_NAME, name, 64);
		oapiGetDlgItemText (hWnd, IDC_SAVE_DESC, desc, desc_len+1);
		res = pTab->SaveCurScenarioAs (name, desc);
		if (res == 2) {
			if (QMessageBox::question (hWnd, "Warning", "File exists. Overwrite?") == QMessageBox::Yes)
				res = pTab->SaveCurScenarioAs (name, desc, true);
			else { delete []desc; return; }
		}
		if (res == 1) {
			QMessageBox::warning (hWnd, "Save Error", "Error writing scenario file.");
			delete []desc;
			return;
		}
		delete []desc;
		desc = NULL;
		dlg->accept(); // EndDialog
	});
	QObject::connect (DlgItem<QPushButton> (hWnd, IDCANCEL), &QPushButton::clicked, dlg, &QDialog::reject);
}

//-----------------------------------------------------------------------------
// Name: ClearQSFolder()
// Desc: Delete all scenarios in the Quicksave folder
//-----------------------------------------------------------------------------
void orbiter::ScenarioTab::ClearQSFolder()
{
	fs::path scnpath{ oapiResolvePath(pLp->App()->ScnPath("Quicksave")) };
	scnpath.replace_extension(); // remove ".scn"

	std::string msg = "Are you sure you want to delete all quicksaves? This affects:\n";
	int qsCount = 0;
	
	if (fs::exists(scnpath) && fs::is_directory(scnpath)) {
		for (auto& entry : fs::directory_iterator(scnpath)) {
			if (entry.is_regular_file() && entry.path().extension().string() == ".scn") {
				qsCount++;
				if (qsCount <= 10) {
					msg += "- " + entry.path().stem().string() + "\n";
				}
			}
		}
	}
	
	if (qsCount == 0) {
		QMessageBox::information(LaunchpadWnd(), "Clear Quicksaves", "There are no quicksaves to delete.");
		return;
	}
	
	if (qsCount > 10) {
		msg += "... and " + std::to_string(qsCount - 10) + " more.\n";
	}
	
	if (QMessageBox::warning(LaunchpadWnd(), "Clear Quicksaves", QString::fromStdString(msg), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
		return;
	}

	std::error_code ec;
	fs::remove_all(scnpath, ec);
	if (!ec) {
		fs::create_directory(scnpath);
	}
}

//-----------------------------------------------------------------------------
// Name: OpenScenarioHelp()
// Desc: Opens the help file associated with the scenario
//-----------------------------------------------------------------------------
void orbiter::ScenarioTab::OpenScenarioHelp ()
{
	if (!scnhelp[0]) return;
	char str[256], path[256], *scenario, *topic;
	strncpy (str, scnhelp, 256);
	scenario = strtok (str, ",");
	topic = strtok (NULL, "\n");
	snprintf(path, 256, "html/scenarios/%s.chm", scenario);
	::OpenHelp(LaunchpadWnd(), path, topic);
}

// the scenario directory tree watcher (upstream: a thread on FindFirstChangeNotification) is set up in Create
