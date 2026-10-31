module;
// No ATL header: outside the str_utils shim, atlbase.h without afx.h first gives
// ATL's inline functions (AtlThrowImpl) a non-MFC body in an MFC target - an ODR
// violation the linker resolves silently. The locks are std, the asserts _ASSERTE.
#include <winsock2.h>		// before windows.h, whose winsock.h would otherwise collide with libgit2's
#include <windows.h>
#include <shlwapi.h>		// PathFileExists, PathIsDirectory, PathCanonicalize, StrCmpLogicalW
#pragma comment(lib, "Shlwapi.lib")	// named here, not left to whatever header used to pull it in
#include <crtdbg.h>			// _ASSERTE: a macro, which no import carries
#include <strsafe.h>
#include <tgitlib/sanity.h>			// Expects/SCOPE_EXIT: macros, which no import carries
#include <gitdll.h>
#include <git2.h>
#include <source_location>
#include <stdexcept>

#include <git2/sys/errors.h>
#include <git2/sys/filter.h>
#include <git2/sys/transport.h>

#include "tgitlib/ssh-wintunnel.h"
#include "tgitlib/filter-filter.h"

#define STR2(x) #x
#define STR(x) STR2(x)
#define FILE_LINE __FUNCTION__ __FILE__ ":" STR(__LINE__)
#define HERE std::to_array(FILE_LINE)



export module git_h;
// NOLINTBEGIN(readability-convert-member-functions-to-static, cppcoreguidelines-narrowing-conversions, readability-non-const-parameter)
import std;
using TGString = std::wstring;
using TGStringView = std::wstring_view;

import wil;		// qualified at every use; no using-directive

import gsl;
import DebugOutput;
import GitHash;
import PathUtils;
import str_utils;
import TGitPath;
import invarients;
import libgit2;
import Registry;
import ShellEnvironment;
export import GitCall;	// CGitCall, the TGIT_GIT_* codes and G_OK/GFail are part of git_h's surface

using namespace TGitPath;
using TGitHash::CGitHash;
using tgit::str::StringView;
using CStringW = tgit::wstr::CString;
using CStringA = tgit::zstr::CString;
// CSimpleStringT's own buffer-helper typedefs: right in both flavors, where
// atlsimpstr.h's global CStrBuf is CStrBufT<TCHAR, false> even under MFC.
using CStrBufW = CStringW::CStrBuf;
using CStrBufA = CStringA::CStrBuf;
using wil::CFormatMessageWrapper;
using tgit::DebugOutput::TracePrintf;
namespace GitFormatHolder = tgit::invarients::GitFormatHolder;
using namespace tgit;
using namespace GitCall;


constexpr auto scope_flag(const CONFIG_TYPE t) {
	switch (t) {
	case CONFIG_XDGGLOBAL:
	case CONFIG_GLOBAL:
		return "--global";

	case CONFIG_LOCAL:
		return "--local";

	case CONFIG_SYSTEM:
		return "--system";
	}
	std::unreachable();
}


export namespace TGitH {
namespace fs = std::filesystem;
using namespace std::literals;

/// The scoped lock over CGit::m_critGitDllSec. Exported: GitRev, GitRevLoglist,
/// GitMailmap and TortoiseProc take g_Git.m_critGitDllSec with it.
/// Recursive, as the CComAutoCriticalSection it replaces was: a holder calls back
/// into CGit members that take the same lock.
using CAutoLocker = std::scoped_lock<std::recursive_mutex>;
// using CScopeLocker = std::scoped_lock<std::recursive_mutex>;

using dict_t = std::map<std::wstring, std::wstring>;
using time_formater_f = std::function<CStringW(__time64_t)>;


// ---- From tgittypes (moved verbatim; tgittypes is gone) ---------------------------

struct TGitRef {
	std::wstring name;
	CGitHash hash;
	// There was an `operator const CStringW&() const { return name; }` here. Same
	// disease as CGitByteArray's operator CStringW(): it let a whole ref stand in
	// for its name silently, so a call site could not be read without knowing the
	// overload set around it. Say `.name`.
};



// std::wstring rather than CStringW, for the reason WideString.h opens with:
// CStringW is a different type in an MFC project than in an ATL one, so an alias
// naming it cannot appear in a header both flavors compile. Values, not views -
// these containers own their strings and outlive whatever produced them.
// The one-argv, string-map and byte-buffer aliases are gone: those are spelled
// std::vector<std::wstring>, dict_t and std::string.
//
// A list of *commands*, each one an argv vector. Named because the distinction
// was previously invisible: CProgressDlg's list of whole command lines was also
// spelled as a single argv, one alias away from a single command's arguments.
using ARGV_VECTOR = std::vector<wil::ArgvData>;
using MAP_HASH_NAME = std::unordered_map<CGitHash, std::vector<std::wstring>>;
using REF_VECTOR = std::vector<TGitRef>;

// gourd against an MSVC bug
static_assert(std::ranges::borrowed_range<decltype(std::views::drop(std::declval<REF_VECTOR&>(), 0))>);



/// git's version as one comparable int, 8 bits per field. It returned std::byte,
/// where `major << 24` shifts every bit out - so each version compared by its
/// build number alone. Exported: the unit tests gate on it. constexpr, not
/// consteval: CGit compares it against the version it reads at GitCall::Runtime.
constexpr int ConvertVersionToInt(const int major, const int minor, const int patchlevel, const int build = 0) {
	return (major << 24) | (minor << 16) | (patchlevel << 8) | build;
}


// Value names under HKCU\Registry::TortoiseGitKey. Arrays, not views: they go
// straight to the registry API, so they are NUL-terminated by construction.
constexpr auto REG_MSYSGIT_PATH = L"MSysGit"sv;
constexpr auto REG_SYSTEM_GITCONFIGPATH = L"SystemConfig"sv;
constexpr auto REG_MSYSGIT_EXTRA_PATH = L"MSysGitExtra"sv;

// The REG_SOFTWARE_ROOT / REG_SOFTWARE32_ROOT / REG_UNINSTALL_TGIT_SUBKEY macros
// spelled these by literal concatenation; the bytes are the same.
constexpr auto UNINSTALL_REG_VALUE = L"InstallLocation"sv;
constexpr auto UNINSTALL_REG_SUBKEY = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Git_is1)"sv;
constexpr auto UNINSTALL_RGSK_WOW64 = LR"(SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Git_is1)"sv;



enum LOG_ORDER {
	LOG_ORDER_CHRONOLOGIALREVERSED,
	LOG_ORDER_TOPOORDER,
	LOG_ORDER_DATEORDER,
	LOG_ORDER_AUTHORDATEORDER,
};



enum BRANCH_TYPE {
	BRANCH_LOCAL = 0x1,
	BRANCH_REMOTE = 0x2,
	BRANCH_FETCH_HEAD = 0x4,
	BRANCH_LOCAL_F = BRANCH_LOCAL | BRANCH_FETCH_HEAD,
	BRANCH_ALL = BRANCH_LOCAL | BRANCH_REMOTE,
	BRANCH_ALL_F = BRANCH_ALL | BRANCH_FETCH_HEAD,
};



enum LOG_INFO_MASK {
	LOG_INFO_STAT = 0x1,
	LOG_INFO_FILESTATE = 0x2,
	LOG_INFO_BOUNDARY = 0x10,
	LOG_INFO_ALL_BRANCH = 0x20,
	LOG_INFO_ONLY_HASH = 0x40,
	LOG_INFO_DETECT_RENAME = 0x80,
	LOG_INFO_DETECT_COPYRENAME = 0x100,
	LOG_INFO_FIRST_PARENT = 0x200,
	LOG_INFO_NO_MERGE = 0x400,
	LOG_INFO_FOLLOW = 0x800,
	LOG_INFO_SHOW_MERGEDFILE = 0x1000,
	LOG_INFO_FULL_DIFF = 0x2000,
	LOG_INFO_SIMPILFY_BY_DECORATION = 0x4000,
	LOG_INFO_LOCAL_BRANCHES = 0x8000,
	LOG_INFO_BASIC_REFS = 0x10000,
	LOG_INFO_SPARSE = 0x20000,
	LOG_INFO_ALWAYS_APPLY_RANGE = 0x40000,
	LOG_INFO_FULL_HISTORY = 0x80000,
};



enum REF_TYPE {
	LOCAL_BRANCH,
	REMOTE_BRANCH,
	ANNOTATED_TAG,
	TAG,
	STASH,
	BISECT_GOOD,
	BISECT_BAD,
	BISECT_SKIP,
	NOTES,
	UNKNOWN,
};



enum LIBGIT2_CMD : std::uint32_t {
	GIT_CMD_CLONE = 1,
	GIT_CMD_FETCH,
	GIT_CMD_COMMIT_UPDATE_INDEX,
	GIT_CMD_DIFF,
	GIT_CMD_RESET,
	GIT_CMD_REVERT,
	GIT_CMD_MERGE_BASE,
	GIT_CMD_DELETETAGBRANCH,
	GIT_CMD_GETONEFILE,
	GIT_CMD_ADD,
	GIT_CMD_PUSH,
	GIT_CMD_CHECK_CLEAN_WT,
	GIT_CMD_CHECKCONFLICTS,
	GIT_CMD_GET_COMMIT,
	GIT_CMD_LOGLISTDIFF,
	GIT_CMD_BRANCH_CONTAINS,
	GIT_CMD_GETCONFLICTINFO,
	GIT_CMD_FOREACHREF,
	LAST_VALUE,
};



// Only the commands that still fork at GitCall::Runtime appear here; the ones in
// LIBGIT2_ONLY_MASK are on whatever the registry says. GETONEFILE, GET_COMMIT
// and FOREACHREF are what is left of the old default list - the other five
// entries moved into LIBGIT2_ONLY_MASK, which is not a change of behaviour for
// anyone since they were on by default already.
constexpr std::uint32_t DEFAULT_USE_LIBGIT2_MASK = (
	1 << GIT_CMD_GETONEFILE |
	1 << GIT_CMD_GET_COMMIT |
	1 << GIT_CMD_FOREACHREF
);

// Commands whose git.exe implementation is gone, so there is nothing left to
// switch between. They keep their LIBGIT2_CMD value because GetGitLastErr still
// has to know that the error worth reporting is libgit2's - and because when
// this mask has swallowed the enum, UsingLibGit2 and the registry mask can go
// with it in one move.
//
// GETONEFILE, GET_COMMIT and LOGLISTDIFF are deliberately absent: their other
// branch is gitdll, not git.exe, and gitdll is a backend this project wants more
// of rather than less. FOREACHREF is absent because its git.exe path is the
// primary implementation when Browse References filters on merge status, not a
// fallback. CLONE, FETCH, RESET and COMMIT_UPDATE_INDEX are absent because they
// select between two whole dialogs, which is a bigger change than this one.
constexpr std::uint32_t LIBGIT2_ONLY_MASK = (
	1 << GIT_CMD_DIFF |
	1 << GIT_CMD_REVERT |
	1 << GIT_CMD_MERGE_BASE |
	1 << GIT_CMD_DELETETAGBRANCH |
	1 << GIT_CMD_ADD |
	1 << GIT_CMD_PUSH |
	1 << GIT_CMD_CHECK_CLEAN_WT |
	1 << GIT_CMD_CHECKCONFLICTS |
	1 << GIT_CMD_BRANCH_CONTAINS |
	1 << GIT_CMD_GETCONFLICTINFO
);



struct CFilterData {
	enum : std::uint8_t {
		SHOW_NO_LIMIT,
		// NOTE: no limitation does not mean "without all limitations", it's just without the following limitations. That say, the log still could be limited by author, committer, etc.
		SHOW_LAST_SEL_DATE,
		SHOW_LAST_N_COMMITS,
		SHOW_LAST_N_YEARS,
		SHOW_LAST_N_MONTHS,
		SHOW_LAST_N_WEEKS,
	};



	CFilterData() {
		m_From = m_To = -1;
		m_NumberOfLogsScale = SHOW_NO_LIMIT;
		m_NumberOfLogs = 1;
	}


	DWORD m_NumberOfLogsScale;
	DWORD m_NumberOfLogs;
	__time64_t m_From;
	__time64_t m_To;
};



// ---- From str_utils (parked there unexported until git_h took them) ---------------

/**
 * Unescapes Git quoted filenames
 * This is not a full implementation of the unescaper as we skip some conversions that will result in invalid filenames.
 *
 * Restored: it had been commented out, but TortoiseGitBlame and CleanupCommand
 * call it. It returns the *wide* string, as upstream's did - the CStringA return
 * type the commented copy declared contradicted its own GetUnicode return.
 */
CStringW UnescapeGitQuotePathA(const std::string_view s) {
	const int i_size = gsl::narrow<int>(s.size());
	CStringA t;
	t.Preallocate(i_size);
	bool isEscaped = false;
	for (int i = 0; i < i_size; ++i) {
		char c = s[i];
		if (isEscaped) {
			if (c >= '0' && c <= '3') {
				if (i + 2 < i_size) {
					c = static_cast<char>((((c - '0') & 03) << 6) | (((s[i + 1] - '0') & 07) << 3) | ((s[i + 2] - '0') & 07));
					i += 2;
					t += c;
				}
			}
			else {
				// we're on purpose not supporting all possible abbreviations such as \n, \r, \t in this as these filenames are invalid on Windows anaway
				t += c;
			}
			isEscaped = false;
		}
		else {
			if (c == '\\')
				isEscaped = true;
			else if (c == '"')
				break;
			else
				t += c;
		}
	}
	return CUnicodeUtils::GetUnicode<CStringW>(t);
}


CStringW UnescapeGitQuotePath(const std::wstring_view s) {
	return UnescapeGitQuotePathA(CUnicodeUtils::StdGetUTF8(s));
}



// ---- From wil_git2 (the SmartLibgit2 bridge, retired) -----------------------------
// What the CAuto* classes did beyond owning a handle. The owners themselves are
// wil's (wil::unique_git_*); these are the parts that need CUnicodeUtils, which
// wil cannot see.

/// What CAutoRepository(std::wstring_view) / Open(std::wstring_view) did: the one
/// place a wide repository path is encoded for libgit2 - UTF-8, through
/// StdGetUTF8 - and then opened through wil::OpenGitRepository, which latches the
/// object format. A caller holding a wide path uses this, never its own narrow
/// conversion (CStringA(path) would be the ACP). Check the result with (bool) /
/// is_valid(): empty on failure, and git_error_last() says why.
wil::unique_git_repository OpenGitRepository(const std::wstring_view gitDir) {
	return wil::OpenGitRepository(CUnicodeUtils::StdGetUTF8(gitDir).c_str());
}


/// CAutoConfig's three readers, moved with their contract unchanged (an int
/// return and an in/out parameter): -1 without a config, libgit2's error
/// otherwise (GIT_ENOTFOUND for a missing key). GetConfigBOOL takes Win32's BOOL,
/// i.e. int, and like GetConfigBool leaves \a value untouched on any error, so a
/// caller's preset default survives.
int GetConfigBOOL(const git_config* const config, const std::wstring_view key, int& value) {
	if (!config)
		return -1;
	return git_config_get_bool(&value, config, CUnicodeUtils::StdGetUTF8(key).c_str());
}


int GetConfigBool(const git_config* const config, const std::wstring_view key, bool& value) {
	int out = 0;
	if (const int ret = GetConfigBOOL(config, key, out); ret < 0)
		return ret;
	value = out != 0;
	return 0;
}


/// \a value is emptied on a libgit2 error and left untouched without a config.
/// TOut is anything that assigns from a const wchar_t* - a CStringW or a std::wstring.
template <typename TOut>
int GetConfigString(const git_config* const config, const std::wstring_view key, TOut& value) {
	if (!config)
		return -1;
	wil::unique_git_buf buf;
	if (const int ret = git_config_get_string_buf(&buf, config, CUnicodeUtils::StdGetUTF8(key).c_str()); ret) {
		value = L"";
		return ret;
	}
	value = CUnicodeUtils::GetUnicode<std::wstring>(std::string_view(buf.ptr, buf.size)).c_str();
	return 0;
}


/// A CStringW key - the MFC UI's. Constrained, so a literal binds the view one.
template <tgit::str::IsATLCStringDuck TKey, typename TOut>
int GetConfigString(const git_config* const config, const TKey& key, TOut& value) {
	return GetConfigString(config, std::wstring_view(tgit::str::StringView{ key }), value);
}


/// What CAutoStrArray::AppendTo was.
void AppendTo(const git_strarray& array, std::vector<std::wstring>& list) {
	for (size_t i = 0; i < array.count; ++i)
		list.push_back(CUnicodeUtils::StdGetUnicode(array.strings[i]));
}



// ---- git_h's own ---------------------------------------------------------------


template <std::array Name, bool isDll = true>
int LogError(const CStringW& out, const std::exception& ex) {
	constexpr std::string_view prefix{ Name.data(), Name.size() - 1 };
	const auto msg = std::format(
		"[TortoiseGit] {} -> {}:\n{}\n{}",
		prefix,
		isDll ? "GitDll" : "CreateProcess",
		ex,
		out);
	OutputDebugStringA(msg.c_str());
	MessageBoxA(nullptr, msg.c_str(), "TortoiseGit", MB_OK | MB_ICONERROR);
	return GFail;
}


template <auto FuncName = __FUNCSIG__>
constexpr auto LogError(const CStringW& out, const std::exception& ex, const bool isDll = true) {
	[[maybe_unused]] const auto msg = std::format(
		"[TortoiseGit] {} -> {}:\n{}\n{}",
		FuncName,
		isDll ? "GitDll" : "CreateProcess",
		ex,
		out);
	return GFail;
}


CStringW GetLibGit2LastErr(const CStringW& msg = {}) {
	if (!msg.IsEmpty())
		return msg + L'\n' + GetLibGit2LastErr();

	if (const git_error* libgit2err = git_error_last()) {
		const auto lastError = CUnicodeUtils::GetUnicode<CStringW>(std::string_view{ libgit2err->message });
		tgit::libgit2::git_error_clear();
		return L"libgit2 returned: " + lastError;
	}
	else
		return L"An error occoured in libgit2, but no message is available.";
}


CStringW StripRefName(CStringW refName) {
	if (StringUtils::StartsWith(refName, L"refs/heads/"))
		refName = refName.Mid(static_cast<int>(wcslen(L"refs/heads/")));
	else if (StringUtils::StartsWith(refName, L"refs/"))
		refName = refName.Mid(static_cast<int>(wcslen(L"refs/")));
	return refName.TrimRight();
}


std::optional<std::wstring> LoadTextFile(const fs::path& filename) {
	if (!fs::exists(filename))
		return std::nullopt;

	const wil::unique_file pFile{ _wfsopen(filename.c_str(), L"rb", SH_DENYWR) };
	if (!pFile) {
		::MessageBox(nullptr, wstr::CString(L"Could not open ") + filename.c_str(), L"TortoiseGit", MB_ICONERROR);
		return {}; // load no further files
	}

	// Bytes, through the handle that holds the SH_DENYWR lock. The file is UTF-8;
	// a wifstream in the default locale widened it byte by byte.
	std::string bytes;
	std::array<char, 4096> buf{};
	for (size_t n; (n = std::fread(buf.data(), 1, buf.size(), pFile.get())) > 0;)
		bytes.append(buf.data(), n);

	auto str = CUnicodeUtils::GetUnicode<std::wstring>(std::string_view{ bytes });
	wstr::Replace(str, L"\r\n", L"\n");
	// Exactly one trailing newline. rfind(L'\n') cut at the last newline anywhere,
	// so a file without a trailing one lost its last line.
	str.erase(str.find_last_not_of(L'\n') + 1);
	str += L'\n';

	return str;
}


bool LoadTextFile(const CStringW& filename, CStringW& msg) {
	auto&& ret = LoadTextFile(fs::path{ StringView{ filename }.as_view() });
	if (!ret)
		return false;
	// Appends: callers assemble one message from several files.
	msg.Append(ret->data(), gsl::narrow<int>(ret->size()));
	return true;
}


