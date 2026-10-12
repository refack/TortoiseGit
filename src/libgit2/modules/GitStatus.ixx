module;
#include <windows.h>
#include <atlbase.h>	// CComCritSecLock/CComCriticalSection, not CString: that is str_utils'
// wil::GetFileInfo<> (instantiated below) reaches wil's error-reporting helpers, which call
// strsafe's *static* StringCch* workers. A template instantiated here cannot use the static
// definitions from module wil's own fragment (internal linkage), so this TU needs its own: C2129.
#include <strsafe.h>
#include "sanity.h"


export module TGitStatus;
import std;
import wil;

import libgit2;
import str_utils;
import TGitPath;
import PathUtils;
import Registry;
import GitHash;
import GitIndex;
import DebugOutput;
import wil_git2;

namespace fs = std::filesystem;

using tgit::wstr::to_path;
using CString = tgit::wstr::CString;
using namespace GitIndex;


export namespace GitStatus
{

// From tgittypes (moved verbatim). TGitStatus is its lowest user. The values are
// persisted in the registry (HKCU\Software\TortoiseGit\CacheType): never renumber.
enum CacheType {
	none,
	exe,
	dll,
	dllFull, // same as dll except it uses commandline git tool with all status modes supported
};


CGitIndexFileMap g_IndexFileMap;
CGitHeadFileMap g_HeadFileMap;
CGitIgnoreList  g_IgnoreList;


struct CGitFileName
{
	fs::path m_FileName{};
	size_t m_Size = 0;
	int64_t m_LastModified = 0;
	bool m_bSymlink = false;
};


constexpr auto MAX_STATUS_STRING_LENGTH = 256;

using FILL_STATUS_CALLBACK = BOOL(const CString& path, const git_wc_status2_t* status, bool isDir, __int64 lastwritetime, void* baton);

// checks whether indexPath is a direct submodule and not one in a subfolder:
// it ends with '/' and that is the only '/' after its first prefix characters
bool IsDirectSubmodule(const std::wstring_view indexPath, const int prefix)
{
	if (!indexPath.ends_with(L'/'))
		return false;

	// the old pointer walk read past the end here; an out-of-range prefix is no submodule
	if (prefix < 0 || static_cast<size_t>(prefix) > indexPath.size())
		return false;

	return std::ranges::count(indexPath.substr(static_cast<size_t>(prefix)), L'/') == 1;
}


int GetFileStatus_int(const std::wstring& gitdir, CGitRepoLists& repolists, const std::wstring& path, git_wc_status2_t& status, const BOOL IsFull, const BOOL IsIgnore, const BOOL update)
{
	Expects(repolists.pIndex);
	Expects(!status.assumeValid && !status.skipWorktree);


	CGitHash hash;
	if (repolists.pIndex->GetFileStatus(gitdir, path, status, &hash))
	{
		// an error occurred in GetFileStatus_NoRepo
		status.status = git_wc_status_none;
		return -1;
	}

	if (status.status == git_wc_status_conflicted)
		return 0;

	if (status.status == git_wc_status_unversioned)
	{
		if (IsFull)
		{
			if (!repolists.pTree)
			{
				if (update)
					repolists.pTree = g_HeadFileMap.CheckHeadAndUpdate(gitdir, repolists.pIndex->IsIgnoreCase());
				else
					repolists.pTree = g_HeadFileMap.SafeGet(gitdir);
			}
			// broken HEAD
			if (!repolists.pTree)
			{
				status.status = git_wc_status_none;
				return -1;
			}

			// deleted only in index item?
			if (SearchInSortVector(*repolists.pTree, path, -1, repolists.pIndex->IsIgnoreCase()) != NPOS)
			{
				status.status = git_wc_status_deleted;
				return 0;
			}
		}

		if (!IsIgnore)
		{
			status.status = git_wc_status_unversioned;
			return 0;
		}

		g_IgnoreList.CheckAndUpdateIgnoreFiles(gitdir, path, false);
		if (g_IgnoreList.IsIgnore(path, gitdir, false, g_AdminDirMap.GetAdminDir(gitdir)))
			status.status = git_wc_status_ignored;

		return 0;
	}

	if ((status.status == git_wc_status_normal || status.status == git_wc_status_modified) && IsFull)
	{
		if (!repolists.pTree)
		{
			if (update)
				repolists.pTree = g_HeadFileMap.CheckHeadAndUpdate(gitdir, repolists.pIndex->IsIgnoreCase());
			else
				repolists.pTree = g_HeadFileMap.SafeGet(gitdir);
		}
		// broken HEAD
		if (!repolists.pTree)
		{
			status.status = git_wc_status_none;
			return -1;
		}

		//add item
		size_t start = SearchInSortVector(*repolists.pTree, path, -1, repolists.pIndex->IsIgnoreCase());
		if (start == NPOS)
		{
			status.status = git_wc_status_added;
			tgit::DebugOutput::TracePrintf(_T(__FUNCTION__) L": File miss in head tree %s\n", path.c_str());
			return 0;
		}

		// staged and not commit
		if ((*repolists.pTree)[start].m_Hash != hash)
		{
			status = { .status = git_wc_status_modified, .assumeValid = false, .skipWorktree = false };
			return 0;
		}
	}

	return 0;
}


/**
 * \ingroup Git
 * Handles git status of working copies.
 */
class GitStatus
{
	git_wc_status2_t m_status; // used for GetStatus

