// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//=============================================================================
// ModuleTab class
//=============================================================================

#include "Orbiter.h"
#include "Launchpad.h"
#include "TabModule.h"
#include "resource.h"
#include "ResDialog.h"
#include "Util.h"
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <algorithm>
#include <filesystem>
namespace fs = std::filesystem;

using std::max;

extern char DBG_MSG[256];
static int counter = -1;

//-----------------------------------------------------------------------------

orbiter::ModuleTab::ModuleTab (const LaunchpadDialog *lp): LaunchpadTab (lp)
{
	nmodulerec = 0;
}

//-----------------------------------------------------------------------------

orbiter::ModuleTab::~ModuleTab ()
{
	int i;

	if (nmodulerec) {
		for (i = 0; i < nmodulerec; i++) {
			delete []modulerec[i]->name;
			modulerec[i]->name = NULL;
			if (modulerec[i]->info) {
				delete []modulerec[i]->info;
				modulerec[i]->info = NULL;
			}
			delete modulerec[i];
		}
		delete []modulerec;
		modulerec = NULL;
	}
}

//-----------------------------------------------------------------------------

void orbiter::ModuleTab::Create ()
{
	hTab = CreateTab (IDD_PAGE_MOD);

	r_lst0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_MOD_TREE)); // REMOVE!
	r_dsc0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_MOD_INFO)); // REMOVE!
	r_pane  = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_MOD_SPLIT1));
	r_bt0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_MOD_BUTTON1));
	r_bt1 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_MOD_BUTTON2));
	r_bt2 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_MOD_DEACTALL));
	splitListDesc.SetHwnd (oapiResDlgItem (hTab, IDC_MOD_SPLIT1), oapiResDlgItem (hTab, IDC_MOD_TREE), oapiResDlgItem (hTab, IDC_MOD_INFO));
}

//-----------------------------------------------------------------------------

BOOL orbiter::ModuleTab::OnInitDialog (QWidget *hWnd)
{
	QTreeWidget *hTree = DlgItem<QTreeWidget> (hWnd, IDC_MOD_TREE);
	hTree->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff); // hack: hide horizontal scroll bar

	// WM_NOTIFY: TVN_SELCHANGED shows the module description
	QObject::connect (hTree, &QTreeWidget::currentItemChanged, hWnd, [hWnd](QTreeWidgetItem *item) {
		MODULEREC* rec = (item ? (MODULEREC*)item->data (0, Qt::UserRole).value<void*>() : NULL);
		if (rec && rec->info)
			oapiSetDlgItemText (hWnd, IDC_MOD_INFO, rec->info);
		else
			oapiSetDlgItemText (hWnd, IDC_MOD_INFO, "");
	});
	// a ticked or unticked item (de)activates its module once the initial ticks are set
	QObject::connect (hTree, &QTreeWidget::itemChanged, hWnd, [this](QTreeWidgetItem *item, int column) {
		if (counter == 4) ActivateFromList ();
	});

	// WM_COMMAND
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_MOD_DEACTALL), &QPushButton::clicked, hWnd, [this]() { DeactivateAll (); });
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_MOD_BUTTON1), &QPushButton::clicked, hWnd, [this]() { ExpandCollapseAll (true); });
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_MOD_BUTTON2), &QPushButton::clicked, hWnd, [this]() { ExpandCollapseAll (false); });

	return FALSE;
}

//-----------------------------------------------------------------------------

void orbiter::ModuleTab::GetConfig (const Config *cfg)
{
	RefreshLists();
	InitActivation(); // the ticks can be set right away (Win32 needed a deferred WM_USER for this)
	counter = 4;
	oapiSetDlgItemText (hTab, IDC_MOD_INFO, "Optional Orbiter plugin modules.\r\n\r\nDouble-click on a category to show or hide its entries.\r\n\r\nCheck or uncheck items to activate the corresponding modules.\r\n\r\nSelect an item to see a description of the module function.");
	int listw = cfg->CfgWindowPos.LaunchpadModListWidth;
	if (!listw) {
		listw = oapiResDlgItem (hTab, IDC_MOD_TREE)->width();
	}
	splitListDesc.SetStaticPane (SplitterCtrl::PANE1, listw);
}

//-----------------------------------------------------------------------------

void orbiter::ModuleTab::SetConfig (Config *cfg)
{
	cfg->CfgWindowPos.LaunchpadModListWidth = splitListDesc.GetPaneWidth (SplitterCtrl::PANE1);
}

//-----------------------------------------------------------------------------

bool orbiter::ModuleTab::OpenHelp ()
{
	OpenTabHelp ("tab_modules");
	return true;
}

