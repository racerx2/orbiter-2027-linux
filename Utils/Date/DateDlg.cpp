// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// DateDlg.cpp : implementation file
//

#include "StdAfx.h"
#include "Date.h"
#include "DateDlg.h"
#include "Convert.h"
#include <QAction>
#include <cassert>
#include <cstring>

// DEBUG_NEW (_DEBUG) left out: MFC's debug allocator

static bool bIgnore = false;

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
// CDateDlg dialog

CDateDlg::CDateDlg(QWidget* pParent /*=NULL*/)
	: ResDlg(CDateDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CDateDlg)
	m_MJD = "";
	//}}AFX_DATA_INIT
	m_hIcon = ResDlg::LoadIcon(IDR_MAINFRAME);
}

void CDateDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(CDateDlg)
	ExchangeText(bSaveAndValidate, IDC_MJD, m_MJD);
	ValidateMaxChars(bSaveAndValidate, m_MJD, 32);
	//}}AFX_DATA_MAP
}

// message map: WM_COMMAND notifications of the controls, connected in ResDlg (oapiConnectDlgCommands)
BOOL CDateDlg::OnCommand(int nID, int nCode)
{
	//{{AFX_MSG_MAP(CDateDlg)
	// ON_WM_SYSCOMMAND: OnInitDialog connects the About entry of the context menu
	// ON_WM_PAINT, ON_WM_QUERYDRAGICON left out (see DateDlg.h)
	if (nCode == RESN_CHANGE) switch (nID) { // ON_EN_CHANGE
	case IDC_MJD:      OnChangeMjd();     return TRUE;
	case IDC_UT_DAY:   OnChangeUtDay();   return TRUE;
	case IDC_UT_MONTH: OnChangeUtMonth(); return TRUE;
	case IDC_UT_YEAR:  OnChangeUtYear();  return TRUE;
	case IDC_UT_HOUR:  OnChangeUtHour();  return TRUE;
	case IDC_UT_MIN:   OnChangeUtMin();   return TRUE;
	case IDC_UT_SEC:   OnChangeUtSec();   return TRUE;
	case IDC_JD:       OnChangeJd();      return TRUE;
	case IDC_JC:       OnChangeJc();      return TRUE;
	case IDC_EPOCH:    OnChangeEpoch();   return TRUE;
	}
	//}}AFX_MSG_MAP
	return ResDlg::OnCommand(nID, nCode);
}

/////////////////////////////////////////////////////////////////////////////
// CDateDlg message handlers

BOOL CDateDlg::OnInitDialog()
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
	
	SetMJD (MJD (time (NULL)), true);
	// initialise to current system time
	
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CDateDlg::OnSysCommand(UINT nID, LPARAM lParam)
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

void CDateDlg::UpdateUT (void)
{
	char cbuf[256];

	sprintf (cbuf, "%02d", date.tm_mday);
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_UT_DAY), cbuf);
	bIgnore = false;

	sprintf (cbuf, "%02d", date.tm_mon);
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_UT_MONTH), cbuf);
	bIgnore = false;

	sprintf (cbuf, "%04d", date.tm_year+1900);
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_UT_YEAR), cbuf);
	bIgnore = false;

	sprintf (cbuf, "%02d", date.tm_hour);
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_UT_HOUR), cbuf);
	bIgnore = false;

	sprintf (cbuf, "%02d", date.tm_min);
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_UT_MIN), cbuf);
	bIgnore = false;

	sprintf (cbuf, "%02d", date.tm_sec);
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_UT_SEC), cbuf);
	bIgnore = false;
}

void CDateDlg::UpdateMJD (void)
{
	char cbuf[256];
	sprintf (cbuf, "%0.6f", mjd);
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_MJD), cbuf);
	bIgnore = false;
}

void CDateDlg::UpdateJD (void)
{
	char cbuf[256];
	sprintf (cbuf, "%0.6f", mjd + 2400000.5);
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_JD), cbuf);
	bIgnore = false;
}

void CDateDlg::UpdateJC (void)
{
	char cbuf[256];
	sprintf (cbuf, "%0.10f", MJD2JC(mjd));
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_JC), cbuf);
	bIgnore = false;
}

void CDateDlg::UpdateEpoch (void)
{
	char cbuf[256];
	sprintf (cbuf, "%0.8f", MJD2Jepoch (mjd));
	bIgnore = true;
	oapiSetDlgText (GetDlgItem (IDC_EPOCH), cbuf);
	bIgnore = false;
}

void CDateDlg::SetMJD (double new_mjd, bool reset_mjd)
{
	mjd = new_mjd;
	memcpy (&date, mjddate(mjd), sizeof (date));

	UpdateUT();
	UpdateJD();
	UpdateJC();
	UpdateEpoch();
	if (reset_mjd) UpdateMJD();
}

