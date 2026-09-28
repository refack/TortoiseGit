module;
#include <sys/stat.h>
#include <gitdll.h>
#include <git2.h>
#include <atlstr.h>

#include "Git.h"
#include "GitStatus.h"
#include "Utils/ReaderWriterLock.h"

export module GitIndex;
import std;
import gsl;
import GitAdminDir;
import SmartLibgit2;
import wstr;
import StringUtils;
import Registry;
import PathUtils;
import DebugOutput;
import RIAA;
import TGitPath;
import TGitHash;

using SmartLibgit2::CAutoRepository;
using SmartLibgit2::CAutoConfig;
using namespace Registry;
using namespace PathUtils::PathUtils;

#ifndef S_IFLNK
#define S_IFLNK 0120000
#undef _S_IFLNK
#define _S_IFLNK S_IFLNK
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & _S_IFMT) == _S_IFLNK)
#endif

namespace GitIndex {
struct CGitIndex
{
	/* m_Size and m_ModifyTime are only uint32_t in libgit2, cf. https://github.com/libgit2/libgit2/blob/8535fdb9cbad8fcd15ee4022ed29c4138547e22d/include/git2/index.h#L48-L51 and https://tortoisegit.org/issue/4108 */
	CString    m_FileName;
	mutable int32_t	m_ModifyTime;
	mutable uint32_t	m_ModifyTimeNanos;
	uint16_t	m_Flags;
	uint16_t	m_FlagsExtended;
	CGitHash	m_IndexHash;
	uint32_t	m_Size;
	uint32_t	m_Mode;

	int Print();
};

class CGitIndexList : private std::vector<CGitIndex>
{
public:
	BOOL		m_bHasConflicts = FALSE;
	CString		m_branch;
	size_t		m_incoming = static_cast<size_t>(-1);
	size_t		m_outgoing = static_cast<size_t>(-1);
	size_t		m_stashCount = 0;
	inline bool IsIgnoreCase() const { return m_iIndexCaps & GIT_INDEX_CAPABILITY_IGNORE_CASE; }

	CGitIndexList();
	~CGitIndexList();

	bool HasIndexChangedOnDisk(const CString& gitdir) const;
	int ReadIndex(const CString& dotgitdir);
	int ReadIncomingOutgoing(git_repository* repo);
	int GetFileStatus(const CString& gitdir, const CString& path, git_wc_status2_t& status, CGitHash* pHash = nullptr) const;
	int GetFileStatus(CAutoRepository& repository, const CString& gitdir, const CGitIndex& entry, git_wc_status2_t& status, __int64 time, __int64 filesize, bool isSymlink) const;

	using std::vector<CGitIndex>::begin;
	using std::vector<CGitIndex>::end;
	using std::vector<CGitIndex>::cbegin;
	using std::vector<CGitIndex>::cend;
	using std::vector<CGitIndex>::empty;
	using std::vector<CGitIndex>::size;
	using std::vector<CGitIndex>::operator[];

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
	FRIEND_TEST(GitIndexCBasicGitWithTestRepoFixture, GetFileStatus);
#endif
private:
	__time64_t m_LastModifyTime = 0;
	__int64 m_LastFileSize = -1;

	int		m_iIndexCaps = GIT_INDEX_CAPABILITY_IGNORE_CASE | GIT_INDEX_CAPABILITY_NO_SYMLINKS;
	__int64 m_iMaxCheckSize = 10 * 1024 * 1024;
	bool	m_bCalculateIncomingOutgoing = true;
	CAutoConfig config;
	int GetFileStatus(const CString& gitdir, const CString& path, git_wc_status2_t& status, __int64 time, __int64 filesize, bool isSymlink, CGitHash* pHash = nullptr) const;
};

using SHARED_INDEX_PTR = std::shared_ptr<const CGitIndexList>;
using CAutoLocker = CComCritSecLock<CComCriticalSection>;

template<typename SharedPtr>
class SharedPtrMapTmpl : private std::map<CString, SharedPtr>
{
public:
	[[nodiscard]] SharedPtr SafeGet(const CString& path)
	{
		CString thePath(NormalizePath(std::wstring(path)).c_str());
		[[maybe_unused]]CAutoLocker lock(m_critSec);
		auto lookup = this->find(thePath);
		if (lookup == this->cend())
			return {};
		return lookup->second;
	}

	bool SafeClear(const CString& path)
	{
		CString thePath(NormalizePath(std::wstring(path)).c_str());
		[[maybe_unused]]CAutoLocker lock(m_critSec);
		auto lookup = this->find(thePath);
		if (lookup == this->cend())
			return false;
		this->erase(lookup);
		return true;
	}

	bool SafeClearRecursively(const CString& path)
	{
		CString thePath(NormalizePath(std::wstring(path)).c_str());
		[[maybe_unused]]CAutoLocker lock(m_critSec);
		std::vector<CString> toRemove;
		for (auto it = this->cbegin(); it != this->cend(); ++it)
		{
			if (StringUtils::CStringUtils::StartsWith((*it).first, thePath))
				toRemove.push_back((*it).first);
		}
		for (auto it = toRemove.cbegin(); it != toRemove.cend(); ++it)
			this->erase(*it);
		return !toRemove.empty();
	}

protected:
	void SafeSet(const CString& path, SharedPtr ptr)
	{
		CString thePath(NormalizePath(std::wstring(path)).c_str());
		[[maybe_unused]]CAutoLocker lock(m_critSec);
		(*this)[thePath] = ptr;
	}

private:
	CComAutoCriticalSection m_critSec;
};

class CGitIndexFileMap : protected SharedPtrMapTmpl<SHARED_INDEX_PTR>
{
public:
	[[nodiscard]] SHARED_INDEX_PTR CheckAndUpdate(const CString& gitdir)
	{
		if (auto pIndex = SafeGet(gitdir); pIndex && !pIndex->HasIndexChangedOnDisk(gitdir))
			return pIndex;

		auto newIndex = std::make_shared<CGitIndexList>();
		if (newIndex->ReadIndex(gitdir))
		{
			SafeClear(gitdir);
			return {};
		}

		SafeSet(gitdir, newIndex);

		return newIndex;
	}

	using SharedPtrMapTmpl<SHARED_INDEX_PTR>::SafeClear;
	using SharedPtrMapTmpl<SHARED_INDEX_PTR>::SafeClearRecursively;
	using SharedPtrMapTmpl<SHARED_INDEX_PTR>::SafeGet;
};

struct CGitTreeItem
{
	CString	m_FileName;
	CGitHash	m_Hash;
	int			m_Flags;
};

/* After object create, never change field against
 * that needn't lock to get field
*/
class CGitHeadFileList : private std::vector<CGitTreeItem>
{
private:
	int GetPackRef(const CString &gitdir);

	__time64_t	m_LastModifyTimeHead = 0;
	__time64_t	m_LastModifyTimeRef = 0;
	__time64_t	m_LastModifyTimePackRef = 0;

	__int64		m_LastFileSizeHead = -1;
	__int64		m_LastFileSizePackRef = -1;

	CString		m_HeadRefFile;
	CGitHash	m_Head;
	CString		m_HeadFile;
	CString		m_Gitdir;
	CString		m_PackRefFile;
	bool		m_bRefFromPackRefFile = false;

	std::map<CString,CGitHash> m_PackRefMap;

public:
	CGitHeadFileList() = default;

