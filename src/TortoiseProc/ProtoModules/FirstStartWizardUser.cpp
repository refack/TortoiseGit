// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2016-2017, 2019-2021, 2025 - TortoiseGit

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
#include "std.h"

#include "TortoiseProc.h"
#include "FirstStartWizard.h"
#include "FirstStartWizardUser.h"
import ThemeAPI;

IMPLEMENT_DYNAMIC(CFirstStartWizardUser, CFirstStartWizardBasePage)

CFirstStartWizardUser::CFirstStartWizardUser() : CFirstStartWizardBasePage(CFirstStartWizardUser::IDD)
, m_bNoSave(FALSE)
{
	m_psp.dwFlags |= PSP_DEFAULT | PSP_USEHEADERTITLE;
	m_psp.dwFlags &= ~PSP_HASHELP;
	m_psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_FIRSTSTART_USERTITLE);
}

CFirstStartWizardUser::~CFirstStartWizardUser()
{
}

void CFirstStartWizardUser::DoDataExchange(CDataExchange* pDX)
{
	CFirstStartWizardBasePage::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_DONTSAVE, m_bNoSave);
	DDX_Text(pDX, IDC_GIT_USERNAME, m_sUsername);
	DDX_Text(pDX, IDC_GIT_USEREMAIL, m_sUseremail);
}

BEGIN_MESSAGE_MAP(CFirstStartWizardUser, CFirstStartWizardBasePage)
	ON_BN_CLICKED(IDC_DONTSAVE, OnClickedNoSave)
END_MESSAGE_MAP()

void CFirstStartWizardUser::OnClickedNoSave()
{
	UpdateData();
	GetDlgItem(IDC_GIT_USERNAME)->EnableWindow(!m_bNoSave);
	GetDlgItem(IDC_GIT_USEREMAIL)->EnableWindow(!m_bNoSave);
}

LRESULT CFirstStartWizardUser::OnWizardNext()
{
	UpdateData();

	if (!m_bNoSave)
	{
		if (m_sUsername.Trim().IsEmpty() || m_sUseremail.Trim().IsEmpty())
		{
			CMessageBox::Show(GetSafeHwnd(), IDC_PROC_USERNAMEMAIL_EMPTY, IDS_APPNAME, MB_ICONERROR);
			return -1;
		}

		wil::unique_git_config config;
		git_config_new(config.put());
		int err = git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalXDGConfig().native()).c_str(), GIT_CONFIG_LEVEL_XDG, nullptr, FALSE);
		std::error_code ec;
		if (!err && (fs::exists(TGitH::GetGitGlobalConfig(), ec) || !fs::exists(TGitH::GetGitGlobalXDGConfig(), ec)))
			err = git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalConfig().native()).c_str(), GIT_CONFIG_LEVEL_GLOBAL, nullptr, FALSE);
		if (err)
		{
			MessageBox(TGitH::GetLibGit2LastErr(), L"TortoiseGit", MB_ICONEXCLAMATION);
			return -1;
		}
		if (git_config_set_string(config.get(), "user.name", CUnicodeUtils::GetUTF8<CStringA>(m_sUsername.Trim())))
		{
			MessageBox(TGitH::GetLibGit2LastErr(), L"TortoiseGit", MB_ICONEXCLAMATION);
			return -1;
		}
		if (git_config_set_string(config.get(), "user.email", CUnicodeUtils::GetUTF8<CStringA>(m_sUseremail.Trim())))
		{
			MessageBox(TGitH::GetLibGit2LastErr(), L"TortoiseGit", MB_ICONEXCLAMATION);
			return -1;
		}
	}

	return __super::OnWizardNext();
}

BOOL CFirstStartWizardUser::OnInitDialog()
{
	CFirstStartWizardBasePage::OnInitDialog();

	AdjustControlSize(IDC_DONTSAVE);

	wil::unique_git_config config;
	git_config_new(config.put());
	if (git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalConfig().native()).c_str(), GIT_CONFIG_LEVEL_GLOBAL, nullptr, FALSE))
		MessageBox(TGitH::GetLibGit2LastErr(), L"TortoiseGit", MB_ICONEXCLAMATION);
	else
	{
		if (git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalXDGConfig().native()).c_str(), GIT_CONFIG_LEVEL_XDG, nullptr, FALSE))
			MessageBox(TGitH::GetLibGit2LastErr(), L"TortoiseGit", MB_ICONEXCLAMATION);
	}
	if (git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitSystemConfig().native()).c_str(), GIT_CONFIG_LEVEL_SYSTEM, nullptr, FALSE))
		MessageBox(TGitH::GetLibGit2LastErr(), L"TortoiseGit", MB_ICONEXCLAMATION);

	TGitH::GetConfigString(config.get(), L"user.name", m_sUsername);
	TGitH::GetConfigString(config.get(), L"user.email", m_sUseremail);

	UpdateData(FALSE);

	CTheme::Instance().SetThemeForDialog(GetSafeHwnd(), CTheme::Instance().IsDarkTheme());

	return TRUE;
}

BOOL CFirstStartWizardUser::OnSetActive()
{
	auto wiz = static_cast<CFirstStartWizard*>(GetParent());

	wiz->SetWizardButtons(PSWIZB_NEXT | PSWIZB_BACK);

	return CFirstStartWizardBasePage::OnSetActive();
}
