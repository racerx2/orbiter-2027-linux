// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// TransformDlg.cpp : implementation file
//

#include "StdAfx.h"
#include "Shipedit.h"
#include "transformdlg.h"
#include <cstring>

// DEBUG_NEW (_DEBUG) left out: MFC's debug allocator

extern CShipeditApp theApp;

/////////////////////////////////////////////////////////////////////////////
// TranslateDlg dialog


TranslateDlg::TranslateDlg(Mesh *_mesh, QWidget* pParent)
: ResDlg(TranslateDlg::IDD, pParent), mesh(_mesh)
{
	//{{AFX_DATA_INIT(TranslateDlg)
	m_Translatex = 0.0f;
	m_Translatey = 0.0f;
	m_Translatez = 0.0f;
	//}}AFX_DATA_INIT
}


void TranslateDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(TranslateDlg)
	ExchangeText(bSaveAndValidate, IDC_TRANSLATEX, m_Translatex);
	ExchangeText(bSaveAndValidate, IDC_TRANSLATEY, m_Translatey);
	ExchangeText(bSaveAndValidate, IDC_TRANSLATEZ, m_Translatez);
	//}}AFX_DATA_MAP
}


// BEGIN_MESSAGE_MAP(TranslateDlg): no entries, ResDlg::OnCommand calls OnOK/OnCancel

/////////////////////////////////////////////////////////////////////////////
// TranslateDlg message handlers

void TranslateDlg::OnOK() 
{
	UpdateData();
	mesh->Translate (m_Translatex, m_Translatey, m_Translatez);
	ResDlg::OnOK();
}
/////////////////////////////////////////////////////////////////////////////
// RotateDlg dialog


RotateDlg::RotateDlg(Mesh *_mesh, QWidget* pParent /*=NULL*/)
	: ResDlg(RotateDlg::IDD, pParent), mesh(_mesh)
{
	//{{AFX_DATA_INIT(RotateDlg)
	m_Rotx = 0.0;
	m_Roty = 0.0;
	m_Rotz = 0.0;
	//}}AFX_DATA_INIT
}


void RotateDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(RotateDlg)
	ExchangeText(bSaveAndValidate, IDC_ROTX, m_Rotx);
	ValidateMinMaxDouble(bSaveAndValidate, m_Rotx, -360., 360.);
	ExchangeText(bSaveAndValidate, IDC_ROTY, m_Roty);
	ValidateMinMaxDouble(bSaveAndValidate, m_Roty, -360., 360.);
	ExchangeText(bSaveAndValidate, IDC_ROTZ, m_Rotz);
	ValidateMinMaxDouble(bSaveAndValidate, m_Rotz, -360., 360.);
	//}}AFX_DATA_MAP
}


// message map: WM_COMMAND from the controls, connected in ResDlg (oapiConnectDlgCommands)
BOOL RotateDlg::OnCommand(int nID, int nCode)
{
	//{{AFX_MSG_MAP(RotateDlg)
	if (nCode == RESN_CLICKED) switch (nID) { // ON_BN_CLICKED
	case IDC_DO_ROTX:  OnDoRotx(); return TRUE;
	case IDC_DO_ROTY:  OnDoRoty(); return TRUE;
	case IDC_DO_ROTZ:  OnDoRotz(); return TRUE;
	}
	//}}AFX_MSG_MAP
	return ResDlg::OnCommand(nID, nCode);
}

/////////////////////////////////////////////////////////////////////////////
// RotateDlg message handlers

void RotateDlg::OnDoRotx() 
{
	UpdateData();
	mesh->Rotate (mesh->ROTATE_X, (float)(RAD*m_Rotx));
}

void RotateDlg::OnDoRoty() 
{
	UpdateData();
	mesh->Rotate (mesh->ROTATE_Y, (float)(RAD*m_Roty));
}

void RotateDlg::OnDoRotz() 
{
	UpdateData();
	mesh->Rotate (mesh->ROTATE_Z, (float)(RAD*m_Rotz));
}
/////////////////////////////////////////////////////////////////////////////
// ScaleDlg dialog


ScaleDlg::ScaleDlg(Mesh *_mesh, QWidget* pParent /*=NULL*/)
	: ResDlg(ScaleDlg::IDD, pParent), mesh(_mesh)
{
	//{{AFX_DATA_INIT(ScaleDlg)
	m_ScaleX = 1.0;
	m_ScaleY = 1.0;
	m_ScaleZ = 1.0;
	//}}AFX_DATA_INIT
}


void ScaleDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(ScaleDlg)
	ExchangeText(bSaveAndValidate, IDC_SCALEX, m_ScaleX);
	ExchangeText(bSaveAndValidate, IDC_SCALEY, m_ScaleY);
	ExchangeText(bSaveAndValidate, IDC_SCALEZ, m_ScaleZ);
	//}}AFX_DATA_MAP
}