	int ReadTree(bool ignoreCase);
	int ReadHeadHash(const CString& gitdir);
	bool CheckHeadUpdate() const;

	using std::vector<CGitTreeItem>::begin;
	using std::vector<CGitTreeItem>::end;
	using std::vector<CGitTreeItem>::cbegin;
	using std::vector<CGitTreeItem>::cend;
	using std::vector<CGitTreeItem>::empty;
	using std::vector<CGitTreeItem>::size;
	using std::vector<CGitTreeItem>::operator[];

private:
	int ReadTreeRecursive(git_repository& repo, const git_tree* tree, const CString& base);
};

using SHARED_TREE_PTR = std::shared_ptr<const CGitHeadFileList>;
class CGitHeadFileMap : protected SharedPtrMapTmpl<SHARED_TREE_PTR>
{
public:
	[[nodiscard]] SHARED_TREE_PTR CheckHeadAndUpdate(const CString& gitdir, bool ignoreCase);

	using SharedPtrMapTmpl<SHARED_TREE_PTR>::SafeClear;
	using SharedPtrMapTmpl<SHARED_TREE_PTR>::SafeClearRecursively;
	using SharedPtrMapTmpl<SHARED_TREE_PTR>::SafeGet;
};

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

class CGitIgnoreItem
{
public:
	~CGitIgnoreItem()
	{
		if(m_pExcludeList)
			git_free_exclude_list(m_pExcludeList);
	}

	__time64_t	m_LastModifyTime = 0;
	__int64		m_LastFileSize = -1;
	CStringA m_BaseDir;
	std::unique_ptr<char[]> m_buffer;
	EXCLUDE_LIST m_pExcludeList = nullptr;
	int* m_iIgnoreCase = nullptr;

	int FetchIgnoreList(const CString& projectroot, const CString& file, bool isGlobal, int* ignoreCase);

	/**
	* patha: the filename to be checked whether it is ignored or not
	* base: must be a pointer to the beginning of the base filename WITHIN patha
	* type: DT_DIR or DT_REG
	*/
	int IsPathIgnored(const CStringA& patha, const char* base, int& type);
#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
	int IsPathIgnored(const CStringA& patha, int& type);
#endif
};

class CGitIgnoreList
{
private:
	bool CheckFileChanged(const CString &path);
	int FetchIgnoreFile(const CString &gitdir, const CString &gitignore, bool isGlobal);

	int CheckIgnore(const CString& path, const CString& root, bool isDir, const CString& adminDir);
	int CheckFileAgainstIgnoreList(const CString &ignorefile, const CStringA &patha, const char * base, int &type);

	// core.excludesfile stuff
	std::map<CString, CString> m_CoreExcludesfiles;
	std::map<CString, int> m_IgnoreCase;
	CString m_sGitSystemConfigPath;
	ULONGLONG m_dGitSystemConfigPathLastChecked = 0LL;
	CReaderWriterLock	m_coreExcludefilesSharedMutex;
	// checks if the msysgit path has changed and return true/false
	// if the path changed, the cache is update
	// force is only ised in constructor
	bool CheckAndUpdateGitSystemConfigPath(bool force = true);
	bool CheckAndUpdateCoreExcludefile(const CString &adminDir);
	const CString GetWindowsHome();

public:
	CReaderWriterLock		m_SharedMutex;

	CGitIgnoreList(){ CheckAndUpdateGitSystemConfigPath(true); }

	std::map<CString, CGitIgnoreItem> m_Map;

	bool CheckAndUpdateIgnoreFiles(const CString& gitdir, const CString& path, bool isDir, std::set<CString>* lastChecked = nullptr);
	bool IsIgnore(CString path, const CString& root, bool isDir, const CString& adminDir);
};

template<class T>
inline void DoSortFilenametSortVector(T& vector, bool ignoreCase)
{
	if (ignoreCase)
		std::sort(vector.begin(), vector.end(), [](const auto& e1, const auto& e2) { return e1.m_FileName.CompareNoCase(e2.m_FileName) < 0; });
	else
		std::sort(vector.begin(), vector.end(), [](const auto& e1, const auto& e2) { return e1.m_FileName.Compare(e2.m_FileName) < 0; });
}

static const size_t NPOS = static_cast<size_t>(-1); // bad/missing length/position
static_assert(MAXSIZE_T == NPOS, "NPOS must equal MAXSIZE_T");
static_assert(-1 == static_cast<int>(NPOS), "NPOS must equal -1");

template<class T>
inline int GetRangeInSortVector(const T& vector, LPCWSTR pstr, size_t len, bool ignoreCase, size_t* start, size_t* end, size_t pos)
{
	if (ignoreCase)
		return GetRangeInSortVector_int(vector, pstr, len, _wcsnicmp, start, end, pos);

	return GetRangeInSortVector_int(vector, pstr, len, wcsncmp, start, end, pos);
}

template<class T, class V>
int GetRangeInSortVector_int(const T& vector, LPCWSTR pstr, size_t len, V compare, size_t* start, size_t* end, size_t pos)
{
	static_assert(std::is_convertible_v<V, std::function<int(const wchar_t*, const wchar_t*, size_t)>>, "Wrong signature for the compare method, needs to be a wcsncmp equivalent");
	if (pos == NPOS)
		return -1;
	if (!start || !end)
		return -1;

	*start = *end = NPOS;

	if (vector.empty())
		return -1;

	if (pos >= vector.size())
		return -1;

	if (compare(vector[pos].m_FileName, pstr, len) != 0)
		return -1;

	*start = 0;
	*end = vector.size() - 1;

	// shortcut, if all entries are going match
	if (!len)
		return 0;

	for (size_t i = pos; i < vector.size(); ++i)
	{
		if (compare(vector[i].m_FileName, pstr, len) != 0)
			break;

		*end = i;
	}
	for (size_t i = pos + 1; i-- > 0;)
	{
		if (compare(vector[i].m_FileName, pstr, len) != 0)
			break;

		*start = i;
	}

	return 0;
}

template<class T>
inline size_t SearchInSortVector(const T& vector, LPCWSTR pstr, int len, bool ignoreCase)
{
	if (ignoreCase)
	{
		if (len < 0)
			return SearchInSortVector_int(vector, pstr, _wcsicmp);

		return SearchInSortVector_int(vector, pstr, [len](const auto& s1, const auto& s2) { return _wcsnicmp(s1, s2, len); });
	}

	if (len < 0)
		return SearchInSortVector_int(vector, pstr, wcscmp);

	return SearchInSortVector_int(vector, pstr, [len](const auto& s1, const auto& s2) { return wcsncmp(s1, s2, len); });
}

template<class T, class V>
size_t SearchInSortVector_int(const T& vector, LPCWSTR pstr, V compare)
{
	static_assert(std::is_convertible_v<V, std::function<int(const wchar_t*, const wchar_t*)>>, "Wrong signature for the compare method, needs to be a wcscmp equivalent");
	size_t end = vector.size() - 1;
	size_t start = 0;
	size_t mid = (start + end) / 2;

	if (vector.empty())
		return NPOS;

	while(!( start == end && start==mid))
	{
		const int cmp = compare(vector[mid].m_FileName, pstr);
		if (cmp == 0)
			return mid;
		else if (cmp < 0)
			start = mid + 1;
		else // (cmp > 0)
			end = mid;

		mid=(start +end ) /2;

	}

	if (compare(vector[mid].m_FileName, pstr) == 0)
		return mid;

	return NPOS;
};

class CGitAdminDirMap : private std::map<CString, CString>
{
public:
	CComAutoCriticalSection		m_critIndexSec;
	std::map<CString, CString>	m_reverseLookup;
	std::map<CString, CString>	m_WorktreeAdminDirLookup;

