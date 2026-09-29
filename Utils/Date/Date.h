// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// Date.h : main header file for the DATE application
//

#if !defined(AFX_DATE_H__C7114870_6AAA_4AD8_A08F_CA2ADA650ED8__INCLUDED_)
#define AFX_DATE_H__C7114870_6AAA_4AD8_A08F_CA2ADA650ED8__INCLUDED_


#ifndef AFX_STDAFX_H__2749A3D2_C3AC_49E9_94A0_4873DA6265FB__INCLUDED_ // __AFXWIN_H__: StdAfx.h no longer brings afxwin.h
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "Resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CDateApp:
// See Date.cpp for the implementation of this class
//

class CDateApp // CWinApp: main() in Date.cpp runs InitInstance with a QApplication
{
public:
	CDateApp();
	ResDlg *m_pMainWnd; // CWinThread::m_pMainWnd

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDateApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CDateApp)
	//}}AFX_MSG
	// DECLARE_MESSAGE_MAP left out: see Date.cpp
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DATE_H__C7114870_6AAA_4AD8_A08F_CA2ADA650ED8__INCLUDED_)
