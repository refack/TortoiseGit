// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2026 - TortoiseGit
// Copyright (C) 2003-2008, 2014 - TortoiseSVN

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
#include "gittype.h"
#include <string>
#include <string_view>
#include <concepts>

// The string type here is std::wstring rather than CString on purpose, and it
// is not a style choice: CString is CStringT<wchar_t, StrTraitMFC_DLL<...>> in
// an MFC project and CStringT<wchar_t, StrTraitATL<...>> in an ATL one. Those
// mangle differently, so a header carrying CString members or signatures cannot
// be compiled once and linked by both TortoiseGitProc and TortoiseGit.dll.
// CTGitPath is passed through every layer of both, which makes it the widest
// piece of that boundary. See CLAUDE.md, "Making src\ cohesive".
//
// Parameters are std::wstring_view uniformly. A CString call site therefore
// converts by spelling .GetString(), which is one implicit conversion; passing
// the CString itself would need two (operator PCWSTR, then the view's
// constructor) and will not compile.

#define PARENT_MASK   0xFFFFFF
#define MERGE_MASK	(0x1000000)

class CTGitPath
{
public:
	CTGitPath();
#ifdef GOOGLEMOCK_INCLUDE_GMOCK_GMOCK_H_
	virtual ~CTGitPath();
#else
	~CTGitPath();
#endif
	CTGitPath(const std::wstring_view sUnknownPath);
	CTGitPath(const std::wstring_view sUnknownPath, bool bIsDirectory);
	int m_ParentNo = 0;

	enum class StagingStatus
	{
		DontCare,
		TotallyStaged,
		PartiallyStaged,
		TotallyUnstaged
	};

	enum Actions : unsigned int
	{
		LOGACTIONS_ADDED	= 0x00000001,
		LOGACTIONS_MODIFIED	= 0x00000002,
		LOGACTIONS_REPLACED	= 0x00000004,
		LOGACTIONS_DELETED	= 0x00000008,
		LOGACTIONS_UNMERGED = 0x00000010,
		LOGACTIONS_COPY		= 0x00000040,
		LOGACTIONS_MERGED   = 0x00000080,
		LOGACTIONS_ASSUMEVALID = 0x00000200,
		LOGACTIONS_SKIPWORKTREE = 0x00000400,
		LOGACTIONS_MISSING  = 0x00001000,
		LOGACTIONS_UNVER	= 0x80000000,
		LOGACTIONS_IGNORE	= 0x40000000,

		// For log filter only
		LOGACTIONS_HIDE		= 0x20000000,
		LOGACTIONS_GRAY		= 0x10000000,
	};

	std::wstring m_StatAdd;
	std::wstring m_StatDel;
	StagingStatus m_stagingStatus = StagingStatus::DontCare;
#ifdef TGIT_LFS
	std::wstring m_LFSLockOwner;
#endif
	unsigned int m_Action = 0;
	bool m_Checked = false;
	static unsigned ParseStatus(const char status);
	inline void ParseAndUpdateStatus(const char status) { m_Action |= ParseStatus(status); }
	unsigned int ParseAndUpdateStatus(git_delta_t status);
	std::wstring GetActionName() const;
	static std::wstring GetActionName(unsigned int action);
	// Split out so the action-to-resource mapping, including its precedence
	// order, can be asserted without a resource module: loading the string
	// needs an MFC app instance that the test binary does not have.
	static UINT GetActionNameResourceId(unsigned int action);
	/**
	 * Set the path as an UTF8 string with forward slashes
	 */
	void SetFromGit(const char* pPath);
	void SetFromGit(const char* pPath, bool bIsDirectory);
	void SetFromGit(const wchar_t* pPath, bool bIsDirectory);
	void SetFromGit(const std::wstring_view sPath, std::wstring* oldPath = nullptr, int* bIsDirectory = nullptr);

	// Poison, and it earned its place. SetFromGit(cstring, &oldPath) reached the
	// out-parameter overload back when that took CString. It now takes
	// std::wstring_view, which a CString cannot reach implicitly (two
	// user-defined conversions), so the call silently re-bound to
	// SetFromGit(const wchar_t*, bool) - because a pointer converts to bool by
	// a *standard* conversion, which outranks a user-defined one. The
	// out-parameter was read as "this is a directory", the old path was never
	// set, and nothing complained: no compiler error, and no diff to review,
	// because the line itself never changed. Only
	// CTGitPath.GetAbbreviatedRename failing caught it.
	//
	// Deleted for every pointer type including std::wstring*, deliberately.
	// With a const wchar_t* first argument the two-argument call is ambiguous
	// anyway (exact/standard against user-defined/exact, neither better), so
	// there is no spelling of it worth keeping. Pass a std::wstring or an
	// explicit std::wstring_view as the first argument and the intended
	// overload is the only viable one.
	template <typename T>
	void SetFromGit(const wchar_t* pPath, T* notADirectoryFlag) = delete;