	CString GetAdminDir(const CString &path)
	{
		CString thePath(NormalizePath(std::wstring(path)).c_str());
		[[maybe_unused]]CAutoLocker lock(m_critIndexSec);
		auto lookup = find(thePath);
		if (lookup == cend())
		{
			CString adminDir;
			bool isWorktree = false;
			if (GitAdminDir::GetAdminDirPath(path, adminDir, &isWorktree) && PathIsDirectory(adminDir))
			{
				(*this)[thePath] = adminDir;
				if (!isWorktree) // GitAdminDir::GetAdminDirPath returns the commongit dir ("parent/.git") and this would override the lookup path for the main repo
					m_reverseLookup[BuildPathWithPathDelimiter(NormalizePath(std::wstring(adminDir))).c_str()] = path;
				return (*this)[thePath];
			}
			ATLASSERT(false);
			return (BuildPathWithPathDelimiter(tgit::wstr::StringView(path)) + L".git\\").c_str(); // in case of an error stick to old behavior
		}

		return lookup->second;
	}

	CString GetAdminDirConcat(const CString& path, const CString& subpath)
	{
		CString result(GetAdminDir(path));
		result += subpath;
		return result;
	}

	CString GetWorktreeAdminDir(const CString& path)
	{
		CString thePath(NormalizePath(std::wstring(path)).c_str());
		[[maybe_unused]]CAutoLocker lock(m_critIndexSec);
		auto lookup = m_WorktreeAdminDirLookup.find(thePath);
		if (lookup == m_WorktreeAdminDirLookup.cend())
		{
			CString wtadmindir;
			if (GitAdminDir::GetWorktreeAdminDirPath(path, wtadmindir) && PathIsDirectory(wtadmindir))
			{
				m_WorktreeAdminDirLookup[thePath] = wtadmindir;
				m_reverseLookup[BuildPathWithPathDelimiter(NormalizePath(std::wstring(wtadmindir))).c_str()] = path;
				return m_WorktreeAdminDirLookup[thePath];
			}
			ATLASSERT(false);
			return (BuildPathWithPathDelimiter(tgit::wstr::StringView(path)) + L".git\\").c_str(); // we should never get here
		}
		return lookup->second;
	}

	CString GetWorktreeAdminDirConcat(const CString& path, const CString& subpath)
	{
		CString result(GetWorktreeAdminDir(path));
		result += subpath;
		return result;
	}

	CString GetWorkingCopy(const CString &gitDir)
	{
		CString path(BuildPathWithPathDelimiter(NormalizePath(std::wstring(gitDir))).c_str());
		[[maybe_unused]]CAutoLocker lock(m_critIndexSec);
		auto lookup = m_reverseLookup.find(path);
		if (lookup == m_reverseLookup.cend())
			return gitDir;
		return lookup->second;
	}

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
	using std::map<CString, CString>::clear;
#endif
};


CGitAdminDirMap g_AdminDirMap;

// this file is compiled into projects with and without GIT_EXPERIMENTAL_SHA256; only SHA1 repos are supported so far
static int tgit_odb_hash(git_oid* out, const void* data, size_t len, git_object_t type)
{
#ifdef GIT_EXPERIMENTAL_SHA256
	return git_odb_hash(out, data, len, type, GIT_OID_SHA1);
#else
	return git_odb_hash(out, data, len, type);
#endif
}

int CGitIndex::Print()
{
	wprintf(L"0x%08X  0x%08X %s %s\n",
		static_cast<int>(this->m_ModifyTime),
		this->m_Flags,
		static_cast<LPCWSTR>(this->m_IndexHash.ToString().c_str()),
		static_cast<LPCWSTR>(this->m_FileName));

	return 0;
}

CGitIndexList::CGitIndexList()
{
#ifndef TGIT_TESTS_ONLY
	m_iMaxCheckSize = static_cast<__int64>(CRegDWORD(L"Software\\TortoiseGit\\TGitCacheCheckContentMaxSize", 10 * 1024)) * 1024; // stored in KiB
	m_bCalculateIncomingOutgoing = (CRegStdDWORD(L"Software\\TortoiseGit\\ModifyExplorerTitle", TRUE) != FALSE);
#endif
}

CGitIndexList::~CGitIndexList()
{
}

bool CGitIndexList::HasIndexChangedOnDisk(const CString& gitdir) const
{
	__int64 time = -1, size = -1;

	CString indexFile = g_AdminDirMap.GetWorktreeAdminDirConcat(gitdir, L"index");
	// no need to refresh if there is no index right now and the current index is empty, but otherwise lastFileSize or lastmodifiedTime differ
	return (GetFileModifyTime(indexFile, &time, nullptr, &size) && !empty()) || m_LastModifyTime != time || m_LastFileSize != size;
}

int CGitIndexList::ReadIndex(const CString& dgitdir)
{
#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
	clear(); // HACK to make tests work, until we use CGitIndexList
#endif
	ATLASSERT(empty());

	CString repodir = dgitdir;
	if (dgitdir.GetLength() == 2 && dgitdir[1] == L':')
		repodir += L'\\'; // libgit2 requires a drive root to end with a (back)slash

	CAutoRepository repository(repodir);
	if (!repository)
	{
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": Could not open git repository in %s: %s\n", static_cast<LPCWSTR>(dgitdir), static_cast<LPCWSTR>(GetLibGit2LastErr()));
		return -1;
	}

	CString projectConfig = g_AdminDirMap.GetAdminDir(dgitdir) + L"config";
	CString globalConfig = g_Git.GetGitGlobalConfig();
	CString globalXDGConfig = g_Git.GetGitGlobalXDGConfig();
	CString systemConfig(CRegStdString<CString>(REG_SYSTEM_GITCONFIGPATH, L"", false));

	CAutoConfig temp { true };
	git_config_add_file_ondisk(temp, CGit::GetGitPathStringA(projectConfig), GIT_CONFIG_LEVEL_LOCAL, repository, FALSE);
	git_config_add_file_ondisk(temp, CGit::GetGitPathStringA(globalConfig), GIT_CONFIG_LEVEL_GLOBAL, repository, FALSE);
	git_config_add_file_ondisk(temp, CGit::GetGitPathStringA(globalXDGConfig), GIT_CONFIG_LEVEL_XDG, repository, FALSE);
	if (!systemConfig.IsEmpty())
		git_config_add_file_ondisk(temp, CGit::GetGitPathStringA(systemConfig), GIT_CONFIG_LEVEL_SYSTEM, repository, FALSE);

	git_config_snapshot(config.GetPointer(), temp);
	temp.Free();
	git_repository_set_config(repository, config);

	CGit::GetFileModifyTime(g_AdminDirMap.GetWorktreeAdminDir(dgitdir) + L"index", &m_LastModifyTime, nullptr, &m_LastFileSize);

