// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2009, 2012-2016, 2018-2019, 2022, 2025-2026 - TortoiseGit

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
#include "SubmoduleCommand.h"
#include "MessageBox.h"
#include "Git.h"
#include "SubmoduleAddDlg.h"
#include "SubmoduleUpdateDlg.h"
#include "ProgressDlg.h"
#include "AppUtils.h"
#include "MassiveGitTaskBase.h"

bool SubmoduleAddCommand::Execute()
{
	bool bRet = false;
	CSubmoduleAddDlg dlg;
	dlg.m_strPath = cmdLinePath.GetDirectory().GetWinPathString().c_str();
	dlg.m_strProject = g_Git.m_CurrentDir;
	if( dlg.DoModal() == IDOK )
	{
		if (CStringUtils::StartsWith(dlg.m_strPath, g_Git.m_CurrentDir))
			dlg.m_strPath = dlg.m_strPath.Right(dlg.m_strPath.GetLength()-g_Git.m_CurrentDir.GetLength()-1);

		STRING_VECTOR cmd{ L"git.exe", L"submodule", L"add" };
		// Note the "=" rather than "+=" below in the original: --force *replaced*
		// the branch option rather than adding to it. Preserved.
		if (dlg.m_bForce)
			cmd.emplace_back(L"--force");
		else if (dlg.m_bBranch)
			cmd.insert(cmd.cend(), { L"-b", std::wstring(dlg.m_strBranch) });
		cmd.insert(cmd.cend(), { L"--", std::wstring(dlg.m_strRepos), std::wstring(dlg.m_strPath) });

		CProgressDlg progress;
		progress.m_GitCmd = cmd;
		progress.DoModal();

		bRet = TRUE;
	}
	return bRet;
}

bool SubmoduleUpdateCommand::Execute()
{
	CString bkpath;
	if (parser.HasKey(L"bkpath"))
		bkpath = parser.GetVal(L"bkpath");
	else
	{
		bkpath = this->orgPathList[0].GetWinPathString().c_str();
		int start = bkpath.ReverseFind(L'\\');
		if (start >= 0)
			bkpath = bkpath.Left(start);
	}

	CString super = GitAdminDir::GetSuperProjectRoot(bkpath);
	if (super.IsEmpty())
	{
		CMessageBox::Show(GetExplorerHWND(), IDS_ERR_NOTFOUND_SUPER_PRJECT, IDS_APPNAME, MB_OK | MB_ICONERROR);
		//change current project root to super project
		return false;
	}

	STRING_VECTOR pathFilterList;
	for (int i = 0; i < orgPathList.GetCount(); i++)
	{
		if (orgPathList[i].IsDirectory())
		{
			CString path = orgPathList[i].GetSubPath(CTGitPath(super.GetString())).GetGitPathString().c_str();
			if (!path.IsEmpty())
				pathFilterList.push_back(std::wstring(path));
		}
	}

	CSubmoduleUpdateDlg submoduleUpdateDlg;
	submoduleUpdateDlg.m_PathFilterList = pathFilterList;
	if (parser.HasKey(L"selectedpath"))
	{
		CString selectedPath = parser.GetVal(L"selectedpath");
		selectedPath.Replace(L'\\', L'/');
		submoduleUpdateDlg.m_PathList.push_back(std::wstring(selectedPath));
	}
	if (submoduleUpdateDlg.DoModal() != IDOK)
		return false;

	CProgressDlg progress;
	theApp.m_pMainWnd = &progress;

	g_Git.SetCurrentDirExact(super);

	STRING_VECTOR cmd{ L"git.exe", L"submodule", L"update", L"--progress" };
	if (submoduleUpdateDlg.m_bInit)
		cmd.emplace_back(L"--init");
	if (submoduleUpdateDlg.m_bRecursive)
		cmd.emplace_back(L"--recursive");
	if (submoduleUpdateDlg.m_bForce)
		cmd.emplace_back(L"--force");
	if (submoduleUpdateDlg.m_bNoFetch)
		cmd.emplace_back(L"--no-fetch");
	if (submoduleUpdateDlg.m_bMerge)
		cmd.emplace_back(L"--merge");
	if (submoduleUpdateDlg.m_bRebase)
		cmd.emplace_back(L"--rebase");
	if (submoduleUpdateDlg.m_bRemote)
		cmd.emplace_back(L"--remote");

	if (!submoduleUpdateDlg.m_bAllSubmodulesSelected)
	{
		// If not all submodules are selected, let CMassiveGitTaskBase create the list of commands.
		// Otherwise, there is no need to specify any submodule.
		STRING_VECTOR base = cmd;
		base.emplace_back(L"--");
		CMassiveGitTaskBase::ConvertToCmdList(base, submoduleUpdateDlg.m_PathList, progress.m_GitCmdList);
	}
	else
		progress.m_GitCmdList.push_back(cmd);

	progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
	{
		if (status)
			return;

		CTGitPath gitPath(g_Git.m_CurrentDir.GetString());
		if (gitPath.IsBisectActive())
		{
			postCmdList.emplace_back(IDI_THUMB_UP, IDS_MENUBISECTGOOD, [] { CAppUtils::RunTortoiseGitProc(L"/command:bisect /good"); });
			postCmdList.emplace_back(IDI_THUMB_DOWN, IDS_MENUBISECTBAD, [] { CAppUtils::RunTortoiseGitProc(L"/command:bisect /bad"); });
			postCmdList.emplace_back(IDI_BISECT, IDS_MENUBISECTSKIP, [] { CAppUtils::RunTortoiseGitProc(L"/command:bisect /skip"); });
			postCmdList.emplace_back(IDI_BISECT_RESET, IDS_MENUBISECTRESET, [] { CAppUtils::RunTortoiseGitProc(L"/command:bisect /reset"); });
		}
	};

	progress.DoModal();

	return !progress.m_GitStatus;
}

bool SubmoduleSyncCommand::Execute()
{
	CProgressDlg progress;
	theApp.m_pMainWnd = &progress;

	CString bkpath;

	if (parser.HasKey(L"bkpath"))
		bkpath=parser.GetVal(L"bkpath");
	else
	{
		bkpath=this->orgPathList[0].GetWinPathString().c_str();
		int start = bkpath.ReverseFind(L'\\');
		if( start >= 0 )
			bkpath=bkpath.Left(start);
	}

	CString super = GitAdminDir::GetSuperProjectRoot(bkpath);
	if(super.IsEmpty())
	{
		CMessageBox::Show(GetExplorerHWND(), IDS_ERR_NOTFOUND_SUPER_PRJECT, IDS_APPNAME, MB_OK | MB_ICONERROR);
		//change current project root to super project
		return false;
	}

	g_Git.SetCurrentDirExact(super);

	for (int i = 0; i < this->orgPathList.GetCount(); ++i)
	{
		if(orgPathList[i].IsDirectory())
		{
			STRING_VECTOR str{ L"git.exe", L"submodule", L"sync" };
			if (const std::wstring path = orgPathList[i].GetSubPath(CTGitPath(super.GetString())).GetGitPathString(); !path.empty())
				str.insert(str.cend(), { L"--", path });
			progress.m_GitCmdList.push_back(str);
		}
	}

	progress.DoModal();

	return !progress.m_GitStatus;
}
