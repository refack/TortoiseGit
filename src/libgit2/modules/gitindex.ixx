module;
#include <sys/stat.h>
#include <gitdll.h>
#include <git2.h>
#include <git2/sys/repository.h> // git_repository_set_config

#include <atlbase.h>	// CComCritSecLock/CComAutoCriticalSection, not CString: that is str_utils'
// detail::ReadSmallFile instantiates wil::GetFileInfo<>, which reaches wil's error-reporting
// helpers and through them strsafe's *static* StringCch* workers. A template instantiated
// here cannot use module wil's internal-linkage definitions, so this TU needs its own: C2129.
// (GitStatus.ixx carries the same include for the same reason.)
#include <new>
#include <strsafe.h>

#include "sanity.h"

export module GitIndex;
import std;
import gsl;
import wil;
namespace fs = std::filesystem;

import ReaderWriterLock;
import DebugOutput;
import GitHash;
import PathUtils;
import RIAA;
import Registry;
import wil_git2;
import str_utils;
import TGitPath;
import git_h;
import invarients;

using namespace Registry;
using namespace PathUtils;
using namespace SmartLibgit2;
using namespace tgit::DebugOutput;
using namespace tgit;
using tgit::str::StringView;
using CString = tgit::wstr::CString;
using CStringA = tgit::zstr::CString;


// Exported: `export using namespace GitIndex;` at the end exports the using-
// directive, not the entities, so importers (GitStatus.cpp) could not name any
// of these. The out-of-line definitions below reopen the namespace unexported.
export namespace GitIndex {
using CAutoLocker = CComCritSecLock<CComCriticalSection>;


// From tgittypes (moved verbatim). GitIndex is the lowest module that uses them,
// and TGitStatus imports GitIndex. The enumerator order and the UINT8 width are
// the wire format TGitCache.CacheInterface's static_assert checks: append only.
enum git_wc_status_kind {
	git_wc_status_none,
	git_wc_status_unversioned,
	git_wc_status_ignored,
	git_wc_status_normal,
	git_wc_status_deleted,
	git_wc_status_modified,
	git_wc_status_added,
	git_wc_status_conflicted,
	git_wc_status_unknown, // should be last, see TGitCache/CacheInterface.h
};



struct git_wc_status2_t {
	git_wc_status_kind status = git_wc_status_none;

	bool assumeValid = false;
	bool skipWorktree = false;
};


struct CGitIndex
{
	/* m_Size and m_ModifyTime are only uint32_t in libgit2, cf. https://github.com/libgit2/libgit2/blob/8535fdb9cbad8fcd15ee4022ed29c4138547e22d/include/git2/index.h#L48-L51 and https://tortoisegit.org/issue/4108 */
	std::wstring	m_FileName;
	mutable int32_t	m_ModifyTime{};
	mutable uint32_t	m_ModifyTimeNanos{};
	uint16_t	m_Flags{};
	uint16_t	m_FlagsExtended{};
	CGitHash	m_IndexHash;
	uint32_t	m_Size{};
	uint32_t	m_Mode{};

	// git splits mtime into seconds since 1970 plus nanoseconds; ours is FILETIME ticks
	[[nodiscard]] bool HasModifyTime(const std::int64_t filetime) const noexcept
	{
		return m_ModifyTime == static_cast<int32_t>(TGitH::filetime_to_time_t(filetime)) && m_ModifyTimeNanos == (filetime % 10000000) * 100;
	}
	// const: the members are mutable, refreshing the cached stat is not a change to the entry
	void SetModifyTime(const std::int64_t filetime) const noexcept
	{
		m_ModifyTime = static_cast<int32_t>(TGitH::filetime_to_time_t(filetime));
		m_ModifyTimeNanos = static_cast<uint32_t>((filetime % 10000000) * 100);
	}

	/// One line to stdout's wide stream, as the wprintf it replaces wrote it.
	/// The time goes through uint32_t because %08X printed an int's bits, where
	/// {:08X} would print a negative int with a minus sign.
	void Print() const
	{
		std::fputws(std::format(
			L"0x{:08X}  0x{:08X} {} {}\n",
			static_cast<std::uint32_t>(m_ModifyTime),
			m_Flags,
			m_IndexHash.ToString(),
			m_FileName
		).c_str(), stdout);
	}
};


struct CGitTreeItem
{
	std::wstring	m_FileName;
	CGitHash	m_Hash;
	int			m_Flags{};
};


namespace detail
{
/// The key every path-keyed cache below files a path under, so that two
/// spellings of one directory share an entry.
inline std::wstring NormalizedKey(const std::wstring& path)
{
	return NormalizePath(path);
}

/// CString::Trim(): leading and trailing whitespace as iswspace sees it, which
/// is wider than tgit::wstr::Trim's default " \t\r\n" (\v, \f, Unicode spaces).
inline void TrimSpace(std::wstring& s)
{
	constexpr auto notSpace = [](const wchar_t c) { return !tgit::str::is_space(c); };
	s.erase(std::ranges::find_if(s | std::views::reverse, notSpace).base(), s.end());
	s.erase(s.begin(), std::ranges::find_if(s, notSpace));
}

// git's file modes: the POSIX st_mode encoding git stores, whatever the host's.
// (The CRT has no S_IFLNK; these replace the S_IFLNK / S_ISLNK / S_IFGITLINK
// macros this file used to define for itself.)
inline constexpr std::uint32_t ModeTypeMask = 0170000;
inline constexpr std::uint32_t ModeSymlink = 0120000;
inline constexpr std::uint32_t ModeGitlink = 0160000;
static_assert(ModeTypeMask == _S_IFMT, "git's type mask is the CRT's");

constexpr bool IsSymlinkMode(const std::uint32_t mode) noexcept { return (mode & ModeTypeMask) == ModeSymlink; }
constexpr bool IsGitlinkMode(const std::uint32_t mode) noexcept { return (mode & ModeTypeMask) == ModeGitlink; }

/// What CGitIgnoreList appends to a directory to name the ignore file in it.
inline constexpr std::wstring_view GitIgnoreSuffix = L"\\.gitignore";

/// The read-only random-access range a sorted entry list shows its readers:
/// GitStatus walks it by index and iterator, and hands it to the sorted-name
/// helpers below. Only the owning list fills, sorts and clears m_entries.
template <class TEntry>
class EntryList
{
public:
	[[nodiscard]] auto begin() noexcept { return m_entries.begin(); }
	[[nodiscard]] auto begin() const noexcept { return m_entries.begin(); }
	[[nodiscard]] auto end() noexcept { return m_entries.end(); }
	[[nodiscard]] auto end() const noexcept { return m_entries.end(); }
	[[nodiscard]] auto cbegin() const noexcept { return m_entries.cbegin(); }
	[[nodiscard]] auto cend() const noexcept { return m_entries.cend(); }
	[[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
	[[nodiscard]] size_t size() const noexcept { return m_entries.size(); }
	[[nodiscard]] TEntry& operator[](const size_t i) { return m_entries[i]; }
	[[nodiscard]] const TEntry& operator[](const size_t i) const { return m_entries[i]; }

protected:
	std::vector<TEntry> m_entries;
};
} // namespace detail


class CGitIndexList : public detail::EntryList<CGitIndex>
{
public:
	bool		m_bHasConflicts = false;
	std::wstring	m_branch;
	/// commits behind / ahead of the upstream branch; both empty when there is
	/// no upstream (or it was not calculated), both set together otherwise
	std::optional<size_t>	m_incoming;
	std::optional<size_t>	m_outgoing;
	size_t		m_stashCount = 0;
	[[nodiscard]] bool IsIgnoreCase() const { return m_iIndexCaps & GIT_INDEX_CAPABILITY_IGNORE_CASE; }

	CGitIndexList()
	{
		// The registry defaults equal the member defaults, which is what the unit
		// tests (once compiled with TGIT_TESTS_ONLY to skip this) rely on.
		m_iMaxCheckSize = static_cast<std::int64_t>(CRegDWORD(L"Software\\TortoiseGit\\TGitCacheCheckContentMaxSize", 10 * 1024)) * 1024; // stored in KiB
		m_bCalculateIncomingOutgoing = (CRegStdDWORD(L"Software\\TortoiseGit\\ModifyExplorerTitle", TRUE) != FALSE);
	}

	~CGitIndexList() = default;

	[[nodiscard]] bool HasIndexChangedOnDisk(const std::wstring& gitdir) const;
	int ReadIndex(const std::wstring& dgitdir);
	std::expected<void, int> ReadIncomingOutgoing(git_repository* repo);
	int GetFileStatus(const std::wstring& gitdir, const std::wstring& path, git_wc_status2_t& status, CGitHash* pHash = nullptr) const;
	int GetFileStatus(CAutoRepository& repository, const std::wstring& gitdir, const CGitIndex& entry, git_wc_status2_t& status, std::int64_t time, std::int64_t filesize, bool isSymlink) const;

