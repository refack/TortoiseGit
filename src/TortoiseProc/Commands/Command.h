// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2009, 2011, 2013-2014, 2016, 2018-2019, 2025-2026 - TortoiseGit
// Copyright (C) 2007 - TortoiseSVN

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
#include "TortoiseProc.h"
import CmdLineParser;
import TGitPath;
import git_h;

enum class PathRequirement
{
	NoPathRequired,
	MayTakePath,
	PathRequired, // TortoiseGit historically also accepts CWD instead of explicitly passed paths
	WorkingTreeOrBareRepoRequired,
	WorkingTreeRequired,
};

/**
 * \ingroup TortoiseProc
 * Interface for all command line commands TortoiseProc can execute.
 */
class Command
{
public:
	/**
	 * Executes the command.
	 */
	virtual bool			Execute() = 0;

	virtual bool RequiresGitExe() const { return true; };
	bool MayTakePath() const { return m_pathRequirement >= PathRequirement::MayTakePath; };
	bool RequiresWorkingTreeOrBareRepo() const { return m_pathRequirement >= PathRequirement::WorkingTreeOrBareRepoRequired; };
	bool RequiresWorkingTree() const { return m_pathRequirement >= PathRequirement::WorkingTreeRequired; };

	// allow sub-classes to execute code during destruction
	virtual ~Command() = default;

	void SetParser(const CCmdLineParser& p) {parser = p;}
	void SetExplorerHwnd(HWND hWnd) {hwndExplorer = hWnd;}
	HWND GetExplorerHWND() const { return ::IsWindow(hwndExplorer) ? hwndExplorer : nullptr; }

	void SetPaths(const CTGitPathList& plist, const CTGitPath &path) {
		{
			this->orgCmdLinePath = path;
			const fs::path rel = path.GetStdPath().lexically_relative(g_Git.m_CurrentDir);
			if (!rel.empty() && *rel.begin() != L"..")
				this->cmdLinePath.SetFromWin(rel.c_str());
		}

		this->orgPathList = plist;
		for (int i = 0; i < plist.GetCount(); ++i) {
			const fs::path rel = plist[i].GetStdPath().lexically_relative(g_Git.m_CurrentDir);
			if (!rel.empty() && *rel.begin() != L"..")
				this->pathList.AddPath(CTGitPath(rel));

		}
	}

	bool CheckRepo() const { return CheckRepo(m_pathRequirement); }

protected:
	CCmdLineParser			parser;
	CTGitPathList			pathList;
	CTGitPathList			orgPathList;
	CTGitPath				cmdLinePath;
	CTGitPath				orgCmdLinePath;
	PathRequirement			m_pathRequirement = PathRequirement::WorkingTreeRequired;

	bool					CheckRepo(PathRequirement requirement) const;

private:
	HWND					hwndExplorer = nullptr;
};

/**
 * \ingroup TortoiseProc
 * Factory for the different commands which TortoiseProc executes from the
 * command line.
 */
class CommandServer
{
public:
	static inline std::unique_ptr<Command> GetCommand(const CString& sCmd, HWND explorerhWnd, const CCmdLineParser& p)
	{
		auto cmd = std::unique_ptr<Command>(CreateRawCommand(sCmd));
		cmd->SetExplorerHwnd(explorerhWnd);
		cmd->SetParser(p);
		return cmd;
	}

private:
	CommandServer() = delete;

	static Command* CreateRawCommand(const CString& sCmd);
};