// similar code in CTGitPath::ParserFromLsFile
int ParseConflictHashesFromLsFile(const std::string& out,
                                  CGitHash& baseHash,
                                  bool& baseIsFile,
                                  CGitHash& mineHash,
                                  bool& mineIsFile,
                                  CGitHash& remoteHash,
                                  bool& remoteIsFile) {
	size_t pos = 0;
	const size_t end = out.size();
	if (end == 0)
		return 1;
	while (pos < end) {
		const size_t lineStart = pos;

		if (out[pos] != 'M') {
			_ASSERTE(false && "this should never happen as this method should only be called for output of git ls-files -u -t -z");
			return -1;
		}

		pos = out.find(' ', pos); // advance to mode
		if (pos == std::string::npos)
			return -1;

		const size_t modeStart = pos + 1;
		pos = out.find(' ', modeStart); // advance to hash
		if (pos == std::string::npos)
			return -1;

		const size_t hashStart = pos + 1;
		pos = out.find(' ', hashStart); // advance to Stage
		if (pos == std::string::npos)
			return -1;

		const size_t stageStart = pos + 1;
		pos = out.find('\t', stageStart); // advance to filename
		if (pos == std::string::npos || stageStart - 1 - hashStart >= INT_MAX)
			return -1;

		++pos;
		const size_t fileNameEnd = out.find('\0', pos);
		if (fileNameEnd == std::string::npos || fileNameEnd == pos || pos - lineStart != 52)
			return -1;

		std::string_view hash{ &out[hashStart], stageStart - 1 - hashStart };
		const long mode = strtol(&out[modeStart], nullptr, 10);
		const int stage = strtol(&out[stageStart], nullptr, 10);
		if (stage == 0)
			return -1;
		else if (stage == 1) {
			baseHash = CGitHash::FromHexStr(hash);
			baseIsFile = mode != 160000;
		}
		else if (stage == 2) {
			mineHash = CGitHash::FromHexStr(hash);
			mineIsFile = mode != 160000;
		}
		else if (stage == 3) {
			remoteHash = CGitHash::FromHexStr(hash);
			remoteIsFile = mode != 160000;
		}

		pos = tgit::zstr::FindNextString(out, pos);
	}

	return 0;
}


constexpr std::int64_t filetime_to_time_t(std::int64_t winTime) noexcept {
	winTime -= 116444736000000000LL; /* Windows to Unix Epoch conversion */
	winTime /= 10000000; /* Nano to seconds resolution */
	return winTime;
}


// lstat semantics, as git means them - see PathUtils::get_path_attributes
int GetFileModifyTime(const fs::path& filename, std::int64_t* time, bool* isDir = nullptr, std::int64_t* size = nullptr, bool* isSymlink = nullptr) {
	const auto attrs = get_path_attributes(filename);
	if (!attrs || !attrs->exists())
		return GFail;

	if (time)
		*time = attrs->lastWriteTime;
	if (size)
		*size = attrs->size;
	if (isDir)
		*isDir = attrs->isDirectory;
	if (isSymlink)
		*isSymlink = attrs->type == std::filesystem::file_type::symlink; // a junction is not a symlink
	return G_OK;
}


// legacy signature for raw wide-string callers
int GetFileModifyTime(const LPCWSTR filename, std::int64_t* time, bool* isDir = nullptr, std::int64_t* size = nullptr, bool* isSymlink = nullptr) {
	if (!filename) // path(nullptr) is UB; GetFileAttributesEx(nullptr) merely failed
		return GFail;
	return GetFileModifyTime(fs::path(filename), time, isDir, size, isSymlink);
}



namespace details {
	// the unit tests pin these three in their fixture, after construction
	constexpr auto sw_Explorer = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";

	const bool g_bSortLogical = !wil::reg::get_bool(HKEY_CURRENT_USER, sw_Explorer, L"NoStrCmpLogical", false) &&
		!wil::reg::get_bool(HKEY_LOCAL_MACHINE, sw_Explorer, L"NoStrCmpLogical", false);
	const bool g_bSortLocalBranchesFirst = !wil::reg::get_bool(HKEY_CURRENT_USER, Registry::TortoiseGitKey, L"NoSortLocalBranchesFirst", false) &&
		!wil::reg::get_bool(HKEY_LOCAL_MACHINE, Registry::TortoiseGitKey, L"NoSortLocalBranchesFirst", false);

	const bool g_bSortTagsReversed = wil::reg::get_bool(HKEY_CURRENT_USER, Registry::TortoiseGitKey, L"SortTagsReversed", false);

	int LogicalCompareSz(const LPCWSTR left, const LPCWSTR right) {
		if (g_bSortLogical)
			return StrCmpLogicalW(left, right);
		return StrCmpIW(left, right);
	}

	/// A sort predicate, so bool and "less": returning LogicalCompareSz's int read
	/// as "differs", which the debug STL rejects as an invalid comparator.
	bool LogicalCompareBranchesPredicate(const std::wstring_view left, const std::wstring_view right) {
		if (g_bSortLocalBranchesFirst) {
			const bool leftIsRemote = left.starts_with(L"remotes/");
			const bool rightIsRemote = right.starts_with(L"remotes/");

			if (leftIsRemote && !rightIsRemote)
				return false;
			else if (!leftIsRemote && rightIsRemote)
				return true;
		}
		return LogicalCompareSz(left.data(), right.data()) < 0;
	}

	struct CustomPred
	{
		const bool Reverse = false;

		bool operator()(const std::wstring_view left, const std::wstring_view right) const {
			// read per call: g_bSortLogical is the user's Explorer setting, which a
			// constexpr member defaulting to false had silently overridden
			const auto comperator = g_bSortLogical ? &StrCmpLogicalW : &StrCmpIW;
			if (!Reverse)
				return comperator(left.data(), right.data()) < 0;
			else
				return comperator(left.data(), right.data()) > 0;
		}
	};

	static_assert(std::is_class_v<CustomPred>, "CustomPred");

	constexpr auto LogicalComparePredicate = CustomPred{};

	constexpr auto LogicalCompareReversedPredicate = CustomPred{true};


   /*
	* SerializeArgv deliberately does not rewrite argument content, so a call site
	* that hands git a Windows path has to say so. Both callers of the two
	* functions below pass a temp file path with backslashes, and under
	* msys2/cygwin git those do not resolve - which is what the old non-relaxed
	* QuoteParameter's \ -> / conversion was quietly doing for them.
	*/
	// TODO: get rid of this
	std::wstring AsGitPath(const CStringW& path) {
		std::wstring gitPath(path);
		std::ranges::replace(gitPath, L'\\', L'/');
		return gitPath;
	}
}



void UnifiedDiffStatToStringA(const git_buf* text, void* payload) {
	_ASSERTE(payload && text);
	const auto str = static_cast<CStringA*>(payload);
	str->Append(text->ptr, CUnicodeUtils::SafeSizeToInt(text->size));
	str->AppendChar('\n');
}


int UnifiedDiffToStringA(const git_diff_delta* /*delta*/, const git_diff_hunk* /*hunk*/, const git_diff_line* line, void* payload) {
	_ASSERTE(payload && line);
	const auto str = static_cast<CStringA*>(payload);
	if (line->origin == GIT_DIFF_LINE_CONTEXT || line->origin == GIT_DIFF_LINE_ADDITION || line->origin == GIT_DIFF_LINE_DELETION)
		str->Append(&line->origin, 1);
	str->Append(line->content, CUnicodeUtils::SafeSizeToInt(line->content_len));
	return 0;
}


fs::path GetHomeDirectory() {
	auto homeDirectory = Environment::g_Environment.GetEnv(L"HOME");
	if (!homeDirectory.empty())
		return homeDirectory;

	if (auto tmp = Environment::g_Environment.GetEnv(L"HOMEDRIVE"); !tmp.empty()) {
		if (const auto tmp2 = Environment::g_Environment.GetEnv(L"HOMEPATH"); !tmp2.empty()) {
			tmp += tmp2;
			if (CStringW windowsSysDirectory; GetSystemDirectory(CStrBufW(windowsSysDirectory, 4096), 4096) != FALSE)
				if (concepts::as_view(windowsSysDirectory) != tmp && fs::is_directory(tmp)) {
					return tmp;
				}
		}
	}

	if (const auto tmp = Environment::g_Environment.GetEnv(L"USERPROFILE"); !tmp.empty()) {
		return tmp;
	}

	return {};
}


fs::path GetGitGlobalConfig() {
	return GetHomeDirectory() / L".gitconfig";
}



fs::path GetGitLocalConfig() {
	const auto adminDir = GitAdminDir::GetAdminDirPath(m_CurrentDirStorage.native());
	return (adminDir.value_or(GitAdminDir::AdminDir{}).path + L"config").c_str();
}


fs::path GetGitGlobalXDGConfig() {
	// TODO: Attention: also see GitWCRev/status.cpp!
	const fs::path xdgPath = Environment::g_Environment.GetEnv(L"XDG_CONFIG_HOME");
	const fs::path appData = Environment::g_Environment.GetEnv(L"APPDATA");
	if (!xdgPath.empty()) {
		return xdgPath / L"git" / L"config";
	}

	if (!appData.empty() && !ms_bCygwinGit && !ms_bMsys2Git) {
		auto appDataPath = fs::path{appData} / L"Git" / L"config";
		if (fs::exists(appDataPath)) {
			return appDataPath;
		}
	}

	auto xdgInHome = GetHomeDirectory() / L".config" / L"git" / L"config";
	return xdgInHome;
}


fs::path GetGitSystemConfig() {
	return wget_msysgit_etc(Environment::g_Environment.put());
}



int ms_LastMsysGitVersion = 0;



class CGit {
	GIT_DIFF m_GitDiff{};
	GIT_DIFF m_GitSimpleListDiff{};

	/*
	 * The single config reader. \a canonicalizeBool asks git.exe for --bool, which
	 * is what makes a valueless key ("[section]\nkey" with no =) read as true; the
	 * gitdll path ignores it and leaves the parsing to the caller.
	 *
	 * It is a *type* request, so it does not belong on the untyped public getter -
	 * GetConfigValueBool is its only user.
	 */
	std::wstring ReadConfigValue(const CStringW& name, const CStringW& def, bool canonicalizeBool);

public:

	// Public for the unit tests, which force a lazy re-init between fixtures. It used
	// to be public only where gtest was included first; a module is compiled once, so
	// access can no longer depend on the importer.
	static inline git_credential_acquire_cb g_Git2CredCallback = nullptr;
	static inline git_transport_certificate_check_cb g_Git2CheckCertificateCallback = nullptr;

	bool m_IsGitDllInited = false;
	// Directory g_gitObjectFormat was latched from. The latched format only describes
	// the repository at m_CurrentDir, so the two have to move together; keeping the
	// directory here lets CheckAndInitDll() notice when they have drifted apart.
	fs::path m_formatLatchedFor{};
	std::recursive_mutex m_critGitDllSec{}; // see TGitH::CAutoLocker
	bool m_IsUseGitDLL{};
	bool m_IsUseLibGit2{};
	DWORD m_IsUseLibGit2_mask{};

	bool m_bInitialized = false;

	fs::path& m_CurrentDir;

	CGit() : m_CurrentDir(GitCall::m_CurrentDirStorage) {
		git_libgit2_init();
		// Seeded from the process's own working directory, which is emphatically not guaranteed to be a working copy root - every caller that cares sets one.
		// This is the third writer of the directory and the only one a grep for "m_CurrentDir =" could never find, which is the argument for making the member
		// const rather than merely documenting that it should not be assigned. It writes the storage rather than a setter because CStrBuf fills a buffer
		// inplace, and because this write is not choosing a repository.
		m_CurrentDirStorage = fs::current_path();
		m_IsUseGitDLL = wil::reg::get_bool(HKEY_CURRENT_USER, Registry::TortoiseGitKey, L"UsingGitDLL", false);
		m_IsUseLibGit2 = wil::reg::get_bool(HKEY_CURRENT_USER, Registry::TortoiseGitKey, L"UseLibgit2", true);
		m_IsUseLibGit2_mask = wil::reg::try_get_dword(HKEY_CURRENT_USER, Registry::TortoiseGitKey, L"UseLibgit2_mask").value_or(DEFAULT_USE_LIBGIT2_MASK);

		CheckMsysGitDir();
	}


	~CGit() {
		if (this->m_GitDiff) {
			git_close_diff(m_GitDiff);
			m_GitDiff = nullptr;
		}
		if (this->m_GitSimpleListDiff) {
			git_close_diff(m_GitSimpleListDiff);
			m_GitSimpleListDiff = nullptr;
		}
		git_libgit2_shutdown();
	}


	/// the most common signature
	static int Run(const wil::ArgvData& argv, std::string* out = nullptr, std::string* err = nullptr) {
		CGitCall call{ argv.SerializeArgv(has_weird_paths()), out, err };
		return GitCall::Run(call);
	}

	static int Run(const wil::ArgvData& argv, wstr::CString* out = nullptr, wstr::CString* err = nullptr) { // NOLINT(*-non-const-parameter)
		auto sOut = std::string{};
		auto sErr = std::string{};
		CGitCall call{ argv.SerializeArgv(has_weird_paths()), &sOut, &sErr };
		auto&& ret = GitCall::Run(call);
		if (out)
			*out = CUnicodeUtils::GetUnicode<wstr::CString>(sOut);
		if (err)
			*err = CUnicodeUtils::GetUnicode<wstr::CString>(sErr);
		return ret;
	}


	void ForceReInitDll() {
#ifdef TGITCACHE
		std::unreachable();
#endif
		m_IsGitDllInited = false;
		CheckAndInitDll();
	}


	void CheckAndInitDll() {
#ifdef TGITCACHE
		std::unreachable();
#endif
		// Re-init when the working copy changed, not only when the dll was never
		// initialized: g_gitObjectFormat describes the repository we latched it from,
		// so a stale latch would size object ids for the *previous* repository and
		// silently truncate or pad them. There is no valid state in which
		// m_CurrentDir and the latched format disagree.
		if (!m_IsGitDllInited || m_formatLatchedFor != m_CurrentDir) {
			git_init(Environment::g_Environment.get());
			// latch the repository's object format for the whole process, cf. GitHash.h
			const auto isSHA1 = static_cast<git_oid_t>(git_get_hash_algo()) == GIT_OID_SHA1;
			GitFormatHolder::set(isSHA1 ? GIT_OID_SHA1 : GIT_OID_SHA256);
			m_formatLatchedFor = m_CurrentDir;
			m_IsGitDllInited = true;
		}
	}


	static int git_open_diff_ex(GIT_DIFF* diff, const wil::ArgvData& argv) {
		auto&& temp = argv.utf8();
		return git_open_diff(diff, temp.argc(), temp.argv());
	}


	static int git_open_log_ex(GIT_DIFF* m_DllGitLog, wil::ArgvData& argv) {
		auto&& temp = argv.utf8();
		return git_open_log(m_DllGitLog, temp.argc(), temp.argv());
	}


	GIT_DIFF GetGitDiff() {
#ifdef TGITCACHE
		std::unreachable();
#endif
		if (m_GitDiff)
			return m_GitDiff;

		// cf. GitRevLoglist::SafeFetchFullInfo
		static const std::string cParameter = std::format("-C{}%", ms_iSimilarityIndexThreshold);
		static const std::string mParameter = std::format("-M{}%", ms_iSimilarityIndexThreshold);
		const wil::ArgvData argv = { ""s, cParameter, mParameter, "-r"s };
		git_open_diff_ex(&m_GitDiff, argv);
		return m_GitDiff;
	}


	GIT_DIFF GetGitSimpleListDiff() {
#ifdef TGITCACHE
		std::unreachable();
#endif
		if (m_GitSimpleListDiff)
			return m_GitSimpleListDiff;
		const wil::ArgvData argv = { "", "-r" };
		git_open_diff_ex(&m_GitSimpleListDiff, argv);
		return m_GitSimpleListDiff;
	}


	bool CheckMsysGitDir(bool bFallback = TRUE);

	static_assert(LIBGIT2_CMD::LAST_VALUE < sizeof(DWORD) * 8, "too many flags for storing them in a DWORD bitfield");
	bool UsingLibGit2(const LIBGIT2_CMD cmd) const { return (1 << cmd) & LIBGIT2_ONLY_MASK ? true : (m_IsUseLibGit2 && (1 << cmd) & m_IsUseLibGit2_mask); }
	// callback type should be git_cred_acquire_cb
	// but it could be void* and a cast
	static void SetGit2CredentialCallback(const git_credential_acquire_cb& callback) { g_Git2CredCallback = callback; }
	static void SetGit2CertificateCheckCertificate(const git_transport_certificate_check_cb& callback) { g_Git2CheckCertificateCallback = callback; }

	wil::unique_git_repository GetGitRepository() const {
		// Through wil::OpenGitRepository, the chokepoint that latches the object format.
		return wil::OpenGitRepository(TGitPath::ToGitPathA(m_CurrentDir.native()).c_str());
	}

	/**
	 * The configured SSH client, HKCU then HKLM, empty when none is set and the OpenSSH
	 * default applies. A stale path to the retired TortoiseGitPlink.exe reads as unset,
	 * so upgrades do not keep exporting a binary that no longer exists.
	 */
	[[nodiscard]] CStringW GetNotesRef() const {
		return CUnicodeUtils::GetUnicode<CStringW>(std::string_view{git_default_notes_ref()});
	}
	bool IsLocalBranch(const CStringW& shortName) const {
		std::vector<std::wstring> list;
		GetBranchList(list, nullptr, TGitH::BRANCH_LOCAL);
		return std::find(list.cbegin(), list.cend(), StringView(shortName)) != list.cend();
	}
	bool IsBranchTagNameUnique(const std::wstring& name) const;
	/**
	 * Checks if a branch or tag with the given name exists
	 *isBranch is true -> branch, tag otherwise
	 */
	bool BranchTagExists(const CStringW& name, bool isBranch = true) const;

	/* m_CurrentDir is a reference into *this, so an implicit copy would bind the
	 * copy's member to the *source* object's storage and the two would silently
	 * share a working directory. Nothing copies CGit today - g_Git and the handful
	 * of `CGit subgit;` locals are all default-constructed - so deleting these
	 * costs nothing and closes the hole before someone opens it.
	 */
	CGit(const CGit&) = delete;
	CGit& operator=(const CGit&) = delete;


	bool IsFastForward(const CStringW& from, const CStringW& to, CGitHash* commonAncestor = nullptr) const;
	/// A path in git's spelling (forward slashes) as UTF-8, for libgit2 calls. Restored:
	/// it was a CGit static with 77 callers in TortoiseProc.
	[[nodiscard]] static CStringA GetGitPathStringA(const CStringW& path) {
		return CUnicodeUtils::GetUTF8<CStringA>(CTGitPath(path).GetGitPathString());
	}

	/**
	 * A git config value as text, or \a def when the key is unset.
	 *
	 * Prefer GetConfigValueBool/GetConfigValueInt32 whenever the key has a type.
	 * They parse with git's own parser, so "yes", "on", "1" and "2k" mean here
	 * exactly what they mean to git.exe, and cannot drift from it the way an
	 * _wtoi() or an == L"true" chain at the call site does.
	 *
	 * What legitimately stays text, so this does not get "cleaned up" again:
	 *   - free text - user.name, remote.*.url, commit.template
	 *   - enum-valued keys - `core.cleanup`, remote.*.tagopt, push.recurseSubmodules
	 *   - tri-state settings pages, where *unset* is a third state that a bool
	 *     cannot carry. `remote.<name>.prune` renders as true / false / inherited,
	 *     and GetConfigValueBool would fold the third case into the default. That
	 *     is the reason an untyped reader has to exist at all.
	 */
	CStringW GetConfigValue(const CStringW& name, const CStringW& def = CStringW());
	bool GetConfigValueBool(const CStringW& name, bool def = false);
	int GetConfigValueInt32(const CStringW& name, int def = 0);

