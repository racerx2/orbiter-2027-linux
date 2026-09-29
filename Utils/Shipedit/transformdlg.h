// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#if !defined(AFX_TRANSFORMDLG_H__1900EF76_C581_47A3_8AF8_BCB3180148B4__INCLUDED_)
#define AFX_TRANSFORMDLG_H__1900EF76_C581_47A3_8AF8_BCB3180148B4__INCLUDED_

// TransformDlg.h : header file
//

#include "Mesh.h"

/////////////////////////////////////////////////////////////////////////////
// TranslateDlg dialog

class TranslateDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	TranslateDlg(Mesh *_mesh, QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(TranslateDlg)
	enum { IDD = IDD_DIALOG1 };
	float	m_Translatex;
	float	m_Translatey;
	float	m_Translatez;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(TranslateDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	Mesh *mesh;

	// Generated message map functions
	//{{AFX_MSG(TranslateDlg)
	virtual void OnOK();
	//}}AFX_MSG
	// DECLARE_MESSAGE_MAP: no entries
};

/////////////////////////////////////////////////////////////////////////////
// RotateDlg dialog

class RotateDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	RotateDlg(Mesh *_mesh, QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(RotateDlg)
	enum { IDD = IDD_TRANSFORM_ROT };
	double	m_Rotx;
	double	m_Roty;
	double	m_Rotz;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(RotateDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	Mesh *mesh;

	// Generated message map functions
	//{{AFX_MSG(RotateDlg)
	void OnDoRotx();
	void OnDoRoty();
	void OnDoRotz();
	//}}AFX_MSG
	virtual BOOL OnCommand(int nID, int nCode); // DECLARE_MESSAGE_MAP: the map is a WM_COMMAND switch
};
/////////////////////////////////////////////////////////////////////////////
// ScaleDlg dialog

class ScaleDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	ScaleDlg(Mesh *_mesh, QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(ScaleDlg)
	enum { IDD = IDD_TRANSFORM_SCALE };
	double	m_ScaleX;
	double	m_ScaleY;
	double	m_ScaleZ;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(ScaleDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	Mesh *mesh;

	// Generated message map functions
	//{{AFX_MSG(ScaleDlg)
	virtual void OnOK();
	//}}AFX_MSG
	// DECLARE_MESSAGE_MAP: no entries
};
/////////////////////////////////////////////////////////////////////////////
// ZerolevelDlg dialog

class ZerolevelDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	ZerolevelDlg(Mesh *_mesh, QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(ZerolevelDlg)
	enum { IDD = IDD_ZEROLEVEL };
	float	m_Zlevel;
	BOOL	m_ResetVtx;
	BOOL	m_ResetNml;
	BOOL	m_ResetTex;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(ZerolevelDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	Mesh *mesh;

	// Generated message map functions
	//{{AFX_MSG(ZerolevelDlg)
	virtual void OnOK();
	//}}AFX_MSG
	// DECLARE_MESSAGE_MAP: no entries
};
/////////////////////////////////////////////////////////////////////////////
// MergeDlg dialog

class MergeDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	MergeDlg(Mesh *_mesh, QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(MergeDlg)
	enum { IDD = IDD_MERGEGRP };
	UINT	m_Grp1;
	UINT	m_Grp2;
	std::string	m_Label1;
	std::string	m_Label2;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(MergeDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	Mesh *mesh;

	// Generated message map functions
	//{{AFX_MSG(MergeDlg)
	virtual void OnOK();
	//}}AFX_MSG
	// DECLARE_MESSAGE_MAP: no entries
};
/////////////////////////////////////////////////////////////////////////////
// NormalDlg dialog

class NormalDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	NormalDlg(Mesh *_mesh, QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(NormalDlg)
	enum { IDD = IDD_CALCNORMAL };
	int		m_Selgrp;
	int		m_Selvtx;
	UINT	m_Group;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(NormalDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	Mesh *mesh;

	// Generated message map functions
	//{{AFX_MSG(NormalDlg)
	void OnNmlSelall();
	void OnNmlSelone();
	void OnNmlapply();
	//}}AFX_MSG
	virtual BOOL OnCommand(int nID, int nCode); // DECLARE_MESSAGE_MAP: the map is a WM_COMMAND switch
};
/////////////////////////////////////////////////////////////////////////////
// MirrorDlg dialog

class MirrorDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	MirrorDlg(Mesh *_mesh, QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(MirrorDlg)
	enum { IDD = IDD_MIRROR };
	int		m_MirrorX;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(MirrorDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	Mesh *mesh;

	// Generated message map functions
	//{{AFX_MSG(MirrorDlg)
	virtual void OnOK();
	//}}AFX_MSG
	// DECLARE_MESSAGE_MAP: no entries
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TRANSFORMDLG_H__1900EF76_C581_47A3_8AF8_BCB3180148B4__INCLUDED_)
