// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2009-2013, 2015-2019, 2022, 2025-2026 - TortoiseGit
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

#include "RemoveCommand.h"

import TGitMessageBox;

bool RemoveCommand::Execute()
{
	bool bRet = false;
	// removing items from a working copy is done item-by-item so we
	// have a chance to show a progress bar
	//
	// removing items from an URL in the repository requires that we
	// ask the user for a log message.

	//we don't ask user about if keep local copy.
	//because there are command "Delete(keep local copy)" at explore context menu
	//int key = CMessageBox::Show(GetExplorerHWND(), L"File will removed from version control\r\n Do you want to keep local copy", L"TortoiseGit", MB_ICONINFORMATION | MB_YESNOCANCEL);
	//if(key == IDCANCEL)

	CString format;
	BOOL keepLocal = parser.HasKey(L"keep");
	if (pathList.GetCount() > 1)
		format.Format(keepLocal ? IDS_WARN_DELETE_MANY_FROM_INDEX : IDS_WARN_DELETE_MANY, pathList.GetCount());
	else
		format.Format(keepLocal ? IDS_WARN_DELETE_ONE_FROM_INDEX : IDS_WARN_REMOVE, static_cast<LPCWSTR>(pathList[0].GetGitPathString().c_str()));
	if (CMessageBox::Show(GetExplorerHWND(), format, IDS_APPNAME, 2, IDI_QUESTION, IDS_REMOVEBUTTON, IDS_MSGBOX_ABORT) == 2)
		return false;

	// The two command templates differed by one flag. (Note the flag order is
	// not the same as IgnoreCommand's; both are preserved as they were.)
	std::vector<std::wstring> baseCmd{ L"git.exe", L"rm", L"-r", L"-f" };
	if (keepLocal)
		baseCmd.emplace_back(L"--cached");
	baseCmd.emplace_back(L"--");

	int pathsHandled = 0;
	for (int nPath = 0; nPath < pathList.GetCount(); ++nPath)
	{
		std::vector<std::wstring> cmd(baseCmd);
		cmd.emplace_back(pathList[nPath].GetGitPathString());
		std::string output;
		if (g_Git.Run(cmd, &output))
		{
			if (CMessageBox::Show(GetExplorerHWND(), CUnicodeUtils::StdGetUnicode(output).c_str(), IDS_APPNAME, 2, IDI_ERROR, IDS_IGNOREBUTTON, IDS_ABORTBUTTON) == 2)
			{
				bRet = FALSE;
				break;
			}
		}
		else
			++pathsHandled;
	}

	CString output;
	output.Format(IDS_PROC_FILESREMOVED, pathsHandled);

	CShellUpdater::Instance().AddPathsForUpdate(pathList);

	CMessageBox::Show(GetExplorerHWND(), output, L"TortoiseGit", MB_ICONINFORMATION | MB_OK);

	CShellUpdater::Instance().Flush();
	return bRet;
}