	CAutoIndex index;
	// load index in order to enumerate files
	if (git_repository_index(index.GetPointer(), repository))
	{
		config.Free();
		CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": Could not get index of git repository in %s: %s\n", static_cast<LPCWSTR>(dgitdir), static_cast<LPCWSTR>(GetLibGit2LastErr()));
		return -1;
	}

	m_bHasConflicts = FALSE;
	m_iIndexCaps = git_index_caps(index);
	if (CRegDWORD(L"Software\\TortoiseGit\\OverlaysCaseSensitive", TRUE) != FALSE)
		m_iIndexCaps &= ~GIT_INDEX_CAPABILITY_IGNORE_CASE;

	const size_t ecount = git_index_entrycount(index);
	try
	{
		resize(ecount);
	}
	catch (const std::bad_alloc& ex)
	{
		config.Free();
		CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Could not resize index-vector: %s\n", ex.what());
		return -1;
	}
	catch (const std::length_error& ex)
	{
		config.Free();
		CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Could not resize index-vector, length_error: %s\n", ex.what());
		return -1;
	}
	for (size_t i = 0; i < ecount; ++i)
	{
		const git_index_entry *e = git_index_get_byindex(index, i);

		auto& item = (*this)[i];
		item.m_FileName = CUnicodeUtils::GetUnicode<CString>(e->path);
		if (e->mode & S_IFDIR)
			item.m_FileName += L'/';
		static_assert(std::is_same<decltype(item.m_ModifyTime), decltype(e->mtime.seconds)>::value);
		item.m_ModifyTime = e->mtime.seconds;
		static_assert(std::is_same<decltype(item.m_ModifyTimeNanos), decltype(e->mtime.nanoseconds)>::value);
		item.m_ModifyTimeNanos = e->mtime.nanoseconds;
		item.m_Flags = e->flags;
		item.m_FlagsExtended = e->flags_extended;
		item.m_IndexHash = e->id;
		static_assert(std::is_same<decltype(item.m_Size), decltype(e->file_size)>::value);
		item.m_Size = e->file_size;
		item.m_Mode = e->mode;
		m_bHasConflicts |= GIT_INDEX_ENTRY_STAGE(e);
	}

	DoSortFilenametSortVector(*this, IsIgnoreCase());

	ReadIncomingOutgoing(repository);

	CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": Reloaded index for repo: %s\n", static_cast<LPCWSTR>(dgitdir));

	return 0;
}

int CGitIndexList::ReadIncomingOutgoing(git_repository* repository)
{
	ATLASSERT(m_stashCount == 0 && m_outgoing == static_cast<size_t>(-1) && m_incoming == static_cast<size_t>(-1) && m_branch.IsEmpty());

	if (!m_bCalculateIncomingOutgoing)
		return 0;

	if (git_stash_foreach(repository, [](size_t, const char*, const git_oid*, void* payload) -> int {
		auto stashCount = static_cast<size_t*>(payload);
		++(*stashCount);
		return 0;
		}, &m_stashCount) < 0)
		return -1;

	if (const int detachedhead = git_repository_head_detached(repository); detachedhead == 1)
	{
		m_branch = L"detached HEAD";
		return 0;
	}
	else if (detachedhead < 0)
		return -1;

	CAutoReference head;
	if (const int unborn = git_repository_head_unborn(repository); unborn < 0)
		return -1;
	else if (unborn == 1)
	{
		if (git_reference_lookup(head.GetPointer(), repository, "HEAD") < 0)
			return -1;

		m_branch = CGit::StripRefName(CUnicodeUtils::GetUnicode<CString>(git_reference_symbolic_target(head)));
		return 0;
	}

	if (git_repository_head(head.GetPointer(), repository) < 0)
		return -1;

	m_branch = CUnicodeUtils::GetUnicode<CString>(git_reference_shorthand(head));

	CAutoBuf upstreambranchname;
	git_oid upstream{};
	// check whether there is an upstream branch
	if (git_branch_upstream_name(upstreambranchname, repository, git_reference_name(head)) != 0 || git_reference_name_to_id(&upstream, repository, upstreambranchname->ptr) != 0)
		return 0; // we don't have an upstream branch

	if (git_graph_ahead_behind(&m_outgoing, &m_incoming, repository, git_reference_target(head), &upstream) < 0)
		return -1;

	return 0;
}

int CGitIndexList::GetFileStatus(const CString& gitdir, const CString& pathorg, git_wc_status2_t& status, __int64 time, __int64 filesize, bool isSymlink, CGitHash* pHash) const
{
	size_t index = SearchInSortVector(*this, pathorg, -1, IsIgnoreCase());

	if (index == NPOS)
	{
		status.status = git_wc_status_unversioned;
		if (pHash)
			pHash->Empty();

		return 0;
	}

	auto& entry = (*this)[index];
	if (pHash)
		*pHash = entry.m_IndexHash;
	ATLASSERT(IsIgnoreCase() ? pathorg.CompareNoCase(entry.m_FileName) == 0 : pathorg.Compare(entry.m_FileName) == 0);
	CAutoRepository repository;
	return GetFileStatus(repository, gitdir, entry, status, time, filesize, isSymlink);
}

int CGitIndexList::GetFileStatus(CAutoRepository& repository, const CString& gitdir, const CGitIndex& entry, git_wc_status2_t& status, __int64 time, __int64 filesize, bool isSymlink) const
{
	ATLASSERT(!status.assumeValid && !status.skipWorktree);

	// skip-worktree has higher priority than assume-valid
	if (entry.m_FlagsExtended & GIT_INDEX_ENTRY_SKIP_WORKTREE)
	{
		status.status = git_wc_status_normal;
		status.skipWorktree = true;
	}
	else if (entry.m_Flags & GIT_INDEX_ENTRY_VALID)
	{
		status.status = git_wc_status_normal;
		status.assumeValid = true;
	}
	else if (filesize == -1)
		status.status = git_wc_status_deleted;
	else if ((isSymlink && !S_ISLNK(entry.m_Mode)) || ((m_iIndexCaps & GIT_INDEX_CAPABILITY_NO_SYMLINKS) != GIT_INDEX_CAPABILITY_NO_SYMLINKS && isSymlink != S_ISLNK(entry.m_Mode)))
		status.status = git_wc_status_modified;
	else if (!isSymlink && static_cast<uint32_t>(filesize) != entry.m_Size)
		status.status = git_wc_status_modified;
	else if (static_cast<int32_t>(CGit::filetime_to_time_t(time)) == entry.m_ModifyTime && entry.m_ModifyTimeNanos == (time % 10000000) * 100)
		status.status = git_wc_status_normal;
	else if (config && filesize < m_iMaxCheckSize)
	{
		/*
		 * Opening a new repository each time is not yet optimal, however, there is no API to clear the pack-cache
		 * When a shared repository is used, we might need a mutex to prevent concurrent access to repository instance and especially filter-lists
		 */
		if (!repository)
		{
			CString repodir = gitdir;
			if (gitdir.GetLength() == 2 && gitdir[1] == L':')
				repodir += L'\\'; // libgit2 requires a drive root to end with a (back)slash

			if (repository.Open(repodir))
			{
				CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": Could not open git repository in %s for checking file: %s\n", static_cast<LPCWSTR>(gitdir), static_cast<LPCWSTR>(CGit::GetLibGit2LastErr()));
				return -1;
			}
			git_repository_set_config(repository, config);
		}

		git_oid actual;
		CStringA fileA = CUnicodeUtils::GetUTF8<CStringA, CString>(entry.m_FileName);
		if (isSymlink && S_ISLNK(entry.m_Mode))
		{
			std::string linkDestination;
			if (!PathUtils::ReadLink(CombinePath(gitdir, entry.m_FileName), &linkDestination) && !tgit_odb_hash(&actual, linkDestination.c_str(), gsl::narrow<int>(linkDestination.size()), GIT_OBJECT_BLOB) && !git_oid_cmp(&actual, entry.m_IndexHash))
			{
				entry.m_ModifyTime = static_cast<int32_t>(CGit::filetime_to_time_t(time));
				entry.m_ModifyTimeNanos = (time % 10000000) * 100;
				status.status = git_wc_status_normal;
			}
			else
				status.status = git_wc_status_modified;
		}
		else if (!git_repository_hashfile(&actual, repository, fileA, GIT_OBJECT_BLOB, nullptr) && !git_oid_cmp(&actual, entry.m_IndexHash))
		{
			entry.m_ModifyTime = static_cast<int32_t>(CGit::filetime_to_time_t(time));
			entry.m_ModifyTimeNanos = (time % 10000000) * 100;
			status.status = git_wc_status_normal;
		}
		else
			status.status = git_wc_status_modified;
	}
	else
		status.status = git_wc_status_modified;

	if (entry.m_Flags & GIT_INDEX_ENTRY_STAGEMASK)
		status.status = git_wc_status_conflicted;
	else if (entry.m_FlagsExtended & GIT_INDEX_ENTRY_INTENT_TO_ADD)
		status.status = git_wc_status_added;

	return 0;
}

