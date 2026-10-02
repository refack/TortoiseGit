module;
#include <windows.h>
#include <atlstr.h>
#include "sanity.h"



export module TGitStatus;
import std;
import wstr;
import TGitPath;
import PathUtils;
import Registry;
import tgittypes;
import GitHash;

using tgit::types::CacheType;
using tgit::types::git_wc_status2_t;
using tgit::types::git_wc_status_kind;

export namespace GitStatus
{


struct CGitFileName
{
	CGitFileName() = default;
	CGitFileName(LPCWSTR filename, __int64 size, __int64 lastmodified)
	: m_FileName(filename)
	, m_Size(size)
	, m_LastModified(lastmodified)
	{
	}
	CString m_FileName;
	__int64 m_Size = -1;
	__int64 m_LastModified = 0;
	bool	m_bSymlink = false;
};


#define MAX_STATUS_STRING_LENGTH		256

using FILL_STATUS_CALLBACK = BOOL(const CString& path, const git_wc_status2_t* status, bool isDir, __int64 lastwritetime, void* baton);

static CString CombinePath(const CString& part1, const CString& part2)
{
	CString path(part1);
	path += L'\\';
	path += part2;
	return path;
}

static CString CombinePath(const CString& part1, const CString& part2, const CString& part3)
{
	std::wstring path(part1);
	path += L'\\';
	path += tgit::wstr::StringView(part2);
	PathUtils::EnsureTrailingPathDelimiter(path);
	path += tgit::wstr::StringView(part3);
	return path.c_str();
}

/**
 * \ingroup Git
 * Handles git status of working copies.
 */
class GitStatus
{
	git_wc_status2_t			m_status;		// used for GetStatus

	// Returns a numeric value indicating the importance of a status.
	// A higher number indicates a more important status.
	static int GetStatusRanking(git_wc_status_kind status);

public:

	static int GetFileStatus(const CString& gitdir, CString path, git_wc_status2_t& status, BOOL IsFull = FALSE, BOOL isIgnore = TRUE, bool update = true);
	static int GetDirStatus(const CString& gitdir, const CString& path, git_wc_status_kind* status, BOOL IsFull = false, BOOL IsRecursive = false, BOOL isIgnore = true);
	static int EnumDirStatus(const CString& gitdir, const CString& path, git_wc_status_kind* dirstatus, FILL_STATUS_CALLBACK callback, void* pData);
	static int GetFileList(const CString& path_, std::vector<CGitFileName>& list, bool& isRepoRoot, bool ignoreCase);
	static bool IsExistIndexLockFile(const CTGitPath& path);
	static bool ReleasePath(const CString &gitdir);
	static bool ReleasePathsRecursively(const CString &rootpath);

	GitStatus();

	// prevent cloning
	GitStatus(const GitStatus&) = delete;
	GitStatus& operator=(const GitStatus&) = delete;

	/**
	 * Reads the git status of the working copy entry. No
	 * recurse is done, even if the entry is a directory.
	 * If the status of the text and property part are different
	 * then the more important status is returned.
	 */
	static int GetAllStatus(const CTGitPath& path, bool bIsRecursive, git_wc_status2_t& status);

	/**
	 * Returns the status which is more "important" of the two statuses specified.
	 * This is used for the "recursive" status functions on folders - i.e. which status
	 * should be returned for a folder which has several files with different statuses
	 * in it.
	 */
	static git_wc_status_kind GetMoreImportant(git_wc_status_kind status1, git_wc_status_kind status2);

	static void AdjustFolderStatus(git_wc_status_kind& status);

	/**
	 * Reads the git text status of the working copy entry. No
	 * recurse is done, even if the entry is a directory.
	 * The result is stored in the public member variable status.
	 * Use this method if you need detailed information about a file/folder, not just the raw status (like "normal", "modified").
	 *
	 * \param path the pathname of the entry
	 * \param update true if the status should be updated with the repository. Default is false.
	 * \return If update is set to true the HEAD revision of the repository is returned. If update is false then -1 is returned.
	 * \remark If the return value is -2 then the status could not be obtained.
	 */
	void GetStatus(const CTGitPath& path, bool update = false, bool noignore = false, bool noexternals = false);

