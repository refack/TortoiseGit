// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2019, 2021-2026 - TortoiseGit
// Copyright (C) 2012 - TortoiseSVN

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
#include "CloneCommand.h"
#include "GitProgressDlg.h"
#include "StringUtils.h"
#include "CloneDlg.h"
#include "ProgressDlg.h"
#include "AppUtils.h"
#include "UnicodeUtils.h"
#include "ProgressCommands/CloneProgressCommand.h"
#include "CmdLineParser.h"

static CString GetExistingDirectoryForClone(CString path)
{
	if (PathFileExists(path))
		return path;
	int index = path.ReverseFind('\\');
	while (index >= 0 && path.GetLength() >= 3)
	{
		if (PathFileExists(path.Left(index)))
		{
			if (index == 2 && path[1] == L':')
				return path.Left(index + 1);
			return path.Left(index);
		}
		path = path.Left(index);
		index = path.ReverseFind('\\');
	}
	GetTempPath(path);
	return path;
}

bool CloneCommand::Execute()
{
	CTGitPath cloneDirectory;
	if (!parser.HasKey(L"hasurlhandler"))
	{
		if (orgCmdLinePath.IsEmpty())
		{
			cloneDirectory.SetFromWin(CPathUtils::GetCWD(), true);
		}
		else
			cloneDirectory = orgCmdLinePath;
	}

	CCloneDlg dlg;
	dlg.m_Directory = cloneDirectory.GetWinPathString().c_str();

	if (parser.HasKey(L"url"))
		dlg.m_URL = parser.GetVal(L"url");
	if (parser.HasKey(L"exactpath"))
		dlg.m_bExactPath = TRUE;

	if(dlg.DoModal()==IDOK)
	{
		STRING_VECTOR args;
		if(dlg.m_bRecursive)
			args.emplace_back(L"--recursive");

		if(dlg.m_bBare)
			args.emplace_back(L"--bare");

		if (dlg.m_bNoCheckout)
			args.emplace_back(L"--no-checkout");

		if (dlg.m_bBranch)
			args.insert(args.cend(), { L"--branch", std::wstring(dlg.m_strBranch) });

		if (dlg.m_bOrigin && !dlg.m_bSVN)
			args.insert(args.cend(), { L"--origin", std::wstring(dlg.m_strOrigin) });

		CString dir=dlg.m_Directory;
		CString url=dlg.m_URL;

		// is this a windows format UNC path, ie starts with \\?
		if (CStringUtils::StartsWith(url, L"\\\\"))
		{
			// yes, change all \ to /
			// this should not be necessary but msysgit does not support the use \ here yet
			int atSign = url.Find(L'@');
			if (atSign > 0)
			{
				CString path = url.Mid(atSign);
				path.Replace(L'\\', L'/');
				url = url.Left(atSign) + path;
			}
			else
				url.Replace( L'\\', L'/');
		}

		if (dlg.m_bDepth)
			args.insert(args.cend(), { L"--depth", std::to_wstring(dlg.m_nDepth) });

		STRING_VECTOR cmd{ L"git.exe", L"clone", L"--progress" };
		cmd.insert(cmd.cend(), args.cbegin(), args.cend());
		cmd.insert(cmd.cend(), { L"-v", L"--", std::wstring(url), std::wstring(dir) });

		bool retry = false;
		auto postCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
		{
			if (status)
			{
				postCmdList.emplace_back(IDI_REFRESH, IDS_MSGBOX_RETRY, [&]{ retry = true; });
				return;
			}

			if (dlg.m_bBare)
				CAppUtils::SetupBareRepoIcon(dir);

			// After cloning, change current directory to the cloned directory
			g_Git.SetCurrentDirExact(dlg.m_Directory);

			postCmdList.emplace_back(IDI_LOG, IDS_MENULOG, [&]
			{
				CString cmd = L"/command:log";
				cmd += L" /path:" + CCmdLineParser::EscapeValue(dlg.m_Directory);
				CAppUtils::RunTortoiseGitProc(cmd);
			});

			postCmdList.emplace_back(IDI_EXPLORER, IDS_STATUSLIST_CONTEXT_EXPLORE, [&]{ CAppUtils::ExploreTo(GetExplorerHWND(), dlg.m_Directory); });
		};

		// Handle Git SVN-clone
		if(dlg.m_bSVN)
		{
			// git-svn requires some mangling: \ -> /
			if (!PathIsURL(url))
			{
				url.Replace(L'\\', L'/');
				if (PathIsUNC(url))
					url = L"file:" + url;
				else if (PathIsDirectory(url))
				{
					// prefix: file:///, and no colon after drive letter for normal paths
					if (url.GetLength() > 2 && url.GetAt(1) == L':')
						url.Delete(1, 1);
					url = L"file:///" + url;
				}
			}

			//g_Git.SetCurrentDirExact(dlg.m_Directory);
			cmd = { L"git.exe", L"svn", L"clone" };
			if (dlg.m_bOrigin)
			{
				// An empty prefix is meaningful here - it used to be spelled
				// QuoteParameter(L""), which produced an explicit "" argument.
				cmd.insert(cmd.cend(), { L"--prefix", dlg.m_strOrigin.IsEmpty() ? std::wstring() : std::format(L"{}/", dlg.m_strOrigin) });
			}

			if (dlg.m_bSVNTrunk)
				cmd.insert(cmd.cend(), { L"-T", std::wstring(dlg.m_strSVNTrunk) });

			if (dlg.m_bSVNBranch)
				cmd.insert(cmd.cend(), { L"-b", std::wstring(dlg.m_strSVNBranchs) });

			if (dlg.m_bSVNTags)
				cmd.insert(cmd.cend(), { L"-t", std::wstring(dlg.m_strSVNTags) });

			if (dlg.m_bSVNFrom)
				cmd.insert(cmd.cend(), { L"-r", std::format(L"{}:HEAD", dlg.m_nSVNFrom) });

			if (dlg.m_bSVNUserName)
				cmd.insert(cmd.cend(), { L"--username", std::wstring(dlg.m_strUserName) });

			cmd.insert(cmd.cend(), { L"--", std::wstring(url), std::wstring(dlg.m_Directory) });
		}
		else
		{
			if (g_Git.UsingLibGit2(CGit::GIT_CMD_CLONE))
			{
				while (true)
				{
					retry = false;
					CGitProgressDlg GitDlg;
					CTGitPathList list;
					g_Git.SetCurrentDirExact(GetExistingDirectoryForClone(dlg.m_Directory));
					list.AddPath(CTGitPath(dir.GetString()));
					CloneProgressCommand cloneProgressCommand;
					GitDlg.SetCommand(&cloneProgressCommand);
					cloneProgressCommand.m_PostCmdCallback = postCmdCallback;
					cloneProgressCommand.SetUrl(url);
					cloneProgressCommand.SetPathList(list);
					cloneProgressCommand.SetIsBare(dlg.m_bBare == TRUE);
					if (dlg.m_bBranch)
						cloneProgressCommand.SetRefSpec(dlg.m_strBranch);
					if (dlg.m_bOrigin)
						cloneProgressCommand.SetRemote(dlg.m_strOrigin);
					cloneProgressCommand.SetNoCheckout(dlg.m_bNoCheckout == TRUE);
					GitDlg.DoModal();
					if (!retry)
						return !GitDlg.DidErrorsOccur();
				}
			}
		}

		while (true)
		{
			retry = false;
			g_Git.SetCurrentDirExact(GetExistingDirectoryForClone(dlg.m_Directory));
			CProgressDlg progress;
			progress.m_GitCmd=cmd;
			progress.m_PostCmdCallback = postCmdCallback;
			INT_PTR ret = progress.DoModal();

			if (!retry)
				return ret == IDOK;
		}
	}
	return FALSE;
}
