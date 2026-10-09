module;
#include <strsafe.h>
#include <atlstr.h>
#include <atlbase.h>
#include <gitdll.h>
#include <git2.h>

#include <git2/sys/errors.h>
#include <git2/sys/filter.h>
#include <git2/sys/transport.h>

#include "libgit2/ssh-wintunnel.h"
#include "libgit2/filter-filter.h"



export module git_h;
import std;
import gsl;

using std::operator""s;
using std::operator""sv;
using TGString = std::wstring;
using TGStringView = std::wstring_view;
using std::filesystem::path;
namespace fs = std::filesystem;

import wil;
using namespace wil;


import DebugOutput;
import GitHash;
import PathUtils;
import StringUtils;
import TGitPath;
import invarients;
import libgit2;
import Registry;
import tgittypes;
import wil_git2;
import wstr;

using namespace TGitPath;
using namespace tgit::types;
using TGitHash::CGitHash;
using tgit::zstr::StringView;
using wil::CFormatMessageWrapper;
using tgit::DebugOutput::CTraceToOutputDebugString;
using SmartLibgit2::CAutoRepository;
using SmartLibgit2::CAutoObject;
namespace GitFormatHolder = tgit::invarients::GitFormatHolder;

// ReSharper disable once CppEnforceCVQualifiersPlacement
using ArgvP = char const*;


#define STR2(x) #x
#define STR(x) STR2(x)
#define FILE_LINE __FUNCTION__ __FILE__ ":" STR(__LINE__)
#define HERE std::to_array(FILE_LINE)


consteval std::byte ConvertVersionToInt(const std::byte major, const std::byte minor, const std::byte patchlevel, const std::byte build = {})
{
	return (major << 24) | (minor << 16) | (patchlevel << 8) | build;
}

