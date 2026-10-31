// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2009, 2011-2019, 2021, 2025-2026 - TortoiseGit
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
#include "std.h"

#include "DropMoveCommand.h"

#include "SysProgressDlg.h"
import MessageBox;
#include "RenameDlg.h"

bool DropMoveCommand::Execute()
{
	fs::path droppath = parser.GetValStr(L"droptarget");
	std::wstring ProjectTop;
	if (!CTGitPath(droppath).HasAdminDir(&ProjectTop))
		return FALSE;

	if (tgit::wstr::CompareNoCase(g_Git.m_CurrentDir.wstring(), ProjectTop) != 0) {
		CMessageBox::Show(GetExplorerHWND(), IDS_ERR_MUSTBESAMEWT, IDS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
		return FALSE;
	}

	droppath = fs::relative(droppath, ProjectTop) / "";

	unsigned long count = 0;
	pathList.RemoveAdminPaths();
	CString sNewName;

	if (parser.HasKey(L"rename") && pathList.GetCount() == 1)
	{
		// ask for a new name of the source item
		CRenameDlg renDlg;
		renDlg.SetRenameRequired(pathList[0].GetContainingDirectory().IsEquivalentToWithoutCase(CTGitPath(droppath)));
		renDlg.SetInputValidator([&](const int /*nID*/, const CString& input) -> CString
		{
			if (fs::exists(g_Git.CombinePath(droppath / input.GetString())))
				return CString((CFormatMessageWrapper(ERROR_FILE_EXISTS)).c_str());

			return{};
		});
		renDlg.m_sBaseDir = g_Git.CombinePath(droppath).c_str();
		renDlg.m_windowtitle.LoadString(IDS_PROC_MOVERENAME);
		renDlg.m_name = pathList[0].GetFileOrDirectoryName().c_str();
		if (renDlg.DoModal() != IDOK)
			return FALSE;
		sNewName = renDlg.m_name;
	}
	CSysProgressDlg progress;
	if (progress.IsValid())
	{
		progress.SetTitle(IDS_PROC_MOVING);
		progress.SetTime(true);
		std::ignore = progress.ShowModeless(CWnd::FromHandle(GetExplorerHWND()));
	}
	for (int nPath = 0; nPath < pathList.GetCount(); ++nPath)
	{
		CTGitPath destPath;
		if (sNewName.IsEmpty())
			destPath = CTGitPath(std::format(L"{}{}", droppath, pathList[nPath].GetFileOrDirectoryName()));
		else
			destPath = CTGitPath(std::format(L"{}{}", droppath, sNewName));
		if (destPath.Exists())
		{
			progress.Stop();

			const CString name = destPath.GetFileOrDirectoryName().c_str();
			progress.Stop();
			CRenameDlg dlg;
			dlg.SetInputValidator([&](const int /*nID*/, const CString& input) -> CString
			{
				if (fs::exists(g_Git.CombinePath(droppath / input.GetString())))
					return CString((CFormatMessageWrapper(ERROR_FILE_EXISTS)).c_str());

				return{};
			});
			dlg.m_sBaseDir = g_Git.CombinePath(destPath).c_str();
			dlg.m_name = name;
			dlg.m_windowtitle.Format(IDS_PROC_NEWNAMEMOVE, name);
			if (dlg.DoModal() != IDOK)
				return FALSE;

			// rebuild the progress dialog
			progress.EnsureValid();
			progress.SetTitle(IDS_PROC_MOVING);
			progress.SetTime(true);
			progress.SetProgress(count, pathList.GetCount());
			std::ignore = progress.ShowModeless(CWnd::FromHandle(GetExplorerHWND()));

			// Rebuild the destination path, with the new name
			destPath.SetFromUnknown(droppath.wstring());
			destPath.AppendPathString(dlg.m_name.GetString());
		}

		CString out;
		if (g_Git.Run({ L"git.exe", L"mv", L"--", pathList[nPath].GetGitPathString(), destPath.GetGitPathString() }, &out))
		{
			if (CMessageBox::Show(GetExplorerHWND(), out, IDS_APPNAME, 2, IDI_EXCLAMATION, IDS_IGNOREBUTTON, IDS_ABORTBUTTON) == 1)
			{
#if 0
					if (!svn.Move(CTSVNPathList(pathList[nPath]), destPath, TRUE))
					{
						CMessageBox::Show(GetExplorerHWND(), svn.GetLastErrorMessage(), L"TortoiseGit", MB_ICONERROR);
						return FALSE;		//get out of here
					}
					CShellUpdater::Instance().AddPathForUpdate(destPath);
#endif
			}
			else
			{
				CMessageBox::Show(GetExplorerHWND(), IDS_USERCANCELLED, IDS_APPNAME, MB_ICONERROR);
				return FALSE;		//get out of here
			}
		}
		else
			CShellUpdater::Instance().AddPathForUpdate(destPath);
		++count;
		if (progress.IsValid())
		{
			progress.FormatPathLine(1, IDS_PROC_MOVINGPROG, pathList[nPath].GetWinPath());
			progress.FormatPathLine(2, IDS_PROC_CPYMVPROG2, destPath.GetWinPath());
			progress.SetProgress(count, pathList.GetCount());
		}
		if ((progress.IsValid())&&(progress.HasUserCancelled()))
		{
			CMessageBox::Show(GetExplorerHWND(), IDS_USERCANCELLED, IDS_APPNAME, MB_ICONINFORMATION);
			return FALSE;
		}
	}
	return true;
}
