// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ==============================================================
//              ORBITER MODULE: Scenario Editor
//                  Part of the ORBITER SDK
//
// Editor.cpp
//
// Implementation of ScnEditor class and editor tab subclasses
// derived from ScnEditorTab.
// ==============================================================

#include "Orbitersdk.h"
#include "OrbiterResource.h"
#include "resource.h"
#include "Editor.h"
#include "DlgCtrl.h"
// commctrl.h left out: the common controls are Qt widgets
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <dlfcn.h>
#include <functional>
#include <stdio.h>

using std::min;
using std::max;

extern ScnEditor *g_editor;
extern QImage *g_hPause;

// ==============================================================
// Local prototypes
// ==============================================================

void OpenDialog (void *context);
void Crt2Pol (VECTOR3 &pos, VECTOR3 &vel);
void Pol2Crt (VECTOR3 &pos, VECTOR3 &vel);
void EditorProc (QWidget*, void*);

// not upstream: Qt counterparts of the list box, combo box, tree view and dialog messages used below
const int LB_ERR = -1; // winuser.h list/combo box error value

static int ListGetCurSel (QListWidget *lb) // LB_GETCURSEL; in an LBS_NOSEL list the item last clicked
{
	if (lb->selectionMode() == QAbstractItemView::NoSelection) return lb->currentRow();
	QList<QListWidgetItem*> sel = lb->selectedItems();
	return (sel.isEmpty() ? LB_ERR : lb->row (sel.first()));
}

static void ListGetText (QListWidget *lb, int idx, char *buf, int buflen) // LB_GETTEXT
{
	QListWidgetItem *it = lb->item (idx);
	snprintf (buf, buflen, "%s", it ? it->text().toUtf8().constData() : "");
}

static int ListFindString (QListWidget *lb, const char *str) // LB_FINDSTRING: first item starting with str, any case
{
	QString s = QString::fromUtf8 (str);
	for (int i = 0; i < lb->count(); i++)
		if (lb->item (i)->text().startsWith (s, Qt::CaseInsensitive)) return i;
	return LB_ERR;
}

static int ComboFindString (QComboBox *cb, const char *str) // CB_FINDSTRING: first item starting with str, any case
{
	QString s = QString::fromUtf8 (str);
	for (int i = 0; i < cb->count(); i++)
		if (cb->itemText (i).startsWith (s, Qt::CaseInsensitive)) return i;
	return LB_ERR;
}

static int ComboSelectString (QComboBox *cb, const char *str) // CB_SELECTSTRING
{
	int idx = ComboFindString (cb, str);
	if (idx != LB_ERR) cb->setCurrentIndex (idx);
	return idx;
}

static void PostCommand (QWidget *hDlg, int id) // PostMessage (hDlg, WM_COMMAND, id, 0): a queued click of that button
{
	QMetaObject::invokeMethod (DlgItem<QAbstractButton> (hDlg, id), "click", Qt::QueuedConnection);
}

static int AddTreeIcon (std::vector<QPixmap> &imglist, void *hInst, int resId) // ImageList_Add (imglist, LoadBitmap (hInst, resId), 0)
{
	QImage *bmp = oapiLoadResImage (hInst, resId);
	oapiClearImageBackground (bmp); // not upstream: the white surround shows on a dark desktop theme; Windows' tree is always white
	imglist.push_back (bmp ? QPixmap::fromImage (*bmp) : QPixmap());
	delete bmp;
	return (int)imglist.size()-1;
}

static QIcon TreeIcon (const std::vector<QPixmap> &imglist, int iImage, int iSelectedImage) // TVIF_IMAGE | TVIF_SELECTEDIMAGE
{
	QIcon icon;
	icon.addPixmap (imglist[iImage], QIcon::Normal);
	icon.addPixmap (imglist[iSelectedImage], QIcon::Selected);
	return icon;
}

// routes a dialog's events to a handler (the window procedure part that is not WM_COMMAND/WM_NOTIFY)
class DlgEvents: public QObject {
public:
	DlgEvents (QWidget *hWnd, std::function<bool(QEvent*)> handler): QObject (hWnd), handler (handler) { hWnd->installEventFilter (this); }
	bool eventFilter (QObject *o, QEvent *e) override { return handler (e); }
private:
	std::function<bool(QEvent*)> handler;
};

static double lengthscale[4] = {1.0, 1e-3, 1.0/AU, 1.0};
static double anglescale[2] = {DEG, 1.0};

static HELPCONTEXT g_hc = {
	(char*)"html/plugin/ScnEditor.chm",
	0,
	(char*)"html/plugin/ScnEditor.chm::/ScnEditor.hhc",
	(char*)"html/plugin/ScnEditor.chm::/ScnEditor.hhk"
};

// ==============================================================
// ScnEditor class definition
// ==============================================================

ScnEditor::ScnEditor (void *hDLL)
{
	hInst  = hDLL;
	hEdLib = NULL;
	hDlg   = NULL;

	// ImageList_Create (16, 16, ILC_COLOR8, 4, 0): the image list is a vector of pixmaps
	treeicon_idx[0] = AddTreeIcon (imglist, hInst, IDB_TREEICON_FOLDER1);
	treeicon_idx[1] = AddTreeIcon (imglist, hInst, IDB_TREEICON_FOLDER2);
	treeicon_idx[2] = AddTreeIcon (imglist, hInst, IDB_TREEICON_FILE1);
	treeicon_idx[3] = AddTreeIcon (imglist, hInst, IDB_TREEICON_FILE2);

	dwCmd = oapiRegisterCustomCmd (
		(char*)"Scenario Editor",
		(char*)"Create, delete and configure spacecraft",
		::OpenDialog, this);

	dwMenuCmd = oapiRegisterCustomMenuCmd("ScnEdit", "MenuInfoBar/ScnEdit.png", ::OpenDialog, this);
}

ScnEditor::~ScnEditor ()
{
	CloseDialog();
	oapiUnregisterCustomCmd (dwCmd);
	oapiUnregisterCustomMenuCmd (dwMenuCmd);
	imglist.clear(); // ImageList_Destroy
}

void ScnEditor::OpenDialog ()
{
	nTab  = 0;
	cTab  = NULL;
	hDlg  = oapiOpenDialogEx (hInst, IDD_EDITOR, EditorProc, 0, this);
}

void ScnEditor::CloseDialog ()
{
	if (hDlg) {
		oapiCloseDialog (hDlg);
		hDlg = NULL;
		cTab = NULL;
		if (nTab) {
			for (DWORD i = 0; i < nTab; i++) delete pTab[i];
			delete []pTab;
			nTab = 0;
		}
	}
	if (hEdLib) {
		dlclose (hEdLib);
		hEdLib = 0;
	}
}

void ScnEditor::ScanCBodyList (QWidget *hDlg, int hList, OBJHANDLE hSelect)
{
	// populate a list of celestial bodies
	char cbuf[256];
	QSignalBlocker block (DlgItem<QComboBox> (hDlg, hList)); // CB_ messages don't notify
	DlgItem<QComboBox> (hDlg, hList)->clear();
	for (DWORD n = 0; n < oapiGetGbodyCount(); n++) {
		oapiGetObjectName (oapiGetGbodyByIndex (n), cbuf, 256);
		oapiComboAddString (DlgItem<QComboBox> (hDlg, hList), cbuf);
	}
	// select the requested body
	oapiGetObjectName (hSelect, cbuf, 256);
	ComboSelectString (DlgItem<QComboBox> (hDlg, hList), cbuf);
}

void ScnEditor::ScanPadList (QWidget *hDlg, int hList, OBJHANDLE hBase)
{
	DlgItem<QComboBox> (hDlg, hList)->clear();
	if (hBase) {
		DWORD n, npad = oapiGetBasePadCount (hBase);
		char cbuf[16];
		for (n = 1; n <= npad; n++) {
			sprintf (cbuf, "%d", n);
			oapiComboAddString (DlgItem<QComboBox> (hDlg, hList), cbuf);
		}
		DlgItem<QComboBox> (hDlg, hList)->setCurrentIndex (0);
	}
}

void ScnEditor::SelectBase (QWidget *hDlg, int hList, OBJHANDLE hRef, OBJHANDLE hBase)
{
	char cbuf[256];
	oapiGetDlgItemText (hDlg, hList, cbuf, 256);
	OBJHANDLE hOldBase = oapiGetBaseByName (hRef, cbuf);
	if (hBase) {
		oapiGetObjectName (hBase, cbuf, 256);
		ComboSelectString (DlgItem<QComboBox> (hDlg, hList), cbuf);
	}
	if (hOldBase != hBase) ScanPadList (hDlg, IDC_PAD, hBase);
}

void ScnEditor::SetBasePosition (QWidget *hDlg)
{
	char cbuf[256];
	DWORD pad;
	double lng, lat;
	oapiGetDlgItemText (hDlg, IDC_REF, cbuf, 256);
	OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
	if (!hRef) return;
	oapiGetDlgItemText (hDlg, IDC_BASE, cbuf, 256);
	OBJHANDLE hBase = oapiGetBaseByName (hRef, cbuf);
	if (!hBase) return;
	oapiGetDlgItemText (hDlg, IDC_PAD, cbuf, 256);
	if (sscanf (cbuf, "%d", &pad) && pad >= 1 && pad <= oapiGetBasePadCount (hBase))
		oapiGetBasePadEquPos (hBase, pad-1, &lng, &lat);
	else
		oapiGetBaseEquPos (hBase, &lng, &lat);
	sprintf (cbuf, "%lf", lng * DEG);
	oapiSetDlgItemText (hDlg, IDC_EDIT1, cbuf);
	sprintf (cbuf, "%lf", lat * DEG);
	oapiSetDlgItemText (hDlg, IDC_EDIT2, cbuf);
}

bool ScnEditor::SaveScenario (QWidget *hDlg)
{
	char fname[256], title[256], text[4096], desc[4096];
	oapiGetDlgItemText (hDlg, IDC_EDIT1, fname, 256);
	oapiGetDlgItemText (hDlg, IDC_EDIT3, title, 256);
	oapiGetDlgItemText (hDlg, IDC_EDIT2, text, 4096);

	if (strlen(title)) {
		sprintf(desc, "<h1>%s</h1>\n", title);
	}
	else {
		desc[0] = '\0';
	}
	if (strlen(text)) {
		strcat(desc, "<p>");
		int j = strlen(desc);
		for (int i = 0; i < strlen(text) && j < 4090; i++) {
			if (text[i] == '\r') {
				continue;
			}
			else if (text[i] == '\n') {
				strcpy(desc + j, "</p>\n<p>");
				j = strlen(desc);
			}
			else
				desc[j++] = text[i];
		}
		strcpy(desc + j, "</p>");
	}
	return oapiSaveScenario(fname, desc);
}

void ScnEditor::InitDialog (QWidget *_hDlg)
{
	hDlg = _hDlg;
	AddTab (new EditorTab_Vessel (this));
	AddTab (new EditorTab_New (this));
	AddTab (new EditorTab_Save (this));
	AddTab (new EditorTab_Edit (this));
	AddTab (new EditorTab_Elements (this));
	AddTab (new EditorTab_Statevec (this));
	AddTab (new EditorTab_Landed (this));
	AddTab (new EditorTab_Orientation (this));
	AddTab (new EditorTab_AngularVel (this));
	AddTab (new EditorTab_Propellant (this));
	AddTab (new EditorTab_Docking (this));
	AddTab (new EditorTab_Date (this));
	nTab0 = nTab;
	oapiAddTitleButton (IDPAUSE, g_hPause, DLG_CB_TWOSTATE);
	Pause (oapiGetPause());
	ShowTab (0);
}

DWORD ScnEditor::AddTab (ScnEditorTab *newTab)
{
	if (newTab->TabHandle()) {
		ScnEditorTab **tmpTab = new ScnEditorTab*[nTab+1];
		if (nTab) {
			memcpy (tmpTab, pTab, nTab*sizeof(ScnEditorTab*));
			delete []pTab;
		}
		pTab = tmpTab;
		pTab[nTab] = newTab;
		return nTab++;
	} else return (DWORD)-1;
}

void ScnEditor::DelCustomTabs ()
{
	if (!nTab || nTab == nTab0) return; // nothing to do
	for (DWORD i = nTab0; i < nTab; i++) delete pTab[i];
	ScnEditorTab **tmpTab = new ScnEditorTab*[nTab0];
	memcpy (tmpTab, pTab, nTab0*sizeof(ScnEditorTab*));
	delete []pTab;
	pTab = tmpTab;
	nTab = nTab0;
}

void ScnEditor::ShowTab (DWORD t)
{
	if (t < nTab) {
		cTab = pTab[t];
		cTab->Show();
	}
}

void ScnEditor::MsgProc (QWidget *hDlg)
{
	// WM_INITDIALOG
		InitDialog (hDlg);
	// WM_COMMAND
	auto command = [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDCANCEL:
			CloseDialog();
			return;
		case IDHELP:
			if (cTab) cTab->OpenHelp();
			return;
		case IDPAUSE: // title button: the state comes as the notification code
			oapiSetPause (code != 0);
			return;
		}
	};
	oapiConnectDlgCommands (hDlg, command);
	// not upstream: DefDlgProc's WM_CLOSE (and Esc) -> IDCANCEL to this procedure, as an event filter
	new DlgEvents (hDlg, [command](QEvent *e) -> bool {
		if (e->type() == QEvent::Close || (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape)) {
			e->ignore();
			command (IDCANCEL, RESN_CLICKED, NULL);
			return true;
		}
		return false;
	});
	// oapiDefDialogProc left out: oapiOpenDialogEx wires the default dialog behaviour
}

bool ScnEditor::CreateVessel (char *name, char *classname)
{
	// define an arbitrary status (the user will have to edit this after creation)
	VESSELSTATUS2 vs;
	memset (&vs, 0, sizeof(vs));
	vs.version = 2;
	vs.rbody = oapiGetGbodyByName ((char*)"Earth");
	if (!vs.rbody) vs.rbody = oapiGetGbodyByIndex (0);
	double rad = 1.1 * oapiGetSize (vs.rbody);
	double vel = sqrt (GGRAV * oapiGetMass (vs.rbody) / rad);
	vs.rpos = _V(rad,0,0);
	vs.rvel = _V(0,0,vel);
	// create the vessel
	OBJHANDLE newvessel = oapiCreateVesselEx (name, classname, &vs);
	if (!newvessel) return false; // failure
	hVessel = newvessel;
	return true;
}

