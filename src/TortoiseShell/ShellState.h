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
#include "ShellExt.h"		// FileState
#include "ShellObjects.h"
#include "AdminDirCache.h"
import GitIndex;	// git_wc_status_kind
import LangDll;

/**
 * \ingroup TortoiseShell
 * What the shell extension knows about worktrees on disk. Unlike the settings
 * in g_ShellCache, this is filled in by looking at the file system.
 */
struct WorktreeState
{
	AdminDirCache adminDirs;

	/**
	 * The overlay handlers' one-entry cache. The shell asks every overlay
	 * handler (one CShellExt per FileState) about the same path in turn: the
	 * first to ask computes the status, the rest read it from here, and the one
	 * that claims the overlay clears it. Guarded by g_csGlobalCOMGuard.
	 */
	struct LastOverlay
	{
		std::wstring path;									///< empty: nothing cached
		git_wc_status_kind status = git_wc_status_none;
		bool readonly = false;								///< show the read-only (assume-valid) overlay
		bool locked = false;								///< show the locked (skip-worktree) overlay
		FileState state = FileStateInvalid;					///< the handler that computed it
	} lastOverlay;
};

/**
 * \ingroup TortoiseShell
 * The shell extension's runtime state, behind the one accessor GetShellState().
 *
 * It is built by the first CShellExt, so a process that loads the DLL but is
 * never handed a TortoiseGit object never builds it. What stays a plain global
 * (TortoiseGIT.cpp) is either a handful of bytes or g_ShellCache, the user's
 * settings, whose construction is a few registry reads.
 */
struct ShellState
{
	ShellObjects objects;	///< every live CShellExt
	WorktreeState worktree;

	CLangDll langDll;		///< the loaded translation, if any; see LoadLangDll()
	DWORD langid = 0;		///< language langDll was loaded for
	ULONGLONG langTimeout = 0;	///< no retry before this tick, after a failed load
};

/// The shell extension's runtime state, created on first use. It is destroyed
/// at DLL_PROCESS_DETACH, as were the globals it replaces.
/// (Not "Shell()": shldisp.h already declares the Shell.Application coclass as `class Shell`.)
ShellState& GetShellState();