	// One body for set and unset, reporting its *caller* - the two things the
	// template <auto FuncName = __FUNCSIG__> draft was after. The caller comes from
	// std::source_location as a defaulted function argument, which is evaluated at
	// the call site; a string literal cannot be a template argument, so the
	// __FUNCSIG__ default made every call fail to deduce. (The draft also passed
	// nullptr as the value to gitdll for set as well as unset, and wide keys to
	// git_set_config's const char*.)
	int SetConfigValue(const CStringW& key,
	                   const CStringW& value,
	                   const CONFIG_TYPE type = CONFIG_LOCAL,
	                   const std::source_location where = std::source_location::current()) {
		return WriteConfigValue(key, &value, type, where);
	}


	int UnsetConfigValue(const CStringW& key, const CONFIG_TYPE type = CONFIG_LOCAL, const std::source_location where = std::source_location::current()) {
		return WriteConfigValue(key, nullptr, type, where);
	}


	/// The shared body: a null \a value unsets the key. 0 on success, -1 on failure.
	int WriteConfigValue(const CStringW& key, const CStringW* value, const CONFIG_TYPE type, const std::source_location& where) {
		if (m_IsUseGitDLL) {
			[[maybe_unused]] CAutoLocker lock(m_critGitDllSec);
			try {
				CheckAndInitDll();
			} catch (...) {}
			const auto keya = CUnicodeUtils::GetUTF8<CStringA>(key);
			const auto valuea = value ? CUnicodeUtils::GetUTF8<CStringA>(*value) : CStringA();
			try {
				return git_set_config(keya, value ? static_cast<const char*>(valuea) : nullptr, type);
			} catch (const std::exception& ex) {
				TracePrintf(L"%S: could not %s config %s: %S\n", where.function_name(), value ? L"set" : L"unset", key, ex.what());
				return -1;
			}
		}

		wil::ArgvData argv{ L"git.exe", L"config" };
		// CONFIG_LOCAL (and XDG) pass no scope flag, as before: an empty argv element
		// would reach git as a real empty argument.
		if (type == CONFIG_GLOBAL)
			argv << L"--global";
		else if (type == CONFIG_SYSTEM)
			argv << L"--system";
		if (!value)
			argv << L"--unset";
		argv << L"--end-of-options";
		argv << StringView{ key };
		if (value)
			argv << StringView{ *value };

		std::wstring out;
		std::wstring err;
		auto&& ret = GitCall::Run(argv, &out, &err);
		Trace(L"{}({}): git config for {}: {}\n", til::UTF8ToWString(where.file_name()), where.line(), key, out);
		if (ret) {
			Trace(L"{}: git config for {} failed:\n\t{}", til::UTF8ToWString(where.function_name()), key, err);
			return GFail;
		}
		return G_OK;
	}


	CStringW GetUserNameW();
	CStringW GetUserEmail();
	CStringW GetCommitterName();
	CStringW GetCommitterEmail();
	CStringW GetCurrentBranch(bool fallback = false) const;
	void GetRemoteTrackedBranch(const CStringW& localBranch, CStringW& remote, CStringW& branch);
	void GetRemoteTrackedBranchForHEAD(CStringW& remote, CStringW& branch);
	void GetRemotePushBranch(const CStringW& localBranch, CStringW& pushRemote, CStringW& pushBranch);
	// read current branch name from HEAD file, returns 0 on success, -1 on failure, 1 detached (branch name "HEAD" returned)
	static int GetCurrentBranchFromFile(const fs::path& sProjectRoot, CStringW& sBranchOut, bool fallback = false);
	/**
	Use this method only when the HEAD is exist.
	*/
	BOOL CheckCleanWorkTree(bool stagedOk = false) const;
	BOOL IsResultingCommitBecomeEmpty(bool amend = false);
	int DeleteRef(const CStringW& reference) const;


	/**
	 * Adopts the working copy that *contains* \a path: walks up looking for the
	 * admin directory and latches the root it finds. Returns false when there is
	 * none, in which case the current directory is left as whatever HasAdminDir
	 * wrote (historically: emptied).
	 *
	 * Use SetCurrentDirExact() when the caller already knows the root. That is not
	 * a stylistic split - it is why the ~30 direct assignments this replaced could
	 * not simply be rewritten to call this one. They hand over a root they have
	 * already established (a submodule path, a clone target, a CombinePath result)
	 * and a re-discovery from there would be at best wasted work and at worst a
	 * walk up into the *super*project.
	 */
	bool SetCurrentDir(const CStringW& path_s, const bool submodule = false) {
		const auto path = fs::path{ path_s.GetString() };
		const auto topDir = GitAdminDir::HasAdminDir(path);
		m_CurrentDirStorage = topDir;
		if (topDir.empty() && GitAdminDir::IsBareRepo(path)) {
			m_CurrentDirStorage = path;
		}
		return !topDir.empty();
	}


	/**
	Use this method only if m_IsUseLibGit2 is used for fallbacks.
	If you directly use libgit2 methods, use GetLibGit2LastErr instead.
	*/
	CStringW GetGitLastErr(const CStringW& msg, const std::optional<LIBGIT2_CMD> cmd = std::nullopt) const {
		const bool using_lib_git2 = cmd.has_value() ? UsingLibGit2(cmd.value()) : this->m_IsUseLibGit2;
		if (using_lib_git2) {
			return TGitH::GetLibGit2LastErr(msg);
		} else if (gitLastErr.empty()) {
			return msg + L"\nUnknown git.exe error.";
		} else {
			gitLastErr.clear();
			return msg + L'\n' + gitLastErr.c_str();
		}
	}


	/**
	 * Adopts \a path as the working copy root verbatim, with no discovery - the
	 * caller asserts it already is one.
	 *
	 * This is what every direct `m_CurrentDir = ...` used to be. Naming it does two
	 * things a raw assignment could not: it distinguishes the two intents above at
	 * the call site, and it makes the writes enumerable, which is the prerequisite
	 * for ever latching the object format here (see the open risk in CLAUDE.md -
	 * today the latch self-corrects on the next CheckAndInitDll()).
	 */
	template<str::IsTCompString TStr>
	void SetCurrentDirExact(const TStr& path) { m_CurrentDirStorage = fs::path{StringView{ path }.as_view()}; }

	void SetCurrentDirExact(const fs::path& path) { m_CurrentDirStorage = path; }

	/*
	 * Root of the working copy this process operates on. Equivalent to reading
	 * m_CurrentDir; kept as the spelling new code should use, and now returning a
	 * reference because the value can no longer be repointed by an assignment
	 * somewhere else in the expression.
	 */
	[[nodiscard]] const auto& GetCurrentDir() const { return m_CurrentDir; }


	/*
	 * Reads exactly like the public member it replaces - `GitCall::m_CurrentDirStorage` still
	 * compiles verbatim everywhere - but it is a reference to const, so every
	 * *write* is now a compile error that has to pick one of the two setters above.
	 *
	 * That gate is the whole point. The latched object format (GitObjectFormat.h)
	 * describes the repository this directory names, so a write that bypasses the
	 * setters can leave the two describing different repositories; the invalid state
	 * used to be merely representable, and now it has exactly two authors.
	 *
	 * Spelled as a reference to a private member rather than as a `const CStringW`
	 * written through a const_cast in the setters. The enforcement and the churn
	 * are identical, but modifying an object that was *declared* const is undefined
	 * behaviour rather than a trick - the optimizer is entitled to fold reads of it,
	 * and would be within its rights to serve a stale directory.
	 */

	int GetRemoteList(std::vector<std::wstring>& list) const;
	int GetBranchList(std::vector<std::wstring>& list, int* current, BRANCH_TYPE type = BRANCH_LOCAL, bool skipCurrent = false) const;
	int GetTagList(std::vector<std::wstring>& list) const;
	int GetRefsCommitIsOn(std::vector<std::wstring>& list, const CGitHash& hash, bool includeTags, bool includeBranches, BRANCH_TYPE type = BRANCH_LOCAL) const;
	int GetRemoteRefs(const CStringW& remote, REF_VECTOR& list, bool includeTags, bool includeBranches) const;
	int DeleteRemoteRefs(const CStringW& remote, const std::vector<std::wstring>& list) const;
	int GetBranchDescriptions(dict_t& map) const;
	int GuessRefForHash(CStringW& ref, const CGitHash& hash) const;
	int git_show_ref(MAP_HASH_NAME& map) const {
		std::wstring out;
		auto&& ret = GitCall::RunText({ L"git.exe", L"show-ref", L"-d" }, &out, &gitLastErr);
		for (const auto& [lineA, _] : str::lazy_tokenize_lines(out)) {
			const size_t start = lineA.find(' ');
			if (start != 2 * GitFormatHolder::GitHashSize() || start >= lineA.size())
				continue;

			const CGitHash hash = CGitHash::FromHexStr(lineA.substr(0, start));
			map[hash].emplace_back(lineA.substr(start + 1));
		}
		return ret;
	}
	int GetMapHashToFriendName(MAP_HASH_NAME& map) const;

	[[nodiscard]] CStringW DerefFetchHead() const;

	// FixBranchName():
	// When branchName == FETCH_HEAD, dereference it.
	// A selected branch name got from GetBranchList(), with flag BRANCH_FETCH_HEAD enabled,
	// should go through this function before it is used.
	CStringW FixBranchName(const CStringW& branchName) const {
		CStringW tempBranchName = branchName;
		if (branchName == L"FETCH_HEAD")
			tempBranchName = DerefFetchHead();
		return tempBranchName;
	}

	static wil::ArgvData GetLogCmd(CStringW range, const TGitPath::CTGitPath* path, int InfoMask, const CFilterData* filter, int logOrderBy);
	int GetHash(CGitHash& hash, const std::wstring& friendname) const;
	/// A CStringW - the MFC UI's. Constrained, so a literal still binds the std one.
	template <tgit::str::IsATLCStringDuck TStr>
	int GetHash(CGitHash& hash, const TStr& friendname) { return GetHash(hash, std::wstring(tgit::str::StringView{ friendname })); }


	static int GetHash(git_repository* repo, CGitHash& hash, std::wstring_view friendname, bool skipFastCheck = false);
	/// A CStringW ref - the MFC UI's. Constrained, so a literal binds the view one.
	template <tgit::str::IsATLCStringDuck TStr>
	static int GetHash(git_repository* repo, CGitHash& hash, const TStr& friendname, const bool skipFastCheck = false) {
		return GetHash(repo, hash, std::wstring_view(tgit::str::StringView{ friendname }), skipFastCheck);
	}


	int GetSubmoduleHash(const CStringW& pathOfSubmodule, const CGitHash& revision, CGitHash& submoduleRevision, CStringW& err) const;

	int IsUnbornBranch(const CStringW& ref) const;
	/**
	Checks if HEAD points to an unborn branch
	This method assumes, that we already know that we are in a working tree.
	the formal argument is just to hold the res of GetHash;
	*/
	bool IsInitRepos(CGitHash hash = {}) const { return GetHash(hash, L"HEAD") != 0 ? false : hash.IsEmpty(); }
	// Returns 0 if no conflict, if a conflict was found and -1 in case of a failure
	int HasWorkingTreeConflicts(git_repository* repo = nullptr) const;
	void GetBisectTerms(CStringW* good, CStringW* bad) const;
	int GetRefList(std::vector<std::wstring>& list) const;



	class SubmoduleInfo {
	public:
		CGitHash superProjectHash{};
		CGitHash mergeconflictMineHash{};
		CGitHash mergeconflictTheirsHash{};
		CStringW mineLabel{};
		CStringW theirsLabel{};


		[[nodiscard]] bool AnyMatches(const CGitHash& hash) const {
			return !superProjectHash.IsEmpty() && superProjectHash == hash || !mergeconflictMineHash.IsEmpty() && mergeconflictMineHash == hash || !
			mergeconflictTheirsHash.IsEmpty() && mergeconflictTheirsHash == hash;
		}


		void Empty() {
			superProjectHash.Empty();
			mergeconflictMineHash.Empty();
			mergeconflictTheirsHash.Empty();
		}
	};



	int RefreshGitIndex() {
		// HACK: don't use internal update-index if we have a git-lfs enabled repository as the libgit version fails when executing the filter, issue #3220
		if (this->m_IsUseGitDLL && !TGitPath::CTGitPath{this->m_CurrentDir}.HasLFS()) {
			CAutoLocker _(this->m_critGitDllSec);
			try {
				this->CheckAndInitDll();

				[[maybe_unused]] const auto cleanup = gsl::finally([] { git_exit_cleanup(); });
				return git_update_index();
			} catch (...) {
				return GFail;
			}
		} else {
			return GitCall::Run({ L"git.exe", L"update-index", L"--refresh" });
		}
	}
	int GetOneFile(const CStringW& Refname, const CTGitPath& path, const CStringW& outputfile) const;

	// Example: master -> refs/heads/master
	CStringW GetFullRefName(const CStringW& shortRefName);
	std::wstring GetFullRefName(const std::wstring& shortRefName);
	// Removes 'refs/heads/' or just 'refs'. Example: refs/heads/master -> master

	int GetCommitDiffList(const CStringW& rev1,
	                      const CStringW& rev2,
	                      CTGitPathList& outpathlist,
	                      CStringW& error,
	                      bool ignoreSpaceAtEol = false,
	                      bool ignoreSpaceChange = false,
	                      bool ignoreAllSpace = false,
	                      bool ignoreBlankLines = false);
	int GetInitAddList(CTGitPathList& outputlist, bool getStagingStatus = false);
	int GetWorkingTreeChanges(CTGitPathList& result,
	                          bool amend = false,
	                          const CTGitPathList* filterlist = nullptr,
	                          bool includedStaged = false,
	                          bool getStagingStatus = false);
	[[nodiscard]] static int GetShortHASHLength() { return 8; }

	int GetGitNotes(const CGitHash& hash, CStringW& notes) const;
	int SetGitNotes(const CGitHash& hash, const CStringW& notes) const;

	int GetTagInfo(const CStringW& tagName, CStringW& output, const time_formater_f& timeformatter) const;

	int GetUnifiedDiff(const CTGitPath& path,
	                   const CStringW& rev1,
	                   const CStringW& rev2,
	                   CStringW patchfile,
	                   bool bMerge,
	                   bool bCombine,
	                   int diffContext,
	                   bool bNoPrefix = false);
	int GetUnifiedDiff(const CTGitPath& path, const CStringW& rev1, const CStringW& rev2, CStringA& buffer, bool bMerge, bool bCombine, int diffContext);

	int GitRevert(int parent, const CGitHash& hash) const;

	int GetGitVersion(CStringW* versiondebug, CStringW* errStr);


	[[nodiscard]] fs::path CombinePath(const auto& path) const {
		// the directory itself for "": operator/ would append a trailing separator
		const auto sub = str::to_path(path);
		return sub.empty() ? m_CurrentDir : m_CurrentDir / sub;
	}
	[[nodiscard]] fs::path CombinePath(const CTGitPath& path) const {
		return m_CurrentDir / path.GetWString();
	}


	[[nodiscard]] static CStringW QuoteParameter(CStringW value, const bool relaxed = false) {
		if (ms_bMsys2Git || ms_bCygwinGit) {
			if (!relaxed)
				value.Replace(L'\\', L'/');
			value.Replace(L"'", L"'\\''");
			return L'\'' + value + L'\'';
		}

		if (relaxed)
			return StringUtils::EscapeWindowsCliArguments(value);

		value.Replace(L'\\', L'/');

		// Any value is quotable: a double quote is escaped (\") the way
		// CommandLineToArgvW reads it back, and ? and * are plain characters inside
		// quotes. illegal_git_parameter, which refused all three, is gone on purpose.
		// ForceQuotes keeps the old always-quoted shape ("" for an empty value);
		// FirstArgumentIsNotPath, because this one element is never argv[0].
		const auto quoted = wil::ArgvToCommandLine(std::vector{ std::wstring(value) }, wil::ArgvToCommandLineFlags::ForceQuotes | wil::ArgvToCommandLineFlags::FirstArgumentIsNotPath);
		return str::to_atl<CStringW>(quoted);
	}


	// Same reason, and the same constraint, as the CombinePath overload above.
	template <tgit::str::IsStdStringDuck TStr>
		requires std::same_as<std::remove_cvref_t<TStr>, std::wstring>
	[[nodiscard]] static CStringW QuoteParameter(const TStr& value, const bool relaxed = false) {
		using SV = str::StringViewTraits<TStr>;
		return QuoteParameter(CStringW(SV::data(value), gsl::narrow<int>(SV::size(value.size))), relaxed);
	}


	static bool IsBranchNameValid(const CStringW& branchname) {
		if (branchname.FindOneOf(L"\"|<>") >= 0) // not valid on Windows
			return false;
		int valid = 0;
		git_branch_name_is_valid(&valid, CUnicodeUtils::GetUTF8<CStringA>(branchname));
		return valid == 1;
	}
};


CGit g_Git{};



namespace details {
	int resolve_to_tree(git_repository* repo, const char* identifier, git_tree** tree) {
		_ASSERTE(repo && identifier && tree);

		/* try to resolve identifier */
		wil::unique_git_object obj;
		if (git_revparse_single(&obj, repo, identifier))
			return -1;

		if (!obj)
			return GIT_ENOTFOUND;

		int err = 0;
		switch (git_object_type(obj.get())) {
		case GIT_OBJECT_TREE:
			*tree = reinterpret_cast<git_tree*>(obj.release());
			break;
		case GIT_OBJECT_COMMIT:
			err = git_commit_tree(tree, reinterpret_cast<git_commit*>(obj.get()));
			break;
		default:
			err = GIT_ENOTFOUND;
		}

		return err;
	}


	int resolve_to_commit(git_repository* repo, const char* identifier, git_commit** commit) {
		_ASSERTE(repo && identifier && commit);

		wil::unique_git_object obj;
		if (git_revparse_single(&obj, repo, identifier))
			return -1;

		if (!obj)
			return GIT_ENOTFOUND;

		return git_object_peel(reinterpret_cast<git_object**>(commit), obj.get(), GIT_OBJECT_COMMIT);
	}


	/* the tail every diff below shares. It is not done once at the end because a
	   merge diffed with -m emits one stat block and one patch series per parent. */
	int EmitUnifiedDiff(git_diff* diff, const std::function<void(const git_buf*, void*)>& statCallback, const git_diff_line_cb& callback, void* data) {
		wil::unique_git_diff_stats stats;
		if (git_diff_get_stats(&stats, diff))
			return -1;
		wil::unique_git_buf statBuf;
		if (git_diff_stats_to_buf(&statBuf, stats.get(), GIT_DIFF_STATS_FULL, 0))
			return -1;
		statCallback(&statBuf, data);

		for (size_t i = 0; i < git_diff_num_deltas(diff); ++i) {
			wil::unique_git_patch patch;
			if (git_patch_from_diff(&patch, diff, i))
				return -1;

			if (git_patch_print(patch.get(), callback, data))
				return -1;
		}

		return 0;
	}





	void UnifiedDiffStatToFile(const git_buf* text, void* payload) {
		_ASSERTE(payload && text);
		const auto file = static_cast<FILE*>(payload);
		fwrite(text->ptr, 1, text->size, file);
		fwrite("\n", 1, 1, file);
	}


	int UnifiedDiffToFile(const git_diff_delta* /* delta */, const git_diff_hunk* /* hunk */, const git_diff_line* line, void* payload) {
		_ASSERTE(payload && line);
		const auto file = static_cast<FILE*>(payload);
		if (line->origin == GIT_DIFF_LINE_CONTEXT || line->origin == GIT_DIFF_LINE_ADDITION || line->origin == GIT_DIFF_LINE_DELETION)
			fwrite(&line->origin, 1, 1, file);
		fwrite(line->content, 1, line->content_len, file);
		return 0;
	}