int CGitIndexList::GetFileStatus(const CString& gitdir, const CString& path, git_wc_status2_t& status, CGitHash* pHash) const
{
	ATLASSERT(!status.assumeValid && !status.skipWorktree);

	__int64 time, filesize = 0;
	bool isDir = false;
	bool isSymlink = false;

	int result;
	if (path.IsEmpty())
		result = CGit::GetFileModifyTime(gitdir, &time, &isDir);
	else
		result = CGit::GetFileModifyTime(CombinePath(gitdir, path), &time, &isDir, &filesize, &isSymlink);

	if (result)
		filesize = -1;

	if (!isDir || (isSymlink && (m_iIndexCaps & GIT_INDEX_CAPABILITY_NO_SYMLINKS) != GIT_INDEX_CAPABILITY_NO_SYMLINKS))
		return GetFileStatus(gitdir, path, status, time, filesize, isSymlink, pHash);

	if (tgit::wstr::EndsWith(tgit::wstr::StringView{path}, L"/"))
	{
		size_t index = SearchInSortVector(*this, path, -1, IsIgnoreCase());
		if (index == NPOS)
		{
			status.status = git_wc_status_unversioned;
			if (pHash)
				pHash->Empty();

			return 0;
		}

		if (pHash)
			*pHash = (*this)[index].m_IndexHash;

		if (!result)
			status.status = git_wc_status_normal;
		else
			status.status = git_wc_status_deleted;
		return 0;
	}

	// we get here for symlinks which are handled as files inside the git index
	if ((m_iIndexCaps & GIT_INDEX_CAPABILITY_NO_SYMLINKS) != GIT_INDEX_CAPABILITY_NO_SYMLINKS)
		return GetFileStatus(gitdir, path, status, time, filesize, isSymlink, pHash);

	// we should never get here
	status.status = git_wc_status_unversioned;

	return -1;
}

