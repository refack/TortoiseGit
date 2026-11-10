// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2016-2020, 2024-2025 - TortoiseGit

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

#include "stdafx.h"
#include "FirstStartWizard.h"
#include "FirstStartWizardLanguage.h"

import tgit;
import TortoiseProc;
import ThemeAPI;
import AppUtils.Launch;
import LangDll;

constexpr auto DOWNLOAD_URL = L"https://tortoisegit.org/download/"sv;
constexpr auto WM_SETPAGEFOCUS = WM_APP+2;

IMPLEMENT_DYNAMIC(CFirstStartWizardLanguage, CFirstStartWizardBasePage)

void CFirstStartWizardLanguage::DoDataExchange(CDataExchange* pDX)
{
	CFirstStartWizardBasePage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LANGUAGECOMBO, m_LanguageCombo);
	DDX_Control(pDX, IDC_LINK, m_link);
	if (m_LanguageCombo.GetCurSel() >= 0)
		m_dwLanguage = static_cast<DWORD>(m_LanguageCombo.GetItemData(m_LanguageCombo.GetCurSel()));
}

BEGIN_MESSAGE_MAP(CFirstStartWizardLanguage, CFirstStartWizardBasePage)
	ON_MESSAGE(WM_SETPAGEFOCUS, OnDialogDisplayed)
	ON_NOTIFY(NM_CLICK, IDC_FIRSTSTART_HINT, OnClickedLink)
	ON_BN_CLICKED(IDC_REFRESH, &CFirstStartWizardLanguage::OnBnClickedRefresh)
END_MESSAGE_MAP()

static void AppendStringResource(CString& text, UINT resouceID)
{
	CString temp;
	temp.LoadString(resouceID);
	text.AppendChar(L'\n');
	text.AppendChar(L'\n');
	text.Append(temp);
}

BOOL CFirstStartWizardLanguage::OnInitDialog()
{
	CFirstStartWizardBasePage::OnInitDialog();

	CTheme::Instance().SetThemeForDialog(GetSafeHwnd(), CTheme::Instance().IsDarkTheme());

	CString hinttext;
	hinttext.LoadString(IDS_FIRSTSTART_LANGUAGEHINT1);
	AppendStringResource(hinttext, IDS_FIRSTSTART_LANGUAGEHINT2);
	AppendStringResource(hinttext, IDS_FIRSTSTART_LANGUAGEHINT3);
	GetDlgItem(IDC_FIRSTSTART_HINT)->SetWindowText(hinttext);

	GetDlgItem(IDC_LINK)->SetWindowText(DOWNLOAD_URL.data());
	m_link.SetURL(DOWNLOAD_URL.data());
	AdjustControlSize(IDC_LINK, false);

	OnBnClickedRefresh();

	return TRUE;
}

BOOL CFirstStartWizardLanguage::OnSetActive()
{
	auto wiz = static_cast<CFirstStartWizard*>(GetParent());

	wiz->SetWizardButtons(PSWIZB_NEXT);

	PostMessage(WM_SETPAGEFOCUS, 0, 0);

	return CFirstStartWizardBasePage::OnSetActive();
}

LRESULT CFirstStartWizardLanguage::OnDialogDisplayed(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	GetDlgItem(IDC_LANGUAGECOMBO)->SetFocus();

	return 0;
}

LRESULT CFirstStartWizardLanguage::OnWizardNext()
{
	UpdateData();

	if (m_dwLanguage != m_dwInitialLanguage)
	{
		m_regLanguage = m_dwLanguage;

		TortoiseProc::RunTortoiseGitProc(L"/command:firststart /page:1"sv, false, false);

		EndDialog(0);

		return -1;
	}

	return __super::OnWizardNext();
}


void CFirstStartWizardLanguage::OnClickedLink(NMHDR* pNMHDR, LRESULT* pResult)
{
	ATLASSERT(pNMHDR && pResult);
	auto *pNMLink = reinterpret_cast<PNMLINK>(pNMHDR);
	if (wcscmp(pNMLink->item.szID, L"download") == 0)
		ShellExecute(GetSafeHwnd(), L"open", DOWNLOAD_URL.data(), nullptr, nullptr, SW_SHOWNORMAL);
	*pResult = 0;
}


void CFirstStartWizardLanguage::OnBnClickedRefresh()
{
	UpdateData();
	m_LanguageCombo.ResetContent();

	// set up the language selecting combobox
	auto installed = LangDll::GetInstalledLanguages(true);
	std::for_each(installed.cbegin(), installed.cend(), [&](auto& item) {
		const int pos = m_LanguageCombo.AddString(item.first);
		m_LanguageCombo.SetItemData(pos, item.second);
	});

	m_regLanguage.read();
	m_dwLanguage = m_regLanguage;
	for (int i = 0; i < m_LanguageCombo.GetCount(); ++i)
	{
		if (m_LanguageCombo.GetItemData(i) == m_dwLanguage)
			m_LanguageCombo.SetCurSel(i);
	}
	UpdateData(FALSE);
}