	/**
	 * Set the path as UNICODE with backslashes
	 */
	void SetFromWin(LPCWSTR pPath);
	void SetFromWin(const std::wstring_view sPath);
	void SetFromWin(LPCWSTR pPath, bool bIsDirectory);
	void SetFromWin(const std::wstring_view sPath, bool bIsDirectory);
	/**
	 * Set the path from an unknown source.
	 */
	void SetFromUnknown(const std::wstring_view sPath);
	/**
	 * Returns the path in Windows format, i.e. with backslashes
	 */
	LPCWSTR GetWinPath() const;
	/**
	 * Returns the path in Windows format, i.e. with backslashes
	 */
	const std::wstring& GetWinPathString() const;
	/**
	 * Returns the path with forward slashes.
	 */
	const std::wstring& GetGitPathString() const;

	const std::wstring& GetGitOldPathString() const;

	/**
	 * Returns the path for showing in an UI.
	 *
	 * URL's are returned with forward slashes, unescaped if necessary
	 * Paths are returned with backward slashes
	 */
	const std::wstring& GetUIPathString() const;
	/**
	 * Returns true if the path points to a directory
	 */
	bool IsDirectory() const;

	bool IsDirectoryKnown() const { return m_bDirectoryKnown; }

	CTGitPath GetSubPath(const CTGitPath &root) const;

	/**
	 * Returns the directory. If the path points to a directory, then the path
	 * is returned unchanged. If the path points to a file, the path to the
	 * parent directory is returned.
	 */
	CTGitPath GetDirectory() const;
	/**
	 * Same as above, except in the case that the path points to nothing (to a deleted file/folder).
	 * In that case GetDirectory returns the path unchanged and GetDirectoryOrParentIfDeleted returns the path to the parent.
	 */
	CTGitPath GetDirectoryOrParentIfDeleted() const;
	/**
	* Returns the directory which contains the item the path refers to.
	* If the path is a directory, then this returns the directory above it.
	* If the path is to a file, then this returns the directory which contains the path
	* parent directory is returned.
	*/
	CTGitPath GetContainingDirectory() const;
	/**
	 * Get the 'root path' (e.g. "c:\") - Used to pass to GetDriveType
	 */
	std::wstring GetRootPathString() const;
	/**
	 * Returns the filename part of the full path.
	 * \remark don't call this for directories.
	 */
	std::wstring GetFilename() const;
	std::wstring GetBaseFilename() const;
	/**
	 * Returns the item's name without the full path.
	 */
	std::wstring GetFileOrDirectoryName() const;
	/**
	 * Returns the item's name without the full path, unescaped if necessary.
	 */
	std::wstring GetUIFileOrDirectoryName() const;
	/**
	 * Returns the file extension, including the dot.
	 * \remark Returns an empty string for directories
	 */
	std::wstring GetFileExtension() const;

	void UpdateCase();

	bool IsEmpty() const;
	void Reset();
	/**
	 * Checks if two paths are equal. The slashes are taken care of.
	 */
	bool IsEquivalentTo(const CTGitPath& rhs) const;
	bool IsEquivalentToWithoutCase(const CTGitPath& rhs) const;
	bool operator==(const CTGitPath& x) const {return IsEquivalentTo(x);}

	/**
	 * Checks if \c possibleDescendant is a child of this path.
	 */
	bool IsAncestorOf(const CTGitPath& possibleDescendant) const;
	/**
	 * Get a string representing the file path, optionally with a base
	 * section stripped off the front
	 * Returns a string with fwdslash paths
	 */
	std::wstring GetDisplayString(const CTGitPath* pOptionalBasePath = nullptr) const;
	/**
	 * Compares two paths. Slash format is irrelevant.
	 */
	static int Compare(const CTGitPath& left, const CTGitPath& right);

	/** As PredLeftLessThanRight, but for checking if paths are equivalent
	 */
	static bool PredLeftEquivalentToRight(const CTGitPath& left, const CTGitPath& right);

	/** Checks if the left path is pointing to the same working copy path as the right.
	 * The same wc path means the paths are equivalent once all the admin dir path parts
	 * are removed. This is used in the TGitCache crawler to filter out all the 'duplicate'
	 * paths to crawl.
	 */
	static bool PredLeftSameWCPathAsRight(const CTGitPath& left, const CTGitPath& right);

	static bool CheckChild(const CTGitPath &parent, const CTGitPath& child);

	/**
	 * appends a string to this path.
	 *\remark - missing slashes are not added - this is just a string concatenation, but with
	 * preservation of the proper caching behavior.
	 * If you want to join a file- or directory-name onto the path, you should use AppendPathString
	 */
	void AppendRawString(const std::wstring_view sAppend);

