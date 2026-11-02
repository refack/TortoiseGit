// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2012-2026 - TortoiseGit
// Copyright (C) 2003-2017 - TortoiseSVN

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
#include <map>
#include <optional>
#include <string>
#include <string_view>
import TGitPath;	// GitAdminDir lives in the TGitPath module

/**
 * \ingroup TortoiseShell
 * Which worktree, if any, a folder belongs to: a short-lived cache in front of
 * GitAdminDir::HasAdminDir, which walks up the tree probing for ".git".
 *
 * This used to be part of ShellCache. ShellCache holds the user's settings,
 * which are system-wide and cheap to load; this holds facts about worktrees on
 * disk, so it belongs to whoever owns the worktree state - ShellState in the
 * shell extension, CGitStatusCache in TGitCache.
 */
class AdminDirCache
{
public:
	/// how long an answer stays valid, in ms
	static constexpr ULONGLONG lifetime = 10000;

	AdminDirCache() = default;
	AdminDirCache(const AdminDirCache&) = delete;
	AdminDirCache& operator=(const AdminDirCache&) = delete;

	/// Whether \a path (or, for a file, its folder) is inside a worktree.
	/// On success the worktree root goes to \a projectTopDir, if given.
	bool HasAdminDir(std::wstring_view path, bool isDir, std::wstring* projectTopDir = nullptr)
	{
		const ULONGLONG now = GetTickCount64();

		std::wstring folder(path);
		if (!isDir)
		{
			if (const size_t pos = folder.rfind(L'\\'); pos != std::wstring::npos)
				folder.erase(pos);
		}

		{
			Locker lock(m_critSec);
			if (now >= m_nextCleanup)
			{
				// Clear stale elements every now and then
				m_nextCleanup = now + 10 * 60 * 1000; // 10 minutes
				std::erase_if(m_cache, [now](const auto& pair) { return now >= pair.second.timeout; });
			}
			if (const auto hit = Lookup(folder, now, projectTopDir))
				return *hit;
		}

		// When explorer calls CShellExt::IsMemberOf for a lot of subfolders, the cache above won't be
		// effective because folders can be their own repository. But most of the time they are not and
		// we don't want to waste time searching for ".git" in all our parent folders in that common case.
		// So, for subfolders that are not bare and with no ".git" inside them, we check the cache again for their parent folder.
		// This way, all CShellExt::IsMemberOf calls for a given folder will check the cache for that folder
		// and GitAdminDir::HasAdminDir has to be called at most once.
		if (isDir)
		{
			if (const size_t pos = folder.rfind(L'\\'); pos != std::wstring::npos)
			{
				if (!GitAdminDir::GitPathFileExists(folder + L"\\.git") && !GitAdminDir::IsBareRepo(folder))
				{
					folder.erase(pos);
					Locker lock(m_critSec);
					if (const auto hit = Lookup(folder, now, projectTopDir))
						return *hit;
				}
			}
		}

		const std::wstring projectRoot = GitAdminDir::HasAdminDir(folder);
		const bool hasAdminDir = !projectRoot.empty();
		Locker lock(m_critSec);
		Entry& entry = m_cache[folder];
		entry.hasAdminDir = hasAdminDir;
		entry.timeout = now + lifetime;
		if (hasAdminDir)
		{
			entry.projectRoot = projectRoot;
			if (projectTopDir)
				*projectTopDir = projectRoot;
		}
		return hasAdminDir;
	}

private:
	using Locker = CComCritSecLock<CComCriticalSection>;

	struct Entry
	{
		bool hasAdminDir = false;
		std::wstring projectRoot;
		ULONGLONG timeout = 0;
	};

	/// caller holds m_critSec
	std::optional<bool> Lookup(const std::wstring& folder, ULONGLONG now, std::wstring* projectTopDir) const
	{
		const auto iter = m_cache.find(folder);
		if (iter == m_cache.cend() || now >= iter->second.timeout)
			return std::nullopt;
		if (projectTopDir && iter->second.hasAdminDir)
			*projectTopDir = iter->second.projectRoot;
		return iter->second.hasAdminDir;
	}

	std::map<std::wstring, Entry> m_cache;
	ULONGLONG m_nextCleanup = 0;
	CComAutoCriticalSection m_critSec;
};