	/**
	 * This member variable hold the status of the last call to GetStatus().
	 */
	git_wc_status2_t* status = nullptr;	///< the status result of GetStatus()

};


GitStatus::GitStatus()
{
}

int GitStatus::GetAllStatus(const CTGitPath& path, bool bIsRecursive, git_wc_status2_t& status)
{
	std::wstring				sProjectRoot;

	bool isDir = path.IsDirectory();
	if (!path.HasAdminDir(&sProjectRoot))
		return git_wc_status_kind::git_wc_status_none;

	CString sSubPath;
	CString s = path.GetWinPathString().c_str();
	if (static_cast<size_t>(s.GetLength()) > sProjectRoot.size())
	{
		if (sProjectRoot.size() == 3 && sProjectRoot[1] == L':')
			sSubPath = s.Right(s.GetLength() - static_cast<int>(sProjectRoot.size()));
		else
			sSubPath = s.Right(s.GetLength() - static_cast<int>(sProjectRoot.size()) - 1/*otherwise it gets initial slash*/);
	}

	const bool isfull = (
		static_cast<DWORD>(Registry::CRegStdDWORD(L"Software\\TortoiseGit\\CacheType", GetSystemMetrics(SM_REMOTESESSION))
		?
		CacheType::dll
		:
		CacheType::exe
	) == CacheType::dllFull);

	if(isDir)
	{
		auto err = GetDirStatus(sProjectRoot.c_str(), sSubPath, &status.status, isfull, bIsRecursive, isfull);
		AdjustFolderStatus(status.status);
		return err;
	}

	return GetFileStatus(sProjectRoot.c_str(), sSubPath, status, isfull, isfull);
}

// static method
git_wc_status_kind GitStatus::GetMoreImportant(git_wc_status_kind status1, git_wc_status_kind status2)
{
	if (GetStatusRanking(status1) >= GetStatusRanking(status2))
		return status1;
	return status2;
}


// static private method
int GitStatus::GetStatusRanking(git_wc_status_kind status)
{
	using namespace tgit::types;
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
		[[unlikely]]
	}
	return 0;
}

void GitStatus::GetStatus(const CTGitPath& path, bool /*update*/ /* = false */, bool noignore /* = false */, bool /*noexternals*/ /* = false */)
{
	// NOTE: unlike the SVN version this one does not cache the enumerated files, because in practice no code in all of
	//       Tortoise uses this, all places that call GetStatus create a temp GitStatus object which gets destroyed right
	//       after the call again

	std::wstring sProjectRoot;
	if ( !path.HasAdminDir(&sProjectRoot) )
		return;

	const bool isfull = (static_cast<DWORD>(Registry::CRegStdDWORD(L"Software\\TortoiseGit\\CacheType", GetSystemMetrics(SM_REMOTESESSION)) ? CacheType::dll : CacheType::exe) == CacheType::dllFull);

	int err = 0;

	LPCWSTR lpszSubPath = nullptr;
	CString sSubPath;
	CString s = path.GetWinPathString().c_str();
	if (static_cast<size_t>(s.GetLength()) > sProjectRoot.size())
	{
		sSubPath = s.Right(s.GetLength() - static_cast<int>(sProjectRoot.size()));
		lpszSubPath = sSubPath;
		// skip initial slash if necessary
		if (*lpszSubPath == L'\\')
			++lpszSubPath;
	}

	m_status.status = git_wc_status_kind::git_wc_status_none;
	m_status.assumeValid = false;
	m_status.skipWorktree = false;

	if (path.IsDirectory())
	{
		err = GetDirStatus(sProjectRoot.c_str(), lpszSubPath, &m_status.status, isfull, false, !noignore);
		AdjustFolderStatus(m_status.status);
	}
	else
		err = GetFileStatus(sProjectRoot.c_str(), lpszSubPath, m_status, isfull, !noignore);

	// Error present if function is not under version control
	if (err)
	{
		status = nullptr;
		return;
	}

	status = &m_status;
}

using CAutoLocker = CComCritSecLock<CComCriticalSection>;