// This method is assumed to be called with m_SharedMutex locked.
int CGitHeadFileList::GetPackRef(const CString &gitdir)
{
	CString PackRef = g_AdminDirMap.GetAdminDirConcat(gitdir, L"packed-refs");

	__int64 mtime = 0, packsize = -1;
	if (CGit::GetFileModifyTime(PackRef, &mtime, nullptr, &packsize))
	{
		//packed refs is not existed
		this->m_PackRefFile.Empty();
		this->m_PackRefMap.clear();
		return 0;
	}
	else if (mtime == m_LastModifyTimePackRef && packsize == m_LastFileSizePackRef)
		return 0;
	else
	{
		this->m_PackRefFile = PackRef;
		this->m_LastModifyTimePackRef = mtime;
		this->m_LastFileSizePackRef = packsize;
	}

	m_PackRefMap.clear();

	CAutoFile hfile = CreateFile(PackRef,
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	if (!hfile)
		return -1;

	LARGE_INTEGER fileSize;
	if (!::GetFileSizeEx(hfile, &fileSize) || fileSize.QuadPart >= INT_MAX)
		return -1;

	DWORD size = 0;
	auto buff = std::unique_ptr<char[]>(new (std::nothrow) char[fileSize.LowPart]); // prevent default initialization and throwing on allocation error
	if (!buff)
		return -1;

	if (!ReadFile(hfile, buff.get(), fileSize.LowPart, &size, nullptr))
		return -1;

	if (size != fileSize.LowPart)
		return -1;

	CStringA hash;
	CStringA ref;
	for (DWORD i = 0; i < fileSize.LowPart;)
	{
		if (buff[i] == '#' || buff[i] == '^')
		{
			while (buff[i] != '\n')
			{
				++i;
				if (i == fileSize.LowPart)
					break;
			}
			++i;
		}

		if (i >= fileSize.LowPart)
			break;

		while (buff[i] != ' ')
		{
			hash.AppendChar(buff[i]);
			++i;
			if (i == fileSize.LowPart)
				break;
		}

		++i;
		if (i >= fileSize.LowPart)
			break;

		while (buff[i] != '\n')
		{
			ref.AppendChar(buff[i]);
			++i;
			if (i == fileSize.LowPart)
				break;
		}

		if (!ref.IsEmpty())
		{
			CGitHash refHash = CGitHash::FromHexStr(hash);
			if (!refHash.IsEmpty())
				m_PackRefMap[CUnicodeUtils::GetUnicode<CString>(ref)] = refHash;
		}
		hash.Empty();
		ref.Empty();

		while (buff[i] == '\n')
		{
			++i;
			if (i == fileSize.LowPart)
				break;
		}
	}
	return 0;
}
int CGitHeadFileList::ReadHeadHash(const CString& gitdir)
{
	ATLASSERT(m_Gitdir.IsEmpty() && m_HeadFile.IsEmpty() && m_Head.IsEmpty());

	m_Gitdir = g_AdminDirMap.GetWorktreeAdminDir(gitdir);

	m_HeadFile = m_Gitdir;
	m_HeadFile += L"HEAD";

	if (CGit::GetFileModifyTime(m_HeadFile, &m_LastModifyTimeHead, nullptr, &m_LastFileSizeHead))
		return -1;

	CAutoFile hfile = CreateFile(m_HeadFile,
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	if (!hfile)
		return -1;

	DWORD size = 0;
	unsigned char buffer[GIT_HASH_MAX_HEXSIZE];
	// hex length of an object id in the active format; DWORD so it matches what ReadFile reports
	const DWORD hexSize = static_cast<DWORD>(2 * HashSize());
	ReadFile(hfile, buffer, static_cast<DWORD>(strlen("ref:")), &size, nullptr);
	if (size != strlen("ref:"))
		return -1;
	buffer[strlen("ref:")] = '\0';
	if (strcmp(reinterpret_cast<const char*>(buffer), "ref:") == 0)
	{
		m_HeadRefFile.Empty();
		LARGE_INTEGER fileSize;
		if (!::GetFileSizeEx(hfile, &fileSize) || fileSize.QuadPart < static_cast<int>(strlen("ref:") + 1) || fileSize.QuadPart >= 100 * 1024 * 1024)
			return -1;

		{
			auto p = std::unique_ptr<char[]>(new (std::nothrow) char[fileSize.LowPart - strlen("ref:")]); // prevent default initialization and throwing on allocation error
			if (!p)
				return -1;

			if (!ReadFile(hfile, p.get(), fileSize.LowPart - static_cast<DWORD>(strlen("ref:")), &size, nullptr))
				return -1;
			m_HeadRefFile = CUnicodeUtils::GetUnicode<CString>(std::string_view(p.get(), fileSize.LowPart - strlen("ref:")));
		}
		CString ref = m_HeadRefFile.Trim();
		int start = 0;
		ref = ref.Tokenize(L"\n", start);
		m_HeadRefFile = g_AdminDirMap.GetAdminDir(gitdir) + m_HeadRefFile;
		m_HeadRefFile.Replace(L'/', L'\\');

		__int64 time;
		if (CGit::GetFileModifyTime(m_HeadRefFile, &time, nullptr))
		{
			if (GetPackRef(gitdir))
				return -1;
			if (m_PackRefMap.find(ref) != m_PackRefMap.end())
			{
				m_bRefFromPackRefFile = true;
				m_Head = m_PackRefMap[ref];
				return 0;
			}

			// unborn branch
			m_Head.Empty();

			return 0;
		}

		CAutoFile href = CreateFile(m_HeadRefFile,
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);

		if (!href)
		{
			if (GetPackRef(gitdir))
				return -1;

			if (m_PackRefMap.find(ref) == m_PackRefMap.end())
				return -1;

			m_bRefFromPackRefFile = true;
			m_Head = m_PackRefMap[ref];
			return 0;
		}

		ReadFile(href, buffer, hexSize, &size, nullptr);
		if (size != hexSize)
			return -1;

		m_Head = CGitHash::FromHexStr(std::string_view(reinterpret_cast<const char*>(buffer), size));

		m_LastModifyTimeRef = time;

		return 0;
	}

	ReadFile(hfile, buffer + static_cast<DWORD>(strlen("ref:")), hexSize - static_cast<DWORD>(strlen("ref:")), &size, nullptr);
	if (size != hexSize - static_cast<DWORD>(strlen("ref:")))
		return -1;

	m_HeadRefFile.Empty();

	m_Head = CGitHash::FromHexStr(std::string_view(reinterpret_cast<const char*>(buffer), hexSize));

	return 0;
}

bool CGitHeadFileList::CheckHeadUpdate() const
{
	if (this->m_HeadFile.IsEmpty())
		return true;

	__int64 mtime = 0, size = -1;

	if (CGit::GetFileModifyTime(m_HeadFile, &mtime, nullptr, &size))
		return true;

	if (mtime != m_LastModifyTimeHead || size != m_LastFileSizeHead)
		return true;

	if (!this->m_HeadRefFile.IsEmpty())
	{
		// we need to check for the HEAD ref file here, because the original ref might have come from packedrefs and now is a ref-file
		if (CGit::GetFileModifyTime(m_HeadRefFile, &mtime))
		{
			if (!m_bRefFromPackRefFile)
				return true;

		} else if (mtime != this->m_LastModifyTimeRef)
			return true;
	}

	if (m_bRefFromPackRefFile && !m_PackRefFile.IsEmpty())
	{
		size = -1;
		if (CGit::GetFileModifyTime(m_PackRefFile, &mtime, nullptr, &size))
			return true;

		if (mtime != m_LastModifyTimePackRef || size != m_LastFileSizePackRef)
			return true;
	}

	// in an empty repo HEAD points to refs/heads/master, but this ref doesn't exist.
	// So we need to retry again and again until the ref exists - otherwise we will never notice
	if (this->m_Head.IsEmpty() && this->m_HeadRefFile.IsEmpty() && this->m_PackRefFile.IsEmpty())
		return true;

	return false;
}

int CGitHeadFileList::ReadTreeRecursive(git_repository& repo, const git_tree* tree, const CString& base)
{
#define S_IFGITLINK	0160000
	size_t count = git_tree_entrycount(tree);
	for (size_t i = 0; i < count; ++i)
	{
		const git_tree_entry *entry = git_tree_entry_byindex(tree, i);
		if (!entry)
			continue;
		const int mode = git_tree_entry_filemode(entry);
		const bool isDir = (mode & S_IFDIR) == S_IFDIR;
		const bool isSubmodule = (mode & S_IFMT) == S_IFGITLINK;
		if (!isDir || isSubmodule)
		{
			CGitTreeItem item;
			item.m_Hash = git_tree_entry_id(entry);
			item.m_FileName = base;
			CGit::StringAppend(item.m_FileName, git_tree_entry_name(entry), CP_UTF8);
			if (isSubmodule)
				item.m_FileName += L'/';
			push_back(item);
			continue;
		}

		CAutoObject object;
		git_tree_entry_to_object(object.GetPointer(), &repo, entry);
		if (!object)
			continue;
		CString parent = base;
		CGit::StringAppend(parent, git_tree_entry_name(entry));
		parent += L'/';
		ReadTreeRecursive(repo, reinterpret_cast<git_tree*>(static_cast<git_object*>(object)), parent);
	}

	return 0;
}

// ReadTree is/must only be executed on an empty list
int CGitHeadFileList::ReadTree(bool ignoreCase)
{
	ATLASSERT(empty());

	// unborn branch
	if (m_Head.IsEmpty())
		return 0;

	CAutoRepository repository(m_Gitdir);
	CAutoCommit commit;
	CAutoTree tree;
	bool ret = repository;
	ret = ret && !git_commit_lookup(commit.GetPointer(), repository, m_Head);
	ret = ret && !git_commit_tree(tree.GetPointer(), commit);
	try
	{
		ret = ret && !ReadTreeRecursive(*repository, tree, L"");
	}
	catch (const std::bad_alloc& ex)
	{
		CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Catched exception inside ReadTreeRecursive: %s\n", ex.what());
		return -1;
	}
	catch (const std::length_error& ex)
	{
		CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Catched exception inside ReadTreeRecursive, length_error: %s\n", ex.what());
		return -1;
	}
	if (!ret)
	{
		clear();
		CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": Could not open git repository in %s and read HEAD commit %s: %s\n", m_Gitdir, m_Head.ToString().c_str(), CGit::GetLibGit2LastErr());
		m_LastModifyTimeHead = 0;
		m_LastFileSizeHead = -1;
		return -1;
	}

	DoSortFilenametSortVector(*this, ignoreCase);

	CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": Reloaded HEAD tree (commit is %s) for repo: %s\n", m_Head.ToString().c_str(), m_Gitdir);

	return 0;
}
int CGitIgnoreItem::FetchIgnoreList(const CString& projectroot, const CString& file, bool isGlobal, int* ignoreCase)
{
	if (this->m_pExcludeList)
	{
		git_free_exclude_list(m_pExcludeList);
		m_pExcludeList = nullptr;
	}
	m_buffer = nullptr;

	this->m_BaseDir.Empty();
	if (!isGlobal)
	{
		CString base = file.Mid(projectroot.GetLength() + 1);
		base.Replace(L'\\', L'/');

		int start = base.ReverseFind(L'/');
		if(start > 0)
		{
			base.Truncate(start);
			this->m_BaseDir = CUnicodeUtils::GetUTF8<CStringA>(base) + "/";
		}
	}

	if (CGit::GetFileModifyTime(file, &m_LastModifyTime, nullptr, &m_LastFileSize))
		return -1;

	CAutoFile hfile = CreateFile(file,
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);

	if (!hfile)
		return -1 ;

	LARGE_INTEGER fileSize;
	if (!::GetFileSizeEx(hfile, &fileSize) || fileSize.QuadPart > 100 * 1024 * 1024)
		return -1;

	m_buffer = std::unique_ptr<char[]>(new (std::nothrow) char[fileSize.LowPart + 1]); // prevent default initialization and throwing on allocation error
	if (!m_buffer)
		return -1;

	DWORD size = 0;
	if (!ReadFile(hfile, m_buffer.get(), fileSize.LowPart, &size, nullptr))
	{
		m_buffer = nullptr;
		return -1;
	}
	m_buffer[size] = '\0';

	if (git_create_exclude_list(&m_pExcludeList))
	{
		m_buffer = nullptr;
		return -1;
	}

	m_iIgnoreCase = ignoreCase;

	const char *p = m_buffer.get();
	int line = 0;
	for (DWORD i = 0; i < size; ++i)
	{
		if (m_buffer[i] == '\n' || m_buffer[i] == '\r' || i == (size - 1))
		{
			if (m_buffer[i] == '\n' || m_buffer[i] == '\r')
				m_buffer[i] = '\0';

			if (p[0] != '#' && p[0])
				git_add_exclude(p, m_BaseDir, m_BaseDir.GetLength(), m_pExcludeList, ++line);

			p = m_buffer.get() + i + 1;
		}
	}

	if (!line)
	{
		git_free_exclude_list(m_pExcludeList);
		m_pExcludeList = nullptr;
		m_buffer = nullptr;
	}

	return 0;
}

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
int CGitIgnoreItem::IsPathIgnored(const CStringA& patha, int& type)
{
	int pos = patha.ReverseFind('/');
	const char* base = (pos >= 0) ? (static_cast<const char*>(patha) + pos + 1) : static_cast<const char*>(patha);

	return IsPathIgnored(patha, base, type);
}
#endif