	/* use libgit2 get unified diff */
	int GetUnifiedDiffLibGit2(
		const CTGitPath& path,
		const CStringW& revOld,
		const CStringW& revNew,
		std::function<void(const git_buf*, void*)> statCallback,
		const git_diff_line_cb& callback,
		void* data,
		const bool bMerge,
		const int diffContext,
		const bool bNoPrefix
	) {
		auto tree1 = CUnicodeUtils::GetUTF8<CStringA>(revNew);
		auto tree2 = CUnicodeUtils::GetUTF8<CStringA>(revOld);

		const wil::unique_git_repository repo(g_Git.GetGitRepository());
		if (!repo)
			return -1;

		const int isHeadOrphan = git_repository_head_unborn(repo.get());
		if (isHeadOrphan == 1)
			return 0;
		else if (isHeadOrphan != 0)
			return -1;

		git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
		// GetString() rather than GetBuffer(): nothing writes through the pointer, and
		// every early return below skipped the matching ReleaseBuffer anyway.
		auto pathA = CUnicodeUtils::StdGetUTF8(path.GetWString());
		auto* buf = pathA.data();
		if (!pathA.empty()) {
			opts.pathspec.strings = &buf;
			opts.pathspec.count = 1;
		}
		if (bNoPrefix) {
			opts.new_prefix = "";
			opts.old_prefix = "";
		}
		// -1 is "whatever git's default is", which GIT_DIFF_OPTIONS_INIT already set.
		if (diffContext >= 0)
			opts.context_lines = static_cast<uint32_t>(diffContext);

		if (revNew == tgit::invarients::ZeroRevSV().data() || revOld == tgit::invarients::ZeroRevSV().data()) {
			// At most one side is a tree. When it is the *new* side, the working copy is
			// the old one and the diff GitCall::Runs backwards - the `-R` of the command line
			// spelling, which this had no equivalent of and so produced the diff the
			// wrong way round. git_diff__merge swaps its two sides when GIT_DIFF_REVERSE
			// is set, so the flag composes through tree -> index -> workdir correctly.
			if (revNew != tgit::invarients::ZeroRevSV().data())
				opts.flags |= GIT_DIFF_REVERSE;

			wil::unique_git_tree t1;
			if (revNew != tgit::invarients::ZeroRevSV().data() && resolve_to_tree(repo.get(), tree1, &t1))
				return -1;

			if (revOld != tgit::invarients::ZeroRevSV().data() && resolve_to_tree(repo.get(), tree2, &t1))
				return -1;

			// One call rather than a hand-rolled tree_to_index + index_to_workdir + merge:
			// it is the same three steps, but it loads the index once and hands the same
			// one to both halves instead of letting each reload it.
			wil::unique_git_diff diff;
			if (git_diff_tree_to_workdir_with_index(&diff, repo.get(), t1.get(), &opts))
				return -1;

			return EmitUnifiedDiff(diff.get(), statCallback, callback, data);
		}

		if (tree1.IsEmpty() && tree2.IsEmpty())
			return -1;

		if (tree1.IsEmpty()) {
			tree1 = tree2;
			tree2.Empty();
		}

		wil::unique_git_tree t1;
		if (!tree1.IsEmpty() && resolve_to_tree(repo.get(), tree1, &t1))
			return -1;

		// With no old rev, `-m` means "against each parent in turn" - which is the
		// entire difference between a merge commit's diff and any other one. The
		// single tree1~1 diff below is the first parent only, so every other parent
		// used to be dropped without a word.
		wil::unique_git_commit commit;
		if (bMerge && tree2.IsEmpty() && !resolve_to_commit(repo.get(), tree1, &commit) && git_commit_parentcount(commit.get()) > 1) {
			for (unsigned int i = 0; i < git_commit_parentcount(commit.get()); ++i) {
				wil::unique_git_commit parent;
				if (git_commit_parent(&parent, commit.get(), i))
					return -1;

				wil::unique_git_tree parentTree;
				if (git_commit_tree(&parentTree, parent.get()))
					return -1;

				wil::unique_git_diff diff;
				if (git_diff_tree_to_tree(&diff, repo.get(), parentTree.get(), t1.get(), &opts))
					return -1;

				// A parent the merge result is identical to contributes nothing at all,
				// not an empty stat block - which is what git prints, and what the stat
				// callback would otherwise emit a stray newline for.
				if (!git_diff_num_deltas(diff.get()))
					continue;

				if (EmitUnifiedDiff(diff.get(), statCallback, callback, data))
					return -1;
			}

			return 0;
		}

		wil::unique_git_tree t2;
		if (tree2.IsEmpty()) {
			/* don't check return value, there are not parent commit at first commit*/
			resolve_to_tree(repo.get(), tree1 + "~1", &t2);
		}
		else if (resolve_to_tree(repo.get(), tree2, &t2))
			return -1;

		wil::unique_git_diff diff;
		if (git_diff_tree_to_tree(&diff, repo.get(), t2.get(), t1.get(), &opts))
			return -1;

		return EmitUnifiedDiff(diff.get(), statCallback, callback, data);
	}


	auto GetUnifiedDiffCmd(
		const CTGitPath& path,
        const CStringW& rev1,
        const CStringW& rev2,
        const bool bMerge,
        const bool bCombine,
        int diffContext,
        const bool bNoPrefix = false
    ) {
		wil::ArgvData argv{ L"git.exe" };
		// rev2 is tested first, so a call with *both* revs set to the working copy
		// diffs rev1 forwards. That is what the format strings did; note that
		// GetCommitDiffList above tests rev1 first and so resolves the same case the
		// other way round. Neither is obviously right, and neither is changed here.
		if (rev2 == tgit::invarients::ZeroRevSV().data() || rev1 == tgit::invarients::ZeroRevSV().data()) {
			const bool reverse = rev2 != tgit::invarients::ZeroRevSV().data();
			argv.emplace_back(L"diff");
			if (reverse)
				argv.emplace_back(L"-R");
			argv.emplace_back(L"--stat");
			if (bNoPrefix)
				argv.emplace_back(L"--no-prefix");
			argv.emplace_back(L"-p");
			argv.emplace_back(L"--end-of-options");
			argv.emplace_back(std::wstring(reverse ? rev2 : rev1));
		} else {
			argv.emplace_back(L"diff-tree");
			argv.emplace_back(L"-r");
			argv.emplace_back(L"-p");
			if (bMerge)
				argv.emplace_back(L"-m");
			if (bCombine)
				argv.emplace_back(L"-c");
			if (diffContext >= 0)
				argv.emplace_back(std::format(L"--unified={}", diffContext));
			argv.emplace_back(L"--stat");
			if (bNoPrefix)
				argv.emplace_back(L"--no-prefix");
			argv.emplace_back(L"--end-of-options");
			// An empty rev used to interpolate as nothing at all. As an argv element
			// it would be a real empty argument, so it has to be skipped instead.
			if (!rev1.IsEmpty())
				argv.emplace_back(std::wstring(rev1));
			if (!rev2.IsEmpty())
				argv.emplace_back(std::wstring(rev2));
		}

		// put the terminator anyway so others can append
		argv.emplace_back(L"--");
		if (!path.IsEmpty())
			argv.emplace_back(path.GetWString());

		return argv;
	}


	template <tgit::str::IsTCompString Str>
	/// nullopt when ref does not start with prefix. An empty short name is still a
	/// match - "refs/stash" under "refs/stash" - which an empty Str could not say.
	std::optional<Str> GetShortName(const StringView<Str> ref, const StringView<Str> prefix) {
		constexpr auto weird_tail = L"^{}"sv;
		if (ref.starts_with(prefix)) {
			auto shortname = ref.substr(prefix.length());
			if (shortname.ends_with(weird_tail))
				shortname.remove_suffix(weird_tail.length());
			// (data, length): from data() alone it read to the NUL, undoing remove_suffix
			return Str(shortname.data(), gsl::narrow<int>(shortname.size()));
		}
		return std::nullopt;
	}


	BOOL GetShortName(const CStringW& ref, CStringW& shortname, const CStringW& prefix) {
		if (const auto str = GetShortName(StringView{ ref }, StringView{ prefix })) {
			shortname = *str;
			return TRUE;
		}
		return FALSE;
	}


	CStringW GetShortName(const CStringW& str, REF_TYPE* const out_type) {
		CStringW shortname;
		REF_TYPE type = UNKNOWN;

		if (GetShortName(str, shortname, L"refs/heads/"))
			type = LOCAL_BRANCH;
		else if (GetShortName(str, shortname, L"refs/remotes/"))
			type = REMOTE_BRANCH;
		else if (StringUtils::EndsWith(str, L"^{}") && GetShortName(str, shortname, L"refs/tags/"))
			type = ANNOTATED_TAG;
		else if (GetShortName(str, shortname, L"refs/tags/"))
			type = TAG;
		else if (GetShortName(str, shortname, L"refs/stash")) {
			type = STASH;
			shortname = L"stash";
		}
		else if (GetShortName(str, shortname, L"refs/bisect/")) {
			CStringW bisectGood;
			CStringW bisectBad;
			g_Git.GetBisectTerms(&bisectGood, &bisectBad);
			wchar_t c;
			if (tgit::wstr::StartsWith(StringView(shortname), StringView(bisectGood)) && ((c = shortname.GetAt(bisectGood.GetLength())) == '-' || c == '\0')) {
				type = BISECT_GOOD;
				shortname = bisectGood;
			}

			if (tgit::wstr::StartsWith(StringView(shortname), StringView(bisectBad)) && ((c = shortname.GetAt(bisectBad.GetLength())) == '-' || c == '\0')) {
				type = BISECT_BAD;
				shortname = bisectBad;
			}

			if (StringUtils::StartsWith(shortname, L"skip") && ((c = shortname.GetAt(4)) == '-' || c == '\0')) {
				type = BISECT_SKIP;
				shortname = L"skip";
			}
		}
		else if (GetShortName(str, shortname, L"refs/notes/"))
			type = NOTES;
		else if (GetShortName(str, shortname, L"refs/"))
			type = UNKNOWN;
		else {
			type = UNKNOWN;
			shortname = str;
		}

		if (out_type)
			*out_type = type;

		return shortname;
	}


	/// Splits the next entry off a PATH-style list into \a entry: ';'-separated,
	/// and an entry that starts with '"' GitCall::Runs to the closing quote. Returns where
	/// the following entry starts, or nullptr when nothing was consumed.
	LPCWSTR nextpath(const wchar_t* path, std::wstring& entry) {
		entry.clear();
		if (!path)
			return nullptr;

		const wchar_t* base = path;
		const wchar_t term = (*path == L'"') ? *path++ : L';';

		while (*path && *path != term)
			entry += *path++;

		while (*path == term || *path == L';')
			++path;

		return (path != base) ? path : nullptr;
	}


	CStringW FindFileOnPath(const CStringW& filename, LPCWSTR env, const bool wantDirectory = false) {
		// search in all paths defined in PATH, stopping at the first empty entry
		std::wstring directory;
		while ((env = nextpath(env, directory)) != nullptr && !directory.empty()) {
			// ensure trailing slash
			if (directory.back() != L'/' && directory.back() != L'\\')
				directory += L'\\';

			std::wstring candidate = directory + filename.GetString();
			if (candidate.size() >= MAX_PATH) // PathFileExists is MAX_PATH-bound; as before, give up rather than probe a truncated path
				break;

			if (PathFileExists(candidate.c_str()))
				return (wantDirectory ? directory : candidate).c_str();
		}

		return L"";
	}


	BOOL FindGitPath() {
		size_t size;
		_wgetenv_s(&size, nullptr, 0, L"PATH");
		if (!size)
			return FALSE;

		const auto env = std::make_unique<wchar_t[]>(size);
		if (!env)
			return FALSE;
		_wgetenv_s(&size, env.get(), size, L"PATH");

		auto gitExeDirectory = StringView{ FindFileOnPath(L"git.exe", env.get(), true) };
		if (!gitExeDirectory.empty()) {
			ms_LastMsysGitDir = fs::path{ gitExeDirectory.as_view() };

			// check for (broken) scoop shim, cf. issue #4033
			if (const auto contents = LoadTextFile(ms_LastMsysGitDir / L"git.shim")) {
				tgit::DebugOutput::TracePrintf(__FUNCTION__ L": found git.shim\n");
				int start = 0;
				while (start >= 0) {
					auto line = wstr::Tokenize(*contents, L"\n", start);
					const auto separatorPos = line.find(L'=');
					if (separatorPos != line.npos && line.starts_with(L"path")) {
						auto path = line.substr(separatorPos + 1);
						if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
							path = path.substr(1, path.length() - 2);
						const auto p = fs::path(path);
						if (wstr::CompareNoCase(p.filename().wstring(), L"git.exe") != 0 && fs::exists(p))
							ms_LastMsysGitDir = p.parent_path();
						break;
					}
				}
			}

			// The string version matched suffixes with EndsWith and sliced with Left;
			// on a path the same questions are about the last one or two components.
			// A directory off PATH may carry a trailing separator, which makes
			// filename() empty, so strip it before looking.
			const auto lastDir = [](const fs::path& p) { return (p.has_filename() ? p : p.parent_path()).filename(); };
			const auto parentOf = [](const fs::path& p) { return (p.has_filename() ? p : p.parent_path()).parent_path(); };

			auto& dir = ms_LastMsysGitDir;
			if (lastDir(dir) == L"bin" && (lastDir(parentOf(dir)) == L"mingw32" || lastDir(parentOf(dir)) == L"mingw64")) {
				// prefer cmd directory as early Git for Windows 2.x releases only had this
				const auto cmdDir = parentOf(parentOf(dir)) / L"cmd";
				if (fs::exists(cmdDir / L"git.exe"))
					dir = cmdDir;
			}
			if (lastDir(dir) == L"cmd") {
				// often the msysgit\cmd folder is on the %PATH%, but
				// that git.exe does not work, so try to guess the bin folder
				const auto binDir = parentOf(dir) / L"bin";
				if (fs::exists(binDir / L"git.exe"))
					dir = binDir;
			}
			return TRUE;
		}

		return FALSE;
	}


	constexpr CStringW FindExecutableOnPath(const CStringW& executable, const LPCWSTR env) {
		CStringW filename = executable;

		if (!StringUtils::EndsWith(executable, L".exe"))
			filename += L".exe";

		if (PathFileExists(filename))
			return CStringW{ filename };

		filename = FindFileOnPath(filename, env);
		if (!filename.IsEmpty())
			return CStringW{ filename };

		return executable;
	}



	struct map_each_ref_payload {
		git_repository* repo = nullptr;
		MAP_HASH_NAME* map = nullptr;
	};



	int libgit2_addto_map_each_ref_fn(git_reference* rawref, void* payload) {
		const wil::unique_git_reference ref{ std::move(rawref) };
		const auto payloadContent = static_cast<map_each_ref_payload*>(payload);

		std::wstring str = CUnicodeUtils::StdGetUnicode(git_reference_name(ref.get()));

		wil::unique_git_object gitObject;
		if (git_revparse_single(&gitObject, payloadContent->repo, git_reference_name(ref.get())))
			return (git_reference_is_remote(ref.get()) && git_reference_type(ref.get()) == GIT_REFERENCE_SYMBOLIC) ? 0 : 1;
		// don't bail out for symbolic remote references ("git.exe show-ref -d" also doesn't complain), cf. issue #2926

		if (git_object_type(gitObject.get()) == GIT_OBJECT_TAG) {
			str += L"^{}"; // deref tag
			wil::unique_git_object derefedTag;
			if (git_tag_target(&derefedTag, reinterpret_cast<git_tag*>(gitObject.get())))
				return 1;
			gitObject.swap(derefedTag);
		}

		(*payloadContent->map)[CGitHash(git_object_id(gitObject.get()))].push_back(std::move(str));

		return 0;
	}


	int GetMapHashToFriendName(git_repository* repo, MAP_HASH_NAME& map) {
		_ASSERTE(repo);

		map_each_ref_payload payloadContent = { .repo = repo, .map = &map };

		if (git_reference_foreach(repo, libgit2_addto_map_each_ref_fn, &payloadContent))
			return -1;

		for (auto it = map.begin(); it != map.end(); ++it) {
			std::ranges::sort(it->second);
		}

		return 0;
	}


	fs::path FindGitForWindows() // not static: an internal-linkage name cannot be exported (C2294)
	{
		fs::path sMsysGitPath;

		// First non-empty InstallLocation wins: per-user install, then machine, then 32-bit machine.
		const auto candidates = std::array<std::pair<HKEY, std::wstring_view>, 3>{
			std::pair{ HKEY_CURRENT_USER, UNINSTALL_REG_SUBKEY.data() },
			std::pair{ HKEY_LOCAL_MACHINE, UNINSTALL_REG_SUBKEY.data() },
			std::pair{ HKEY_LOCAL_MACHINE, UNINSTALL_RGSK_WOW64.data() },
		};
		for (const auto& [root, key] : candidates) {
			sMsysGitPath = wil::reg::try_get_string(root, key.data(), UNINSTALL_REG_VALUE.data()).value_or(std::wstring{}).c_str();
			if (!sMsysGitPath.empty())
				break;
		}

		// Check for exsistance
		if (sMsysGitPath.empty()) {
			tgit::DebugOutput::Trace(__FUNCTION__ L": Found no git.exe\n");
			return sMsysGitPath;
		}
		if (auto with_bin = sMsysGitPath / L"bin"; fs::exists(with_bin / L"git.exe")) {
			return with_bin;
		} else {
			tgit::DebugOutput::Trace(__FUNCTION__ L": Git for Windows installation found, but git.exe not exists in {}\n", sMsysGitPath);
			return {};
		}
	}


	int FindAndSetGitExePath(const bool bFallback) {
		const fs::path str = wil::reg::try_get_string(HKEY_CURRENT_USER, Registry::TortoiseGitKey, REG_MSYSGIT_PATH).value_or({});
		if (!str.empty() && fs::exists(str / L"git.exe")) {
			ms_LastMsysGitDir = str;
			return true;
		}

		Trace(__FUNCTION__ L": git.exe not exists: {}\n", ms_LastMsysGitDir);
		if (!bFallback)
			return FALSE;

		// first, search PATH if git/bin directory is already present
		if (FindGitPath()) {
			Trace(__FUNCTION__ L": FindGitPath() => {}\n", ms_LastMsysGitDir);
			std::ignore = wil::reg::set_tgit_string(REG_MSYSGIT_PATH, ms_LastMsysGitDir.wstring());
			return true;
		}

		if (const auto ms_LastMsysGitDir = FindGitForWindows(); ms_LastMsysGitDir.empty()) {
			std::ignore = wil::reg::set_tgit_string(REG_MSYSGIT_PATH, ms_LastMsysGitDir.wstring());
			return true;
		}

		return false;
	}

	fs::path GetConfiguredSshClient() {
		const auto maybe = wil::reg::try_get_tgit_string(L"SSH");
		if (!maybe)
			return {};

		if (fs::exists(*maybe))
			return *maybe;

		// Installs from before TortoiseGitPlink was retired still point at it, and an upgrade does
		// not rewrite an existing value. Left alone it would be exported verbatim and every network
		// operation would fail to spawn. Only *our own* removed binary is cleared, and only when it
		// is really gone: TortoiseSVN ships a working TortoisePlink.exe, and someone who deliberately
		// pointed at that must keep it.
		if (wstr::SpaceshipNoCase(fs::path{*maybe}.stem().wstring(), L"tortoisegitplink") == 0) {
			std::ignore = wil::reg::delete_tgit_value(L"SSH");
		}

		return {};
	}

}



// Public API that the module conversion left inside details: CGit::GetShortName
// was a static with callers in TortoiseProc and the unit tests.
using details::GetShortName;
using details::FindGitForWindows;
using details::GetMapHashToFriendName;