struct CGitRepoLists
{
	SHARED_INDEX_PTR pIndex;
	SHARED_TREE_PTR pTree;
};

static int GetFileStatus_int(const CString& gitdir, CGitRepoLists& repolists, const CString& path, git_wc_status2_t& status, BOOL IsFull, BOOL IsIgnore, BOOL update)
{
	Expects(repolists.pIndex);
	Expects(!status.assumeValid && !status.skipWorktree);

	using namespace tgit::types;

	CGitHash hash;
	if (repolists.pIndex->GetFileStatus(gitdir, path, status, &hash))
	{
		// an error occurred in GetFileStatus
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
			tgit::DebugOutput::CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": File miss in head tree %s\n", static_cast<LPCWSTR>(path));
			return 0;
		}

		// staged and not commit
		if ((*repolists.pTree)[start].m_Hash != hash)
		{
			status = { git_wc_status_modified, false, false };
			return 0;
		}
	}

	return 0;
}

int GitStatus::GetFileStatus(const CString& gitdir, CString path, git_wc_status2_t& status, BOOL IsFull, BOOL IsIgnore, bool update)
{
	Expects(!status.assumeValid && !status.skipWorktree);

	path.Replace(L'\\', L'/');

	CGitRepoLists sharedRepoLists;
	if (update)
		sharedRepoLists.pIndex = g_IndexFileMap.CheckAndUpdate(gitdir);
	else
		sharedRepoLists.pIndex = g_IndexFileMap.SafeGet(gitdir);
	if (!sharedRepoLists.pIndex)
	{
		// git working tree has broken index
		status.status = git_wc_status_kind::git_wc_status_none;
		return -1;
	}

	return GetFileStatus_int(gitdir, sharedRepoLists, path, status, IsFull, IsIgnore, update);
}

// checks whether indexPath is a direct submodule and not one in a subfolder
static bool IsDirectSubmodule(const CString& indexPath, int prefix)
{
	if (!StringUtils::EndsWith(indexPath, L'/'))
		return false;

	auto ptr = indexPath.GetString() + prefix;
	int folderdepth = 0;
	while (*ptr)
	{
		if (*ptr == L'/')
			++folderdepth;
		++ptr;
	}

	return folderdepth == 1;
}