int CGitIgnoreItem::IsPathIgnored(const CStringA& patha, const char* base, int& type)
{
	if (!m_pExcludeList)
		return -1; // error or undecided

	return git_check_excluded_1(patha, patha.GetLength(), base, &type, m_pExcludeList, m_iIgnoreCase ? *m_iIgnoreCase : 1);
}

bool CGitIgnoreList::CheckFileChanged(const CString &path)
{
	__int64 time = 0, size = -1;

	const int ret = CGit::GetFileModifyTime(path, &time, nullptr, &size);

	bool cacheExist;
	{
		CAutoReadLock lock(m_SharedMutex);
		cacheExist = (m_Map.find(path) != m_Map.end());
	}

	if (!cacheExist && ret == 0)
	{
		CAutoWriteLock lock(m_SharedMutex);
		m_Map[path].m_LastModifyTime = 0;
		m_Map[path].m_LastFileSize = -1;
	}
	// both cache and file is not exist
	if ((ret != 0) && (!cacheExist))
		return false;

	// file exist but cache miss
	if ((ret == 0) && (!cacheExist))
		return true;

	// file not exist but cache exist
	if ((ret != 0) && (cacheExist))
		return true;
	// file exist and cache exist

	{
		CAutoReadLock lock(m_SharedMutex);
		if (m_Map[path].m_LastModifyTime == time && m_Map[path].m_LastFileSize == size)
			return false;
	}
	return true;
}

int CGitIgnoreList::FetchIgnoreFile(const CString &gitdir, const CString &gitignore, bool isGlobal)
{
	if (CGit::GitPathFileExists(gitignore)) // if .gitignore is removed, we need also to remove it from our cache
	{
		CAutoWriteLock lock(m_SharedMutex);
		m_Map[gitignore].FetchIgnoreList(gitdir, gitignore, isGlobal, &m_IgnoreCase[g_AdminDirMap.GetAdminDir(gitdir)]);
	}
	else
	{
		CAutoWriteLock lock(m_SharedMutex);
		m_Map.erase(gitignore);
	}
	return 0;
}

bool CGitIgnoreList::CheckAndUpdateIgnoreFiles(const CString& gitdir, const CString& path, bool isDir, std::set<CString>* lastChecked)
{
	CString temp(gitdir);
	temp += L'\\';
	temp += path;

	temp.Replace(L'/', L'\\');

	if (!isDir)
	{
		const int x = temp.ReverseFind(L'\\');
		if (x >= 2)
			temp.Truncate(x);
	}

	bool updated = false;
	while (!temp.IsEmpty())
	{
		if (lastChecked)
		{
			if (lastChecked->find(temp) != lastChecked->end())
				return updated;
			lastChecked->insert(temp);
		}

		temp += L"\\.gitignore";

		if (CheckFileChanged(temp))
		{
			FetchIgnoreFile(gitdir, temp, false);
			updated = true;
		}

		temp.Truncate(temp.GetLength() - static_cast<int>(wcslen(L"\\.gitignore")));
		if (PathUtils::ArePathStringsEqual(tgit::wstr::StringView(temp), tgit::wstr::StringView(gitdir)))
		{
			CString adminDir = g_AdminDirMap.GetAdminDir(temp);
			CString wcglobalgitignore = adminDir + L"info\\exclude";
			if (CheckFileChanged(wcglobalgitignore))
			{
				FetchIgnoreFile(gitdir, wcglobalgitignore, true);
				updated = true;
			}

			if (CheckAndUpdateCoreExcludefile(adminDir))
			{
				CString excludesFile;
				{
					CAutoReadLock lock(m_SharedMutex);
					excludesFile = m_CoreExcludesfiles[adminDir];
				}
				if (!excludesFile.IsEmpty())
				{
					FetchIgnoreFile(gitdir, excludesFile, true);
					updated = true;
				}
			}

			return updated;
		}

		const int i = temp.ReverseFind(L'\\');
		temp.Truncate(std::max(0, i));
	}
	return updated;
}

