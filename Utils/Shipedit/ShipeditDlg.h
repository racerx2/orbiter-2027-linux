// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ShipeditDlg.h : header file
//

#if !defined(AFX_SHIPEDITDLG_H__2089FACA_79D2_409D_A3E7_D2F94DBCF16D__INCLUDED_)
#define AFX_SHIPEDITDLG_H__2089FACA_79D2_409D_A3E7_D2F94DBCF16D__INCLUDED_


/////////////////////////////////////////////////////////////////////////////
// CShipeditDlg dialog

class CShipeditDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	CShipeditDlg(CShipeditApp *app, QWidget* pParent = NULL);	// standard constructor
	void Refresh ();
	void RefreshCalc ();

// Dialog Data
	//{{AFX_DATA(CShipeditDlg)
	enum { IDD = IDD_SHIPEDIT_DIALOG };
		// NOTE: the ClassWizard will add data members here
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CShipeditDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	QIcon m_hIcon;
	CShipeditApp *m_app;

	// Generated message map functions
	//{{AFX_MSG(CShipeditDlg)
	virtual BOOL OnInitDialog();
	void OnSysCommand(UINT nID, LPARAM lParam);
	// OnPaint, OnQueryDragIcon left out: the window manager draws the minimised window's icon (setWindowIcon)
	void OnCalcstart();
	void OnCalcstop();
	void OnExit();
	virtual void OnClose();
	void OnCheck();
	//}}AFX_MSG
	virtual BOOL OnCommand(int nID, int nCode); // DECLARE_MESSAGE_MAP: the map is a WM_COMMAND switch
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_SHIPEDITDLG_H__2089FACA_79D2_409D_A3E7_D2F94DBCF16D__INCLUDED_)
