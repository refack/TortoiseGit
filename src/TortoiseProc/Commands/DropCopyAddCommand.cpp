// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2011, 2013-2016, 2018-2019, 2023-2026 - TortoiseGit
// Copyright (C) 2007-2008,2010,2012 - TortoiseSVN

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

#include "DropCopyAddCommand.h"
#include "GitProgressDlg.h"
import MessageBox;
#include "ProgressCommands/AddProgressCommand.h"
import DirFileEnum;


bool DropCopyAddCommand::Execute()
{
	bool bRet = false;
	auto git_path = fs::path{parser.GetValStr(L"droptarget")};
	auto droppath = CTGitPath{git_path};
	if (droppath.IsAdminDir())
		return FALSE;

	std::wstring projectTopDir;
	const bool hasAdminDir = droppath.HasAdminDir(&projectTopDir);
	g_Git.SetCurrentDirExact(projectTopDir.c_str());
	if (!hasAdminDir)
		return FALSE;

	const int worktreePathLen = win32_cast(g_Git.m_CurrentDir.wstring().size());
	orgPathList.RemoveAdminPaths();
	CTGitPathList copiedFiles;
	for(int nPath = 0; nPath < orgPathList.GetCount(); ++nPath)
	{
		if (orgPathList[nPath].IsEquivalentTo(droppath))
			continue;

		//copy the file to the new location
		auto name = orgPathList[nPath].GetFileOrDirectoryName();
		auto psz_path = git_path / name;
		if (fs::exists(psz_path))
		{
			if (fs::is_directory(psz_path))
			{
				if (orgPathList[nPath].IsDirectory())
					continue;
			}

			CString strMessage;
			strMessage.Format(IDS_PROC_OVERWRITE_CONFIRM, psz_path.c_str());
			CString sBtn1(MAKEINTRESOURCE(IDS_PROC_OVERWRITEEXPORT_OVERWRITE));
			CString sBtn2(MAKEINTRESOURCE(IDS_PROC_OVERWRITEEXPORT_KEEP));
			CString sBtn3(MAKEINTRESOURCE(IDS_PROC_OVERWRITEEXPORT_CANCEL));
			const UINT ret = CMessageBox::Show(GetExplorerHWND(), strMessage, L"TortoiseGit", 2, IDI_QUESTION, sBtn1, sBtn2, sBtn3);

			if (ret == 3)
				return FALSE; //cancel the whole operation
			if (ret == 1)
			{
				if (!::CopyFile(orgPathList[nPath].GetWinPath(), psz_path.c_str(), FALSE))
				{
					//the copy operation failed! Get out of here!
					ShowErrorMessage();
					return FALSE;
				}
			}
		}
		else
		{
			if (orgPathList[nPath].IsDirectory())
			{
				CString fromPath(std::format(L"{}||", orgPathList[nPath].GetWinPathString()).c_str());
				auto toPath = psz_path.wstring() + L"||";
				auto fromBuf = std::make_unique<wchar_t[]>(fromPath.GetLength() + 1);
				auto toBuf = std::make_unique<wchar_t[]>(toPath.length() + 1);
				wcscpy_s(fromBuf.get(), fromPath.GetLength() + 1, fromPath);
				wcscpy_s(toBuf.get(), toPath.length() + 1, toPath.c_str());
				StringUtils::PipesToNulls(fromBuf.get(), fromPath.GetLength());
				StringUtils::PipesToNulls(toBuf.get(), toPath.length());

				SHFILEOPSTRUCT fileop = {};
				fileop.wFunc = FO_COPY;
				fileop.pFrom = fromBuf.get();
				fileop.pTo = toBuf.get();
				fileop.fFlags = FOF_NO_CONNECTED_ELEMENTS | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR | FOF_NOCOPYSECURITYATTRIBS | FOF_SILENT;
				if (!SHFileOperation(&fileop))
				{
					// add all copied files WITH special handling for repos/submodules (folders which include a .git entry)
					DirFileEnum::CDirFileEnum finder(psz_path.c_str());
					CString lastRepo;
					bool isRepo = false;
					while (auto file = finder.NextFile(!isRepo)) // don't recurse into .git directories
					{
						CString filepath = file->GetFilePath();
						if (!lastRepo.IsEmpty())
						{
							if (StringUtils::StartsWith(filepath, lastRepo))
								continue;
							else
								lastRepo.Empty();
						}
						isRepo = StringUtils::EndsWith(filepath, L"\\.git");
						if (isRepo)
						{
							lastRepo = filepath.Left(filepath.GetLength() - static_cast<int>(wcslen(L".git")));
							CString msg;
							if (!file->IsDirectory())
								msg.Format(IDS_PROC_COPY_SUBMODULE, lastRepo);
							else
								msg.Format(IDS_PROC_COPY_REPOSITORY, lastRepo);
							const auto ret = CMessageBox::Show(GetExplorerHWND(), msg, IDS_APPNAME, 1, IDI_QUESTION, IDS_DELETEBUTTON, IDS_IGNOREBUTTON, IDS_ABORTBUTTON);
							if (ret == 3)
								return FALSE;
							if (ret == 1)
							{
								std::ignore = CTGitPath(filepath.GetString()).Delete(false, true);
								lastRepo.Empty();
							}
							continue;
						}
						if (!file->IsDirectory())
							copiedFiles.AddPath(CTGitPath(filepath.Mid(worktreePathLen + 1).GetString())); //add the new filepath
					}
				}
				continue; // do not add a directory to copiedFiles
			}
			else if (std::error_code ec; fs::copy(orgPathList[nPath].GetWinPath(), psz_path, ec), ec)
			{
				//the copy operation failed! Get out of here!
				ShowErrorMessage();
				return FALSE;
			}
		}
		CString destPath(psz_path.c_str());
		copiedFiles.AddPath(CTGitPath(destPath.Mid(worktreePathLen + 1).GetString())); //add the new filepath
	}
	//now add all the newly copied files to the working copy
	CGitProgressDlg progDlg;
	theApp.m_pMainWnd = &progDlg;
	AddProgressCommand addCommand;
	progDlg.SetCommand(&addCommand);
	addCommand.SetPathList(copiedFiles);
	progDlg.DoModal();
	bRet = !progDlg.DidErrorsOccur();

	return bRet;
}

void DropCopyAddCommand::ShowErrorMessage() const {
	const CFormatMessageWrapper errorDetails;
	CString strMessage;
	strMessage.Format(IDS_ERR_COPYFILES, (errorDetails).c_str());
	MessageBox(GetExplorerHWND(), strMessage, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
}