constexpr auto scope_flag(const CONFIG_TYPE t)
{
	switch (t)
	{
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


export namespace TGitH
{

using BITS32 = std::uint32_t;
using CAutoLocker = CComCritSecLock<CComCriticalSection>;


constexpr auto REG_MSYSGIT_PATH = L"MSysGit"sv;
constexpr auto REG_SYSTEM_GITCONFIGPATH = L"SystemConfig"sv;
constexpr auto REG_MSYSGIT_EXTRA_PATH = L"MSysGitExtra"sv;

constexpr auto G_OK = 0;
constexpr auto GFail = -1;
constexpr auto GIT_EXE_BUFFER_LIMIT = 50uz * 1024 * 1024;	// arbitrary chosen 50 MiB limit that should be large enough for all real life situations in remote situations
constexpr auto GIT_EXE_STDERR_BUFFER_LIMIT = 1024uz;		// arbitrary chosen 1 KiB limit that should be large enough for all real life situations in remote situations

constexpr auto DiffSimilarityIndexThreshold = L"DiffSimilarityIndexThreshold"sv;
constexpr auto CygwinHack = L"CygwinHack"sv;
constexpr auto Msys2Hack = L"Msys2Hack"sv;

#define REG_SOFTWARE_ROOT          L"SOFTWARE"
#define REG_SOFTWARE32_ROOT        REG_SOFTWARE_ROOT LR"(\Wow6432Node)"
#define REG_UNINSTALL_TGIT_SUBKEY  LR"(\Microsoft\Windows\CurrentVersion\Uninstall\Git_is1)"
constexpr auto UNINSTALL_REG_VALUE  = L"InstallLocation";
constexpr auto UNINSTALL_REG_SUBKEY	= std::wstring_view{ REG_SOFTWARE_ROOT   REG_UNINSTALL_TGIT_SUBKEY };
constexpr auto UNINSTALL_RGSK_WOW64 = std::wstring_view{ REG_SOFTWARE32_ROOT REG_UNINSTALL_TGIT_SUBKEY };


constexpr auto CALL_OUTPUT_READ_CHUNK_SIZE = 1024u;


enum LOG_ORDER
{
	LOG_ORDER_CHRONOLOGIALREVERSED,
	LOG_ORDER_TOPOORDER,
	LOG_ORDER_DATEORDER,
	LOG_ORDER_AUTHORDATEORDER,
};
enum BRANCH_TYPE
{
	BRANCH_LOCAL = 0x1,
	BRANCH_REMOTE = 0x2,
	BRANCH_FETCH_HEAD = 0x4,
	BRANCH_LOCAL_F = BRANCH_LOCAL | BRANCH_FETCH_HEAD,
	BRANCH_ALL = BRANCH_LOCAL | BRANCH_REMOTE,
	BRANCH_ALL_F = BRANCH_ALL | BRANCH_FETCH_HEAD,
};
enum LOG_INFO_MASK
{
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
enum REF_TYPE
{
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
enum LIBGIT2_CMD : std::uint32_t
{
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

// Only the commands that still fork at runtime appear here; the ones in
// LIBGIT2_ONLY_MASK are on whatever the registry says. GETONEFILE, GET_COMMIT
// and FOREACHREF are what is left of the old default list - the other five
// entries moved into LIBGIT2_ONLY_MASK, which is not a change of behaviour for
// anyone since they were on by default already.
constexpr BITS32 DEFAULT_USE_LIBGIT2_MASK = (
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
constexpr BITS32 LIBGIT2_ONLY_MASK = (
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


constexpr size_t CalculateDiffSimilarityIndexThreshold() noexcept
{
	const size_t index = wil::reg::get_value_dword(DiffSimilarityIndexThreshold);
	return (index <= 100) ? index : 50;
}

// the last msysgitdir added to the path, blank if none
CString ms_LastMsysGitDir;
CString ms_MsysGitRootDir;
int ms_LastMsysGitVersion = 0;
bool ms_bCygwinGit = wil::reg::get_value_typed<bool>(CygwinHack);
bool ms_bMsys2Git = wil::reg::get_value_typed<bool>(Msys2Hack);
size_t ms_iSimilarityIndexThreshold = CalculateDiffSimilarityIndexThreshold();
int m_LogEncode = CP_UTF8;
// this is a HACK to protect again excessive input from remote servers
bool s_limitGitExeOutput = false;



git_credential_acquire_cb g_Git2CredCallback;
git_transport_certificate_check_cb g_Git2CheckCertificateCallback;

template <std::array Name, bool isDll = true>
int LogError(const CString& out, const std::exception& ex)
{
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
constexpr auto LogError(CString out, std::exception ex, const bool isDll = true)
{
	[[maybe_unused]] const auto msg = std::format(
	"[TortoiseGit] {} -> {}:\n{}\n{}",
	FuncName,
	isDll ? "GitDll" : "CreateProcess",
	ex,
	out);
	return GFail;
}


CString GetLibGit2LastErr(const CString& msg = {})
{
	if (!msg.IsEmpty())
		return msg + L'\n' + GetLibGit2LastErr();

	if (const git_error* libgit2err = git_error_last())
	{
		const auto lastError = CUnicodeUtils::GetUnicode<CString>(std::string_view{libgit2err->message});
		tgit::libgit2::git_error_clear();
		return L"libgit2 returned: " + lastError;
	}
	else
		return L"An error occoured in libgit2, but no message is available.";
}


bool g_bSortLogical;
bool g_bSortLocalBranchesFirst;
bool g_bSortTagsReversed;

bool LogicalCompareSz(const LPCWSTR left, const LPCWSTR right)
{
	if (g_bSortLogical)
		return StrCmpLogicalW(left, right) < 0;
	return StrCmpI(left, right) < 0;
}



#define BUFSIZE 512
DWORD GetTortoiseGitTempPath(const DWORD nBufferLength, const LPWSTR lpBuffer)
{
	constexpr auto subdirectory = L"TortoiseGit\\"sv;
	constexpr auto extra = static_cast<DWORD>(subdirectory.size() + 1); // + the NUL: the 13 this used to spell out

	const DWORD result = ::GetTempPath(nBufferLength, lpBuffer);
	if (result == 0) return 0;
	if (!lpBuffer || (result + extra > nBufferLength))
	{
		if (lpBuffer)
			lpBuffer[0] = '\0';
		return result + extra;
	}

	// GetTempPath returned the length without its NUL, and the check above left room for the rest
	std::ranges::copy(subdirectory, lpBuffer + result);
	lpBuffer[result + subdirectory.size()] = L'\0';
	CreateDirectory(lpBuffer, nullptr);

	return result + extra;
}



CString GetTempFile()
{
	wchar_t lpPathBuffer[BUFSIZE] = {};
	const DWORD dwBufSize = BUFSIZE;
	wchar_t szTempName[BUFSIZE] = {};

	const auto dwRetVal = GetTortoiseGitTempPath(dwBufSize, lpPathBuffer);
	if (dwRetVal > dwBufSize || (dwRetVal == 0))
		return L"";

	// Create a temporary file.
	if (!GetTempFileName(lpPathBuffer,			// directory for tmp files
								TEXT("Patch"),	// temp file name prefix
								0,				// create unique name
								szTempName))	// buffer for name
									return L"";

	return CString(szTempName);
}
[[deprecated("Yuck! Use `CString GetTempFile()`")]]
void GetTempPath(CString &path)
{
	path = GetTempFile();
}



CString StripRefName(CString refName)
{
	if (StringUtils::StartsWith(refName, L"refs/heads/"))
		refName = refName.Mid(static_cast<int>(wcslen(L"refs/heads/")));
	else if (StringUtils::StartsWith(refName, L"refs/"))
		refName = refName.Mid(static_cast<int>(wcslen(L"refs/")));
	return refName.TrimRight();
}


bool LoadTextFile(const CString &filename, CString &msg)
{
	if (!PathFileExists(filename))
		return false;

	const wil::CAutoFile pFile{_wfsopen(filename, L"rb", SH_DENYWR)};
	if (!pFile)
	{
		::MessageBox(nullptr, L"Could not open " + filename, L"TortoiseGit", MB_ICONERROR);
		return true; // load no further files
	}

	CStringA str;
	do
	{
		char s[8196] = {};
		const int read = static_cast<int>(fread(s, sizeof(char), sizeof(s), pFile.get()));
		if (read == 0)
			break;
		str += CStringA(s, read);
	} while (true);
	msg += CUnicodeUtils::GetUnicode<CString>(str);
	msg.Replace(L"\r\n", L"\n");
	msg.TrimRight(L'\n');
	msg += L'\n';

	return true; // load no further files
}

// similar code in CTGitPath::ParserFromLsFile
int ParseConflictHashesFromLsFile(const BYTE_VECTOR& out, CGitHash& baseHash, bool& baseIsFile, CGitHash& mineHash, bool& mineIsFile, CGitHash& remoteHash, bool& remoteIsFile)
{
	size_t pos = 0;
	const size_t end = out.size();
	if (end == 0)
		return 1;
	while (pos < end)
	{
		const size_t lineStart = pos;

		if (out[pos] != 'M')
		{
			ATLASSERT(false && "this should never happen as this method should only be called for output of git ls-files -u -t -z");
			return -1;
		}

		pos = out.find(' ', pos); // advance to mode
		if (pos == CGitByteArray::npos)
			return -1;

		const size_t modeStart = pos + 1;
		pos = out.find(' ', modeStart); // advance to hash
		if (pos == CGitByteArray::npos)
			return -1;

		const size_t hashStart = pos + 1;
		pos = out.find(' ', hashStart); // advance to Stage
		if (pos == CGitByteArray::npos)
			return -1;

		const size_t stageStart = pos + 1;
		pos = out.find('\t', stageStart); // advance to filename
		if (pos == CGitByteArray::npos || stageStart - 1 - hashStart >= INT_MAX)
			return -1;

		++pos;
		const size_t fileNameEnd = out.find(0, pos);
		if (fileNameEnd == CGitByteArray::npos || fileNameEnd == pos || pos - lineStart != 52)
			return -1;

		std::string_view hash{ &out[hashStart], stageStart - 1 - hashStart };
		const long mode = strtol(&out[modeStart], nullptr, 10);
		const int stage = strtol(&out[stageStart], nullptr, 10);
		if (stage == 0)
			return -1;
		else if (stage == 1)
		{
			baseHash = CGitHash::FromHexStr(hash);
			baseIsFile = mode != 160000;
		}
		else if (stage == 2)
		{
			mineHash = CGitHash::FromHexStr(hash);
			mineIsFile = mode != 160000;
		}
		else if (stage == 3)
		{
			remoteHash = CGitHash::FromHexStr(hash);
			remoteIsFile = mode != 160000;
		}

		pos = out.findNextString(pos);
	}

	return 0;
}


constexpr std::int64_t filetime_to_time_t(std::int64_t winTime) noexcept
{
	winTime -= 116444736000000000LL; /* Windows to Unix Epoch conversion */
	winTime /= 10000000; /* Nano to seconds resolution */
	return winTime;
}

// lstat semantics, as git means them - see PathUtils::get_path_attributes
int GetFileModifyTime(const path& filename, std::int64_t* time, bool* isDir = nullptr, std::int64_t* size = nullptr, bool* isSymlink = nullptr)
{
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
		*isSymlink = attrs->type == fs::file_type::symlink;      // a junction is not a symlink
	return G_OK;
}

// legacy signature for raw wide-string callers
int GetFileModifyTime(LPCWSTR filename, std::int64_t* time, bool* isDir = nullptr, std::int64_t* size = nullptr, bool* isSymlink = nullptr)
{
	if (!filename)      // path(nullptr) is UB; GetFileAttributesEx(nullptr) merely failed
		return GFail;
	return GetFileModifyTime(path(filename), time, isDir, size, isSymlink);
}


namespace details {
int LogicalComparePredicate(const std::wstring& left, const std::wstring& right)
{
	return LogicalCompareSz(left.c_str(), right.c_str());
}

int LogicalCompareReversedPredicate(const std::wstring& left, const std::wstring& right)
{
	return LogicalComparePredicate(right, left);
}

// A REF_VECTOR sorts by ref name. That used to be spelled by handing the whole
// TGitRef to the CString predicates and letting `operator const CString&` pick
// the name out - which is to say, the sort key was invisible at the call site.
// Separate names rather than overloads: these are passed to std::sort by name,
// and an overload set cannot be deduced there.
bool LogicalCompareRefPredicate(const TGitRef& left, const TGitRef& right)
{
	return LogicalCompareSz(left.name.c_str(), right.name.c_str());
}

bool LogicalCompareRefReversedPredicate(const TGitRef& left, const TGitRef& right)
{
	return LogicalCompareRefPredicate(right, left);
}

int LogicalCompareBranchesPredicate(const std::wstring& left, const std::wstring& right)
{
	if (g_bSortLocalBranchesFirst)
	{
		const bool leftIsRemote = left.starts_with(L"remotes/");
		const bool rightIsRemote = right.starts_with(L"remotes/");

		if (leftIsRemote && !rightIsRemote)
			return false;
		else if (!leftIsRemote && rightIsRemote)
			return true;
	}
	return LogicalComparePredicate(left, right);
}
}


void UnifiedDiffStatToStringA(const git_buf* text, void* payload)
{
	ATLASSERT(payload && text);
	const auto str = static_cast<CStringA*>(payload);
	str->Append(text->ptr, CUnicodeUtils::SafeSizeToInt(text->size));
	str->AppendChar('\n');
}

int UnifiedDiffToStringA(const git_diff_delta * /*delta*/, const git_diff_hunk * /*hunk*/, const git_diff_line *line, void *payload)
{
	ATLASSERT(payload && line);
	const auto str = static_cast<CStringA*>(payload);
	if (line->origin == GIT_DIFF_LINE_CONTEXT || line->origin == GIT_DIFF_LINE_ADDITION || line->origin == GIT_DIFF_LINE_DELETION)
		str->Append(&line->origin, 1);
	str->Append(line->content, CUnicodeUtils::SafeSizeToInt(line->content_len));
	return 0;
}


/// A C-style (argc, argv) over strings it owns, for gitdll's git_open_log /
/// git_open_diff, which take `int argc, const char** argv` as git's own main does.
///
/// argv points into the std::strings in m_strings. Moving is safe - moving a
/// std::vector hands over its buffer, so the std::string objects (and any SSO
/// characters inside them) stay where they are - but copying is not, since a
/// copy's pointers would still point into the original. Hence move-only.
struct ArgvData
{
	ArgvData() = default;
	ArgvData(ArgvData&&) noexcept = default;
	ArgvData(const ArgvData&) = delete;
	ArgvData& operator=(const ArgvData&) = delete;
	ArgvData& operator=(ArgvData&&) = delete;

	/// Callers pass argv[0] themselves (usually ""), as they always have.
	/// Empty when the count would not fit an int argc.
	static ArgvData VectorToARGV(std::vector<std::string> args)
	{
		ArgvData result;
		if (args.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
			return result;
		result.m_strings = std::move(args);
		result.m_argv.reserve(result.m_strings.size() + 1);
		for (const auto& arg : result.m_strings)
			result.m_argv.push_back(arg.c_str());
		result.m_argv.push_back(nullptr); // argv[argc] == NULL, as the C runtime guarantees for main
		return result;
	}

	[[nodiscard]] int argc() const noexcept { return static_cast<int>(m_strings.size()); }
	const char** argv() noexcept { return m_argv.data(); }

	explicit operator bool() const noexcept { return !m_strings.empty(); }

private:
	std::vector<std::string> m_strings{};
	std::vector<ArgvP> m_argv{};
};

/**
 * SerializeArgv deliberately does not rewrite argument content, so a call site
 * that hands git a Windows path has to say so. Both callers of the two
 * functions below pass a temp file path with backslashes, and under
 * msys2/cygwin git those do not resolve - which is what the old non-relaxed
 * QuoteParameter's \ -> / conversion was quietly doing for them.
 */
std::wstring AsGitPath(const CString& path)
{
	std::wstring gitPath(path);
	std::replace(gitPath.begin(), gitPath.end(), L'\\', L'/');
	return gitPath;
}


class CFilterData
{
public:
	enum : std::uint8_t
	{
		SHOW_NO_LIMIT, // NOTE: no limitation does not mean "without all limitations", it's just without the following limitations. That say, the log still could be limited by author, committer, etc.
		SHOW_LAST_SEL_DATE,
		SHOW_LAST_N_COMMITS,
		SHOW_LAST_N_YEARS,
		SHOW_LAST_N_MONTHS,
		SHOW_LAST_N_WEEKS,
	};

	CFilterData()
	{
		m_From = m_To = -1;
		m_NumberOfLogsScale = SHOW_NO_LIMIT;
		m_NumberOfLogs = 1;
	}

	DWORD m_NumberOfLogsScale;
	DWORD m_NumberOfLogs;
	__time64_t m_From;
	__time64_t m_To;
};


class illegal_git_parameter
{
	CString m_error;

public:
	explicit illegal_git_parameter(const CString& error)
		: m_error(error)
	{}

	[[nodiscard]] const CString& cause() const { return m_error; }
};


class CGitCall
{
public:
	CGitCall() = default;
	explicit CGitCall(const CString& cmd)
		: m_Cmd(cmd)
	{}
	explicit CGitCall(const STRING_VECTOR& argv); // Out of line because it needs CGit::SerializeArgv, and CGit is declared below.
	virtual ~CGitCall() = default;

	[[nodiscard]] const CString& GetCmd() const { return m_Cmd; }

	// These functions are called when command output data is available.
	// When this function returns 'true' further output is ignored.
	virtual bool OnOutputData(std::string_view data) = 0;
	virtual bool OnOutputErrData(std::string_view data) = 0;
	virtual void OnEnd() {}
	void setLimit(const bool enableLimit) { m_maxLength = enableLimit ? GIT_EXE_BUFFER_LIMIT : std::numeric_limits<size_t>::max(); }
	[[nodiscard]] virtual bool wasLimited() const = 0;

private:
	CString m_Cmd;

protected:
	// Unlimited unless setLimit(true): only remote-facing calls opt in.
	size_t m_maxLength = std::numeric_limits<size_t>::max();
};


class CGitCall_ByteVector : public CGitCall
{
public:
	CGitCall_ByteVector(const CString& cmd, BYTE_VECTOR* pvector, BYTE_VECTOR* pvectorErr = nullptr)
		: CGitCall(cmd)
		, m_pvector(pvector)
		, m_pvectorErr(pvectorErr)
	{}

	bool OnOutputData(const std::string_view data) override
	{
		ATLASSERT(data.size() <= 1024); // data.size() cannot be larger than 1024, as RunAsync uses a buffer of that size
		if (!m_pvector)
			return true;
		if (m_pvector->size() >= m_maxLength) // this simple check is fine as it is intended to be a rough length limit for remote connections as a safety measure
			return true;
		m_pvector->append(data);
		return false;
	}

	bool OnOutputErrData(const std::string_view data) override
	{
		ATLASSERT(data.size() <= 1024); // data.size() cannot be larger than 1024, as RunAsync uses a buffer of that size
		if (!m_pvectorErr)
			return true;
		if (m_maxLength != std::numeric_limits<size_t>::max() && m_pvectorErr->size() > GIT_EXE_STDERR_BUFFER_LIMIT) // arbitrary length limit for remote connections as a safety measure
			return true;
		m_pvectorErr->append(data);
		return false;
	}
	BYTE_VECTOR* m_pvector{};
	BYTE_VECTOR* m_pvectorErr{};

	bool wasLimited() const override
	{
		if (!m_pvector)
			return false;
		return m_pvector->size() >= m_maxLength;
	}
};


template <typename GitReceiverFunc>
	requires std::is_convertible_v<GitReceiverFunc, std::function<void(std::string_view)>>
class CGitCallCb : public CGitCall
{
	size_t m_received = 0;
	GitReceiverFunc m_recv;
	BYTE_VECTOR m_buffer{};
	BYTE_VECTOR* m_pvectorErr{};

public:
	using CallbackType = GitReceiverFunc;

	CGitCallCb(const CString& cmd, const GitReceiverFunc recv, BYTE_VECTOR* p_VectorErr = nullptr)
		: CGitCall(cmd)
		, m_recv(recv)
		, m_pvectorErr(p_VectorErr)
	{}

	bool OnOutputData(const std::string_view data) override
	{
		ATLASSERT(data.size() <= 1024); // data.size() cannot be larger than 1024, as RunAsync uses a buffer of that size
		if (data.empty())
			return false;
		// a rough length limit for remote connections, as a safety measure: once reached, stop.
		// The count clamps at the limit rather than wrapping, even when the limit is "none" (SIZE_MAX).
		if (m_received >= m_maxLength)
			return true;
		m_received += (std::min)(data.size(), m_maxLength - m_received);
		m_buffer.append(data);
		m_buffer.append("\n");
		auto reader = std::istringstream(m_buffer.data());

		// Break into lines and feed to m_recv
		for (std::string line; std::getline(reader, line);)
		{
			if (line.empty())
				continue;
			m_recv(line);
		}
		m_buffer.clear();
		return false;
	}

	bool OnOutputErrData(const std::string_view data) override
	{
		ATLASSERT(data.size() <= 1024); // data.size() cannot be larger than 1024, as RunAsync uses a buffer of that size
		if (!m_pvectorErr)
			return true;
		if (m_maxLength != std::numeric_limits<size_t>::max() && m_pvectorErr->size() > GIT_EXE_STDERR_BUFFER_LIMIT) // arbitrary length limit for remote connections as a safety measure
			return true;
		m_pvectorErr->append(data);
		return false;
	}

	void OnEnd() override
	{
		// do not send any incomplete lines
		m_buffer.clear(); // Just for sure
	}

	[[nodiscard]] bool wasLimited() const override
	{
		return m_received >= m_maxLength;
	}

};


/// Ordering for environment variable names. Windows resolves them
/// case-insensitively - PATH and Path are one variable - so that has to be a
/// property of the container rather than something every lookup remembers to do.
/// It also gives the serialized block the case-insensitive name order Windows'
/// own environment blocks come in, which the flat block lost the moment anything
/// was added to it.
struct EnvNameLess
{
	using is_transparent = void;
	[[nodiscard]] bool operator()(const std::wstring_view lhs, const std::wstring_view rhs) const noexcept
	{
		return tgit::wstr::CompareNoCase(lhs, rhs) < 0;
	}
};
using MAP_ENVIRONMENT = std::map<std::wstring, std::wstring, EnvNameLess>;



/**
 * The environment handed to CreateProcess and to gitdll.
 *
 * It is a map of name to value, because that is what an environment is. It used
 * to be the serialized form - a std::vector<wchar_t> of "name=value\0...\0\0" -
 * with every lookup a linear scan that re-parsed it, and SetEnv splicing
 * characters in and out of the middle by iterator.
 *
 * Three properties of the flat form are load-bearing and are preserved:
 *
 *  - `operator const LPWSTR*` returns the address of a member, not of the block.
 *    gitdll's git_init(const LPWSTR* env), the ssh subtransport and the filter
 *    driver all *retain* that address and dereference it later, so the extra
 *    indirection is how a later SetEnv() reaches them without re-registering.
 *    m_baseptr therefore has a stable address and is repointed at the freshly
 *    serialized block on every mutation. GitTest's CEnvironment test captures
 *    the pointer once at the top and re-checks it after every operation, which
 *    is the contract stated as clearly as a test can state it.
 *  - An empty environment serializes to nullptr, not to an empty block.
 *    CreateProcess reads nullptr as "inherit the parent's environment" and an
 *    empty block as "the child gets no environment at all"; the difference is a
 *    git.exe with no PATH.
 *  - Names are matched case-insensitively, and the spelling of the most recent
 *    SetEnv wins. The second half matters because the msys and cygwin builds run
 *    git through bash, and a shell is not as forgiving about name case as
 *    Windows is.
 */
class CEnvironment
{
public:
	CEnvironment() = default;
	CEnvironment(const CEnvironment& env)
		: m_vars(env.m_vars)
	{
		Serialize();
	}
	CEnvironment& operator=(const CEnvironment& env)
	{
		if (this != &env)
		{
			m_vars = env.m_vars;
			Serialize();
		}
		return *this;
	}
	// Deleted for the same reason they were before: m_baseptr's address is handed
	// out and retained, so an object that has been moved from would leave its
	// consumers pointing at an empty block.
	CEnvironment(CEnvironment&& env) = delete;
	CEnvironment& operator=(CEnvironment&& env) = delete;

	/// Merges the process's own environment in. Variables already set here win,
	/// which is what the flat block did by returning the first match on lookup.
	void CopyProcessEnvironment();
	[[nodiscard]] std::wstring GetEnv(std::wstring_view name) const;
	void SetEnv(std::wstring_view name, std::wstring_view value);
	/// Removes a variable. This used to be SetEnv(name, nullptr) - a deletion
	/// spelled as an assignment, and invisible at the ~20 call sites that use it.
	void UnsetEnv(std::wstring_view name);
	void AddToPath(std::wstring value);
	void clear();
	[[nodiscard]] bool empty() const { return m_vars.empty(); }
	[[nodiscard]] auto get_cp() const { return m_block.data(); }
	[[nodiscard]] auto get_cpp() { return reinterpret_cast<wchar_t**>(m_block.data()); }

private:
	void Serialize();

	MAP_ENVIRONMENT m_vars{};
	/// The serialized "name=value\0...\0\0" block, rebuilt on every mutation.
	std::vector<wchar_t> m_block{};
};

void CEnvironment::Serialize()
{
	m_block.clear();
	if (m_vars.empty())
	{
		return;
	}

	for (const auto& [name, value] : m_vars)
	{
		m_block.insert(m_block.end(), name.cbegin(), name.cend());
		m_block.push_back(L'=');
		m_block.insert(m_block.end(), value.cbegin(), value.cend());
		m_block.push_back(L'\0');
	}
	m_block.push_back(L'\0'); // the block's own terminator, after the last entry's
}

void CEnvironment::clear()
{
	m_vars.clear();
	m_vars.clear();
}

void CEnvironment::CopyProcessEnvironment()
{
	wchar_t* porig = GetEnvironmentStrings();
	for (const wchar_t* p = porig; *p; p += wcslen(p) + 1)
	{
		// Split on the first '=' that is not at offset 0. Windows keeps a hidden
		// per-drive working directory in each environment block as "=C:=C:\some\dir",
		// and splitting on the very first '=' would give every one of those the same
		// empty name - so a map would keep exactly one of them.
		const wchar_t* separator = wcschr(p + 1, L'=');
		if (!separator)
			continue;
		// emplace, not insert_or_assign: what is already set here wins, which is what
		// the flat block did by returning the first match on lookup.
		m_vars.emplace(std::wstring(p, separator - p), std::wstring(separator + 1));
	}
	FreeEnvironmentStrings(porig);

	Serialize();
}

std::wstring CEnvironment::GetEnv(const std::wstring_view name) const
{
	const auto it = m_vars.find(name);
	return it == m_vars.cend() ? std::wstring() : it->second;
}

void CEnvironment::SetEnv(const std::wstring_view name, const std::wstring_view value)
{
	// Erase before inserting rather than insert_or_assign, so the *passed* spelling
	// of the name survives when it differs in case from what is already stored. The
	// map is case-insensitive, so insert_or_assign would keep the older spelling,
	// and the cygwin/msys builds hand this block to a shell that is not as relaxed
	// about that as Windows is.
	m_vars.erase(std::wstring(name));
	m_vars.emplace(name, value);
	Serialize();
}

void CEnvironment::UnsetEnv(const std::wstring_view name)
{
	if (m_vars.erase(std::wstring(name)) == 0)
		return; // nothing removed, so the block cannot have changed
	Serialize();
}

void CEnvironment::AddToPath(std::wstring value)
{
	TrimRight(value, L"\\");
	if (value.empty())
		return;

	std::wstring path = GetEnv(L"PATH");
	TrimRight(path, L";");
	path += L';';

	// do not double add paths to %PATH%
	if (path.find(value + L';') != std::wstring::npos || path.find(value + L"\\;") != std::wstring::npos)
		return;

	path += value;

	SetEnv(L"PATH", path);
}



class CGit
{
protected:
	GIT_DIFF m_GitDiff = nullptr;
	GIT_DIFF m_GitSimpleListDiff = nullptr;

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
public:
#endif
	bool m_IsGitDllInited = false;
	// Directory g_gitObjectFormat was latched from. The latched format only describes
	// the repository at m_CurrentDir, so the two have to move together; keeping the
	// directory here lets CheckAndInitDll() notice when they have drifted apart.
	CString m_formatLatchedFor;

private:
	CString gitLastErr;

public:
	CComAutoCriticalSection m_critGitDllSec;
	bool m_IsUseGitDLL{};
	bool m_IsUseLibGit2{};
	DWORD m_IsUseLibGit2_mask;

	CEnvironment m_Environment;

	void ForceReInitDll()
	{
#ifdef TGITCACHE
		ATLASSERT("we should never get here");
#endif
		m_IsGitDllInited = false;
		CheckAndInitDll();
	}
	void CheckAndInitDll()
	{
#ifdef TGITCACHE
		ATLASSERT("we should never get here");
#endif
		// Re-init when the working copy changed, not only when the dll was never
		// initialized: g_gitObjectFormat describes the repository we latched it from,
		// so a stale latch would size object ids for the *previous* repository and
		// silently truncate or pad them. There is no valid state in which
		// m_CurrentDir and the latched format disagree.
		if (!m_IsGitDllInited || m_formatLatchedFor != m_CurrentDir)
		{
			git_init(m_Environment.get_cp());
			// latch the repository's object format for the whole process, cf. GitHash.h
			const auto isSHA1 = static_cast<git_oid_t>(git_get_hash_algo()) == GIT_OID_SHA1;
			GitFormatHolder::set(isSHA1 ? GIT_OID_SHA1 : GIT_OID_SHA256);
			m_formatLatchedFor = m_CurrentDir;
			m_IsGitDllInited = true;
		}
	}

	static int git_open_diff_ex(GIT_DIFF* diff, ArgvData& argv)
	{
		return git_open_diff(diff, argv.argc(), argv.argv());
	}

	static int git_open_log_ex(GIT_DIFF* m_DllGitLog, ArgvData& argv)
	{
		return git_open_log(m_DllGitLog, argv.argc(), argv.argv());
	}

	GIT_DIFF GetGitDiff()
	{
#ifdef TGITCACHE
		ATLASSERT("we should never get here");
#endif
		if (m_GitDiff)
			return m_GitDiff;

		// cf. GitRevLoglist::SafeFetchFullInfo
		static const std::string cParameter = std::format("-C{}%", ms_iSimilarityIndexThreshold);
		static const std::string mParameter = std::format("-M{}%", ms_iSimilarityIndexThreshold);
		auto argv = ArgvData::VectorToARGV({ "", cParameter, mParameter, "-r" });
		git_open_diff_ex(&m_GitDiff, argv);
		return m_GitDiff;
	}

	GIT_DIFF GetGitSimpleListDiff()
	{
#ifdef TGITCACHE
		ATLASSERT("we should never get here");
#endif
		if (m_GitSimpleListDiff)
			return m_GitSimpleListDiff;
		auto argv = ArgvData::VectorToARGV({ "", "-r" });
		git_open_diff_ex(&m_GitSimpleListDiff, argv);
		return m_GitSimpleListDiff;
	}

	BOOL CheckMsysGitDir(BOOL bFallback = TRUE);
	bool m_bInitialized = false;

	static_assert(LIBGIT2_CMD::LAST_VALUE < sizeof(DWORD) * 8, "too many flags for storing them in a DWORD bitfield");
	bool UsingLibGit2(const LIBGIT2_CMD cmd) const { return (1 << cmd) & LIBGIT2_ONLY_MASK ? true : (m_IsUseLibGit2 && (1 << cmd) & m_IsUseLibGit2_mask); }
	// callback type should be git_cred_acquire_cb
	// but it could be void* and a cast
	static void SetGit2CredentialCallback(git_credential_acquire_cb callback) { g_Git2CredCallback = callback; }
	static void SetGit2CertificateCheckCertificate(git_transport_certificate_check_cb callback) { g_Git2CheckCertificateCallback = callback; }

	[[nodiscard]] CString GetHomeDirectory() const;
	[[nodiscard]] CString GetGitLocalConfig() const;
	[[nodiscard]] CString GetGitGlobalConfig() const;
	[[nodiscard]] CString GetGitGlobalXDGConfig(bool returnDirectory = false) const;
	[[nodiscard]] CString GetGitSystemConfig() const;
	[[nodiscard]] unique_git_repository GetGitRepository() const;

	/**
	 * The configured SSH client, HKCU then HKLM, empty when none is set and the OpenSSH
	 * default applies. A stale path to the retired TortoiseGitPlink.exe reads as unset,
	 * so upgrades do not keep exporting a binary that no longer exists.
	 */
	static std::wstring GetConfiguredSshClient()
	{
		std::wstring sshclient = wil::reg::get_value_string(L"SSH");
		if (sshclient.empty())
			sshclient = wil::reg::get_value_string(L"SSH", L"", HKEY_LOCAL_MACHINE);

		// Installs from before TortoiseGitPlink was retired still point at it, and an upgrade does
		// not rewrite an existing value. Left alone it would be exported verbatim and every network
		// operation would fail to spawn. Only *our own* removed binary is cleared, and only when it
		// is really gone: TortoiseSVN ships a working TortoisePlink.exe, and someone who deliberately
		// pointed at that must keep it.
		if (!sshclient.empty() && !PathFileExists(sshclient.c_str()))
		{
			std::wstring exe = PathUtils::GetFileNameFromPath(sshclient);
			tgit::wstr::MakeLower(exe);
			if (exe == L"tortoisegitplink.exe" || exe == L"tortoiseplink.exe")
				sshclient.clear();
		}

		return sshclient;
	}
 	[[nodiscard]] CString GetNotesRef() const;
	static bool IsBranchNameValid(const CString& branchname);
	bool IsLocalBranch(const CString& shortName);
	bool IsBranchTagNameUnique(const CString& name);
	/**
	 * Checks if a branch or tag with the given name exists
	 *isBranch is true -> branch, tag otherwise
	 */
	bool BranchTagExists(const CString& name, bool isBranch = true);

	PROCESS_INFORMATION m_CurrentGitPi{};

	CGit();
	~CGit();
	// m_CurrentDir is a reference into *this, so an implicit copy would bind the
	// copy's member to the *source* object's storage and the two would silently
	// share a working directory. Nothing copies CGit today - g_Git and the handful
	// of `CGit subgit;` locals are all default-constructed - so deleting these
	// costs nothing and closes the hole before someone opens it.
	CGit(const CGit&) = delete;
	CGit& operator=(const CGit&) = delete;

	/**
	 * The invoke API, in two spellings of the same thing.
	 *
	 * The argv overloads are the ones to write new code against: an element is
	 * an element, and quoting happens once, in SerializeArgv. The CString
	 * overloads take a line that is *already* a command line - the caller has
	 * done its own quoting, historically with QuoteParameter at every site - and
	 * they exist because ~280 call sites still spell it that way. They are the
	 * legacy half of this migration, not a second supported style.
	 *
	 * The two cannot be collapsed by conversion: splitting a flat line back into
	 * argv is not something you can do correctly after the fact, which is the
	 * reason the flat form has to be retired site by site rather than adapted.
	 */
	int Run(const STRING_VECTOR& argv, CString* output, const int code) { return Run(SerializeArgvToCString(argv), output, code); }
	int Run(const STRING_VECTOR& argv, CString* output, CString* outputErr, const int code = CP_UTF8) { return Run(SerializeArgvToCString(argv), output, outputErr, code); }
	int Run(const STRING_VECTOR& argv, BYTE_VECTOR* byte_array, BYTE_VECTOR* byte_arrayErr = nullptr) { return Run(SerializeArgvToCString(argv), byte_array, byte_arrayErr); }
	template <typename GitReceiverFunc>
	int Run(const STRING_VECTOR& argv, GitReceiverFunc recv, CString* outputErr = nullptr) { return Run(SerializeArgvToCString(argv), recv, outputErr); }

	int Run(const CString& cmd, CString* output, int code);
	int Run(const CString& cmd, CString* output, CString* outputErr, int code);
	int Run(const auto& argv, auto* output, auto* outputErr = nullptr)
	{
		const auto cmd = SerializeArgv(argv);
		return Run(cmd, output, outputErr);
	}
	int Run(const CString& cmd, BYTE_VECTOR* vector, BYTE_VECTOR* vectorErr = nullptr);
	int Run(CGitCall& pcall);
	template <typename GitReceiverFunc>
	int Run(const CString& cmd, GitReceiverFunc recv, CString* outputErr = nullptr)
	{
		if (outputErr)
		{
			BYTE_VECTOR vectorErr;
			CGitCallCb callback(cmd, recv, &vectorErr);
			callback.setLimit(s_limitGitExeOutput);
			const int ret = Run(callback);
			if (!vectorErr.empty())
			{
				StringAppend(*outputErr, std::string_view(vectorErr.data(), vectorErr.size()));
				if (s_limitGitExeOutput && vectorErr.size() > GIT_EXE_STDERR_BUFFER_LIMIT)
					StringAppend(*outputErr, std::format(
											 "\n\n[Git.exe error output truncated by TortoiseGit at about {} KiB]",
											 GIT_EXE_STDERR_BUFFER_LIMIT / 1024));
			}
			if (s_limitGitExeOutput && callback.wasLimited())
				vectorErr.append(std::format("\n\n[Git.exe's output truncated by TortoiseGit at about {} MiB]", GIT_EXE_BUFFER_LIMIT / 1024 / 1024));
			return ret;
		}

		CGitCallCb callback(cmd, recv);
		callback.setLimit(s_limitGitExeOutput);
		return Run(callback);
	}

private:
	CComAutoCriticalSection m_critSecThreadMap;
	std::map<DWORD, HANDLE> m_AsyncReadStdErrThreadMap;
	static DWORD WINAPI AsyncReadStdErrThread(LPVOID lpParam);
	struct ASYNCREADSTDERRTHREADARGS
	{
		HANDLE fileHandle;
		CGitCall* pcall;
	};

public:
#ifdef _MFC_VER
	void KillRelatedThreads(CWinThread* thread);
#endif
	int RunAsync(CString cmd, PROCESS_INFORMATION& pi, HANDLE* hRead, HANDLE* hErrReadOut, const CString* StdioFile = nullptr);
	int RunAsync(const STRING_VECTOR& argv, PROCESS_INFORMATION& pi, HANDLE* hRead, HANDLE* hErrReadOut, const CString* StdioFile = nullptr) { return RunAsync(SerializeArgvToCString(argv), pi, hRead, hErrReadOut, StdioFile); }
	int RunLogFile(const CString& cmd, const CString& filename, CString* stdErr);
	int RunLogFile(const STRING_VECTOR& argv, const CString& filename, CString* stdErr) { return RunLogFile(SerializeArgvToCString(argv), filename, stdErr); }

	bool IsFastForward(const CString& from, const CString& to, CGitHash* commonAncestor = nullptr);

private:
	/**
	 * The single config reader. \a canonicalizeBool asks git.exe for --bool, which
	 * is what makes a valueless key ("[section]\nkey" with no =) read as true; the
	 * gitdll path ignores it and leaves the parsing to the caller.
	 *
	 * It is a *type* request, so it does not belong on the untyped public getter -
	 * GetConfigValueBool is its only user.
	 */
	CString ReadConfigValue(const CString& name, const CString& def, bool canonicalizeBool);

public:
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
	CString GetConfigValue(const CString& name, const CString& def = CString());
	bool GetConfigValueBool(const CString& name, bool def = false);
	int GetConfigValueInt32(const CString& name, int def = 0);

	template <auto FuncName = __FUNCSIG__>
	int SetConfigValue(const auto& key, const auto& value, const CONFIG_TYPE type = CONFIG_LOCAL)
	{
		auto out = CString{};
		auto ex = std::exception{};
		try
		{
			if (m_IsUseGitDLL)
			{
				[[maybe_unused]] CAutoLocker lock(m_critGitDllSec);

				// TODO: globalize this
				CheckAndInitDll();
				const auto ret = git_set_config(key.data(), nullptr, type);
				if (ret == EXIT_SUCCESS)
					return ret;
			}
			else
			{
				auto argv = value.empty() ?
							std::array{ "git.exe", "config", scope_flag(type), "--unset", "--end-of-options", key.data() } :
							std::array{ "git.exe", "config", scope_flag(type), "--end-of-options", key.data(), value.data() };
				const auto ret = Run(argv, &out, nullptr, CP_UTF8);
				// TODO: trace `argv` and `out`
				if (ret == EXIT_SUCCESS)
					return ret;
			}
		}
		catch (const std::exception& _ex)
		{
			ex = _ex;
		}
		return LogError<FuncName>(out, ex);
	}
	constexpr auto UnsetConfigValue(const auto& key, const auto& _, const CONFIG_TYPE type = CONFIG_LOCAL)
	{
		return SetConfigValue<__FUNCSIG__>(key, nullptr, type);
	}

	CString GetUserName();
	CString GetUserEmail();
	CString GetCommitterName();
	CString GetCommitterEmail();
	CString GetCurrentBranch(bool fallback = false);
	void GetRemoteTrackedBranch(const CString& localBranch, CString& remote, CString& branch);
	void GetRemoteTrackedBranchForHEAD(CString& remote, CString& branch);
	void GetRemotePushBranch(const CString& localBranch, CString& pushRemote, CString& pushBranch);
	// read current branch name from HEAD file, returns 0 on success, -1 on failure, 1 detached (branch name "HEAD" returned)
	static int GetCurrentBranchFromFile(const CString& sProjectRoot, CString& sBranchOut, bool fallback = false);
	/**
	Use this method only when the HEAD is exist.
	*/
	BOOL CheckCleanWorkTree(bool stagedOk = false);
	BOOL IsResultingCommitBecomeEmpty(bool amend = false);
	int DeleteRef(const CString& reference);


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
	bool SetCurrentDir(const CString& path, const bool submodule = false)
	{
		std::wstring topDir;
		bool b = GitAdminDir::HasAdminDir(tgit::zstr::StringView(path), submodule ? false : !!PathIsDirectory(path), &topDir);
		m_CurrentDirStorage = topDir.c_str();
		if (!b && GitAdminDir::IsBareRepo(tgit::zstr::StringView(path)))
		{
			m_CurrentDirStorage = path;
			b = true;
		}
		NormalizeCurrentDir();
		return b;
	}


	/**
	Use this method only if m_IsUseLibGit2 is used for fallbacks.
	If you directly use libgit2 methods, use GetLibGit2LastErr instead.
	*/
	CString GetGitLastErr(const CString& msg, const std::optional<LIBGIT2_CMD> cmd = std::nullopt)
	{
		// ReSharper disable once CppTooWideScope
		const bool using_lib_git2 = cmd.has_value() ? UsingLibGit2(cmd.value()) : this->m_IsUseLibGit2;
		if (using_lib_git2)
			return TGitH::GetLibGit2LastErr(msg);
		else if (gitLastErr.IsEmpty())
			return msg + L"\nUnknown git.exe error.";
		else
		{
			const CString lastError = gitLastErr;
			gitLastErr.Empty();
			return msg + L'\n' + lastError;
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
	void SetCurrentDirExact(const CString& path)
	{
		m_CurrentDirStorage = path;
		NormalizeCurrentDir();
	}

	/*
	 * Root of the working copy this process operates on. Equivalent to reading
	 * m_CurrentDir; kept as the spelling new code should use, and now returning a
	 * reference because the value can no longer be repointed by an assignment
	 * somewhere else in the expression.
	 */
	[[nodiscard]] const CString& GetCurrentDir() const { return m_CurrentDir; }

private:
	// Only the two setters above may name this. Everything else - some 400 read
	// sites - goes through the reference below.
	CString m_CurrentDirStorage;

	void NormalizeCurrentDir()
	{
		// "C:" is relative to that drive's own working directory, "C:\" is its root.
		// Applied by both setters so the two cannot disagree about a drive root; the
		// raw assignments this replaced only got it when they happened to go through
		// SetCurrentDir.
		if (m_CurrentDirStorage.GetLength() == 2 && m_CurrentDirStorage[1] == L':')
			m_CurrentDirStorage += L'\\';
	}

public:
	/*
	 * Reads exactly like the public member it replaces - `g_Git.m_CurrentDir` still
	 * compiles verbatim everywhere - but it is a reference to const, so every
	 * *write* is now a compile error that has to pick one of the two setters above.
	 *
	 * That gate is the whole point. The latched object format (GitObjectFormat.h)
	 * describes the repository this directory names, so a write that bypasses the
	 * setters can leave the two describing different repositories; the invalid state
	 * used to be merely representable, and now it has exactly two authors.
	 *
	 * Spelled as a reference to a private member rather than as a `const CString`
	 * written through a const_cast in the setters. The enforcement and the churn
	 * are identical, but modifying an object that was *declared* const is undefined
	 * behaviour rather than a trick - the optimizer is entitled to fold reads of it,
	 * and would be within its rights to serve a stale directory.
	 */
	const CString& m_CurrentDir = m_CurrentDirStorage;

	int GetRemoteList(STRING_VECTOR& list);
	int GetBranchList(STRING_VECTOR& list, int* current, BRANCH_TYPE type = BRANCH_LOCAL, bool skipCurrent = false);
	int GetTagList(STRING_VECTOR& list);
	int GetRefsCommitIsOn(STRING_VECTOR& list, const CGitHash& hash, bool includeTags, bool includeBranches, BRANCH_TYPE type = BRANCH_LOCAL) const;
	int GetRemoteRefs(const CString& remote, REF_VECTOR& list, bool includeTags, bool includeBranches);
	int DeleteRemoteRefs(const CString& remote, const STRING_VECTOR& list);
	int GetBranchDescriptions(MAP_STRING_STRING& map) const;
	int GuessRefForHash(CString& ref, const CGitHash& hash);
	int GetMapHashToFriendName(MAP_HASH_NAME& map);

	[[nodiscard]] CString DerefFetchHead() const;

	// FixBranchName():
	// When branchName == FETCH_HEAD, dereference it.
	// A selected branch name got from GetBranchList(), with flag BRANCH_FETCH_HEAD enabled,
	// should go through this function before it is used.
	CString FixBranchName_Mod(CString& branchName);
	CString FixBranchName(const CString& branchName);

	static std::vector<std::string> GetLogCmd(CString range, const TGitPath::CTGitPath* path, int InfoMask, const CFilterData* filter, int logOrderBy);
	int GetHash(CGitHash& hash, const std::wstring& friendname);
	static int GetHash(git_repository* repo, CGitHash& hash, std::wstring_view friendname, bool skipFastCheck = false);

	int GetSubmoduleHash(const CString& pathOfSubmodule, const CGitHash& revision, CGitHash& submoduleRevision, CString& err);

	static void StringAppend(CString& str, std::string_view, int code = CP_UTF8);

	int IsUnbornBranch(const CString& ref);
	/**
	Checks if HEAD points to an unborn branch
	This method assumes, that we already know that we are in a working tree.
	the formal argument is just to hold the res of GetHash;
	*/
	bool IsInitRepos(CGitHash hash = {}) { return GetHash(hash, L"HEAD") != 0 ? false : hash.IsEmpty(); }
	// Returns 0 if no conflict, if a conflict was found and -1 in case of a failure
	int HasWorkingTreeConflicts(git_repository* repo = nullptr) const;
	void GetBisectTerms(CString* good, CString* bad);
	int GetRefList(STRING_VECTOR& list);

	class SubmoduleInfo
	{
	public:
		CGitHash superProjectHash;
		CGitHash mergeconflictMineHash;
		CGitHash mergeconflictTheirsHash;
		CString mineLabel;
		CString theirsLabel;

		[[nodiscard]] bool AnyMatches(const CGitHash& hash) const
		{
			return !superProjectHash.IsEmpty() && superProjectHash == hash || !mergeconflictMineHash.IsEmpty() && mergeconflictMineHash == hash || !mergeconflictTheirsHash.IsEmpty() && mergeconflictTheirsHash == hash;
		}
		void Empty()
		{
			superProjectHash.Empty();
			mergeconflictMineHash.Empty();
			mergeconflictTheirsHash.Empty();
		}
	};

	int ApplyPatchToIndex(const CString& patchPath, CString* out);
	int ApplyPatchToIndexReverse(const CString& patchPath, CString* out);

	int RefreshGitIndex();
	int GetOneFile(const CString& Refname, const CTGitPath& path, const CString& outputfile);

	// Example: master -> refs/heads/master
	CString GetFullRefName(const CString& shortRefName);
	// Removes 'refs/heads/' or just 'refs'. Example: refs/heads/master -> master

	int GetCommitDiffList(const CString& rev1, const CString& rev2, CTGitPathList& outpathlist, CString& error, bool ignoreSpaceAtEol = false, bool ignoreSpaceChange = false, bool ignoreAllSpace = false, bool ignoreBlankLines = false);
	int GetInitAddList(CTGitPathList& outputlist, bool getStagingStatus = false);
	int GetWorkingTreeChanges(CTGitPathList& result, bool amend = false, const CTGitPathList* filterlist = nullptr, bool includedStaged = false, bool getStagingStatus = false);
	[[nodiscard]] static int GetShortHASHLength() { return 8; }

	int GetGitNotes(const CGitHash& hash, CString& notes);
	int SetGitNotes(const CGitHash& hash, const CString& notes);

	int GetTagInfo(const CString& tagName, CString& output, const std::function<CString(__time64_t)>& timeformatter);

	int GetUnifiedDiff(const CTGitPath& path, const CString& rev1, const CString& rev2, CString patchfile, bool bMerge, bool bCombine, int diffContext, bool bNoPrefix = false);
	int GetUnifiedDiff(const CTGitPath& path, const CString& rev1, const CString& rev2, CStringA& buffer, bool bMerge, bool bCombine, int diffContext);

	int GitRevert(int parent, const CGitHash& hash);

	int GetGitVersion(CString* versiondebug, CString* errStr);

	[[nodiscard]] CString CombinePath(const CString& path) const
	{
		if (path.IsEmpty())
			return m_CurrentDir;
		if (m_CurrentDir.IsEmpty())
			return path;
		return m_CurrentDir + (m_CurrentDir.Right(0) == L'\\' ? L"" : L"\\") + path;
	}

	// Exists so that CTGitPath's accessors can return std::wstring without every
	// CombinePath call site having to convert: a std::wstring does not reach the
	// CString overload implicitly, since that would need two user-defined
	// conversions.
	//
	// Constrained to std::wstring exactly, and that is not fussiness. Taking
	// std::wstring_view (or const std::wstring&) makes every existing
	// CombinePath(L"literal") call ambiguous, because a const wchar_t* converts
	// to CString and to the new parameter by one user-defined conversion each,
	// so neither wins. That is not hypothetical - it broke ProjectProperties,
	// GitSettings and CloneCommand the first time this was written. A
	// constrained template never enters overload resolution for a literal.
	template <typename T>
		requires std::same_as<std::remove_cvref_t<T>, std::wstring>
	CString CombinePath(const T& path) const
	{
		return CombinePath(CString(path.data(), SafeSizeToInt(path.size())));
	}

	[[nodiscard]] CString CombinePath(const CTGitPath& path) const
	{
		return CombinePath(path.GetWinPathString().c_str());
	}

	// CString CombinePath(const CTGitPath *path) const
	// {
	// 	Expects(path != nullptr);
	// 	return CombinePath(path->GetWinPathString().c_str());
	// }

	[[nodiscard]] static CString QuoteParameter(CString value, bool relaxed = false)
	{
		if (ms_bMsys2Git || ms_bCygwinGit)
		{
			if (!relaxed)
				value.Replace(L'\\', L'/');
			value.Replace(L"'", L"'\\''");
			return L'\'' + value + L'\'';
		}

		if (relaxed)
			return StringUtils::EscapeWindowsCliArguments(value);

		value.Replace(L'\\', L'/');

		if (value.FindOneOf(L"\"?*") != -1)
			throw illegal_git_parameter(L"The git.exe command could not be constructed. The following parameter contains a special character, such as a double quotation mark (\") or an asterisk (*), which is not supported by TortoiseGit (perhaps due to the default functionality being disabled):\n" + value);

		return L'"' + value + L'"';
	}

	// Same reason, and the same constraint, as the CombinePath overload above.
	template <tgit::zstr::IsRealString T>
		requires std::same_as<std::remove_cvref_t<T>, std::wstring>
	[[nodiscard]] static CString QuoteParameter(const T& value, bool relaxed = false)
	{
		return QuoteParameter(CString(value.data(), gsl::narrow<int>(value.size())), relaxed);
	}

	/**
	 * Turns an argv vector into the one flat command line Win32 insists on.
	 *
	 * This is the whole point of the argv overloads: CreateProcess has no argv
	 * API, so *someone* has to do this - and doing it here, once, is the
	 * difference between one implementation of the quoting rules and the ~256
	 * QuoteParameter calls scattered over the call sites it replaces.
	 *
	 * Two serializations, because RunAsync has two backends. Native git.exe gets
	 * MSDN's backslash/quote rules, read back by CommandLineToArgvW. For
	 * msys2/cygwin the line is written to a temp file and handed to bash.exe, so
	 * there it is shell *source* and gets POSIX single-quoting instead.
	 *
	 * Elements are quoted only when quoting changes the reading. That is not an
	 * optimization: RunAsync decides whether to prepend ms_LastMsysGitDir by
	 * testing whether the line starts with "git", so a serializer that quoted
	 * argv[0] unconditionally would silently resolve git through PATH instead.
	 */

	/**
	 * SerializeArgv in the spelling the flat overloads want. It will go away with
	 * them; SerializeArgv is the one that stays, because it is what a future
	 * wstring-native (or third-party) process launcher would call.
	 */

	[[nodiscard]] static std::wstring SerializeArgv(const STRING_VECTOR& argv)
	{
		return wil::ArgvToCommandLine(argv);
	}
	[[nodiscard]] static auto SerializeArgvToCString(const STRING_VECTOR& argv)
	{
		const std::wstring cmd = SerializeArgv(argv);
		return CString(cmd.data(), gsl::narrow<int>(cmd.size()));
	}
};


CGit g_Git;



namespace details {
int resolve_to_tree(git_repository *repo, const char *identifier, git_tree **tree)
{
	ATLASSERT(repo && identifier && tree);

	/* try to resolve identifier */
	CAutoObject obj;
	if (git_revparse_single(&obj, repo, identifier))
		return -1;

	if (obj == nullptr)
		return GIT_ENOTFOUND;

	int err = 0;
	switch (git_object_type(obj))
	{
	case GIT_OBJECT_TREE:
		*tree = reinterpret_cast<git_tree*>(obj.Detach());
		break;
	case GIT_OBJECT_COMMIT:
		err = git_commit_tree(tree, reinterpret_cast<git_commit*>(static_cast<git_object*>(obj)));
		break;
	default:
		err = GIT_ENOTFOUND;
	}

	return err;
}

int resolve_to_commit(git_repository* repo, const char* identifier, git_commit** commit)
{
	ATLASSERT(repo && identifier && commit);

	CAutoObject obj;
	if (git_revparse_single(&obj, repo, identifier))
		return -1;

	if (obj == nullptr)
		return GIT_ENOTFOUND;

	return git_object_peel(reinterpret_cast<git_object**>(commit), obj, GIT_OBJECT_COMMIT);
}

/* the tail every diff below shares. It is not done once at the end because a
   merge diffed with -m emits one stat block and one patch series per parent. */
int EmitUnifiedDiff(git_diff* diff, const std::function<void(const git_buf*, void*)>& statCallback, git_diff_line_cb callback, void* data)
{
	CAutoDiffStats stats;
	if (git_diff_get_stats(&stats, diff))
		return -1;
	CAutoBuf statBuf;
	if (git_diff_stats_to_buf(statBuf, stats, GIT_DIFF_STATS_FULL, 0))
		return -1;
	statCallback(statBuf, data);

	for (size_t i = 0; i < git_diff_num_deltas(diff); ++i)
	{
		CAutoPatch patch;
		if (git_patch_from_diff(&patch, diff, i))
			return -1;

		if (git_patch_print(patch, callback, data))
			return -1;
	}

	return 0;
}


void SetLibGit2SearchPath(const int level, const CString& value)
{
	const CStringA valueA = CUnicodeUtils::GetUTF8<CStringA>(value);
	git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, level, static_cast<LPCSTR>(valueA));
}

void SetLibGit2TemplatePath(const CString &value)
{
	const auto valueA = CUnicodeUtils::GetUTF8<CStringA>(value);
	git_libgit2_opts(GIT_OPT_SET_TEMPLATE_PATH, static_cast<LPCSTR>(valueA));
}

void UnifiedDiffStatToFile(const git_buf* text, void* payload)
{
	ATLASSERT(payload && text);
	const auto file = static_cast<FILE*>(payload);
	fwrite(text->ptr, 1, text->size, file);
	fwrite("\n", 1, 1, file);
}

int UnifiedDiffToFile(const git_diff_delta * /* delta */, const git_diff_hunk * /* hunk */, const git_diff_line * line, void *payload)
{
	ATLASSERT(payload && line);
	const auto file = static_cast<FILE*>(payload);
	if (line->origin == GIT_DIFF_LINE_CONTEXT || line->origin == GIT_DIFF_LINE_ADDITION || line->origin == GIT_DIFF_LINE_DELETION)
		fwrite(&line->origin, 1, 1, file);
	fwrite(line->content, 1, line->content_len, file);
	return 0;
}

/* use libgit2 get unified diff */
int GetUnifiedDiffLibGit2(
	const CTGitPath& path,
	const CString& revOld,
	const CString& revNew,
	std::function<void(const git_buf*, void*)> statCallback,
	git_diff_line_cb callback,
	void* data,
	const bool bMerge,
	const int diffContext,
	const bool bNoPrefix
) {
	CStringA tree1 = CUnicodeUtils::GetUTF8<CStringA>(revNew);
	CStringA tree2 = CUnicodeUtils::GetUTF8<CStringA>(revOld);

	const CAutoRepository repo(g_Git.GetGitRepository());
	if (!repo)
		return -1;

	const int isHeadOrphan = git_repository_head_unborn(repo);
	if (isHeadOrphan == 1)
		return 0;
	else if (isHeadOrphan != 0)
		return -1;

	git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
	// GetString() rather than GetBuffer(): nothing writes through the pointer, and
	// every early return below skipped the matching ReleaseBuffer anyway.
	const CStringA pathA = CUnicodeUtils::StdGetUTF8(path.GetGitPathString().c_str()).c_str();
	const char* buf = pathA.GetString();
	if (!pathA.IsEmpty())
	{
		opts.pathspec.strings = const_cast<char**>(&buf);
		opts.pathspec.count = 1;
	}
	if (bNoPrefix)
	{
		opts.new_prefix = "";
		opts.old_prefix = "";
	}
	// -1 is "whatever git's default is", which GIT_DIFF_OPTIONS_INIT already set.
	if (diffContext >= 0)
		opts.context_lines = static_cast<uint32_t>(diffContext);

	if (revNew == tgit::invarients::ZeroRevSV().data() || revOld == tgit::invarients::ZeroRevSV().data())
	{
		// At most one side is a tree. When it is the *new* side, the working copy is
		// the old one and the diff runs backwards - the `-R` of the command line
		// spelling, which this had no equivalent of and so produced the diff the
		// wrong way round. git_diff__merge swaps its two sides when GIT_DIFF_REVERSE
		// is set, so the flag composes through tree -> index -> workdir correctly.
		if (revNew != tgit::invarients::ZeroRevSV().data())
			opts.flags |= GIT_DIFF_REVERSE;

		CAutoTree t1;
		if (revNew != tgit::invarients::ZeroRevSV().data() && resolve_to_tree(repo, tree1, &t1))
			return -1;

		if (revOld != tgit::invarients::ZeroRevSV().data() && resolve_to_tree(repo, tree2, &t1))
			return -1;

		// One call rather than a hand-rolled tree_to_index + index_to_workdir + merge:
		// it is the same three steps, but it loads the index once and hands the same
		// one to both halves instead of letting each reload it.
		CAutoDiff diff;
		if (git_diff_tree_to_workdir_with_index(&diff, repo, t1, &opts))
			return -1;

		return EmitUnifiedDiff(diff, statCallback, callback, data);
	}

	if (tree1.IsEmpty() && tree2.IsEmpty())
		return -1;

	if (tree1.IsEmpty())
	{
		tree1 = tree2;
		tree2.Empty();
	}

	CAutoTree t1;
	if (!tree1.IsEmpty() && resolve_to_tree(repo, tree1, &t1))
		return -1;

	// With no old rev, `-m` means "against each parent in turn" - which is the
	// entire difference between a merge commit's diff and any other one. The
	// single tree1~1 diff below is the first parent only, so every other parent
	// used to be dropped without a word.
	CAutoCommit commit;
	if (bMerge && tree2.IsEmpty() && !resolve_to_commit(repo, tree1, &commit) && git_commit_parentcount(commit) > 1)
	{
		for (unsigned int i = 0; i < git_commit_parentcount(commit); ++i)
		{
			CAutoCommit parent;
			if (git_commit_parent(&parent, commit, i))
				return -1;

			CAutoTree parentTree;
			if (git_commit_tree(&parentTree, parent))
				return -1;

			CAutoDiff diff;
			if (git_diff_tree_to_tree(&diff, repo, parentTree, t1, &opts))
				return -1;

			// A parent the merge result is identical to contributes nothing at all,
			// not an empty stat block - which is what git prints, and what the stat
			// callback would otherwise emit a stray newline for.
			if (!git_diff_num_deltas(diff))
				continue;

			if (EmitUnifiedDiff(diff, statCallback, callback, data))
				return -1;
		}

		return 0;
	}

	CAutoTree t2;
	if (tree2.IsEmpty())
	{
		/* don't check return value, there are not parent commit at first commit*/
		resolve_to_tree(repo, tree1 + "~1", &t2);
	}
	else if (resolve_to_tree(repo, tree2, &t2))
		return -1;

	CAutoDiff diff;
	if (git_diff_tree_to_tree(&diff, repo, t2, t1, &opts))
		return -1;

	return EmitUnifiedDiff(diff, statCallback, callback, data);
}

STRING_VECTOR GetUnifiedDiffCmd(const CTGitPath& path, const CString& rev1, const CString& rev2, const bool bMerge, const bool bCombine, int diffContext, const bool bNoPrefix = false)
{
	STRING_VECTOR argv{ L"git.exe" };
	// rev2 is tested first, so a call with *both* revs set to the working copy
	// diffs rev1 forwards. That is what the format strings did; note that
	// GetCommitDiffList above tests rev1 first and so resolves the same case the
	// other way round. Neither is obviously right, and neither is changed here.
	if (rev2 == tgit::invarients::ZeroRevSV().data() || rev1 == tgit::invarients::ZeroRevSV().data())
	{
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
	}
	else
	{
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

	argv.emplace_back(L"--");
	if (!path.IsEmpty())
		argv.emplace_back(path.GetGitPathString());

	return argv;
}


template <tgit::zstr::IsTCompString Str>
Str GetShortName(const StringView<Str> ref, const StringView<Str> prefix)
{
	constexpr auto weird_tail = L"^{}"sv;
	if (ref.starts_with(prefix))
	{
		auto shortname = ref.substr(prefix.length());
		if (shortname.ends_with(weird_tail))
			shortname.remove_suffix(weird_tail.length());
		return Str{ shortname.data() };
	}
	return {};
}

BOOL GetShortName(const CString& ref, CString& shortname, const CString& prefix)
{
	if (const auto str = GetShortName(StringView{ref}, StringView{prefix}); !str.IsEmpty())
	{
		shortname = str;
		return TRUE;
	}
	return FALSE;
}

CString GetShortName(const CString& str, REF_TYPE* const out_type)
{
	CString shortname;
	REF_TYPE type = UNKNOWN;

	if (GetShortName(str, shortname, L"refs/heads/"))
		type = LOCAL_BRANCH;
	else if (GetShortName(str, shortname, L"refs/remotes/"))
		type = REMOTE_BRANCH;
	else if (StringUtils::EndsWith(str, L"^{}") && GetShortName(str, shortname, L"refs/tags/"))
		type = ANNOTATED_TAG;
	else if (GetShortName(str, shortname, L"refs/tags/"))
		type = TAG;
	else if (GetShortName(str, shortname, L"refs/stash"))
	{
		type = STASH;
		shortname = L"stash";
	}
	else if (GetShortName(str, shortname, L"refs/bisect/"))
	{
		CString bisectGood;
		CString bisectBad;
		g_Git.GetBisectTerms(&bisectGood, &bisectBad);
		wchar_t c;
		if (tgit::wstr::StartsWith(StringView(shortname), StringView(bisectGood)) && ((c = shortname.GetAt(bisectGood.GetLength())) == '-' || c == '\0'))
		{
			type = BISECT_GOOD;
			shortname = bisectGood;
		}

		if (tgit::wstr::StartsWith(StringView(shortname), StringView(bisectBad)) && ((c = shortname.GetAt(bisectBad.GetLength())) == '-' || c == '\0'))
		{
			type = BISECT_BAD;
			shortname = bisectBad;
		}

		if (StringUtils::StartsWith(shortname, L"skip") && ((c = shortname.GetAt(4)) == '-' || c == '\0'))
		{
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

	if(out_type)
		*out_type = type;

	return shortname;
}

/// Splits the next entry off a PATH-style list into \a entry: ';'-separated,
/// and an entry that starts with '"' runs to the closing quote. Returns where
/// the following entry starts, or nullptr when nothing was consumed.
LPCWSTR nextpath(const wchar_t* path, std::wstring& entry)
{
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


CString FindFileOnPath(const CString& filename, LPCWSTR env, const bool wantDirectory = false)
{
	// search in all paths defined in PATH, stopping at the first empty entry
	std::wstring directory;
	while ((env = nextpath(env, directory)) != nullptr && !directory.empty())
	{
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


BOOL FindGitPath()
{
	size_t size;
	_wgetenv_s(&size, nullptr, 0, L"PATH");
	if (!size)
		return FALSE;

	const auto env = std::make_unique<wchar_t[]>(size);
	if (!env)
		return FALSE;
	_wgetenv_s(&size, env.get(), size, L"PATH");

	const CString gitExeDirectory = FindFileOnPath(L"git.exe", env.get(), true);
	if (!gitExeDirectory.IsEmpty())
	{
		TGitH::ms_LastMsysGitDir = gitExeDirectory;
		TGitH::ms_LastMsysGitDir.TrimRight(L'\\');

		// check for (broken) scoop shim, cf. issue #4033
		if (CString contents; LoadTextFile(TGitH::ms_LastMsysGitDir + L"\\git.shim", contents))
		{
			tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ L": found git.shim\n");
			int start = 0 ;
			while (start >= 0)
			{
				CString line = contents.Tokenize(L"\n", start);
				if (const auto separatorPos = line.Find(L'='); separatorPos > 0 && line.Mid(0, separatorPos).Trim() == L"path")
				{
					CString path = line.Mid(separatorPos + 1).Trim();
					if (path.GetLength() >= 2 && path[0] == L'"' && path[path.GetLength() - 1] == L'"')
						path = path.Mid(1, path.GetLength() - 2);
					if (StringUtils::EndsWithI(path, L"\\git.exe") && PathFileExists(path))
						TGitH::ms_LastMsysGitDir = path.Mid(0, path.GetLength() - static_cast<int>(wcslen(L"\\git.exe")));
					break;
				}
			}
		}

		if (StringUtils::EndsWith(TGitH::ms_LastMsysGitDir, L"\\mingw32\\bin") || StringUtils::EndsWith(TGitH::ms_LastMsysGitDir, L"\\mingw64\\bin"))
		{
			// prefer cmd directory as early Git for Windows 2.x releases only had this
			const CString installRoot = TGitH::ms_LastMsysGitDir.Left(TGitH::ms_LastMsysGitDir.GetLength() - static_cast<int>(wcslen(L"\\mingw64\\bin"))) + L"\\cmd\\git.exe";
			if (PathFileExists(installRoot))
				TGitH::ms_LastMsysGitDir = TGitH::ms_LastMsysGitDir.Left(TGitH::ms_LastMsysGitDir.GetLength() - static_cast<int>(wcslen(L"\\mingw64\\bin"))) + L"\\cmd";
		}
		if (StringUtils::EndsWith(TGitH::ms_LastMsysGitDir, L"\\cmd"))
		{
			// often the msysgit\cmd folder is on the %PATH%, but
			// that git.exe does not work, so try to guess the bin folder
			if (PathFileExists(TGitH::ms_LastMsysGitDir.Left(TGitH::ms_LastMsysGitDir.GetLength() - static_cast<int>(wcslen(L"\\cmd"))) + L"\\bin\\git.exe"))
				TGitH::ms_LastMsysGitDir = TGitH::ms_LastMsysGitDir.Left(TGitH::ms_LastMsysGitDir.GetLength() - static_cast<int>(wcslen(L"\\cmd"))) + L"\\bin";
		}
		return TRUE;
	}

	return FALSE;
}


constexpr CString FindExecutableOnPath(const CString& executable, const LPCWSTR env)
{
	CString filename = executable;

	if (!StringUtils::EndsWith(executable, L".exe"))
		filename += L".exe";

	if (PathFileExists(filename))
		return CString{filename};

	filename = FindFileOnPath(filename, env);
	if (!filename.IsEmpty())
		return CString{filename};

	return executable;
}

struct map_each_ref_payload {
	git_repository* repo = nullptr;
	MAP_HASH_NAME* map = nullptr;
};

int libgit2_addto_map_each_ref_fn(git_reference* rawref, void* payload)
{
	const CAutoReference ref{ std::move(rawref) };
	const auto payloadContent = static_cast<map_each_ref_payload*>(payload);

	std::wstring str = CUnicodeUtils::Convert2Unicode<std::wstring>(git_reference_name(ref));

	CAutoObject gitObject;
	if (git_revparse_single(&gitObject, payloadContent->repo, git_reference_name(ref)))
		return (git_reference_is_remote(ref) && git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC) ? 0 : 1; // don't bail out for symbolic remote references ("git.exe show-ref -d" also doesn't complain), cf. issue #2926

	if (git_object_type(gitObject) == GIT_OBJECT_TAG)
	{
		str += L"^{}"; // deref tag
		CAutoObject derefedTag;
		if (git_tag_target(&derefedTag, reinterpret_cast<git_tag*>(static_cast<git_object*>(gitObject))))
			return 1;
		gitObject.Swap(derefedTag);
	}

	(*payloadContent->map)[CGitHash(git_object_id(gitObject))].push_back(std::move(str));

	return 0;
}

int GetMapHashToFriendName(git_repository* repo, MAP_HASH_NAME &map)
{
	ATLASSERT(repo);

	map_each_ref_payload payloadContent = { repo, &map };

	if (git_reference_foreach(repo, libgit2_addto_map_each_ref_fn, &payloadContent))
		return -1;

	for (auto it = map.begin(); it != map.end(); ++it)
	{
		std::sort(it->second.begin(), it->second.end());
	}

	return 0;
}

bool FindGitForWindows(CString& sMsysGitPath) // not static: an internal-linkage name cannot be exported (C2294)
{
	// First non-empty InstallLocation wins: per-user install, then machine, then 32-bit machine.
	const std::pair<HKEY, std::wstring_view> candidates[]{
		{ HKEY_CURRENT_USER,  UNINSTALL_REG_SUBKEY },
		{ HKEY_LOCAL_MACHINE, UNINSTALL_REG_SUBKEY },
		{ HKEY_LOCAL_MACHINE, UNINSTALL_RGSK_WOW64 },
	};
	for (const auto& [root, key] : candidates)
	{
		sMsysGitPath = wil::reg::get_value_string(UNINSTALL_REG_VALUE, L"", root, key).c_str();
		sMsysGitPath.TrimRight(L'\\');
		if (!sMsysGitPath.IsEmpty())
			break;
	}

	if (sMsysGitPath.IsEmpty())
	{
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ L": Found no git.exe\n");
		return false;
	}
	if (PathFileExists(sMsysGitPath + L"\\bin\\git.exe")) {
		sMsysGitPath += L"\\bin";
		return true;
	} else if (PathFileExists(sMsysGitPath + L"\\cmd\\git.exe")) {
		// only needed for older Git for Windows 2.x packages
		sMsysGitPath += L"\\cmd";
		return true;
	} else {
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(
		__FUNCTION__ L": Git for Windows installation found, but git.exe not exists in %s\n", sMsysGitPath);
		sMsysGitPath.Empty();
		return false;
	}
}

int FindAndSetGitExePath(const BOOL bFallback)
{
	const std::wstring str = wil::reg::get_value_string(REG_MSYSGIT_PATH);
	if (!str.empty() && PathFileExists((str + L"\\git.exe").c_str()))
	{
		TGitH::ms_LastMsysGitDir = str.c_str();
		return TRUE;
	}

	CTraceToOutputDebugString::Instance()(__FUNCTION__ L": git.exe not exists: %s\n", static_cast<LPCWSTR>(TGitH::ms_LastMsysGitDir));
	if (!bFallback)
		return FALSE;

	// first, search PATH if git/bin directory is already present
	if (FindGitPath())
	{
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": FindGitPath() => %s\n", static_cast<LPCWSTR>(TGitH::ms_LastMsysGitDir));
		std::ignore = wil::reg::set_value_string(REG_MSYSGIT_PATH, StringView(TGitH::ms_LastMsysGitDir));
		return TRUE;
	}

	if (CString found = str.c_str(); FindGitForWindows(found))
	{
		TGitH::ms_LastMsysGitDir = found;
		std::ignore = wil::reg::set_value_string(REG_MSYSGIT_PATH, StringView(found));
		return TRUE;
	}

	return FALSE;
}

}



CGit::CGit()
{
	git_libgit2_init();
	// Seeded from the process's own working directory, which is emphatically not
	// guaranteed to be a working copy root - every caller that cares sets one.
	// This is the third writer of the directory and the only one a grep for
	// "m_CurrentDir =" could never find, which is the argument for making the
	// member const rather than merely documenting that it should not be assigned.
	// It writes the storage rather than a setter because CStrBuf fills a buffer in
	// place, and because this write is not choosing a repository.
	GetCurrentDirectory(MAX_PATH, CStrBuf(m_CurrentDirStorage, MAX_PATH));
	m_IsUseGitDLL = wil::reg::get_value_typed<bool>(L"UsingGitDLL"sv);
	m_IsUseLibGit2 = wil::reg::get_value_typed<bool>(L"UseLibgit2"sv, true);
	m_IsUseLibGit2_mask = wil::reg::get_value_typed<BITS32>(L"UseLibgit2_mask"sv, DEFAULT_USE_LIBGIT2_MASK);

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
		g_bSortLogical = true;
		g_bSortLocalBranchesFirst = true;
		g_bSortTagsReversed = false;
#else
		g_bSortLogical = !Registry::CRegDWORD(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\NoStrCmpLogical", 0, false, HKEY_CURRENT_USER);
		if (g_bSortLogical)
			g_bSortLogical = !Registry::CRegDWORD(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\NoStrCmpLogical", 0, false, HKEY_LOCAL_MACHINE);
		g_bSortLocalBranchesFirst = !Registry::CRegDWORD(L"Software\\TortoiseGit\\NoSortLocalBranchesFirst", 0, false, HKEY_CURRENT_USER);
		if (g_bSortLocalBranchesFirst)
			g_bSortLocalBranchesFirst = !Registry::CRegDWORD(L"Software\\TortoiseGit\\NoSortLocalBranchesFirst", 0, false, HKEY_LOCAL_MACHINE);
		g_bSortTagsReversed = !!Registry::CRegDWORD(L"Software\\TortoiseGit\\SortTagsReversed", 0, false, HKEY_LOCAL_MACHINE);
		if (!g_bSortTagsReversed)
			g_bSortTagsReversed = !!Registry::CRegDWORD(L"Software\\TortoiseGit\\SortTagsReversed", 0, false, HKEY_CURRENT_USER);
#endif


	CheckMsysGitDir();
}

CGit::~CGit()
{
	if(this->m_GitDiff)
	{
		git_close_diff(m_GitDiff);
		m_GitDiff = nullptr;
	}
	if(this->m_GitSimpleListDiff)
	{
		git_close_diff(m_GitSimpleListDiff);
		m_GitSimpleListDiff = nullptr;
	}
	git_libgit2_shutdown();
}

bool CGit::IsBranchNameValid(const CString& branchname)
{
	if (branchname.FindOneOf(L"\"|<>") >= 0) // not valid on Windows
		return false;
	int valid = 0;
	git_branch_name_is_valid(&valid, CUnicodeUtils::GetUTF8<CStringA>(branchname));
	return valid == 1;
}


int CGit::RunAsync(
	CString cmd,
	PROCESS_INFORMATION& piOut,
	HANDLE* hReadOut,
	HANDLE* hErrReadOut,
	const CString* StdioFile
) {
	wil::CAutoGeneralHandle hRead, hWrite, hReadErr, hWriteErr, hWriteIn, hReadIn;

	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	if (!CreatePipe(&hReadIn, &hWriteIn, &sa, 0)) {
		CString err{ CFormatMessageWrapper().c_str() };
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": could not open stdin pipe: %s\n",
		                                      static_cast<LPCWSTR>(err.Trim()));
		return TGIT_GIT_ERROR_OPEN_PIP;
	}
	if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
		CString err{ CFormatMessageWrapper().c_str() };
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": could not open stdout pipe: %s\n",
		                                      static_cast<LPCWSTR>(err.Trim()));
		return TGIT_GIT_ERROR_OPEN_PIP;
	}
	if (hErrReadOut && !CreatePipe(&hReadErr, &hWriteErr, &sa, 0)) {
		CString err{ CFormatMessageWrapper().c_str() };
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": could not open stderr pipe: %s\n",
		                                      static_cast<LPCWSTR>(err.Trim()));
		return TGIT_GIT_ERROR_OPEN_PIP;
	}

	wil::unique_handle hStdioFile;
	if (StdioFile)
		// Not CreateFileUnique: that throws, and a failed open here has always meant
		// an invalid stdout handle for the child, not an exception.
		hStdioFile.reset(CreateFile(StdioFile->GetString(),
		                            GENERIC_WRITE,
		                            FILE_SHARE_READ | FILE_SHARE_WRITE,
		                            &sa,
		                            CREATE_ALWAYS,
		                            FILE_ATTRIBUTE_NORMAL,
		                            nullptr));

	STARTUPINFO si = {};
	PROCESS_INFORMATION pi = {};
	si.cb = sizeof(STARTUPINFO);
	si.hStdInput = hReadIn.get();
	if (hErrReadOut)
		si.hStdError = hWriteErr.get();
	else
		si.hStdError = hWrite.get();
	if (StdioFile)
		si.hStdOutput = hStdioFile.get();
	else
		si.hStdOutput = hWrite.get();

	si.wShowWindow = SW_HIDE;
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

	const DWORD dwFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;

	memset(&this->m_CurrentGitPi, 0, sizeof(PROCESS_INFORMATION));

	const bool startsWithGit = StringUtils::StartsWith(cmd, L"git");
	if ((ms_bMsys2Git || ms_bCygwinGit) && startsWithGit) {
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": executing via bash: %s\n", static_cast<LPCWSTR>(cmd));
		CString tmpFile = GetTempFile();
		if (ms_bMsys2Git)
			cmd = L"/usr/bin/" + cmd;
		else if (ms_bCygwinGit)
			cmd = L"/bin/" + cmd;
		StringUtils::WriteStringToTextFile(tmpFile, cmd);
		tmpFile.Replace(L'\\', L'/');
		cmd = L'"' + TGitH::ms_LastMsysGitDir + L"\\bash.exe\" \"" + tmpFile + L'"';
	}
	else if (startsWithGit || StringUtils::StartsWith(cmd, L"bash")) {
		const int firstSpace = cmd.Find(L' ');
		if (firstSpace > 0)
			cmd = L'"' + TGitH::ms_LastMsysGitDir + L'\\' + cmd.Left(firstSpace) + L'"' + cmd.Mid(firstSpace);
		else
			cmd = L'"' + TGitH::ms_LastMsysGitDir + L'\\' + cmd + L'"';
	}

	CTraceToOutputDebugString::Instance()(__FUNCTION__ L": executing %s\n", static_cast<LPCWSTR>(cmd));
	// lpCurrentDirectory is LPCWSTR, so the GetBuffer() this used to call was
	// pointless as well as unbalanced - only lpCommandLine is written to.
	const auto pEnv = const_cast<wchar_t*>(m_Environment.get_cp());

	if (!CreateProcess(nullptr, cmd.GetBuffer(), nullptr, nullptr, TRUE, dwFlags, pEnv, m_CurrentDir, &si, &pi)) {
		CString err{ CFormatMessageWrapper().c_str() };
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": error while executing command: %s\n",
		                                      static_cast<LPCWSTR>(err.Trim()));
		return TGIT_GIT_ERROR_CREATE_PROCESS;
	}

	// Close the pipe handle so the child process stops reading.
	hWriteIn.reset();

	m_CurrentGitPi = pi;
	piOut = pi;
	if (hReadOut)
		*hReadOut = hRead.release();
	if (hErrReadOut)
		*hErrReadOut = hReadErr.release();
	return 0;
}

//Must use sperate function to convert ANSI str to union code string
//Because A2W use stack as internal convert buffer.
void CGit::StringAppend(CString& str, const std::string_view append, const int code)
{
	if (append.empty())
		return;
	const int currentContentLen = str.GetLength();
	if (2 * append.length() >= gsl::narrow<size_t>(INT_MAX - currentContentLen))
		throw std::overflow_error("CString would become too long");
	const int len = static_cast<int>(append.length()); // len is guaranteed to fit into an int, as checked above
	auto* buf = str.GetBuffer(len * 2 + currentContentLen) + currentContentLen; // maximum buffer size is less than INT_MAX as checked above
	const int appendedLen = MultiByteToWideChar(code, 0, append.data(), len, buf, len * 2);
	str.ReleaseBuffer(currentContentLen + appendedLen); // no - 1 because MultiByteToWideChar is called with a fixed length (thus no nul char included)
}

// Checkes whether ref is an unborn branch
int CGit::IsUnbornBranch(const CString& ref)
{
	if (ref.IsEmpty() || ref == L"HEAD")
		return IsInitRepos();
	CString currentBranch;
	const int ret = GetCurrentBranchFromFile(g_Git.m_CurrentDir, currentBranch);
	if (ret < 0)
		return ret;
	if (ref == currentBranch || currentBranch == StripRefName(ref))
		return IsInitRepos();
	return false;
}

DWORD WINAPI CGit::AsyncReadStdErrThread(const LPVOID lpParam)
{
	const auto pDataArray = static_cast<ASYNCREADSTDERRTHREADARGS*>(lpParam);

	DWORD readnumber{};
	char data[CALL_OUTPUT_READ_CHUNK_SIZE] = {};
	bool dropMode = false;
	while (ReadFile(pDataArray->fileHandle, data, CALL_OUTPUT_READ_CHUNK_SIZE, &readnumber, nullptr))
	{
		if (!dropMode && pDataArray->pcall->OnOutputErrData(std::string_view(data, readnumber)))
			dropMode = true;
	}

	return 0;
}

#ifdef _MFC_VER
void CGit::KillRelatedThreads(CWinThread* thread)
{
	CAutoLocker lock(m_critSecThreadMap);
	auto it = m_AsyncReadStdErrThreadMap.find(thread->m_nThreadID);
	if (it != m_AsyncReadStdErrThreadMap.cend())
	{
		TerminateThread(it->second, static_cast<DWORD>(-1));
		m_AsyncReadStdErrThreadMap.erase(it);
	}
	TerminateThread(thread->m_hThread, static_cast<DWORD>(-1));
}
#endif


int CGit::Run(CGitCall& pcall)
{
	PROCESS_INFORMATION pi;
	wil::CAutoGeneralHandle hRead, hReadErr;
	if (RunAsync(pcall.GetCmd(), pi, &hRead, &hReadErr))
		return TGIT_GIT_ERROR_CREATE_PROCESS;

	wil::CAutoGeneralHandle piThread(pi.hThread);
	wil::CAutoGeneralHandle piProcess(pi.hProcess);

	ASYNCREADSTDERRTHREADARGS threadArguments{ .fileHandle = hReadErr.get(), .pcall = &pcall };
	const wil::CAutoGeneralHandle thread{ CreateThread(nullptr, 0, AsyncReadStdErrThread, &threadArguments, 0, nullptr) };

	if (thread)
	{
		CAutoLocker lock(m_critSecThreadMap);
		m_AsyncReadStdErrThreadMap[GetCurrentThreadId()] = thread.get();
	}

	DWORD readnumber = {};
	char data[CALL_OUTPUT_READ_CHUNK_SIZE] = {};
	bool bAborted=false;
	while (ReadFile(hRead.get(), data, CALL_OUTPUT_READ_CHUNK_SIZE, &readnumber, nullptr))
	{
		// TODO: when OnOutputData() returns 'true', abort git-command. Send CTRL-C signal?
		if(!bAborted)//For now, flush output when command aborted.
			if (pcall.OnOutputData(std::string_view(data, readnumber)))
				bAborted=true;
	}
	if(!bAborted)
		pcall.OnEnd();

	if (thread)
	{
		WaitForSingleObject(thread.get(), INFINITE);

		CAutoLocker lock(m_critSecThreadMap);
		m_AsyncReadStdErrThreadMap.erase(GetCurrentThreadId());
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exitcode =0;

	if(!GetExitCodeProcess(pi.hProcess,&exitcode))
	{
		CString err { CFormatMessageWrapper().c_str() };
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": could not get exit code: %s\n", static_cast<LPCWSTR>(err.Trim()));
		return TGIT_GIT_ERROR_GET_EXIT_CODE;
	}
	else
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": process exited: %d\n", exitcode);

	if (pcall.wasLimited())
		return -1;

	return exitcode;
}

int CGit::Run(const CString& cmd, BYTE_VECTOR* vector, BYTE_VECTOR* vectorErr)
{
	CGitCall_ByteVector call(cmd, vector, vectorErr);
	if (s_limitGitExeOutput && vectorErr)
	{
		if (vectorErr->size() > GIT_EXE_STDERR_BUFFER_LIMIT)
			vectorErr->append(std::format("\n\n[Git.exe error output truncated by TortoiseGit at about {} KiB]", GIT_EXE_STDERR_BUFFER_LIMIT / 1024));
		if (call.wasLimited())
			vectorErr->append(std::format("\n\n[Git.exe's output truncated by TortoiseGit at about {} MiB]", GIT_EXE_BUFFER_LIMIT / 1024 / 1024));
	}
	return Run(call);
}

int CGit::Run(const CString& cmd, CString* output, const int code)
{
	CString err;
	const int ret = Run(cmd, output, &err, code);

	if (output && !err.IsEmpty())
	{
		if (!output->IsEmpty())
			*output += L'\n';
		*output += err;
	}

	return ret;
}

int CGit::Run(const CString& cmd, CString* output, CString* outputErr, const int code)
{
	BYTE_VECTOR vector, vectorErr;
	int ret;
	if (outputErr)
		ret = Run(cmd, &vector, &vectorErr);
	else
		ret = Run(cmd, &vector);

	if (output && !vector.empty())
		StringAppend(*output, std::string_view(vector.data(), vector.size()), code);

	if (outputErr && !vectorErr.empty())
		StringAppend(*outputErr, std::string_view(vectorErr.data(), vectorErr.size()), code);

	return ret;
}

CString CGit::GetUserName()
{
	CEnvironment env;
	env.CopyProcessEnvironment();
	CString envname = env.GetEnv(L"GIT_AUTHOR_NAME").c_str();
	if (!envname.IsEmpty())
		return envname;

	if (auto authorname = GetConfigValue(L"author.name"); !authorname.IsEmpty())
		return authorname;

	return GetConfigValue(L"user.name");
}

CString CGit::GetUserEmail()
{
	CEnvironment env;
	env.CopyProcessEnvironment();
	CString envmail = env.GetEnv(L"GIT_AUTHOR_EMAIL").c_str();
	if (!envmail.IsEmpty())
		return envmail;

	if (auto authormail = GetConfigValue(L"author.email"); !authormail.IsEmpty())
		return authormail;

	return GetConfigValue(L"user.email");
}

CString CGit::GetCommitterName()
{
	CEnvironment env;
	env.CopyProcessEnvironment();
	CString envname = env.GetEnv(L"GIT_COMMITTER_NAME").c_str();
	if (!envname.IsEmpty())
		return envname;

	if (auto committername = GetConfigValue(L"committer.name"); !committername.IsEmpty())
		return committername;

	return GetConfigValue(L"user.name");
}

CString CGit::GetCommitterEmail()
{
	CEnvironment env;
	env.CopyProcessEnvironment();
	CString envmail = env.GetEnv(L"GIT_AUTHOR_EMAIL").c_str();
	if (!envmail.IsEmpty())
		return envmail;

	if (auto committermail = GetConfigValue(L"committer.email"); !committermail.IsEmpty())
		return committermail;

	return GetConfigValue(L"user.email");
}

CString CGit::GetConfigValue(const CString& name, const CString& def)
{
	return ReadConfigValue(name, def, false);
}

CString CGit::ReadConfigValue(const CString& name, const CString& def, const bool canonicalizeBool)
{
	if(this->m_IsUseGitDLL)
	{
		CAutoLocker lock(g_Git.m_critGitDllSec);

		try
		{
			CheckAndInitDll();
		}catch(...)
		{
		}
		CStringA key, value;
		key =  CUnicodeUtils::GetUTF8<CStringA>(name);

		try
		{
			if (git_get_config(key, CStrBufA(value, 4096), 4096))
				return def;
		}
		catch (const char *msg)
		{
			::MessageBox(nullptr, L"Could not get config.\nlibgit reports:\n" + CUnicodeUtils::Convert2Unicode<CString>(msg), L"TortoiseGit", MB_OK | MB_ICONERROR);
			return def;
		}

		return CUnicodeUtils::GetUnicode<CString>(value);
	}
	else
	{
		STRING_VECTOR argv{ L"git.exe", L"config" };
		if (canonicalizeBool)
			argv.emplace_back(L"--bool");
		argv.emplace_back(L"--end-of-options");
		argv.emplace_back(std::wstring(name));

		CString configValue;
		if (Run(argv, &configValue, nullptr, CP_UTF8))
			return def;
		if (configValue.IsEmpty())
			return configValue;
		return configValue.Left(configValue.GetLength() - 1); // strip last newline character
	}
}

bool CGit::GetConfigValueBool(const CString& name, const bool def)
{
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

int CGit::GetConfigValueInt32(const CString& name, const int def)
{
	const CString configValue = GetConfigValue(name);
	int value = def;
	if (!git_config_parse_int32(&value, CUnicodeUtils::GetUTF8<CStringA>(configValue)))
		return value;
	return def;
}

int CGit::ApplyPatchToIndex(const CString& patchPath, CString* out)
{
	return Run({ L"git.exe", L"apply", L"--cached", L"--", AsGitPath(patchPath) }, out, CP_UTF8);
}

int CGit::ApplyPatchToIndexReverse(const CString& patchPath, CString* out)
{
	return Run({ L"git.exe", L"apply", L"--cached", L"-R", L"--", AsGitPath(patchPath) }, out, CP_UTF8);
}

CString CGit::GetCurrentBranch(const bool fallback)
{
	CString output;
	const int result = GetCurrentBranchFromFile(m_CurrentDir, output, fallback);
	if (result != 0 && ((result == 1 && !fallback) || result != 1))
		return L"(no branch)";
	else
		return output;
}

void CGit::GetRemoteTrackedBranch(const CString& localBranch, CString& pullRemote, CString& pullBranch)
{
	if (localBranch.IsEmpty())
		return;

	CString configName;
	configName.Format(L"branch.%s.remote", static_cast<LPCWSTR>(localBranch));
	pullRemote =  GetConfigValue(configName);

	//Select pull-branch from current branch
	configName.Format(L"branch.%s.merge", static_cast<LPCWSTR>(localBranch));
	pullBranch = StripRefName(GetConfigValue(configName));
}

void CGit::GetRemoteTrackedBranchForHEAD(CString& remote, CString& branch)
{
	CString refName;
	if (GetCurrentBranchFromFile(m_CurrentDir, refName))
		return;
	GetRemoteTrackedBranch(StripRefName(refName), remote, branch);
}

void CGit::GetRemotePushBranch(const CString& localBranch, CString& pushRemote, CString& pushBranch)
{
	if (localBranch.IsEmpty())
		return;

	CString configName;

	configName.Format(L"branch.%s.pushremote", static_cast<LPCWSTR>(localBranch));
	pushRemote = g_Git.GetConfigValue(configName);
	if (pushRemote.IsEmpty())
	{
		pushRemote = g_Git.GetConfigValue(L"remote.pushdefault");
		if (pushRemote.IsEmpty())
		{
			configName.Format(L"branch.%s.remote", static_cast<LPCWSTR>(localBranch));
			pushRemote = g_Git.GetConfigValue(configName);
		}
	}

	configName.Format(L"branch.%s.pushbranch", static_cast<LPCWSTR>(localBranch));
	pushBranch = g_Git.GetConfigValue(configName); // do not strip branch name (for gerrit), see issue #1609)
	if (pushBranch.IsEmpty())
	{
		configName.Format(L"branch.%s.merge", static_cast<LPCWSTR>(localBranch));
		pushBranch = TGitH::StripRefName(g_Git.GetConfigValue(configName));
	}
}

CString CGit::GetFullRefName(const CString& shortRefName)
{
	CString refName;
	if (Run({ L"git.exe", L"rev-parse", L"--symbolic-full-name", L"--verify", L"--end-of-options", std::wstring(shortRefName) }, &refName, nullptr, CP_UTF8) != 0)
		return CString();//Error
	return refName.TrimRight();
}

int CGit::GetCurrentBranchFromFile(const CString &sProjectRoot, CString &sBranchOut, const bool fallback)
{
	// read current branch name like git-gui does, by parsing the .git/HEAD file directly

	if ( sProjectRoot.IsEmpty() )
		return -1;

	const auto sDotGitPath = GitAdminDir::GetWorktreeAdminDirPath(sProjectRoot.GetString());
	if (!sDotGitPath)
		return -1;

	const CString sHeadFile = (*sDotGitPath + L"HEAD").c_str();

	const wil::CAutoFile pFile{_wfsopen(sHeadFile.GetString(), L"r", SH_DENYWR)};
	if (!pFile)
		return -1;

	char s[MAX_PATH] = {};
	fgets(s, sizeof(s), pFile.get());

	const char *pfx = "ref: refs/heads/";
	const size_t len = strlen(pfx);

	if ( !strncmp(s, pfx, len) )
	{
		//# We're on a branch.  It might not exist.  But
		//# HEAD looks good enough to be a branch.
		const CStringA utf8Branch(s + len);
		sBranchOut = CUnicodeUtils::GetUnicode<CString>(utf8Branch);
		sBranchOut.TrimRight(L" \r\n\t");

		if ( sBranchOut.IsEmpty() )
			return -1;
	}
	else if (fallback)
	{
		const std::string_view utf8Hash{s, std::strlen(s)};
		bool ok = false;
		const CGitHash hash = CGitHash::FromHexStr(utf8Hash, &ok);
		if (ok)
			sBranchOut = CString{hash.ToString().c_str()};
		else
			//# Assume this is a detached head.
			sBranchOut = L"HEAD";
		return 1;
	}
	else
	{
		//# Assume this is a detached head.
		sBranchOut = L"HEAD";

		return 1;
	}

	return 0;
}

std::vector<std::string> CGit::GetLogCmd(CString range, const CTGitPath* path, const int mask, const CFilterData* Filter, const int logOrderBy)
{
	std::vector<std::string> params;

	params.push_back("-z");

	if(mask& LOG_INFO_STAT )
		params.push_back("--numstat");
	if(mask& LOG_INFO_FILESTATE)
		params.push_back("--raw");

	if(mask& LOG_INFO_BOUNDARY)
	{
		params.push_back("--left-right");
		params.push_back("--boundary");
	}

	if(mask& TGitH::LOG_INFO_ALL_BRANCH)
	{
		params.push_back("--all");
		if ((mask & LOG_INFO_ALWAYS_APPLY_RANGE) == 0)
			range.Empty();
	}

	if (mask& TGitH::LOG_INFO_BASIC_REFS)
	{
		params.push_back("--branches");
		params.push_back("--tags");
		params.push_back("--remotes");
		params.push_back("--glob=stas[h]"); // require at least one glob operator
		params.push_back("--glob=bisect");
		if ((mask & LOG_INFO_ALWAYS_APPLY_RANGE) == 0)
			range.Empty();
	}

	if(mask & TGitH::LOG_INFO_LOCAL_BRANCHES)
	{
		params.push_back("--branches");
		if ((mask & LOG_INFO_ALWAYS_APPLY_RANGE) == 0)
			range.Empty();
	}

	if(mask& TGitH::LOG_INFO_DETECT_COPYRENAME)
		params.push_back(std::format("-C{}%", ms_iSimilarityIndexThreshold));

	if(mask& TGitH::LOG_INFO_DETECT_RENAME )
		params.push_back(std::format("-M{}%", ms_iSimilarityIndexThreshold));

	if(mask& TGitH::LOG_INFO_FIRST_PARENT )
		params.push_back("--first-parent");

	if(mask& TGitH::LOG_INFO_NO_MERGE )
		params.push_back("--no-merges");

	if (mask & TGitH::LOG_INFO_FULL_HISTORY)
		params.push_back("--full-history");
	else
		params.push_back("--parents"); // cf. issue #3728

	if(mask& TGitH::LOG_INFO_FOLLOW)
		params.push_back("--follow");

	if(mask& TGitH::LOG_INFO_SHOW_MERGEDFILE)
		params.push_back("-c");

	if(mask& TGitH::LOG_INFO_FULL_DIFF)
		params.push_back("--full-diff");

	if(mask& TGitH::LOG_INFO_SIMPILFY_BY_DECORATION)
		params.push_back("--simplify-by-decoration");

	if (mask & TGitH::LOG_INFO_SPARSE)
		params.push_back("--sparse");

	if (Filter)
	{
		auto from = Filter->m_From;
		if (Filter->m_NumberOfLogsScale >= CFilterData::SHOW_LAST_N_YEARS)
		{
			const auto zone = std::chrono::current_zone();
			const auto today = std::chrono::floor<std::chrono::days>(zone->to_local(std::chrono::system_clock::now()));
			const auto midnight = zone->to_sys(today, std::chrono::choose::earliest);
			std::chrono::seconds substract = std::chrono::days{ 1 };
			switch (Filter->m_NumberOfLogsScale)
			{
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
			params.push_back(std::format("-n{}", Filter->m_NumberOfLogs));
		else if (Filter->m_NumberOfLogsScale >= CFilterData::SHOW_LAST_SEL_DATE && from > 0)
			params.push_back(std::format("--max-age={}", from));
	}

	if( Filter && (Filter->m_To != -1))
		params.push_back(std::format("--min-age={}", Filter->m_To));

	if (logOrderBy == LOG_ORDER_TOPOORDER || (mask & TGitH::LOG_ORDER_TOPOORDER))
		params.push_back("--topo-order");
	else if (logOrderBy == LOG_ORDER_DATEORDER)
		params.push_back("--date-order");
	else if (logOrderBy == LOG_ORDER_AUTHORDATEORDER)
		params.push_back("--author-date-order");

	params.push_back("--end-of-options");

	if (!range.IsEmpty())
	{
		const int len = range.GetLength();
		const LPCWSTR data = range;
		int i = 0;
		while (i < len)
		{
			// skip leading whitespace
			while (i < len && iswspace(data[i]))
				++i;

			const int start = i;
			// read ref
			while (i < len && !iswspace(data[i]))
				++i;
			if (start < i)
				params.push_back(CUnicodeUtils::StdGetUTF8(std::wstring_view(data + start, i - start)));
		}
	}

	params.push_back("--");

	if (path)
		params.push_back(CUnicodeUtils::StdGetUTF8(path->GetGitPathString().c_str()));

	return params;
}

int CGit::RunLogFile(const CString& cmd, const CString& filename, CString* stdErr)
{
	PROCESS_INFORMATION pi{};
	wil::CAutoGeneralHandle hReadErr{};
	if (RunAsync(cmd, pi, nullptr, &hReadErr, &filename))
		return TGIT_GIT_ERROR_CREATE_PROCESS;

	[[maybe_unused]]wil::CAutoGeneralHandle piThread(pi.hThread);
	[[maybe_unused]]wil::CAutoGeneralHandle piProcess(pi.hProcess);

	BYTE_VECTOR stderrVector{};
	CGitCall_ByteVector pcall(L"", nullptr, &stderrVector);
	ASYNCREADSTDERRTHREADARGS threadArguments{ .fileHandle = hReadErr.get(), .pcall = &pcall };
	const wil::CAutoGeneralHandle thread{ CreateThread(nullptr, 0, AsyncReadStdErrThread, &threadArguments, 0, nullptr) };

	if (thread)
	{
		[[maybe_unused]]CAutoLocker lock(m_critSecThreadMap);
		m_AsyncReadStdErrThreadMap[GetCurrentThreadId()] = thread.get();
	}

	WaitForSingleObject(pi.hProcess,INFINITE);

	if (thread)
	{
		WaitForSingleObject(thread.get(), INFINITE);

		[[maybe_unused]]CAutoLocker lock(m_critSecThreadMap);
		m_AsyncReadStdErrThreadMap.erase(GetCurrentThreadId());
	}

	if (stdErr && !stderrVector.empty())
		StringAppend(*stdErr, std::string_view(stderrVector.data(), stderrVector.size()));

	DWORD exitcode = 0;
	if (!GetExitCodeProcess(pi.hProcess, &exitcode))
	{
		CString err { CFormatMessageWrapper().c_str() };
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": could not get exit code: %s\n", static_cast<LPCWSTR>(err.Trim()));
		return TGIT_GIT_ERROR_GET_EXIT_CODE;
	}
	else
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": process exited: %d\n", exitcode);

	return exitcode;
}

wil::unique_git_repository CGit::GetGitRepository() const
{
	// Through CAutoRepository for its Open(), the chokepoint that latches the
	// object format; the caller gets the plain owner back.
	CAutoRepository repo(TGitPath::ToGitPathA(m_CurrentDir.GetString()).c_str());
	return std::move(repo);
}

int CGit::GetHash(git_repository * repo, CGitHash &hash, const std::wstring_view friendname, const bool skipFastCheck /* = false */)
{
	ATLASSERT(repo);

	// no need to look up a ref if it's already an OID
	if (!skipFastCheck)
	{
		bool isHash = false;
		hash = CGitHash::FromHexStr(friendname, &isHash);
		if (isHash)
			return 0;
	}

	const int isHeadOrphan = git_repository_head_unborn(repo);
	if (isHeadOrphan != 0)
	{
		hash.Empty();
		if (isHeadOrphan == 1)
		{
			if (friendname == L"HEAD") // special check for unborn branch: if not requesting HEAD, do normal commit lookup
				return 0;
		}
		else
			return -1;
	}

	CAutoObject gitObject;
	if (git_revparse_single(gitObject.GetPointer(), repo, CUnicodeUtils::StdGetUTF8(friendname).c_str()))
		return -1;

	hash = git_object_id(gitObject);

	return 0;
}

int CGit::GetHash(CGitHash &hash, const std::wstring& friendname)
{
	// no need to look up a ref if it's already an OID
	bool hashOk = false;
	hash = CGitHash::FromHexStr(friendname, &hashOk);
	if (hashOk)
		return 0;

	if (m_IsUseLibGit2)
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return -1;

		return GetHash(repo, hash, friendname, true);
	}
	else
	{
		if (friendname.empty())
		{
			gitLastErr.Empty();
			return -1;
		}
		CString branch = FixBranchName(friendname.c_str());
		if (friendname == L"FETCH_HEAD" && branch.IsEmpty())
			branch = friendname.c_str();
		gitLastErr.Empty();
		const int ret = Run({ L"git.exe", L"rev-parse", L"--verify", L"--end-of-options", std::wstring(branch) }, &gitLastErr, nullptr, CP_UTF8);

		const std::wstring_view strsv = StringView(gitLastErr.Trim());
		bool isHash{};
		hash = CGitHash::FromHexStr(strsv, &isHash);
		if (ret == 0)
			gitLastErr.Empty();
		else if (friendname == L"HEAD") // special check for unborn branch
		{
			CString currentbranch;
			if (GetCurrentBranchFromFile(m_CurrentDir, currentbranch))
				return -1;
			gitLastErr.Empty();
			return 0;
		}
		return ret;
	}
}

int CGit::GetInitAddList(CTGitPathList& outputlist, const bool getStagingStatus)
{
	BYTE_VECTOR cmdout;

	outputlist.Clear();
	if (Run({ L"git.exe", L"ls-files", L"-s", L"-t", L"-z" }, &cmdout))
		return -1;

	if (outputlist.ParserFromLsFile(cmdout))
		return -1;
	for(int i = 0; i < outputlist.GetCount(); ++i)
		const_cast<CTGitPath&>(outputlist[i]).m_Action = CTGitPath::LOGACTIONS_ADDED;

	if (getStagingStatus)
	{
		BYTE_VECTOR cmdunstagedout;
		for (int i = 0; i < outputlist.GetCount(); ++i)
			const_cast<CTGitPath&>(outputlist[i]).m_stagingStatus = CTGitPath::StagingStatus::TotallyStaged;
		if (Run({ L"git.exe", L"diff-files", L"--raw", L"--numstat", L"-C", L"-M", L"-z", L"--" }, &cmdunstagedout))
			return -1;

		CTGitPathList unstaged;
		unstaged.ParserFromLog(cmdunstagedout);
		// File shows up both in the output of ls-files and diff-files: partially staged (typically modified after being added)
		for (int j = 0; j < unstaged.GetCount(); ++j)
		{
			const std::wstring& path = unstaged[j].GetGitPathString().c_str();
			outputlist.UpdateStagingStatusFromPath(path, CTGitPath::StagingStatus::PartiallyStaged); // TODO: This is inefficient
		}
	}
	return 0;
}

int CGit::GetSubmoduleHash(const CString& pathOfSubmodule, const CGitHash& revision, CGitHash& submoduleRevision, CString& err)
{
	if (!m_IsUseLibGit2)
		return 0;

	const CAutoRepository repository(g_Git.GetGitRepository());
	if (!repository)
	{
		err = GetLibGit2LastErr(L"Could not open repository.");
		return -1;
	}

	CAutoCommit commit;
	if (git_commit_lookup(&commit, repository, revision))
	{
		err = GetLibGit2LastErr(L"Could not lookup commit.");
		return -1;
	}

	CAutoTree tree;
	if (git_commit_tree(&tree, commit))
	{
		err = GetLibGit2LastErr(L"Could not get tree of commit.");
		return -1;
	}

	CAutoTreeEntry treeEntry;
	if (git_tree_entry_bypath(treeEntry.GetPointer(), tree, CUnicodeUtils::StdGetUTF8(StringView(pathOfSubmodule)).c_str()))
	{
		err = GetLibGit2LastErr(L"Could not lookup path.");
		return -1;
	}

	if (git_tree_entry_type(treeEntry) != GIT_OBJECT_COMMIT)
	{
		err.Format(L"No submodule found at \"%s\" in revision %s.", pathOfSubmodule, revision.ToString().c_str());
		return -1;
	}

	submoduleRevision = git_tree_entry_id(treeEntry);
	return 0;
}

int CGit::GetCommitDiffList(const CString& rev1, const CString& rev2, CTGitPathList& outputlist, CString& error, const bool ignoreSpaceAtEol, const bool ignoreSpaceChange, const bool ignoreAllSpace, const bool ignoreBlankLines)
{
	// One flag per element now, rather than one string of space-separated flags
	// that only worked because it was interpolated into a command line.
	STRING_VECTOR ignore;
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

	STRING_VECTOR argv{ L"git.exe", againstWorkingCopy ? L"diff" : L"diff-tree", L"-r" };
	if (againstWorkingCopy && !rev1IsWorkingCopy)
		argv.emplace_back(L"-R");
	argv.emplace_back(L"--raw");
	argv.emplace_back(std::format(L"-C{}%", ms_iSimilarityIndexThreshold));
	argv.emplace_back(std::format(L"-M{}%", ms_iSimilarityIndexThreshold));
	argv.emplace_back(L"--numstat");
	argv.emplace_back(L"-z");
	argv.insert(argv.cend(), ignore.cbegin(), ignore.cend());
	argv.emplace_back(L"--end-of-options");
	if (againstWorkingCopy)
		argv.emplace_back(std::wstring(rev1IsWorkingCopy ? rev2 : rev1));
	else
	{
		argv.emplace_back(std::wstring(rev2));
		argv.emplace_back(std::wstring(rev1));
	}
	argv.emplace_back(L"--");

	BYTE_VECTOR out;
	BYTE_VECTOR err;
	if (Run(argv, &out, &err))
	{
		error = err.Decode().c_str();
		return -1;
	}

	return outputlist.ParserFromLog(out);
}

int CGit::GetTagList(STRING_VECTOR &list)
{
	const auto pred = g_bSortTagsReversed ? details::LogicalCompareReversedPredicate : details::LogicalComparePredicate;
	const size_t prevCount = list.size();
	if (this->m_IsUseLibGit2)
	{
		const SmartLibgit2::CAutoRepository repo(GetGitRepository());
		if (!repo)
			return GFail;

		SmartLibgit2::CAutoStrArray tag_names;
		if (git_tag_list(tag_names, repo))
			return GFail;

		tag_names.AppendTo(list);
	}
	else
	{
		gitLastErr.Empty();
		const int ret = Run({ L"git.exe", L"tag", L"-l" }, [&](const std::string_view lineA)
		{
			if (lineA.empty())
				return;
			list.push_back(CUnicodeUtils::GetUnicode<std::wstring>(lineA));
		}, &gitLastErr);
		if (ret) {
			if (ret == 1 && IsInitRepos())
				return G_OK;
			return ret;
		}
	}
	std::sort(list.begin() + prevCount, list.end(), pred);
	return 0;
}

CString CGit::FixBranchName_Mod(CString& branchName)
{
	if (branchName == L"FETCH_HEAD")
		branchName = DerefFetchHead();
	return branchName;
}

CString	CGit::FixBranchName(const CString& branchName)
{
	CString tempBranchName = branchName;
	FixBranchName_Mod(tempBranchName);
	return tempBranchName;
}

bool CGit::IsBranchTagNameUnique(const CString& name)
{
	if (m_IsUseLibGit2)
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return true; // TODO: optimize error reporting

		CAutoReference tagRef;
		if (git_reference_lookup(tagRef.GetPointer(), repo, CUnicodeUtils::StdGetUTF8(StringView{L"refs/tags/" + name}).c_str()))
			return true;

		CAutoReference branchRef;
		if (git_reference_lookup(branchRef.GetPointer(), repo, CUnicodeUtils::StdGetUTF8(StringView(L"refs/heads/" + name)).c_str()))
			return true;

		return false;
	}
	// else
	int refCnt = 0;
	Run({ L"git.exe", L"show-ref", L"--tags", L"--heads", L"--", StringView(L"refs/heads/" + name).data(), StringView(L"refs/tags/" + name).data() }, [&](const std::string_view lineA)
	{
		if (lineA.empty())
			return;
		++refCnt;
	});

	return (refCnt <= 1);
}

bool CGit::IsLocalBranch(const CString& shortName)
{
	STRING_VECTOR list;
	GetBranchList(list, nullptr, TGitH::BRANCH_LOCAL);
	return std::find(list.cbegin(), list.cend(), StringView(shortName)) != list.cend();
}

bool CGit::BranchTagExists(const CString& name, const bool isBranch /*= true*/)
{
	if (m_IsUseLibGit2)
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return false; // TODO: optimize error reporting

		const auto prefix = isBranch ? L"refs/heads/" : L"refs/tags/";

		CAutoReference ref;
		if (git_reference_lookup(ref.GetPointer(), repo, CUnicodeUtils::StdGetUTF8(StringView(prefix + name)).c_str()))
			return false;

		return true;
	}
	// else
	// Note that both refs are always passed, whichever of --heads/--tags is
	// asked for; that predates the argv rewrite and is left as it was.
	const STRING_VECTOR argv{ L"git.exe", L"show-ref", isBranch ? L"--heads" : L"--tags", L"--", StringView(L"refs/heads/" + name).data(), StringView(L"refs/tags/" + name).data() };

	CString output;
	if (!Run(argv, &output, nullptr, CP_UTF8))
	{
		if (!output.IsEmpty())
			return true;
	}

	return false;
}

CString CGit::DerefFetchHead() const
{
	const std::wstring dotGitPath = GitAdminDir::GetWorktreeAdminDirPath(StringView(m_CurrentDir)).value_or(std::wstring{});
	std::ifstream fetchHeadFile(dotGitPath + L"FETCH_HEAD", std::ios::in | std::ios::binary);
	int forMergeLineCount = 0;
	std::string line;
	std::string hashToReturn;
	while(getline(fetchHeadFile, line))
	{
		//Tokenize this line
		std::string::size_type pos = line.find('\t');
		if(pos == std::string::npos)	continue; //invalid line

		const std::string hash = line.substr(0, pos);
		++pos;
		const std::string::size_type prevPos = pos;
		pos = line.find('\t', pos);
		if(pos == std::string::npos) continue;

		const bool forMerge = pos == prevPos;
		pos = line.size();
		if(pos == std::string::npos) continue;

		//std::string remoteBranch = line.substr(prevPos, pos - prevPos);

		//Process this line
		if(forMerge)
		{
			hashToReturn = hash;
			++forMergeLineCount;
			if(forMergeLineCount > 1)
				return {}; //More then 1 branch to merge found. Octopus merge needed. Cannot pick single ref from FETCH_HEAD
		}
	}

	return CUnicodeUtils::GetUnicode<CString>(hashToReturn);
}

int CGit::GetBranchList(STRING_VECTOR &list, int *current, const BRANCH_TYPE type, const bool skipCurrent /*false*/)
{
	const size_t prevCount = list.size();
	int ret = 0;
	std::wstring cur;
	bool headIsDetached = false;
	if (m_IsUseLibGit2)
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return -1;

		if (git_repository_head_detached(repo) == 1)
			headIsDetached = true;

		if ((type & (BRANCH_LOCAL | BRANCH_REMOTE)) != 0)
		{
			git_branch_t flags = GIT_BRANCH_LOCAL;
			if ((type & BRANCH_LOCAL) && (type & BRANCH_REMOTE))
				flags = GIT_BRANCH_ALL;
			else if (type & BRANCH_REMOTE)
				flags = GIT_BRANCH_REMOTE;

			CAutoBranchIterator it;
			if (git_branch_iterator_new(&it, repo, flags))
				return -1;

			CAutoReference ref;
			git_branch_t branchType;
			while (git_branch_next(&ref, &branchType, it) == 0)
			{
				const char * name = nullptr;
				if (git_branch_name(&name, ref))
					continue;

				std::wstring branchname = CUnicodeUtils::Convert2Unicode<std::wstring>(name);
				if (branchType & GIT_BRANCH_REMOTE)
					list.push_back(L"remotes/" + branchname);
				else
				{
					if (git_branch_is_head(ref))
					{
						if (skipCurrent)
							continue;
						cur = branchname;
					}
					list.push_back(branchname);
				}
			}
		}
	}
	else
	{
		CString cmd = L"git.exe branch --no-color";

		if ((type & BRANCH_ALL) == BRANCH_ALL)
			cmd += L" -a";
		else if (type & BRANCH_REMOTE)
			cmd += L" -r";

		ret = Run(cmd, [&](std::string_view lineA)
		{
			StringUtils::TrimLeft(lineA);
			if (lineA.empty())
				return;
			if (lineA.find(" -> ") != std::string_view::npos)
				return; // skip something like: refs/origin/HEAD -> refs/origin/master

			std::wstring branch = CUnicodeUtils::GetUnicode<std::wstring>(lineA);
			if (lineA[0] == '*')
			{
				if (skipCurrent)
					return;
				// tgit::wstr::Mid, not substr: these carry CString's clamping, and a
				// line shorter than the prefix would make substr throw where Mid
				// returned empty
				branch = tgit::wstr::Mid(branch, static_cast<int>(wcslen(L"* ")));
				cur = branch;

				// check whether HEAD is detached
				CString currentHead;
				if (branch[0] == L'(' && GetCurrentBranchFromFile(m_CurrentDir, currentHead) == 1)
				{
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

	if(type & BRANCH_FETCH_HEAD && !DerefFetchHead().IsEmpty())
		list.push_back(L"FETCH_HEAD");

	std::sort(list.begin() + prevCount, list.end(), details::LogicalCompareBranchesPredicate);

	if (current && !headIsDetached && !skipCurrent)
	{
		for (unsigned int i = 0; i < list.size(); ++i)
		{
			if (list[i] == cur)
			{
				*current = gsl::narrow_cast<int>(i);
				break;
			}
		}
	}

	return ret;
}

int CGit::GetRefsCommitIsOn(STRING_VECTOR& list, const CGitHash& hash, const bool includeTags, const bool includeBranches, const BRANCH_TYPE type) const
{
	if (!includeTags && !includeBranches || hash.IsEmpty())
		return 0;

	const size_t prevCount = list.size();
	CAutoRepository repo(GetGitRepository());
	if (!repo)
		return -1;

	CAutoReferenceIterator it;
	if (git_reference_iterator_new(&it, repo))
		return -1;

	auto checkDescendent = [&list, &hash, &repo](const git_oid* oid, const git_reference* ref) {
		if (!oid)
			return;
		if (git_oid_equal(oid, hash) || git_graph_descendant_of(repo, oid, hash) == 1)
		{
			const char* name = git_reference_name(ref);
			if (!name)
				return;

			list.push_back(CUnicodeUtils::Convert2Unicode<std::wstring>(name));
		}
	};

	CAutoReference ref;
	while (git_reference_next(&ref, it) == 0)
	{
		if (git_reference_is_tag(ref))
		{
			if (!includeTags)
				continue;

			CAutoTag tag;
			if (git_tag_lookup(&tag, repo, git_reference_target(ref)) == 0)
			{
				CAutoObject obj;
				if (git_tag_peel(&obj, tag) < 0)
					continue;
				checkDescendent(git_object_id(obj), ref);
				continue;
			}
		}
		else if (git_reference_is_remote(ref))
		{
			if (!includeBranches || !(type & BRANCH_REMOTE))
				continue;
		}
		else if (git_reference_is_branch(ref))
		{
			if (!includeBranches || !(type & BRANCH_LOCAL))
				continue;
		}
		else
			continue;

		if (git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC)
		{
			CAutoReference peeledRef;
			if (git_reference_resolve(&peeledRef, ref) < 0)
				continue;

			checkDescendent(git_reference_target(peeledRef), ref);
			continue;
		}

		checkDescendent(git_reference_target(ref), ref);
	}

	std::sort(list.begin() + prevCount, list.end(), details::LogicalCompareBranchesPredicate);
	list.erase(std::unique(list.begin() + prevCount, list.end()), list.end());

	return 0;
}

int CGit::GetRemoteList(STRING_VECTOR &list)
{
	const size_t prevCount = list.size();
	if (this->m_IsUseLibGit2)
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return -1;

		CAutoStrArray remotes;
		if (git_remote_list(remotes, repo))
			return -1;

		remotes.AppendTo(list);

		std::sort(list.begin() + prevCount, list.end(), details::LogicalComparePredicate);

		return 0;
	}

	gitLastErr.Empty();
	return Run({ L"git.exe", L"remote" }, [&](const std::string_view lineA)
	{
		if (lineA.empty())
			return;
		list.push_back(CUnicodeUtils::GetUnicode<std::wstring>(lineA));
	}, &gitLastErr);
}

int CGit::GetRemoteRefs(const CString& remote, REF_VECTOR& list, const bool includeTags, const bool includeBranches)
{
	if (!includeTags && !includeBranches)
		return 0;

	const size_t prevCount = list.size();
	// GIT_CMD_FETCH is the one gate in this file that is deliberately left alone:
	// it also selects between two whole dialogs in CAppUtils::Fetch and SyncDlg,
	// so it cannot become libgit2-only here without GetGitLastErr(.., GIT_CMD_FETCH)
	// starting to report the wrong side's error at the call sites of this function.
	if (UsingLibGit2(GIT_CMD_FETCH))
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return -1;

		const std::string remoteA = CUnicodeUtils::StdGetUTF8(StringView(remote));
		CAutoRemote gitremote;
		// first try with a named remote (e.g. "origin")
		if (git_remote_lookup(gitremote.GetPointer(), repo, remoteA.c_str()) < 0)
		{
			// retry with repository located at a specific url
			if (git_remote_create_anonymous(gitremote.GetPointer(), repo, remoteA.c_str()) < 0)
				return -1;
		}

		git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;
		callbacks.credentials = g_Git2CredCallback;
		callbacks.certificate_check = g_Git2CheckCertificateCallback;
		git_proxy_options proxy = GIT_PROXY_OPTIONS_INIT;
		proxy.type = GIT_PROXY_AUTO;
		if (git_remote_connect(gitremote, GIT_DIRECTION_FETCH, &callbacks, &proxy, nullptr) < 0)
			return -1;

		const git_remote_head** heads = nullptr;
		size_t size = 0;
		if (git_remote_ls(&heads, &size, gitremote) < 0)
			return -1;

		for (size_t i = 0; i < size; i++)
		{
			auto ref = CUnicodeUtils::Convert2Unicode<CString>(heads[i]->name);
			CString shortname;
			if (details::GetShortName(ref, shortname, L"refs/tags/"))
			{
				if (!includeTags)
					continue;
			}
			else
			{
				if (!includeBranches)
					continue;
				if (!details::GetShortName(ref, shortname, L"refs/heads/"))
					shortname = ref;
			}
			if (includeTags && includeBranches)
				list.emplace_back(TGitRef{ std::wstring(ref), CGitHash(&heads[i]->oid) });
			else
				list.emplace_back(TGitRef{ std::wstring(shortname), CGitHash(&heads[i]->oid) });
		}
		std::sort(list.begin() + prevCount, list.end(), g_bSortTagsReversed && includeTags && !includeBranches ? details::LogicalCompareRefReversedPredicate : details::LogicalCompareRefPredicate);
		return 0;
	}

	const STRING_VECTOR argv{ L"git.exe", L"ls-remote", (includeTags && !includeBranches) ? L"-t" : L"--refs", L"--", std::wstring(remote) };
	// The command line is echoed into the error output, so it still has to be
	// serialized here even though nothing else needs it flat.
	gitLastErr = SerializeArgvToCString(argv) + L'\n';

	[[maybe_unused]]const auto _ = gsl::finally([] { TGitH::s_limitGitExeOutput = false; });
	TGitH::s_limitGitExeOutput = true;
	if (Run(
		argv, [&](const std::string_view origLineA) {
			if (origLineA.size() <= GitFormatHolder::GitHashSize() * 2 + strlen("\t") || origLineA[GitFormatHolder::GitHashSize() * 2] != '\t') // OID, tab, refname
				return;
			const CGitHash hash = CGitHash::FromHexStr(origLineA.substr(0, GitFormatHolder::GitHashSize() * 2));
			const auto ref = CUnicodeUtils::GetUnicode<CString>(origLineA.substr(GitFormatHolder::GitHashSize() * 2 + strlen("\t")));
			CString shortname;
			if (details::GetShortName(ref, shortname, L"refs/tags/"))
			{
				if (!includeTags)
					return;
			}
			else
			{
				if (!includeBranches)
					return;
				if (!details::GetShortName(ref, shortname, L"refs/heads/"))
					shortname = ref;
			}
			if (includeTags && includeBranches)
				list.emplace_back(TGitRef{ std::wstring(ref), hash });
			else if (includeTags && StringUtils::EndsWith(ref, L"^{}"))
				list.emplace_back(TGitRef{ std::wstring(shortname + L"^{}"), hash });
			else
				list.emplace_back(TGitRef{ std::wstring(shortname), hash });
		},
		&gitLastErr))
		return -1;
	std::sort(list.begin() + prevCount, list.end(), g_bSortTagsReversed && includeTags && !includeBranches ? details::LogicalCompareRefReversedPredicate : details::LogicalCompareRefPredicate);
	return 0;
}

int CGit::GetRefList(STRING_VECTOR &list)
{
	const size_t prevCount = list.size();
	if (this->m_IsUseLibGit2)
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return -1;

		auto callback = [](git_reference* rawref, void* payload) -> int
		{
			// the callback owns the reference
			const auto ref = wil::unique_git_reference(rawref);
			const auto list = static_cast<STRING_VECTOR*>(payload);

			const auto multibyte = git_reference_name(ref.get());
			list->push_back(CUnicodeUtils::Convert2Unicode<std::wstring>(multibyte));

			return GIT_OK;
		};


		if (git_reference_foreach(repo, callback, &list))
			return -1;

		std::sort(list.begin() + prevCount, list.end(), details::LogicalComparePredicate);

		return 0;
	}

	gitLastErr.Empty();
	const int ret = Run({ L"git.exe", L"show-ref", L"-d" }, [&](const std::string_view lineA)
	{
		const size_t start = lineA.find(' ');
		ATLASSERT(start == 2 * GitFormatHolder::GitHashSize());
		if (start != 2 * GitFormatHolder::GitHashSize() || start >= lineA.size())
			return;

		std::wstring name = CUnicodeUtils::GetUnicode<std::wstring>(lineA.substr(start + 1));
		if (list.empty() || name != *list.crbegin() + L"^{}")
			list.push_back(std::move(name));
	}, &gitLastErr);
	if (!ret)
		std::sort(list.begin() + prevCount, list.end(), details::LogicalComparePredicate);
	else if (ret == 1 && IsInitRepos())
		return 0;
	return ret;
}

int CGit::GetMapHashToFriendName(MAP_HASH_NAME &map)
{
	if (this->m_IsUseLibGit2)
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return -1;

		return details::GetMapHashToFriendName(repo, map);
	}

	gitLastErr.Empty();
	const int ret = Run({ L"git.exe", L"show-ref", L"-d" }, [&](const std::string_view lineA)
	{
		const size_t start = lineA.find(' ');
		ATLASSERT(start == 2 * GitFormatHolder::GitHashSize());
		if (start != 2 * GitFormatHolder::GitHashSize() || start >= lineA.size())
			return;

		const CGitHash hash = CGitHash::FromHexStr(lineA.substr(0, start));
		map[hash].push_back(CUnicodeUtils::GetUnicode<std::wstring>(lineA.substr(start + 1)));
	}, &gitLastErr);

	if (ret == 1 && IsInitRepos())
		return 0;
	return ret;
}

int CGit::GuessRefForHash(CString& ref, const CGitHash& hash)
{
	MAP_HASH_NAME map;
	if (GetMapHashToFriendName(map))
		return -1;

	const auto it = map.find(hash);
	if (it == map.cend())
	{
		ref = hash.ToString(GetShortHASHLength()).c_str();
		return 1;
	}

	const auto& reflist = it->second;
	for (const auto& reftype : { L"refs/heads/", L"refs/remotes/", L"refs/tags/" })
	{
		auto found = std::find_if(reflist.cbegin(), reflist.cend(), [&reftype](const auto& ref) { return ref.starts_with(reftype); });
		if (found == reflist.cend())
			continue;

		details::GetShortName(found->c_str(), ref, reftype);
		return 0;
	}

	ref = hash.ToString(GetShortHASHLength()).c_str();
	return 1;
}

int CGit::GetBranchDescriptions(MAP_STRING_STRING& map) const
{
	const CAutoConfig config(true);
	if (git_config_add_file_ondisk(config, TGitPath::ToGitPathA(StringView(GetGitLocalConfig())).c_str(), GIT_CONFIG_LEVEL_LOCAL, nullptr, FALSE) < 0)
		return -1;
	return git_config_foreach_match(config, "^branch\\..*\\.description$", [](const git_config_entry* entry, void* data)
	{
		const auto descriptions = static_cast<MAP_STRING_STRING*>(data);
		// extract branch name from config key; the match pattern above guarantees
		// both affixes are present, so the length arithmetic cannot underflow
		constexpr size_t prefix = std::size(L"branch.") - 1;
		constexpr size_t suffix = std::size(L".description") - 1;
		const std::wstring key = CUnicodeUtils::Convert2Unicode<std::wstring>(entry->name);
		descriptions->emplace(key.substr(prefix, key.size() - prefix - suffix), CUnicodeUtils::Convert2Unicode<std::wstring>(entry->value));
		return 0;
	}, &map);
}

BOOL CGit::CheckMsysGitDir(const BOOL bFallback)
{
	if (m_bInitialized)
		return TRUE;

	CTraceToOutputDebugString::Instance()(__FUNCTION__ L": CheckMsysGitDir(%d)\n", bFallback);
	this->m_Environment.clear();
	m_Environment.CopyProcessEnvironment();

	m_Environment.SetEnv(L"TGIT_INITIATED_CALL", L"1");

	// Sanitize GIT_* environment variables, cf. https://github.com/git-for-windows/build-extra/pull/529 and git/environment.h
	m_Environment.UnsetEnv(L"GIT_INDEX_FILE");
	m_Environment.UnsetEnv(L"GIT_INDEX_VERSION");
	m_Environment.UnsetEnv(L"GIT_OBJECT_DIRECTORY");
	m_Environment.UnsetEnv(L"GIT_ALTERNATE_OBJECT_DIRECTORIES");
	m_Environment.UnsetEnv(L"GIT_DIR");
	m_Environment.UnsetEnv(L"GIT_WORK_TREE");
	m_Environment.UnsetEnv(L"GIT_NAMESPACE");
	m_Environment.UnsetEnv(L"GIT_CEILING_DIRECTORIES");
	m_Environment.UnsetEnv(L"GIT_DISCOVERY_ACROSS_FILESYSTEM");
	m_Environment.UnsetEnv(L"GIT_COMMON_DIR");
	m_Environment.UnsetEnv(L"GIT_DEFAULT_HASH");
	m_Environment.UnsetEnv(L"GIT_CONFIG");
	m_Environment.UnsetEnv(L"GIT_CONFIG_GLOBAL");
	m_Environment.UnsetEnv(L"GIT_CONFIG_SYSTEM");
	m_Environment.UnsetEnv(L"GIT_CONFIG_NOSYSTEM");
	m_Environment.UnsetEnv(L"GIT_CONFIG_COUNT");
	m_Environment.UnsetEnv(L"GIT_ATTR_NOSYSTEM");
	m_Environment.UnsetEnv(L"GIT_ATTR_SOURCE");
	m_Environment.UnsetEnv(L"GIT_SHALLOW_FILE");
	m_Environment.UnsetEnv(L"GIT_GRAFT_FILE");

	// Git for Windows 2.10.1 and 2.10.2 require LC_ALL to be set, see https://tortoisegit.org/issue/2859 and https://github.com/git-for-windows/git/issues/945,
	// because MSys2 changed the default to "ASCII". SO, make sure we have a proper default set
	if (m_Environment.GetEnv(L"LC_ALL").empty())
		m_Environment.SetEnv(L"LC_ALL", L"C");

	// set HOME if not set already
	size_t homesize;
	_wgetenv_s(&homesize, nullptr, 0, L"HOME");
	if (!homesize)
		m_Environment.SetEnv(L"HOME", StringView(GetHomeDirectory()));

	//setup ssh client
	auto sshclient = GetConfiguredSshClient();

	if(!sshclient.empty())
	{
		if (ms_bCygwinGit)
			std::replace(sshclient.begin(), sshclient.end(), L'\\', L'/');
		m_Environment.SetEnv(L"GIT_SSH", sshclient);
		m_Environment.SetEnv(L"SVN_SSH", sshclient);
	}
	else
	{
		// The default is OpenSSH, which ships with Windows and needs no path: git and
		// ssh-wintunnel both resolve it from PATH. This must name *something* — the libgit2
		// transport hard-errors with "No GIT_SSH tool configured" when the variable is unset.
		// It is a console program with no console here, so its prompts reach the user through
		// SSH_ASKPASS/SshAskPass.exe, set below.
		m_Environment.SetEnv(L"GIT_SSH", L"ssh.exe");
		m_Environment.SetEnv(L"SVN_SSH", L"ssh.exe");
	}

	{
		// SshAskPass.exe, next to this executable
		std::wstring sAskPass = PathUtils::GetModuleFileNameString(nullptr);
		if (const size_t slash = sAskPass.rfind(L'\\'); slash != std::wstring::npos)
		{
			sAskPass.resize(slash + 1);
			sAskPass += L"SshAskPass.exe";
			if (ms_bCygwinGit)
				std::ranges::replace(sAskPass, L'\\', L'/');
			m_Environment.SetEnv(L"DISPLAY",L":9999");
			m_Environment.SetEnv(L"SSH_ASKPASS", sAskPass.c_str());
			m_Environment.SetEnv(L"GIT_ASKPASS", sAskPass.c_str());
			m_Environment.SetEnv(L"GIT_ASK_YESNO", sAskPass.c_str());
			m_Environment.SetEnv(L"SSH_ASKPASS_REQUIRE", L"force"); // improve compatibility with Win32-OpenSSH, see issue #3996
		}
	}

	if (!details::FindAndSetGitExePath(bFallback))
		return FALSE;

	CString msysGitDir;
	PathCanonicalize(CStrBuf(msysGitDir, MAX_PATH), TGitH::ms_LastMsysGitDir + L"\\..\\");
	static const CString prefixes[] = { L"mingw64\\etc", L"mingw32\\etc", L"etc" };
	static const int prefixes_len[] = { 8, 8, 0 };
	for (int i = 0; i < _countof(prefixes); ++i)
	{
#ifndef _WIN64
		if (i == 0)
			continue;
#endif
		if (PathIsDirectory(msysGitDir + prefixes[i])) {
			msysGitDir += prefixes[i].Left(prefixes_len[i]);
			break;
		}
	}
	if (ms_bMsys2Git) // in Msys2 git.exe is in usr\bin; this also need to be after the check for etc folder, as Msys2 also has mingw64\etc, but uses etc
		PathCanonicalize(CStrBuf(msysGitDir, MAX_PATH), TGitH::ms_LastMsysGitDir + L"\\..\\..");
	TGitH::ms_MsysGitRootDir = msysGitDir;

	if (const std::wstring_view systemConfig = StringView(g_Git.GetGitSystemConfig()); wil::reg::get_value_string(REG_SYSTEM_GITCONFIGPATH) != systemConfig)
		std::ignore = wil::reg::set_value_string(REG_SYSTEM_GITCONFIGPATH, systemConfig);

	CTraceToOutputDebugString::Instance()(__FUNCTION__ L": ms_LastMsysGitDir = %s\n", static_cast<LPCWSTR>(TGitH::ms_LastMsysGitDir));
	CTraceToOutputDebugString::Instance()(__FUNCTION__ L": ms_MsysGitRootDir = %s\n", static_cast<LPCWSTR>(TGitH::ms_MsysGitRootDir));
	CTraceToOutputDebugString::Instance()(__FUNCTION__ L": System config = %s\n", static_cast<LPCWSTR>(g_Git.GetGitSystemConfig()));
	details::SetLibGit2SearchPath(GIT_CONFIG_LEVEL_PROGRAMDATA, L"");

	if (ms_bCygwinGit)
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": CygwinHack: true\n");
	if (ms_bMsys2Git)
		CTraceToOutputDebugString::Instance()(__FUNCTION__ L": Msys2Hack: true\n");

	ms_LastMsysGitVersion = Registry::CRegDWORD(L"Software\\TortoiseGit\\git_cached_version");

	// Configure libgit2 search paths
	details::SetLibGit2SearchPath(GIT_CONFIG_LEVEL_SYSTEM, (CTGitPath(std::wstring_view{g_Git.GetGitSystemConfig().GetString()}).GetContainingDirectory().GetWinPathString().c_str()));
	details::SetLibGit2SearchPath(GIT_CONFIG_LEVEL_GLOBAL, g_Git.GetHomeDirectory());
	details::SetLibGit2SearchPath(GIT_CONFIG_LEVEL_XDG, g_Git.GetGitGlobalXDGConfig(true));
	static git_smart_subtransport_definition ssh_wintunnel_subtransport_definition = {
		.callback{
			[](git_smart_subtransport **out, git_transport* owner, void*) -> int {
				const auto sshtoolpath = details::FindExecutableOnPath(g_Git.m_Environment.GetEnv(L"GIT_SSH").c_str(), g_Git.m_Environment.GetEnv(L"PATH").c_str());
				const auto ret = git_smart_subtransport_ssh_wintunnel(out, owner, sshtoolpath, g_Git.m_Environment.get_cpp());
				return ret;
			}
		},
		.rpc{},
		.param{}
	};
	git_transport_register("ssh", git_transport_smart, &ssh_wintunnel_subtransport_definition);
	git_transport_register("ssh+git", git_transport_smart, &ssh_wintunnel_subtransport_definition);
	git_transport_register("git+ssh", git_transport_smart, &ssh_wintunnel_subtransport_definition);
	git_libgit2_opts(GIT_OPT_SET_USER_AGENT, "TortoiseGit libgit2");
	if (!(ms_bCygwinGit || ms_bMsys2Git))
		details::SetLibGit2TemplatePath(TGitH::ms_MsysGitRootDir + L"share\\git-core\\templates");
	else
		details::SetLibGit2TemplatePath(TGitH::ms_MsysGitRootDir + L"usr\\share\\git-core\\templates");

	m_Environment.AddToPath(std::wstring(TGitH::ms_LastMsysGitDir));
	m_Environment.AddToPath(wil::reg::get_value_string(REG_MSYSGIT_EXTRA_PATH));

#if !defined(TGITCACHE) && !defined(TORTOISESHELL)
	// register filter only once
	if (!git_filter_lookup("filter"))
	{
		CString sh;
		for (const CString& binDirPrefix : { L"\\..\\usr\\bin", L"\\..\\bin", L"" })
		{
			CString possibleShExe = TGitH::ms_LastMsysGitDir + binDirPrefix + L"\\sh.exe";
			if (PathFileExists(possibleShExe))
			{
				CString temp;
				PathCanonicalize(CStrBuf(temp, MAX_PATH), possibleShExe);
				sh.Format(L"\"%s\"", static_cast<LPCWSTR>(temp));
				// we need to put the usr\bin folder on the path for Git for Windows based on msys2
				m_Environment.AddToPath(std::wstring(temp.Left(temp.GetLength() - static_cast<int>(wcslen(L"\\sh.exe")))));
				break;
			}
		}

		// Add %GIT_EXEC_PATH% to %PATH% when launching libgit2 filter executable
		// It is possible that the filter points to a git subcommand, that is located at libexec\git-core
		CString gitExecPath = TGitH::ms_MsysGitRootDir;
		gitExecPath.Append(L"libexec\\git-core");
		m_Environment.AddToPath(std::wstring(gitExecPath));

		if (git_filter_register("filter", git_filter_filter_new(sh, m_Environment.get_cpp()), GIT_FILTER_DRIVER_PRIORITY))
			return FALSE;
	}
#endif

	m_bInitialized = true;
	return true;
}

CString CGit::GetHomeDirectory() const
{
	static CString homeDirectory;

	if (!homeDirectory.IsEmpty())
		return homeDirectory;

	homeDirectory = m_Environment.GetEnv(L"HOME").c_str();
	if (!homeDirectory.IsEmpty())
		return homeDirectory;

	if (CString tmp = m_Environment.GetEnv(L"HOMEDRIVE").c_str(); !tmp.IsEmpty())
	{
		if (const CString tmp2 = m_Environment.GetEnv(L"HOMEPATH").c_str(); !tmp2.IsEmpty())
		{
			tmp += tmp2;
			if (CString windowsSysDirectory; GetSystemDirectory(CStrBuf(windowsSysDirectory, 4096), 4096) != FALSE && windowsSysDirectory != tmp && PathIsDirectory(tmp))
			{
				homeDirectory = tmp;
				return homeDirectory;
			}
		}
	}

	if (const CString tmp = m_Environment.GetEnv(L"USERPROFILE").c_str(); !tmp.IsEmpty())
	{
		homeDirectory = tmp;
		return homeDirectory;
	}

	return homeDirectory;
}

CString CGit::GetGitLocalConfig() const
{
	const auto adminDir = GitAdminDir::GetAdminDirPath(StringView(m_CurrentDir));
	return (adminDir.value_or(GitAdminDir::AdminDir{}).path + L"config").c_str();
}

CString CGit::GetGitGlobalConfig() const
{
	return g_Git.GetHomeDirectory() + L"\\.gitconfig";
}

CString CGit::GetGitGlobalXDGConfig(const bool returnDirectory) const
{
	// Attention: also see GitWCRev/status.cpp!
	if (CString xdgPath = m_Environment.GetEnv(L"XDG_CONFIG_HOME").c_str(); !xdgPath.IsEmpty())
	{
		xdgPath += L"\\git";
		if (!returnDirectory)
			xdgPath += L"\\config";
		return xdgPath;
	}

	if (CString appData = m_Environment.GetEnv(L"APPDATA").c_str(); !appData.IsEmpty() && !ms_bCygwinGit && !ms_bMsys2Git)
	{
		appData += L"\\Git";
		CString appDataConfigFile = appData + L"\\config";
		if (PathFileExists(appDataConfigFile))
		{
			if (returnDirectory)
				return appData;
			return appDataConfigFile;
		}
	}

	CString xdgInHome = g_Git.GetHomeDirectory();
	xdgInHome += L"\\.config\\git";
	if (returnDirectory)
		return xdgInHome;

	return xdgInHome + L"\\config";
}

CString CGit::GetGitSystemConfig() const
{
	return wget_msysgit_etc(static_cast<CEnvironment>(m_Environment).get_cpp());
}

CString CGit::GetNotesRef() const
{
	return CUnicodeUtils::Convert2Unicode<CString>(git_default_notes_ref());
}

BOOL CGit::CheckCleanWorkTree(const bool stagedOk /* false */)
{
	const CAutoRepository repo(GetGitRepository());
	if (!repo)
		return FALSE;

	if (git_repository_head_unborn(repo))
		return FALSE;

	git_status_options statusopt = GIT_STATUS_OPTIONS_INIT;
	statusopt.show = stagedOk ? GIT_STATUS_SHOW_WORKDIR_ONLY : GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	statusopt.flags = GIT_STATUS_OPT_UPDATE_INDEX | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;

	CAutoStatusList status;
	if (git_status_list_new(&status, repo, &statusopt))
		return FALSE;

	return (0 == git_status_list_entrycount(status));
}

BOOL CGit::IsResultingCommitBecomeEmpty(const bool amend /* = false */)
{
	// "HEAD~1" is one revision, so the suffix is part of the element rather than
	// something appended to the command line.
	return Run({ L"git.exe", L"diff-index", L"--cached", L"--quiet", amend ? L"HEAD~1" : L"HEAD", L"--" }, nullptr, nullptr, CP_UTF8) == 0;
}

int CGit::HasWorkingTreeConflicts(git_repository* repo /* = nullptr */) const {
prologoue:
	[[maybe_unused]] CAutoRepository def_repo;
	if (!repo) {
		def_repo = CAutoRepository(GetGitRepository());
		if (!def_repo)
			return GFail;
		repo = def_repo.get();
	}
body:
	CAutoIndex index;
	if (git_repository_index(&index, repo))
		return GFail;

	return git_index_has_conflicts(index);
}


bool CGit::IsFastForward(const CString &from, const CString &to, CGitHash * commonAncestor)
{
	const CAutoRepository repo(GetGitRepository());
	if (!repo)
		return false;

	CGitHash fromHash, toHash, baseHash;
	if (GetHash(repo, toHash, FixBranchName(to).GetBuffer()))
		return false;

	if (GetHash(repo, fromHash, FixBranchName(from).GetBuffer()))
		return false;

	git_oid baseOid;
	if (git_merge_base(&baseOid, repo, toHash, fromHash))
		return false;

	baseHash = baseOid;

	if (commonAncestor)
		*commonAncestor = baseHash;

	return fromHash == baseHash;
}

int CGit::RefreshGitIndex()
{
	// HACK: don't use internal update-index if we have a git-lfs enabled repository as the libgit version fails when executing the filter, issue #3220
	if (g_Git.m_IsUseGitDLL && !TGitPath::CTGitPath(std::wstring_view{g_Git.m_CurrentDir.GetString()}).HasLFS())
	{
		[[maybe_unused]]CAutoLocker lock(g_Git.m_critGitDllSec);
		try
		{
			g_Git.CheckAndInitDll();

			const auto cleanup = gsl::finally([] { git_exit_cleanup(); });
			return git_update_index();
		}catch(...)
		{
		}
		return -1;
	}
	else
		return Run({ L"git.exe", L"update-index", L"--refresh" }, nullptr, nullptr, CP_UTF8);
}

int CGit::GetOneFile(const CString &Refname, const CTGitPath &path, const CString &outputfile)
{
	// The other branch here is gitdll, not git.exe, so this gate stays: it runs
	// git's own checkout and with it the configured smudge filters, which
	// git_blob_filter below does not - that is the difference on an LFS repository.
	if (UsingLibGit2(GIT_CMD_GETONEFILE) || !g_Git.m_IsUseGitDLL)
	{
		const CAutoRepository repo(GetGitRepository());
		if (!repo)
			return -1;

		CGitHash hash;
		const auto tmp = std::wstring{ Refname } + L"^{}";
		if (GetHash(repo, hash, tmp)) // add ^{} in order to dereference signed tags
			return -1;

		CAutoCommit commit;
		if (git_commit_lookup(&commit, repo, hash))
			return -1;

		CAutoTree tree;
		if (git_commit_tree(&tree, commit))
			return -1;

		CAutoTreeEntry entry;
		if (const auto ret = git_tree_entry_bypath(&entry, tree, CUnicodeUtils::StdGetUTF8(path.GetGitPathString().c_str()).c_str()); ret)
			return ret;

		if (git_tree_entry_filemode(entry) == GIT_FILEMODE_COMMIT)
		{
			git_error_set_str(GIT_ERROR_NONE, "The requested object is a submodule and not a file.");
			return -1;
		}

		CAutoBlob blob;
		if (git_tree_entry_to_object(reinterpret_cast<git_object**>(&blob), repo, entry))
			return -1;

		const wil::CAutoFile file{_wfsopen(outputfile, L"wb", SH_DENYWR)};
		if (!file)
		{
			git_error_set_str(GIT_ERROR_NONE, "Could not create file.");
			return -1;
		}
		CAutoBuf buf;
		git_blob_filter_options opts = GIT_BLOB_FILTER_OPTIONS_INIT;
		opts.flags &= ~static_cast<uint32_t>(GIT_BLOB_FILTER_CHECK_FOR_BINARY);
		if (git_blob_filter(buf, blob, CUnicodeUtils::StdGetUTF8(path.GetGitPathString().c_str()).c_str(), &opts))
			return -1;
		if (fwrite(buf->ptr, sizeof(char), buf->size, file.get()) != buf->size)
		{
			git_error_set_str(GIT_ERROR_OS, "Could not write to file.");
			return -1;
		}

		return 0;
	}

	CAutoLocker _(g_Git.m_critGitDllSec);
	try
	{
		g_Git.CheckAndInitDll();
		CStringA ref, patha, outa;
		ref = CUnicodeUtils::GetUTF8<CStringA>(Refname);
		patha = CUnicodeUtils::StdGetUTF8(path.GetGitPathString().c_str()).c_str();
		outa = CUnicodeUtils::GetUTF8<CStringA>(outputfile);
		::DeleteFile(outputfile);
		return git_checkout_file(ref, patha, CStrBufA(outa));
	}
	catch (const char * msg)
	{
		gitLastErr = L"gitdll.dll reports: " + CUnicodeUtils::Convert2Unicode<CString>(msg);
		return -1;
	}
	catch (...)
	{
		gitLastErr = L"An unknown gitdll.dll error occurred.";
		return -1;
	}
}


int CGit::GetUnifiedDiff(const CTGitPath& path, const CString& rev1, const CString& rev2, CString patchfile, const bool bMerge, const bool bCombine, const int diffContext, const bool bNoPrefix)
{
	// The remaining git.exe path is not a fallback but a capability fork, of the
	// same kind as the `--cc` in GitLogListAction: libgit2 has no combined-diff
	// support at all, so `-c` only ever had this implementation and always will.
	if (bCombine)
	{
		gitLastErr.Empty();
		const int ret = RunLogFile(details::GetUnifiedDiffCmd(path, rev1, rev2, bMerge, bCombine, diffContext, bNoPrefix), patchfile, &gitLastErr);
		// GIT_CMD_DIFF is libgit2-only now, so GetGitLastErr(.., GIT_CMD_DIFF) reads
		// libgit2's error slot. Put git.exe's message there rather than leaving the
		// one path that still shells out reporting nothing.
		if (ret)
			git_error_set_str(GIT_ERROR_NONE, CUnicodeUtils::GetUTF8<CStringA>(gitLastErr));
		return ret;
	}

	const wil::CAutoFILE file{_wfsopen(patchfile, L"wb", SH_DENYRW)};
	if (!file)
		return -1;
	return details::GetUnifiedDiffLibGit2(path, rev1, rev2,  details::UnifiedDiffStatToFile,  details::UnifiedDiffToFile, file.get(), bMerge, diffContext, bNoPrefix);
}

int CGit::GetUnifiedDiff(const CTGitPath& path, const CString& rev1, const CString& rev2, CStringA& buffer, const bool bMerge, const bool bCombine, const int diffContext)
{
	if (!bCombine) // see the note on the overload above
		return  details::GetUnifiedDiffLibGit2(path, rev1, rev2, UnifiedDiffStatToStringA, UnifiedDiffToStringA, &buffer, bMerge, diffContext, false);

	BYTE_VECTOR vector;
	const int ret = Run( details::GetUnifiedDiffCmd(path, rev1, rev2, bMerge, bCombine, diffContext), &vector);
	if (!vector.empty())
		buffer.Append(vector.data(), CUnicodeUtils::SafeSizeToInt(vector.size()));
	return ret;
}

int CGit::GitRevert(const int parent, const CGitHash &hash)
{
	const CAutoRepository repo(GetGitRepository());
	if (!repo)
		return -1;

	CAutoCommit commit;
	if (git_commit_lookup(&commit, repo, hash))
		return -1;

	git_revert_options revert_opts = GIT_REVERT_OPTIONS_INIT;
	revert_opts.mainline = parent;
	return !git_revert(repo, commit, &revert_opts) ? 0 : -1;
}

int CGit::DeleteRef(const CString& reference)
{
	const CAutoRepository repo(GetGitRepository());
	if (!repo)
		return -1;

	CStringA refA;
	if (StringUtils::EndsWith(reference, L"^{}"))
		refA = CUnicodeUtils::GetUTF8<CStringA>(reference.Left(reference.GetLength() - static_cast<int>(wcslen(L"^{}"))));
	else
		refA = CUnicodeUtils::GetUTF8<CStringA>(reference);

	CAutoReference ref;
	if (git_reference_lookup(&ref, repo, refA))
		return -1;

	int result = -1;
	if (git_reference_is_tag(ref))
		result = git_tag_delete(repo, git_reference_shorthand(ref));
	else if (git_reference_is_branch(ref))
		result = git_branch_delete(ref);
	else if (git_reference_is_remote(ref))
		result = git_branch_delete(ref);
	else
		result = git_reference_delete(ref);

	return result;
}

// This lived in src\TortoiseProc\CGit_2.cpp, which existed only because its
// git.exe fallback used CMassiveGitTaskBase - an MFC-aware header that a file
// compiled by TortoiseShell and TGitCache cannot reach. With the fallback gone
// so is the reason for the split.
int CGit::DeleteRemoteRefs(const CString& sRemote, const STRING_VECTOR& list)
{
	const CAutoRepository repo(GetGitRepository());
	if (!repo)
		return -1;

	const CStringA remoteA = CUnicodeUtils::GetUTF8<CStringA>(sRemote);
	CAutoRemote remote;
	if (git_remote_lookup(&remote, repo, remoteA) < 0)
		return -1;

	git_push_options pushOpts = GIT_PUSH_OPTIONS_INIT;
	git_remote_callbacks& callbacks = pushOpts.callbacks;
	callbacks.credentials = g_Git2CredCallback;
	callbacks.certificate_check = g_Git2CheckCertificateCallback;
	std::vector<std::string> refspecs;
	refspecs.reserve(list.size());
	std::transform(list.cbegin(), list.cend(), std::back_inserter(refspecs), [](const auto& ref) { return CUnicodeUtils::StdGetUTF8(L":" + ref); });

	std::vector<char*> vc;
	vc.reserve(refspecs.size());
	// data() rather than CStringA::GetBuffer(): std::string has guaranteed
	// contiguous, null-terminated storage and needs no matching release
	std::transform(refspecs.begin(), refspecs.end(), std::back_inserter(vc), [](std::string& s) -> char* { return s.data(); });
	const git_strarray specs = { vc.data(), vc.size() };

	if (git_remote_push(remote, &specs, &pushOpts) < 0)
		return -1;

	return 0;
}

int CGit::GetWorkingTreeChanges(CTGitPathList& result, bool amend, const CTGitPathList* filterlist, bool includedStaged /* = false */, bool getStagingStatus /* = false */)
{
	if (IsInitRepos())
		return GetInitAddList(result, getStagingStatus);

	BYTE_VECTOR out;

	int count = 1;
	if (filterlist)
		count = filterlist->GetCount();
	ATLASSERT(count > 0);

	CString head = L"HEAD";
	if (amend)
		head = L"HEAD~1";

	for (int i = 0; i < count; ++i)
	{
		ATLASSERT(!filterlist || !(*filterlist)[i].GetGitPathString().empty()); // pathspec must not be empty, be compatible with Git >= 2.16.0
		BYTE_VECTOR cmdout;
		// The pathspec that used to be a pre-quoted " %s" appendix. Kept as a
		// vector rather than an optional string so appending it is the same
		// expression whether or not there is one.
		STRING_VECTOR pathspec;
		if (filterlist)
			pathspec.emplace_back((*filterlist)[i].GetGitPathString());

		if (ms_bCygwinGit || ms_bMsys2Git)
		{
			// Prevent showing all files as modified when using cygwin's git
			STRING_VECTOR status{ L"git.exe", L"status", L"--no-ahead-behind", L"--" };
			status.insert(status.cend(), pathspec.cbegin(), pathspec.cend());
			Run(status, &cmdout);
			cmdout.clear();
		}

		// also list staged files which will be in the commit
		STRING_VECTOR argv{ L"git.exe", L"diff-index", L"--cached", L"--raw", std::wstring(head), L"--numstat", L"-C", L"-M", L"-z", L"--" };
		if (!includedStaged && filterlist)
			argv.insert(argv.cend(), pathspec.cbegin(), pathspec.cend());
		Run(argv, &cmdout);

		argv = { L"git.exe", L"diff-index", L"--raw", std::wstring(head), L"--numstat", std::format(L"-C{}%", ms_iSimilarityIndexThreshold), std::format(L"-M{}%", ms_iSimilarityIndexThreshold), L"-z", L"--" };
		argv.insert(argv.cend(), pathspec.cbegin(), pathspec.cend());

		BYTE_VECTOR cmdErr;
		if (Run(argv, &cmdout, &cmdErr))
		{
			CString str{ cmdErr.Decode().c_str() };
			if (str.IsEmpty())
				str.Format(L"\"%s\" exited with an error code, but did not output any error message", SerializeArgvToCString(argv));
			MessageBox(nullptr, str, L"TortoiseGit", MB_OK | MB_ICONERROR);
		}

		out.append({cmdout.data(), cmdout.size()});
	}
	result.ParserFromLog(out);

	if (getStagingStatus)
	{
		// This will show staged files regardless of any filterlist, so that it has the same behavior that the commit window has when staging support is disabled
		BYTE_VECTOR cmdStagedUnfilteredOut, cmdUnstagedUnfilteredOut;
		Run({ L"git.exe", L"diff-index", L"--cached", L"--raw", std::wstring(head), L"--numstat", L"-C", L"-M", L"-z", L"--" }, &cmdStagedUnfilteredOut);

		CTGitPathList stagedUnfiltered;
		stagedUnfiltered.ParserFromLog(cmdStagedUnfilteredOut);

		Run({ L"git.exe", L"diff-files", L"--raw", L"--numstat", L"-C", L"-M", L"-z", L"--" }, &cmdUnstagedUnfilteredOut);

		CTGitPathList unstagedUnfiltered;
		unstagedUnfiltered.ParserFromLog(cmdUnstagedUnfilteredOut); // Necessary to detect partially staged files outside the filterlist

		// File shows up both in the output of diff-index --cached and diff-files: partially staged
		// File shows up only in the output of diff-index --cached: totally staged
		// File shows up only in the output of diff-files: totally unstaged
		// TODO: This is inefficient. It would be better if ParserFromLog received the output of each command
		// separately and did this processing there, dropping the new function UpdateStagingStatusFromPath entirely.
		for (int j = 0; j < stagedUnfiltered.GetCount(); ++j)
		{
			const std::wstring& path = stagedUnfiltered[j].GetGitPathString().c_str();
			if (unstagedUnfiltered.LookForGitPath(path))
				result.UpdateStagingStatusFromPath(path, CTGitPath::StagingStatus::PartiallyStaged);
			else
				result.UpdateStagingStatusFromPath(path, CTGitPath::StagingStatus::TotallyStaged);
		}
		for (int j = 0; j < unstagedUnfiltered.GetCount(); ++j)
		{
			const std::wstring& path = unstagedUnfiltered[j].GetGitPathString().c_str();
			if (!stagedUnfiltered.LookForGitPath(path))
				result.UpdateStagingStatusFromPath(path, CTGitPath::StagingStatus::TotallyUnstaged);
		}
		// make sure conflicted files show up as unstaged instead of partially staged
		for (int j = 0; j < result.GetCount(); ++j)
		{
			if (result[j].m_Action & CTGitPath::LOGACTIONS_UNMERGED)
				const_cast<CTGitPath&>(result[j]).m_stagingStatus = CTGitPath::StagingStatus::TotallyUnstaged;
		}
	}

	std::map<std::wstring, int> duplicateMap;
	for (int i = 0; i < result.GetCount(); ++i)
		duplicateMap.insert(std::pair<std::wstring, int>(result[i].GetGitPathString().c_str(), i));

	// handle delete conflict case, when remote : modified, local : deleted.
	for (int i = 0; i < count; ++i)
	{
		BYTE_VECTOR cmdout;

		STRING_VECTOR argv{ L"git.exe", L"ls-files", L"-u", L"-t", L"-z" };
		if (filterlist)
			argv.insert(argv.cend(), { L"--", (*filterlist)[i].GetGitPathString() }); // already checked above

		Run(argv, &cmdout);

		CTGitPathList conflictlist;
		conflictlist.ParserFromLsFile(cmdout);
		for (int j = 0; j < conflictlist.GetCount(); ++j)
		{
			auto existing = duplicateMap.find(conflictlist[j].GetGitPathString().c_str());
			if (existing != duplicateMap.end())
			{
				CTGitPath& p = const_cast<CTGitPath&>(result[existing->second]);
				p.m_Action |= CTGitPath::LOGACTIONS_UNMERGED;
			}
			else
			{
				// should we ever get here?
				ATLASSERT(false);
				result.AddPath(conflictlist[j]);
				duplicateMap.insert(std::pair<std::wstring, int>(result[i].GetGitPathString().c_str(), result.GetCount() - 1));
			}
		}
	}

	static bool useOldLSFilesDBehaviorKS = Registry::CRegDWORD(L"Software\\TortoiseGit\\OldLSFilesDBehaviorKS", FALSE, false, HKEY_LOCAL_MACHINE) == TRUE; // TODO: remove kill-switch
	// handle source files of file renames/moves (issue #860)
	// if a file gets renamed and the new file "git add"ed, diff-index doesn't list the source file anymore
	for (int i = 0; i < count; ++i)
	{
		BYTE_VECTOR cmdout;

		STRING_VECTOR argv;
		if (!useOldLSFilesDBehaviorKS)
			argv = { L"git.exe", L"diff", L"--name-only", L"--diff-filter=D", L"-z" };
		else
			argv = { L"git.exe", L"ls-files", L"-d", L"-z" };
		if (filterlist)
			argv.insert(argv.cend(), { L"--", (*filterlist)[i].GetGitPathString() });

		Run(argv, &cmdout);

		CTGitPathList deletelist;
		deletelist.ParserFromLsFileSimple(cmdout, CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING);
		for (int j = 0; j < deletelist.GetCount(); ++j)
		{
			auto existing = duplicateMap.find(deletelist[j].GetGitPathString().c_str());
			if (existing == duplicateMap.end())
			{
				result.AddPath(deletelist[j]);
				duplicateMap.insert(std::pair<std::wstring, int>(result[i].GetGitPathString().c_str(), result.GetCount() - 1));
			}
			else
			{
				CTGitPath& p = const_cast<CTGitPath&>(result[existing->second]);
				p.m_Action |= CTGitPath::LOGACTIONS_MISSING;
				result.m_Action |= CTGitPath::LOGACTIONS_MISSING;
			}
		}
	}

	return 0;
}

void CGit::GetBisectTerms(CString* good, CString* bad)
{
	static CString lastGood;
	static CString lastBad;
	static ULONGLONG lastRead = 0;

	const auto publish = gsl::finally([good, bad] {
		if (bad)
			*bad = lastBad;
		if (good)
			*good = lastGood;
	});

#ifndef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
	// add some caching here, because this method might be called multiple times in a short time from LogDlg and RevisionGraph
	// as we only read a small file the performance effects should be negligible
	if (lastRead + 5000 > GetTickCount64())
		return;
#endif

	lastGood = L"good";
	lastBad = L"bad";

	const auto adminDir = GitAdminDir::GetAdminDirPath(StringView(m_CurrentDir));
	if (!adminDir)
		return;

	const std::wstring termsFile = adminDir->path + L"BISECT_TERMS";
	// _wfsopen, like the other readers here; _wfopen_s would need a FILE** out of CAutoFILE.
	const wil::CAutoFILE fp{_wfsopen(termsFile.c_str(), L"rb", SH_DENYWR)};
	if (!fp)
		return;
	char badA[MAX_PATH] = {};
	fgets(badA, sizeof(badA), fp.get());
	size_t len = strlen(badA);
	if (len > 0 && badA[len - 1] == '\n')
		badA[len - 1] = '\0';
	char goodA[MAX_PATH] = {};
	fgets(goodA, sizeof(goodA), fp.get());
	len = strlen(goodA);
	if (len > 0 && goodA[len - 1] == '\n')
		goodA[len - 1] = '\0';
	lastGood = CUnicodeUtils::Convert2Unicode<CString>(goodA);
	lastBad = CUnicodeUtils::Convert2Unicode<CString>(badA);
	lastRead = GetTickCount64();
}

int CGit::GetGitVersion(CString* versiondebug, CString* errStr)
{
	CString version, err;
	if (Run({ L"git.exe", L"--version" }, &version, &err, CP_UTF8))
	{
		if (errStr)
			*errStr = err;
		return -1;
	}

	int ver = 0;
	if (versiondebug)
		*versiondebug = version;

	try
	{
		int start = 0;
		CString str = version.Tokenize(L".", start);
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
	}
	catch (...)
	{
		if (!ver)
			return -1;
	}

	return ver;
}

int CGit::GetGitNotes(const CGitHash& hash, CString& notes)
{
	const CAutoRepository repo(GetGitRepository());
	if (!repo)
		return -1;

	CAutoNote note;
	const int ret = git_note_read(&note, repo, nullptr, hash);
	if (ret == GIT_ENOTFOUND)
	{
		notes.Empty();
		return 0;
	}
	else if (ret)
		return -1;
	notes = CUnicodeUtils::Convert2Unicode<CString>(git_note_message(note));
	return 0;
}

int CGit::SetGitNotes(const CGitHash& hash, const CString& notes)
{
	const CAutoRepository repo(GetGitRepository());
	if (!repo)
		return -1;

	CAutoSignature signature;
	if (git_signature_default(&signature, repo) < 0)
		return -1;

	git_oid oid;
	if (git_note_create(&oid, repo, nullptr, signature, signature, hash, CUnicodeUtils::GetUTF8<CStringA>(notes), 1) < 0)
		return -1;

	return 0;
}

int CGit::GetTagInfo(const CString& tagName, CString& output, const std::function<CString(const __time64_t)>& timeformatter)
{
	if (m_IsUseLibGit2)
	{
		const CAutoRepository repo{ g_Git.GetGitRepository() };
		if (!repo)
			return -1;
		CAutoReference ref;
		if (git_reference_lookup(&ref, repo, CUnicodeUtils::GetUTF8<CStringA>(tagName)) < 0)
		{
			output = TGitH::GetLibGit2LastErr();
			return -1;
		}
		CAutoObject obj;
		if (git_object_lookup(&obj, repo, git_reference_target(ref), GIT_OBJECT_TAG))
		{
			output = TGitH::GetLibGit2LastErr();
			return -1;
		}
		CAutoTag tag;
		if (git_tag_lookup(&tag, repo, git_reference_target(ref)) < 0)
		{
			output = TGitH::GetLibGit2LastErr();
			return -1;
		}
		output = L"tag " + CUnicodeUtils::Convert2Unicode<CString>(git_tag_name(tag)) + L"\n";
		const auto tagger = git_tag_tagger(tag);
		if (tagger)
		{
			output += "tagger ";
			if (tagger->name)
			{
				output += CUnicodeUtils::Convert2Unicode<CString>(tagger->name);
				output += L' ';
			}
			if (tagger->email)
			{
				output += L'<';
				output += CUnicodeUtils::Convert2Unicode<CString>(tagger->email);
				output += L'>';
				output += L' ';
			}
			output += timeformatter(tagger->when.time);
			output += L'\n';
		}
		output += L'\n';
		output += CUnicodeUtils::Convert2Unicode<CString>(git_tag_message(tag));
		output.TrimRight().AppendChar(L'\n');
		return 0;
	}

	output.Empty();
	if (g_Git.Run({ L"git.exe", L"cat-file", L"tag", L"--", std::wstring(tagName) }, &output, CP_UTF8) != 0)
		return -1;

	if (StringUtils::StartsWith(output, L"object "))
	{
		if (const int pos = output.Find(L"\n"); pos > 0)
		{
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

	CString str = output.Mid(pos1, pos2 - pos1);
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

int GetSubmodulePointer(CGit::SubmoduleInfo& submoduleinfo)
{
	submoduleinfo.Empty();

	if (Registry::CRegDWORD(L"Software\\TortoiseGit\\LogShowSuperProjectSubmodulePointer", TRUE) != TRUE)
		return 0;

	if (GitAdminDir::IsBareRepo(g_Git.m_CurrentDir.GetString()))
		return -1;
	std::wstring superprojectRoot;
	GitAdminDir::HasAdminDir(StringView(g_Git.m_CurrentDir), false, &superprojectRoot);
	if (superprojectRoot.empty())
		return -1;

	const CAutoRepository repo(std::wstring_view{ superprojectRoot });
	if (!repo)
		return -1;
	CAutoIndex index;
	if (git_repository_index(index.GetPointer(), repo))
		return -1;

	const int rootLength = CUnicodeUtils::SafeSizeToInt(superprojectRoot.size());
	CString submodulePath;
	if (superprojectRoot.back() == L'\\')
		submodulePath = g_Git.m_CurrentDir.Right(g_Git.m_CurrentDir.GetLength() - rootLength);
	else
		submodulePath = g_Git.m_CurrentDir.Right(g_Git.m_CurrentDir.GetLength() - rootLength - 1);
	submodulePath.Replace(L'\\', L'/');

	// if current submodule is in conflict state, return the relevant hashes
	const git_index_entry* ancestor{};
	const git_index_entry* our{};
	const git_index_entry* their{};
	if (!git_index_conflict_get(&ancestor, &our, &their, index, CUnicodeUtils::GetUTF8<CStringA>(submodulePath)))
	{
		if (our)
			submoduleinfo.mergeconflictMineHash = our->id;
		if (their)
			submoduleinfo.mergeconflictTheirsHash = their->id;

		const CTGitPath superProject{superprojectRoot};
		if (superProject.IsRebaseActive())
		{
			submoduleinfo.mineLabel = L"super-project-rebase-head";
			submoduleinfo.theirsLabel = L"super-project-head";
		}
		else
		{
			submoduleinfo.mineLabel = L"super-project-head";
			submoduleinfo.theirsLabel = L"super-project-merge-head";
		}
		return 0;
	}

	// determine hash of submodule
	const git_index_entry* entry = git_index_get_bypath(index, CUnicodeUtils::GetUTF8<CStringA>(submodulePath), 0);
	if (!entry)
		return -1;

	submoduleinfo.superProjectHash = entry->id;
	return 0;
}





} // namespace TGitH

export using namespace TGitH;
export using TGitH::g_Git;