	// Returns a numeric value indicating the importance of a status.
	// A higher number indicates a more important status.
	static int GetStatusRanking(const git_wc_status_kind status)
	{
			switch (status)
		{
		case git_wc_status_none:
			return 0;
		case git_wc_status_unversioned:
			return 1;
		case git_wc_status_ignored:
			return 2;
		case git_wc_status_normal:
		case git_wc_status_added:
			return 6;
		case git_wc_status_deleted:
			return 8;
		case git_wc_status_modified:
			return 10;
		case git_wc_status_conflicted:
			return 12;

		default:
			break;
		}
		return 0;
	}

public:
	using CAutoLocker = CComCritSecLock<CComCriticalSection>;


	static int GetFileStatus(const CString& gitdir, CString path, git_wc_status2_t& status, const bool IsFull = false, const bool IsIgnore = true, const bool update = true)
	{
		Expects(!status.assumeValid && !status.skipWorktree);

		path.Replace(L'\\', L'/');

		const std::wstring gitdirW(gitdir); // GitIndex speaks std::wstring

		CGitRepoLists sharedRepoLists;
		if (update)
			sharedRepoLists.pIndex = g_IndexFileMap.CheckAndUpdate(gitdirW);
		else
			sharedRepoLists.pIndex = g_IndexFileMap.SafeGet(gitdirW);
		if (!sharedRepoLists.pIndex)
		{
			// git working tree has broken index
			status.status = git_wc_status_kind::git_wc_status_none;
			return -1;
		}

		return GetFileStatus_int(gitdirW, sharedRepoLists, std::wstring(path), status, IsFull, IsIgnore, update);
	}