int GitStatus::GetDirStatus(const CString& gitdir, const CString& subpath, git_wc_status_kind* status, BOOL IsFul, BOOL IsRecursive, BOOL IsIgnore)
{
	using namespace tgit::types;

	Expects(status);

	CString path = subpath;

	path.Replace(L'\\', L'/');
	if (!path.IsEmpty() && path[path.GetLength() - 1] != L'/')
		path += L'/'; //Add trail / to show it is directory, not file name.

	CGitRepoLists sharedRepoLists;
	sharedRepoLists.pIndex = g_IndexFileMap.CheckAndUpdate(gitdir);

	// broken index
	if (!sharedRepoLists.pIndex)
	{
		*status = git_wc_status_none;
		return -1;
	}

	size_t pos = SearchInSortVector(*sharedRepoLists.pIndex, path, path.GetLength(), sharedRepoLists.pIndex->IsIgnoreCase());

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

		sharedRepoLists.pTree = g_HeadFileMap.CheckHeadAndUpdate(gitdir, sharedRepoLists.pIndex->IsIgnoreCase());
		// broken HEAD
		if (!sharedRepoLists.pTree)
		{
			*status = git_wc_status_none;
			return -1;
		}

		// check whether there files in head with are not in index
		pos = SearchInSortVector(*sharedRepoLists.pTree, path, path.GetLength(), sharedRepoLists.pIndex->IsIgnoreCase());
		if (pos != NPOS)
		{
			*status = git_wc_status_deleted;
			return 0;
		}

		// WC root is at least normal if there are no files added/deleted
		if (path.IsEmpty())
		{
			*status = git_wc_status_normal;
			return 0;
		}

		// Check ignore
		g_IgnoreList.CheckAndUpdateIgnoreFiles(gitdir, path, true);
		if (g_IgnoreList.IsIgnore(path, gitdir, true, g_AdminDirMap.GetAdminDir(gitdir)))
			*status = git_wc_status_ignored;
		else
			*status = git_wc_status_unversioned;

		return 0;
	}

	// In version control
	*status = git_wc_status_normal;

	size_t start = 0;
	size_t end = 0;

	GetRangeInSortVector(*sharedRepoLists.pIndex, path,path.GetLength(), sharedRepoLists.pIndex->IsIgnoreCase(), &start, &end, pos);

	// Check Conflict;
	for (auto it = sharedRepoLists.pIndex->cbegin() + start, itlast = sharedRepoLists.pIndex->cbegin() + end; sharedRepoLists.pIndex->m_bHasConflicts && it <= itlast; ++it)
	{
		if (((*it).m_Flags & GIT_INDEX_ENTRY_STAGEMASK) != 0)
		{
			*status = git_wc_status_conflicted;
			// When status == git_wc_status_conflicted, we don't need to check each file status
			// because git_wc_status_conflicted is the highest.
			return 0;
		}
	}

	if (IsFul)
	{
		// Check Add
		{
			// Check if new init repository
			sharedRepoLists.pTree = g_HeadFileMap.CheckHeadAndUpdate(gitdir, sharedRepoLists.pIndex->IsIgnoreCase());
			// broken HEAD
			if (!sharedRepoLists.pTree)
			{
				*status = git_wc_status_none;
				return -1;
			}

			{
				for (auto it = sharedRepoLists.pIndex->cbegin() + start, itlast = sharedRepoLists.pIndex->cbegin() + end; it <= itlast; ++it)
				{
					auto& indexentry = *it;
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
					pos = SearchInSortVector(*sharedRepoLists.pTree, path, path.GetLength(), sharedRepoLists.pIndex->IsIgnoreCase());
					if (pos == NPOS)
						*status = GetMoreImportant(git_wc_status_added, *status); // added file found
					else
					{
						size_t hstart, hend;
						// we know that pos exists in treeptr
						GetRangeInSortVector(*sharedRepoLists.pTree, path, path.GetLength(), sharedRepoLists.pIndex->IsIgnoreCase(), &hstart, &hend, pos);
						for (auto hit = sharedRepoLists.pTree->cbegin() + hstart, lastElement = sharedRepoLists.pTree->cbegin() + hend; hit <= lastElement; ++hit)
						{
							if (SearchInSortVector(*sharedRepoLists.pIndex, (*hit).m_FileName, -1, sharedRepoLists.pIndex->IsIgnoreCase()) == NPOS)
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

	for (auto it = sharedRepoLists.pIndex->cbegin() + start, itlast = sharedRepoLists.pIndex->cbegin() + end; it <= itlast; ++it)
	{
		auto& indexentry = *it;
		// skip child directory, but handle submodules
		if (!IsRecursive && indexentry.m_FileName.Find(L'/', path.GetLength()) > 0 && !IsDirectSubmodule(indexentry.m_FileName, path.GetLength()))
			continue;

		git_wc_status2_t filestatus = { git_wc_status_none, false, false };
		GetFileStatus_int(gitdir, sharedRepoLists, indexentry.m_FileName, filestatus, IsFul, IsIgnore, false);
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


bool GitStatus::IsExistIndexLockFile(const CTGitPath& path)
{
	std::wstring rootDir;
	if (!path.HasAdminDir(&rootDir))
		return false;
	CString indexPath = g_AdminDirMap.GetWorktreeAdminDirConcat(rootDir.c_str(), L"index.lock");
	return !!PathFileExists(indexPath);
}


bool GitStatus::ReleasePath(const CString &gitdir)
{
	g_IndexFileMap.SafeClear(gitdir);
	g_HeadFileMap.SafeClear(gitdir);
	return true;
}

bool GitStatus::ReleasePathsRecursively(const CString &rootpath)
{
	g_IndexFileMap.SafeClearRecursively(rootpath);
	g_HeadFileMap.SafeClearRecursively(rootpath);
	return true;
}


void GitStatus::AdjustFolderStatus(git_wc_status_kind& status)
{
	using namespace tgit::types;

	if (status == git_wc_status_deleted || status == git_wc_status_added)
		status = git_wc_status_modified;
}
}