bool CGitIgnoreList::CheckAndUpdateGitSystemConfigPath(bool force)
{
	// recheck every 30 seconds
	if (GetTickCount64() - m_dGitSystemConfigPathLastChecked > 30000UL || force)
	{
		m_dGitSystemConfigPathLastChecked = GetTickCount64();
		CString gitSystemConfigPath{CRegString<CString>(REG_SYSTEM_GITCONFIGPATH, L"", FALSE)};
		if (gitSystemConfigPath != m_sGitSystemConfigPath)
		{
			m_sGitSystemConfigPath = gitSystemConfigPath;
			return true;
		}
	}
	return false;
}
bool CGitIgnoreList::CheckAndUpdateCoreExcludefile(const CString &adminDir)
{
	CString projectConfig(adminDir);
	projectConfig += L"config";
	CString globalConfig = g_Git.GetGitGlobalConfig();
	CString globalXDGConfig = g_Git.GetGitGlobalXDGConfig();

	CAutoWriteLock lock(m_coreExcludefilesSharedMutex);
	bool hasChanged = CheckAndUpdateGitSystemConfigPath();
	hasChanged = hasChanged || CheckFileChanged(projectConfig);
	hasChanged = hasChanged || CheckFileChanged(globalConfig);
	hasChanged = hasChanged || CheckFileChanged(globalXDGConfig);
	if (!m_sGitSystemConfigPath.IsEmpty())
		hasChanged = hasChanged || CheckFileChanged(m_sGitSystemConfigPath);

	CString excludesFile;
	{
		CAutoReadLock lock2(m_SharedMutex);
		excludesFile = m_CoreExcludesfiles[adminDir];
	}
	if (!excludesFile.IsEmpty())
		hasChanged = hasChanged || CheckFileChanged(excludesFile);

	if (!hasChanged)
		return false;

	CAutoConfig config(true);
	CAutoRepository repo(adminDir);
	git_config_add_file_ondisk(config, CGit::GetGitPathStringA(projectConfig), GIT_CONFIG_LEVEL_LOCAL, repo, FALSE);
	git_config_add_file_ondisk(config, CGit::GetGitPathStringA(globalConfig), GIT_CONFIG_LEVEL_GLOBAL, repo, FALSE);
	git_config_add_file_ondisk(config, CGit::GetGitPathStringA(globalXDGConfig), GIT_CONFIG_LEVEL_XDG, repo, FALSE);
	if (!m_sGitSystemConfigPath.IsEmpty())
		git_config_add_file_ondisk(config, CGit::GetGitPathStringA(m_sGitSystemConfigPath), GIT_CONFIG_LEVEL_SYSTEM, repo, FALSE);

	config.GetString(L"core.excludesfile", excludesFile);
	if (excludesFile.IsEmpty())
		excludesFile = GetWindowsHome() + L"\\.config\\git\\ignore";
	else if (CStringUtils::StartsWith(excludesFile, L"~/"))
		excludesFile = GetWindowsHome() + excludesFile.Mid(static_cast<int>(wcslen(L"~")));

	CAutoWriteLock lockMap(m_SharedMutex);
	m_IgnoreCase[adminDir] = 1;
	config.GetBOOL(L"core.ignorecase", m_IgnoreCase[adminDir]);
	CGit::GetFileModifyTime(projectConfig, &m_Map[projectConfig].m_LastModifyTime, nullptr, &m_Map[projectConfig].m_LastFileSize);
	CGit::GetFileModifyTime(globalXDGConfig, &m_Map[globalXDGConfig].m_LastModifyTime, nullptr, &m_Map[globalXDGConfig].m_LastFileSize);
	if (m_Map[globalXDGConfig].m_LastModifyTime == 0)
		m_Map.erase(globalXDGConfig);
	CGit::GetFileModifyTime(globalConfig, &m_Map[globalConfig].m_LastModifyTime, nullptr, &m_Map[globalConfig].m_LastFileSize);
	if (m_Map[globalConfig].m_LastModifyTime == 0)
		m_Map.erase(globalConfig);
	if (!m_sGitSystemConfigPath.IsEmpty())
		CGit::GetFileModifyTime(m_sGitSystemConfigPath, &m_Map[m_sGitSystemConfigPath].m_LastModifyTime, nullptr, &m_Map[m_sGitSystemConfigPath].m_LastFileSize);
	if (m_Map[m_sGitSystemConfigPath].m_LastModifyTime == 0 || m_sGitSystemConfigPath.IsEmpty())
		m_Map.erase(m_sGitSystemConfigPath);
	m_CoreExcludesfiles[adminDir] = excludesFile;

	return true;
}
const CString CGitIgnoreList::GetWindowsHome()
{
	static CString sWindowsHome(g_Git.GetHomeDirectory());
	return sWindowsHome;
}
bool CGitIgnoreList::IsIgnore(CString str, const CString& projectroot, bool isDir, const CString& adminDir)
{
	str.Replace(L'\\', L'/');

	if (!str.IsEmpty() && str[str.GetLength() - 1] == L'/')
		str.Truncate(str.GetLength() - 1);

	int ret = CheckIgnore(str, projectroot, isDir, adminDir);
	while (ret < 0)
	{
		int start = str.ReverseFind(L'/');
		if(start < 0)
			return (ret == 1);

		str.Truncate(start);
		ret = CheckIgnore(str, projectroot, TRUE, adminDir);
	}

	return (ret == 1);
}
int CGitIgnoreList::CheckFileAgainstIgnoreList(const CString &ignorefile, const CStringA &patha, const char * base, int &type)
{
	if (m_Map.find(ignorefile) == m_Map.end())
		return -1; // error or undecided

	return (m_Map[ignorefile].IsPathIgnored(patha, base, type));
}
int CGitIgnoreList::CheckIgnore(const CString &path, const CString &projectroot, bool isDir, const CString& adminDir)
{
	CString temp = CombinePath(projectroot, path);
	temp.Replace(L'/', L'\\');

	CStringA patha = CUnicodeUtils::GetUTF8<CStringA>(path);
	patha.Replace('\\', '/');

	int type = 0;
	if (isDir)
	{
		type = DT_DIR;

		// strip directory name
		// we do not need to check for a .ignore file inside a directory we might ignore
		const int i = temp.ReverseFind(L'\\');
		if (i >= 0)
			temp.Truncate(i);
	}
	else
	{
		type = DT_REG;

		int x = temp.ReverseFind(L'\\');
		if (x >= 2)
			temp.Truncate(x);
	}

	int pos = patha.ReverseFind('/');
	const char* base = (pos >= 0) ? (static_cast<const char*>(patha) + pos + 1) : static_cast<const char*>(patha);


	CAutoReadLock lock(m_SharedMutex);
	while (!temp.IsEmpty())
	{
		temp += L"\\.gitignore";

		if (auto ret = CheckFileAgainstIgnoreList(temp, patha, base, type); ret != -1)
			return ret;

		temp.Truncate(temp.GetLength() - static_cast<int>(wcslen(L"\\.gitignore")));

		if (PathUtils::ArePathStringsEqual(tgit::wstr::StringView(temp), tgit::wstr::StringView(projectroot)))
		{
			CString wcglobalgitignore = adminDir;
			wcglobalgitignore += L"info\\exclude";
			if (auto ret = CheckFileAgainstIgnoreList(wcglobalgitignore, patha, base, type); ret != -1)
				return ret;

			CString excludesFile = m_CoreExcludesfiles[adminDir];
			if (!excludesFile.IsEmpty())
				return CheckFileAgainstIgnoreList(excludesFile, patha, base, type);

			return -1;
		}

		const int i = temp.ReverseFind(L'\\');
		temp.Truncate(std::max(0, i));
	}

	return -1;
}

SHARED_TREE_PTR CGitHeadFileMap::CheckHeadAndUpdate(const CString& gitdir, bool ignoreCase)
{
	if (auto ptr = this->SafeGet(gitdir); ptr.get() && !ptr->CheckHeadUpdate())
		return ptr;

	auto newPtr = std::make_shared<CGitHeadFileList>();
	if (newPtr->ReadHeadHash(gitdir) || newPtr->ReadTree(ignoreCase))
	{
		SafeClear(gitdir);
		return {};
	}

	this->SafeSet(gitdir, newPtr);

	return newPtr;
}

}

export using namespace GitIndex;