	static int GetDirStatus(const CString& gitdir, const CString& subpath, git_wc_status_kind* status, const bool IsFull = false, const bool IsRecursive = false, const bool IsIgnore = true)
	{
	
		Expects(status);

		const std::wstring gitdirW(gitdir); // GitIndex speaks std::wstring
		std::wstring path(subpath);

		std::ranges::replace(path, L'\\', L'/');
		if (!path.empty() && !path.ends_with(L'/'))
			path += L'/'; //Add trail / to show it is directory, not file name.
		const int pathLength = static_cast<int>(path.size());

		CGitRepoLists sharedRepoLists;
		sharedRepoLists.pIndex = g_IndexFileMap.CheckAndUpdate(gitdirW);

		// broken index
		if (!sharedRepoLists.pIndex)
		{
			*status = git_wc_status_none;
			return -1;
		}

		size_t pos = SearchInSortVector(*sharedRepoLists.pIndex, path, pathLength, sharedRepoLists.pIndex->IsIgnoreCase());

		// Not In Version Contorl
		if (pos == NPOS)
		{
			if (!IsIgnore)
			{
				// WC root is at least normal if there are no files added/deleted
				if (subpath.IsEmpty())
				{
					*status = git_wc_status_normal;
					return 0;
				}
				*status = git_wc_status_unversioned;
				return 0;
			}

			sharedRepoLists.pTree = g_HeadFileMap.CheckHeadAndUpdate(gitdirW, sharedRepoLists.pIndex->IsIgnoreCase());
			// broken HEAD
			if (!sharedRepoLists.pTree)
			{
				*status = git_wc_status_none;
				return -1;
			}

			// check whether there files in head with are not in index
			pos = SearchInSortVector(*sharedRepoLists.pTree, path, pathLength, sharedRepoLists.pIndex->IsIgnoreCase());
			if (pos != NPOS)
			{
				*status = git_wc_status_deleted;
				return 0;
			}

			// WC root is at least normal if there are no files added/deleted
			if (path.empty())
			{
				*status = git_wc_status_normal;
				return 0;
			}

			// Check ignore
			g_IgnoreList.CheckAndUpdateIgnoreFiles(gitdirW, path, true);
			if (g_IgnoreList.IsIgnore(path, gitdirW, true, g_AdminDirMap.GetAdminDir(gitdirW)))
				*status = git_wc_status_ignored;
			else
				*status = git_wc_status_unversioned;

			return 0;
		}

		// In version control
		*status = git_wc_status_normal;

		// the index entries under path; pos matched above, so the run holds at least that one
		const auto indexRun = GetRangeInSortVector(*sharedRepoLists.pIndex, path, path.size(), sharedRepoLists.pIndex->IsIgnoreCase(), pos);

		// Check Conflict;
		if (sharedRepoLists.pIndex->m_bHasConflicts)
		{
			for (const auto& indexentry : indexRun)
			{
				if ((indexentry.m_Flags & tgit::libgit2::TGIT_INDEX_ENTRY_STAGEMASK) != 0)
				{
					*status = git_wc_status_conflicted;
					// When status == git_wc_status_conflicted, we don't need to check each file status
					// because git_wc_status_conflicted is the highest.
					return 0;
				}
			}
		}

		if (IsFull)
		{
			// Check Add
			{
				// Check if new init repository
				sharedRepoLists.pTree = g_HeadFileMap.CheckHeadAndUpdate(gitdirW, sharedRepoLists.pIndex->IsIgnoreCase());
				// broken HEAD
				if (!sharedRepoLists.pTree)
				{
					*status = git_wc_status_none;
					return -1;
				}

				{
					for (const auto& indexentry : indexRun)
					{
						pos = SearchInSortVector(*sharedRepoLists.pTree, indexentry.m_FileName, -1, sharedRepoLists.pIndex->IsIgnoreCase());

						if (pos == NPOS)
						{
							*status = GetMoreImportant(git_wc_status_added, *status); // added file found
							AdjustFolderStatus(*status);
							if (GetMoreImportant(*status, git_wc_status_modified) == *status) // the only potential higher status which me might get in this loop
								break;
							continue;
						}

						if ((*sharedRepoLists.pTree)[pos].m_Hash != indexentry.m_IndexHash)
						{
							*status = GetMoreImportant(git_wc_status_modified, *status); // modified file found
							break;
						}
					}

					// Check Delete
					if (*status == git_wc_status_normal)
					{
						pos = SearchInSortVector(*sharedRepoLists.pTree, path, pathLength, sharedRepoLists.pIndex->IsIgnoreCase());
						if (pos == NPOS)
							*status = GetMoreImportant(git_wc_status_added, *status); // added file found
						else
						{
							// we know that pos exists in treeptr
							for (const auto& treeentry : GetRangeInSortVector(*sharedRepoLists.pTree, path, path.size(), sharedRepoLists.pIndex->IsIgnoreCase(), pos))
							{
								if (SearchInSortVector(*sharedRepoLists.pIndex, treeentry.m_FileName, -1, sharedRepoLists.pIndex->IsIgnoreCase()) == NPOS)
								{
									*status = GetMoreImportant(git_wc_status_deleted, *status); // deleted file found
									break;
								}
							}
						}
					}
				}
			} /* End lock*/
		}

		auto mostImportantPossibleFolderStatus = GetMoreImportant(git_wc_status_added, GetMoreImportant(git_wc_status_modified, git_wc_status_deleted));
		AdjustFolderStatus(mostImportantPossibleFolderStatus);
		// we can skip here when we already have the highest possible status
		if (mostImportantPossibleFolderStatus == *status)
			return 0;

		for (const auto& indexentry : indexRun)
		{
			// skip child directory, but handle submodules
			if (!IsRecursive && tgit::wstr::Find(indexentry.m_FileName, L'/', pathLength) > 0 && !IsDirectSubmodule(indexentry.m_FileName, pathLength))
				continue;

			git_wc_status2_t filestatus = { .status = git_wc_status_none, .assumeValid = false, .skipWorktree = false };
			GetFileStatus_int(gitdirW, sharedRepoLists, indexentry.m_FileName, filestatus, IsFull, IsIgnore, false);
			switch (filestatus.status)
			{
			case git_wc_status_added:
			case git_wc_status_modified:
			case git_wc_status_deleted:
				// case git_wc_status_conflicted: cannot happen, we exit as soon we found a conflict in subpath
				*status = GetMoreImportant(filestatus.status, *status);
				AdjustFolderStatus(*status);
				if (mostImportantPossibleFolderStatus == *status)
					return 0;

			default:
				// don't care
				break;
			}
		}

		return 0;
	}

