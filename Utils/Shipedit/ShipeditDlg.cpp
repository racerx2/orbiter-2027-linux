// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ShipeditDlg.cpp : implementation file
//

#include "StdAfx.h"
#include "Shipedit.h"
#include "ShipeditDlg.h"
#include <QAction>
#include <QMessageBox>
#include <cassert>

// DEBUG_NEW (_DEBUG) left out: MFC's debug allocator

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public ResDlg
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
};

CAboutDlg::CAboutDlg() : ResDlg(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

// BEGIN_MESSAGE_MAP(CAboutDlg): no message handlers, ResDlg::OnCommand ends the dialog on IDOK

/////////////////////////////////////////////////////////////////////////////
// CShipeditDlg dialog

CShipeditDlg::CShipeditDlg(CShipeditApp *app, QWidget* pParent /*=NULL*/)
	: ResDlg(CShipeditDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CShipeditDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	m_hIcon = ResDlg::LoadIcon(IDR_MAINFRAME);
	m_app = app;
}

void CShipeditDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(CShipeditDlg)
		// NOTE: the ClassWizard will add DDX and DDV calls here
	//}}AFX_DATA_MAP
}

// message map: menu commands (the menu bar of OnInitDialog); what the dialog doesn't handle goes to the app (CDialog::OnCmdMsg)
BOOL CShipeditDlg::OnCommand(int nID, int nCode)
{
	//{{AFX_MSG_MAP(CShipeditDlg)
	// ON_WM_SYSCOMMAND: OnInitDialog connects the About entry of the context menu
	// ON_WM_PAINT, ON_WM_QUERYDRAGICON left out (see ShipeditDlg.h)
	// ON_WM_CLOSE: ResDlg calls OnClose on the window's close event
	if (nCode == RESN_CLICKED) switch (nID) { // ON_COMMAND
	case MID_CALCSTART: OnCalcstart(); return TRUE;
	case MID_CALCSTOP:  OnCalcstop();  return TRUE;
	case MID_EXIT:      OnExit();      return TRUE;
	case MID_CHECK:     OnCheck();     return TRUE;
	}
	//}}AFX_MSG_MAP
	if (ResDlg::OnCommand(nID, nCode)) return TRUE;
	return nCode == RESN_CLICKED && m_app->OnCommand(nID);
}

/////////////////////////////////////////////////////////////////////////////
// CShipeditDlg message handlers

BOOL CShipeditDlg::OnInitDialog()
{
	ResDlg::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	assert((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	assert(IDM_ABOUTBOX < 0xF000);

	QWidget* pSysMenu = hDlg; // the title bar menu is the window manager's: the entry goes to the dialog's context menu
	if (pSysMenu != NULL)
	{
		char strAboutMenu[256];
		oapiLoadResString(nullptr, IDS_ABOUTBOX, strAboutMenu, 256);
		if (strAboutMenu[0])
		{
			QAction* pSep = new QAction(pSysMenu);
			pSep->setSeparator(true);
			pSysMenu->addAction(pSep); // MF_SEPARATOR
			QAction* pAbout = new QAction(QString::fromUtf8(strAboutMenu), pSysMenu);
			QObject::connect(pAbout, &QAction::triggered, pSysMenu, [this]() { OnSysCommand(IDM_ABOUTBOX, 0); });
			pSysMenu->addAction(pAbout); // MF_STRING, IDM_ABOUTBOX
			pSysMenu->setContextMenuPolicy(Qt::ActionsContextMenu);
		}
	}

	hDlg->setWindowIcon(m_hIcon);	// Set big and small icon
	
	// TODO: Add extra initialization here
	
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CShipeditDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		// CDialog::OnSysCommand left out: the other system commands belong to the window manager
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

// OnPaint, OnQueryDragIcon left out: the window manager draws the minimised window's icon

void CShipeditDlg::Refresh ()
{
	char cbuf[256];
	sprintf (cbuf, "%d", m_app->ngrp);
	oapiSetDlgText (GetDlgItem (IDC_NGROUP), cbuf);
	sprintf (cbuf, "%d", m_app->nvtx);
	oapiSetDlgText (GetDlgItem (IDC_NVTX), cbuf);
	sprintf (cbuf, "%d", m_app->ntri);
	oapiSetDlgText (GetDlgItem (IDC_NTRI), cbuf);
	sprintf (cbuf, "[%0.2f %0.2f %0.2f] [%0.2f %0.2f %0.2f]",
		m_app->bbmin.x, m_app->bbmin.y, m_app->bbmin.z,
		m_app->bbmax.x, m_app->bbmax.y, m_app->bbmax.z);
	oapiSetDlgText (GetDlgItem (IDC_BB), cbuf);
}

void CShipeditDlg::RefreshCalc ()
{
	char cbuf[256];
	sprintf (cbuf, "Parameters (%d samples)", m_app->nop);
	oapiSetDlgText (GetDlgItem (IDC_NSAMPLE), cbuf);

	sprintf (cbuf, "%0.2f", m_app->vol);
	oapiSetDlgText (GetDlgItem (IDC_VOL), cbuf);
	sprintf (cbuf, "%0.2f %0.2f %0.2f", m_app->cg.x, m_app->cg.y, m_app->cg.z);
	oapiSetDlgText (GetDlgItem (IDC_CG), cbuf);
	sprintf (cbuf, "%0.2f %0.2f %0.2f", m_app->cs.x, m_app->cs.y, m_app->cs.z);
	oapiSetDlgText (GetDlgItem (IDC_CS), cbuf);
	sprintf (cbuf, "%0.2f\t%0.2f\t%0.2f", m_app->J.m11, m_app->J.m12, m_app->J.m13);
	oapiSetDlgText (GetDlgItem (IDC_INERTIA1), cbuf);
	sprintf (cbuf, "%0.2f\t%0.2f\t%0.2f", m_app->J.m12, m_app->J.m22, m_app->J.m23);
	oapiSetDlgText (GetDlgItem (IDC_INERTIA2), cbuf);
	sprintf (cbuf, "%0.2f\t%0.2f\t%0.2f", m_app->J.m13, m_app->J.m23, m_app->J.m33);
	oapiSetDlgText (GetDlgItem (IDC_INERTIA3), cbuf);
}

void CShipeditDlg::OnCalcstart() 
{
	if (m_app->ngrp) // have mesh?
		m_app->bBackgroundOp = TRUE;
}

void CShipeditDlg::OnCalcstop() 
{
	m_app->bBackgroundOp = FALSE;
}

void CShipeditDlg::OnCheck() 
{
	DWORD i, nremoved, tot_removed = 0;
	if (m_app->ngrp) {// have mesh?
		Mesh &mesh = m_app->mesh;
		for (i = 0; i < mesh.nGroup(); i++) {
			mesh.CheckGroup (i, nremoved);
			tot_removed += nremoved;
		}
		if (tot_removed) {
			char cbuf[256];
			sprintf (cbuf, "Removed %d unused vertices from mesh.", tot_removed);
			QMessageBox (QMessageBox::NoIcon, "Check result", cbuf, QMessageBox::Ok, hDlg).exec (); // MessageBox, MB_OK
		} else {
			QMessageBox (QMessageBox::NoIcon, "Check result", "No problems found", QMessageBox::Ok, hDlg).exec ();
		}
	}
	m_app->InitMesh();
}

void CShipeditDlg::OnExit() 
{
	DestroyWindow ();
}

void CShipeditDlg::OnClose() 
{
	ResDlg::OnClose();
	DestroyWindow ();
}