void ScnEditor::VesselDeleted (OBJHANDLE hV)
{
	if (!hDlg) return; // no action required

	// update editor after vessel has been deleted
	if (hVessel == hV) { // vessel is currently being edited
		for (DWORD i = 0; i < nTab; i++)
			pTab[i]->Hide();
		ShowTab (0);
	}
	((EditorTab_Vessel*)pTab[0])->VesselDeleted (hV);
}

char *ScnEditor::ExtractVesselName (char *str)
{
	int i;
	for (i = 0; str[i] && str[i] != '\t'; i++);
	str[i] = '\0';
	return str;
}

void ScnEditor::Pause (bool pause)
{
	if (hDlg) oapiSetTitleButtonState (hDlg, IDPAUSE, pause ? 1:0);
}

void *ScnEditor::LoadVesselLibrary (const VESSEL *vessel)
{
	// load vessel-specific editor extensions
	char cbuf[256], path[300];
	if (hEdLib) dlclose (hEdLib); // remove previous library
	if (vessel->GetEditorModule (cbuf)) {
		snprintf (path, 300, "Modules/%s.so", cbuf); // LoadLibrary found it through the Modules folder on the DLL path
		hEdLib = dlopen (oapiResolvePath (path).c_str(), RTLD_NOW);
	}
	else hEdLib = 0;
	return hEdLib;
}

// ==============================================================
// nonmember functions

void OpenDialog (void *context)
{
	((ScnEditor*)context)->OpenDialog();
}

void Crt2Pol (VECTOR3 &pos, VECTOR3 &vel)
{
	// position in polar coordinates
	double r      = sqrt  (pos.x*pos.x + pos.y*pos.y + pos.z*pos.z);
	double lng    = atan2 (pos.z, pos.x);
	double lat    = asin  (pos.y/r);
	// derivatives in polar coordinates
	double drdt   = (vel.x*pos.x + vel.y*pos.y + vel.z*pos.z) / r;
	double dlngdt = (vel.z*pos.x - pos.z*vel.x) / (pos.x*pos.x + pos.z*pos.z);
	double dlatdt = (vel.y*r - pos.y*drdt) / (r*sqrt(r*r - pos.y*pos.y));
	pos.data[0] = r;
	pos.data[1] = lng;
	pos.data[2] = lat;
	vel.data[0] = drdt;
	vel.data[1] = dlngdt;
	vel.data[2] = dlatdt;
}

void Pol2Crt (VECTOR3 &pos, VECTOR3 &vel)
{
	double r   = pos.data[0];
	double lng = pos.data[1], clng = cos(lng), slng = sin(lng);
	double lat = pos.data[2], clat = cos(lat), slat = sin(lat);
	// position in cartesian coordinates
	double x   = r * cos(lat) * cos(lng);
	double z   = r * cos(lat) * sin(lng);
	double y   = r * sin(lat);
	// derivatives in cartesian coordinates
	double dxdt = vel.data[0]*clat*clng - vel.data[1]*r*clat*slng - vel.data[2]*r*slat*clng;
	double dzdt = vel.data[0]*clat*slng + vel.data[1]*r*clat*clng - vel.data[2]*r*slat*slng;
	double dydt = vel.data[0]*slat + vel.data[2]*r*clat;
	pos.data[0] = x;
	pos.data[1] = y;
	pos.data[2] = z;
	vel.data[0] = dxdt;
	vel.data[1] = dydt;
	vel.data[2] = dzdt;
}

void EditorProc (QWidget *hDlg, void *context)
{
	g_editor->MsgProc (hDlg);
}

// ==============================================================
// ScnEditorTab class definition
// ==============================================================

ScnEditorTab::ScnEditorTab (ScnEditor *editor)
{
	ed = editor;
	QWidget *hDlg = ed->DlgHandle();
	hTab = 0;
}

ScnEditorTab::~ScnEditorTab ()
{
	DestroyTab ();
}

QWidget *ScnEditorTab::CreateTab (void *hInst, WORD ResId,  DLGINIT TabProc)
{
	hTab = oapiCreateResDialog (hInst, ResId, ed->DlgHandle()); // CreateDialogParam
	if (!hTab) return hTab;
	if (!hTab->testAttribute(Qt::WA_WState_ExplicitShowHide)) hTab->hide(); // no WS_VISIBLE: Qt shows a child not hidden explicitly along with its parent
	hTab->setProperty ("DWLP_USER", QVariant::fromValue ((void*)this)); // SetWindowLongPtr, set before WM_INITDIALOG: a custom page may call ScnEditorMsg there
	TabProc (hTab, this); // WM_INITDIALOG
	return hTab;
}

QWidget *ScnEditorTab::CreateTab (WORD ResId, DLGINIT TabProc)
{
	return CreateTab (ed->InstHandle(), ResId, TabProc);
}

void ScnEditorTab::DestroyTab ()
{
	if (hTab) {
		hTab->hide(); // DestroyWindow; deleted later, since a control of the page may be sending the request
		hTab->deleteLater();
		hTab = 0;
	}
}

void ScnEditorTab::SwitchTab (int newtab)
{
	hTab->hide();
	ed->ShowTab (newtab);
}

void ScnEditorTab::Show ()
{
	hTab->show();
	g_hc.topic = HelpTopic();
	InitTab ();
}

void ScnEditorTab::Hide ()
{
	hTab->hide();
}

char *ScnEditorTab::HelpTopic ()
{
	return (char*)"/ScnEditor.htm";
}

void ScnEditorTab::OpenHelp ()
{
	static HELPCONTEXT hc = {
		(char*)"html/plugin/ScnEditor.chm",
		0,
		(char*)"html/plugin/ScnEditor.chm::/ScnEditor.hhc",
		(char*)"html/plugin/ScnEditor.chm::/ScnEditor.hhk"
	};
	hc.topic = HelpTopic();
	oapiOpenHelp (&hc);
}

void ScnEditorTab::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDHELP:
			OpenHelp();
			return;
		}
	});
}

ScnEditorTab *ScnEditorTab::TabPointer (QWidget *hDlg, void *context)
{
	if (context) return (ScnEditorTab*)context; // WM_INITDIALOG
	else return (ScnEditorTab*)hDlg->property ("DWLP_USER").value<void*>();
}

void ScnEditorTab::ScanVesselList (int ResId, bool detail, OBJHANDLE hExclude)
{
	char cbuf[256], *cp;

	// populate vessel list
	QSignalBlocker block (DlgItem<QListWidget> (hTab, ResId)); // LB_ messages don't notify
	DlgItem<QListWidget> (hTab, ResId)->clear();
	for (DWORD i = 0; i < oapiGetVesselCount(); i++) {
		OBJHANDLE hR, hV = oapiGetVesselByIndex (i);
		if (hV == hExclude) continue;
		VESSEL *vessel = oapiGetVesselInterface (hV);
		strcpy (cbuf, vessel->GetName());
		if (detail) {
			if (cp = vessel->GetClassName())
				sprintf (cbuf+strlen(cbuf), "\t(%s)", cp);
			else strcat (cbuf, "\t");
			if (hR = vessel->GetGravityRef()) {
				char rname[256];
				oapiGetObjectName (hR, rname, 256);
				sprintf (cbuf+strlen(cbuf), "\t%s", rname);
			}
		}
		DlgItem<QListWidget> (hTab, ResId)->addItem (QString::fromUtf8 (cbuf));
	}
}

OBJHANDLE ScnEditorTab::GetVesselFromList (int ResId)
{
	char cbuf[256];
	int idx = ListGetCurSel (DlgItem<QListWidget> (hTab, ResId));
	ListGetText (DlgItem<QListWidget> (hTab, ResId), idx, cbuf, 256);
	OBJHANDLE hV = oapiGetVesselByName (ed->ExtractVesselName (cbuf));
	return hV;
}


// ==============================================================
// EditorTab_Vessel class definition
// ==============================================================

EditorTab_Vessel::EditorTab_Vessel (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_VESSEL, EditorTab_Vessel::DlgProc);
}

void EditorTab_Vessel::InitTab ()
{
	// LB_SETTABSTOPS (70 dialog units) left out: Qt list items expand tabs to fixed tab stops
	ScanVesselList ();
}

void EditorTab_Vessel::ScanVesselList ()
{
	ScnEditorTab::ScanVesselList (IDC_LIST1, true);
	// update vessel list
	SelectVessel (0);
}

char *EditorTab_Vessel::HelpTopic ()
{
	return (char*)"/ScnEditor.htm";
}

void EditorTab_Vessel::TabProc (QWidget *hDlg)
{
	// WM_INITDIALOG
		DlgItem<QAbstractButton> (hDlg, IDC_TRACK)->setChecked (true);
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		int i;
		char cbuf[256];
		switch (id) {
		case IDCANCEL:
			ed->CloseDialog();
			return;
		case IDC_VESSELNEW:
			SwitchTab (1);
			return;
		case IDC_VESSELEDIT:
			i = ListGetCurSel (DlgItem<QListWidget> (hTab, IDC_LIST1));
			ListGetText (DlgItem<QListWidget> (hTab, IDC_LIST1), i, cbuf, 256);
			ed->hVessel = oapiGetVesselByName (ed->ExtractVesselName(cbuf));
			if (ed->hVessel) SwitchTab (3);
			return;
		case IDC_VESSELDEL:
			DeleteVessel();
			return;
		case IDC_SAVE:
			SwitchTab (2);
			return;
		case IDC_DATE:
			SwitchTab (11);
			return;
		case IDC_LIST1:
			if (code == RESN_SELCHANGE) {
				VesselSelected ();
				return;
			}
			break;
		}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Vessel::SelectVessel (OBJHANDLE hV)
{
	char cbuf[256];
	if (!hV) hV = oapiCameraTarget();
	oapiGetObjectName (hV, cbuf, 256);
	int idx = ListFindString (DlgItem<QListWidget> (hTab, IDC_LIST1), cbuf);
	if (idx == LB_ERR) { // last resort: focus object
		hV = oapiGetFocusObject();
		oapiGetObjectName (hV, cbuf, 256);
		idx = ListFindString (DlgItem<QListWidget> (hTab, IDC_LIST1), cbuf);
	}
	if (idx != LB_ERR) {
		QSignalBlocker block (DlgItem<QListWidget> (hTab, IDC_LIST1)); // LB_SETCURSEL doesn't notify
		DlgItem<QListWidget> (hTab, IDC_LIST1)->setCurrentRow (idx);
		ed->hVessel = hV;
	}
}

void EditorTab_Vessel::VesselSelected ()
{
	OBJHANDLE hV = GetVesselFromList (IDC_LIST1);
	if (!hV) return;
	bool track = (DlgItem<QAbstractButton> (hTab, IDC_TRACK)->isChecked());
	if (track) oapiCameraAttach (hV, 1);
}

void EditorTab_Vessel::VesselDeleted (OBJHANDLE hV)
{
	char cbuf[256];
	oapiGetObjectName (hV, cbuf, 256);
	int idx = ListFindString (DlgItem<QListWidget> (hTab, IDC_LIST1), cbuf);
	if (idx == LB_ERR) return;
	QSignalBlocker block (DlgItem<QListWidget> (hTab, IDC_LIST1)); // LB_ messages don't notify
	delete DlgItem<QListWidget> (hTab, IDC_LIST1)->takeItem (idx); // LB_DELETESTRING
	idx = ListGetCurSel (DlgItem<QListWidget> (hTab, IDC_LIST1));
	if (idx == LB_ERR) { // deleted current selection
		DlgItem<QListWidget> (hTab, IDC_LIST1)->setCurrentRow (0);
		VesselSelected ();
	}
}

// Orbiter closes when there is no focusable vessels in the scenario
// Check if there's another focusable object present
bool EditorTab_Vessel::CanDelete(OBJHANDLE hVessel)
{
	for(int i = 0; i < oapiGetVesselCount(); i++) {
		OBJHANDLE obj = oapiGetVesselByIndex (i);
		if(obj == hVessel)
			continue;
		VESSEL *v = oapiGetVesselInterface (obj);
		if(v->GetEnableFocus())
			return true;
	}
	return false;
}

bool EditorTab_Vessel::DeleteVessel ()
{
	char cbuf[256];
	int idx = ListGetCurSel (DlgItem<QListWidget> (hTab, IDC_LIST1));
	if (idx == LB_ERR) return false;
	ListGetText (DlgItem<QListWidget> (hTab, IDC_LIST1), idx, cbuf, 256);
	OBJHANDLE hV = oapiGetVesselByName (ed->ExtractVesselName (cbuf));
	if (!hV) return false;
	//ed->hVessel = hV;
	if(CanDelete(hV)) {
		oapiDeleteVessel (hV);
	} else {
		QMessageBox (QMessageBox::NoIcon, "Error", "Cannot delete the last focusable vessel in a scenario", QMessageBox::Ok, hTab).exec(); // MessageBox with a NULL caption
		return false;
	}
	return true;
}

void EditorTab_Vessel::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Vessel *pTab = (EditorTab_Vessel*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}

// ==============================================================
// EditorTab_New class definition
// ==============================================================

EditorTab_New::EditorTab_New (ScnEditor *editor) : ScnEditorTab (editor)
{
	hVesselBmp = NULL;
	CreateTab (IDD_TAB_NEW, EditorTab_New::DlgProc);
}

EditorTab_New::~EditorTab_New ()
{
	if (hVesselBmp) delete hVesselBmp;
}

void EditorTab_New::InitTab ()
{
	DlgItem<QAbstractButton> (hTab, IDC_CAMERA)->setChecked (true);

	// populate vessel type list
	RefreshVesselTpList ();
	// TVM_SETIMAGELIST left out: the tree items carry their icons from ed->imglist (see ScanConfigDir)

	QRect r = oapiResDlgItem (hTab, IDC_VESSELBMP)->rect(); // GetClientRect
	imghmax = r.height();
}

char *EditorTab_New::HelpTopic ()
{
	return (char*)"/NewVessel.htm";
}

void EditorTab_New::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDCANCEL:
			SwitchTab (0);
			return;
		case IDC_CREATE:
			if (CreateVessel ()) SwitchTab (3); // switch to editor page
			return;
		}
	});
	// WM_NOTIFY: IDC_VESSELTP
	QTreeWidget *tv = DlgItem<QTreeWidget> (hDlg, IDC_VESSELTP);
	QObject::connect (tv, &QTreeWidget::itemSelectionChanged, hDlg, [this]() { // TVN_SELCHANGED
		VesselTpChanged ();
	});
	QObject::connect (tv, &QTreeWidget::itemDoubleClicked, hDlg, [hDlg]() { // NM_DBLCLK
		PostCommand (hDlg, IDC_CREATE);
	});
	// WM_PAINT left out: DrawVesselBmp puts the picture into the label, which paints itself
	ScnEditorTab::TabProc (hDlg);
}