// Checkes whether ref is an unborn branch
int CGit::IsUnbornBranch(const CStringW& ref) const {
	if (ref.IsEmpty() || ref == L"HEAD")
		return IsInitRepos();
	CStringW currentBranch;
	const int ret = GetCurrentBranchFromFile(GitCall::m_CurrentDirStorage, currentBranch);
	if (ret < 0)
		return ret;
	if (ref == currentBranch || currentBranch == StripRefName(ref))
		return IsInitRepos();
	return false;
}


CStringW CGit::GetUserNameW() {
	const CEnvironment env;
	env.CopyProcessEnvironment();
	CStringW envname = env.GetEnv(L"GIT_AUTHOR_NAME").c_str();
	if (!envname.IsEmpty())
		return envname;

	if (auto authorname = GetConfigValue(L"author.name"); !authorname.IsEmpty())
		return authorname;

	return GetConfigValue(L"user.name");
}


CStringW CGit::GetUserEmail() {
	const CEnvironment env;
	env.CopyProcessEnvironment();
	CStringW envmail = env.GetEnv(L"GIT_AUTHOR_EMAIL").c_str();
	if (!envmail.IsEmpty())
		return envmail;

	if (auto authormail = GetConfigValue(L"author.email"); !authormail.IsEmpty())
		return authormail;

	return GetConfigValue(L"user.email");
}


CStringW CGit::GetCommitterName() {
	const CEnvironment env;
	env.CopyProcessEnvironment();
	CStringW envname = env.GetEnv(L"GIT_COMMITTER_NAME").c_str();
	if (!envname.IsEmpty())
		return envname;

	if (auto committername = GetConfigValue(L"committer.name"); !committername.IsEmpty())
		return committername;

	return GetConfigValue(L"user.name");
}


CStringW CGit::GetCommitterEmail() {
	const CEnvironment env;
	env.CopyProcessEnvironment();
	CStringW envmail = env.GetEnv(L"GIT_AUTHOR_EMAIL").c_str();
	if (!envmail.IsEmpty())
		return envmail;

	if (auto committermail = GetConfigValue(L"committer.email"); !committermail.IsEmpty())
		return committermail;

	return GetConfigValue(L"user.email");
}


CStringW CGit::GetConfigValue(const CStringW& name, const CStringW& def) {
	return CStringW{ReadConfigValue(name, def, false).c_str()};
}


std::wstring CGit::ReadConfigValue(const CStringW& name, const CStringW& def_, const bool canonicalizeBool) {
	const auto def = std::wstring{ def_.GetString(), size_t_cast(def_.GetLength()) };
	if (this->m_IsUseGitDLL) {
		CAutoLocker _(g_Git.m_critGitDllSec);

		try {
			CheckAndInitDll();
		} catch (...) {}
		CStringA value;
		const CStringA key = CUnicodeUtils::GetUTF8<CStringA>(name);

		try {
			if (git_get_config(key, CStrBufA(value, 4096), 4096))
				return def;
		} catch (const std::exception& ex) {
			::MessageBox(nullptr,
			             L"Could not get config.\nlibgit reports:\n" + CUnicodeUtils::GetUnicode<CStringW>(ex.what()),
			             L"TortoiseGit",
			             MB_OK | MB_ICONERROR);
			return def;
		}

		return CUnicodeUtils::StdGetUnicode(StringView{ value });
	} else {
		wil::ArgvData argv{ L"git.exe", L"config" };
		if (canonicalizeBool) argv << L"--bool";
		argv << L"--end-of-options" << StringView{ name };

		std::wstring configValue;
		if (GitCall::Run(argv, &configValue, nullptr)) // nullptr: the value is stdout alone, never git's warnings
			return def;
		if (configValue.empty())
			return {};
		return configValue.substr(0, configValue.size() - 1); // strip last newline character
	}
}


bool CGit::GetConfigValueBool(const CStringW& name, const bool def) {
	// git's own parser, for the same reason GetConfigValueInt32 uses
	// git_config_parse_int32: "true"/"yes"/"on"/"1"/"-2" and their negatives then
	// mean here exactly what they mean to git, with no second implementation to
	// drift. The inline chain this replaces also fell through to `false` for a
	// value git itself would reject, which disagreed with what the --bool path
	// did with the same input - the two backends were not answering the same
	// question. Now an unparseable value yields def on both.
	int value = def;
	if (!git_config_parse_bool(&value, CUnicodeUtils::GetUTF8<CStringA>(ReadConfigValue(name, def ? L"true" : L"false", true))))
		return value != 0;
	return def;
}


int CGit::GetConfigValueInt32(const CStringW& name, const int def) {
	const CStringW configValue = GetConfigValue(name);
	int value = def;
	if (!git_config_parse_int32(&value, CUnicodeUtils::GetUTF8<CStringA>(configValue)))
		return value;
	return def;
}


CStringW CGit::GetCurrentBranch(const bool fallback) const {
	CStringW output;
	const int result = GetCurrentBranchFromFile(m_CurrentDir, output, fallback);
	if (result != 0 && ((result == 1 && !fallback) || result != 1))
		return L"(no branch)";
	else
		return output;
}


void CGit::GetRemoteTrackedBranch(const CStringW& localBranch, CStringW& pullRemote, CStringW& pullBranch) {
	if (localBranch.IsEmpty())
		return;

	CStringW configName;
	configName.Format(L"branch.%s.remote", localBranch);
	pullRemote = GetConfigValue(configName);

	//Select pull-branch from current branch
	configName.Format(L"branch.%s.merge", localBranch);
	pullBranch = StripRefName(GetConfigValue(configName));
}


void CGit::GetRemoteTrackedBranchForHEAD(CStringW& remote, CStringW& branch) {
	CStringW refName;
	if (GetCurrentBranchFromFile(m_CurrentDir, refName))
		return;
	GetRemoteTrackedBranch(StripRefName(refName), remote, branch);
}


void CGit::GetRemotePushBranch(const CStringW& localBranch, CStringW& pushRemote, CStringW& pushBranch) {
	if (localBranch.IsEmpty())
		return;

	CStringW configName;

	configName.Format(L"branch.%s.pushremote", localBranch);
	pushRemote = g_Git.GetConfigValue(configName);
	if (pushRemote.IsEmpty()) {
		pushRemote = g_Git.GetConfigValue(L"remote.pushdefault");
		if (pushRemote.IsEmpty()) {
			configName.Format(L"branch.%s.remote", localBranch);
			pushRemote = g_Git.GetConfigValue(configName);
		}
	}

	configName.Format(L"branch.%s.pushbranch", localBranch);
	pushBranch = g_Git.GetConfigValue(configName); // do not strip branch name (for gerrit), see issue #1609)
	if (pushBranch.IsEmpty()) {
		configName.Format(L"branch.%s.merge", localBranch);
		pushBranch = TGitH::StripRefName(g_Git.GetConfigValue(configName));
	}
}


CStringW CGit::GetFullRefName(const CStringW& shortRefName) {
	CStringW refName;
	auto&& run = GitCall::Run(
		{ L"git.exe", L"rev-parse", L"--symbolic-full-name", L"--verify", L"--end-of-options", shortRefName.GetString() },
	    &refName
	);
	if (run != 0)
		return {}; //Error
	return refName.TrimRight();
}


std::wstring CGit::GetFullRefName(const std::wstring& shortRefName) {
	std::wstring refName;
	if (GitCall::Run({L"git.exe", L"rev-parse", L"--symbolic-full-name", L"--verify", L"--end-of-options", shortRefName.c_str()}, &refName, nullptr) != G_OK) // stdout alone: not "warning: refname is ambiguous"
		return {}; //Error
	wstr::TrimRight(refName);
	return refName;
}


int CGit::GetCurrentBranchFromFile(const fs::path& projectRoot, CStringW& branchOut, const bool fallback)
{
	// Read the current branch like git-gui does: parse .git/HEAD directly.
	if (projectRoot.empty())
		return GFail;

	const auto adminDir = GitAdminDir::GetWorktreeAdminDirPath(projectRoot.native());
	if (!adminDir)
		return GFail;

	std::ifstream head(fs::path{*adminDir} / L"HEAD", std::ios::binary, _SH_DENYWR);
	std::string line;
	if (!head || !std::getline(head, line))
		return GFail;
	const std::string_view headLine = StringUtils::TrimRight(line);     // " \r\n\t"

	if (constexpr auto pfx = "ref: refs/heads/"sv; headLine.starts_with(pfx)) {
		const auto in_str = headLine.substr(pfx.size());
		// On a branch. It might not exist, but HEAD looks good enough.
		branchOut = CUnicodeUtils::GetUnicode<CStringW>(in_str);
		return branchOut.IsEmpty() ? GFail : 0;
	}

	// Detached HEAD: report the hash if asked and it parses, else "HEAD".
	if (fallback) {
		bool ok = false;
		if (const CGitHash hash = CGitHash::FromHexStr(headLine, &ok); ok) {
			branchOut = hash.ToString().c_str();
			return 1;
		}
	}
	branchOut = L"HEAD";
	return 1;
}


wil::ArgvData CGit::GetLogCmd(CStringW range, const CTGitPath* path, const int mask, const CFilterData* Filter, const int logOrderBy) {
	wil::ArgvData params;

	params.emplace_back(L"-z");

	if (mask & LOG_INFO_STAT)
		params.emplace_back(L"--numstat");
	if (mask & LOG_INFO_FILESTATE)
		params.emplace_back(L"--raw");

	if (mask & LOG_INFO_BOUNDARY) {
		params.emplace_back(L"--left-right");
		params.emplace_back(L"--boundary");
	}

	if (mask & TGitH::LOG_INFO_ALL_BRANCH) {
		params.emplace_back(L"--all");
		if ((mask & LOG_INFO_ALWAYS_APPLY_RANGE) == 0)
			range.Empty();
	}

	if (mask & TGitH::LOG_INFO_BASIC_REFS) {
		params.emplace_back(L"--branches");
		params.emplace_back(L"--tags");
		params.emplace_back(L"--remotes");
		params.emplace_back(L"--glob=stas[h]"); // require at least one glob operator
		params.emplace_back(L"--glob=bisect");
		if ((mask & LOG_INFO_ALWAYS_APPLY_RANGE) == 0)
			range.Empty();
	}

	if (mask & TGitH::LOG_INFO_LOCAL_BRANCHES) {
		params.emplace_back(L"--branches");
		if ((mask & LOG_INFO_ALWAYS_APPLY_RANGE) == 0)
			range.Empty();
	}

	if (mask & TGitH::LOG_INFO_DETECT_COPYRENAME)
		params.emplace_back(std::format(L"-C{}%", ms_iSimilarityIndexThreshold));

	if (mask & TGitH::LOG_INFO_DETECT_RENAME)
		params.emplace_back(std::format(L"-M{}%", ms_iSimilarityIndexThreshold));

	if (mask & TGitH::LOG_INFO_FIRST_PARENT)
		params.emplace_back(L"--first-parent");

	if (mask & TGitH::LOG_INFO_NO_MERGE)
		params.emplace_back(L"--no-merges");

	if (mask & TGitH::LOG_INFO_FULL_HISTORY)
		params.emplace_back(L"--full-history");
	else
		params.emplace_back(L"--parents"); // cf. issue #3728

	if (mask & TGitH::LOG_INFO_FOLLOW)
		params.emplace_back(L"--follow");

	if (mask & TGitH::LOG_INFO_SHOW_MERGEDFILE)
		params.emplace_back(L"-c");

	if (mask & TGitH::LOG_INFO_FULL_DIFF)
		params.emplace_back(L"--full-diff");

	if (mask & TGitH::LOG_INFO_SIMPILFY_BY_DECORATION)
		params.emplace_back(L"--simplify-by-decoration");

	if (mask & TGitH::LOG_INFO_SPARSE)
		params.emplace_back(L"--sparse");

	if (Filter) {
		auto from = Filter->m_From;
		if (Filter->m_NumberOfLogsScale >= CFilterData::SHOW_LAST_N_YEARS) {
			const auto zone = std::chrono::current_zone();
			const auto today = std::chrono::floor<std::chrono::days>(zone->to_local(std::chrono::system_clock::now()));
			const auto midnight = zone->to_sys(today, std::chrono::choose::earliest);
			std::chrono::seconds substract = std::chrono::days{ 1 };
			switch (Filter->m_NumberOfLogsScale) {
			case CFilterData::SHOW_LAST_N_YEARS:
				substract *= 365;
				break;
			case CFilterData::SHOW_LAST_N_MONTHS:
				substract *= 30;
				break;
			case CFilterData::SHOW_LAST_N_WEEKS:
				substract *= 7;
				break;

			default:
				break;
			}
			from = midnight.time_since_epoch().count() - (Filter->m_NumberOfLogs * substract.count());
		}
		if (Filter->m_NumberOfLogsScale == CFilterData::SHOW_LAST_N_COMMITS)
			params.emplace_back(std::format(L"-n{}", Filter->m_NumberOfLogs));
		else if (Filter->m_NumberOfLogsScale >= CFilterData::SHOW_LAST_SEL_DATE && from > 0)
			params.emplace_back(std::format(L"--max-age={}", from));
	}

	if (Filter && (Filter->m_To != -1))
		params.emplace_back(std::format(L"--min-age={}", Filter->m_To));

	if (logOrderBy == LOG_ORDER_TOPOORDER || (mask & TGitH::LOG_ORDER_TOPOORDER))
		params.emplace_back(L"--topo-order");
	else if (logOrderBy == LOG_ORDER_DATEORDER)
		params.emplace_back(L"--date-order");
	else if (logOrderBy == LOG_ORDER_AUTHORDATEORDER)
		params.emplace_back(L"--author-date-order");

	params.emplace_back(L"--end-of-options");

	if (!range.IsEmpty()) {
		const int len = range.GetLength();
		const LPCWSTR data = range;
		int i = 0;
		while (i < len) {
			// skip leading whitespace
			while (i < len && iswspace(data[i]))
				++i;

			const int start = i;
			// read ref
			while (i < len && !iswspace(data[i]))
				++i;
			if (start < i)
				params.emplace_back(std::wstring_view(data + start, i - start));
		}
	}

	params.emplace_back(L"--");

	if (path)
		params.emplace_back(path->GetWString());

	return params;
}


int CGit::GetHash(git_repository* repo, CGitHash& hash, const std::wstring_view friendname, const bool skipFastCheck /* = false */) {
	_ASSERTE(repo);

	// no need to look up a ref if it's already an OID
	if (!skipFastCheck) {
		bool isHash = false;
		hash = CGitHash::FromHexStr(friendname, &isHash);
		if (isHash)
			return 0;
	}

	const int isHeadOrphan = git_repository_head_unborn(repo);
	if (isHeadOrphan != 0) {
		hash.Empty();
		if (isHeadOrphan == 1) {
			if (friendname == L"HEAD") // special check for unborn branch: if not requesting HEAD, do normal commit lookup
				return 0;
		}
		else
			return -1;
	}

	wil::unique_git_object gitObject;
	if (git_revparse_single(gitObject.put(), repo, CUnicodeUtils::StdGetUTF8(friendname).c_str()))
		return -1;

	hash = git_object_id(gitObject.get());

	return 0;
}


int CGit::GetHash(CGitHash& hash, const std::wstring& friendname) const {
	// no need to look up a ref if it's already an OID
	bool hashOk = false;
	hash = CGitHash::FromHexStr(friendname, &hashOk);
	if (hashOk)
		return 0;

	if (m_IsUseLibGit2) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return -1;

		return GetHash(repo.get(), hash, friendname, true);
	}
	else {
		if (friendname.empty()) {
			gitLastErr.clear();
			return GFail;
		}
		CStringW branch = FixBranchName(friendname.c_str());
		if (friendname == L"FETCH_HEAD" && branch.IsEmpty())
			branch = friendname.c_str();
		gitLastErr.clear();
		auto&& ret = GitCall::Run({ L"git.exe", L"rev-parse", L"--verify", L"--end-of-options", branch.GetString() }, &gitLastErr, nullptr); // stdout alone: it is parsed as a hash

		bool isHash{};
		wstr::Trim(gitLastErr); // rev-parse ends its line with '\n', which FromHexStr rejects
		hash = CGitHash::FromHexStr(gitLastErr, &isHash);
		if (ret == 0) {
			gitLastErr.clear();
		} else if (friendname == L"HEAD") { // special check for unborn branch
			CStringW currentbranch;
			if (GetCurrentBranchFromFile(m_CurrentDir, currentbranch))
				return -1;
			gitLastErr.clear();
			return 0;
		}
		return ret;
	}
}


int CGit::GetInitAddList(CTGitPathList& outputlist, const bool getStagingStatus) {
	std::string cmdout; // -z output: NUL-separated records, parsed as bytes

	outputlist.Clear();
	if (GitCall::Run({ L"git.exe", L"ls-files", L"-s", L"-t", L"-z" }, &cmdout))
		return GFail;

	if (outputlist.ParserFromLsFile(cmdout))
		return GFail;
	for (int i = 0; i < outputlist.GetCount(); ++i)
		const_cast<CTGitPath&>(outputlist[i]).m_Action = CTGitPath::LOGACTIONS_ADDED;

	if (getStagingStatus) {
		std::wstring cmdunstagedout;
		for (int i = 0; i < outputlist.GetCount(); ++i)
			const_cast<CTGitPath&>(outputlist[i]).m_stagingStatus = CTGitPath::StagingStatus::TotallyStaged;
		if (GitCall::Run({ L"git.exe", L"diff-files", L"--raw", L"--numstat", L"-C", L"-M", L"-z", L"--" }, &cmdunstagedout, nullptr))
			return GFail;

		CTGitPathList unstaged;
		unstaged.ParserFromLog(cmdunstagedout);
		// File shows up both in the output of ls-files and diff-files: partially staged (typically modified after being added)
		for (int j = 0; j < unstaged.GetCount(); ++j) {
			const std::wstring& path = unstaged[j].GetGitPathString().c_str();
			outputlist.UpdateStagingStatusFromPath(path, CTGitPath::StagingStatus::PartiallyStaged); // TODO: This is inefficient
		}
	}
	return G_OK;
}


int CGit::GetSubmoduleHash(const CStringW& pathOfSubmodule, const CGitHash& revision, CGitHash& submoduleRevision, CStringW& err) const {
	if (!m_IsUseLibGit2)
		return 0;

	const wil::unique_git_repository repository(g_Git.GetGitRepository());
	if (!repository) {
		err = GetLibGit2LastErr(L"Could not open repository.");
		return -1;
	}

	wil::unique_git_commit commit;
	if (git_commit_lookup(&commit, repository.get(), revision)) {
		err = GetLibGit2LastErr(L"Could not lookup commit.");
		return -1;
	}

	wil::unique_git_tree tree;
	if (git_commit_tree(&tree, commit.get())) {
		err = GetLibGit2LastErr(L"Could not get tree of commit.");
		return -1;
	}

	wil::unique_git_tree_entry treeEntry;
	if (git_tree_entry_bypath(treeEntry.put(), tree.get(), CUnicodeUtils::StdGetUTF8(StringView(pathOfSubmodule)).c_str())) {
		err = GetLibGit2LastErr(L"Could not lookup path.");
		return -1;
	}

	if (git_tree_entry_type(treeEntry.get()) != GIT_OBJECT_COMMIT) {
		err.Format(L"No submodule found at \"%s\" in revision %s.", pathOfSubmodule, revision.ToString().c_str());
		return -1;
	}

	submoduleRevision = git_tree_entry_id(treeEntry.get());
	return 0;
}


