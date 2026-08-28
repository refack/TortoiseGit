// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2013, 2015-2019, 2021, 2026 - TortoiseGit
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
#include "DropCopyCommand.h"
#include "SysProgressDlg.h"
#include "MessageBox.h"
#include "RenameDlg.h"
#include "Git.h"
#include "ShellUpdater.h"

bool DropCopyCommand::Execute()
{
	CString sDroppath = parser.GetVal(L"droptarget");
	if (CTGitPath(sDroppath.GetString()).IsAdminDir())
	{
		MessageBox(GetExplorerHWND(), L"Can't drop to .git repository directory\n", L"TortoiseGit", MB_OK | MB_ICONERROR);
		return FALSE;
	}
	unsigned long count = 0;

	CString sNewName;
	pathList.RemoveAdminPaths();
	if (parser.HasKey(L"rename") && pathList.GetCount() == 1)
	{
		// ask for a new name of the source item
		CRenameDlg renDlg;
		renDlg.SetInputValidator([&](const int /*nID*/, const CString& input) -> CString
		{
			if (PathFileExists(sDroppath + L'\\' + input))
				return CString(static_cast<LPCWSTR>(CFormatMessageWrapper(ERROR_FILE_EXISTS)));

			return{};
		});
		renDlg.m_sBaseDir = sDroppath;
		renDlg.m_windowtitle.LoadString(IDS_PROC_COPYRENAME);
		renDlg.m_name = pathList[0].GetFileOrDirectoryName().c_str();
		if (renDlg.DoModal() != IDOK)
			return FALSE;
		sNewName = renDlg.m_name;
	}
	CSysProgressDlg progress;
	progress.SetTitle(IDS_PROC_COPYING);
	progress.SetTime(true);
	progress.ShowModeless(CWnd::FromHandle(GetExplorerHWND()));
	for (int nPath = 0; nPath < pathList.GetCount(); ++nPath)
	{
		const CTGitPath& sourcePath = orgPathList[nPath];

		CTGitPath fullDropPath(sDroppath.GetString());

		if (sNewName.IsEmpty())
			fullDropPath.AppendPathString(sourcePath.GetFileOrDirectoryName().c_str());
		else
			fullDropPath.AppendPathString(sNewName.GetString());

		// Check for a drop-on-to-ourselves
		if (sourcePath.IsEquivalentTo(fullDropPath))
		{
			// Offer a rename
			progress.Stop();
			CRenameDlg dlg;
			dlg.SetInputValidator([&](const int /*nID*/, const CString& input) -> CString
			{
				CTGitPath newPath(sDroppath.GetString());
				newPath.AppendPathString(input.GetString());
				if (newPath.Exists())
					return CString(static_cast<LPCWSTR>(CFormatMessageWrapper(ERROR_FILE_EXISTS)));

				return{};
			});
			dlg.m_sBaseDir = fullDropPath.GetContainingDirectory().GetWinPathString().c_str();
			dlg.m_name = fullDropPath.GetFileOrDirectoryName().c_str();
			dlg.m_windowtitle.Format(IDS_PROC_NEWNAMECOPY, static_cast<LPCWSTR>(sourcePath.GetUIFileOrDirectoryName().c_str()));
			if (dlg.DoModal() != IDOK)
				return FALSE;
			// rebuild the progress dialog
			progress.EnsureValid();
			progress.SetTitle(IDS_PROC_COPYING);
			progress.SetTime(true);
			progress.SetProgress(count, pathList.GetCount());
			progress.ShowModeless(CWnd::FromHandle(GetExplorerHWND()));
			// Rebuild the destination path, with the new name
			fullDropPath.SetFromUnknown(sDroppath.GetString());
			fullDropPath.AppendPathString(dlg.m_name.GetString());
		}

		if( CopyFile( sourcePath.GetWinPath(), fullDropPath.GetWinPath(), true))
		{
			std::wstring ProjectTopDir;
			if(fullDropPath.HasAdminDir(&ProjectTopDir))
			{
				g_Git.SetCurrentDir(ProjectTopDir.c_str());
				SetCurrentDirectory(ProjectTopDir.c_str());
				CString path;
				path = tgit::wstr::Mid(fullDropPath.GetGitPathString(), static_cast<int>(ProjectTopDir.size())).c_str();
				if (!path.IsEmpty() && (path[0] == L'\\' || path[0] == L'/'))
					path = path.Mid(1);

				CString output;
				if (g_Git.Run({ L"git.exe", L"add", L"--", std::wstring(path) }, &output, CP_UTF8))
					MessageBox(GetExplorerHWND(), output, L"TortoiseGit", MB_OK | MB_ICONERROR);
				else
					CShellUpdater::Instance().AddPathForUpdate(fullDropPath);
			}

		}else
		{
			CString str;
			str += L"Copy from \"";
			str += sourcePath.GetWinPathString().c_str();
			str += L"\" to \"";
			str += fullDropPath.GetWinPathString().c_str();
			str += L"\" failed:\n";
			str += static_cast<LPCWSTR>(CFormatMessageWrapper());

			MessageBox(GetExplorerHWND(), str, L"TortoiseGit", MB_OK | MB_ICONERROR);
		}

		++count;
		if (progress.IsValid())
		{
			progress.FormatPathLine(1, IDS_PROC_COPYINGPROG, sourcePath.GetWinPath());
			progress.FormatPathLine(2, IDS_PROC_CPYMVPROG2, fullDropPath.GetWinPath());
			progress.SetProgress(count, pathList.GetCount());
		}
		if ((progress.IsValid())&&(progress.HasUserCancelled()))
		{
			CMessageBox::Show(GetExplorerHWND(), IDS_USERCANCELLED, IDS_APPNAME, MB_ICONINFORMATION);
			return false;
		}
	}

	return true;
}
