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

#include <scintilla/Scintilla.h>
// No std header here: this file is included textually, often after `import`s,
// and a textual <mutex> after an import redefines <chrono>/<thread>. The
// once-registration uses Win32 INIT_ONCE instead (the includer has <windows.h>).

/**
 * Registers the "Scintilla" window class, once per process.
 *
 * Scintilla used to arrive as SciLexer_tgit.dll, whose DllMain registered the
 * class as a side effect of the DLL being loaded. It is a static library now,
 * so nothing registers the class unless we do.
 *
 * This has to run before any window of class "Scintilla" is created, and the
 * .rc dialog templates create them - CONTROL "Scintilla", IDC_LOGMESSAGE,
 * "Scintilla", ... - so "before DoModal" is not early enough on its own. Call
 * this from InitInstance, or from the constructor of whatever owns the control,
 * which runs before the dialog is created.
 *
 * Calling it more than once is harmless but pointless, hence the INIT_ONCE.
 */
inline void EnsureScintillaRegistered(HINSTANCE hInstance)
{
	static INIT_ONCE registered = INIT_ONCE_STATIC_INIT;
	InitOnceExecuteOnce(
		&registered,
		[](PINIT_ONCE, PVOID parameter, PVOID*) -> BOOL {
			Scintilla_RegisterClasses(parameter);
			return TRUE;
		},
		hInstance, nullptr);
}