	// public for the unit tests (it was a FRIEND_TEST, which cannot reach into a module)
	int GetFileStatus_NoRepo(const std::wstring& gitdir, const std::wstring& path, git_wc_status2_t& status, std::int64_t time, std::int64_t filesize, bool isSymlink, CGitHash* pHash = nullptr) const;

private:
	std::int64_t m_LastModifyTime = 0;
	std::int64_t m_LastFileSize = -1;

	int		m_iIndexCaps = GIT_INDEX_CAPABILITY_IGNORE_CASE | GIT_INDEX_CAPABILITY_NO_SYMLINKS;
	std::int64_t m_iMaxCheckSize = 10 * 1024 * 1024;
	bool	m_bCalculateIncomingOutgoing = true;
	CAutoConfig config;
};


using SHARED_INDEX_PTR = std::shared_ptr<const CGitIndexList>;



/// A thread-safe cache of shared lists, keyed by normalised path.
///
/// The lock is a plain std::mutex, not a recursive one: no member calls
/// another while holding it, and nothing run under it can reach back in - the
/// map operations, StartsWith, and the destructors of an erased or replaced
/// CGitIndexList / CGitHeadFileList (strings, vectors, maps, a git_config) do
/// not touch any SharedPtrMapTmpl. Callers such as CheckAndUpdate take it once
/// per call, in sequence, never nested.
template<typename SharedPtr>
class SharedPtrMapTmpl
{
public:
	[[nodiscard]] SharedPtr SafeGet(const std::wstring& path) const
	{
		const auto key = detail::NormalizedKey(path);
		std::scoped_lock lock(m_mutex);
		const auto lookup = m_map.find(key);
		if (lookup == m_map.cend())
			return {};
		return lookup->second;
	}

	bool SafeClear(const std::wstring& path)
	{
		const auto key = detail::NormalizedKey(path);
		std::scoped_lock lock(m_mutex);
		return m_map.erase(key) != 0;
	}

	bool SafeClearRecursively(const std::wstring& path)
	{
		const auto key = detail::NormalizedKey(path);
		std::scoped_lock lock(m_mutex);
		return std::erase_if(m_map, [&key](const auto& entry) { return entry.first.starts_with(key); }) != 0;
	}

protected:
	void SafeSet(const std::wstring& path, SharedPtr ptr)
	{
		auto key = detail::NormalizedKey(path);
		std::scoped_lock lock(m_mutex);
		m_map.insert_or_assign(std::move(key), std::move(ptr));
	}

private:
	mutable std::mutex m_mutex;
	std::map<std::wstring, SharedPtr> m_map;
};



class CGitIndexFileMap : protected SharedPtrMapTmpl<SHARED_INDEX_PTR>
{
public:
	[[nodiscard]] SHARED_INDEX_PTR CheckAndUpdate(const std::wstring& gitdir)
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



/* After object create, never change field against
 * that needn't lock to get field
*/
class CGitHeadFileList : public detail::EntryList<CGitTreeItem>
{
private:
	std::expected<void, int> GetPackRef(const std::wstring& gitdir);

	std::int64_t	m_LastModifyTimeHead = 0;
	std::int64_t	m_LastModifyTimeRef = 0;
	std::int64_t	m_LastModifyTimePackRef = 0;

	std::int64_t	m_LastFileSizeHead = -1;
	std::int64_t	m_LastFileSizePackRef = -1;

	std::wstring	m_HeadRefFile;
	CGitHash	m_Head;
	std::wstring	m_HeadFile;
	std::wstring	m_Gitdir;
	std::wstring	m_PackRefFile;
	bool		m_bRefFromPackRefFile = false;

	std::map<std::wstring, CGitHash> m_PackRefMap;

public:
	CGitHeadFileList() = default;

	/// error: -1 (the only failure value they have ever reported)
	std::expected<void, int> ReadTree(bool ignoreCase);
	std::expected<void, int> ReadHeadHash(const std::wstring& gitdir);
	[[nodiscard]] bool CheckHeadUpdate() const;

private:
	void ReadTreeRecursive(git_repository& repo, const git_tree* tree, const std::wstring& base);
};



using SHARED_TREE_PTR = std::shared_ptr<const CGitHeadFileList>;
class CGitHeadFileMap : protected SharedPtrMapTmpl<SHARED_TREE_PTR>
{
public:
	[[nodiscard]] SHARED_TREE_PTR CheckHeadAndUpdate(const std::wstring& gitdir, bool ignoreCase);

	using SharedPtrMapTmpl<SHARED_TREE_PTR>::SafeClear;
	using SharedPtrMapTmpl<SHARED_TREE_PTR>::SafeClearRecursively;
	using SharedPtrMapTmpl<SHARED_TREE_PTR>::SafeGet;
};



namespace detail
{
/// Frees a gitdll exclude list; what makes a unique_ptr own one.
struct ExcludeListDeleter
{
	void operator()(const EXCLUDE_LIST list) const noexcept { git_free_exclude_list(list); }
};
using unique_exclude_list = std::unique_ptr<void, ExcludeListDeleter>;
} // namespace detail


/// Rule of zero, move-only: the exclude list is owned by a unique_ptr, so a
/// copy does not compile, and everything it points at - the patterns and the
/// base directory, which git_add_exclude keeps a pointer to as well - lives in
/// one vector, whose move hands over the same heap block - so a moved item's
/// list still points into its own buffer. (A std::string buffer would not: its
/// move copies a short value out of the small-string storage the list points
/// into. That is why m_BaseDir is only the readable copy, never what the list
/// is handed.)
class CGitIgnoreItem
{
public:
	std::int64_t	m_LastModifyTime = 0;
	std::int64_t	m_LastFileSize = -1;
	/// the directory the patterns are relative to, UTF-8 with a trailing '/',
	/// empty for a global file or one at the root
	std::string m_BaseDir{};
	/// m_BaseDir plus a NUL, then the ignore file's bytes plus a NUL, lines
	/// NUL-terminated in place; m_pExcludeList points into it, so it is
	/// declared first and outlives it
	std::vector<char> m_buffer{};
	detail::unique_exclude_list m_pExcludeList{};
	int* m_iIgnoreCase = nullptr;

	int FetchIgnoreList(const std::wstring& projectroot, const std::wstring& file, bool isGlobal, int* ignoreCase);

	/**
	* patha: the filename to be checked whether it is ignored or not
	* base: must be a pointer to the beginning of the base filename WITHIN patha
	* type: DT_DIR or DT_REG
	*/
	int IsPathIgnored(const std::string& patha, const char* base, int& type) const
	{
		if (!m_pExcludeList)
			return -1; // error or undecided

		return git_check_excluded_1(patha.c_str(), gsl::narrow<int>(patha.size()), base, &type, m_pExcludeList.get(), m_iIgnoreCase ? *m_iIgnoreCase : 1);
	}

	/// As above, with base derived from patha. The unit tests' entry point.
	int IsPathIgnored(const std::string& patha, int& type);
};



class CGitIgnoreList
{
private:
	bool CheckFileChanged(const std::wstring& path);
	void FetchIgnoreFile(const std::wstring& gitdir, const std::wstring& gitignore, bool isGlobal);

	int CheckIgnore(const std::wstring& path, const std::wstring& root, bool isDir, const std::wstring& adminDir);
	int CheckFileAgainstIgnoreList(const std::wstring& ignorefile, const std::string& patha, const char* base, int& type);

	// core.excludesfile stuff
	std::map<std::wstring, std::wstring> m_CoreExcludesfiles{};
	std::map<std::wstring, int> m_IgnoreCase{};
	std::wstring m_sGitSystemConfigPath{};
	ULONGLONG m_dGitSystemConfigPathLastChecked = 0LL;
	CReaderWriterLock	m_coreExcludefilesSharedMutex;
	// checks if the msysgit path has changed and return true/false
	// if the path changed, the cache is update
	// force is only ised in constructor
	bool CheckAndUpdateGitSystemConfigPath(bool force = true);
	bool CheckAndUpdateCoreExcludefile(const std::wstring& adminDir);
	static const std::wstring& GetWindowsHome()
	{
		static const std::wstring sWindowsHome(g_Git.GetHomeDirectory());
		return sWindowsHome;
	}

public:
	CReaderWriterLock		m_SharedMutex;

	CGitIgnoreList(){ CheckAndUpdateGitSystemConfigPath(true); }

	std::map<std::wstring, CGitIgnoreItem> m_Map{};