//-----------------------------------------------------------------------------

BOOL orbiter::ModuleTab::OnSize (int w, int h)
{
	int dw = w - (int)(pos0.right-pos0.left);
	int dh = h - (int)(pos0.bottom-pos0.top);
	int w0 = r_pane.right - r_pane.left; // initial splitter pane width
	int h0 = r_pane.bottom - r_pane.top; // initial splitter pane height

	// the elements below may need updating
	int lstw0 = r_lst0.right-r_lst0.left;
	int lsth0 = r_lst0.bottom-r_lst0.top;
	int dscw0 = r_dsc0.right-r_dsc0.left;
	int wg  = r_dsc0.right - r_lst0.left - lstw0 - dscw0;  // gap width
	int wl  = lstw0 + (dw*lstw0)/(lstw0+dscw0);
	wl = max (wl, lstw0/2);
	int xr = r_lst0.left+wl+wg;
	int wr = max(10,lstw0+dscw0+dw-wl);

	oapiResDlgItem (hTab, IDC_MOD_SPLIT1)->resize (w0+dw, h0+dh);
	oapiResDlgItem (hTab, IDC_MOD_BUTTON1)->move (r_bt0.left, r_bt0.top+dh);
	oapiResDlgItem (hTab, IDC_MOD_BUTTON2)->move (r_bt1.left, r_bt1.top+dh);
	oapiResDlgItem (hTab, IDC_MOD_DEACTALL)->move (r_bt2.left, r_bt2.top+dh);

	return FALSE;
}

//-----------------------------------------------------------------------------

void orbiter::ModuleTab::Show ()
{
	LaunchpadTab::Show();
}

//-----------------------------------------------------------------------------
// TVI_SORT: position of a new item among the children of parent (the top level if parent is NULL)
static int SortedIndex (QTreeWidget *hTree, QTreeWidgetItem *parent, const char *text)
{
	int n = (parent ? parent->childCount() : hTree->topLevelItemCount());
	QString s = QString::fromUtf8 (text);
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *it = (parent ? parent->child (i) : hTree->topLevelItem (i));
		if (QString::compare (s, it->text (0), Qt::CaseInsensitive) < 0) return i;
	}
	return n;
}

void orbiter::ModuleTab::RefreshLists ()
{
	QTreeWidget *hTree = DlgItem<QTreeWidget> (hTab, IDC_MOD_TREE);
	QSignalBlocker block (hTree);
	hTree->clear();

	int len;
	char catstr[256];

	const fs::path moddir{ oapiResolvePath ("Modules/Plugin") };

	std::error_code ec;
	for (const auto& file : fs::directory_iterator(moddir, ec)) {
		if (file.path().extension().string() == ".so") {
			auto name = file.path().filename().string();
			// add module record
			MODULEREC** tmp = new MODULEREC * [nmodulerec + 1];
			if (nmodulerec) {
				memcpy(tmp, modulerec, nmodulerec * sizeof(MODULEREC*));
				delete[]modulerec;
			}
			modulerec = tmp;

			MODULEREC* rec = modulerec[nmodulerec++] = new MODULEREC;
			len = name.length() - 3;
			rec->name = new char[len + 1];
			strncpy(rec->name, name.c_str(), len);
			rec->name[len] = '\0';
			rec->info = 0;
			rec->active = false;
			rec->locked = false;

			// check if module is set active in config
			if (pCfg->IsActiveModule(rec->name))
				rec->active = true;

			// check if module is set active in command line
			if (std::find(pCfg->CfgCmdlinePrm.LoadPlugins.begin(), pCfg->CfgCmdlinePrm.LoadPlugins.end(), rec->name) != pCfg->CfgCmdlinePrm.LoadPlugins.end()) {
				rec->active = true;
				rec->locked = true; // modules activated from the command line are not to be unloaded
			}

			// read the module's strings from the file, without loading the module
			std::string modfile = (moddir / name).string();
			char buf[1024];
			// read module info string
			if (LoadModuleString(modfile.c_str(), 1000, buf, 1024)) {
				buf[1023] = '\0';
				rec->info = new char[strlen(buf) + 1];
				strcpy(rec->info, buf);
			}
			// read category string
			if (LoadModuleString(modfile.c_str(), 1001, buf, 1024)) {
				strncpy(catstr, buf, 255);
				catstr[255] = '\0';
			}
			else {
				strcpy(catstr, "Miscellaneous");
			}

			if (!strcmp(catstr, "Graphics engines"))
				continue; // graphics client modules are loaded via the Video tab

			// find the category entry
			QTreeWidgetItem *catItem = GetCategoryItem(catstr);

			// tree view entry (checkable; TVI_SORT)
			QTreeWidgetItem *hti = new QTreeWidgetItem();
			hti->setText(0, QString::fromUtf8(rec->name));
			hti->setData(0, Qt::UserRole, QVariant::fromValue((void*)rec));
			hti->setFlags(hti->flags() | Qt::ItemIsUserCheckable);
			hti->setCheckState(0, Qt::Unchecked);
			catItem->insertChild(SortedIndex(hTree, catItem, rec->name), hti);
		}
	}
	counter = 0;
}