bool EditorTab_New::CreateVessel ()
{
	char name[256], classname[256];

	oapiGetDlgItemText (hTab, IDC_NAME, name, 256);
	if (name[0] == '\0') return false;            // no name provided
	if (oapiGetVesselByName (name)) return false; // vessel name already in use

	if (GetSelVesselTp (classname, 256) != 1) return false; // no type selected
	if (!ed->CreateVessel (name, classname)) return false; // creation failed
	if (DlgItem<QAbstractButton> (hTab, IDC_FOCUS)->isChecked())
		oapiSetFocusObject (ed->hVessel);
	if (DlgItem<QAbstractButton> (hTab, IDC_CAMERA)->isChecked()) // BM_GETSTATE == BST_CHECKED
		oapiCameraAttach (ed->hVessel, 1);
	//	ed->VesselSelection();
	return true;
}

void EditorTab_New::ScanConfigDir (const fs::path& dir, QTreeWidgetItem *hti)
{
	// recursively scans a directory tree and adds to the list
	QTreeWidget *tv = DlgItem<QTreeWidget> (hTab, IDC_VESSELTP);
	QTreeWidgetItem *parent = (hti ? hti : tv->invisibleRootItem()); // TVI_ROOT
	QTreeWidgetItem *ht, *hts0, *ht0;
	char cbuf[256];
	int i;

	// TV_INSERTSTRUCT: folders show a child indicator (cChildren = 1) and the folder icon
	QIcon diricon = TreeIcon (ed->imglist, ed->treeicon_idx[0], ed->treeicon_idx[0]);

	// scan for subdirectories
	for (const auto& entry : fs::directory_iterator(dir)) {
		if (entry.is_directory()) {
			strcpy(cbuf, entry.path().filename().string().c_str());
			QString s = QString::fromUtf8 (cbuf);
			for (i = 0; i < parent->childCount(); i++) // TVI_SORT
				if (QString::compare (parent->child (i)->text (0), s, Qt::CaseInsensitive) > 0) break;
			ht = new QTreeWidgetItem (QStringList (s)); // TVM_INSERTITEM
			ht->setChildIndicatorPolicy (QTreeWidgetItem::ShowIndicator);
			ht->setIcon (0, diricon);
			parent->insertChild (i, ht);
			ScanConfigDir(entry.path(), ht);
		}
	}

	hts0 = parent->child (0); // TVGN_CHILD
	// the first subdirectory entry in this folder

	// scan for files
	QIcon fileicon = TreeIcon (ed->imglist, ed->treeicon_idx[2], ed->treeicon_idx[3]); // cChildren = 0
	for (const auto& entry : fs::directory_iterator(dir)) {
		if (entry.is_regular_file() && entry.path().extension().string() == ".cfg") {
			bool skip = false;
			FILEHANDLE hFile = oapiOpenFile(entry.path().string().c_str(), FILE_IN);
			if (hFile) {
				bool b;
				skip = (oapiReadItem_bool(hFile, (char*)"EditorCreate", b) && !b);
				oapiCloseFile(hFile, FILE_IN);
			}
			if (skip) continue;

			strcpy(cbuf, entry.path().stem().string().c_str());

			ht0 = parent->child (0); // TVGN_CHILD
			for (i = 0, ht = ht0; ht && ht != hts0; ht = parent->child (++i)) { // TVGN_NEXT
				if (strcmp(ht->text (0).toUtf8().constData(), cbuf) > 0) break;
			}
			if (!ht) { // otherwise insert after TVGN_PREVIOUS of ht, i.e. at its position
				i = (hts0 ? 0 : parent->childCount()); // TVI_FIRST : TVI_LAST
			}
			ht = new QTreeWidgetItem (QStringList (QString::fromUtf8 (cbuf))); // TVM_INSERTITEM
			ht->setIcon (0, fileicon);
			parent->insertChild (i, ht);
		}
	}
}

void EditorTab_New::RefreshVesselTpList ()
{
	DlgItem<QTreeWidget> (hTab, IDC_VESSELTP)->clear(); // TVM_DELETEITEM TVI_ROOT
	ScanConfigDir ("Config/Vessels/", NULL);
}

int EditorTab_New::GetSelVesselTp (char *name, int len)
{
	char cbuf[256];
	int type;

	QTreeWidgetItem *hItem = DlgItem<QTreeWidget> (hTab, IDC_VESSELTP)->currentItem(); // TreeView_GetSelection

	if (!hItem) return 0; // TreeView_GetItem fails
	snprintf (name, len, "%s", hItem->text (0).toUtf8().constData());
	type = (hItem->childIndicatorPolicy() == QTreeWidgetItem::ShowIndicator ? 2 : 1); // cChildren

	// build path
	while ((hItem = hItem->parent())) { // TreeView_GetParent
		snprintf (cbuf, 256, "%s", hItem->text (0).toUtf8().constData()); // TreeView_GetItem
		strcat (cbuf, "/");
		strcat (cbuf, name);
		strcpy (name, cbuf);
	}
	return type;
}

void EditorTab_New::VesselTpChanged ()
{
	UpdateVesselBmp();
}

bool EditorTab_New::UpdateVesselBmp ()
{
	char classname[256], pathname[256], imagename[256];

	if (hVesselBmp) {
		delete hVesselBmp; // DeleteObject
		hVesselBmp = NULL;
	}

	if (GetSelVesselTp (classname, 256) == 1) {
		sprintf (pathname, "Vessels/%s.cfg", classname);
		FILEHANDLE hFile = oapiOpenFile (pathname, FILE_IN, CONFIG);
		if (!hFile) return false;
		if (oapiReadItem_string (hFile, (char*)"ImageBmp", imagename)) {
			hVesselBmp = new QImage (QString::fromStdString (oapiResolvePath (imagename))); // LoadImage (LR_LOADFROMFILE)
			if (hVesselBmp->isNull()) { delete hVesselBmp; hVesselBmp = NULL; }
		}
		oapiCloseFile (hFile, FILE_IN);
	}
	DrawVesselBmp();
	return (hVesselBmp != NULL);
}

void EditorTab_New::DrawVesselBmp ()
{
	QLabel *hImgWnd = DlgItem<QLabel> (hTab, IDC_VESSELBMP);
	// InvalidateRect/UpdateWindow left out: the label repaints itself with a new picture
	if (hVesselBmp) {
		QRect r;
		int dx, dy, h;
		r = hImgWnd->rect(); // GetClientRect
		dx = hVesselBmp->width(), dy = hVesselBmp->height();
		h = min (imghmax, (int)(r.width()*dy)/dx);
		hImgWnd->resize (r.width(), h); // SetWindowPos (SWP_NOMOVE)
		hImgWnd->setPixmap (QPixmap::fromImage (hVesselBmp->scaled (r.width(), h))); // StretchBlt
		hImgWnd->show();
	} else {
		hImgWnd->hide();
	}
}

void EditorTab_New::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_New *pTab = (EditorTab_New*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}

// ==============================================================
// EditorTab_Save class definition
// ==============================================================

EditorTab_Save::EditorTab_Save (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_SAVE, EditorTab_Save::DlgProc);
	// this page has no IDC_RADIO1/IDC_RADIO2 (SendDlgItemMessage did nothing)
	if (QAbstractButton *b = DlgItem<QAbstractButton> (hTab, IDC_RADIO1)) b->setChecked (true);
	if (QAbstractButton *b = DlgItem<QAbstractButton> (hTab, IDC_RADIO2)) b->setChecked (false);
}

char *EditorTab_Save::HelpTopic ()
{
	return (char*)"/SaveScenario.htm";
}

void EditorTab_Save::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (0);
			return;
		case IDOK:
			if (ed->SaveScenario (hTab))
				SwitchTab (0);
			return;
		}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Save::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Save *pTab = (EditorTab_Save*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}


// ==============================================================
// EditorTab_Date class definition
// ==============================================================

EditorTab_Date::EditorTab_Date (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_DATE, EditorTab_Date::DlgProc);
}

void EditorTab_Date::InitTab ()
{
	Refresh();
}

char *EditorTab_Date::HelpTopic ()
{
	return (char*)"/Date.htm";
}

void EditorTab_Date::Apply ()
{
	int OrbitalMode[3] = {PROP_ORBITAL_FIXEDSTATE, PROP_ORBITAL_FIXEDSURF, PROP_ORBITAL_ELEMENTS};
	int SOrbitalMode[4] = {PROP_SORBITAL_FIXEDSTATE, PROP_SORBITAL_FIXEDSURF, PROP_SORBITAL_ELEMENTS, PROP_SORBITAL_DESTROY};
	int omode = OrbitalMode[DlgItem<QComboBox> (hTab, IDC_PROP_ORBITAL)->currentIndex()];
	int smode = SOrbitalMode[DlgItem<QComboBox> (hTab, IDC_PROP_SORBITAL)->currentIndex()];
	oapiSetSimMJD (mjd, omode | smode);
}

void EditorTab_Date::Refresh ()
{
	SetMJD (oapiGetSimMJD (), true);
}

void EditorTab_Date::UpdateDateTime ()
{
	char cbuf[256];

	sprintf (cbuf, "%02d", date.tm_mday);
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_UT_DAY, cbuf);
	bIgnore = false;

	sprintf (cbuf, "%02d", date.tm_mon);
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_UT_MONTH, cbuf);
	bIgnore = false;

	sprintf (cbuf, "%04d", date.tm_year+1900);
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_UT_YEAR, cbuf);
	bIgnore = false;

	sprintf (cbuf, "%02d", date.tm_hour);
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_UT_HOUR, cbuf);
	bIgnore = false;

	sprintf (cbuf, "%02d", date.tm_min);
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_UT_MIN, cbuf);
	bIgnore = false;

	sprintf (cbuf, "%02d", date.tm_sec);
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_UT_SEC, cbuf);
	bIgnore = false;
}

void EditorTab_Date::UpdateMJD (void)
{
	char cbuf[256];
	sprintf (cbuf, "%0.6f", mjd);
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_MJD, cbuf);
	bIgnore = false;
}

void EditorTab_Date::UpdateJD (void)
{
	char cbuf[256];
	sprintf (cbuf, "%0.6f", mjd + 2400000.5);
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_JD, cbuf);
	bIgnore = false;
}

void EditorTab_Date::UpdateJC (void)
{
	char cbuf[256];
	sprintf (cbuf, "%0.10f", MJD2JC(mjd));
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_JC2000, cbuf);
	bIgnore = false;
}

void EditorTab_Date::UpdateEpoch (void)
{
	char cbuf[256];
	sprintf (cbuf, "%0.8f", MJD2Jepoch (mjd));
	bIgnore = true;
	oapiSetDlgItemText (hTab, IDC_EPOCH, cbuf);
	bIgnore = false;
}

void EditorTab_Date::SetUT (struct tm *new_date, bool reset_ut)
{
	mjd = date2mjd (new_date);
	UpdateMJD();
	UpdateJD();
	UpdateJC();
	UpdateEpoch();
	if (reset_ut) UpdateDateTime();
}

void EditorTab_Date::SetMJD (double new_mjd, bool reset_mjd)
{
	mjd = new_mjd;
	memcpy (&date, mjddate(mjd), sizeof (date));

	UpdateDateTime();
	UpdateJD();
	UpdateJC();
	UpdateEpoch();
	if (reset_mjd) UpdateMJD();
}

void EditorTab_Date::SetJD (double new_jd, bool reset_jd)
{
	mjd = new_jd - 2400000.5;
	memcpy (&date, mjddate(mjd), sizeof (date));

	UpdateDateTime();
	UpdateMJD();
	UpdateJC();
	UpdateEpoch();
	if (reset_jd) UpdateJD();
}

void EditorTab_Date::SetJC (double new_jc, bool reset_jc)
{
	mjd = JC2MJD (new_jc);
	memcpy (&date, mjddate(mjd), sizeof (date));

	UpdateDateTime();
	UpdateMJD();
	UpdateJD();
	UpdateEpoch();
	if (reset_jc) UpdateJC();
}

void EditorTab_Date::SetEpoch (double new_epoch, bool reset_epoch)
{
	mjd = Jepoch2MJD (new_epoch);
	memcpy (&date, mjddate(mjd), sizeof (date));

	UpdateDateTime();
	UpdateMJD();
	UpdateJD();
	UpdateJC();
	if (reset_epoch) UpdateEpoch();
}

void EditorTab_Date::OnChangeDateTime() 
{
	if (bIgnore) return;
	char cbuf[256];
	int day, month, year, hour, min, sec;

	oapiGetDlgItemText (hTab, IDC_UT_DAY, cbuf, 256);
	if (sscanf (cbuf, "%d", &day) != 1 || day < 1 || day > 31) return;
	oapiGetDlgItemText (hTab, IDC_UT_MONTH, cbuf, 256);
	if (sscanf (cbuf, "%d", &month) != 1 || month < 1 || month > 12) return;
	oapiGetDlgItemText (hTab, IDC_UT_YEAR, cbuf, 256);
	if (sscanf (cbuf, "%d", &year) != 1) return;
	year -= 1900;
	oapiGetDlgItemText (hTab, IDC_UT_HOUR, cbuf, 256);
	if (sscanf (cbuf, "%d", &hour) != 1) return;
	oapiGetDlgItemText (hTab, IDC_UT_MIN, cbuf, 256);
	if (sscanf (cbuf, "%d", &min) != 1) return;
	oapiGetDlgItemText (hTab, IDC_UT_SEC, cbuf, 256);
	if (sscanf (cbuf, "%d", &sec) != 1) return;

	if (day == date.tm_mday && month == date.tm_mon && year == date.tm_year &&
		hour == date.tm_hour && min == date.tm_min && sec == date.tm_sec) return;

	date.tm_mday = day;
	date.tm_mon  = month;
	date.tm_year = year;
	date.tm_hour = hour;
	date.tm_min  = min;
	date.tm_sec  = sec;
	SetUT (&date);
}

