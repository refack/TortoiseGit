// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2012, 2015-2019, 2021, 2026 - TortoiseGit
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
#include "RenameCommand.h"

#include "MessageBox.h"
#include "RenameDlg.h"
#include "Git.h"
#include "ShellUpdater.h"

bool RenameCommand::Execute()
{
	bool bRet = true;
	CString filename = cmdLinePath.GetFileOrDirectoryName().c_str();
	CString basePath = cmdLinePath.GetContainingDirectory().GetGitPathString().c_str();

	// show the rename dialog until the user either cancels or enters a new
	// name (one that's different to the original name
	CString sNewName;
	CRenameDlg dlg;
	dlg.SetInputValidator([&](const int /*nID*/, const CString& input) -> CString
	{
		CString newName;
		if (!basePath.IsEmpty())
			newName = basePath + "/" + input;
		else
			newName = input;

		if (newName.CompareNoCase(cmdLinePath.GetGitPathString().c_str()) != 0 && PathFileExists(g_Git.CombinePath(newName)))
			return CString(static_cast<LPCWSTR>(CFormatMessageWrapper(ERROR_FILE_EXISTS)));

		return{};
	});
	dlg.m_sBaseDir = g_Git.CombinePath(basePath);
	dlg.m_name = filename;
	if (dlg.DoModal() != IDOK)
		return FALSE;
	if (!basePath.IsEmpty())
		sNewName = basePath + "/" + dlg.m_name;
	else
		sNewName = dlg.m_name;

	STRING_VECTOR cmd{ L"git.exe", L"mv" };
	// if the filenames only differ in case, we have to pass "-f"
	if (sNewName.CompareNoCase(cmdLinePath.GetGitPathString().c_str()) == 0)
		cmd.emplace_back(L"-f");
	cmd.insert(cmd.cend(), { std::wstring(L"--"), cmdLinePath.GetGitPathString(), std::wstring(sNewName) });

	CString output;
	if (g_Git.Run(cmd, &output, CP_UTF8))
	{
		CMessageBox::Show(GetExplorerHWND(), output, L"TortoiseGit", MB_OK);
		bRet = false;
	}

	CTGitPath newpath;
	newpath.SetFromGit(sNewName.GetString());

	CShellUpdater::Instance().AddPathForUpdate(newpath);
	CShellUpdater::Instance().Flush();
	return bRet;
}