QTreeWidgetItem *orbiter::ModuleTab::GetCategoryItem (char *cat)
{
	QTreeWidget *hTree = DlgItem<QTreeWidget> (hTab, IDC_MOD_TREE);
	for (int i = 0; i < hTree->topLevelItemCount(); i++) {
		QTreeWidgetItem *item = hTree->topLevelItem (i);
		if (!strcmp (cat, item->text (0).toUtf8().constData())) return item;
	}
	// not found - create new category item (no check box; TVI_SORT)
	QTreeWidgetItem *item = new QTreeWidgetItem();
	item->setText (0, QString::fromUtf8 (cat));
	item->setData (0, Qt::UserRole, QVariant::fromValue ((void*)NULL));
	hTree->insertTopLevelItem (SortedIndex (hTree, NULL, cat), item);
	return item;
}

void orbiter::ModuleTab::ExpandCollapseAll (bool expand)
{
	QTreeWidget *hTree = DlgItem<QTreeWidget> (hTab, IDC_MOD_TREE);
	for (int i = 0; i < hTree->topLevelItemCount(); i++)
		hTree->topLevelItem (i)->setExpanded (expand);
}

void orbiter::ModuleTab::InitActivation ()
{
	QTreeWidget *hTree = DlgItem<QTreeWidget> (hTab, IDC_MOD_TREE);
	QSignalBlocker block (hTree);

	// tick the active modules
	for (int i = 0; i < hTree->topLevelItemCount(); i++) {
		QTreeWidgetItem *catitem = hTree->topLevelItem (i);
		for (int j = 0; j < catitem->childCount(); j++) {
			QTreeWidgetItem *subitem = catitem->child (j);
			MODULEREC *rec = (MODULEREC*)subitem->data (0, Qt::UserRole).value<void*>();
			if (rec->active) {
				subitem->setCheckState (0, Qt::Checked);
			}
		}
	}

	// categories have no check boxes (they are created without)

	ExpandCollapseAll (true);
}

void orbiter::ModuleTab::ActivateFromList ()
{
	const char *path = "Modules/Plugin";

	QTreeWidget *hTree = DlgItem<QTreeWidget> (hTab, IDC_MOD_TREE);

	for (int i = 0; i < hTree->topLevelItemCount(); i++) {
		QTreeWidgetItem *catitem = hTree->topLevelItem (i);
		for (int j = 0; j < catitem->childCount(); j++) {
			QTreeWidgetItem *subitem = catitem->child (j);
			MODULEREC *rec = (MODULEREC*)subitem->data (0, Qt::UserRole).value<void*>();
			bool checked = (subitem->checkState (0) != Qt::Unchecked);
			if (checked != rec->active) {
				if (!rec->locked) {
					rec->active = checked;
					if (checked) {
						pCfg->AddActiveModule(rec->name);
						pLp->App()->LoadModule(path, rec->name);
					}
					else {
						pCfg->DelActiveModule(rec->name);
						pLp->App()->UnloadModule(rec->name);
					}
				}
				else {
					{
						QSignalBlocker block (hTree);
						subitem->setCheckState(0, rec->active ? Qt::Checked : Qt::Unchecked);
					}
					QMessageBox::warning(NULL, "Orbiter: Plugin Modules", "This module has been requested on the command line and cannot be deactivated interactively.");
				}
			}
		}
	}
}

void orbiter::ModuleTab::DeactivateAll ()
{
	QTreeWidget *hTree = DlgItem<QTreeWidget> (hTab, IDC_MOD_TREE);
	{
		QSignalBlocker block (hTree);
		for (int i = 0; i < hTree->topLevelItemCount(); i++) {
			QTreeWidgetItem *catitem = hTree->topLevelItem (i);
			for (int j = 0; j < catitem->childCount(); j++)
				catitem->child (j)->setCheckState (0, Qt::Unchecked);
		}
	}
	ActivateFromList ();
}

// WM_NOTIFY (TVN_SELCHANGED, check box changes) and WM_COMMAND are connected in OnInitDialog
