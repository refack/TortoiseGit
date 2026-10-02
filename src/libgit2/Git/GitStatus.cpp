#include <windows.h>
#include <atlstr.h>
#include <git2/index.h>
#include "sanity.h"

import std;

import wil;
import wstr;

import StringUtils;
import Registry;
import RIAA;
import DebugOutput;
import GitHash;
import GitAdminDir;
import GitIndex; // exports g_AdminDirMap itself; the extern declaration here named a different, global one
import SmartLibgit2;
import TGitStatus;
import TGitPath;
import tgittypes;

using std::operator""sv;
namespace fs = std::filesystem;
using namespace tgit;

using tgit::types::CacheType;
using tgit::types::git_wc_status2_t;
using tgit::types::git_wc_status_kind;


namespace GitStatus
{
CGitIndexFileMap g_IndexFileMap;
CGitHeadFileMap g_HeadFileMap;
CGitIgnoreList  g_IgnoreList;


int GitStatus::GetFileList(const CString& path_, std::vector<CGitFileName>& list, bool& isRepoRoot, bool ignoreCase)
{
	const auto path = wstr::to_path(path_);
	for (const auto& data : fs::directory_iterator(path))
	{
		if (data.path().filename() == L".git")
		{
			isRepoRoot = true;
			continue;
		}

		if (data.path().filename() == L"." || data.path().filename() == L"..")
			continue;


		CGitFileName filename(
			data.path().filename().c_str(),
			data.file_size(),
			data.last_write_time().time_since_epoch().count()
		);

		const auto t = data.symlink_status().type();      // link itself, no following
		if (t == fs::file_type::directory) {
			filename.m_FileName += L'/';
		}
		if (t != fs::file_type::regular && t != fs::file_type::directory)
		{
			auto h = PathUtils::query_file(data.path());
			auto basic = wil::GetFileInfo<FileBasicInfo>(h.get());			// times + attributes
			auto std   = wil::GetFileInfo<FileStandardInfo>(h.get());		// size
			if (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
			{
				filename.m_bSymlink = true;
			}
			filename.m_LastModified = basic.LastWriteTime;
			filename.m_Size = std.AllocationSize;
		}

		list.push_back(filename);
	}

	DoSortFilenametSortVector(list, ignoreCase);

	return 0;
}


int GitStatus::EnumDirStatus(const CString& gitdir, const CString& subpath, git_wc_status_kind* dirstatus, FILL_STATUS_CALLBACK callback, void* pData)
{
	using namespace tgit::types;

	CString path = subpath;

	path.Replace(L'\\', L'/');
	if (!path.IsEmpty() && path[path.GetLength() - 1] != L'/')
		path += L'/'; // Add trail / to show it is directory, not file name.

	SHARED_INDEX_PTR indexptr = g_IndexFileMap.CheckAndUpdate(gitdir);
	// there was an error loading the index
	if (!indexptr)
		return -1;

	SHARED_TREE_PTR treeptr = g_HeadFileMap.CheckHeadAndUpdate(gitdir, indexptr->IsIgnoreCase());
	// there was an error loading the HEAD commit/tree
	if (!treeptr)
		return -1;

	size_t indexpos = SearchInSortVector(*indexptr, path, path.GetLength(), indexptr->IsIgnoreCase()); // match path prefix, (sub)folders end with slash
	size_t treepos = SearchInSortVector(*treeptr, path, path.GetLength(), indexptr->IsIgnoreCase()); // match path prefix, (sub)folders end with slash

	std::set<CString> localLastCheckCache;
	CString adminDir = g_AdminDirMap.GetAdminDir(gitdir);

	std::vector<CGitFileName> filelist;
	int folderignoredchecked = false;
	bool isRepoRoot = false;
	GetFileList(CombinePath(gitdir, subpath), filelist, isRepoRoot, indexptr->IsIgnoreCase());
	*dirstatus = git_wc_status_unknown;
	if (isRepoRoot)
		*dirstatus = git_wc_status_normal;
	else if (indexpos == NPOS && treepos == NPOS)
	{
		// if folder does not contain any versioned items, it might be ignored
		g_IgnoreList.CheckAndUpdateIgnoreFiles(gitdir, subpath, true, &localLastCheckCache);
		if (g_IgnoreList.IsIgnore(subpath, gitdir, true, adminDir))
			*dirstatus = git_wc_status_ignored;
		folderignoredchecked = true;
	}

	SmartLibgit2::CAutoRepository repository;
	for (auto it = filelist.cbegin(), itend = filelist.cend(); it != itend; ++it)
	{
		auto& fileentry = *it;

		CString onepath(path);
		onepath += fileentry.m_FileName;

		bool bIsDir = false;
		if (!onepath.IsEmpty() && onepath[onepath.GetLength() - 1] == L'/')
			bIsDir = true;

		int matchLength = -1;
		if (bIsDir)
			matchLength = onepath.GetLength();
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

				g_IgnoreList.CheckAndUpdateIgnoreFiles(gitdir, onepath, bIsDir, &localLastCheckCache);
				// whole folder might be ignored, check this once if we are not the root folder in order to speed up all following ignored files
				if (!folderignoredchecked && *dirstatus != git_wc_status_normal)
				{
					if (g_IgnoreList.IsIgnore(subpath, gitdir, true, adminDir))
					{
						*dirstatus = git_wc_status_ignored;
						status.status = git_wc_status_ignored;
					}
					folderignoredchecked = true;
				}
				if (status.status != git_wc_status_ignored && g_IgnoreList.IsIgnore(onepath, gitdir, bIsDir, adminDir))
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
			if ((*indexptr)[pos].m_Flags & GIT_INDEX_ENTRY_STAGEMASK)
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
				if (indexentry.m_Flags & GIT_INDEX_ENTRY_STAGEMASK)
				{
					status.status = git_wc_status_conflicted;
					callback(CombinePath(gitdir, onepath), &status, false, fileentry.m_LastModified, pData);
					continue;
				}
				if ((*indexptr).GetFileStatus(repository, gitdir, indexentry, status, fileentry.m_LastModified, fileentry.m_Size, fileentry.m_bSymlink))
					return -1;
				if (status.status == git_wc_status_normal && (*treeptr)[posintree].m_Hash != indexentry.m_IndexHash)
					status = { git_wc_status_modified, false, false };
				callback(CombinePath(gitdir, onepath), &status, false, fileentry.m_LastModified, pData);
			}
		}
	}/*End of For*/
	repository.Free(); // explicitly free the handle here in order to keep an open repository as short as possible

