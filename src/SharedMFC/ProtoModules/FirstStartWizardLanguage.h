// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2016, 2023-2024 - TortoiseGit

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
#include "FirstStartWizardBasePage.h"
#include "HyperLink.h"
import Registry;
import LangDll;

/**
 * First page in the first start wizard
 */
class CFirstStartWizardLanguage : public CFirstStartWizardBasePage
{
	DECLARE_DYNAMIC(CFirstStartWizardLanguage)

	CFirstStartWizardLanguage()
	: CFirstStartWizardBasePage(CFirstStartWizardLanguage::IDD)
	, m_dwLanguage(LangDll::s_defaultLang)
	, m_regLanguage(L"Software\\TortoiseGit\\LanguageID", LangDll::s_defaultLang)
	{
		m_dwInitialLanguage = m_regLanguage;
		m_psp.dwFlags |= PSP_DEFAULT | PSP_USEHEADERTITLE;
		m_psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_FIRSTSTART_STARTTITLE);
	}

	enum { IDD = IDD_FIRSTSTARTWIZARD_LANGUAGE };

protected:
	void	DoDataExchange(CDataExchange* pDX) override;
	BOOL	OnInitDialog() override;
	BOOL	OnSetActive() override;
	LRESULT	OnWizardNext() override;
	afx_msg LRESULT	OnDialogDisplayed(WPARAM wParam, LPARAM lParam);
	afx_msg void	OnClickedLink(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void	OnBnClickedRefresh();

	DECLARE_MESSAGE_MAP()

	CComboBox		m_LanguageCombo;
	CRegDWORD		m_regLanguage{};
	DWORD			m_dwLanguage{};
	DWORD			m_dwInitialLanguage{};

	CHyperLink		m_link;
};
