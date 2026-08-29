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

// The meaning of the Software\TortoiseGit\CacheType registry value. It lives
// here rather than in ShellCache.h because GitStatus reads it too, and the
// library cannot reach into the shell extension for a four-value enum.
// Unscoped on purpose: consumers compare it against the raw DWORD.
namespace TGitCacheType
{
	enum CacheType
	{
		none,
		exe,
		dll,
		dllFull, // same as dll except it uses commandline git tool with all status modes supported
	};
}