	static int EnumDirStatus(const CString& gitdir, const CString& subpath, git_wc_status_kind* dirstatus, FILL_STATUS_CALLBACK callback, void* pData);
	static int GetFileList(const CString& path_, std::vector<CGitFileName>& list, bool& isRepoRoot, bool ignoreCase);
	static int GetFileList(const fs::path& path, std::vector<CGitFileName>& list, bool& isRepoRoot, bool ignoreCase);

	static bool IsExistIndexLockFile(const CTGitPath& path)
	{
		std::wstring rootDir;
		if (!path.HasAdminDir(&rootDir))
			return false;
		const std::wstring indexPath = g_AdminDirMap.GetWorktreeAdminDirConcat(rootDir, L"index.lock");
		return !!PathFileExists(indexPath.c_str());
	}

	static bool ReleasePath(const CString& gitdir)
	{
		const std::wstring gitdirW(gitdir); // GitIndex speaks std::wstring
		g_IndexFileMap.SafeClear(gitdirW);
		g_HeadFileMap.SafeClear(gitdirW);
		return true;
	}

	static bool ReleasePathsRecursively(const CString& rootpath)
	{
		const std::wstring rootpathW(rootpath); // GitIndex speaks std::wstring
		g_IndexFileMap.SafeClearRecursively(rootpathW);
		g_HeadFileMap.SafeClearRecursively(rootpathW);
		return true;
	}

	GitStatus() = default;

	// prevent cloning
	GitStatus(const GitStatus&) = delete;
	GitStatus& operator=(const GitStatus&) = delete;