int CGit::GetCommitDiffList(
	const CStringW& rev1,
    const CStringW& rev2,
    CTGitPathList& outpathlist,
    CStringW& error,
    const bool ignoreSpaceAtEol,
    const bool ignoreSpaceChange,
    const bool ignoreAllSpace,
    const bool ignoreBlankLines
) {
	// One flag per element now, rather than one string of space-separated flags
	// that only worked because it was interpolated into a command line.
	std::vector<std::wstring> ignore;
	if (ignoreSpaceAtEol)
		ignore.emplace_back(L"--ignore-space-at-eol");
	if (ignoreSpaceChange)
		ignore.emplace_back(L"--ignore-space-change");
	if (ignoreAllSpace)
		ignore.emplace_back(L"--ignore-all-space");
	if (ignoreBlankLines)
		ignore.emplace_back(L"--ignore-blank-lines");

	const bool rev1IsWorkingCopy = rev1 == tgit::invarients::ZeroRevSV().data();
	const bool rev2IsWorkingCopy = rev2 == tgit::invarients::ZeroRevSV().data();
	const bool againstWorkingCopy = rev1IsWorkingCopy || rev2IsWorkingCopy;

	wil::ArgvData argv{ L"git.exe", againstWorkingCopy ? L"diff" : L"diff-tree", L"-r" };
	if (againstWorkingCopy && !rev1IsWorkingCopy)
		argv.emplace_back(L"-R");
	argv.emplace_back(L"--raw");
	argv.emplace_back(std::format(L"-C{}%", ms_iSimilarityIndexThreshold));
	argv.emplace_back(std::format(L"-M{}%", ms_iSimilarityIndexThreshold));
	argv.emplace_back(L"--numstat");
	argv.emplace_back(L"-z");
	for (const auto& arg : ignore)
		argv.emplace_back(arg);
	argv.emplace_back(L"--end-of-options");
	if (againstWorkingCopy) {
		argv.emplace_back(std::wstring(rev1IsWorkingCopy ? rev2 : rev1));
	} else {
		argv.emplace_back(std::wstring(rev2));
		argv.emplace_back(std::wstring(rev1));
	}
	argv.emplace_back(L"--");

	// CStringW& error
	std::wstring out;
	std::wstring err;
	if (GitCall::Run(argv, &out, &err)) {
		error = str::to_atl<CStringW>(err);
		return GFail;
	}

	return outpathlist.ParserFromLog(CUnicodeUtils::StdGetUTF8(out));
}


int CGit::GetTagList(std::vector<std::wstring>& list) const {
	const auto prevCount = std::ssize(list);
	if (this->m_IsUseLibGit2) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return GFail;

		wil::unique_git_strarray tag_names;
		if (git_tag_list(&tag_names, repo.get()))
			return GFail;

		AppendTo(tag_names, list);
	}
	else {
		std::wstring tempOut;
		gitLastErr.clear();
		auto&& ret = GitCall::Run({ L"git.exe", L"tag", L"-l" }, &tempOut, &gitLastErr);
		for (const auto& lineA : str::stringtok(tempOut, true, L"\n")) {
			if (lineA.empty())
				return G_OK;
			list.emplace_back(StringView{lineA});
		}
		if (ret) {
			if (ret == 1 && IsInitRepos())
				return G_OK;
			return ret;
		}
	}
	const auto pred = details::g_bSortTagsReversed ? details::LogicalCompareReversedPredicate : details::LogicalComparePredicate;
	std::sort(list.begin() + prevCount, list.end(), pred);
	return 0;
}


bool CGit::IsBranchTagNameUnique(const std::wstring& name) const {
	const auto branch = L"refs/heads/"s + name;
	const auto tag = L"refs/tags/"s + name;
	if (m_IsUseLibGit2) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return true; // TODO: optimize error reporting

		wil::unique_git_reference tagRef;
		if (git_reference_lookup(tagRef.put(), repo.get(), CUnicodeUtils::StdGetUTF8(tag).c_str()))
			return true;

		wil::unique_git_reference branchRef;
		auto&& ret = git_reference_lookup(branchRef.put(), repo.get(), CUnicodeUtils::StdGetUTF8(branch).c_str());
		return ret;
	} else {
		int refCnt = 0;
		const auto argv = wil::ArgvData{ L"git.exe", L"show-ref", L"--tags", L"--heads", L"--", branch, tag};
		RunReceiver(
			argv,
			[&](const std::string_view lineA) {
				if (lineA.empty())
					return;
				++refCnt;
			}
		);
		return (refCnt <= 1);
	}

}


bool CGit::BranchTagExists(const CStringW& name, const bool isBranch /*= true*/) const {
	if (m_IsUseLibGit2) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return false; // TODO: optimize error reporting

		const auto prefix = isBranch ? L"refs/heads/" : L"refs/tags/";

		wil::unique_git_reference ref;
		if (git_reference_lookup(ref.put(), repo.get(), CUnicodeUtils::StdGetUTF8(StringView(prefix + name)).c_str()))
			return false;

		return true;
	}
	// else
	// Note that both refs are always passed, whichever of --heads/--tags is
	// asked for; that predates the argv rewrite and is left as it was.
	const std::vector<std::wstring> argv{ L"git.exe", L"show-ref", isBranch ? L"--heads" : L"--tags", L"--", StringView(L"refs/heads/" + name).data(),
	                                      StringView(L"refs/tags/" + name).data() };

	CStringW output;
	if (!GitCall::Run(argv, &output, nullptr, CP_UTF8)) {
		if (!output.IsEmpty())
			return true;
	}

	return false;
}


CStringW CGit::DerefFetchHead() const {
	const std::wstring dotGitPath = GitAdminDir::GetWorktreeAdminDirPath(m_CurrentDir.native()).value_or(std::wstring{});
	std::ifstream fetchHeadFile(dotGitPath + L"FETCH_HEAD", std::ios::in | std::ios::binary);
	int forMergeLineCount = 0;
	std::string line;
	std::string hashToReturn;
	while (getline(fetchHeadFile, line)) {
		//Tokenize this line
		std::string::size_type pos = line.find('\t');
		if (pos == std::string::npos)
			continue; //invalid line

		const std::string hash = line.substr(0, pos);
		++pos;
		const std::string::size_type prevPos = pos;
		pos = line.find('\t', pos);
		if (pos == std::string::npos)
			continue;

		const bool forMerge = pos == prevPos;
		pos = line.size();
		if (pos == std::string::npos)
			continue;

		//std::string remoteBranch = line.substr(prevPos, pos - prevPos);

		//Process this line
		if (forMerge) {
			hashToReturn = hash;
			++forMergeLineCount;
			if (forMergeLineCount > 1)
				return {}; //More then 1 branch to merge found. Octopus merge needed. Cannot pick single ref from FETCH_HEAD
		}
	}

	return CUnicodeUtils::GetUnicode<CStringW>(hashToReturn);
}


int CGit::GetBranchList(std::vector<std::wstring>& list, int* current, const BRANCH_TYPE type, const bool skipCurrent /*false*/) const {
	const size_t prevCount = list.size();
	int ret = 0;
	std::wstring cur;
	bool headIsDetached = false;
	if (m_IsUseLibGit2) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return -1;

		if (git_repository_head_detached(repo.get()) == 1)
			headIsDetached = true;

		if ((type & (BRANCH_LOCAL | BRANCH_REMOTE)) != 0) {
			git_branch_t flags = GIT_BRANCH_LOCAL;
			if ((type & BRANCH_LOCAL) && (type & BRANCH_REMOTE))
				flags = GIT_BRANCH_ALL;
			else if (type & BRANCH_REMOTE)
				flags = GIT_BRANCH_REMOTE;

			wil::unique_git_branch_iterator it;
			if (git_branch_iterator_new(&it, repo.get(), flags))
				return -1;

			wil::unique_git_reference ref;
			git_branch_t branchType;
			while (git_branch_next(&ref, &branchType, it.get()) == 0) {
				const char* name = nullptr;
				if (git_branch_name(&name, ref.get()))
					continue;

				std::wstring branchname = CUnicodeUtils::StdGetUnicode(name);
				if (branchType & GIT_BRANCH_REMOTE)
					list.push_back(L"remotes/" + branchname);
				else {
					if (git_branch_is_head(ref.get())) {
						if (skipCurrent)
							continue;
						cur = branchname;
					}
					list.push_back(branchname);
				}
			}
		}
	}
	else {
		wil::ArgvData argv{ L"git.exe", L"branch", L"--no-color" };

		if ((type & BRANCH_ALL) == BRANCH_ALL)
			argv << L"-a";
		else if (type & BRANCH_REMOTE)
			argv << L"-r";

		ret = GitCall::RunReceiver(argv, [&](std::string_view lineA) {
			StringUtils::TrimLeft(lineA);
			if (lineA.empty())
				return;
			if (lineA.find(" -> ") != std::string_view::npos)
				return; // skip something like: refs/origin/HEAD -> refs/origin/master

			auto branch = CUnicodeUtils::StdGetUnicode(lineA);
			if (lineA[0] == '*') {
				if (skipCurrent)
					return;
				// tgit::wstr::Mid, not substr: these carry CStringW's clamping, and a
				// line shorter than the prefix would make substr throw where Mid
				// returned empty
				branch = tgit::wstr::Mid(branch, static_cast<int>(wcslen(L"* ")));
				cur = branch;

				// check whether HEAD is detached
				CStringW currentHead;
				if (!branch.empty() && branch[0] == L'(' && GetCurrentBranchFromFile(m_CurrentDir, currentHead) == 1) {
					headIsDetached = true;
					return;
				}
			}
			else if (lineA[0] == '+') // since Git 2.23 branches that are checked out in other worktrees connected to the same repository prefixed with '+'
				branch = tgit::wstr::Mid(branch, static_cast<int>(wcslen(L"+ ")));
			if ((type & BRANCH_REMOTE) != 0 && (type & BRANCH_LOCAL) == 0)
				branch = L"remotes/" + branch;
			list.push_back(std::move(branch));
		});
		if (ret == 1 && IsInitRepos())
			return 0;
	}

	if (type & BRANCH_FETCH_HEAD && !DerefFetchHead().IsEmpty())
		list.push_back(L"FETCH_HEAD");

	std::sort(list.begin() + prevCount, list.end(), details::LogicalCompareBranchesPredicate);

	if (current && !headIsDetached && !skipCurrent) {
		for (unsigned int i = 0; i < list.size(); ++i) {
			if (list[i] == cur) {
				*current = gsl::narrow_cast<int>(i);
				break;
			}
		}
	}

	return ret;
}


int CGit::GetRefsCommitIsOn(std::vector<std::wstring>& list,
                            const CGitHash& hash,
                            const bool includeTags,
                            const bool includeBranches,
                            const BRANCH_TYPE type) const {
	if (!includeTags && !includeBranches || hash.IsEmpty())
		return 0;

	const size_t prevCount = list.size();
	const wil::unique_git_repository repo(GetGitRepository());
	if (!repo)
		return -1;

	wil::unique_git_reference_iterator it;
	if (git_reference_iterator_new(&it, repo.get()))
		return -1;

	auto checkDescendent = [&list, &hash, &repo](const git_oid* oid, const git_reference* ref) {
		if (!oid)
			return;
		if (git_oid_equal(oid, hash) || git_graph_descendant_of(repo.get(), oid, hash) == 1) {
			const char* name = git_reference_name(ref);
			if (!name)
				return;

			list.push_back(CUnicodeUtils::StdGetUnicode(name));
		}
	};

	wil::unique_git_reference ref;
	while (git_reference_next(&ref, it.get()) == 0) {
		if (git_reference_is_tag(ref.get())) {
			if (!includeTags)
				continue;

			wil::unique_git_tag tag;
			if (git_tag_lookup(&tag, repo.get(), git_reference_target(ref.get())) == 0) {
				wil::unique_git_object obj;
				if (git_tag_peel(&obj, tag.get()) < 0)
					continue;
				checkDescendent(git_object_id(obj.get()), ref.get());
				continue;
			}
		}
		else if (git_reference_is_remote(ref.get())) {
			if (!includeBranches || !(type & BRANCH_REMOTE))
				continue;
		}
		else if (git_reference_is_branch(ref.get())) {
			if (!includeBranches || !(type & BRANCH_LOCAL))
				continue;
		}
		else
			continue;

		if (git_reference_type(ref.get()) == GIT_REFERENCE_SYMBOLIC) {
			wil::unique_git_reference peeledRef;
			if (git_reference_resolve(&peeledRef, ref.get()) < 0)
				continue;

			checkDescendent(git_reference_target(peeledRef.get()), ref.get());
			continue;
		}

		checkDescendent(git_reference_target(ref.get()), ref.get());
	}

	std::sort(list.begin() + prevCount, list.end(), details::LogicalCompareBranchesPredicate);
	list.erase(std::unique(list.begin() + prevCount, list.end()), list.end());

	return 0;
}


int CGit::GetRemoteList(std::vector<std::wstring>& list) const {
	const size_t prevCount = list.size();
	if (this->m_IsUseLibGit2) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return -1;

		wil::unique_git_strarray remotes;
		if (git_remote_list(&remotes, repo.get()))
			return -1;

		AppendTo(remotes, list);

		std::sort(list.begin() + prevCount, list.end(), details::LogicalComparePredicate);

		return 0;
	}

	gitLastErr.clear();
	return GitCall::RunReceiver(
		{ L"git.exe", L"remote" },
		[&](const std::string_view lineA) {
		   if (lineA.empty())
		       return;
		   list.push_back(CUnicodeUtils::StdGetUnicode(lineA));
		}
	);
}


int CGit::GetRemoteRefs(const CStringW& remote, REF_VECTOR& list, const bool includeTags, const bool includeBranches) const {
	if (!includeTags && !includeBranches)
		return 0;

	const auto prevCount = std::ssize(list);
	// GIT_CMD_FETCH is the one gate in this file that is deliberately left alone:
	// it also selects between two whole dialogs in CAppUtils::Fetch and SyncDlg,
	// so it cannot become libgit2-only here without GetGitLastErr(.., GIT_CMD_FETCH)
	// starting to report the wrong side's error at the call sites of this function.
	if (UsingLibGit2(GIT_CMD_FETCH)) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return -1;

		const std::string remoteA = CUnicodeUtils::StdGetUTF8(StringView(remote));
		wil::unique_git_remote gitremote;
		// first try with a named remote (e.g. "origin")
		if (git_remote_lookup(gitremote.put(), repo.get(), remoteA.c_str()) < 0) {
			// retry with repository located at a specific url
			if (git_remote_create_anonymous(gitremote.put(), repo.get(), remoteA.c_str()) < 0)
				return -1;
		}

		git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;
		callbacks.credentials = g_Git2CredCallback;
		callbacks.certificate_check = g_Git2CheckCertificateCallback;
		git_proxy_options proxy = GIT_PROXY_OPTIONS_INIT;
		proxy.type = GIT_PROXY_AUTO;
		if (git_remote_connect(gitremote.get(), GIT_DIRECTION_FETCH, &callbacks, &proxy, nullptr) < 0)
			return -1;

		const git_remote_head** heads = nullptr;
		size_t size = 0;
		if (git_remote_ls(&heads, &size, gitremote.get()) < 0)
			return -1;

		for (size_t i = 0; i < size; i++) {
			auto ref = CUnicodeUtils::GetUnicode<CStringW>(std::string_view{heads[i]->name});
			CStringW shortname;
			if (details::GetShortName(ref, shortname, L"refs/tags/")) {
				if (!includeTags)
					continue;
			}
			else {
				if (!includeBranches)
					continue;
				if (!details::GetShortName(ref, shortname, L"refs/heads/"))
					shortname = ref;
			}
			list.emplace_back(TGitRef{
				.name = std::wstring{ includeTags && includeBranches ? ref : shortname },
				.hash = CGitHash{ heads[i]->oid }
			});
		}
	} else {
		const wil::ArgvData argv{
			L"git.exe",
			L"ls-remote",
			(includeTags && !includeBranches) ? L"-t" : L"--refs",
			L"--",
			std::wstring(remote)
		};
		// The command line is echoed into the error output, so it still has to be
		// serialized here even though nothing else needs it flat.
		gitLastErr = argv.SerializeArgv(has_weird_paths());
		gitLastErr += L'\n';

		[[maybe_unused]] const auto _ = gsl::finally([this] { s_limitGitExeOutput = false; });
		s_limitGitExeOutput = true;
		auto&& ret = GitCall::RunReceiver(
			argv,
			[&](const std::string_view origLineA) {
				if (origLineA.size() <= GitFormatHolder::GitHashSize() * 2 + strlen("\t") || origLineA[GitFormatHolder::GitHashSize() * 2] != '\t')
					// OID, tab, refname
						return;
					const CGitHash hash = CGitHash::FromHexStr(origLineA.substr(0, GitFormatHolder::GitHashSize() * 2));
					const auto ref = CUnicodeUtils::GetUnicode<CStringW>(origLineA.substr(GitFormatHolder::GitHashSize() * 2 + strlen("\t")));
					CStringW shortname;
					if (details::GetShortName(ref, shortname, L"refs/tags/")) {
						if (!includeTags)
							return;
					}
					else {
						if (!includeBranches)
							return;
						if (!details::GetShortName(ref, shortname, L"refs/heads/"))
							shortname = ref;
					}
					if (includeTags && includeBranches)
						list.emplace_back(TGitRef{ .name = std::wstring(ref), .hash = hash });
					else if (includeTags && StringUtils::EndsWith(ref, L"^{}"))
						list.emplace_back(TGitRef{ .name = std::wstring(shortname + L"^{}"), .hash = hash });
					else
						list.emplace_back(TGitRef{ .name = std::wstring(shortname), .hash = hash });
				}
			);
		if (ret != G_OK)
			return ret;
	}
	if (details::g_bSortTagsReversed && includeTags && !includeBranches)
		std::ranges::sort(std::views::drop(list, prevCount), details::LogicalCompareReversedPredicate, &TGitRef::name);
	else
		std::ranges::sort(std::views::drop(list, prevCount), details::LogicalComparePredicate, &TGitRef::name);
	return G_OK;
}


int CGit::GetRefList(std::vector<std::wstring>& list) const {
	const auto prevCount = std::ssize(list);
	if (this->m_IsUseLibGit2) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return -1;

		auto callback = [](git_reference* rawref, void* payload) -> int {
			// the callback owns the reference
			const auto ref = wil::unique_git_reference(rawref);
			auto* _list = static_cast<std::vector<std::wstring>*>(payload);

			const auto multibyte = git_reference_name(ref.get());
			_list->push_back(CUnicodeUtils::StdGetUnicode(multibyte));

			return GIT_OK;
		};


		auto&& ret = git_reference_foreach(repo.get(), callback, &list);
		if (ret != GIT_OK)
			return ret;
	} else {
		MAP_HASH_NAME map;
		auto&& ret = git_show_ref(map);
		// show-ref -d follows each annotated tag with its peeled "name^{}"; those are
		// the same ref, which upstream skipped and libgit2's foreach never lists
		for (const auto& val : map | std::views::values) {
			list.append_range(val | std::views::filter([](const auto& name) { return !std::wstring_view{ name }.ends_with(L"^{}"); }));
		}
		if (ret != G_OK) {
			return (ret == 1 && IsInitRepos()) ? G_OK : ret;
		}
	}
	std::ranges::sort(std::views::drop(list, prevCount), details::LogicalComparePredicate);
	return G_OK;
}


int CGit::GetMapHashToFriendName(MAP_HASH_NAME& map) const {
	if (this->m_IsUseLibGit2) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return -1;

		return details::GetMapHashToFriendName(repo.get(), map);
	}

	auto&& ret = git_show_ref(map);
	if (ret == 1 && IsInitRepos())
		return 0;
	return ret;
}


int CGit::GuessRefForHash(CStringW& ref, const CGitHash& hash) const {
	MAP_HASH_NAME map;
	if (GetMapHashToFriendName(map))
		return -1;

	const auto it = map.find(hash);
	if (it == map.cend()) {
		ref = hash.ToString(GetShortHASHLength()).c_str();
		return 1;
	}

	const auto& reflist = it->second;
	for (const auto& reftype : { L"refs/heads/", L"refs/remotes/", L"refs/tags/" }) {
		auto found = std::ranges::find_if(reflist.cbegin(), reflist.cend(), [&reftype](const auto& ref_) { return ref_.starts_with(reftype); });
		if (found == reflist.cend())
			continue;

		details::GetShortName(found->c_str(), ref, reftype);
		return 0;
	}

	ref = hash.ToString(GetShortHASHLength()).c_str();
	return 1;
}

