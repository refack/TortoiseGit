// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2009, 2015-2016, 2018-2019, 2023, 2026 - TortoiseGit
// Copyright (C) 2007-2008 - TortoiseSVN

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
#include "FormatPatchCommand.h"
#include "FormatPatchDlg.h"
#include "Git.h"
#include "ShellUpdater.h"
#include "ProgressDlg.h"
#include "AppUtils.h"

bool FormatPatchCommand::Execute()
{
	CFormatPatchDlg dlg;
	CString startval = parser.GetVal(L"startrev");
	CString endval = parser.GetVal(L"endrev");

	if( endval.IsEmpty() && (!startval.IsEmpty()))
	{
		dlg.m_Since=startval;
		dlg.m_From = startval + L"~1";
		dlg.m_To = startval;
		dlg.m_Radio = IDC_RADIO_SINCE;
	}
	else if( (!endval.IsEmpty()) && (!startval.IsEmpty()))
	{
		dlg.m_From=startval;
		dlg.m_To=endval;
		dlg.m_Radio = IDC_RADIO_RANGE;
	}

	if(dlg.DoModal()==IDOK)
	{
		STRING_VECTOR cmd{ L"git.exe", L"format-patch" };
		if (dlg.m_bNoPrefix)
			cmd.emplace_back(L"--no-prefix");
		cmd.insert(cmd.cend(), { L"-o", std::wstring(dlg.m_Dir) });

		switch (dlg.m_Radio)
		{
		case IDC_RADIO_SINCE:
			cmd.insert(cmd.cend(), { L"--end-of-options", std::wstring(g_Git.FixBranchName(dlg.m_Since)) });
			break;
		case IDC_RADIO_NUM:
			cmd.emplace_back(std::format(L"-{}", dlg.m_Num));
			break;
		case IDC_RADIO_RANGE:
			cmd.insert(cmd.cend(), { L"--end-of-options", std::format(L"{}..{}", dlg.m_From, dlg.m_To) });
			break;
		}
		cmd.emplace_back(L"--");

		CProgressDlg progress;
		progress.m_GitCmd = cmd;
		progress.DoModal();

		CShellUpdater::Instance().AddPathForUpdate(CTGitPath(dlg.m_Dir.GetString()));
		CShellUpdater::Instance().Flush();

		if(!progress.m_GitStatus)
		{
			if(dlg.m_bSendMail)
				// SendPatchMail looks for the echoed command line in the log output
				// and skips past it, so it needs the same spelling CProgressDlg logged.
				CAppUtils::SendPatchMail(GetExplorerHWND(), CGit::SerializeArgvToCString(cmd), progress.m_LogText);
		}
		return !progress.m_GitStatus;
	}
	return FALSE;
}