void EditorTab_Date::OnChangeMjd() 
{
	if (bIgnore) return;
	char cbuf[256];
	double new_mjd;
	oapiGetDlgItemText (hTab, IDC_MJD, cbuf, 256);
	if (sscanf (cbuf, "%lf", &new_mjd) == 1 && fabs (new_mjd-mjd) > 1e-6)
		SetMJD (new_mjd);
}

void EditorTab_Date::OnChangeJd() 
{
	if (bIgnore) return;
	char cbuf[256];
	double new_jd;
	oapiGetDlgItemText (hTab, IDC_JD, cbuf, 256);
	if (sscanf (cbuf, "%lf", &new_jd) == 1)
		SetJD (new_jd);
}

void EditorTab_Date::OnChangeJc() 
{
	if (bIgnore) return;
	char cbuf[256];
	double new_jc;
	oapiGetDlgItemText (hTab, IDC_JC2000, cbuf, 256);
	if (sscanf (cbuf, "%lf", &new_jc) == 1)
		SetJC (new_jc);
}

void EditorTab_Date::OnChangeEpoch() 
{
	if (bIgnore) return;
	char cbuf[256];
	double new_epoch;
	oapiGetDlgItemText (hTab, IDC_EPOCH, cbuf, 256);
	if (sscanf (cbuf, "%lf", &new_epoch) == 1)
		SetEpoch (new_epoch);
}

void EditorTab_Date::TabProc (QWidget *hDlg)
{
	// WM_INITDIALOG
	{
		int i;
		DlgItem<QComboBox> (hDlg, IDC_PROP_ORBITAL)->clear();
		for (i = 0; i < 3; i++) {
			char cbuf[128];
			oapiLoadResString (ed->InstHandle(), IDS_PROP1+i, cbuf, 128);
			oapiComboAddString (DlgItem<QComboBox> (hDlg, IDC_PROP_ORBITAL), cbuf);
		}
		DlgItem<QComboBox> (hDlg, IDC_PROP_ORBITAL)->setCurrentIndex (2);
		DlgItem<QComboBox> (hDlg, IDC_PROP_SORBITAL)->clear();
		for (i = 0; i < 4; i++) {
			char cbuf[128];
			oapiLoadResString (ed->InstHandle(), IDS_PROP1+i, cbuf, 128);
			oapiComboAddString (DlgItem<QComboBox> (hDlg, IDC_PROP_SORBITAL), cbuf);
		}
		DlgItem<QComboBox> (hDlg, IDC_PROP_SORBITAL)->setCurrentIndex (1);
	}
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (0);
			return;
		case IDC_REFRESH:
			Refresh();
			return;
		case IDC_NOW:
			SetMJD (oapiGetSysMJD(), true);
			// fall through
		case IDC_APPLY:
			Apply();
			return;
		case IDC_UT_DAY:
		case IDC_UT_MONTH:
		case IDC_UT_YEAR:
		case IDC_UT_HOUR:
		case IDC_UT_MIN:
		case IDC_UT_SEC:
			OnChangeDateTime ();
			return;
		case IDC_MJD:
			OnChangeMjd ();
			return;
		case IDC_JD:
			OnChangeJd ();
			return;
		case IDC_JC2000:
			OnChangeJc ();
			return;
		case IDC_EPOCH:
			OnChangeEpoch ();
			return;
		}
	});
	// WM_NOTIFY
	oapiConnectDlgDeltaPos (hDlg, [this](int idFrom, int iPos, int iDelta) { // UDN_DELTAPOS
			double dmjd = 0;
			bool dut = false;
			switch (idFrom) {
			case IDC_SPIN_DAY:
				dmjd = -iDelta;
				break;
			case IDC_SPIN_HOUR:
				dmjd = -iDelta/24.0;
				break;
			case IDC_SPIN_MINUTE:
				dmjd = -iDelta/(24.0*60.0);
				break;
			case IDC_SPIN_SECOND:
				dmjd = -iDelta/(24.0*3600.0);
				break;
			case IDC_SPIN_MONTH:
				if (iDelta > 0) {
					date.tm_mon--;
					if (date.tm_mon < 1) date.tm_year--, date.tm_mon = 12;
				} else {
					date.tm_mon++;
					if (date.tm_mon > 12) date.tm_year++, date.tm_mon = 1;
				}
				dut = true;
				break;
			case IDC_SPIN_YEAR:
				date.tm_year -= iDelta;
				dut = true;
				break;
			}
			if (dmjd || dut) {
				if (dmjd) SetMJD (mjd+dmjd, true);
				else SetUT (&date, true);
				Apply();
			}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Date::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Date *pTab = (EditorTab_Date*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}


// ==============================================================
// EditorTab_Edit class definition
// ==============================================================

EditorTab_Edit::EditorTab_Edit (ScnEditor *editor) : ScnEditorTab (editor)
{
	hVessel = 0;
	CreateTab (IDD_TAB_EDIT1, EditorTab_Edit::DlgProc);
}

void EditorTab_Edit::InitTab ()
{
	VESSEL *vessel = Vessel();

	if (hVessel != ed->hVessel) {
		hVessel = ed->hVessel;

		// fill vessel and class name boxes
		oapiSetDlgItemText (hTab, IDC_EDIT1, vessel->GetName());
		oapiSetDlgItemText (hTab, IDC_EDIT2, vessel->GetClassName());
		oapiSetDlgItemText (hTab, IDC_STATIC1, vessel->GetClassName());

		BOOL bFuel = (vessel->GetPropellantCount() > 0 && vessel->GetMaxFuelMass () > 0.0);
		oapiResDlgItem (hTab, IDC_PROPELLANT)->setEnabled (bFuel);

		BOOL bDocking = (vessel->DockCount() > 0);
		oapiResDlgItem (hTab, IDC_DOCKING)->setEnabled (bDocking);

		// disable custom buttons by default
		nCustom = 0;
		ed->DelCustomTabs();
		oapiResDlgItem (hTab, IDC_STATIC1)->hide();
		for (int i = 0; i < 6; i++) {
			CustomPage[i] = 0;
			oapiResDlgItem (hTab, IDC_EXTRA1+i)->hide();
		}

		// now load vessel-specific interface
		void *hLib = ed->LoadVesselLibrary (vessel);
		if (hLib) {
			typedef void (*SEC_Init)(QWidget*,OBJHANDLE);
			SEC_Init secInit = (SEC_Init)dlsym (hLib, "secInit");
			if (secInit) secInit (hTab, hVessel);
		}
	}
	oapiSetDlgItemText (hTab, IDC_EDIT3,
		vessel->GetFlightStatus() & 1 ? "Inactive (Landed)":"Active (Flight)");
}

char *EditorTab_Edit::HelpTopic ()
{
	return (char*)"/EditVessel.htm";
}

BOOL EditorTab_Edit::AddFuncButton (EditorFuncSpec *efs)
{
	if (nCustom == 6) return FALSE;
	oapiResDlgItem (hTab, IDC_STATIC1)->show();
	oapiSetDlgItemText (hTab, IDC_EXTRA1+nCustom, efs->btnlabel);
	oapiResDlgItem (hTab, IDC_EXTRA1+nCustom)->show();
	funcCustom[nCustom++] = efs->func;
	return TRUE;

}

BOOL EditorTab_Edit::AddPageButton (EditorPageSpec *eps)
{
	if (nCustom == 6) return FALSE;
	oapiResDlgItem (hTab, IDC_STATIC1)->show();
	oapiSetDlgItemText (hTab, IDC_EXTRA1+nCustom, eps->btnlabel);
	oapiResDlgItem (hTab, IDC_EXTRA1+nCustom)->show();
	CustomPage[nCustom++] = ed->AddTab (new EditorTab_Custom (ed, eps->hDLL, eps->ResId, eps->TabProc));
	return TRUE;
}

void EditorTab_Edit::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int ctlid, int code, QWidget *hCtrl) {
		switch (ctlid) {
		case IDC_BACK:
			SwitchTab (0);
			return;
		case IDC_ELEMENTS:
			SwitchTab (4);
			return;
		case IDC_STATEVEC:
			SwitchTab (5);
			return;
		case IDC_GROUND:
			SwitchTab (6);
			return;
		case IDC_ORIENT:
			SwitchTab (7);
			return;
		case IDC_ANGVEL:
			SwitchTab (8);
			return;
		case IDC_PROPELLANT:
			SwitchTab (9);
			return;
		case IDC_DOCKING:
			SwitchTab (10);
			return;
		case IDC_EXTRA1:
		case IDC_EXTRA2:
		case IDC_EXTRA3:
		case IDC_EXTRA4:
		case IDC_EXTRA5:
		case IDC_EXTRA6: {
			int id = ctlid-IDC_EXTRA1;
			if (CustomPage[id])
				SwitchTab (CustomPage[id]);
			else
				funcCustom[id](ed->hVessel);
			} return;
		}
	});
	// WM_SCNEDITOR: requests of the vessel module through ScnEditorMsg, also while its secInit runs
	SCNEDITORMSG msgproc = [](QWidget *hDlg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
		EditorTab_Edit *pTab = (EditorTab_Edit*)TabPointer (hDlg);
		switch (LOWORD (wParam)) {
		case SE_ADDFUNCBUTTON:
			return pTab->AddFuncButton ((EditorFuncSpec*)lParam);
		case SE_ADDPAGEBUTTON:
			return pTab->AddPageButton ((EditorPageSpec*)lParam);
		}
		return FALSE;
	};
	hDlg->setProperty ("ScnEditorMsg", QVariant::fromValue ((void*)msgproc));
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Edit::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Edit *pTab = (EditorTab_Edit*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}


// ==============================================================
// EditorTab_Elements class definition
// ==============================================================

EditorTab_Elements::EditorTab_Elements (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_EDIT2, EditorTab_Elements::DlgProc);
	elmjd = oapiGetSimMJD(); // initial reference date
}

void EditorTab_Elements::InitTab ()
{
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	OBJHANDLE hRef = vessel->GetGravityRef();
	DlgItem<QComboBox> (hTab, IDC_COMBO1)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO1), "m");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO1), "km");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO1), "AU");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO1), "planet rad.");
	DlgItem<QComboBox> (hTab, IDC_COMBO1)->setCurrentIndex (0);

	DlgItem<QComboBox> (hTab, IDC_COMBO2)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO2), "deg");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO2), "rad");
	DlgItem<QComboBox> (hTab, IDC_COMBO2)->setCurrentIndex (0);

	DlgItem<QComboBox> (hTab, IDC_COMBO3)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO3), "deg");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO3), "rad");
	DlgItem<QComboBox> (hTab, IDC_COMBO3)->setCurrentIndex (0);

	DlgItem<QComboBox> (hTab, IDC_COMBO4)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO4), "deg");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO4), "rad");
	DlgItem<QComboBox> (hTab, IDC_COMBO4)->setCurrentIndex (0);

	DlgItem<QComboBox> (hTab, IDC_COMBO5)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO5), "deg");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO5), "rad");
	DlgItem<QComboBox> (hTab, IDC_COMBO5)->setCurrentIndex (0);

	DlgItem<QComboBox> (hTab, IDC_COMBO6)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO6), "current");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO6), "MJD");
	DlgItem<QComboBox> (hTab, IDC_COMBO6)->setCurrentIndex (1);

	DlgItem<QComboBox> (hTab, IDC_FRM)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_FRM), "ecliptic");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_FRM), "ref. equator");
	DlgItem<QComboBox> (hTab, IDC_FRM)->setCurrentIndex (0);

	ed->ScanCBodyList (hTab, IDC_REF, hRef);

	char cbuf[256];
	sprintf (cbuf, "%0.5f", elmjd);
	oapiSetDlgItemText (hTab, IDC_EDIT7, cbuf);

	Refresh ();
}

char *EditorTab_Elements::HelpTopic ()
{
	return (char*)"/Elements.htm";
}

void EditorTab_Elements::Apply ()
{
	const double eps = 1e-7;
	char cbuf[256];
	int i;
	double mjd;
	oapiGetDlgItemText (hTab, IDC_REF, cbuf, 256);
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
	if (hRef) {
		int frm = DlgItem<QComboBox> (hTab, IDC_FRM)->currentIndex();
		int epc = DlgItem<QComboBox> (hTab, IDC_COMBO6)->currentIndex();
		if (!epc) mjd = 0;
		else {
			oapiGetDlgItemText (hTab, IDC_EDIT7, cbuf, 256);
			sscanf (cbuf, "%lf", &elmjd);
			mjd = (elmjd ? elmjd : 1e-10);
		}
		oapiGetDlgItemText (hTab, IDC_EDIT1, cbuf, 256);
		sscanf (cbuf, "%lf", &el.a);
		i = DlgItem<QComboBox> (hTab, IDC_COMBO1)->currentIndex();
		el.a /= lengthscale[i];
		oapiGetDlgItemText (hTab, IDC_EDIT2, cbuf, 256);
		sscanf (cbuf, "%lf", &el.e);
		if (el.e >= 1 && el.e < 1+eps) el.e = 1+eps; // e=1 causes problems
		oapiGetDlgItemText (hTab, IDC_EDIT3, cbuf, 256);
		sscanf (cbuf, "%lf", &el.i);
		i = DlgItem<QComboBox> (hTab, IDC_COMBO2)->currentIndex();
		el.i /= anglescale[i];
		oapiGetDlgItemText (hTab, IDC_EDIT4, cbuf, 256);
		sscanf (cbuf, "%lf", &el.theta);
		i = DlgItem<QComboBox> (hTab, IDC_COMBO3)->currentIndex();
		el.theta /= anglescale[i];
		oapiGetDlgItemText (hTab, IDC_EDIT5, cbuf, 256);
		sscanf (cbuf, "%lf", &el.omegab);
		i = DlgItem<QComboBox> (hTab, IDC_COMBO4)->currentIndex();
		el.omegab /= anglescale[i];
		oapiGetDlgItemText (hTab, IDC_EDIT6, cbuf, 256);
		sscanf (cbuf, "%lf", &el.L);
		i = DlgItem<QComboBox> (hTab, IDC_COMBO5)->currentIndex();
		el.L /= anglescale[i];
		el.a = fabs (el.a);
		if (el.e > 1.0) el.a = -el.a;
		if (vessel->SetElements (hRef, el, &prm, mjd, frm))
			RefreshSecondaryParams (el, prm);
		else
			QApplication::beep(); // MessageBeep
		Refresh ();
	}
}