int CGit::GetBranchDescriptions(dict_t& map) const {
	// Get the `config`
	wil::unique_git_config config;
	git_config_new(config.put());
	// UTF-8, as libgit2 reads it: path::string() would be the ACP
	const auto configFilepath = CUnicodeUtils::StdGetUTF8(GetGitLocalConfig().wstring());
	if (auto&& ret = git_config_add_file_ondisk(config.get(), configFilepath.c_str(), GIT_CONFIG_LEVEL_LOCAL, nullptr, FALSE); ret != GIT_OK)
		return GFail;

	// Get the branches
	auto&& ret = git_config_foreach_match(
		config.get(),
        "^branch\\..*\\.description$",
        [](const git_config_entry* entry, void* data) {
        	const auto value = CUnicodeUtils::StdGetUnicode(entry->value);

        	// extract branch name from config key; the match pattern above guarantees
            // both affixes are present, so the length arithmetic cannot underflow
            constexpr size_t prefix = std::size(L"branch.") - 1;
            constexpr size_t suffix = std::size(L".description") - 1;
            const std::wstring key_full = CUnicodeUtils::StdGetUnicode(entry->name);
        	const auto key = key_full.substr(prefix, key_full.size() - prefix - suffix);

        	// a reference: `auto` copied the map, and every emplace went into the copy
        	auto& descriptions = *static_cast<dict_t*>(data);
            descriptions.emplace(key, value);
            return G_OK;
        },
        &map
	);
	return ret;
}


bool CGit::CheckMsysGitDir(const bool bFallback) {
	if (m_bInitialized)
		return TRUE;

	TracePrintf(__FUNCTION__ L": CheckMsysGitDir(%d)\n", bFallback);
	Environment::g_Environment.CopyProcessEnvironment();

	Environment::g_Environment.SetEnv(L"TGIT_INITIATED_CALL", L"1");

	// Sanitize GIT_* environment variables, cf. https://github.com/git-for-windows/build-extra/pull/529 and git/environment.h
	Environment::g_Environment.UnsetEnv(L"GIT_INDEX_FILE");
	Environment::g_Environment.UnsetEnv(L"GIT_INDEX_VERSION");
	Environment::g_Environment.UnsetEnv(L"GIT_OBJECT_DIRECTORY");
	Environment::g_Environment.UnsetEnv(L"GIT_ALTERNATE_OBJECT_DIRECTORIES");
	Environment::g_Environment.UnsetEnv(L"GIT_DIR");
	Environment::g_Environment.UnsetEnv(L"GIT_WORK_TREE");
	Environment::g_Environment.UnsetEnv(L"GIT_NAMESPACE");
	Environment::g_Environment.UnsetEnv(L"GIT_CEILING_DIRECTORIES");
	Environment::g_Environment.UnsetEnv(L"GIT_DISCOVERY_ACROSS_FILESYSTEM");
	Environment::g_Environment.UnsetEnv(L"GIT_COMMON_DIR");
	Environment::g_Environment.UnsetEnv(L"GIT_DEFAULT_HASH");
	Environment::g_Environment.UnsetEnv(L"GIT_CONFIG");
	Environment::g_Environment.UnsetEnv(L"GIT_CONFIG_GLOBAL");
	Environment::g_Environment.UnsetEnv(L"GIT_CONFIG_SYSTEM");
	Environment::g_Environment.UnsetEnv(L"GIT_CONFIG_NOSYSTEM");
	Environment::g_Environment.UnsetEnv(L"GIT_CONFIG_COUNT");
	Environment::g_Environment.UnsetEnv(L"GIT_ATTR_NOSYSTEM");
	Environment::g_Environment.UnsetEnv(L"GIT_ATTR_SOURCE");
	Environment::g_Environment.UnsetEnv(L"GIT_SHALLOW_FILE");
	Environment::g_Environment.UnsetEnv(L"GIT_GRAFT_FILE");

	// Git for Windows 2.10.1 and 2.10.2 require LC_ALL to be set, see https://tortoisegit.org/issue/2859 and https://github.com/git-for-windows/git/issues/945,
	// because MSys2 changed the default to "ASCII". SO, make sure we have a proper default set
	if (Environment::g_Environment.GetEnv(L"LC_ALL").empty())
		Environment::g_Environment.SetEnv(L"LC_ALL", L"C");

	// set HOME if not set already
	auto home_env = wil::TryGetEnvironmentVariable(L"HOME");
	if (!home_env) {
		Environment::g_Environment.SetEnv(L"HOME", GetHomeDirectory().wstring());
	}

	//setup ssh client

	// The default is OpenSSH, which ships with Windows and needs no path: git and
	// ssh-wintunnel both resolve it from PATH. This must name *something* — the libgit2
	// transport hard-errors with "No GIT_SSH tool configured" when the variable is unset.
	// It is a console program with no console here, so its prompts reach the user through
	// SSH_ASKPASS/SshAskPass.exe, set below.
	auto path_str = L"ssh.exe"s;
	fs::path sshclient = details::GetConfiguredSshClient();
	if (!sshclient.empty())
		 path_str = ms_bCygwinGit ? sshclient.generic_wstring() : sshclient.wstring();
	Environment::g_Environment.SetEnv(L"GIT_SSH", path_str);
	Environment::g_Environment.SetEnv(L"SVN_SSH", path_str);

	{
		// SshAskPass.exe, next to this executable
		fs::path sAskPass = wil::GetModulePath();
		if (!sAskPass.empty()) {
			auto ask_path = sAskPass.parent_path() / L"SshAskPass.exe";
			auto ask_str = ms_bCygwinGit ? ask_path.generic_wstring() : ask_path.wstring();
			Environment::g_Environment.SetEnv(L"SSH_ASKPASS", ask_str);
			Environment::g_Environment.SetEnv(L"GIT_ASKPASS", ask_str);
			Environment::g_Environment.SetEnv(L"GIT_ASK_YESNO", ask_str);
			Environment::g_Environment.SetEnv(L"DISPLAY", L":9999");
			Environment::g_Environment.SetEnv(L"SSH_ASKPASS_REQUIRE", L"force"); // improve compatibility with Win32-OpenSSH, see issue #3996
		}
	}

	if (!details::FindAndSetGitExePath(bFallback))
		return FALSE;

	{
		auto msysGitDir = (ms_LastMsysGitDir / L"..").lexically_normal();
		for (const auto sub : { L"mingw64"sv, L"mingw32"sv, {} }) {
			if (fs::is_directory(msysGitDir / sub / L"etc")) {
				msysGitDir /= sub;
				break;
			}
		}
		if (ms_bMsys2Git) // in Msys2 git.exe is in usr\bin; this also need to be after the check for etc folder, as Msys2 also has mingw64\etc, but uses etc
			msysGitDir = ms_LastMsysGitDir.parent_path().parent_path();
		ms_MsysGitRootDir = msysGitDir;
	}

	if (
		const auto systemConfigOwner = GetGitSystemConfig();
		wil::reg::try_get_string(HKEY_CURRENT_USER, Registry::TortoiseGitKey, REG_SYSTEM_GITCONFIGPATH).value_or(std::wstring{}) != systemConfigOwner
	)
		std::ignore = wil::reg::set_tgit_string(REG_SYSTEM_GITCONFIGPATH, systemConfigOwner.c_str());

	Trace(__FUNCTION__ L": ms_LastMsysGitDir = {}\n", ms_LastMsysGitDir.wstring());
	Trace(__FUNCTION__ L": ms_MsysGitRootDir = {}\n", ms_MsysGitRootDir.wstring());
	Trace(__FUNCTION__ L": System config file = {}\n", GetGitSystemConfig().wstring());
	libgit2::SetLibGit2SearchPath(GIT_CONFIG_LEVEL_PROGRAMDATA, L"");

	if (ms_bCygwinGit)
		Trace(__FUNCTION__ L": CygwinHack: true\n");
	if (ms_bMsys2Git)
		Trace(__FUNCTION__ L": Msys2Hack: true\n");

	ms_LastMsysGitVersion = Registry::CRegDWORD(L"Software\\TortoiseGit\\git_cached_version");

	// Configure libgit2 search paths
	libgit2::SetLibGit2SearchPath(GIT_CONFIG_LEVEL_SYSTEM, GetGitSystemConfig().parent_path().string());
	libgit2::SetLibGit2SearchPath(GIT_CONFIG_LEVEL_GLOBAL, GetHomeDirectory().string());
	libgit2::SetLibGit2SearchPath(GIT_CONFIG_LEVEL_XDG, GetGitGlobalXDGConfig().parent_path().string());

	libgit2::SetLibGit2UserAgent();
	if (!(ms_bCygwinGit || ms_bMsys2Git))
		libgit2::SetLibGit2TemplatePath((ms_MsysGitRootDir / L"share\\git-core\\templates").c_str());
	else
		libgit2::SetLibGit2TemplatePath((ms_MsysGitRootDir / L"usr\\share\\git-core\\templates").c_str());

	auto transportCallback = [](git_smart_subtransport** out, git_transport* owner, void*) -> int {
		const auto sshtoolpath = details::FindExecutableOnPath(Environment::g_Environment.GetEnv(L"GIT_SSH").c_str(), Environment::g_Environment.GetEnv(L"PATH").c_str());
		const auto ret = git_smart_subtransport_ssh_wintunnel(out, owner, sshtoolpath, Environment::g_Environment.put());
		return ret;
	};
	libgit2::libgit2_transport_register("ssh", transportCallback);
	libgit2::libgit2_transport_register("ssh+git", transportCallback);
	libgit2::libgit2_transport_register("git+ssh", transportCallback);

	Environment::g_Environment.AddToPath(ms_LastMsysGitDir.native());
	const auto msys_extra = wil::reg::try_get_string(HKEY_CURRENT_USER, Registry::TortoiseGitKey, REG_MSYSGIT_EXTRA_PATH).value_or(std::wstring{});
	Environment::g_Environment.AddToPath(msys_extra);

#if !defined(TGITCACHE) && !defined(TORTOISESHELL)
	// register filter only once
	if (!git_filter_lookup("filter")) {
		std::wstring sh;
		for (const auto* const binDirPrefix : { L"..\\usr\\bin", L"..\\bin", L"" }) {
			const auto possibleShExe = (ms_LastMsysGitDir / binDirPrefix / L"sh.exe");
			if (fs::exists(possibleShExe)) {
				sh = std::format(L"\"{}\"", possibleShExe.native());
				// we need to put the usr\bin folder on the path for Git for Windows based on msys2
				Environment::g_Environment.AddToPath(possibleShExe.parent_path().native());
				break;
			}
		}

		// Add %GIT_EXEC_PATH% to %PATH% when launching libgit2 filter executable
		// It is possible that the filter points to a git subcommand, that is located at libexec\git-core
		Environment::g_Environment.AddToPath((ms_MsysGitRootDir / L"libexec\\git-core").native());

		auto* filter_p = git_filter_filter_new(sh.c_str(), Environment::g_Environment.put());
		if (git_filter_register("filter", filter_p, GIT_FILTER_DRIVER_PRIORITY))
			return false;
	}
#endif

	m_bInitialized = true;
	return true;
}


BOOL CGit::CheckCleanWorkTree(const bool stagedOk /* false */) const {
	const wil::unique_git_repository repo(GetGitRepository());
	if (!repo)
		return FALSE;

	if (git_repository_head_unborn(repo.get()))
		return FALSE;

	git_status_options statusopt = GIT_STATUS_OPTIONS_INIT;
	statusopt.show = stagedOk ? GIT_STATUS_SHOW_WORKDIR_ONLY : GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	statusopt.flags = GIT_STATUS_OPT_UPDATE_INDEX | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;

	wil::unique_git_status_list status;
	if (git_status_list_new(&status, repo.get(), &statusopt))
		return FALSE;

	return (0 == git_status_list_entrycount(status.get()));
}


BOOL CGit::IsResultingCommitBecomeEmpty(const bool amend /* = false */) {
	// "HEAD~1" is one revision, so the suffix is part of the element rather than
	// something appended to the command line.
	return GitCall::Run(wil::ArgvData{ L"git.exe", L"diff-index", L"--cached", L"--quiet", amend ? L"HEAD~1" : L"HEAD", L"--" }) == 0;
}


int CGit::HasWorkingTreeConflicts(git_repository* repo /* = nullptr */) const {
prologoue:
	[[maybe_unused]] wil::unique_git_repository def_repo;
	if (!repo) {
		def_repo = GetGitRepository();
		if (!def_repo)
			return GFail;
		repo = def_repo.get();
	}
body:
	wil::unique_git_index index;
	if (git_repository_index(&index, repo))
		return GFail;

	return git_index_has_conflicts(index.get());
}


bool CGit::IsFastForward(const CStringW& from, const CStringW& to, CGitHash* commonAncestor) const {
	const wil::unique_git_repository repo(GetGitRepository());
	if (!repo)
		return false;

	CGitHash fromHash, toHash, baseHash;
	if (GetHash(repo.get(), toHash, FixBranchName(to).GetBuffer()))
		return false;

	if (GetHash(repo.get(), fromHash, FixBranchName(from).GetBuffer()))
		return false;

	git_oid baseOid;
	if (git_merge_base(&baseOid, repo.get(), toHash, fromHash))
		return false;

	baseHash = baseOid;

	if (commonAncestor)
		*commonAncestor = baseHash;

	return fromHash == baseHash;
}


int CGit::GetOneFile(const CStringW& Refname, const CTGitPath& path, const CStringW& outputfile) const {
	// The other branch here is gitdll, not git.exe, so this gate stays: it GitCall::Runs
	// git's own checkout and with it the configured smudge filters, which
	// git_blob_filter below does not - that is the difference on an LFS repository.
	if (UsingLibGit2(GIT_CMD_GETONEFILE) || !g_Git.m_IsUseGitDLL) {
		const wil::unique_git_repository repo(GetGitRepository());
		if (!repo)
			return -1;

		CGitHash hash;
		const auto tmp = std::wstring{ Refname } + L"^{}";
		if (GetHash(repo.get(), hash, tmp)) // add ^{} in order to dereference signed tags
			return -1;

		wil::unique_git_commit commit;
		if (git_commit_lookup(&commit, repo.get(), hash))
			return -1;

		wil::unique_git_tree tree;
		if (git_commit_tree(&tree, commit.get()))
			return -1;

		wil::unique_git_tree_entry entry;
		if (const auto ret = git_tree_entry_bypath(&entry, tree.get(), CUnicodeUtils::StdGetUTF8(path.GetGitPathString()).c_str()); ret)
			return ret;

		if (git_tree_entry_filemode(entry.get()) == GIT_FILEMODE_COMMIT) {
			git_error_set_str(GIT_ERROR_NONE, "The requested object is a submodule and not a file.");
			return -1;
		}

		wil::unique_git_blob blob;
		if (git_tree_entry_to_object(reinterpret_cast<git_object**>(&blob), repo.get(), entry.get()))
			return -1;

		const wil::unique_file file{ _wfsopen(outputfile, L"wb", SH_DENYWR) };
		if (!file) {
			git_error_set_str(GIT_ERROR_NONE, "Could not create file.");
			return -1;
		}
		wil::unique_git_buf buf;
		git_blob_filter_options opts = GIT_BLOB_FILTER_OPTIONS_INIT;
		opts.flags &= ~static_cast<uint32_t>(GIT_BLOB_FILTER_CHECK_FOR_BINARY);
		if (git_blob_filter(&buf, blob.get(), CUnicodeUtils::StdGetUTF8(path.GetGitPathString()).c_str(), &opts))
			return -1;
		if (fwrite(buf.ptr, sizeof(char), buf.size, file.get()) != buf.size) {
			git_error_set_str(GIT_ERROR_OS, "Could not write to file.");
			return -1;
		}

		return 0;
	}

	CAutoLocker _(g_Git.m_critGitDllSec);
	try {
		g_Git.CheckAndInitDll();
		const CStringA ref = CUnicodeUtils::GetUTF8<CStringA>(Refname);
		const CStringA patha = CUnicodeUtils::StdGetUTF8(path.GetGitPathString()).c_str();
		auto outa = CUnicodeUtils::GetUTF8<CStringA>(outputfile);
		::DeleteFile(outputfile);
		return git_checkout_file(ref, patha, CStrBufA(outa));
	} catch (const std::exception& ex) {
		gitLastErr = L"gitdll.dll reports: " + CUnicodeUtils::GetUnicode<CStringW>(ex.what());
		return -1;
	} catch (...) {
		gitLastErr = L"An unknown gitdll.dll error occurred.";
		return -1;
	}
}


int CGit::GetUnifiedDiff(const CTGitPath& path,
                         const CStringW& rev1,
                         const CStringW& rev2,
                         CStringW patchfile,
                         const bool bMerge,
                         const bool bCombine,
                         const int diffContext,
                         const bool bNoPrefix) {
	// The remaining git.exe path is not a fallback but a capability fork, of the
	// same kind as the `--cc` in GitLogListAction: libgit2 has no combined-diff
	// support at all, so `-c` only ever had this implementation and always will.
	if (bCombine) {
		gitLastErr.clear();
		const auto argv = details::GetUnifiedDiffCmd(path, rev1, rev2, bMerge, bCombine, diffContext, bNoPrefix);
		auto&& ret = GitCall::RunLogFile(argv, patchfile);
		// GIT_CMD_DIFF is libgit2-only now, so GetGitLastErr(.., GIT_CMD_DIFF) reads
		// libgit2's error slot. Put git.exe's message there rather than leaving the
		// one path that still shells out reporting nothing.
		if (ret)
			git_error_set_str(GIT_ERROR_NONE, CUnicodeUtils::GetUTF8<CStringA>(gitLastErr));
		return ret;
	}

	const wil::unique_file file{ _wfsopen(patchfile, L"wb", SH_DENYRW) };
	if (!file)
		return -1;
	return details::GetUnifiedDiffLibGit2(path,
	                                      rev1,
	                                      rev2,
	                                      details::UnifiedDiffStatToFile,
	                                      details::UnifiedDiffToFile,
	                                      file.get(),
	                                      bMerge,
	                                      diffContext,
	                                      bNoPrefix);
}


int CGit::GetUnifiedDiff(const CTGitPath& path,
                         const CStringW& rev1,
                         const CStringW& rev2,
                         CStringA& buffer,
                         const bool bMerge,
                         const bool bCombine,
                         const int diffContext) {
	if (!bCombine) // see the note on the overload above
		return details::GetUnifiedDiffLibGit2(path, rev1, rev2, UnifiedDiffStatToStringA, UnifiedDiffToStringA, &buffer, bMerge, diffContext, false);

	std::wstring vector;
	auto&& ret = GitCall::Run(details::GetUnifiedDiffCmd(path, rev1, rev2, bMerge, bCombine, diffContext), &vector, nullptr);
	buffer.Append(CUnicodeUtils::GetUTF8<CStringA>(vector));
	return ret;
}


int CGit::GitRevert(const int parent, const CGitHash& hash) const {
	const wil::unique_git_repository repo(GetGitRepository());
	if (!repo)
		return -1;

	wil::unique_git_commit commit;
	if (git_commit_lookup(&commit, repo.get(), hash))
		return GFail;

	git_revert_options revert_opts = GIT_REVERT_OPTIONS_INIT;
	revert_opts.mainline = parent;
	return !git_revert(repo.get(), commit.get(), &revert_opts) ? G_OK : GFail;
}