void CDateDlg::SetJD (double new_jd, bool reset_jd)
{
	mjd = new_jd - 2400000.5;
	memcpy (&date, mjddate(mjd), sizeof (date));

	UpdateUT();
	UpdateMJD();
	UpdateJC();
	UpdateEpoch();
	if (reset_jd) UpdateJD();
}

void CDateDlg::SetJC (double new_jc, bool reset_jc)
{
	mjd = JC2MJD (new_jc);
	memcpy (&date, mjddate(mjd), sizeof (date));

	UpdateUT();
	UpdateMJD();
	UpdateJD();
	UpdateEpoch();
	if (reset_jc) UpdateJC();
}

void CDateDlg::SetEpoch (double new_epoch, bool reset_epoch)
{
	mjd = Jepoch2MJD (new_epoch);
	memcpy (&date, mjddate(mjd), sizeof (date));

	UpdateUT();
	UpdateMJD();
	UpdateJD();
	UpdateJC();
	if (reset_epoch) UpdateEpoch();
}

void CDateDlg::SetUT (struct tm *new_date, bool reset_ut)
{
	mjd = date2mjd (new_date);
	UpdateMJD();
	UpdateJD();
	UpdateJC();
	UpdateEpoch();
	if (reset_ut) UpdateUT();
}

void CDateDlg::OnChangeMjd() 
{
	if (bIgnore) return;
	char cbuf[256];
	double new_mjd;
	oapiGetDlgText (GetDlgItem (IDC_MJD), cbuf, 256);
	if (sscanf (cbuf, "%lf", &new_mjd) == 1 && fabs (new_mjd-mjd) > 1e-6)
		SetMJD (new_mjd);
}

void CDateDlg::OnChangeJd() 
{
	if (bIgnore) return;
	char cbuf[256];
	double new_jd;
	oapiGetDlgText (GetDlgItem (IDC_JD), cbuf, 256);
	if (sscanf (cbuf, "%lf", &new_jd) == 1)
		SetJD (new_jd);
}

void CDateDlg::OnChangeJc() 
{
	if (bIgnore) return;
	char cbuf[256];
	double new_jc;
	oapiGetDlgText (GetDlgItem (IDC_JC), cbuf, 256);
	if (sscanf (cbuf, "%lf", &new_jc) == 1)
		SetJC (new_jc);
}

void CDateDlg::OnChangeEpoch() 
{
	if (bIgnore) return;
	char cbuf[256];
	double new_epoch;
	oapiGetDlgText (GetDlgItem (IDC_EPOCH), cbuf, 256);
	if (sscanf (cbuf, "%lf", &new_epoch) == 1)
		SetEpoch (new_epoch);
}

void CDateDlg::OnChangeUtDay() 
{
	if (bIgnore) return;
	char cbuf[256];
	int day;

	oapiGetDlgText (GetDlgItem (IDC_UT_DAY), cbuf, 256);
	if (sscanf (cbuf, "%d", &day) == 1 && day != date.tm_mday && day >= 1 && day <= 31) {
		date.tm_mday = day;
		SetUT (&date);
	}
}

void CDateDlg::OnChangeUtMonth() 
{
	if (bIgnore) return;
	char cbuf[256];
	int month;

	oapiGetDlgText (GetDlgItem (IDC_UT_MONTH), cbuf, 256);
	if (sscanf (cbuf, "%d", &month) == 1 && month != date.tm_mon && month >= 1 && month <= 12) {
		date.tm_mon = month;
		SetUT (&date);
	}
}

void CDateDlg::OnChangeUtYear() 
{
	if (bIgnore) return;
	char cbuf[256];
	int year;

	oapiGetDlgText (GetDlgItem (IDC_UT_YEAR), cbuf, 256);
	if ((sscanf (cbuf, "%d", &year) == 1) && ((year -= 1900) != date.tm_year)) {
		date.tm_year = year;
		SetUT (&date);
	}
}

void CDateDlg::OnChangeUtHour() 
{
	if (bIgnore) return;
	char cbuf[256];
	int hour;

	oapiGetDlgText (GetDlgItem (IDC_UT_HOUR), cbuf, 256);
	if (sscanf (cbuf, "%d", &hour) == 1 && hour != date.tm_hour) {
		date.tm_hour = hour;
		SetUT (&date);
	}
}

void CDateDlg::OnChangeUtMin() 
{
	if (bIgnore) return;
	char cbuf[256];
	int min;

	oapiGetDlgText (GetDlgItem (IDC_UT_MIN), cbuf, 256);
	if (sscanf (cbuf, "%d", &min) == 1 && min != date.tm_min) {
		date.tm_min = min;
		SetUT (&date);
	}
}

void CDateDlg::OnChangeUtSec() 
{
	if (bIgnore) return;
	char cbuf[256];
	int sec;

	oapiGetDlgText (GetDlgItem (IDC_UT_SEC), cbuf, 256);
	if (sscanf (cbuf, "%d", &sec) == 1 && sec != date.tm_sec) {
		date.tm_sec = sec;
		SetUT (&date);
	}
}