void EditorTab_Elements::Refresh ()
{
	char cbuf[256];
	double scale, mjd;
	oapiGetDlgItemText (hTab, IDC_REF, cbuf, 256);
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
	if (!hRef) return;
	int frm = DlgItem<QComboBox> (hTab, IDC_FRM)->currentIndex();
	int epc = DlgItem<QComboBox> (hTab, IDC_COMBO6)->currentIndex();
	if (!epc) mjd = 0;
	else {
		oapiGetDlgItemText (hTab, IDC_EDIT7, cbuf, 256);
		sscanf (cbuf, "%lf", &elmjd);
		mjd = (elmjd ? elmjd : 1e-10);
	}
	if (!vessel->GetElements (hRef, el, &prm, mjd, frm)) return;
	bool closed = (el.e < 1.0);
	int prec = (closed ? 6:10);
	lengthscale[3] = 1.0/oapiGetSize (hRef);
	scale = lengthscale[DlgItem<QComboBox> (hTab, IDC_COMBO1)->currentIndex()];
	sprintf (cbuf, "%0.10g", el.a*scale);
	oapiSetDlgItemText (hTab, IDC_EDIT1, cbuf);
	sprintf (cbuf, "%0.*g", prec, el.e);
	oapiSetDlgItemText (hTab, IDC_EDIT2, cbuf);
	scale = anglescale[DlgItem<QComboBox> (hTab, IDC_COMBO2)->currentIndex()];
	sprintf (cbuf, "%0.*g", prec, el.i*scale);
	oapiSetDlgItemText (hTab, IDC_EDIT3, cbuf);
	scale = anglescale[DlgItem<QComboBox> (hTab, IDC_COMBO3)->currentIndex()];
	sprintf (cbuf, "%0.*g", prec, el.theta*scale);
	oapiSetDlgItemText (hTab, IDC_EDIT4, cbuf);
	scale = anglescale[DlgItem<QComboBox> (hTab, IDC_COMBO4)->currentIndex()];
	sprintf (cbuf, "%0.*g", prec, el.omegab*scale);
	oapiSetDlgItemText (hTab, IDC_EDIT5, cbuf);
	scale = anglescale[DlgItem<QComboBox> (hTab, IDC_COMBO5)->currentIndex()];
	sprintf (cbuf, "%0.*g", prec, el.L*scale);
	oapiSetDlgItemText (hTab, IDC_EDIT6, cbuf);
	RefreshSecondaryParams (el, prm);
}

void EditorTab_Elements::RefreshSecondaryParams (const ELEMENTS &el, const ORBITPARAM &prm)
{
	char cbuf[256];
	bool closed = (el.e < 1.0); // closed orbit?

	sprintf (cbuf, "%g m", prm.PeD); oapiSetDlgItemText (hTab, IDC_PERIAPSIS, cbuf);
	sprintf (cbuf, "%g s", prm.PeT); oapiSetDlgItemText (hTab, IDC_PET, cbuf);
	sprintf (cbuf, "%0.3f °", prm.MnA*DEG); oapiSetDlgItemText (hTab, IDC_MNANM, cbuf);
	sprintf (cbuf, "%0.3f °", prm.TrA*DEG); oapiSetDlgItemText (hTab, IDC_TRANM, cbuf);
	sprintf (cbuf, "%0.3f °", prm.MnL*DEG); oapiSetDlgItemText (hTab, IDC_MNLNG, cbuf);
	sprintf (cbuf, "%0.3f °", prm.TrL*DEG); oapiSetDlgItemText (hTab, IDC_TRLNG, cbuf);
	if (closed) {
		sprintf (cbuf, "%g s", prm.T);   oapiSetDlgItemText (hTab, IDC_PERIOD, cbuf);
		sprintf (cbuf, "%g m", prm.ApD); oapiSetDlgItemText (hTab, IDC_APOAPSIS, cbuf);
		sprintf (cbuf, "%g s", prm.ApT); oapiSetDlgItemText (hTab, IDC_APT, cbuf);
	} else {
		oapiSetDlgItemText (hTab, IDC_PERIOD, "N/A");
		oapiSetDlgItemText (hTab, IDC_APOAPSIS, "N/A");
		oapiSetDlgItemText (hTab, IDC_APT, "N/A");
	}
}
void EditorTab_Elements::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this, hDlg](int id, int code, QWidget *hCtrl) {
		char cbuf[256];
		int i;
		switch (id) {
		case IDC_BACK:
			SwitchTab (3);
			return;
		case IDC_APPLY:
			Apply ();
			return;
		case IDC_REFRESH:
			Refresh ();
			return;
		case IDC_REF:
			if (code == RESN_SELCHANGE || code == RESN_EDITCHANGE) {
				PostCommand (hDlg, IDC_REFRESH);
				return;
			}
			break;
		case IDC_FRM:
			if (code == RESN_SELCHANGE) {
				PostCommand (hDlg, IDC_REFRESH);
				return;
			}
			break;
		case IDC_COMBO1:
			if (code == RESN_SELCHANGE) {
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO1)->currentIndex();
				sprintf (cbuf, "%g", el.a * lengthscale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT1, cbuf);
				return;
			}
			break;
		case IDC_COMBO2:
			if (code == RESN_SELCHANGE) {
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO2)->currentIndex();
				sprintf (cbuf, "%g", el.i * anglescale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT3, cbuf);
				return;
			}
			break;
		case IDC_COMBO3:
			if (code == RESN_SELCHANGE) {
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO3)->currentIndex();
				sprintf (cbuf, "%g", el.theta * anglescale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT4, cbuf);
				return;
			}
			break;
		case IDC_COMBO4:
			if (code == RESN_SELCHANGE) {
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO4)->currentIndex();
				sprintf (cbuf, "%g", el.omegab * anglescale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT5, cbuf);
				return;
			}
			break;
		case IDC_COMBO5:
			if (code == RESN_SELCHANGE) {
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO5)->currentIndex();
				sprintf (cbuf, "%g", el.L * anglescale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT6, cbuf);
				return;
			}
			break;
		case IDC_COMBO6:
			if (code == RESN_SELCHANGE) {
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO6)->currentIndex();
				oapiResDlgItem (hDlg, IDC_EDIT7)->setEnabled (i != 0);
				return;
			}
			break;
		}
	});
	// WM_NOTIFY
	oapiConnectDlgDeltaPos (hDlg, [this, hDlg](int idFrom, int iPos, int iDelta) { // UDN_DELTAPOS
			char cbuf[256];
			int i;
			switch (idFrom) {
			case IDC_SPIN1:
				el.a *= (1.0 - iDelta*1e-4);
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO1)->currentIndex();
				sprintf (cbuf, "%g", el.a * lengthscale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT1, cbuf);
				Apply ();
				return;
			case IDC_SPIN2:
				el.e *= (1.0 - iDelta*0.001);
				if (el.e < 0.0) el.e = 0.0;
				sprintf (cbuf, "%g", el.e);
				oapiSetDlgItemText (hDlg, IDC_EDIT2, cbuf);
				Apply ();
				return;
			case IDC_SPIN3:
				el.i -= iDelta*RAD*0.1;
				if      (el.i >  PI) el.i -= 2.0*PI;
				else if (el.i < -PI) el.i += 2.0*PI;
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO2)->currentIndex();
				sprintf (cbuf, "%g", el.i * anglescale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT3, cbuf);
				Apply ();
				return;
			case IDC_SPIN4:
				el.theta -= iDelta*RAD*0.1;
				if      (el.theta >= 2.0*PI) el.theta -= 2.0*PI;
				else if (el.theta <  0.0)    el.theta += 2.0*PI;
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO3)->currentIndex();
				sprintf (cbuf, "%g", el.theta * anglescale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT4, cbuf);
				Apply ();
				return;
			case IDC_SPIN5:
				el.omegab -= iDelta*RAD*0.1;
				if      (el.omegab >= 2.0*PI) el.omegab -= 2.0*PI;
				else if (el.omegab <  0.0)    el.omegab += 2.0*PI;
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO4)->currentIndex();
				sprintf (cbuf, "%g", el.omegab * anglescale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT5, cbuf);
				Apply ();
				return;
			case IDC_SPIN6:
				el.L -= iDelta*RAD*0.1;
				if      (el.L >= 2.0*PI) el.L -= 2.0*PI;
				else if (el.L <  0.0)    el.L += 2.0*PI;
				i = DlgItem<QComboBox> (hDlg, IDC_COMBO5)->currentIndex();
				sprintf (cbuf, "%g", el.L * anglescale[i]);
				oapiSetDlgItemText (hDlg, IDC_EDIT6, cbuf);
				Apply ();
				return;
			}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Elements::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Elements *pTab = (EditorTab_Elements*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}

// ==============================================================
// EditorTab_Statevec class definition
// ==============================================================

EditorTab_Statevec::EditorTab_Statevec (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_EDIT3, EditorTab_Statevec::DlgProc);
}

void EditorTab_Statevec::InitTab ()
{
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	OBJHANDLE hRef = vessel->GetGravityRef();

	DlgItem<QComboBox> (hTab, IDC_FRM)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_FRM), "ecliptic");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_FRM), "ref. equator (fixed)");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_FRM), "ref. equator (rotating)");
	DlgItem<QComboBox> (hTab, IDC_FRM)->setCurrentIndex (0);

	DlgItem<QComboBox> (hTab, IDC_CRD)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_CRD), "cartesian");
	oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_CRD), "polar");
	DlgItem<QComboBox> (hTab, IDC_CRD)->setCurrentIndex (0);

	DlgLabels ();
	ed->ScanCBodyList (hTab, IDC_REF, hRef);
	ScanVesselList ();
	Refresh ();
}

void EditorTab_Statevec::ScanVesselList ()
{
	char cbuf[256];

	// populate vessel list
	QSignalBlocker block (DlgItem<QListWidget> (hTab, IDC_STATECPY)); // LB_ messages don't notify
	DlgItem<QListWidget> (hTab, IDC_STATECPY)->clear();
	for (DWORD i = 0; i < oapiGetVesselCount(); i++) {
		OBJHANDLE hV = oapiGetVesselByIndex (i);
		if (hV == ed->hVessel) continue;                  // skip myself
		VESSEL *vessel = oapiGetVesselInterface (hV);
		if ((vessel->GetFlightStatus() & 1) == 1) continue; // skip landed vessels
		strcpy (cbuf, vessel->GetName());
		DlgItem<QListWidget> (hTab, IDC_STATECPY)->addItem (QString::fromUtf8 (cbuf));
	}
}

char *EditorTab_Statevec::HelpTopic ()
{
	return (char*)"/Statevec.htm";
}

void EditorTab_Statevec::DlgLabels ()
{
	int crd = DlgItem<QComboBox> (hTab, IDC_CRD)->currentIndex();
	oapiSetDlgItemText (hTab, IDC_STATIC1A, crd ? "radius" : "x");
	oapiSetDlgItemText (hTab, IDC_STATIC2A, crd ? "longitude" : "y");
	oapiSetDlgItemText (hTab, IDC_STATIC3A, crd ? "latitude" : "z");
	oapiSetDlgItemText (hTab, IDC_STATIC4A, crd ? "d radius / dt" : "dx / dt");
	oapiSetDlgItemText (hTab, IDC_STATIC5A, crd ? "d longitude / dt" : "dy / dt");
	oapiSetDlgItemText (hTab, IDC_STATIC6A, crd ? "d latitude / dt" : "dz / dt");
	oapiSetDlgItemText (hTab, IDC_STATIC2, crd ? "deg" : "m");
	oapiSetDlgItemText (hTab, IDC_STATIC3, crd ? "deg" : "m");
	oapiSetDlgItemText (hTab, IDC_STATIC5, crd ? "deg/s" : "m/s");
	oapiSetDlgItemText (hTab, IDC_STATIC6, crd ? "deg/s" : "m/s");
}