int CGit::DeleteRef(const CStringW& reference) const {
	const wil::unique_git_repository repo(GetGitRepository());
	if (!repo)
		return GFail;

	CStringA refA;
	if (StringUtils::EndsWith(reference, L"^{}"))
		refA = CUnicodeUtils::GetUTF8<CStringA>(reference.Left(reference.GetLength() - static_cast<int>(wcslen(L"^{}"))));
	else
		refA = CUnicodeUtils::GetUTF8<CStringA>(reference);

	wil::unique_git_reference ref;
	if (git_reference_lookup(&ref, repo.get(), refA))
		return GFail;

	int result;
	if (git_reference_is_tag(ref.get()))
		result = git_tag_delete(repo.get(), git_reference_shorthand(ref.get()));
	else if (git_reference_is_branch(ref.get()))
		result = git_branch_delete(ref.get());
	else if (git_reference_is_remote(ref.get()))
		result = git_branch_delete(ref.get());
	else
		result = git_reference_delete(ref.get());

	return result;
}


// This lived in src\TortoiseProc\CGit_2.cpp, which existed only because its
// git.exe fallback used CMassiveGitTaskBase - an MFC-aware header that a file
// compiled by TortoiseShell and TGitCache cannot reach. With the fallback gone
// so is the reason for the split.
int CGit::DeleteRemoteRefs(const CStringW& sRemote, const std::vector<std::wstring>& list) const {
	const wil::unique_git_repository repo(GetGitRepository());
	if (!repo)
		return -1;

	const CStringA remoteA = CUnicodeUtils::GetUTF8<CStringA>(sRemote);
	wil::unique_git_remote remote;
	if (git_remote_lookup(&remote, repo.get(), remoteA) < 0)
		return -1;

	git_push_options pushOpts = GIT_PUSH_OPTIONS_INIT;
	git_remote_callbacks& callbacks = pushOpts.callbacks;
	callbacks.credentials = g_Git2CredCallback;
	callbacks.certificate_check = g_Git2CheckCertificateCallback;
	std::vector<std::string> refspecs;
	refspecs.reserve(list.size());
	std::ranges::transform(list, std::back_inserter(refspecs), [](const auto& ref) { return CUnicodeUtils::StdGetUTF8(L":" + ref); });

	auto vc = std::views::transform(refspecs, [](auto& s) { return s.data(); }) | std::ranges::to<std::vector>();
	const auto specs = git_strarray{ .strings = vc.data(), .count = vc.size() };

	if (git_remote_push(remote.get(), &specs, &pushOpts) < 0)
		return -1;

	return 0;
}


int CGit::GetWorkingTreeChanges(CTGitPathList& result,
                                bool amend,
                                const CTGitPathList* filterlist,
                                bool includedStaged /* = false */,
                                bool getStagingStatus /* = false */) {
	if (IsInitRepos())
		return GetInitAddList(result, getStagingStatus);

	std::wstring out;

	int count = filterlist ? filterlist->GetCount() : 1;
	Ensures(count > 0);

	const auto head = amend ? L"HEAD~1"s : L"HEAD"s;
	for (int i = 0; i < count; ++i)
	{
		_ASSERTE(!filterlist || !(*filterlist)[i].GetGitPathString().empty()); // pathspec must not be empty, be compatible with Git >= 2.16.0
		// The pathspec that used to be a pre-quoted " %s" appendix. Kept as a
		// vector rather than an optional string so appending it is the same
		// expression whether or not there is one.
		std::vector<std::wstring> pathspec;
		if (filterlist)
			pathspec.emplace_back((*filterlist)[i].GetGitPathString());

		if (ms_bCygwinGit || ms_bMsys2Git) {
			// Prevent showing all files as modified just because of different stats, when using cygwin's git
			GitCall::Run(wil::ArgvData{ L"git.exe", L"status", L"--no-ahead-behind", L"--" } << pathspec);
		}

		// also list staged files which will be in the commit
		std::wstring cmdout;
		auto argv1 = wil::ArgvData{ L"git.exe", L"diff-index", L"--cached", L"--raw", head, L"--numstat", L"-C", L"-M", L"-z", L"--" } << pathspec;
		GitCall::Run(argv1, &cmdout, nullptr);

		auto argv = wil::ArgvData{
			L"git.exe", L"diff-index", L"--raw", head, L"--numstat",
			std::format(L"-C{}%", ms_iSimilarityIndexThreshold),
		    std::format(L"-M{}%", ms_iSimilarityIndexThreshold),
			L"-z", L"--",
		} << pathspec;
		//std::wstring cmdout;	// comcat with more lines
		std::wstring cmdErr;
		if (GitCall::Run(argv, &cmdout, &cmdErr)) {
			if (cmdErr.empty())
				cmdErr += std::format(L"[{}] exited with an error code, but did not output any error message", argv.to_cmd());
			MessageBox(nullptr, cmdErr.c_str(), L"TortoiseGit", MB_OK | MB_ICONERROR);
		}
		out.append({ cmdout.data(), cmdout.size() });
	}
	result.ParserFromLog(out);

	if (getStagingStatus) {
		// This will show staged files regardless of any filterlist, so that it has the same behavior that the commit window has when staging support is disabled
		std::wstring cmdStagedUnfilteredOut;
		GitCall::Run({ L"git.exe", L"diff-index", L"--cached", L"--raw", head, L"--numstat", L"-C", L"-M", L"-z", L"--" }, &cmdStagedUnfilteredOut, nullptr);

		CTGitPathList stagedUnfiltered;
		stagedUnfiltered.ParserFromLog(cmdStagedUnfilteredOut);

		std::wstring cmdUnstagedUnfilteredOut;
		GitCall::Run({ L"git.exe", L"diff-files", L"--raw", L"--numstat", L"-C", L"-M", L"-z", L"--" }, &cmdUnstagedUnfilteredOut, nullptr);

		CTGitPathList unstagedUnfiltered;
		unstagedUnfiltered.ParserFromLog(cmdUnstagedUnfilteredOut); // Necessary to detect partially staged files outside the filterlist

		// File shows up both in the output of diff-index --cached and diff-files: partially staged
		// File shows up only in the output of diff-index --cached: totally staged
		// File shows up only in the output of diff-files: totally unstaged
		// TODO: This is inefficient. It would be better if ParserFromLog received the output of each command
		// separately and did this processing there, dropping the new function UpdateStagingStatusFromPath entirely.
		for (int j = 0; j < stagedUnfiltered.GetCount(); ++j) {
			const std::wstring& path = stagedUnfiltered[j].GetGitPathString().c_str();
			if (unstagedUnfiltered.LookForGitPath(path))
				result.UpdateStagingStatusFromPath(path, CTGitPath::StagingStatus::PartiallyStaged);
			else
				result.UpdateStagingStatusFromPath(path, CTGitPath::StagingStatus::TotallyStaged);
		}
		for (int j = 0; j < unstagedUnfiltered.GetCount(); ++j) {
			const std::wstring& path = unstagedUnfiltered[j].GetGitPathString().c_str();
			if (!stagedUnfiltered.LookForGitPath(path))
				result.UpdateStagingStatusFromPath(path, CTGitPath::StagingStatus::TotallyUnstaged);
		}
		// make sure conflicted files show up as unstaged instead of partially staged
		for (int j = 0; j < result.GetCount(); ++j) {
			if (result[j].m_Action & CTGitPath::LOGACTIONS_UNMERGED)
				const_cast<CTGitPath&>(result[j]).m_stagingStatus = CTGitPath::StagingStatus::TotallyUnstaged;
		}
	}

	std::map<std::wstring, int> duplicateMap;
	for (int i = 0; i < result.GetCount(); ++i)
		duplicateMap.insert(std::pair<std::wstring, int>(result[i].GetGitPathString().c_str(), i));

	// handle delete conflict case, when remote : modified, local : deleted.
	for (int i = 0; i < count; ++i) {
		std::string cmdout;

		std::vector<std::wstring> argv{ L"git.exe", L"ls-files", L"-u", L"-t", L"-z" };
		if (filterlist)
			argv.insert(argv.cend(), { L"--", (*filterlist)[i].GetGitPathString() }); // already checked above

		GitCall::Run(argv, &cmdout);

		CTGitPathList conflictlist;
		conflictlist.ParserFromLsFile(cmdout);
		for (int j = 0; j < conflictlist.GetCount(); ++j) {
			auto existing = duplicateMap.find(conflictlist[j].GetGitPathString().c_str());
			if (existing != duplicateMap.end()) {
				CTGitPath& p = const_cast<CTGitPath&>(result[existing->second]);
				p.m_Action |= CTGitPath::LOGACTIONS_UNMERGED;
			}
			else {
				// should we ever get here?
				_ASSERTE(false);
				result.AddPath(conflictlist[j]);
				duplicateMap.insert(std::pair<std::wstring, int>(result[i].GetGitPathString().c_str(), result.GetCount() - 1));
			}
		}
	}

	static bool useOldLSFilesDBehaviorKS = Registry::CRegDWORD(L"Software\\TortoiseGit\\OldLSFilesDBehaviorKS", FALSE, false, HKEY_LOCAL_MACHINE) == TRUE;
	// TODO: remove kill-switch
	// handle source files of file renames/moves (issue #860)
	// if a file gets renamed and the new file "git add"ed, diff-index doesn't list the source file anymore
	for (int i = 0; i < count; ++i) {
		std::string cmdout;

		wil::ArgvData argv;
		if (!useOldLSFilesDBehaviorKS)
			argv = { L"git.exe", L"diff", L"--name-only", L"--diff-filter=D", L"-z" };
		else
			argv = { L"git.exe", L"ls-files", L"-d", L"-z" };
		if (filterlist) {
			argv << L"--" << (*filterlist)[i].GetWString();
		}

		GitCall::Run(argv, &cmdout);

		CTGitPathList deletelist;
		deletelist.ParserFromLsFileSimple(cmdout, CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING);
		for (int j = 0; j < deletelist.GetCount(); ++j) {
			auto existing = duplicateMap.find(deletelist[j].GetGitPathString().c_str());
			if (existing == duplicateMap.end()) {
				result.AddPath(deletelist[j]);
				duplicateMap.insert(std::pair<std::wstring, int>(result[i].GetGitPathString().c_str(), result.GetCount() - 1));
			} else {
				auto p = const_cast<CTGitPath&>(result[existing->second]);
				p.m_Action |= CTGitPath::LOGACTIONS_MISSING;
				result.m_Action |= CTGitPath::LOGACTIONS_MISSING;
			}
		}
	}

	return 0;
}


void CGit::GetBisectTerms(CStringW* good, CStringW* bad) const {
	static CStringW lastGood;
	static CStringW lastBad;
	static fs::path lastDir;
	static ULONGLONG lastRead = 0;

	// TODO: oy vei zmir
	[[maybe_unused]] const auto publish = gsl::finally([good, bad] {
		if (bad)
			*bad = lastBad;
		if (good)
			*good = lastGood;
	});

	// add some caching here, because this method might be called multiple times in a short time from LogDlg and RevisionGraph
	// as we only read a small file the performance effects should be negligible.
	// Keyed on the directory as well as the time: the terms belong to one repository,
	// and a time-only key answered for the previous one when the directory changed
	// within the window - which the unit tests, switching repositories per test, did.
	if (lastDir == m_CurrentDir && lastRead + 5000 > GetTickCount64())
		return;
	lastDir = m_CurrentDir;
	lastRead = 0; // only a successful read below is worth caching

	lastGood = L"good";
	lastBad = L"bad";

	const auto adminDir = GitAdminDir::GetAdminDirPath(m_CurrentDir.native());
	if (!adminDir)
		return;

	const std::wstring termsFile = adminDir->path + L"BISECT_TERMS";
	// _wfsopen, like the other readers here; _wfopen_s would need a FILE** out of the owner.
	const wil::unique_file fp{ _wfsopen(termsFile.c_str(), L"rb", SH_DENYWR) };
	if (!fp)
		return;
	// one term per line; fgets keeps the '\n', which is not part of the term
	const auto readTerm = [&fp] {
		char termA[MAX_PATH] = {};
		fgets(termA, sizeof(termA), fp.get());
		std::string_view term{ termA };
		if (term.ends_with('\n'))
			term.remove_suffix(1);
		return CUnicodeUtils::GetUnicode<CStringW>(term);
	};
	lastBad = readTerm();
	lastGood = readTerm();
	lastRead = GetTickCount64();
}


int CGit::GetGitVersion(CStringW* versiondebug, CStringW* errStr) {
	CStringW version;
	if (GitCall::Run({ L"git.exe", L"--version" }, &version, errStr)) {
		return GFail;
	}

	int ver = 0;
	if (versiondebug)
		*versiondebug = version;

	try {
		int start = 0;
		CStringW str = version.Tokenize(L".", start);
		const int space = str.ReverseFind(L' ');
		str = str.Mid(space + 1, start);
		ver = _wtol(str);
		ver <<= 24;

		version = version.Mid(start);
		start = 0;

		str = version.Tokenize(L".", start);
		ver |= (_wtol(str) & 0xFF) << 16;

		str = version.Tokenize(L".", start);
		ver |= (_wtol(str) & 0xFF) << 8;

		str = version.Tokenize(L".", start);
		ver |= (_wtol(str) & 0xFF);
	} catch (...) {
		if (!ver)
			return GFail;
	}

	return ver;
}


int CGit::GetGitNotes(const CGitHash& hash, CStringW& notes) const {
	const wil::unique_git_repository repo(GetGitRepository());
	if (!repo)
		return -1;

	wil::unique_git_note note;
	auto&& ret = git_note_read(&note, repo.get(), nullptr, hash);
	if (ret == GIT_ENOTFOUND) {
		notes.Empty();
		return 0;
	}
	else if (ret)
		return -1;
	notes = CUnicodeUtils::GetUnicode<CStringW>(std::string_view{git_note_message(note.get())});
	return 0;
}


int CGit::SetGitNotes(const CGitHash& hash, const CStringW& notes) const {
	const wil::unique_git_repository repo(GetGitRepository());
	if (!repo)
		return -1;

	wil::unique_git_signature signature;
	if (git_signature_default(&signature, repo.get()) < 0)
		return -1;

	git_oid oid;
	if (git_note_create(&oid, repo.get(), nullptr, signature.get(), signature.get(), hash, CUnicodeUtils::GetUTF8<CStringA>(notes), 1) < 0)
		return -1;

	return 0;
}


int CGit::GetTagInfo(const CStringW& tagName, CStringW& output, const time_formater_f& timeformatter) const {
	if (m_IsUseLibGit2) {
		const wil::unique_git_repository repo{ g_Git.GetGitRepository() };
		if (!repo)
			return -1;
		wil::unique_git_reference ref;
		if (git_reference_lookup(&ref, repo.get(), CUnicodeUtils::GetUTF8<CStringA>(tagName)) < 0) {
			output = TGitH::GetLibGit2LastErr();
			return -1;
		}
		wil::unique_git_object obj;
		if (git_object_lookup(&obj, repo.get(), git_reference_target(ref.get()), GIT_OBJECT_TAG)) {
			output = TGitH::GetLibGit2LastErr();
			return -1;
		}
		wil::unique_git_tag tag;
		if (git_tag_lookup(&tag, repo.get(), git_reference_target(ref.get())) < 0) {
			output = TGitH::GetLibGit2LastErr();
			return -1;
		}
		output = L"tag " + CUnicodeUtils::GetUnicode<CStringW>(std::string_view{git_tag_name(tag.get())}) + L"\n";
		const auto tagger = git_tag_tagger(tag.get());
		if (tagger) {
			output += "tagger ";
			if (tagger->name) {
				output += CUnicodeUtils::GetUnicode<CStringW>(std::string_view{tagger->name});
				output += L' ';
			}
			if (tagger->email) {
				output += L'<';
				output += CUnicodeUtils::GetUnicode<CStringW>(std::string_view{tagger->email});
				output += L'>';
				output += L' ';
			}
			output += timeformatter(tagger->when.time);
			output += L'\n';
		}
		output += L'\n';
		output += CUnicodeUtils::GetUnicode<CStringW>(std::string_view{git_tag_message(tag.get())});
		output.TrimRight().AppendChar(L'\n');
		return 0;
	}

	output.Empty();
	if (GitCall::Run({ L"git.exe", L"cat-file", L"tag", L"--", tagName.GetString() }, &output) != G_OK)
		return GFail;

	if (StringUtils::StartsWith(output, L"object ")) {
		if (const int pos = output.Find(L"\n"); pos > 0) {
			output.Delete(0, pos + 1);
			if (StringUtils::StartsWith(output, L"type commit\n"))
				output.Delete(0, static_cast<int>(wcslen(L"type commit\n")));
		}
	}

	// parse tag date
	// this assumes that in the header of the tag there is no ">" before the "tagger " header entry
	int pos1 = output.Find(L'>');
	if (pos1 < 0)
		return 0;
	++pos1;
	if (output[pos1] == L' ')
		++pos1;
	const int pos2 = output.Find(L'\n', pos1);
	if (pos2 < 0)
		return 0;

	CStringW str = output.Mid(pos1, pos2 - pos1);
	wchar_t* pEnd = nullptr;
	errno = 0;
	const auto number = wcstoumax(str.GetBuffer(), &pEnd, 10);
	if (str.GetBuffer() == pEnd)
		return 0;
	if (errno == ERANGE)
		return 0;
	output.Delete(pos1, pos2 - pos1);
	output.Insert(pos1, timeformatter(number));

	output.Trim().AppendChar(L'\n');
	return 0;
}


int GetSubmodulePointer(CGit::SubmoduleInfo& submoduleinfo) {
	submoduleinfo.Empty();

	if (wil::reg::try_get_dword(HKEY_CURRENT_USER, Registry::TortoiseGitKey, L"LogShowSuperProjectSubmodulePointer").value_or(1u) != 1u)
		return 0;

	const auto& currentDir = GitCall::m_CurrentDirStorage.native();
	if (GitAdminDir::IsBareRepo(currentDir))
		return -1;
	const auto superprojectRoot = GitAdminDir::HasAdminDir(currentDir);
	if (superprojectRoot.empty())
		return -1;

	const wil::unique_git_repository repo = TGitH::OpenGitRepository(superprojectRoot.native());
	if (!repo)
		return -1;
	wil::unique_git_index index;
	if (git_repository_index(index.put(), repo.get()))
		return -1;

	// past the separator, unless the root already ends in one ("C:\")
	const auto& root = superprojectRoot.native();
	const size_t skip = root.ends_with(L'\\') ? root.size() : root.size() + 1;
	// CString::Right clamped to "", substr throws: the root itself has no
	// submodule path, and "" was never found in the index either
	if (currentDir.size() <= skip)
		return -1;
	const std::string submodulePath = TGitPath::ToGitPathA(std::wstring_view{ currentDir }.substr(skip));

	// if current submodule is in conflict state, return the relevant hashes
	const git_index_entry* ancestor{};
	const git_index_entry* our{};
	const git_index_entry* their{};
	if (!git_index_conflict_get(&ancestor, &our, &their, index.get(), submodulePath.c_str())) {
		if (our)
			submoduleinfo.mergeconflictMineHash = our->id;
		if (their)
			submoduleinfo.mergeconflictTheirsHash = their->id;

		const CTGitPath superProject{ superprojectRoot };
		if (superProject.IsRebaseActive()) {
			submoduleinfo.mineLabel = L"super-project-rebase-head";
			submoduleinfo.theirsLabel = L"super-project-head";
		}
		else {
			submoduleinfo.mineLabel = L"super-project-head";
			submoduleinfo.theirsLabel = L"super-project-merge-head";
		}
		return 0;
	}

	// determine hash of submodule
	const git_index_entry* entry = git_index_get_bypath(index.get(), submodulePath.c_str(), 0);
	if (!entry)
		return -1;

	submoduleinfo.superProjectHash = entry->id;
	return 0;
}
} // namespace TGitH

export using namespace TGitH;
// NOLINTEND(readability-convert-member-functions-to-static, cppcoreguidelines-narrowing-conversions, readability-non-const-parameter)
