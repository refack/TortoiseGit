// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2009, 2011-2016, 2018-2019, 2021, 2023, 2026 - TortoiseGit

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
#include "CatCommand.h"
import str_utils;
import PathUtils;
import git_h;
#include "MessageBox.h"
import RIAA;

bool CatCommand::Execute()
{
	CString savepath = PathUtils::GetLongPathname(parser.GetValStr(L"savepath")).c_str();
	CString revision = parser.GetVal(L"revision");

	if (g_Git.UsingLibGit2(TGitH::GIT_CMD_GETONEFILE))
	{
		const wil::unique_git_repository repo(g_Git.GetGitRepository());
		if (!repo)
		{
			::DeleteFile(savepath);
			MessageBox(GetExplorerHWND(), TGitH::GetLibGit2LastErr(L"Could not open repository."), L"TortoiseGit", MB_ICONERROR);
			return false;
		}

		wil::unique_git_object obj;
		if (git_revparse_single(obj.put(), repo.get(), CUnicodeUtils::GetUTF8<CStringA>(revision)))
		{
			::DeleteFile(savepath);
			MessageBox(GetExplorerHWND(), TGitH::GetLibGit2LastErr(L"Could not parse revision."), L"TortoiseGit", MB_ICONERROR);
			return false;
		}

		if (git_object_type(obj.get()) == GIT_OBJECT_BLOB)
		{
			CAutoFILE file = _wfsopen(savepath, L"wb", SH_DENYRW);
			if (file == nullptr)
			{
				::DeleteFile(savepath);
				MessageBox(GetExplorerHWND(), L"Could not open file for writing.", L"TortoiseGit", MB_ICONERROR);
				return false;
			}

			wil::unique_git_buf buf;
			git_blob_filter_options opts = GIT_BLOB_FILTER_OPTIONS_INIT;
			opts.flags &= ~static_cast<uint32_t>(GIT_BLOB_FILTER_CHECK_FOR_BINARY);
			if (git_blob_filter(&buf, reinterpret_cast<git_blob*>(obj.get()), CUnicodeUtils::Convert2Multibyte<CStringA>(cmdLinePath.GetGitPathString().c_str()), &opts))
			{
				::DeleteFile(savepath);
				MessageBox(GetExplorerHWND(), TGitH::GetLibGit2LastErr(L"Could not get filtered content."), L"TortoiseGit", MB_ICONERROR);
				return false;
			}
			if (fwrite(buf.ptr, sizeof(char), buf.size, file) != buf.size)
			{
				::DeleteFile(savepath);
				CString err { (CFormatMessageWrapper()).c_str() };
				CMessageBox::Show(GetExplorerHWND(), L"Could not write to file: " + err, L"TortoiseGit", MB_ICONERROR);
				return false;
			}
			return true;
		}

		if (g_Git.GetOneFile(revision, cmdLinePath, savepath))
		{
			MessageBox(GetExplorerHWND(), g_Git.GetGitLastErr(L"Could get file.", TGitH::GIT_CMD_GETONEFILE), L"TortoiseGit", MB_ICONERROR);
			return false;
		}
		return true;
	}

	CString output, err;
	if (g_Git.Run({ L"git.exe", L"cat-file", L"-t", L"--", std::wstring(revision) }, &output, &err, CP_UTF8))
	{
		::DeleteFile(savepath);
		MessageBox(GetExplorerHWND(), output + L'\n' + err, L"TortoiseGit", MB_ICONERROR);
		return false;
	}

	std::vector<std::wstring> cmd;
	if (StringUtils::StartsWith(output, L"blob"))
		cmd = { L"git.exe", L"cat-file", L"-p", L"--", std::wstring(revision) };
	else
	{
		// GetGitPathString(), not GetWinPathString(): the old spelling ran the
		// Windows path through non-relaxed QuoteParameter, which turned the
		// backslashes into slashes on the way past. SerializeArgv does not.
		cmd = { L"git.exe", L"show", L"--end-of-options", std::wstring(revision), L"--", cmdLinePath.GetGitPathString() };
	}

	if (g_Git.RunLogFile(cmd, savepath, &err))
	{
		::DeleteFile(savepath);
		MessageBox(GetExplorerHWND(), L"Cat file failed:\n" + err, L"TortoiseGit", MB_ICONERROR);
		return false;
	}
	return true;
}