	/**
	 * Reads the git status of the working copy entry. No
	 * recurse is done, even if the entry is a directory.
	 * If the status of the text and property part are different
	 * then the more important status is returned.
	 */
	static int GetAllStatus(const CTGitPath& path, const bool bIsRecursive, git_wc_status2_t& status)
	{
		const bool isDir = path.IsDirectory();

		const fs::path sProjectRoot = path.TryAdminDir();
		if (sProjectRoot.empty())
			return git_wc_status_kind::git_wc_status_none;

		std::wstring sSubPath;
		if (const auto& s = path.GetWinPathString(); s.size() > sProjectRoot.native().size())
			sSubPath = fs::path(s).lexically_relative(sProjectRoot).native();

		const bool isfull = (
			static_cast<DWORD>(
				Registry::CRegStdDWORD(L"Software\\TortoiseGit\\CacheType", GetSystemMetrics(SM_REMOTESESSION)) ?
				    CacheType::dll :
				    CacheType::exe
			) == CacheType::dllFull
		);

		if (isDir)
		{
			auto&& err = GetDirStatus(sProjectRoot.c_str(), sSubPath.c_str(), &status.status, isfull, bIsRecursive, isfull);
			AdjustFolderStatus(status.status);
			return err;
		}

		return GetFileStatus(sProjectRoot.c_str(), sSubPath.c_str(), status, isfull, isfull);
	}

	/**
	 * Returns the status which is more "important" of the two statuses specified.
	 * This is used for the "recursive" status functions on folders - i.e. which status
	 * should be returned for a folder which has several files with different statuses
	 * in it.
	 */
	static git_wc_status_kind GetMoreImportant(const git_wc_status_kind status1, const git_wc_status_kind status2)
	{
		if (GetStatusRanking(status1) >= GetStatusRanking(status2))
			return status1;
		return status2;
	}


	static void AdjustFolderStatus(git_wc_status_kind& status)
	{
	
		if (status == git_wc_status_deleted || status == git_wc_status_added)
			status = git_wc_status_modified;
	}

	/**
	 * Reads the git text status of the working copy entry. No
	 * recurse is done, even if the entry is a directory.
	 * The result is stored in the public member variable status.
	 * Use this method if you need detailed information about a file/folder, not just the raw status (like "normal", "modified").
	 *
	 * \param path the pathname of the entry
	 * \param update true if the status should be updated with the repository. Default is false.
	 * \param noignore
	 * \param noexternals
	 * \return If update is set to true the HEAD revision of the repository is returned. If update is false then -1 is returned.
	 * \remark If the return value is -2 then the status could not be obtained.
	 */
	[[deprecated("GetStatus - I don't know what this is.")]]
	void GetStatus(const CTGitPath& path, bool update = false, const bool noignore = false, bool noexternals = false)
	{
		// NOTE: unlike the SVN version this one does not cache the enumerated files, because in practice no code in all of
		//       Tortoise uses this, all places that call GetStatus create a temp GitStatus object which gets destroyed right
		//       after the call again

		std::wstring sProjectRoot;
		if (!path.HasAdminDir(&sProjectRoot))
			return;

		const bool isfull = (
			static_cast<DWORD>(Registry::CRegStdDWORD(L"Software\\TortoiseGit\\CacheType", GetSystemMetrics(SM_REMOTESESSION)) ?
				CacheType::dll : CacheType::exe
			) == CacheType::dllFull);

		std::wstring sSubPath;   // must outlive the Get*Status calls
		if (const auto& s = path.GetWinPathString(); s.size() > sProjectRoot.size())
			sSubPath = std::filesystem::path(s).lexically_relative(sProjectRoot).native();

		m_status.status = git_wc_status_kind::git_wc_status_none;
		m_status.assumeValid = false;
		m_status.skipWorktree = false;

		int err;
		if (path.IsDirectory())
		{
			err = GetDirStatus(sProjectRoot.c_str(), sSubPath.c_str(), &m_status.status, isfull, false, !noignore);
			AdjustFolderStatus(m_status.status);
		}
		else
			err = GetFileStatus(sProjectRoot.c_str(), sSubPath.c_str(), m_status, isfull, !noignore);

		// Error present if function is not under version control
		if (err)
		{
			status = nullptr;
			return;
		}

		status = &m_status;
	}

