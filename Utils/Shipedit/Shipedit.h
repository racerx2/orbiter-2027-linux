// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// Shipedit.h : main header file for the SHIPEDIT application
//

#if !defined(AFX_SHIPEDIT_H__820CEA84_B7CD_4458_8801_283B73C584D4__INCLUDED_)
#define AFX_SHIPEDIT_H__820CEA84_B7CD_4458_8801_283B73C584D4__INCLUDED_


#ifndef AFX_STDAFX_H__8DE406CF_DC7E_4C3E_820F_4BED08745A8F__INCLUDED_ // __AFXWIN_H__: StdAfx.h no longer brings afxwin.h
	#error include 'stdafx.h' before including this file for PCH
#endif

// d3d.h left out: the Direct3D 7 data types come from the SDK (see Mesh.h)
#include "resource.h"		// main symbols
#include "Vecmat.h"
#include "Mesh.h"

typedef struct {
	float x1, y1, z1; // vtx 1
	float x2, y2, z2; // vtx 2
	float x3, y3, z3; // vtx 3
	float a, b, c, d; // plane params
	float d1, d2, d3; // dist of vtx i from opposite edge
} TriParam;

typedef struct {
	int gridx, gridy, gridz;
	int gridn;
	float dx, dy, dz;
	BYTE *grid;
} VOXGRID;

/////////////////////////////////////////////////////////////////////////////
// CShipeditApp:
// See Shipedit.cpp for the implementation of this class
//

class CShipeditDlg; // g++: the friend declaration below doesn't make the name visible to m_pMainDlg

class CShipeditApp { // CWinApp: main() in Shipedit.cpp runs InitInstance and Run with a QApplication
	friend class CShipeditDlg;
	friend class GridintDlg;
public:
	CShipeditApp ();
	void InitMesh ();
	Mesh mesh;
	int Run ();                    // CWinThread::Run: message loop with OnIdle, then ExitInstance
	BOOL OnCommand (int nID);      // CCmdTarget::OnCmdMsg: the app's message map, last in the command route
	ResDlg *m_pMainWnd;            // CWinThread::m_pMainWnd

private:
	void ProcessPackage ();
	BOOL OnIdle (LONG lCount);
	CShipeditDlg *m_pMainDlg;
	DWORD ngrp, nvtx, nidx, ntri;  // mesh groups
	NTVERTEX *vtx;
	WORD *idx;
	TriParam *pp;
	oapi::FVECTOR3 bbmin, bbmax;   // bounding box
	double bbvol;                  // bb volume
	Vector bbcs;                   // bb cross sections
	double vol;                    // volume
	Vector cg, cg_base, cg_add;    // centre of gravity
	Vector cs;                     // cross sections
	Matrix J, J_base, J_add;       // inertia tensor
	BOOL bBackgroundOp;
	int flushcount;
	int nop, nvol, ncs[3];

	// grid-integration related functions
	void setup_grid (VOXGRID &g, int level);
	bool scan_gridline (VOXGRID &g, int x, int y, int z, int dir_idx, int ntri, const TriParam *pp);
	void analyse_grid (VOXGRID &g, double &vol, Vector &com, Vector &cs, Matrix &pmi);

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CShipeditApp)
	public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CShipeditApp)
	void OnLoad();
	void OnSaveas();
	void OnTranslate();
	void OnRotate();
	void OnZerolevel();
	void OnVoxint();
	void OnMergegrp();
	void OnCalcnormal();
	void OnScale();
	void OnMirror();
	//}}AFX_MSG
	// DECLARE_MESSAGE_MAP: OnCommand
	void OnFileAddmesh();
};


/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// GridintDlg dialog

class GridintDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	GridintDlg(CShipeditApp *_app, QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(GridintDlg)
	enum { IDD = IDD_GRIDINT };
	int		m_GridDim;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(GridintDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	CShipeditApp *app;

	// Generated message map functions
	//{{AFX_MSG(GridintDlg)
	void OnGridintStart();
	void OnChangeGridintDim();
	//}}AFX_MSG
	virtual BOOL OnCommand(int nID, int nCode); // DECLARE_MESSAGE_MAP: the map is a WM_COMMAND switch
};
/////////////////////////////////////////////////////////////////////////////
// AddMeshDlg dialog

class AddMeshDlg : public ResDlg // CDialog: ResDlg (StdAfx.h)
{
// Construction
public:
	AddMeshDlg(QWidget* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(AddMeshDlg)
	enum { IDD = IDD_MERGEOVERRD };
	int		m_AddMode;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(AddMeshDlg)
	protected:
	virtual void DoDataExchange(BOOL bSaveAndValidate);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(AddMeshDlg)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	// DECLARE_MESSAGE_MAP: no entries
};
//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_SHIPEDIT_H__820CEA84_B7CD_4458_8801_283B73C584D4__INCLUDED_)
