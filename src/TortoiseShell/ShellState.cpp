// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2026 - TortoiseGit
// Copyright (C) 2003-2014 - TortoiseSVN

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
#include "ShellState.h"

ShellState& GetShellState()
{
	// A function-local static: its constructor runs on the first call, from
	// whichever thread gets there first (C++11 guarantees exactly once), and
	// never during DllMain.
	static ShellState state;
	return state;
}

void LoadLangDll()
{
	auto& shell = GetShellState();
	if (shell.langid != g_ShellCache.GetLangID() && (shell.langTimeout == 0 || shell.langTimeout < GetTickCount64()))
	{
		g_hResInst = g_hmodThisDll; // reset to English defaults, because CLangDll.Init() unloads the DLL
		if (HINSTANCE hInst = shell.langDll.Init(L"TortoiseProc", g_hmodThisDll, g_ShellCache.GetLangID()); hInst)
		{
			g_hResInst = hInst;
			shell.langid = shell.langDll.GetLoadedLangId();
			shell.langTimeout = 0;
			return;
		}

		// either the dll for the selected language is not present, or
		// it is the wrong version.
		// fall back to English and set a timeout so we don't retry
		// to load the language dll too often
		g_hResInst = g_hmodThisDll;
		shell.langid = LangDll::s_defaultLang;
		// set a timeout of 10 seconds
		if (g_ShellCache.GetLangID() != LangDll::s_defaultLang)
			shell.langTimeout = GetTickCount64() + 10000;
	}
}