	bool CheckAndUpdateIgnoreFiles(const std::wstring& gitdir, const std::wstring& path, bool isDir, std::set<std::wstring>* lastChecked = nullptr);
	bool IsIgnore(std::wstring path, const std::wstring& root, bool isDir, const std::wstring& adminDir);
};



// ====== The sorted name lists ======
//
// CGitIndexList, CGitHeadFileList and GitStatus's file lists are all kept
// sorted by m_FileName, and searched by a name or a name prefix. The helpers
// below take the list as any random-access range and the name as anything
// that reads as wide text - CString, std::wstring, fs::path, a literal - and
// compare views, so a list whose m_FileName changes type does not have to
// change them. (They used to compare through wcscmp on a const wchar_t*,
// which is how GitStatus's fs::path names stopped compiling.)

namespace detail
{
/// Any name these lists meet, as the view the comparisons take: O(1) for
/// everything that knows its length, one walk for a pointer or literal.
template <typename TName>
std::wstring_view NameView(const TName& name)
{
	if constexpr (requires { name.native(); })
		return name.native();
	else if constexpr (tgit::str::HasViewTraitsOf<TName, wchar_t>)
		return tgit::str::StringView{ name };
	else
	{
		using traits = tgit::str::StringTraits<tgit::str::TOwnedKey<TName>>;
		return { traits::data(name), traits::size(name) };
	}
}

/// The projection every list is sorted and searched by.
inline constexpr auto FileNameOf = [](const auto& entry) { return NameView(entry.m_FileName); };

/// CString::Compare / CompareNoCase over two names - or, with len >= 0, over
/// their first len characters, which is what makes a search a prefix match.
/// The sort and every search over one list must agree on ignoreCase, or the
/// search walks an order the list is not in.
inline auto NameOrder(const int len, const bool ignoreCase)
{
	return [len, ignoreCase](std::wstring_view a, std::wstring_view b) {
		if (len >= 0)
		{
			a = a.substr(0, static_cast<size_t>(len));
			b = b.substr(0, static_cast<size_t>(len));
		}
		return ignoreCase ? tgit::wstr::CompareNoCase(a, b) : a.compare(b);
	};
}

inline auto NameLess(const int len, const bool ignoreCase)
{
	return [order = NameOrder(len, ignoreCase)](const std::wstring_view a, const std::wstring_view b) { return order(a, b) < 0; };
}


// ====== Reading the small files git keeps beside the index ======
//
// HEAD, a loose ref, packed-refs and the ignore files are all read whole.

/// What every reader here has always shared: a writer replacing the file under
/// us (git writes a lock file and renames it over) must not be refused.
inline constexpr DWORD ShareAll = FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE;

/// Whether fewer bytes arriving than the file's size said - it shrank between
/// the size query and the read - fails the read or keeps what did arrive.
enum class ShortRead { Fail, Keep };

/// path opened for GENERIC_READ with exactly shareMode; test with operator bool.
inline wil::unique_hfile OpenForRead(const wchar_t* const path, const DWORD shareMode)
{
	return wil::unique_hfile{ ::CreateFileW(path, GENERIC_READ, shareMode, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
}

/// The rest of an open file, from its current position: nullopt when its size
/// cannot be read or is larger than maxSize (inclusive), allocation fails,
/// ReadFile fails, or - with ShortRead::Fail - fewer bytes arrive than that size.
inline std::optional<std::string> ReadSmallFile(const HANDLE file, const std::uint64_t maxSize, const ShortRead shortRead)
{
	try
	{
		const auto size = wil::GetFileInfo<FileStandardInfo>(file).EndOfFile.QuadPart; // throws on failure
		if (size < 0 || static_cast<std::uint64_t>(size) > maxSize || static_cast<std::uint64_t>(size) > std::numeric_limits<DWORD>::max())
			return std::nullopt;

		const auto expected = static_cast<DWORD>(size);
		bool readOk = false;
		std::string bytes;
		// allocates without zero-filling and keeps exactly what ReadFile delivered;
		// the op must not throw, so a failed read is reported as 0 and checked below
		bytes.resize_and_overwrite(expected, [&](char* const buffer, const size_t count) noexcept {
			DWORD read = 0;
			readOk = ::ReadFile(file, buffer, static_cast<DWORD>(count), &read, nullptr) != FALSE;
			return readOk ? static_cast<size_t>(read) : size_t{ 0 };
		});
		if (!readOk || (shortRead == ShortRead::Fail && bytes.size() != expected))
			return std::nullopt;
		return bytes;
	}
	catch (const std::runtime_error&) // wil::ResultException from GetFileInfo, std::bad_alloc
	{
		return std::nullopt;
	}
}

/// The whole of a small file, opened as OpenForRead opens it; nullopt when it
/// does not open or on any failure ReadSmallFile(HANDLE, ...) reports.
inline std::optional<std::string> ReadSmallFile(const wchar_t* const path, const DWORD shareMode, const std::uint64_t maxSize, const ShortRead shortRead)
{
	const auto file = OpenForRead(path, shareMode);
	if (!file)
		return std::nullopt;
	return ReadSmallFile(file.get(), maxSize, shortRead);
}
} // namespace detail


template <class T>
void DoSortFilenametSortVector(T& vector, const bool ignoreCase)
{
	std::ranges::sort(vector, detail::NameLess(-1, ignoreCase), detail::FileNameOf);
}



constexpr size_t NPOS = std::numeric_limits<size_t>::max(); // bad/missing length/position
static_assert(-1 == static_cast<int>(NPOS), "NPOS must equal -1");



/// The run of entries matching the first len characters of key, widened from
/// pos - which must itself match - in both directions, as a view into vector.
/// It is empty exactly when there is no run (pos is NPOS or out of range, or
/// the entry at pos does not match); otherwise it holds at least vector[pos].
template <class T, class TKey>
[[nodiscard]] std::ranges::subrange<std::ranges::iterator_t<const T>> GetRangeInSortVector(const T& vector, const TKey& key, const size_t len, const bool ignoreCase, const size_t pos)
{
	const auto first = std::ranges::begin(vector);
	const auto last = std::ranges::end(vector);
	if (pos == NPOS || pos >= std::ranges::size(vector))
		return { last, last };

	const auto needle = detail::NameView(key);
	const auto order = detail::NameOrder(gsl::narrow<int>(len), ignoreCase);
	const auto matches = [&](const auto& entry) { return order(detail::FileNameOf(entry), needle) == 0; };
	const auto at = first + pos;
	if (!matches(*at))
		return { last, last };

	// in a sorted list the matches are one contiguous run around pos
	const auto past = std::ranges::find_if_not(at, last, matches);
	const auto before = std::ranges::find_if_not(std::make_reverse_iterator(at), std::make_reverse_iterator(first), matches);
	return { before.base(), past };
}

/// The run would point into a list that dies at the end of the call.
template <class T, class TKey>
	requires (!std::is_lvalue_reference_v<T>)
void GetRangeInSortVector(T&& vector, const TKey& key, size_t len, bool ignoreCase, size_t pos) = delete;

/// The index form of the run above, kept for GitIndexTest's assertions, which
/// pin its out-parameter contract (nullptr outputs fail, outputs reset to NPOS)
/// as well as the run: [*start, *end] is inclusive; both are NPOS when there is
/// no run. -1 on any failure. New code takes the subrange.
template <class T, class TKey>
int GetRangeInSortVector(const T& vector, const TKey& key, const size_t len, const bool ignoreCase, size_t* start, size_t* end, const size_t pos)
{
	if (pos == NPOS || !start || !end)
		return -1;
	*start = *end = NPOS;

	const auto run = GetRangeInSortVector(vector, key, len, ignoreCase, pos);
	if (run.empty())
		return -1;

	const auto first = std::ranges::begin(vector);
	*start = static_cast<size_t>(run.begin() - first);
	*end = static_cast<size_t>(run.end() - first) - 1;
	return 0;
}


/// The index of an entry matching key - whole names when len < 0, otherwise
/// their first len characters - or NPOS. With several matches it is the first
/// of them; callers that want all of them pass it to GetRangeInSortVector.
template <class T, class TKey>
size_t SearchInSortVector(const T& vector, const TKey& key, const int len, const bool ignoreCase)
{
	const auto needle = detail::NameView(key);
	const auto found = std::ranges::lower_bound(vector, needle, detail::NameLess(len, ignoreCase), detail::FileNameOf);
	if (found == std::ranges::end(vector) || detail::NameOrder(len, ignoreCase)(detail::FileNameOf(*found), needle) != 0)
		return NPOS;
	return static_cast<size_t>(found - std::ranges::begin(vector));
}



/// Working tree -> admin dir lookups, and the reverse.
///
/// The lock is a plain std::mutex, not a recursive one (CComAutoCriticalSection
/// was): no member calls another while holding it - the *Concat members call
/// theirs before locking anything - and nothing run under it can name
/// g_AdminDirMap. GitAdminDir::GetAdminDirPath / GetWorktreeAdminDirPath,
/// NormalizePath, BuildPathWithPathDelimiter and PathIsDirectory live in
/// modules GitIndex imports, which therefore cannot import GitIndex back, and
/// none takes a callback.
class CGitAdminDirMap
{
public:
	std::wstring GetAdminDir(const std::wstring& path)
	{
		const auto thePath = detail::NormalizedKey(path);
		std::scoped_lock lock(m_mutex);
		const auto lookup = m_adminDirs.find(thePath);
		if (lookup == m_adminDirs.cend())
		{
			if (const auto found = GitAdminDir::GetAdminDirPath(path); found && PathIsDirectory(found->path.c_str()))
			{
				const std::wstring& adminDir = m_adminDirs.insert_or_assign(thePath, found->path).first->second;
				if (!found->isWorktree) // GitAdminDir::GetAdminDirPath returns the commongit dir ("parent/.git") and this would override the lookup path for the main repo
					m_reverseLookup[BuildPathWithPathDelimiter(NormalizePath(found->path))] = path;
				return adminDir;
			}
			ATLASSERT(false);
			return BuildPathWithPathDelimiter(path) + L".git\\"; // in case of an error stick to old behavior
		}

		return lookup->second;
	}

	std::wstring GetAdminDirConcat(const std::wstring& path, const std::wstring_view subpath)
	{
		std::wstring result = GetAdminDir(path);
		result += subpath;
		return result;
	}

	std::wstring GetWorktreeAdminDir(const std::wstring& path)
	{
		const auto thePath = detail::NormalizedKey(path);
		std::scoped_lock lock(m_mutex);
		const auto lookup = m_WorktreeAdminDirLookup.find(thePath);
		if (lookup == m_WorktreeAdminDirLookup.cend())
		{
			if (const auto wtadmindir = GitAdminDir::GetWorktreeAdminDirPath(path); wtadmindir && PathIsDirectory(wtadmindir->c_str()))
			{
				const std::wstring& adminDir = m_WorktreeAdminDirLookup.insert_or_assign(thePath, *wtadmindir).first->second;
				m_reverseLookup[BuildPathWithPathDelimiter(NormalizePath(*wtadmindir))] = path;
				return adminDir;
			}
			ATLASSERT(false);
			return BuildPathWithPathDelimiter(path) + L".git\\"; // we should never get here
		}
		return lookup->second;
	}

	std::wstring GetWorktreeAdminDirConcat(const std::wstring& path, const std::wstring_view subpath)
	{
		std::wstring result = GetWorktreeAdminDir(path);
		result += subpath;
		return result;
	}

	std::wstring GetWorkingCopy(const std::wstring& gitDir)
	{
		const std::wstring path = BuildPathWithPathDelimiter(NormalizePath(gitDir));
		std::scoped_lock lock(m_mutex);
		const auto lookup = m_reverseLookup.find(path);
		if (lookup == m_reverseLookup.cend())
			return gitDir;
		return lookup->second;
	}

	/// Forgets the working tree -> admin dir lookups (the unit tests do, between
	/// tests). As the map base's clear() it replaces did, it leaves the reverse
	/// and worktree lookups alone. A cache flush, so harmless as public API.
	void clear()
	{
		std::scoped_lock lock(m_mutex);
		m_adminDirs.clear();
	}

private:
	std::mutex								m_mutex;
	std::map<std::wstring, std::wstring>	m_adminDirs;
	std::map<std::wstring, std::wstring>	m_reverseLookup;
	std::map<std::wstring, std::wstring>	m_WorktreeAdminDirLookup;
};


CGitAdminDirMap g_AdminDirMap;
} // export namespace GitIndex



export namespace GitIndex {
// this file is compiled into projects with and without GIT_EXPERIMENTAL_SHA256; only SHA1 repos are supported so far
inline int tgit_odb_hash(git_oid* out, const void* data, size_t len, git_object_t type)
{
#ifdef GIT_EXPERIMENTAL_SHA256
	return git_odb_hash(out, data, len, type, GIT_OID_SHA1);
#else
	return git_odb_hash(out, data, len, type);
#endif
}



bool CGitIndexList::HasIndexChangedOnDisk(const std::wstring& gitdir) const
{
	std::int64_t size = -1;

	const std::wstring indexFile = g_AdminDirMap.GetWorktreeAdminDirConcat(gitdir, L"index");
	// no need to refresh if there is no index right now and the current index is empty, but otherwise lastFileSize or lastmodifiedTime differ
	const auto attr = get_path_attributes(indexFile);
	return !attr || !attr->exists() || m_LastModifyTime != attr->lastWriteTime || m_LastFileSize != attr->size;
}

int CGitIndexList::ReadIndex(const std::wstring& dgitdir)
{
	ATLASSERT(m_entries.empty());

	std::wstring repodir = dgitdir;
	if (dgitdir.size() == 2 && dgitdir[1] == L':')
		repodir += L'\\'; // libgit2 requires a drive root to end with a (back)slash

	const CAutoRepository repository(repodir);
	if (!repository)
	{
		tgit::DebugOutput::TracePrintf(_T(__FUNCTION__) L": Could not open git repository in %s: %s\n", dgitdir.c_str(), static_cast<LPCWSTR>(GetLibGit2LastErr()));
		return -1;
	}

	const std::wstring projectConfig = g_AdminDirMap.GetAdminDir(dgitdir) + L"config";
	const std::wstring globalConfig(g_Git.GetGitGlobalConfig());
	const std::wstring globalXDGConfig(g_Git.GetGitGlobalXDGConfig());
	Registry::CRegStdString systemConfigValue(std::wstring(REG_SYSTEM_GITCONFIGPATH), L"", false);
	const std::wstring& systemConfigW = systemConfigValue; // operator const T& is non-const, hence the named object
	const std::wstring systemConfig(systemConfigW.c_str()); // up to the first NUL, as the CString(c_str()) it replaces kept

	CAutoConfig temp { true };
	git_config_add_file_ondisk(temp, TGitPath::ToGitPathA(projectConfig).c_str(), GIT_CONFIG_LEVEL_LOCAL, repository, FALSE);
	git_config_add_file_ondisk(temp, TGitPath::ToGitPathA(globalConfig).c_str(), GIT_CONFIG_LEVEL_GLOBAL, repository, FALSE);
	git_config_add_file_ondisk(temp, TGitPath::ToGitPathA(globalXDGConfig).c_str(), GIT_CONFIG_LEVEL_XDG, repository, FALSE);
	if (!systemConfig.empty())
		git_config_add_file_ondisk(temp, TGitPath::ToGitPathA(systemConfig).c_str(), GIT_CONFIG_LEVEL_SYSTEM, repository, FALSE);

	git_config_snapshot(config.GetPointer(), temp);
	temp.Free();
	git_repository_set_config(repository, config);

	TGitH::GetFileModifyTime((g_AdminDirMap.GetWorktreeAdminDir(dgitdir) + L"index").c_str(), &m_LastModifyTime, nullptr, &m_LastFileSize);

	CAutoIndex index;
	// load index in order to enumerate files
	if (git_repository_index(index.GetPointer(), repository))
	{
		config.Free();
		TracePrintf(_T(__FUNCTION__) L": Could not get index of git repository in %s: %s\n", dgitdir.c_str(), static_cast<LPCWSTR>(GetLibGit2LastErr()));
		return -1;
	}

	m_bHasConflicts = false;
	m_iIndexCaps = git_index_caps(index);
	if (CRegDWORD(L"Software\\TortoiseGit\\OverlaysCaseSensitive", TRUE) != FALSE)
		m_iIndexCaps &= ~GIT_INDEX_CAPABILITY_IGNORE_CASE;

	const size_t ecount = git_index_entrycount(index);
	try
	{
		m_entries.resize(ecount);
	}
	catch (const std::bad_alloc& ex)
	{
		config.Free();
		TracePrintf(__FUNCTION__ ": Could not resize index-vector: %s\n", ex.what());
		return -1;
	}
	catch (const std::length_error& ex)
	{
		config.Free();
		TracePrintf(__FUNCTION__ ": Could not resize index-vector, length_error: %s\n", ex.what());
		return -1;
	}
	for (size_t i = 0; i < ecount; ++i)
	{
		const git_index_entry *e = git_index_get_byindex(index, i);

		auto& item = m_entries[i];
		item.m_FileName = CUnicodeUtils::Convert2Unicode<std::wstring>(e->path);
		if (e->mode & S_IFDIR)
			item.m_FileName += L'/';
		static_assert(std::is_same_v<decltype(item.m_ModifyTime), decltype(e->mtime.seconds)>);
		item.m_ModifyTime = e->mtime.seconds;
		static_assert(std::is_same_v<decltype(item.m_ModifyTimeNanos), decltype(e->mtime.nanoseconds)>);
		item.m_ModifyTimeNanos = e->mtime.nanoseconds;
		item.m_Flags = e->flags;
		item.m_FlagsExtended = e->flags_extended;
		item.m_IndexHash = e->id;
		static_assert(std::is_same_v<decltype(item.m_Size), decltype(e->file_size)>);
		item.m_Size = e->file_size;
		item.m_Mode = e->mode;
		m_bHasConflicts = m_bHasConflicts || GIT_INDEX_ENTRY_STAGE(e) != 0;
	}

	DoSortFilenametSortVector(m_entries, IsIgnoreCase());

	// best effort: a branch whose upstream state cannot be read does not fail the index
	static_cast<void>(ReadIncomingOutgoing(repository));

	TracePrintf(_T(__FUNCTION__) L": Reloaded index for repo: %s\n", dgitdir.c_str());

	return 0;
}

std::expected<void, int> CGitIndexList::ReadIncomingOutgoing(git_repository* repository)
{
	ATLASSERT(m_stashCount == 0 && !m_outgoing && !m_incoming && m_branch.empty());

	if (!m_bCalculateIncomingOutgoing)
		return {};

	if (git_stash_foreach(repository, [](size_t, const char*, const git_oid*, void* payload) -> int {
		const auto stashCount = static_cast<size_t*>(payload);
		++(*stashCount);
		return 0;
		}, &m_stashCount) < 0)
		return std::unexpected(-1);

	if (const int detachedhead = git_repository_head_detached(repository); detachedhead == 1)
	{
		m_branch = L"detached HEAD";
		return {};
	}
	else if (detachedhead < 0)
		return std::unexpected(-1);

	CAutoReference head;
	if (const int unborn = git_repository_head_unborn(repository); unborn < 0)
		return std::unexpected(-1);
	else if (unborn == 1)
	{
		if (git_reference_lookup(head.GetPointer(), repository, "HEAD") < 0)
			return std::unexpected(-1);

		// StripRefName still speaks CString; a null target reads as empty either way
		m_branch = std::wstring(TGitH::StripRefName(CUnicodeUtils::Convert2Unicode<CString>(git_reference_symbolic_target(head))));
		return {};
	}

	if (git_repository_head(head.GetPointer(), repository) < 0)
		return std::unexpected(-1);

	m_branch = CUnicodeUtils::Convert2Unicode<std::wstring>(git_reference_shorthand(head)); // nullptr reads as empty

	CAutoBuf upstreambranchname;
	git_oid upstream{};
	// check whether there is an upstream branch
	if (git_branch_upstream_name(upstreambranchname, repository, git_reference_name(head)) != 0 || git_reference_name_to_id(&upstream, repository, upstreambranchname->ptr) != 0)
		return {}; // we don't have an upstream branch

	// libgit2 writes through size_t*; only a complete answer is kept
	size_t outgoing = 0;
	size_t incoming = 0;
	if (git_graph_ahead_behind(&outgoing, &incoming, repository, git_reference_target(head), &upstream) < 0)
		return std::unexpected(-1);

	m_outgoing = outgoing;
	m_incoming = incoming;
	return {};
}

int CGitIndexList::GetFileStatus_NoRepo(const std::wstring& gitdir, const std::wstring& pathorg, git_wc_status2_t& status, const std::int64_t time, const std::int64_t filesize, const bool isSymlink, CGitHash* pHash) const
{

	const size_t index = SearchInSortVector(m_entries, pathorg, -1, IsIgnoreCase());

	if (index == NPOS)
	{
		status.status = git_wc_status_unversioned;
		if (pHash)
			pHash->Empty();

		return 0;
	}

	const auto& entry = m_entries[index];
	if (pHash)
		*pHash = entry.m_IndexHash;
	ATLASSERT(IsIgnoreCase() ? tgit::wstr::CompareNoCase(pathorg, entry.m_FileName) == 0 : pathorg == entry.m_FileName);
	CAutoRepository repository;
	return GetFileStatus(repository, gitdir, entry, status, time, filesize, isSymlink);
}

int CGitIndexList::GetFileStatus(CAutoRepository& repository, const std::wstring& gitdir, const CGitIndex& entry, git_wc_status2_t& status, const std::int64_t time, const std::int64_t filesize, const bool isSymlink) const
{
	Expects(!status.assumeValid && !status.skipWorktree);

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
	else if ((isSymlink && !detail::IsSymlinkMode(entry.m_Mode)) || ((m_iIndexCaps & GIT_INDEX_CAPABILITY_NO_SYMLINKS) != GIT_INDEX_CAPABILITY_NO_SYMLINKS && isSymlink != detail::IsSymlinkMode(entry.m_Mode)))
		status.status = git_wc_status_modified;
	else if (!isSymlink && static_cast<uint32_t>(filesize) != entry.m_Size)
		status.status = git_wc_status_modified;
	else if (entry.HasModifyTime(time))
		status.status = git_wc_status_normal;
	else if (config && filesize < m_iMaxCheckSize)
	{
		/*
		 * Opening a new repository each time is not yet optimal, however, there is no API to clear the pack-cache
		 * When a shared repository is used, we might need a mutex to prevent concurrent access to repository instance and especially filter-lists
		 */
		if (!repository)
		{
			std::wstring repodir = gitdir;
			if (gitdir.size() == 2 && gitdir[1] == L':')
				repodir += L'\\'; // libgit2 requires a drive root to end with a (back)slash

			if (repository.Open(repodir))
			{
				TracePrintf(_T(__FUNCTION__) L": Could not open git repository in %s for checking file: %s\n", gitdir.c_str(), static_cast<LPCWSTR>(TGitH::GetLibGit2LastErr()));
				return -1;
			}
			git_repository_set_config(repository, config);
		}

		git_oid actual;
		const auto fileA = CUnicodeUtils::GetUTF8<std::string>(entry.m_FileName);
		if (isSymlink && detail::IsSymlinkMode(entry.m_Mode))
		{
			const auto filename = wstr::to_path(gitdir) / wstr::to_path(entry.m_FileName);
			const auto dest = PathUtils::ReadLink(filename);
			if (
				dest &&
				!tgit_odb_hash(&actual, dest->data(), gsl::narrow<int>(dest->size()), GIT_OBJECT_BLOB) &&
				!git_oid_cmp(&actual, entry.m_IndexHash)
			)

			{
				entry.SetModifyTime(time);
				status.status = git_wc_status_normal;
			}
			else
				status.status = git_wc_status_modified;
		}
		else if (!git_repository_hashfile(&actual, repository, fileA.c_str(), GIT_OBJECT_BLOB, nullptr) && !git_oid_cmp(&actual, entry.m_IndexHash))
		{
			entry.SetModifyTime(time);
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

int CGitIndexList::GetFileStatus(const std::wstring& gitdir, const std::wstring& path, git_wc_status2_t& status, CGitHash* pHash) const
{
	Expects(!status.assumeValid && !status.skipWorktree);

	const auto attrs = get_path_attributes(path.empty() ? wstr::to_path(gitdir) : wstr::to_path(gitdir) / wstr::to_path(path));
	const bool found = attrs && attrs->exists();
	// the repository root itself is only asked whether it is a directory
	const std::int64_t time = found ? attrs->lastWriteTime : 0;
	const std::int64_t filesize = !found ? -1 : path.empty() ? 0 : attrs->size;      // -1 is "deleted" downstream
	const bool isDir = found && attrs->isDirectory;
	const bool isSymlink = found && !path.empty() && attrs->type == fs::file_type::symlink;      // a junction is not a symlink
	const bool symlinksSupported = (m_iIndexCaps & GIT_INDEX_CAPABILITY_NO_SYMLINKS) != GIT_INDEX_CAPABILITY_NO_SYMLINKS;

	if (!isDir || (isSymlink && symlinksSupported))
		return GetFileStatus_NoRepo(gitdir, path, status, time, filesize, isSymlink, pHash);

	if (path.ends_with(L'/'))
	{
		const size_t index = SearchInSortVector(m_entries, path, -1, IsIgnoreCase());
		if (index == NPOS)
		{
			status.status = git_wc_status_unversioned;
			if (pHash)
				pHash->Empty();

			return 0;
		}

		if (pHash)
			*pHash = m_entries[index].m_IndexHash;

		status.status = found ? git_wc_status_normal : git_wc_status_deleted;
		return 0;
	}

	// we get here for symlinks which are handled as files inside the git index
	if (symlinksSupported)
		return GetFileStatus_NoRepo(gitdir, path, status, time, filesize, isSymlink, pHash);

	// we should never get here
	status.status = git_wc_status_unversioned;

	return -1;
}

// This method is assumed to be called with m_SharedMutex locked.
std::expected<void, int> CGitHeadFileList::GetPackRef(const std::wstring& gitdir)
{
	const std::wstring PackRef = g_AdminDirMap.GetAdminDirConcat(gitdir, L"packed-refs");

	std::int64_t mtime = 0, packsize = -1;
	if (TGitH::GetFileModifyTime(PackRef.c_str(), &mtime, nullptr, &packsize))
	{
		//packed refs is not existed
		this->m_PackRefFile.clear();
		this->m_PackRefMap.clear();
		return {};
	}
	else if (mtime == m_LastModifyTimePackRef && packsize == m_LastFileSizePackRef)
		return {};
	else
	{
		this->m_PackRefFile = PackRef;
		this->m_LastModifyTimePackRef = mtime;
		this->m_LastFileSizePackRef = packsize;
	}

	m_PackRefMap.clear();

	const auto packed = detail::ReadSmallFile(PackRef.c_str(), detail::ShareAll, std::numeric_limits<int>::max() - 1, detail::ShortRead::Fail);
	if (!packed)
		return std::unexpected(-1);

	// One "<hash> <ref>" per line. '#' (the header) and '^' (a peeled tag's
	// target) lines are not refs, nor is a line with no ref after the space.
	// The ref runs to the '\n', so a '\r' before it stays part of the ref.
	for (const auto lineRange : std::views::split(*packed, '\n'))
	{
		const std::string_view line(std::ranges::data(lineRange), std::ranges::size(lineRange));
		if (line.empty() || line.starts_with('#') || line.starts_with('^'))
			continue;

		const auto space = line.find(' ');
		if (space == std::string_view::npos)
			continue;

		const auto ref = line.substr(space + 1);
		if (ref.empty())
			continue;

		if (const auto refHash = CGitHash::FromHexStr(line.substr(0, space)); !refHash.IsEmpty())
			m_PackRefMap[CUnicodeUtils::GetUnicode<std::wstring>(ref)] = refHash;
	}
	return {};
}

std::expected<void, int> CGitHeadFileList::ReadHeadHash(const std::wstring& gitdir)
{
	Expects(m_Gitdir.empty() && m_HeadFile.empty() && m_Head.IsEmpty());

	m_Gitdir = g_AdminDirMap.GetWorktreeAdminDir(gitdir);

	m_HeadFile = m_Gitdir;
	m_HeadFile += L"HEAD";

	if (TGitH::GetFileModifyTime(m_HeadFile.c_str(), &m_LastModifyTimeHead, nullptr, &m_LastFileSizeHead))
		return std::unexpected(-1);

	constexpr std::string_view refPrefix = "ref:";
	constexpr std::uint64_t maxHeadSize = 100 * 1024 * 1024 - 1;
	// hex length of an object id in the active format
	const size_t hexSize = static_cast<size_t>(2 * tgit::invarients::HashSize());

	const auto headBytes = detail::ReadSmallFile(m_HeadFile.c_str(), detail::ShareAll, maxHeadSize, detail::ShortRead::Keep);
	if (!headBytes || headBytes->size() < refPrefix.size())
		return std::unexpected(-1);
	const std::string_view head = *headBytes;

	if (head.starts_with(refPrefix))
	{
		m_HeadRefFile.clear();
		if (head.size() < refPrefix.size() + 1)
			return std::unexpected(-1);

		// m_HeadRefFile keeps the trimmed text; ref is only its first line
		m_HeadRefFile = CUnicodeUtils::GetUnicode<std::wstring>(head.substr(refPrefix.size()));
		detail::TrimSpace(m_HeadRefFile);
		int start = 0;
		const std::wstring ref = tgit::wstr::Tokenize(m_HeadRefFile, L"\n", start);
		m_HeadRefFile = g_AdminDirMap.GetAdminDir(gitdir) + m_HeadRefFile;
		std::ranges::replace(m_HeadRefFile, L'/', L'\\');

		std::int64_t time;
		if (TGitH::GetFileModifyTime(m_HeadRefFile.c_str(), &time, nullptr))
		{
			if (auto packed = GetPackRef(gitdir); !packed)
				return packed;
			if (m_PackRefMap.contains(ref))
			{
				m_bRefFromPackRefFile = true;
				m_Head = m_PackRefMap[ref];
				return {};
			}

			// unborn branch
			m_Head.Empty();

			return {};
		}

		const auto href = detail::OpenForRead(m_HeadRefFile.c_str(), detail::ShareAll);
		if (!href)
		{
			if (auto packed = GetPackRef(gitdir); !packed)
				return packed;

			if (!m_PackRefMap.contains(ref))
				return std::unexpected(-1);

			m_bRefFromPackRefFile = true;
			m_Head = m_PackRefMap[ref];
			return {};
		}

		// the loose ref's first hexSize bytes are the id; fewer is a failure
		const auto loose = detail::ReadSmallFile(href.get(), maxHeadSize, detail::ShortRead::Keep);
		if (!loose || loose->size() < hexSize)
			return std::unexpected(-1);

		m_Head = CGitHash::FromHexStr(std::string_view(*loose).substr(0, hexSize));

		m_LastModifyTimeRef = time;

		return {};
	}

	// detached: HEAD's first hexSize bytes are the id, "ref:"'s four included
	if (head.size() < hexSize)
		return std::unexpected(-1);

	m_HeadRefFile.clear();

	m_Head = CGitHash::FromHexStr(head.substr(0, hexSize));

	return {};
}

bool CGitHeadFileList::CheckHeadUpdate() const
{
	if (this->m_HeadFile.empty())
		return true;

	std::int64_t mtime = 0, size = -1;

	if (TGitH::GetFileModifyTime(m_HeadFile.c_str(), &mtime, nullptr, &size))
		return true;

	if (mtime != m_LastModifyTimeHead || size != m_LastFileSizeHead)
		return true;

	if (!this->m_HeadRefFile.empty())
	{
		// we need to check for the HEAD ref file here, because the original ref might have come from packedrefs and now is a ref-file
		if (TGitH::GetFileModifyTime(m_HeadRefFile.c_str(), &mtime))
		{
			if (!m_bRefFromPackRefFile)
				return true;

		} else if (mtime != this->m_LastModifyTimeRef)
			return true;
	}

	if (m_bRefFromPackRefFile && !m_PackRefFile.empty())
	{
		size = -1;
		if (TGitH::GetFileModifyTime(m_PackRefFile.c_str(), &mtime, nullptr, &size))
			return true;

		if (mtime != m_LastModifyTimePackRef || size != m_LastFileSizePackRef)
			return true;
	}

	// in an empty repo HEAD points to refs/heads/master, but this ref doesn't exist.
	// So we need to retry again and again until the ref exists - otherwise we will never notice
	if (this->m_Head.IsEmpty() && this->m_HeadRefFile.empty() && this->m_PackRefFile.empty())
		return true;

	return false;
}

void CGitHeadFileList::ReadTreeRecursive(git_repository& repo, const git_tree* tree, const std::wstring& base)
{
	const size_t count = git_tree_entrycount(tree);
	for (size_t i = 0; i < count; ++i)
	{
		const git_tree_entry *entry = git_tree_entry_byindex(tree, i);
		if (!entry)
			continue;
		const int mode = git_tree_entry_filemode(entry);
		const bool isDir = (mode & S_IFDIR) == S_IFDIR;
		const bool isSubmodule = detail::IsGitlinkMode(static_cast<std::uint32_t>(mode));
		if (!isDir || isSubmodule)
		{
			// tree entry names are UTF-8 by git's contract, so the fast path; like
			// CGit::StringAppend, a failed conversion contributes nothing rather than throwing
			CGitTreeItem item;
			item.m_Hash = git_tree_entry_id(entry);
			item.m_FileName = base + CUnicodeUtils::Convert2Unicode<std::wstring>(git_tree_entry_name(entry));
			if (isSubmodule)
				item.m_FileName += L'/';
			m_entries.push_back(std::move(item));
			continue;
		}

		CAutoObject object;
		git_tree_entry_to_object(object.GetPointer(), &repo, entry);
		if (!object)
			continue;
		std::wstring parent = base + CUnicodeUtils::Convert2Unicode<std::wstring>(git_tree_entry_name(entry));
		parent += L'/';
		ReadTreeRecursive(repo, reinterpret_cast<git_tree*>(static_cast<git_object*>(object)), parent);
	}
}

// ReadTree is/must only be executed on an empty list
std::expected<void, int> CGitHeadFileList::ReadTree(const bool ignoreCase)
{
	Expects(m_entries.empty());

	// unborn branch
	if (m_Head.IsEmpty())
		return {};

	const CAutoRepository repository(m_Gitdir);
	CAutoCommit commit;
	CAutoTree tree;
	bool ret = repository;
	ret = ret && !git_commit_lookup(commit.GetPointer(), repository, m_Head);
	ret = ret && !git_commit_tree(tree.GetPointer(), commit);
	try
	{
		if (ret)
			ReadTreeRecursive(*repository, tree, L""); // cannot fail short of throwing; skips entries it cannot read
	}
	catch (const std::bad_alloc& ex)
	{
		TracePrintf(__FUNCTION__ ": Catched exception inside ReadTreeRecursive: %s\n", ex.what());
		return std::unexpected(-1);
	}
	catch (const std::length_error& ex)
	{
		TracePrintf(__FUNCTION__ ": Catched exception inside ReadTreeRecursive, length_error: %s\n", ex.what());
		return std::unexpected(-1);
	}
	if (!ret)
	{
		m_entries.clear();
		TracePrintf(_T(__FUNCTION__) L": Could not open git repository in %s and read HEAD commit %s: %s\n", m_Gitdir.c_str(), m_Head.ToString().c_str(), static_cast<LPCWSTR>(TGitH::GetLibGit2LastErr()));
		m_LastModifyTimeHead = 0;
		m_LastFileSizeHead = -1;
		return std::unexpected(-1);
	}

	DoSortFilenametSortVector(m_entries, ignoreCase);

	TracePrintf(_T(__FUNCTION__) L": Reloaded HEAD tree (commit is %s) for repo: %s\n", m_Head.ToString().c_str(), m_Gitdir.c_str());

	return {};
}

int CGitIgnoreItem::FetchIgnoreList(const std::wstring& projectroot, const std::wstring& file, const bool isGlobal, int* ignoreCase)
{
	// the list before the buffer its patterns point into
	m_pExcludeList.reset();
	m_buffer = std::vector<char>{};

	this->m_BaseDir.clear();
	if (!isGlobal)
	{
		std::wstring base = tgit::wstr::Mid(file, gsl::narrow<int>(projectroot.size()) + 1); // clamps, as CString::Mid did
		std::ranges::replace(base, L'\\', L'/');

		const int start = tgit::wstr::ReverseFind(base, L'/');
		if(start > 0)
		{
			base.resize(static_cast<size_t>(start));
			this->m_BaseDir = CUnicodeUtils::GetUTF8<std::string>(base) + "/";
		}
	}

	if (TGitH::GetFileModifyTime(file.c_str(), &m_LastModifyTime, nullptr, &m_LastFileSize))
		return -1;

	// a short read keeps what arrived, as it always has
	auto bytes = detail::ReadSmallFile(file.c_str(), detail::ShareAll, 100 * 1024 * 1024, detail::ShortRead::Keep);
	if (!bytes)
		return -1;

	{
		EXCLUDE_LIST list = nullptr;
		if (git_create_exclude_list(&list))
			return -1;
		m_pExcludeList.reset(list);
	}

	m_iIgnoreCase = ignoreCase;

	// git_add_exclude keeps pointers into m_buffer - to each pattern, and to the
	// base directory every pattern shares - so it takes its final storage here,
	// before the first line is handed out, and is not touched again until the
	// list is freed. The base directory goes first, NUL-terminated as the
	// CStringA it was handed as used to be. Each line is NUL-terminated in
	// place; the last one, when the file does not end in a newline, by the NUL
	// appended after the file's bytes.
	m_buffer.reserve(m_BaseDir.size() + 1 + bytes->size() + 1);
	m_buffer.assign(m_BaseDir.cbegin(), m_BaseDir.cend());
	m_buffer.push_back('\0');
	const size_t textOffset = m_buffer.size();
	m_buffer.insert(m_buffer.end(), bytes->cbegin(), bytes->cend());
	m_buffer.push_back('\0');
	const char* const baseDir = m_buffer.data();
	const int baseDirLength = gsl::narrow<int>(m_BaseDir.size());
	char* const text = m_buffer.data() + textOffset;
	const std::string_view content(text, bytes->size());
	int line = 0;
	for (size_t begin = 0; begin < content.size();)
	{
		const size_t eol = content.find_first_of("\r\n", begin);
		if (eol != std::string_view::npos)
			text[eol] = '\0';

		if (text[begin] != '#' && text[begin] != '\0')
			git_add_exclude(text + begin, baseDir, baseDirLength, m_pExcludeList.get(), ++line);

		if (eol == std::string_view::npos)
			break;
		begin = eol + 1;
	}

	if (!line)
	{
		m_pExcludeList.reset();
		m_buffer = std::vector<char>{};
	}

	return 0;
}

int CGitIgnoreItem::IsPathIgnored(const std::string& patha, int& type)
{
	// base points INTO patha, just past its last '/'
	const size_t pos = patha.rfind('/');
	const char* const base = pos != std::string::npos ? patha.c_str() + pos + 1 : patha.c_str();

	return IsPathIgnored(patha, base, type);
}


bool CGitIgnoreList::CheckFileChanged(const std::wstring& path)
{
	std::int64_t time = 0, size = -1;

	const int ret = TGitH::GetFileModifyTime(path.c_str(), &time, nullptr, &size);

	bool cacheExist;
	{
		[[maybe_unused]]CAutoReadLock lock(m_SharedMutex);
		cacheExist = (m_Map.contains(path));
	}

	if (!cacheExist && ret == 0)
	{
		[[maybe_unused]]CAutoWriteLock lock(m_SharedMutex);
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
		[[maybe_unused]]CAutoReadLock lock(m_SharedMutex);
		if (m_Map[path].m_LastModifyTime == time && m_Map[path].m_LastFileSize == size)
			return false;
	}
	return true;
}

void CGitIgnoreList::FetchIgnoreFile(const std::wstring& gitdir, const std::wstring& gitignore, const bool isGlobal)
{
	if (GitAdminDir::GitPathFileExists(gitignore)) // if .gitignore is removed, we need also to remove it from our cache
	{
		[[maybe_unused]]CAutoWriteLock lock(m_SharedMutex);
		m_Map[gitignore].FetchIgnoreList(gitdir, gitignore, isGlobal, &m_IgnoreCase[g_AdminDirMap.GetAdminDir(gitdir)]);
	}
	else
	{
		[[maybe_unused]]CAutoWriteLock lock(m_SharedMutex);
		m_Map.erase(gitignore);
	}
}

bool CGitIgnoreList::CheckAndUpdateIgnoreFiles(const std::wstring& gitdir, const std::wstring& path, const bool isDir, std::set<std::wstring>* lastChecked)
{
	std::wstring temp = std::format(L"{}\\{}", gitdir, path);

	std::ranges::replace(temp, L'/', L'\\');

	if (!isDir)
	{
		const int x = tgit::wstr::ReverseFind(temp, L'\\');
		if (x >= 2)
			temp.resize(static_cast<size_t>(x));
	}

	bool updated = false;
	while (!temp.empty())
	{
		if (lastChecked)
		{
			if (lastChecked->contains(temp))
				return updated;
			lastChecked->insert(temp);
		}

		temp += detail::GitIgnoreSuffix;

		if (CheckFileChanged(temp))
		{
			FetchIgnoreFile(gitdir, temp, false);
			updated = true;
		}

		temp.resize(temp.size() - detail::GitIgnoreSuffix.size());
		if (PathUtils::ArePathStringsEqual(temp, gitdir))
		{
			const std::wstring adminDir = g_AdminDirMap.GetAdminDir(temp);
			const std::wstring wcglobalgitignore = adminDir + L"info\\exclude";
			if (CheckFileChanged(wcglobalgitignore))
			{
				FetchIgnoreFile(gitdir, wcglobalgitignore, true);
				updated = true;
			}

			if (CheckAndUpdateCoreExcludefile(adminDir))
			{
				std::wstring excludesFile;
				{
					[[maybe_unused]]CAutoReadLock lock(m_SharedMutex);
					excludesFile = m_CoreExcludesfiles[adminDir];
				}
				if (!excludesFile.empty())
				{
					FetchIgnoreFile(gitdir, excludesFile, true);
					updated = true;
				}
			}

			return updated;
		}

		const int i = tgit::wstr::ReverseFind(temp, L'\\');
		temp.resize(static_cast<size_t>(std::max(0, i)));
	}
	return updated;
}

bool CGitIgnoreList::CheckAndUpdateGitSystemConfigPath(const bool force)
{
	// recheck every 30 seconds
	if (GetTickCount64() - m_dGitSystemConfigPathLastChecked > 30000UL || force)
	{
		m_dGitSystemConfigPathLastChecked = GetTickCount64();
		Registry::CRegStdString gitSystemConfigValue(std::wstring(REG_SYSTEM_GITCONFIGPATH), L"", false);
		const std::wstring& gitSystemConfigW = gitSystemConfigValue;
		const std::wstring gitSystemConfigPath{ gitSystemConfigW.c_str() }; // up to the first NUL, as the CString(c_str()) it replaces kept
		if (gitSystemConfigPath != m_sGitSystemConfigPath)
		{
			m_sGitSystemConfigPath = gitSystemConfigPath;
			return true;
		}
	}
	return false;
}
bool CGitIgnoreList::CheckAndUpdateCoreExcludefile(const std::wstring& adminDir)
{
	const std::wstring projectConfig = adminDir + L"config";
	const std::wstring globalConfig(g_Git.GetGitGlobalConfig());
	const std::wstring globalXDGConfig(g_Git.GetGitGlobalXDGConfig());

	[[maybe_unused]]CAutoWriteLock lock(m_coreExcludefilesSharedMutex);
	bool hasChanged = CheckAndUpdateGitSystemConfigPath();
	hasChanged = hasChanged || CheckFileChanged(projectConfig);
	hasChanged = hasChanged || CheckFileChanged(globalConfig);
	hasChanged = hasChanged || CheckFileChanged(globalXDGConfig);
	if (!m_sGitSystemConfigPath.empty())
		hasChanged = hasChanged || CheckFileChanged(m_sGitSystemConfigPath);

	std::wstring excludesFile;
	{
		[[maybe_unused]]CAutoReadLock lock2(m_SharedMutex);
		excludesFile = m_CoreExcludesfiles[adminDir];
	}
	if (!excludesFile.empty())
		hasChanged = hasChanged || CheckFileChanged(excludesFile);

	if (!hasChanged)
		return false;

	const CAutoConfig config(true);
	const CAutoRepository repo(adminDir);
	git_config_add_file_ondisk(config, TGitPath::ToGitPathA(projectConfig).c_str(), GIT_CONFIG_LEVEL_LOCAL, repo, FALSE);
	git_config_add_file_ondisk(config, TGitPath::ToGitPathA(globalConfig).c_str(), GIT_CONFIG_LEVEL_GLOBAL, repo, FALSE);
	git_config_add_file_ondisk(config, TGitPath::ToGitPathA(globalXDGConfig).c_str(), GIT_CONFIG_LEVEL_XDG, repo, FALSE);
	if (!m_sGitSystemConfigPath.empty())
		git_config_add_file_ondisk(config, TGitPath::ToGitPathA(m_sGitSystemConfigPath).c_str(), GIT_CONFIG_LEVEL_SYSTEM, repo, FALSE);

	config.GetString(L"core.excludesfile", excludesFile);
	if (excludesFile.empty())
		excludesFile = GetWindowsHome() + L"\\.config\\git\\ignore";
	else if (excludesFile.starts_with(L"~/"))
		excludesFile = GetWindowsHome() + excludesFile.substr(1); // drop the '~', keep the '/'

	[[maybe_unused]]CAutoWriteLock lockMap(m_SharedMutex);
	m_IgnoreCase[adminDir] = 1;
	config.GetBOOL(L"core.ignorecase", m_IgnoreCase[adminDir]);
	TGitH::GetFileModifyTime(projectConfig.c_str(), &m_Map[projectConfig].m_LastModifyTime, nullptr, &m_Map[projectConfig].m_LastFileSize);
	TGitH::GetFileModifyTime(globalXDGConfig.c_str(), &m_Map[globalXDGConfig].m_LastModifyTime, nullptr, &m_Map[globalXDGConfig].m_LastFileSize);
	if (m_Map[globalXDGConfig].m_LastModifyTime == 0)
		m_Map.erase(globalXDGConfig);
	TGitH::GetFileModifyTime(globalConfig.c_str(), &m_Map[globalConfig].m_LastModifyTime, nullptr, &m_Map[globalConfig].m_LastFileSize);
	if (m_Map[globalConfig].m_LastModifyTime == 0)
		m_Map.erase(globalConfig);
	if (!m_sGitSystemConfigPath.empty())
		TGitH::GetFileModifyTime(m_sGitSystemConfigPath.c_str(), &m_Map[m_sGitSystemConfigPath].m_LastModifyTime, nullptr, &m_Map[m_sGitSystemConfigPath].m_LastFileSize);
	if (m_Map[m_sGitSystemConfigPath].m_LastModifyTime == 0 || m_sGitSystemConfigPath.empty())
		m_Map.erase(m_sGitSystemConfigPath);
	m_CoreExcludesfiles[adminDir] = excludesFile;

	return true;
}


bool CGitIgnoreList::IsIgnore(std::wstring str, const std::wstring& projectroot, const bool isDir, const std::wstring& adminDir)
{
	std::ranges::replace(str, L'\\', L'/');

	if (str.ends_with(L'/'))
		str.pop_back();

	int ret = CheckIgnore(str, projectroot, isDir, adminDir);
	while (ret < 0)
	{
		const int start = tgit::wstr::ReverseFind(str, L'/');
		if(start < 0)
			return (ret == 1);

		str.resize(static_cast<size_t>(start));
		ret = CheckIgnore(str, projectroot, true, adminDir);
	}

	return (ret == 1);
}
int CGitIgnoreList::CheckFileAgainstIgnoreList(const std::wstring& ignorefile, const std::string& patha, const char* base, int& type)
{
	const auto item = m_Map.find(ignorefile);
	if (item == m_Map.cend())
		return -1; // error or undecided

	return item->second.IsPathIgnored(patha, base, type);
}
int CGitIgnoreList::CheckIgnore(const std::wstring& path, const std::wstring& projectroot, const bool isDir, const std::wstring& adminDir)
{
	std::wstring temp = (wstr::to_path(projectroot) / wstr::to_path(path)).native();
	std::ranges::replace(temp, L'/', L'\\');

	std::string patha = CUnicodeUtils::GetUTF8<std::string>(path);
	std::ranges::replace(patha, '\\', '/');

	auto type = [&] {
		if (isDir)
		{
			// strip directory name
			// we do not need to check for a .ignore file inside a directory we might ignore
			if (const int i = tgit::wstr::ReverseFind(temp, L'\\'); i >= 0)
				temp.resize(static_cast<size_t>(i));
			return DT_DIR;
		}
		else
		{
			const int x = tgit::wstr::ReverseFind(temp, L'\\');
			if (x >= 2)
				temp.resize(static_cast<size_t>(x));

			return DT_REG;
		}
	}();

	// base points INTO patha, just past its last '/'
	const size_t pos = patha.rfind('/');
	const char* const base = pos != std::string::npos ? patha.c_str() + pos + 1 : patha.c_str();


	[[maybe_unused]]CAutoReadLock lock(m_SharedMutex);
	while (!temp.empty())
	{
		temp += detail::GitIgnoreSuffix;

		if (const auto ret = CheckFileAgainstIgnoreList(temp, patha, base, type); ret != pattern_match_result::UNDECIDED)
			return ret;

		temp.resize(temp.size() - detail::GitIgnoreSuffix.size());

		if (PathUtils::ArePathStringsEqual(temp, projectroot))
		{
			const std::wstring wcglobalgitignore = adminDir + L"info\\exclude";
			if (const auto ret = CheckFileAgainstIgnoreList(wcglobalgitignore, patha, base, type); ret != pattern_match_result::UNDECIDED)
				return ret;

			const std::wstring excludesFile = m_CoreExcludesfiles[adminDir];
			if (!excludesFile.empty())
				return CheckFileAgainstIgnoreList(excludesFile, patha, base, type);

			return -1;
		}

		const int i = tgit::wstr::ReverseFind(temp, L'\\');
		temp.resize(static_cast<size_t>(std::max(0, i)));
	}

	return -1;
}

SHARED_TREE_PTR CGitHeadFileMap::CheckHeadAndUpdate(const std::wstring& gitdir, const bool ignoreCase)
{
	if (auto ptr = this->SafeGet(gitdir); ptr.get() && !ptr->CheckHeadUpdate())
		return ptr;

	auto newPtr = std::make_shared<CGitHeadFileList>();
	// ReadTree runs only once ReadHeadHash has succeeded
	if (!newPtr->ReadHeadHash(gitdir).and_then([&] { return newPtr->ReadTree(ignoreCase); }))
	{
		SafeClear(gitdir);
		return {};
	}

	this->SafeSet(gitdir, newPtr);

	return newPtr;
}

struct CGitRepoLists
{
	SHARED_INDEX_PTR pIndex;
	SHARED_TREE_PTR pTree;
};


}

export using namespace GitIndex;