	/* Check deleted file in system */
	size_t start = 0, end = 0;
	std::set<CString> alreadyReported;
	if (GetRangeInSortVector(*indexptr, path, path.GetLength(), indexptr->IsIgnoreCase(), &start, &end, indexpos) == 0)
	{
		*dirstatus = git_wc_status_normal; // here we know that this folder has versioned entries
		CString oldstring;
		for (auto it = indexptr->cbegin() + start, itlast = indexptr->cbegin() + end; it <= itlast; ++it)
		{
			auto& entry = *it;
			const int commonPrefixLength = path.GetLength();
			int index = entry.m_FileName.Find(L'/', commonPrefixLength);
			if (index < 0)
				index = entry.m_FileName.GetLength();
			else
				++index; // include slash at the end for subfolders, so that we do not match files by mistake

			CString filename = entry.m_FileName.Mid(commonPrefixLength, index - commonPrefixLength);
			if (oldstring != filename)
			{
				oldstring = filename;
				const int length = filename.GetLength();
				const bool isDir = filename[length - 1] == L'/';
				if (SearchInSortVector(filelist, filename, isDir ? length : -1, indexptr->IsIgnoreCase()) == NPOS) // do full match for filenames and only prefix-match ending with "/" for folders
				{
					git_wc_status2_t status = { (!isDir || IsDirectSubmodule(entry.m_FileName, commonPrefixLength)) ? git_wc_status_deleted : git_wc_status_modified, false, false }; // only report deleted submodules and files as deletedy
					if ((entry.m_FlagsExtended & GIT_INDEX_ENTRY_SKIP_WORKTREE) != 0)
					{
						status.skipWorktree = true;
						status.status = git_wc_status_normal;
						oldstring.Empty(); // without this a deleted folder which has two versioned files and only the first is skipwoktree flagged gets reported as normal
						if (alreadyReported.find(filename) != alreadyReported.cend())
							continue;
					}
					alreadyReported.insert(filename);
					callback(CombinePath(gitdir, subpath, filename), &status, isDir, 0, pData);
					if (isDir)
					{
						// folder might be replaced by symlink
						filename.TrimRight(L'/');
						auto filepos = SearchInSortVector(filelist, filename, -1, indexptr->IsIgnoreCase());
						if (filepos == NPOS || !filelist[filepos].m_bSymlink)
							continue;
						status.status = git_wc_status_deleted;
						callback(CombinePath(gitdir, subpath, filename), &status, false, 0, pData);
					}
				}
			}
		}
	}

	start = end = 0;
	if (GetRangeInSortVector(*treeptr, path, path.GetLength(), indexptr->IsIgnoreCase(), &start, &end, treepos) == 0)
	{
		*dirstatus = git_wc_status_normal; // here we know that this folder has versioned entries
		CString oldstring;
		for (auto it = treeptr->cbegin() + start, itlast = treeptr->cbegin() + end; it <= itlast; ++it)
		{
			auto& entry = *it;
			const int commonPrefixLength = path.GetLength();
			int index = entry.m_FileName.Find(L'/', commonPrefixLength);
			if (index < 0)
				index = entry.m_FileName.GetLength();
			else
				++index; // include slash at the end for subfolders, so that we do not match files by mistake

			CString filename = entry.m_FileName.Mid(commonPrefixLength, index - commonPrefixLength);
			if (oldstring != filename && alreadyReported.find(filename) == alreadyReported.cend())
			{
				oldstring = filename;
				const int length = filename.GetLength();
				const bool isDir = filename[length - 1] == L'/';
				if (SearchInSortVector(filelist, filename, isDir ? length : -1, indexptr->IsIgnoreCase()) == NPOS) // do full match for filenames and only prefix-match ending with "/" for folders
				{
					git_wc_status2_t status = { (!isDir || IsDirectSubmodule(entry.m_FileName, commonPrefixLength)) ? git_wc_status_deleted : git_wc_status_modified, false, false };
					callback(CombinePath(gitdir, subpath, filename), &status, isDir, 0, pData);
				}
			}
		}
	}
	return 0;
}


}