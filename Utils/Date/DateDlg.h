// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// DateDlg.h : header file
//

#if !defined(AFX_DATEDLG_H__A0DC46B1_90BB_4A9A_A1D4_5DD52D2CED37__INCLUDED_)
#define AFX_DATEDLG_H__A0DC46B1_90BB_4A9A_A1D4_5DD52D2CED37__INCLUDED_


/////////////////////////////////////////////////////////////////////////////
// CDateDlg dialog

class CDateDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	CDateDlg(QWidget* pParent = NULL);	// standard constructor
	void UpdateUT (void);
	void UpdateMJD (void);
	void UpdateJD (void);
	void UpdateJC (void);
	void UpdateEpoch (void);
	void SetMJD (double new_mjd, bool reset_mjd = false);
	void SetUT (struct tm *new_date, bool reset_ut = false);
	void SetJD (double new_jd, bool reset_jd = false);
	void SetJC (double new_jc, bool reset_jc = false);
	void SetEpoch (double new_epoch, bool reset_epoch = false);

// Dialog Data
	//{{AFX_DATA(CDateDlg)
	enum { IDD = IDD_DATE_DIALOG };
	std::string	m_MJD;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDateDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	QIcon m_hIcon;
	double mjd;
	struct tm date;

	// Generated message map functions
	//{{AFX_MSG(CDateDlg)
	virtual BOOL OnInitDialog();
	void OnSysCommand(UINT nID, LPARAM lParam);
	// OnPaint, OnQueryDragIcon left out: the window manager draws the minimised window's icon (setWindowIcon)
	void OnChangeMjd();
	void OnChangeUtDay();
	void OnChangeUtMonth();
	void OnChangeUtYear();
	void OnChangeUtHour();
	void OnChangeUtMin();
	void OnChangeUtSec();
	void OnChangeJd();
	void OnChangeJc();
	void OnChangeEpoch();
	//}}AFX_MSG
	virtual BOOL OnCommand(int nID, int nCode); // DECLARE_MESSAGE_MAP: the map is a WM_COMMAND switch
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DATEDLG_H__A0DC46B1_90BB_4A9A_A1D4_5DD52D2CED37__INCLUDED_)