	/**
	* appends a part of a path to this path.
	*\remark - missing slashes are dealt with properly. Don't use this to append a file extension, for example
	*
	*/
	void AppendPathString(const std::wstring_view sAppend);

	/**
	 * Get the file modification time - returns zero for files which don't exist
	 */
	__int64 GetLastWriteTime(bool force = false) const;

	/**
	 * Get the file size. Returns zero for directories or files that don't exist.
	 */
	__int64 GetFileSize() const;

	bool IsReadOnly() const;

	/**
	 * Checks if the path really exists.
	 */
	bool Exists() const;

	/**
	 * Deletes the file/folder
	 * \param bTrash if true, uses the Windows trash bin when deleting.
	 */
	bool Delete(bool bTrash, bool bShowErrorUI) const;

	/**
	 * Checks if a git admin directory is present. For files, the check
	 * is done in the same directory. For folders, it checks if the folder itself
	 * contains an admin directory.
	 */
	bool HasAdminDir(std::wstring* projectTopDir = nullptr, bool force = false) const;
	void SetHasAdminDir(bool hasAdminDir, const std::wstring_view projectTopDir) const;
	bool HasSubmodules() const;
	bool HasGitSVNDir() const;
	bool IsBisectActive() const;
	bool IsRebaseActive() const;
	bool IsCherryPickActive() const;
	bool IsMergeActive() const;
	bool HasStashDir() const;
	bool HasRebaseApply() const;
	bool HasLFS() const;

	bool IsWCRoot() const;

	int  GetAdminDirMask() const;

	bool IsRegisteredSubmoduleOfParentProject(std::wstring* parentProjectRoot = nullptr) const;

	/**
	 * Checks if the path point to or below a git admin directory (.Git).
	 */
	bool IsAdminDir() const;

	/**
	 * Checks if the path or URL is valid on Windows.
	 * A path is valid if conforms to the specs in the windows API.
	 * An URL is valid if the path checked out from it is valid
	 * on windows. That means an URL which is valid according to the WWW specs
	 * isn't necessarily valid as a windows path (e.g. http://myserver.com/repos/file:name
	 * is a valid URL, but the path is illegal on windows ("file:name" is illegal), so
	 * this function would return \c false for that URL).
	 */
	bool IsValidOnWindows() const;

	std::wstring GetAbbreviatedRename() const;

private:
	// All these functions are const, and all the data
	// is mutable, in order that the hidden caching operations
	// can be carried out on a const CTGitPath object, which is what's
	// likely to be passed between functions
	// The public 'SetFromxxx' functions are not const, and so the proper
	// const-correctness semantics are preserved
	void SetFwdslashPath(const std::wstring_view sPath) const;
	void SetBackslashPath(const std::wstring_view sPath) const;
	void EnsureBackslashPathSet() const;
	void EnsureFwdslashPathSet() const;

public:
	/**
	 * Marks a path as a file by unsetting the cached IsDirectory status
	 * Used while diffing commits where a submodule changed to a file
	 */
	void UnsetDirectoryStatus() { m_bIsDirectory = false; }
	/**
	 * Marks a path as a directory by setting the cached IsDirectory status
	 * Used while diffing commits where a file changed to a submodule
	 */
	void SetDirectoryStatus() { m_bIsDirectory = true; }

private:
	/**
	 * Adds the required trailing slash to local root paths such as 'C:'
	 */
	void SanitizeRootPath(std::wstring& sPath, bool bIsForwardPath) const;

#ifdef GOOGLEMOCK_INCLUDE_GMOCK_GMOCK_H_
protected:
	virtual void UpdateAttributes() const;
private:
#else
	void UpdateAttributes() const;
#endif

	bool HasStashDir(const std::wstring_view adminDirPath) const;

private:
	mutable std::wstring m_sBackslashPath;
	mutable std::wstring m_sLongBackslashPath;
	mutable std::wstring m_sFwdslashPath;
	mutable std::wstring m_sUIPath;
	mutable std::wstring m_sProjectRoot;

	//used for rename case
	mutable std::wstring m_sOldFwdslashPath;

	// Have we yet determined if this is a directory or not?
	mutable bool m_bDirectoryKnown = false;
	mutable bool m_bIsDirectory = false;
	mutable bool m_bLastWriteTimeKnown = false;
	mutable __int64 m_lastWriteTime = 0;
	mutable __int64 m_fileSize = 0;
	mutable bool m_bIsReadOnly = false;
	mutable bool m_bHasAdminDirKnown = false;
	mutable bool m_bHasAdminDir = false;
	mutable bool m_bIsValidOnWindowsKnown = false;
	mutable bool m_bIsValidOnWindows = false;
	mutable bool m_bIsAdminDirKnown = false;
	mutable bool m_bIsAdminDir = false;
	mutable bool m_bIsWCRootKnown = false;
	mutable bool m_bIsWCRoot = false;
	mutable bool m_bExists = false;
	mutable bool m_bExistsKnown = false;

