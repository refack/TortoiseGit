// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2015-2016, 2025 - TortoiseGit

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
#include "AutoTempDir.h"
#include <atomic>

CAutoTempDir::CAutoTempDir()
{
	// This used to be GetTempFileName + DeleteFile + CreateDirectory. That is a
	// race: GetTempFileName's uniqueness guarantee comes from the *file* it
	// creates, so deleting it reopens the name to anyone else asking at the same
	// moment - and CreateDirectory's result was not checked, so a loser of that
	// race silently ended up pointing at somebody else's directory. Harmless
	// while the suite ran in one process; not harmless once it runs in eight,
	// where it showed up as an unrelated test failing about one run in five.
	//
	// The process id makes collisions between processes impossible rather than
	// unlikely, and the loop covers reuse within one process.
	CString temppath;
	GetTempPath(MAX_PATH, temppath.GetBufferSetLength(MAX_PATH));
	temppath.ReleaseBuffer();

	static std::atomic<unsigned int> s_nextIndex{ 0 };
	for (int attempt = 0; attempt < 1000; ++attempt)
	{
		CString candidate;
		candidate.Format(L"%stgit-tests-%lu-%u", static_cast<LPCWSTR>(temppath), GetCurrentProcessId(), s_nextIndex++);
		if (CreateDirectory(candidate, nullptr))
		{
			tempdir = candidate;
			return;
		}
	}
	ATLASSERT(false); // could not create a temp directory at all
}

void CAutoTempDir::DeleteDirectoryRecursive(const CString& dir)
{
	WIN32_FIND_DATA ffd;
	HANDLE hp = FindFirstFile(dir + L"\\*", &ffd);
	do
	{
		if (!wcscmp(ffd.cFileName, L".") || !wcscmp(ffd.cFileName, L".."))
			continue;
		if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
		{
			CString subdir = dir + L'\\' + ffd.cFileName;
			DeleteDirectoryRecursive(subdir);
		}
		else
		{
			CString file = dir + L'\\' + ffd.cFileName;
			[[maybe_unused]] bool failed = !DeleteFile(file);
			if (failed && GetLastError() == ERROR_ACCESS_DENIED)
			{
				SetFileAttributes(file, GetFileAttributes(file) & ~FILE_ATTRIBUTE_READONLY);
				failed = !DeleteFile(file);
			}
		}
	} while(FindNextFile(hp, &ffd));
	FindClose(hp);

	RemoveDirectory(dir);
}

CAutoTempDir::~CAutoTempDir()
{
	if (!tempdir.IsEmpty())
		DeleteDirectoryRecursive(tempdir);
}

CString CAutoTempDir::GetTempDir() const
{
	return tempdir;
}