void EditorTab_Statevec::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this, hDlg](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (3);
			return;
		case IDC_APPLY:
			Apply ();
			return;
		case IDC_REFRESH:
			Refresh ();
			return;
		case IDC_REF:
			if (code == RESN_SELCHANGE || code == RESN_EDITCHANGE) {
				PostCommand (hDlg, IDC_REFRESH);
				return;
			}
			break;
		case IDC_FRM:
			if (code == RESN_SELCHANGE) {
				PostCommand (hDlg, IDC_REFRESH);
				return;
			}
			break;
		case IDC_CRD:
			if (code == RESN_SELCHANGE) {
				DlgLabels ();
				PostCommand (hDlg, IDC_REFRESH);
				return;
			}
			break;
		case IDC_STATECPY: {
			OBJHANDLE hV = GetVesselFromList (IDC_STATECPY);
			switch (code) {
			case RESN_SELCHANGE:
				Refresh (hV);
				break;
			case RESN_DBLCLK:
				Refresh (hV);
				Apply ();
				break;
			}
			} break;
		}
	});
	// WM_NOTIFY
	oapiConnectDlgDeltaPos (hDlg, [this, hDlg](int idFrom, int iPos, int iDelta) { // UDN_DELTAPOS
			char cbuf[256];
			int crd = DlgItem<QComboBox> (hTab, IDC_CRD)->currentIndex();
			int prec, idx = 0;
			double val, dv;
			switch (idFrom) {
			case IDC_SPIN1:
				idx = IDC_EDIT1; prec = 1; dv = -iDelta*1.0;
				break;
			case IDC_SPIN1A:
				idx = IDC_EDIT1; prec = 1; dv = -iDelta*1000.0;
				break;
			case IDC_SPIN2:
				idx = IDC_EDIT2; prec = (crd?6:1); dv = -iDelta*(crd?0.0001:1.0);
				break;
			case IDC_SPIN2A:
				idx = IDC_EDIT2; prec = (crd?6:1); dv = -iDelta*(crd?0.1:1000.0);
				break;
			case IDC_SPIN3:
				idx = IDC_EDIT3; prec = (crd?6:1); dv = -iDelta*(crd?0.0001:1.0);
				break;
			case IDC_SPIN3A:
				idx = IDC_EDIT3; prec = (crd?6:1); dv = -iDelta*(crd?0.1:1000.0);
				break;
			case IDC_SPIN4:
				idx = IDC_EDIT4; prec = 2; dv = -iDelta*0.1;
				break;
			case IDC_SPIN4A:
				idx = IDC_EDIT4; prec = 2; dv = -iDelta*100.0;
				break;
			case IDC_SPIN5:
				idx = IDC_EDIT5; prec = (crd?7:2); dv = -iDelta*(crd?1e-5:0.1);
				break;
			case IDC_SPIN5A:
				idx = IDC_EDIT5; prec = (crd?7:2); dv = -iDelta*(crd?1e-2:100.0);
				break;
			case IDC_SPIN6:
				idx = IDC_EDIT6; prec = (crd?7:2); dv = -iDelta*(crd?1e-5:0.1);
				break;
			case IDC_SPIN6A:
				idx = IDC_EDIT6; prec = (crd?7:2); dv = -iDelta*(crd?1e-2:100.0);
				break;
			}
			if (idx) {
				oapiGetDlgItemText (hDlg, idx, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val += dv;
				sprintf (cbuf, "%0.*f", prec, val);
				oapiSetDlgItemText (hDlg, idx, cbuf);
				Apply ();
				return;
			}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Statevec::Refresh (OBJHANDLE hV)
{
	if (!hV) hV = ed->hVessel;
	char cbuf[256];
	oapiGetDlgItemText (hTab, IDC_REF, cbuf, 256);
	VESSEL *vessel = oapiGetVesselInterface (hV);
	OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
	if (!hRef) return;
	int frm = DlgItem<QComboBox> (hTab, IDC_FRM)->currentIndex();
	int crd = DlgItem<QComboBox> (hTab, IDC_CRD)->currentIndex();
	VECTOR3 pos, vel;
	oapiGetRelativePos (hV, hRef, &pos);
	oapiGetRelativeVel (hV, hRef, &vel);
	// map ecliptic -> equatorial frame
	if (frm) {
		MATRIX3 rot;
		if (frm == 1) oapiGetPlanetObliquityMatrix (hRef, &rot);
		else          oapiGetRotationMatrix (hRef, &rot);
		pos = tmul (rot, pos);
		vel = tmul (rot, vel);
	}
	// map cartesian -> polar coordinates
	if (crd) {
		Crt2Pol (pos, vel);
		pos.data[1] *= DEG; pos.data[2] *= DEG;
		vel.data[1] *= DEG; vel.data[2] *= DEG;
	}
	// in the rotating reference frame we need to subtract the angular
	// velocity of the planet
	if (frm == 2) {
		double T = oapiGetPlanetPeriod (hRef);
		if (crd) {
			vel.data[1] -= 360.0/T;
		} else { // map back to cartesian
			double r   = std::hypot (pos.x, pos.z);
			double phi = atan2 (pos.z, pos.x);
			double v   = 2.0*PI*r/T;
			vel.x     += v*sin(phi);
			vel.z     -= v*cos(phi);
		}
	}
	sprintf (cbuf, "%0.1f", pos.x); oapiSetDlgItemText (hTab, IDC_EDIT1, cbuf);
	sprintf (cbuf, "%0.*f", (crd?6:1), pos.y); oapiSetDlgItemText (hTab, IDC_EDIT2, cbuf);
	sprintf (cbuf, "%0.*f", (crd?6:1), pos.z); oapiSetDlgItemText (hTab, IDC_EDIT3, cbuf);
	sprintf (cbuf, "%0.2f", vel.x); oapiSetDlgItemText (hTab, IDC_EDIT4, cbuf);
	sprintf (cbuf, "%0.*f", (crd?7:2), vel.y); oapiSetDlgItemText (hTab, IDC_EDIT5, cbuf);
	sprintf (cbuf, "%0.*f", (crd?7:2), vel.z); oapiSetDlgItemText (hTab, IDC_EDIT6, cbuf);
}

void EditorTab_Statevec::Apply ()
{
	char cbuf[256];
	oapiGetDlgItemText (hTab, IDC_REF, cbuf, 256);
	VESSEL *vessel = Vessel();
	OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
	if (hRef) {
		bool needrefresh = false;
		MATRIX3 rot;
		VECTOR3 pos, vel, refpos, refvel;
		VESSELSTATUS vs;
		oapiGetDlgItemText (hTab, IDC_EDIT1, cbuf, 256);
		sscanf (cbuf, "%lf", &pos.x);
		oapiGetDlgItemText (hTab, IDC_EDIT2, cbuf, 256);
		sscanf (cbuf, "%lf", &pos.y);
		oapiGetDlgItemText (hTab, IDC_EDIT3, cbuf, 256);
		sscanf (cbuf, "%lf", &pos.z);
		oapiGetDlgItemText (hTab, IDC_EDIT4, cbuf, 256);
		sscanf (cbuf, "%lf", &vel.x);
		oapiGetDlgItemText (hTab, IDC_EDIT5, cbuf, 256);
		sscanf (cbuf, "%lf", &vel.y);
		oapiGetDlgItemText (hTab, IDC_EDIT6, cbuf, 256);
		sscanf (cbuf, "%lf", &vel.z);
		int frm = DlgItem<QComboBox> (hTab, IDC_FRM)->currentIndex();
		int crd = DlgItem<QComboBox> (hTab, IDC_CRD)->currentIndex();
		// in the rotating reference frame we need to add the angular
		// velocity of the planet
		if (frm == 2) {
			double T = oapiGetPlanetPeriod (hRef);
			if (crd) {
				vel.data[1] += 360.0/T;
			} else { // map back to cartesian
				double r   = std::hypot (pos.x, pos.z);
				double phi = atan2 (pos.z, pos.x);
				double v   = 2.0*PI*r/T;
				vel.x     -= v*sin(phi);
				vel.z     += v*cos(phi);
			}
		}
		// map polar -> cartesian coordinates
		if (crd) {
			pos.data[1] *= RAD, pos.data[2] *= RAD;
			vel.data[1] *= RAD, vel.data[2] *= RAD;
			Pol2Crt (pos, vel);
		}
		// map from celestial/equatorial frame of reference
		if (frm) {
			if (frm == 1) oapiGetPlanetObliquityMatrix (hRef, &rot);
			else          oapiGetRotationMatrix (hRef, &rot);
			pos = mul (rot, pos);
			vel = mul (rot, vel);
		}
		// change reference in case the selected reference object is
		// not the same as the VESSELSTATUS reference
		vessel->GetStatus (vs);
		oapiGetGlobalPos (hRef, &refpos);     pos += refpos;
		oapiGetGlobalVel (hRef, &refvel);     vel += refvel;
		oapiGetGlobalPos (vs.rbody, &refpos); pos -= refpos;
		oapiGetGlobalVel (vs.rbody, &refvel); vel -= refvel;
		if (vs.status != 0) { // enforce freeflight mode
			vs.status = 0;
			vessel->GetRotationMatrix (rot);
			vs.arot.x = atan2(rot.m23, rot.m33);
			vs.arot.y = -asin(rot.m13);
			vs.arot.z = atan2(rot.m12, rot.m11);
			vessel->GetAngularVel(vs.vrot);
		}
#ifdef UNDEF
		// sanity check
		double rad = length(pos);
		double rad0 = oapiGetSize (vs.rbody) + vessel->GetCOG_elev();
		if (rad < rad0) {
			Crt2Pol (pos, vel);
			pos.x = rad0;
			vel.x = 0.0;
			Pol2Crt (pos, vel);
			needrefresh = true;
		}
#endif
		veccpy (vs.rpos, pos);
		veccpy (vs.rvel, vel);
		vessel->DefSetState (&vs);
		if (needrefresh) Refresh();
	}
}

void EditorTab_Statevec::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Statevec *pTab = (EditorTab_Statevec*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}

// ==============================================================
// EditorTab_Landed class definition
// ==============================================================

EditorTab_Landed::EditorTab_Landed (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_EDIT4, EditorTab_Landed::DlgProc);
}

void EditorTab_Landed::InitTab ()
{
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	OBJHANDLE hRef = vessel->GetGravityRef();

	ScanCBodyList (hTab, IDC_REF, hRef);
	ScanBaseList (hTab, IDC_BASE, hRef);
	ScanVesselList ();
	Refresh ();
}

void EditorTab_Landed::ScanVesselList ()
{
	char cbuf[256];

	// populate vessel list
	QSignalBlocker block (DlgItem<QListWidget> (hTab, IDC_STATECPY)); // LB_ messages don't notify
	DlgItem<QListWidget> (hTab, IDC_STATECPY)->clear();
	for (DWORD i = 0; i < oapiGetVesselCount(); i++) {
		OBJHANDLE hV = oapiGetVesselByIndex (i);
		if (hV == ed->hVessel) continue;                  // skip myself
		VESSEL *vessel = oapiGetVesselInterface (hV);
		if ((vessel->GetFlightStatus() & 1) == 0) continue; // skip vessels in flight
		strcpy (cbuf, vessel->GetName());
		DlgItem<QListWidget> (hTab, IDC_STATECPY)->addItem (QString::fromUtf8 (cbuf));
	}
}

char *EditorTab_Landed::HelpTopic ()
{
	return (char*)"/Location.htm";
}

void EditorTab_Landed::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this, hDlg](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (3);
			return;
		case IDC_APPLY:
			Apply ();
			return;
		case IDC_REFRESH:
			if (hCtrl == (QWidget*)1) { // rescan bases (lParam == 1)
				char cbuf[256];
				oapiGetDlgItemText (hDlg, IDC_REF, cbuf, 256);
				OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
				ScanBaseList (hDlg, IDC_BASE, hRef);
			}
			Refresh ();
			return;
		case IDC_REF:
			if (code == RESN_SELCHANGE || code == RESN_EDITCHANGE) {
				QCoreApplication::postEvent (hDlg, new QEvent (QEvent::Type (QEvent::User+0))); // PostMessage WM_USER+0
				return;
			}
		case IDC_BASE:
			if (code == RESN_SELCHANGE || code == RESN_EDITCHANGE) {
				QCoreApplication::postEvent (hDlg, new QEvent (QEvent::Type (QEvent::User+1))); // PostMessage WM_USER+1
				return;
			}
			break;
		case IDC_PAD:
			if (code == RESN_SELCHANGE || code == RESN_EDITCHANGE) {
				ed->SetBasePosition (hDlg);
				PostCommand (hDlg, IDC_APPLY);
				return;
			}
			break;
		case IDC_STATECPY: {
			OBJHANDLE hV = GetVesselFromList (IDC_STATECPY);
			switch (code) {
			case RESN_SELCHANGE:
				Refresh (hV);
				break;
			case RESN_DBLCLK:
				Refresh (hV);
				Apply ();
				break;
			}
			} break;
		}
	});
	// WM_USER+0, WM_USER+1: posted as Qt user events
	new DlgEvents (hDlg, [this, hDlg](QEvent *e) -> bool {
		switch ((int)e->type()) {
		case QEvent::User+0: { // reference body changed
			char cbuf[256];
			oapiGetDlgItemText (hDlg, IDC_REF, cbuf, 256);
			OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
			if (!hRef) break;
			ScanBaseList (hDlg, IDC_BASE, hRef);
			QCoreApplication::postEvent (hDlg, new QEvent (QEvent::Type (QEvent::User+1))); // PostMessage WM_USER+1
			} return true;
		case QEvent::User+1: { // base changed
			char cbuf[256];
			oapiGetDlgItemText (hDlg, IDC_REF, cbuf, 256);
			OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
			if (!hRef) break;
			oapiGetDlgItemText (hDlg, IDC_BASE, cbuf, 256);
			OBJHANDLE hBase = oapiGetBaseByName (hRef, cbuf);
			ed->ScanPadList (hDlg, IDC_PAD, hBase);
			ed->SetBasePosition (hDlg);
			PostCommand (hDlg, IDC_APPLY);
			} return true;
		}
		return false;
	});
	// WM_NOTIFY
	oapiConnectDlgDeltaPos (hDlg, [this, hDlg](int idFrom, int iPos, int iDelta) { // UDN_DELTAPOS
			char cbuf[256];
			double val;
			int id = idFrom;
			switch (id) {
			case IDC_SPIN1:
			case IDC_SPIN1A:
				oapiGetDlgItemText (hDlg, IDC_EDIT1, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN1 ? 0.00001 : 0.001);
				if      (val < -180.0) val += 360.0;
				else if (val > +180.0) val -= 360.0;
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT1, cbuf);
				Apply ();
				return;
			case IDC_SPIN2:
			case IDC_SPIN2A:
				oapiGetDlgItemText (hDlg, IDC_EDIT2, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN2 ? 0.00001 : 0.001);
				val = min (90.0, max (-90.0, val));
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT2, cbuf);
				Apply ();
				return;
			case IDC_SPIN3:
			case IDC_SPIN3A:
				oapiGetDlgItemText (hDlg, IDC_EDIT3, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN3 ? 0.01 : 1.0);
				if      (val <   0.0) val += 360.0;
				else if (val > 360.0) val -= 360.0;
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT3, cbuf);
				Apply ();
				return;
			}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Landed::Refresh (OBJHANDLE hV)
{
	char cbuf[256], lngstr[64], latstr[64];
	bool scancbody = ((hV != NULL) && (hV != ed->hVessel));
	if (!hV) hV = ed->hVessel;
	VESSEL *vessel = oapiGetVesselInterface (hV);
	if (scancbody) SelectCBody (vessel->GetSurfaceRef());
	oapiGetDlgItemText (hTab, IDC_REF, cbuf, 256);
	OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
	if (!hRef) return;

	VESSELSTATUS2 vs;
	memset (&vs, 0, sizeof(vs)); vs.version = 2;
	vessel->GetStatusEx (&vs);
	if (vs.rbody == hRef) {
		sprintf (lngstr, "%lf", vs.surf_lng * DEG);
		sprintf (latstr, "%lf", vs.surf_lat * DEG);
		ed->SelectBase (hTab, IDC_BASE, hRef, vs.base);
	} else {
		// calculate ground position
		VECTOR3 pos;
		MATRIX3 rot;
		oapiGetRelativePos (ed->hVessel, hRef, &pos);
		oapiGetRotationMatrix (hRef, &rot);
		pos = tmul (rot, pos);
		VECTOR3 vel = _V(0,0,0);
		Crt2Pol (pos, vel);
		sprintf (lngstr, "%lf", pos.data[1] * DEG);
		sprintf (latstr, "%lf", pos.data[2] * DEG);
	}
	oapiSetDlgItemText (hTab, IDC_EDIT1, lngstr);
	oapiSetDlgItemText (hTab, IDC_EDIT2, latstr);

	sprintf (cbuf, "%lf", vs.surf_hdg * DEG);
	oapiSetDlgItemText (hTab, IDC_EDIT3, cbuf);
}