	friend bool operator<(const CTGitPath& left, const CTGitPath& right);
};
/**
 * Compares two paths and return true if left is earlier in sort order than right
 * (Uses CTGitPath::Compare logic, but is suitable for std::sort and similar)
 */
 bool operator<(const CTGitPath& left, const CTGitPath& right);


//////////////////////////////////////////////////////////////////////////

/**
 * \ingroup Utils
 * This class represents a list of paths
 */
class CTGitPathList
{
public:
	CTGitPathList();
	// A constructor which allows a path list to be easily built with one initial entry in
	explicit CTGitPathList(const CTGitPath& firstEntry);
	unsigned int m_Action = 0;

public:
	void AddPath(const CTGitPath& newPath);
	bool LoadFromFile(const CTGitPath& filename);
	bool WriteToFile(const std::wstring_view sFilename, bool bUTF8 = false) const;
	bool WriteToPathSpecFile(const std::wstring_view sFilename) const;
	const CTGitPath* LookForGitPath(const std::wstring_view path) const;
	int	ParserFromLog(const BYTE_VECTOR& log);
	int ParserFromLsFileSimple(const BYTE_VECTOR& out, unsigned int action, bool clear = true);
	int ParserFromLsFile(const BYTE_VECTOR& out);
	void UpdateStagingStatusFromPath(const std::wstring_view path, CTGitPath::StagingStatus status);
	int FillUnRev(unsigned int Action, const CTGitPathList* filterlist = nullptr, std::wstring* err = nullptr);
#ifdef TGIT_LFS
	int FillLFSLocks(unsigned int action, std::wstring* err = nullptr);
#ifndef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
private:
#endif
	int ParserFromLFSLocks(unsigned int action, const std::wstring_view output, std::wstring* err = nullptr);
#endif
public:
	int FillBasedOnIndexFlags(unsigned short flag, unsigned short flagextended, const CTGitPathList* filterlist = nullptr);
	unsigned int GetAction();
	/**
	 * Load from the path argument string, when the 'path' parameter is used
	 * This is a list of paths, with '*' between them
	 */
	void LoadFromAsteriskSeparatedString(const std::wstring_view sPathString);
	std::wstring CreateAsteriskSeparatedString() const;

	int GetCount() const;
	bool IsEmpty() const;
	void Clear();
	const CTGitPath& operator[](INT_PTR index) const;
	bool AreAllPathsFiles() const;
	bool AreAllPathsDirectories() const;
	bool AreAllPathsFilesInOneDirectory() const;
	bool IsAnyAncestorOf(const CTGitPath& possibleDescendant) const;

	/**
	 * returns the directory which all items have in common.
	 * if not all paths are in the same directory, then
	 * an empty path is returned
	 */
	CTGitPath GetCommonDirectory() const;
	/**
	 * returns the root path of all paths in the list.
	 * only returns an empty path if not all paths are on
	 * the same drive/root.
	 */
	CTGitPath GetCommonRoot() const;
	void SortByPathname(bool bReverse = false);
	/**
	 * Delete all the files in the list, then clear the list.
	 * \param bTrash if true, the items are deleted using the Windows trash bin
	 * \param bShowErrorUI if true, show error dialog box when error occurs.
	 */
	void DeleteAllFiles(bool bTrash, bool bFilesOnly = true, bool bShowErrorUI = false);
	static bool DeleteViaShell(LPCWSTR path, bool useTrashbin, bool bShowErrorUI);
	/** Remove duplicate entries from the list (sorts the list as a side-effect */
	void RemoveDuplicates();
	/** Removes all paths which are on or in a git admin directory */
	void RemoveAdminPaths();
	void RemovePath(const CTGitPath& path);
	void RemoveItem(const CTGitPath& path);
	/**
	 * Removes all child items and leaves only the top folders. Useful if you
	 * create the list to remove them (i.e. if you remove a parent folder, the
	 * child files and folders don't have to be deleted anymore)
	 */
	void RemoveChildren();

	/** Checks if two CTGitPathLists are the same */
	bool IsEqual(const CTGitPathList& list);

	using PathVector = std::vector<CTGitPath>;
	PathVector m_paths;
	// If the list contains just files in one directory, then
	// this contains the directory name
	mutable CTGitPath m_commonBaseDirectory;

	auto begin() noexcept { return m_paths.begin(); }
	auto begin() const noexcept { return m_paths.cbegin(); }
	auto cbegin() const noexcept { return m_paths.cbegin(); }
	auto end() noexcept { return m_paths.end(); }
	auto end() const noexcept { return m_paths.cend(); }
	auto cend() const noexcept { return m_paths.cend(); }
};