// BEGIN_MESSAGE_MAP(ScaleDlg): no entries, ResDlg::OnCommand calls OnOK/OnCancel

/////////////////////////////////////////////////////////////////////////////
// ScaleDlg message handlers

void ScaleDlg::OnOK() 
{
	UpdateData();
	mesh->Scale ((float)m_ScaleX, (float)m_ScaleY, (float)m_ScaleZ);
	ResDlg::OnOK();
}
/////////////////////////////////////////////////////////////////////////////
// ZerolevelDlg dialog


ZerolevelDlg::ZerolevelDlg(Mesh *_mesh, QWidget* pParent /*=NULL*/)
	: ResDlg(ZerolevelDlg::IDD, pParent), mesh(_mesh)
{
	//{{AFX_DATA_INIT(ZerolevelDlg)
	m_Zlevel = 1e-5f;
	m_ResetVtx = FALSE;
	m_ResetNml = FALSE;
	m_ResetTex = FALSE;
	//}}AFX_DATA_INIT
}


void ZerolevelDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(ZerolevelDlg)
	ExchangeText(bSaveAndValidate, IDC_ZEROLEVEL, m_Zlevel);
	ValidateMinMaxFloat(bSaveAndValidate, m_Zlevel, 0.f, 1.f);
	ExchangeCheck(bSaveAndValidate, IDC_ZERO_VTX, m_ResetVtx);
	ExchangeCheck(bSaveAndValidate, IDC_ZERO_NML, m_ResetNml);
	ExchangeCheck(bSaveAndValidate, IDC_ZERO_TEX, m_ResetTex);
	//}}AFX_DATA_MAP
}


// BEGIN_MESSAGE_MAP(ZerolevelDlg): no entries, ResDlg::OnCommand calls OnOK/OnCancel

/////////////////////////////////////////////////////////////////////////////
// ZerolevelDlg message handlers

void ZerolevelDlg::OnOK() 
{
	UpdateData();
	int which = 0;
	if (m_ResetVtx) which |= 1;
	if (m_ResetNml) which |= 2;
	if (m_ResetTex) which |= 4;
	mesh->ZeroThreshold (m_Zlevel, which);
	ResDlg::OnOK();
}
/////////////////////////////////////////////////////////////////////////////
// MergeDlg dialog


MergeDlg::MergeDlg(Mesh *_mesh, QWidget* pParent /*=NULL*/)
	: ResDlg(MergeDlg::IDD, pParent), mesh(_mesh)
{
	//{{AFX_DATA_INIT(MergeDlg)
	m_Grp1 = 1;
	m_Grp2 = 1;
	m_Label1 = "";
	m_Label2 = "";
	//}}AFX_DATA_INIT
}


void MergeDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	UINT maxgrp = (UINT)mesh->nGroup(), maxgrp1 = maxgrp+1;
	char cbuf1[32], cbuf2[32];
	sprintf (cbuf1, "Merge group (1-%d)", maxgrp);
	sprintf (cbuf2, "with group (1-%d)", maxgrp);
	m_Label1 = cbuf1;
	m_Label2 = cbuf2;
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(MergeDlg)
	ExchangeText(bSaveAndValidate, IDC_MERGE_GRP1, m_Grp1);
	ValidateMinMaxUInt(bSaveAndValidate, m_Grp1, 1, maxgrp1);
	ExchangeText(bSaveAndValidate, IDC_MERGE_GRP2, m_Grp2);
	ValidateMinMaxUInt(bSaveAndValidate, m_Grp2, 1, maxgrp1);
	ExchangeText(bSaveAndValidate, IDC_MERGE_LABEL1, m_Label1);
	ValidateMaxChars(bSaveAndValidate, m_Label1, 32);
	ExchangeText(bSaveAndValidate, IDC_MERGE_LABEL2, m_Label2);
	ValidateMaxChars(bSaveAndValidate, m_Label2, 32);
	//}}AFX_DATA_MAP
}


// BEGIN_MESSAGE_MAP(MergeDlg): no entries, ResDlg::OnCommand calls OnOK/OnCancel

/////////////////////////////////////////////////////////////////////////////
// MergeDlg message handlers