void EditorTab_Landed::Apply ()
{
	char cbuf[256];
	oapiGetDlgItemText (hTab, IDC_REF, cbuf, 256);
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	OBJHANDLE hRef = oapiGetGbodyByName (cbuf);
	if (!hRef) return;
	VESSELSTATUS2 vs;
	memset (&vs, 0, sizeof(vs));
	vs.version = 2;
	vs.rbody = hRef;
	vs.status = 1; // landed
	vs.arot.x = 10; // use default touchdown orientation
	oapiGetDlgItemText (hTab, IDC_EDIT1, cbuf, 256);
	sscanf (cbuf, "%lf", &vs.surf_lng); vs.surf_lng *= RAD;
	oapiGetDlgItemText (hTab, IDC_EDIT2, cbuf, 256);
	sscanf (cbuf, "%lf", &vs.surf_lat); vs.surf_lat *= RAD;
	oapiGetDlgItemText (hTab, IDC_EDIT3, cbuf, 256);
	sscanf (cbuf, "%lf", &vs.surf_hdg); vs.surf_hdg *= RAD;
	vessel->DefSetStateEx (&vs);
}

void EditorTab_Landed::ScanCBodyList (QWidget *hDlg, int hList, OBJHANDLE hSelect)
{
	// populate a list of celestial bodies
	char cbuf[256];
	QSignalBlocker block (DlgItem<QComboBox> (hDlg, hList)); // CB_ messages don't notify
	DlgItem<QComboBox> (hDlg, hList)->clear();
	for (DWORD n = 0; n < oapiGetGbodyCount(); n++) {
		OBJHANDLE hBody = oapiGetGbodyByIndex (n);
		if (oapiGetObjectType (hBody) == OBJTP_STAR) continue; // skip stars
		oapiGetObjectName (hBody, cbuf, 256);
		oapiComboAddString (DlgItem<QComboBox> (hDlg, hList), cbuf);
	}
	// select the requested body
	oapiGetObjectName (hSelect, cbuf, 256);
	ComboSelectString (DlgItem<QComboBox> (hDlg, hList), cbuf);
}

void EditorTab_Landed::ScanBaseList (QWidget *hDlg, int hList, OBJHANDLE hRef)
{
	char cbuf[256];
	DWORD n;

	DlgItem<QComboBox> (hDlg, hList)->clear();
	for (n = 0; n < oapiGetBaseCount (hRef); n++) {
		oapiGetObjectName (oapiGetBaseByIndex (hRef, n), cbuf, 256);
		oapiComboAddString (DlgItem<QComboBox> (hDlg, hList), cbuf);
	}
}

void EditorTab_Landed::SelectCBody (OBJHANDLE hBody)
{
	char cbuf[256];
	oapiGetObjectName (hBody, cbuf, 256);
	int idx = ComboFindString (DlgItem<QComboBox> (hTab, IDC_REF), cbuf);
	if (idx != LB_ERR) {
		QSignalBlocker block (DlgItem<QComboBox> (hTab, IDC_REF)); // CB_SETCURSEL doesn't notify
		DlgItem<QComboBox> (hTab, IDC_REF)->setCurrentIndex (idx);
		QCoreApplication::postEvent (hTab, new QEvent (QEvent::Type (QEvent::User+0))); // PostMessage WM_USER+0
	}
}

void EditorTab_Landed::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Landed *pTab = (EditorTab_Landed*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}


// ==============================================================
// EditorTab_Orientation class definition
// ==============================================================

EditorTab_Orientation::EditorTab_Orientation (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_EDIT5, EditorTab_Orientation::DlgProc);
}

void EditorTab_Orientation::InitTab ()
{
	Refresh();
}

char *EditorTab_Orientation::HelpTopic ()
{
	return (char*)"/Orientation.htm";
}

void EditorTab_Orientation::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (3);
			return;
		case IDC_REFRESH:
			Refresh ();
			return;
		case IDC_APPLY:
			Apply ();
			return;
		}
	});
	// WM_NOTIFY
	oapiConnectDlgDeltaPos (hDlg, [this, hDlg](int idFrom, int iPos, int iDelta) { // UDN_DELTAPOS
			char cbuf[256];
			double val;
			int id = idFrom;
			switch (id) {
			case IDC_SPIN1:
			case IDC_SPIN1A:
				oapiGetDlgItemText (hDlg, IDC_EDIT1, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN1 ? 0.001 : 0.1);
				if      (val < -180.0) val += 360.0;
				else if (val > +180.0) val -= 360.0;
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT1, cbuf);
				Apply ();
				return;
			case IDC_SPIN2:
			case IDC_SPIN2A:
				oapiGetDlgItemText (hDlg, IDC_EDIT2, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN2 ? 0.001 : 0.1);
				val = min (90.0, max (-90.0, val));
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT2, cbuf);
				Apply ();
				return;
			case IDC_SPIN3:
			case IDC_SPIN3A:
				oapiGetDlgItemText (hDlg, IDC_EDIT3, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN3 ? 0.001 : 0.1);
				if      (val <   0.0) val += 360.0;
				else if (val > 360.0) val -= 360.0;
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT3, cbuf);
				Apply ();
				return;
			case IDC_SPIN4:
			case IDC_SPIN5:
			case IDC_SPIN6:
				Rotate (id-IDC_SPIN4, iDelta * -0.005);
				return;
			}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Orientation::Refresh ()
{
	int i;
	char cbuf[256];
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	VECTOR3 arot;
	vessel->GetGlobalOrientation (arot);
	for (i = 0; i < 3; i++) {
		sprintf (cbuf, "%lf", arot.data[i] * DEG);
		oapiSetDlgItemText (hTab, IDC_EDIT1+i, cbuf);
	}
}

void EditorTab_Orientation::Apply ()
{
	int i;
	char cbuf[256];
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	VECTOR3 arot;
	for (i = 0; i < 3; i++) {
		oapiGetDlgItemText (hTab, IDC_EDIT1+i, cbuf, 256);
		sscanf (cbuf, "%lf", &arot.data[i]);
		arot.data[i] *= RAD;
	}
	vessel->SetGlobalOrientation (arot);
}

void EditorTab_Orientation::ApplyAngularVel ()
{
	int i;
	char cbuf[256];
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	VECTOR3 avel;
	for (i = 0; i < 3; i++) {
		oapiGetDlgItemText (hTab, IDC_EDIT4+i, cbuf, 256);
		sscanf (cbuf, "%lf", &avel.data[i]);
		avel.data[i] *= RAD;
	}
	vessel->SetAngularVel (avel);
}

void EditorTab_Orientation::Rotate (int axis, double da)
{
	MATRIX3 R, R2;
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	vessel->GetRotationMatrix (R);
	double sina = sin(da), cosa = cos(da);
	switch (axis) {
	case 0: // pitch
		R2 = _M(1,0,0,  0,cosa,sina,  0,-sina,cosa);
		break;
	case 1: // yaw
		R2 = _M(cosa,0,sina,  0,1,0,  -sina,0,cosa);
		break;
	case 2: // bank
		R2 = _M(cosa,sina,0,  -sina,cosa,0,  0,0,1);
		break;
	}
	vessel->SetRotationMatrix (mul (R,R2));
	Refresh ();
}

void EditorTab_Orientation::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Orientation *pTab = (EditorTab_Orientation*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}

// ==============================================================
// EditorTab_AngularVel class definition
// ==============================================================

EditorTab_AngularVel::EditorTab_AngularVel (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_EDIT6, EditorTab_AngularVel::DlgProc);
}

void EditorTab_AngularVel::InitTab ()
{
	Refresh();
}

char *EditorTab_AngularVel::HelpTopic ()
{
	return (char*)"/AngularVel.htm";
}

void EditorTab_AngularVel::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (3);
			return;
		case IDC_REFRESH:
			Refresh ();
			return;
		case IDC_APPLY:
			Apply ();
			return;
		case IDC_KILLROT:
			Killrot ();
			return;
		}
	});
	// WM_NOTIFY
	oapiConnectDlgDeltaPos (hDlg, [this, hDlg](int idFrom, int iPos, int iDelta) { // UDN_DELTAPOS
			char cbuf[256];
			double val;
			int id = idFrom;
			switch (id) {
			case IDC_SPIN1:
			case IDC_SPIN1A:
				oapiGetDlgItemText (hDlg, IDC_EDIT1, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN4 ? 0.001 : 0.1);
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT1, cbuf);
				Apply ();
				return;
			case IDC_SPIN2:
			case IDC_SPIN2A:
				oapiGetDlgItemText (hDlg, IDC_EDIT2, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN2 ? 0.001 : 0.1);
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT2, cbuf);
				Apply ();
				return;
			case IDC_SPIN3:
			case IDC_SPIN3A:
				oapiGetDlgItemText (hDlg, IDC_EDIT3, cbuf, 256);
				sscanf (cbuf, "%lf", &val);
				val -= iDelta * (id == IDC_SPIN3 ? 0.001 : 0.1);
				sprintf (cbuf, "%lf", val);
				oapiSetDlgItemText (hDlg, IDC_EDIT3, cbuf);
				Apply ();
				return;
			}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_AngularVel::Refresh ()
{
	char cbuf[256];
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	VECTOR3 avel;
	vessel->GetAngularVel (avel);
	for (int i = 0; i < 3; i++) {
		sprintf (cbuf, "%lf", avel.data[i] * DEG);
		oapiSetDlgItemText (hTab, IDC_EDIT1+i, cbuf);
	}
}

void EditorTab_AngularVel::Apply ()
{
	char cbuf[256];
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	VECTOR3 avel;
	for (int i = 0; i < 3; i++) {
		oapiGetDlgItemText (hTab, IDC_EDIT1+i, cbuf, 256);
		sscanf (cbuf, "%lf", &avel.data[i]);
		avel.data[i] *= RAD;
	}
	vessel->SetAngularVel (avel);
}

void EditorTab_AngularVel::Killrot ()
{
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	VECTOR3 avel;
	for (int i = 0; i < 3; i++) {
		avel.data[i] = 0.0;
		oapiSetDlgItemText (hTab, IDC_EDIT1+i, "0");
	}
	vessel->SetAngularVel (avel);

}

void EditorTab_AngularVel::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_AngularVel *pTab = (EditorTab_AngularVel*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}


// ==============================================================
// EditorTab_Propellant class definition
// ==============================================================

EditorTab_Propellant::EditorTab_Propellant (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_EDIT7, EditorTab_Propellant::DlgProc);
}

void EditorTab_Propellant::InitTab ()
{
	GAUGEPARAM gp = { 0, 100, GAUGEPARAM::LEFT, GAUGEPARAM::BLACK };
	oapiSetGaugeParams (oapiResDlgItem (hTab, IDC_PROPLEVEL), &gp);
	lastedit = IDC_EDIT2;
	Refresh();
}

char *EditorTab_Propellant::HelpTopic ()
{
	return (char*)"/Propellant.htm";
}

void EditorTab_Propellant::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (3);
			return;
		case IDC_REFRESH:
			Refresh ();
			return;
		case IDC_APPLY:
			Apply ();
			return;
		case IDC_EMPTY:
			SetLevel (0);
			return;
		case IDC_FULL:
			SetLevel (1);
			return;
		case IDC_EMPTYALL:
			SetLevel (0, true);
			return;
		case IDC_FULLALL:
			SetLevel (1, true);
			return;
		case IDC_EDIT2:
		case IDC_EDIT3:
			if (code == RESN_CHANGE)
				lastedit = id;
			return;
		}
	});
	// WM_NOTIFY
	oapiConnectDlgDeltaPos (hDlg, [this, hDlg](int idFrom, int iPos, int iDelta) { // UDN_DELTAPOS
			char cbuf[256];
			DWORD i, n;
			int id = idFrom;
			switch (id) {
			case IDC_SPIN1:
				oapiGetDlgItemText (hDlg, IDC_EDIT1, cbuf, 256);
				i = sscanf (cbuf, "%d", &n);
				if (!i || !n || n > ntank) n = 1;
				n += (iDelta < 0 ? 1 : -1);
				n = max ((DWORD)1, min (ntank, n));
				sprintf (cbuf, "%d", n);
				oapiSetDlgItemText (hDlg, IDC_EDIT1, cbuf);
				Refresh ();
				return;
			}
	});
	// WM_HSCROLL: IDC_PROPLEVEL
	QObject::connect (DlgItem<GaugeCtrl> (hDlg, IDC_PROPLEVEL), &GaugeCtrl::scrolled, hDlg, [this](int request, int pos) {
			switch (request) {
			case GAUGE_THUMBTRACK: // SB_THUMBTRACK
			case GAUGE_LINEDEC:    // SB_LINELEFT
			case GAUGE_LINEINC:    // SB_LINERIGHT
				SetLevel (pos*0.01);
				return;
			}
	});
	ScnEditorTab::TabProc (hDlg);
}

void EditorTab_Propellant::Refresh ()
{
	int i;
	DWORD n;
	double m, m0;
	char cbuf[256];
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	ntank = vessel->GetPropellantCount();
	sprintf (cbuf, "of %d", ntank);
	oapiSetDlgItemText (hTab, IDC_STATIC1, cbuf);
	oapiGetDlgItemText (hTab, IDC_EDIT1, cbuf, 256);
	i = sscanf (cbuf, "%d", &n);
	if (i != 1 || n > ntank) {
		oapiSetDlgItemText (hTab, IDC_EDIT1, "1");
		n = 0;
	} else n--;
	PROPELLANT_HANDLE hP = vessel->GetPropellantHandleByIndex (n);
	if (!hP) return;
	m0 = vessel->GetPropellantMaxMass (hP);
	m  = vessel->GetPropellantMass (hP);
	sprintf (cbuf, "Mass (0-%0.2f kg)", m0);
	oapiSetDlgItemText (hTab, IDC_STATIC2, cbuf);
	sprintf (cbuf, "%0.4f", m/m0);
	oapiSetDlgItemText (hTab, IDC_EDIT2, cbuf);
	sprintf (cbuf, "%0.2f", m);
	oapiSetDlgItemText (hTab, IDC_EDIT3, cbuf);
	oapiSetGaugePos (oapiResDlgItem (hTab, IDC_PROPLEVEL), (int)(m/m0*100+0.5));
	RefreshTotals();
}