	/**
	 * This member variable hold the status of the last call to GetStatus().
	 */
	git_wc_status2_t* status = nullptr; ///< the status result of GetStatus()
};


// static method


int GitStatus::GetFileList(const fs::path& path, std::vector<CGitFileName>& list, bool& isRepoRoot, const bool ignoreCase)
{
	for (const auto& data : fs::directory_iterator(path))
	{
		if (data.path().filename() == L".git")
		{
			isRepoRoot = true;
			continue;
		}

		if (data.path().filename() == L"." || data.path().filename() == L"..")
			continue;


		const auto attrs = PathUtils::get_path_attributes(data);
		if (!attrs || !attrs->exists())
			continue;      // gone between enumeration and query

		CGitFileName filename = {
			.m_FileName = data.path(),
			.m_Size = static_cast<size_t>(attrs->size),
			.m_LastModified = attrs->lastWriteTime,
			.m_bSymlink = attrs->isLink,
		};
		if (attrs->type == fs::file_type::directory)
			filename.m_FileName += L'/';

		list.push_back(filename);
	}

	DoSortFilenametSortVector(list, ignoreCase);

	return 0;
}

int GitStatus::GetFileList(const CString& path_, std::vector<CGitFileName>& list, bool& isRepoRoot, const bool ignoreCase)
{
	return GetFileList(tgit::wstr::to_path(path_), list, isRepoRoot, ignoreCase);
}

int GitStatus::EnumDirStatus(const CString& gitdir, const CString& subpath, git_wc_status_kind* dirstatus, FILL_STATUS_CALLBACK callback, void* pData)
{
	// the callback still takes a CString; the pieces are CStrings or std::wstrings
	constexpr auto CombinePath = [](const auto& a, const auto& b) -> CString {
		return (to_path(a) / to_path(b)).c_str();
	};
	constexpr auto CombinePath3 = [](const auto& a, const auto& b, const auto& c) -> CString {
		return (to_path(a) / to_path(b) / to_path(c)).c_str();
	};

	// GitIndex speaks std::wstring
	const std::wstring gitdirW(gitdir);
	const std::wstring subpathW(subpath);
	std::wstring path = subpathW;

	std::ranges::replace(path, L'\\', L'/');
	if (!path.empty() && !path.ends_with(L'/'))
		path += L'/'; // Add trail / to show it is directory, not file name.
	const int pathLength = static_cast<int>(path.size());

	SHARED_INDEX_PTR indexptr = g_IndexFileMap.CheckAndUpdate(gitdirW);
	// there was an error loading the index
	if (!indexptr)
		return -1;

	SHARED_TREE_PTR treeptr = g_HeadFileMap.CheckHeadAndUpdate(gitdirW, indexptr->IsIgnoreCase());
	// there was an error loading the HEAD commit/tree
	if (!treeptr)
		return -1;

	size_t indexpos = SearchInSortVector(*indexptr, path, pathLength, indexptr->IsIgnoreCase()); // match path prefix, (sub)folders end with slash
	size_t treepos = SearchInSortVector(*treeptr, path, pathLength, indexptr->IsIgnoreCase()); // match path prefix, (sub)folders end with slash

	std::set<std::wstring> localLastCheckCache;
	const std::wstring adminDir = g_AdminDirMap.GetAdminDir(gitdirW);

	std::vector<CGitFileName> filelist;
	int folderignoredchecked = false;
	bool isRepoRoot = false;
	GetFileList(to_path(gitdir) / to_path(subpath), filelist, isRepoRoot, indexptr->IsIgnoreCase());
	*dirstatus = git_wc_status_unknown;
	if (isRepoRoot)
		*dirstatus = git_wc_status_normal;
	else if (indexpos == NPOS && treepos == NPOS)
	{
		// if folder does not contain any versioned items, it might be ignored
		g_IgnoreList.CheckAndUpdateIgnoreFiles(gitdirW, subpathW, true, &localLastCheckCache);
		if (g_IgnoreList.IsIgnore(subpathW, gitdirW, true, adminDir))
			*dirstatus = git_wc_status_ignored;
		folderignoredchecked = true;
	}

	SmartLibgit2::CAutoRepository repository;
	for (const auto & fileentry : filelist)
	{
		const std::wstring onepath = path + fileentry.m_FileName.native();

		const bool bIsDir = onepath.ends_with(L'/');

		int matchLength = -1;
		if (bIsDir)
			matchLength = static_cast<int>(onepath.size());
		size_t pos = SearchInSortVector(*indexptr, onepath, matchLength, indexptr->IsIgnoreCase());
		size_t posintree = SearchInSortVector(*treeptr, onepath, matchLength, indexptr->IsIgnoreCase());

		git_wc_status2_t status = { git_wc_status_none, false, false };

		if (pos == NPOS && posintree == NPOS)
		{
			if (*dirstatus == git_wc_status_ignored)
				status.status = git_wc_status_ignored;
			else
			{
				status.status = git_wc_status_unversioned;

				g_IgnoreList.CheckAndUpdateIgnoreFiles(gitdirW, onepath, bIsDir, &localLastCheckCache);
				// whole folder might be ignored, check this once if we are not the root folder in order to speed up all following ignored files
				if (!folderignoredchecked && *dirstatus != git_wc_status_normal)
				{
					if (g_IgnoreList.IsIgnore(subpathW, gitdirW, true, adminDir))
					{
						*dirstatus = git_wc_status_ignored;
						status.status = git_wc_status_ignored;
					}
					folderignoredchecked = true;
				}
				if (status.status != git_wc_status_ignored && g_IgnoreList.IsIgnore(onepath, gitdirW, bIsDir, adminDir))
					status.status = git_wc_status_ignored;
			}
			callback(CombinePath(gitdir, onepath), &status, bIsDir, fileentry.m_LastModified, pData);
		}
		else if (pos == NPOS && posintree != NPOS) /* check if file delete in index */
		{
			status.status = git_wc_status_deleted;
			callback(CombinePath(gitdir, onepath), &status, bIsDir, fileentry.m_LastModified, pData);
		}
		else if (pos != NPOS && posintree == NPOS) /* Check if file added */
		{
			status.status = git_wc_status_added;
			if ((*indexptr)[pos].m_Flags & tgit::libgit2::TGIT_INDEX_ENTRY_STAGEMASK)
				status.status = git_wc_status_conflicted;
			callback(CombinePath(gitdir, onepath), &status, bIsDir, fileentry.m_LastModified, pData);
		}
		else
		{
			if (bIsDir)
			{
				status.status = git_wc_status_normal;
				callback(CombinePath(gitdir, onepath), &status, bIsDir, fileentry.m_LastModified, pData);
			}
			else
			{
				auto& indexentry = (*indexptr)[pos];
				if (indexentry.m_Flags & tgit::libgit2::TGIT_INDEX_ENTRY_STAGEMASK)
				{
					status.status = git_wc_status_conflicted;
					callback(CombinePath(gitdir, onepath), &status, false, fileentry.m_LastModified, pData);
					continue;
				}
				if (indexptr->GetFileStatus(repository, gitdirW, indexentry, status, fileentry.m_LastModified, fileentry.m_Size, fileentry.m_bSymlink))
					return -1;
				if (status.status == git_wc_status_normal && (*treeptr)[posintree].m_Hash != indexentry.m_IndexHash)
					status = { git_wc_status_modified, false, false };
				callback(CombinePath(gitdir, onepath), &status, false, fileentry.m_LastModified, pData);
			}
		}
	}/*End of For*/
	repository.Free(); // explicitly free the handle here in order to keep an open repository as short as possible

	/* Check deleted file in system */
	std::set<std::wstring> alreadyReported;
	// a run is never empty when there is one: it holds at least the entry at indexpos
	if (const auto indexRun = GetRangeInSortVector(*indexptr, path, path.size(), indexptr->IsIgnoreCase(), indexpos); !indexRun.empty())
	{
		*dirstatus = git_wc_status_normal; // here we know that this folder has versioned entries
		std::wstring oldstring;
		for (const auto& entry : indexRun)
		{
			const int commonPrefixLength = pathLength;
			int index = tgit::wstr::Find(entry.m_FileName, L'/', commonPrefixLength);
			if (index < 0)
				index = static_cast<int>(entry.m_FileName.size());
			else
				++index; // include slash at the end for subfolders, so that we do not match files by mistake

			std::wstring filename = tgit::wstr::Mid(entry.m_FileName, commonPrefixLength, index - commonPrefixLength);
			if (oldstring != filename)
			{
				oldstring = filename;
				const int length = static_cast<int>(filename.size());
				const bool isDir = filename.ends_with(L'/'); // filename[length - 1], which threw on an empty one
				if (SearchInSortVector(filelist, filename, isDir ? length : -1, indexptr->IsIgnoreCase()) == NPOS) // do full match for filenames and only prefix-match ending with "/" for folders
				{
					git_wc_status2_t status = { (!isDir || IsDirectSubmodule(entry.m_FileName, commonPrefixLength)) ? git_wc_status_deleted : git_wc_status_modified, false, false }; // only report deleted submodules and files as deletedy
					if ((entry.m_FlagsExtended & tgit::libgit2::TGIT_INDEX_ENTRY_SKIP_WORKTREE) != 0)
					{
						status.skipWorktree = true;
						status.status = git_wc_status_normal;
						oldstring.clear(); // without this a deleted folder which has two versioned files and only the first is skipwoktree flagged gets reported as normal
						if (alreadyReported.contains(filename))
							continue;
					}
					alreadyReported.insert(filename);
					callback(CombinePath3(gitdir, subpath, filename), &status, isDir, 0, pData);
					if (isDir)
					{
						// folder might be replaced by symlink
						tgit::wstr::TrimRight(filename, L"/");
						auto filepos = SearchInSortVector(filelist, filename, -1, indexptr->IsIgnoreCase());
						if (filepos == NPOS || !filelist[filepos].m_bSymlink)
							continue;
						status.status = git_wc_status_deleted;
						callback(CombinePath3(gitdir, subpath, filename), &status, false, 0, pData);
					}
				}
			}
		}
	}

	if (const auto treeRun = GetRangeInSortVector(*treeptr, path, path.size(), indexptr->IsIgnoreCase(), treepos); !treeRun.empty())
	{
		*dirstatus = git_wc_status_normal; // here we know that this folder has versioned entries
		std::wstring oldstring;
		for (const auto& entry : treeRun)
		{
			const int commonPrefixLength = pathLength;
			int index = tgit::wstr::Find(entry.m_FileName, L'/', commonPrefixLength);
			if (index < 0)
				index = static_cast<int>(entry.m_FileName.size());
			else
				++index; // include slash at the end for subfolders, so that we do not match files by mistake

			std::wstring filename = tgit::wstr::Mid(entry.m_FileName, commonPrefixLength, index - commonPrefixLength);
			if (oldstring != filename && !alreadyReported.contains(filename))
			{
				oldstring = filename;
				const int length = static_cast<int>(filename.size());
				const bool isDir = filename.ends_with(L'/'); // filename[length - 1], which threw on an empty one
				if (SearchInSortVector(filelist, filename, isDir ? length : -1, indexptr->IsIgnoreCase()) == NPOS) // do full match for filenames and only prefix-match ending with "/" for folders
				{
					git_wc_status2_t status = { (!isDir || IsDirectSubmodule(entry.m_FileName, commonPrefixLength)) ? git_wc_status_deleted : git_wc_status_modified, false, false };
					callback(CombinePath3(gitdir, subpath, filename), &status, isDir, 0, pData);
				}
			}
		}
	}
	return 0;
}

}