void MergeDlg::OnOK() 
{
	UpdateData();
	if (m_Grp1 != m_Grp2) {
		DWORD grp1 = m_Grp1-1, grp2 = m_Grp2-1;
		NTVERTEX *vtx1, *vtx2, *vtx;
		WORD *idx1, *idx2, *idx;
		DWORD i, nvtx1, nvtx2, nidx1, nidx2, nvtx, nidx;
		mesh->GetGroup (grp1, vtx1, nvtx1, idx1, nidx1);
		mesh->GetGroup (grp2, vtx2, nvtx2, idx2, nidx2);
		nvtx = nvtx1 + nvtx2;
		nidx = nidx1 + nidx2;
		vtx = new NTVERTEX[nvtx];
		idx = new WORD[nidx];
		memcpy (vtx, vtx1, nvtx1*sizeof(NTVERTEX));
		memcpy (vtx+nvtx1, vtx2, nvtx2*sizeof(NTVERTEX));
		memcpy (idx, idx1, nidx1*sizeof(WORD));
		memcpy (idx+nidx1, idx2, nidx2*sizeof(WORD));
		// adjust indices
		for (i = nidx1; i < nidx; i++)
			idx[i] += (WORD)nvtx1;
		mesh->DeleteGroup (grp1 < grp2 ? grp2 : grp1);
		mesh->DeleteGroup (grp1 < grp2 ? grp1 : grp2);
		mesh->AddGroup (vtx, nvtx, idx, nidx);
	}

	ResDlg::OnOK();
}
/////////////////////////////////////////////////////////////////////////////
// NormalDlg dialog


NormalDlg::NormalDlg(Mesh *_mesh, QWidget* pParent /*=NULL*/)
	: ResDlg(NormalDlg::IDD, pParent), mesh(_mesh)
{
	//{{AFX_DATA_INIT(NormalDlg)
	m_Selgrp = 0;
	m_Selvtx = 0;
	m_Group = 1;
	//}}AFX_DATA_INIT
}


void NormalDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	UINT maxgrp = (UINT)mesh->nGroup(), maxgrp1 = maxgrp+1;
	char cbuf[32];
	sprintf (cbuf, "Only for group (1-%d)", maxgrp);
	oapiSetDlgText (GetDlgItem (IDC_NML_SELONE), cbuf);
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(NormalDlg)
	ExchangeRadio(bSaveAndValidate, IDC_NML_SELALL, m_Selgrp);
	ExchangeRadio(bSaveAndValidate, IDC_NML_VTXALL, m_Selvtx);
	ExchangeText(bSaveAndValidate, IDC_NML_SELGRP, m_Group);
	ValidateMinMaxUInt(bSaveAndValidate, m_Group, 1, maxgrp);
	//}}AFX_DATA_MAP
}


// message map: WM_COMMAND from the controls, connected in ResDlg (oapiConnectDlgCommands)
BOOL NormalDlg::OnCommand(int nID, int nCode)
{
	//{{AFX_MSG_MAP(NormalDlg)
	if (nCode == RESN_CLICKED) switch (nID) { // ON_BN_CLICKED
	case IDC_NML_SELALL:  OnNmlSelall(); return TRUE;
	case IDC_NML_SELONE:  OnNmlSelone(); return TRUE;
	case ID_NMLAPPLY:     OnNmlapply(); return TRUE;
	}
	//}}AFX_MSG_MAP
	return ResDlg::OnCommand(nID, nCode);
}

/////////////////////////////////////////////////////////////////////////////
// NormalDlg message handlers

void NormalDlg::OnNmlSelall() 
{
	GetDlgItem (IDC_NML_SELGRP)->setEnabled (FALSE);
}

void NormalDlg::OnNmlSelone() 
{
	GetDlgItem (IDC_NML_SELGRP)->setEnabled (TRUE);
}

void NormalDlg::OnNmlapply() 
{
	UINT g1, g2, g;
	bool missing_only;

	UpdateData();
	if (m_Selgrp) g1 = m_Group-1, g2 = g1+1;
	else          g1 = 0, g2 = mesh->nGroup();
	missing_only = m_Selvtx == 1;
	for (g = g1; g < g2; g++)
		mesh->CalcNormals (g, missing_only);
	theApp.InitMesh();
}

/////////////////////////////////////////////////////////////////////////////
// MirrorDlg dialog


MirrorDlg::MirrorDlg(Mesh *_mesh, QWidget* pParent /*=NULL*/)
	: ResDlg(MirrorDlg::IDD, pParent), mesh(_mesh)
{
	//{{AFX_DATA_INIT(MirrorDlg)
	m_MirrorX = 0;
	//}}AFX_DATA_INIT
}


void MirrorDlg::DoDataExchange(BOOL bSaveAndValidate)
{
	ResDlg::DoDataExchange(bSaveAndValidate);
	//{{AFX_DATA_MAP(MirrorDlg)
	ExchangeRadio(bSaveAndValidate, IDC_MIRRORX, m_MirrorX);
	//}}AFX_DATA_MAP
}


// BEGIN_MESSAGE_MAP(MirrorDlg): no entries, ResDlg::OnCommand calls OnOK/OnCancel

/////////////////////////////////////////////////////////////////////////////
// MirrorDlg message handlers

void MirrorDlg::OnOK() 
{
	UpdateData();
	switch (m_MirrorX) {
	case 0: mesh->Mirror (Mesh::MIRROR_X); break;
	case 1: mesh->Mirror (Mesh::MIRROR_Y); break;
	case 2: mesh->Mirror (Mesh::MIRROR_Z); break;
	}
	ResDlg::OnOK();
}