void EditorTab_Propellant::RefreshTotals ()
{
	double m;
	char cbuf[256];
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	m = vessel->GetTotalPropellantMass ();
	sprintf (cbuf, "%0.2f kg", m);
	oapiSetDlgItemText (hTab, IDC_STATIC3, cbuf);
	m = vessel->GetMass ();
	sprintf (cbuf, "%0.2f kg", m);
	oapiSetDlgItemText (hTab, IDC_STATIC4, cbuf);
}

void EditorTab_Propellant::Apply ()
{
	char cbuf[256];
	double level;
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	if (lastedit == IDC_EDIT2) {
		oapiGetDlgItemText (hTab, IDC_EDIT2, cbuf, 256);
		sscanf (cbuf, "%lf", &level);
		level = max (0.0, min (1.0, level));
	} else {
		VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
		oapiGetDlgItemText (hTab, IDC_EDIT1, cbuf, 256);
		DWORD n, i = sscanf (cbuf, "%d", &n);
		if (!i || --n >= ntank) return;
		PROPELLANT_HANDLE hP = vessel->GetPropellantHandleByIndex (n);
		double m, m0 = vessel->GetPropellantMaxMass (hP);
		oapiGetDlgItemText (hTab, IDC_EDIT3, cbuf, 256);
		sscanf (cbuf, "%lf", &m);
		level = max (0.0, min (1.0, m/m0));
	}
	SetLevel (level);
}

void EditorTab_Propellant::SetLevel (double level, bool setall)
{
	char cbuf[256];
	int i, j;
	DWORD n, k, k0, k1;
	double m0;
	VESSEL *vessel = oapiGetVesselInterface (ed->hVessel);
	ntank = vessel->GetPropellantCount();
	if (!ntank) return;
	oapiGetDlgItemText (hTab, IDC_EDIT1, cbuf, 256);
	i = sscanf (cbuf, "%d", &n);
	if (!i || --n >= ntank) return;
	if (setall) k0 = 0, k1 = ntank;
	else        k0 = n, k1 = n+1;
	for (k = k0; k < k1; k++) {
		PROPELLANT_HANDLE hP = vessel->GetPropellantHandleByIndex (k);
		m0 = vessel->GetPropellantMaxMass (hP);
		vessel->SetPropellantMass (hP, level*m0);
		if (k == n) {
			sprintf (cbuf, "%f", level);
			oapiSetDlgItemText (hTab, IDC_EDIT2, cbuf);
			sprintf (cbuf, "%0.2f", level*m0);
			oapiSetDlgItemText (hTab, IDC_EDIT3, cbuf);
			i = oapiGetGaugePos (oapiResDlgItem (hTab, IDC_PROPLEVEL));
			j = (int)(level*100.0+0.5);
			if (i != j) oapiSetGaugePos (oapiResDlgItem (hTab, IDC_PROPLEVEL), j);
		}
	}
	RefreshTotals();
}

void EditorTab_Propellant::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Propellant *pTab = (EditorTab_Propellant*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}


// ==============================================================
// EditorTab_Docking class definition
// ==============================================================

EditorTab_Docking::EditorTab_Docking (ScnEditor *editor) : ScnEditorTab (editor)
{
	CreateTab (IDD_TAB_EDIT8, EditorTab_Docking::DlgProc);
	DlgItem<QAbstractButton> (hTab, IDC_RADIO1)->setChecked (true);
	DlgItem<QAbstractButton> (hTab, IDC_RADIO2)->setChecked (false);
}

void EditorTab_Docking::InitTab ()
{
	oapiSetDlgItemText (hTab, IDC_EDIT1, "1");
	ScanTargetList();
	Refresh ();
}

char *EditorTab_Docking::HelpTopic ()
{
	return (char*)"/Docking.htm";
}

void EditorTab_Docking::TabProc (QWidget *hDlg)
{
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (3);
			return;
		case IDC_REFRESH:
			Refresh ();
			return;
		case IDC_DOCK:
			Dock ();
			return;
		case IDC_UNDOCK:
			Undock ();
			Refresh ();
			return;
		case IDC_IDS:
			ToggleIDS();
			Refresh ();
			return;
		case IDC_EDIT1:
			if (code == RESN_CHANGE)
				if (DockNo()) Refresh();
			return;
		case IDC_EDIT2:
			if (code == RESN_CHANGE)
				IncIDSChannel (0);
			return;
		case IDC_COMBO1:
			if (code == RESN_SELCHANGE) {
				SetTargetDock (1);
			}
			return;
		}
	});
	// WM_NOTIFY
	oapiConnectDlgDeltaPos (hDlg, [this, hDlg](int idFrom, int iPos, int iDelta) { // UDN_DELTAPOS
			char cbuf[256];
			DWORD n;
			switch (idFrom) {
			case IDC_SPIN1:
				oapiGetDlgItemText (hDlg, IDC_EDIT1, cbuf, 256);
				if (!sscanf (cbuf, "%d", &n)) n = 1;
				sprintf (cbuf, "%d", n + (iDelta < 0 ? 1 : -1));
				oapiSetDlgItemText (hDlg, IDC_EDIT1, cbuf);
				Refresh ();
				return;
			case IDC_SPIN2:
				IncIDSChannel (iDelta < 0 ? 1 : -1);
				Refresh ();
				return;
			case IDC_SPIN2A:
				IncIDSChannel (iDelta < 0 ? 20 : -20);
				Refresh ();
				return;
			case IDC_SPIN3:
				oapiGetDlgItemText (hDlg, IDC_EDIT4, cbuf, 256);
				if (!sscanf (cbuf, "%d", &n)) n = 1;
				SetTargetDock (n + (iDelta < 0 ? 1 : -1));
				return;
			}
	});
	ScnEditorTab::TabProc (hDlg);
}

UINT EditorTab_Docking::DockNo ()
{
	// returns the id of the dock currently being processed
	// (>= 1, or 0 if invalid)
	char cbuf[256];
	UINT dock;
	oapiGetDlgItemText (hTab, IDC_EDIT1, cbuf, 256);
	int res = sscanf (cbuf, "%d", &dock);
	if (!res || dock > Vessel()->DockCount()) dock = 0;
	return dock;
}

void EditorTab_Docking::ScanTargetList ()
{
	// populate docking target list
	DlgItem<QComboBox> (hTab, IDC_COMBO1)->clear();
	for (DWORD i = 0; i < oapiGetVesselCount(); i++) {
		OBJHANDLE hV = oapiGetVesselByIndex (i);
		VESSEL *v = oapiGetVesselInterface (hV);
		if (v != Vessel() && v->DockCount() > 0) { // only vessels with docking ports make sense here
			oapiComboAddString (DlgItem<QComboBox> (hTab, IDC_COMBO1), v->GetName());
		}
	}
}

void EditorTab_Docking::ToggleIDS ()
{
	UINT dock = DockNo();
	if (!dock) return;
	DOCKHANDLE hDock = Vessel()->GetDockHandle (dock-1);
	bool enable = (DlgItem<QAbstractButton> (hTab, IDC_IDS)->isChecked());
	Vessel()->EnableIDS (hDock, enable);
}

void EditorTab_Docking::IncIDSChannel (int dch)
{
	char cbuf[256];
	double freq;
	UINT dock = DockNo();
	if (!dock) return;
	DOCKHANDLE hDock = Vessel()->GetDockHandle (dock-1);
	oapiGetDlgItemText (hTab, IDC_EDIT2, cbuf, 256);
	if (sscanf (cbuf, "%lf", &freq)) {
		int ch = (int)((freq-108.0)*20.0+0.5);
		ch = max(0, min (639, ch+dch));
		Vessel()->SetIDSChannel (hDock, ch);
	}
}

void EditorTab_Docking::Dock ()
{
	char cbuf[256];
	DWORD n, ntgt, mode;
	VESSEL *vessel = Vessel();

	oapiGetDlgItemText (hTab, IDC_COMBO1, cbuf, 256);
	OBJHANDLE hTarget = oapiGetVesselByName (cbuf);
	if (!hTarget) return;
	VESSEL *target = oapiGetVesselInterface (hTarget);
	n = DockNo();
	if (!n) return;
	oapiGetDlgItemText (hTab, IDC_EDIT4, cbuf, 256);
	if (!sscanf (cbuf, "%d", &ntgt) || ntgt < 1 || ntgt > target->DockCount()) {
		DisplayErrorMsg (IDS_ERR4);
		return;
	}
	mode = (DlgItem<QAbstractButton> (hTab, IDC_RADIO1)->isChecked() ? 1:2);
	int res = vessel->Dock (hTarget, n-1, ntgt-1, mode);
	Refresh ();
	if (res) {
		static UINT errmsgid[3] = {IDS_ERR1, IDS_ERR2, IDS_ERR3};
		DisplayErrorMsg (errmsgid[res-1]);
	}
}

void EditorTab_Docking::Undock ()
{
	UINT dock = DockNo();
	if (!dock) return;
	Vessel()->Undock (dock-1);
}

void EditorTab_Docking::Refresh ()
{
	static const int dockitem[7] = {IDC_DOCK, IDC_COMBO1, IDC_EDIT4, IDC_STATIC3, IDC_SPIN3, IDC_RADIO1, IDC_RADIO2};
	static const int undockitem[2] = {IDC_UNDOCK, IDC_EDIT3};

	VESSEL *vessel = Vessel();
	char cbuf[256];
	int i;
	DWORD n, ndock = vessel->DockCount();
	oapiSetDlgItemText (hTab, IDC_ERRMSG, "");

	sprintf (cbuf, "of %d", vessel->DockCount());
	oapiSetDlgItemText (hTab, IDC_STATIC1, cbuf);

	oapiGetDlgItemText (hTab, IDC_EDIT1, cbuf, 256);
	if (!sscanf (cbuf, "%d", &n)) n = 0;
	if (n < 1 || n > ndock) {
		n = max ((DWORD)1, min (ndock, n));
		sprintf (cbuf, "%d", n);
		oapiSetDlgItemText (hTab, IDC_EDIT1, cbuf);
	}
	n--; // zero-based

	DOCKHANDLE hDock = vessel->GetDockHandle (n);
	NAVHANDLE hIDS = vessel->GetIDS (hDock);
	if (hIDS) sprintf (cbuf, "%0.2f", oapiGetNavFreq (hIDS));
	else      strcpy (cbuf, "<none>");
	oapiSetDlgItemText (hTab, IDC_EDIT2, cbuf);
	DlgItem<QAbstractButton> (hTab, IDC_IDS)->setChecked (hIDS != NULL);
	oapiResDlgItem (hTab, IDC_EDIT2)->setEnabled (hIDS ? TRUE:FALSE);
	oapiResDlgItem (hTab, IDC_SPIN2)->setEnabled (hIDS ? TRUE:FALSE);

	OBJHANDLE hMate = vessel->GetDockStatus (hDock);
	if (hMate) { // dock is engaged
		oapiSetDlgItemText (hTab, IDC_STATIC2, "Currently docked to");
		for (i = 0; i < 7; i++) oapiResDlgItem (hTab, dockitem[i])->hide();
		for (i = 0; i < 2; i++) oapiResDlgItem (hTab, undockitem[i])->show();
		oapiGetObjectName (hMate, cbuf, 256);
		oapiSetDlgItemText (hTab, IDC_EDIT3, cbuf);
	} else { // dock is free
		oapiSetDlgItemText (hTab, IDC_STATIC2, "Establish docking connection with");
		for (i = 0; i < 2; i++) oapiResDlgItem (hTab, undockitem[i])->hide();
		for (i = 0; i < 7; i++) oapiResDlgItem (hTab, dockitem[i])->show();
	}
}

void EditorTab_Docking::SetTargetDock (DWORD dock)
{
	char cbuf[256];
	DWORD n = 0;
	oapiGetDlgItemText (hTab, IDC_COMBO1, cbuf, 256);
	OBJHANDLE hTarget = oapiGetVesselByName (cbuf);
	if (hTarget) {
		VESSEL *v = oapiGetVesselInterface (hTarget);
		DWORD ndock = v->DockCount();
		if (ndock) n = max ((DWORD)1, min (ndock, dock));
	}
	if (n) sprintf (cbuf, "%d", n);
	else   cbuf[0] = '\0';
	oapiSetDlgItemText (hTab, IDC_EDIT4, cbuf);
}

void EditorTab_Docking::DisplayErrorMsg (UINT err)
{
	char cbuf[256] = "Error: ";
	oapiLoadResString (ed->InstHandle(), err, cbuf+7, 249);
	oapiSetDlgItemText (hTab, IDC_ERRMSG, cbuf);
}

void EditorTab_Docking::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Docking *pTab = (EditorTab_Docking*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}


// ==============================================================
// EditorTab_Custom class definition
// ==============================================================

EditorTab_Custom::EditorTab_Custom (ScnEditor *editor, void *hInst, WORD ResId, DLGINIT UserProc) : ScnEditorTab (editor)
{
	usrProc = UserProc;
	CreateTab (hInst, ResId, EditorTab_Custom::DlgProc);
}

void EditorTab_Custom::OpenHelp ()
{
	// usrProc (hTab, WM_COMMAND, IDHELP, 0): the page's help button, if it has one
	QAbstractButton *help = DlgItem<QAbstractButton> (hTab, IDHELP);
	if (help) help->click();
	else
		ScnEditorTab::OpenHelp();
}

void EditorTab_Custom::TabProc (QWidget *hDlg)
{
	// WM_SCNEDITOR: requests of the vessel module's page through ScnEditorMsg, also from usrProc's set-up
	SCNEDITORMSG msgproc = [](QWidget *hDlg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
		EditorTab_Custom *pTab = (EditorTab_Custom*)TabPointer (hDlg);
		switch (LOWORD (wParam)) {
		case SE_GETVESSEL:
			*(OBJHANDLE*)lParam = pTab->ed->hVessel;
			return TRUE;
		}
		return FALSE;
	};
	hDlg->setProperty ("ScnEditorMsg", QVariant::fromValue ((void*)msgproc));
	// WM_INITDIALOG
		usrProc (hDlg, ed->hVessel);
	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [this](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BACK:
			SwitchTab (3);
			return;
		}
	});
	// all other messages: usrProc connected its own handlers
}

void EditorTab_Custom::DlgProc (QWidget *hDlg, void *context)
{
	EditorTab_Custom *pTab = (EditorTab_Custom*)TabPointer (hDlg, context);
	if (!pTab) return;
	else pTab->TabProc (hDlg);
}

