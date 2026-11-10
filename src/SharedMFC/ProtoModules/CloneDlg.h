// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2013, 2015-2017, 2021-2023 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#pragma once

#include "HorizontalResizableStandAloneDialog.h"
#include "Utils/HistoryCombo.h"
import MenuButton;
import Registry;


// CCloneDlg dialog

class CCloneDlg : public CHorizontalResizableStandAloneDialog {
	DECLARE_DYNCREATE(CCloneDlg)


	CCloneDlg(CWnd* pParent = nullptr) :
		CHorizontalResizableStandAloneDialog(CCloneDlg::IDD, pParent)
		, m_bSVN(BST_UNCHECKED)
		, m_bSVNTrunk(BST_UNCHECKED)
		, m_bSVNTags(BST_UNCHECKED)
		, m_bSVNBranch(BST_UNCHECKED)
		, m_bSVNFrom(BST_UNCHECKED)
		, m_bDepth(BST_UNCHECKED)
		, m_bSVNUserName(BST_UNCHECKED)
		, m_bBare(BST_UNCHECKED)
		, m_bRecursive(BST_UNCHECKED)
		, m_bBranch(BST_UNCHECKED)
		, m_bOrigin(BST_UNCHECKED)
		, m_bNoCheckout(BST_UNCHECKED)
		, m_bExactPath(FALSE)
		, m_nDepth(1)
		, m_strSVNTrunk(L"trunk")
		, m_strSVNTags(L"tags")
		, m_strSVNBranchs(L"branches")
		, m_nSVNFrom(0)
		, m_regBrowseUrl(L"Software\\TortoiseGit\\TortoiseProc\\CloneBrowse", 0)
		, m_regCloneRecursive(L"Software\\TortoiseGit\\TortoiseProc\\CloneRecursive", FALSE)
		, m_regCloneDir(L"Software\\TortoiseGit\\TortoiseProc\\CloneDir")
		, m_bSaving(false) {}


	~CCloneDlg() override = default;



	// Dialog Data
	enum { IDD = IDD_CLONE };

protected:
	// Overrides
	void OnOK() override;
	void OnCancel() override;

	void DoDataExchange(CDataExchange* pDX) override; // DDX/DDV support
	BOOL OnInitDialog() override;

	CString m_ModuleName;
	CString m_OldURL;

	DECLARE_MESSAGE_MAP()

	afx_msg void OnBnClickedCloneBrowseUrl();
	afx_msg void OnBnClickedCloneDirBrowse();

	CHistoryCombo m_URLCombo;

public:
	CString m_Directory;
	CString m_URL;
	CString m_strBranch;
	CString m_strOrigin;
	BOOL m_bSVN;
	BOOL m_bSVNTrunk;
	BOOL m_bSVNTags;
	BOOL m_bSVNBranch;
	BOOL m_bSVNFrom;
	BOOL m_bDepth;
	BOOL m_bSVNUserName;
	BOOL m_bBare;
	BOOL m_bRecursive;
	BOOL m_bBranch;
	BOOL m_bOrigin;
	BOOL m_bNoCheckout;
	BOOL m_bExactPath;

	int m_nDepth;
	int m_nSVNFrom;

	CString m_strSVNTrunk;
	CString m_strSVNTags;
	CString m_strSVNBranchs;
	CString m_strUserName;

protected:
	CMenuButton m_BrowseUrl;
	CRegDWORD m_regBrowseUrl;
	CRegDWORD m_regCloneRecursive;
	CRegString m_regCloneDir;
	bool m_bSaving;

	afx_msg void OnCbnEditchangeUrlcombo();

	afx_msg void OnBnClickedCheckRecursive();
	afx_msg void OnBnClickedCheckBare();
	afx_msg void OnBnClickedCheckBranch();
	afx_msg void OnBnClickedCheckOrigin();
	afx_msg void OnBnClickedCheckSvn();
	afx_msg void OnBnClickedCheckSvnTrunk();
	afx_msg void OnBnClickedCheckSvnTag();
	afx_msg void OnBnClickedCheckSvnBranch();
	afx_msg void OnBnClickedCheckSvnFrom();
	afx_msg void OnBnClickedCheckDepth();
	afx_msg void OnBnClickedCheckUsername();
};
