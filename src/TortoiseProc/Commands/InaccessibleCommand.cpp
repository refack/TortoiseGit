// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2026 - TortoiseGit

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
#pragma once
#include "stdafx.h"
#include "std.h"

#include "InaccessibleCommand.h"
import MessageBox;

bool InaccessibleCommand ::Execute()
{
	CString errorParts;
	g_Git.SetCurrentDirExact(orgCmdLinePath.GetWinPathString().c_str());
	::SetCurrentDirectory(orgCmdLinePath.GetWinPath());
	try
	{
		g_Git.CheckAndInitDll();
	}
	catch (const char* msg)
	{
		errorParts = L"libgit reported: " + CUnicodeUtils::GetUnicode<CString>(msg) + L"\n";
	}
	const std::wstring dotGitPath = std::format(L"{}\\.git", orgCmdLinePath.GetWinPathString());
	if (!(PathFileExists(dotGitPath.c_str()) && !PathIsDirectory(dotGitPath.c_str())) && !g_Git.GetGitRepository())
		errorParts += L"----------------------------\n" + TGitH::GetLibGit2LastErr();
	if (errorParts.IsEmpty())
		errorParts = L"no errors were reported";
	errorParts.TrimRight();

	CString error;
	error.AppendFormat(IDS_INACCESSIBLE, errorParts);

	if (errorParts.Find(L"libgit reported: not a git repository") == -1 && !orgCmdLinePath.GetWinPathString().empty() &&
		(PathFileExists(dotGitPath.c_str()) && !PathIsDirectory(dotGitPath.c_str())
			|| errorParts.Find(std::format(L"detected dubious ownership in repository at '{}'", orgCmdLinePath.GetGitPathString()).c_str()) != -1
			|| errorParts.Find(std::format(L"'{}' is not owned by current user", orgCmdLinePath.GetGitPathString()).c_str()) != -1))
	{
		CString addSafeDirectory;
		addSafeDirectory.Format(IDS_PROC_ADDSAFEDIRECTORY, orgCmdLinePath.GetWinPath());
		if (CMessageBox::Show(GetExplorerHWND(), error, L"TortoiseGit", 1, IDI_INFORMATION, CString(MAKEINTRESOURCE(IDS_OKBUTTON)), addSafeDirectory) == 2)
		{
			wil::unique_git_config config;
			git_config_new(config.put());
			int err = git_config_add_file_ondisk(config.get(), TGitH::GetGitGlobalXDGConfig().string().c_str(), GIT_CONFIG_LEVEL_XDG, nullptr, FALSE);
			if (!err && (fs::exists(TGitH::GetGitGlobalConfig()) || !fs::exists(TGitH::GetGitGlobalXDGConfig())))
				err = git_config_add_file_ondisk(config.get(), TGitH::GetGitGlobalConfig().string().c_str(), GIT_CONFIG_LEVEL_GLOBAL, nullptr, FALSE);
			if (err)
			{
				::MessageBox(GetExplorerHWND(), TGitH::GetLibGit2LastErr(), L"TortoiseGit", MB_ICONEXCLAMATION);
				return false;
			}
			CString path = orgCmdLinePath.GetGitPathString().c_str();
			path.TrimRight(L"/");
			if (git_config_set_multivar(config.get(), "safe.directory", "$^", CUnicodeUtils::GetUTF8<CStringA>(path)) < 0)
			{
				MessageBox(GetExplorerHWND(), TGitH::GetLibGit2LastErr(), L"TortoiseGit", MB_ICONEXCLAMATION);
				return false;
			}
		}
		return true;
	}

	::MessageBox(GetExplorerHWND(), error, L"TortoiseGit", MB_ICONINFORMATION);
	return true;
}
