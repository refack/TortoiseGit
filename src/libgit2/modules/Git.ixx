module;
#include <strsafe.h>
#include <atlstr.h>
#include <atlbase.h>
#include <gitdll.h>
#include <git2.h>

export module git_h;
import std;
import gsl;
import wil;
import libgit2;
import invarients;
import SmartLibgit2;
import GitAdminDir;
import PathUtils;
import StringUtils;
import wstr;
import TGitPath;
import DebugOutput;
import tgittypes;
import GitHash;

using std::operator""s;
using std::operator""sv;
using TGString = std::wstring;
using TGStringView = std::wstring_view;
using std::filesystem::path;
namespace fs = std::filesystem;

using namespace wil;
using namespace TGitPath;
using namespace tgit::types;
using CGitHash = TGitHash::CGitHash;

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


constexpr size_t CalculateDiffSimilarityIndexThreshold() noexcept
{
	const size_t index = wil::reg::get_value_typed<DWORD>(DiffSimilarityIndexThreshold);
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
		const auto lastError = CUnicodeUtils::GetUnicode<CString>(libgit2err->message);
		tgit::libgit2::git_error_clear();
		return L"libgit2 returned: " + lastError;
	}
	else
		return L"An error occoured in libgit2, but no message is available.";
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
	std::vector<std::string> m_strings;
	std::vector<ArgvP> m_argv;
};


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

	MAP_ENVIRONMENT m_vars;
	/// The serialized "name=value\0...\0\0" block, rebuilt on every mutation.
	std::vector<wchar_t> m_block;
};


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
	bool m_IsUseGitDLL;
	bool m_IsUseLibGit2;
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
	BOOL FindAndSetGitExePath(BOOL bFallback);
	bool m_bInitialized = false;

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
	static_assert(LIBGIT2_CMD::LAST_VALUE < sizeof(DWORD) * 8, "too many flags for storing them in a DWORD bitfield");
	[[nodiscard]] bool UsingLibGit2(LIBGIT2_CMD cmd) const;
	/**
	 * callback type should be git_cred_acquire_cb
	 */
	static void SetGit2CredentialCallback(void* callback);
	static void SetGit2CertificateCheckCertificate(void* callback);

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
	static std::wstring GetConfiguredSshClient();
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
		bool b = GitAdminDir::HasAdminDir(tgit::wstr::StringView(path), submodule ? false : !!PathIsDirectory(path), &topDir);
		m_CurrentDirStorage = topDir.c_str();
		if (!b && GitAdminDir::IsBareRepo(tgit::wstr::StringView(path)))
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
	*/
	BOOL IsInitRepos();
	/** Returns 0 if no conflict, if a conflict was found and -1 in case of a failure */
	int HasWorkingTreeConflicts();
	/** Returns 0 if no conflict, if a conflict was found and -1 in case of a failure */
	int HasWorkingTreeConflicts(git_repository* repo);
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
	int GetSubmodulePointer(SubmoduleInfo& mergeInfo) const;

	int ApplyPatchToIndex(const CString& patchPath, CString* out);
	int ApplyPatchToIndexReverse(const CString& patchPath, CString* out);

	int RefreshGitIndex();
	int GetOneFile(const CString& Refname, const CTGitPath& path, const CString& outputfile);

	// Example: master -> refs/heads/master
	CString GetFullRefName(const CString& shortRefName);
	// Removes 'refs/heads/' or just 'refs'. Example: refs/heads/master -> master

	int GetCommitDiffList(const CString& rev1, const CString& rev2, CTGitPathList& outpathlist, CString& error, bool ignoreSpaceAtEol = false, bool ignoreSpaceChange = false, bool ignoreAllSpace = false, bool ignoreBlankLines = false);
	int GetInitAddList(CTGitPathList& outpathlist, bool getStagingStatus = false);
	int GetWorkingTreeChanges(CTGitPathList& result, bool amend = false, const CTGitPathList* filterlist = nullptr, bool includedStaged = false, bool getStagingStatus = false);
	[[nodiscard]] int GetShortHASHLength() const;

	int GetGitNotes(const CGitHash& hash, CString& notes);
	int SetGitNotes(const CGitHash& hash, const CString& notes);

	int GetTagInfo(const CString& tagName, CString& info, const std::function<CString(__time64_t)>& timeformatter);

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
	template <tgit::wstr::IsRealString T>
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
	[[nodiscard]] static CString SerializeArgvToCString(const STRING_VECTOR& argv)
	{
		const std::wstring cmd = SerializeArgv(argv);
		return CString(cmd.data(), gsl::narrow<int>(cmd.size()));
	}
};

git_credential_acquire_cb g_Git2CredCallback;
git_transport_certificate_check_cb g_Git2CheckCertificateCallback;

// Only the commands that still fork at runtime appear here; the ones in
// LIBGIT2_ONLY_MASK are on whatever the registry says. GETONEFILE, GET_COMMIT
// and FOREACHREF are what is left of the old default list - the other five
// entries moved into LIBGIT2_ONLY_MASK, which is not a change of behaviour for
// anyone since they were on by default already.
constexpr BITS32 DEFAULT_USE_LIBGIT2_MASK = (1 << CGit::GIT_CMD_GETONEFILE |
											 1 << CGit::GIT_CMD_GET_COMMIT |
											 1 << CGit::GIT_CMD_FOREACHREF);

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
constexpr BITS32 LIBGIT2_ONLY_MASK = (1 << CGit::GIT_CMD_DIFF |
									  1 << CGit::GIT_CMD_REVERT |
									  1 << CGit::GIT_CMD_MERGE_BASE |
									  1 << CGit::GIT_CMD_DELETETAGBRANCH |
									  1 << CGit::GIT_CMD_ADD |
									  1 << CGit::GIT_CMD_PUSH |
									  1 << CGit::GIT_CMD_CHECK_CLEAN_WT |
									  1 << CGit::GIT_CMD_CHECKCONFLICTS |
									  1 << CGit::GIT_CMD_BRANCH_CONTAINS |
									  1 << CGit::GIT_CMD_GETCONFLICTINFO);



CString StripRefName(CString refName);
void GetTempPath(CString& path);
CString GetTempFile();
DWORD GetTortoiseGitTempPath(DWORD nBufferLength, LPWSTR lpBuffer);


int ParseConflictHashesFromLsFile(const BYTE_VECTOR& out, CGitHash& baseHash, bool& baseIsFile, CGitHash& mineHash, bool& mineIsFile, CGitHash& remoteHash, bool& remoteIsFile);


constexpr std::int64_t filetime_to_time_t(std::int64_t winTime) noexcept
{
	winTime -= 116444736000000000LL; /* Windows to Unix Epoch conversion */
	winTime /= 10000000; /* Nano to seconds resolution */
	return static_cast<time_t>(winTime);
}


int GetFileModifyTime(LPCWSTR filename, std::int64_t* time, bool* isDir = nullptr, std::int64_t* size = nullptr, bool* isSymlink = nullptr)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata{};
	if (GetFileAttributesEx(filename, GetFileExInfoStandard, &fdata))
	{
		if (time)
			*time = static_cast<std::int64_t>(fdata.ftLastWriteTime.dwHighDateTime) << 32 | fdata.ftLastWriteTime.dwLowDateTime;

		if (size)
			*size = static_cast<std::int64_t>(fdata.nFileSizeHigh) << 32 | fdata.nFileSizeLow;

		if (isDir)
			*isDir = !!(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);

		if (isSymlink)
			*isSymlink = (fdata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && !PathUtils::ReadLink(filename);

		return 0;
	}
	return -1;
}
int GetFileModifyTime(const path& filename, std::int64_t* time, bool* isDir = nullptr, std::int64_t* size = nullptr, bool* isSymlink = nullptr)
{
	return GetFileModifyTime(filename.c_str(), time, isDir, size, isSymlink);
}

TGString GetShortName(const TGStringView ref, TGStringView prefix)
{
	constexpr auto weird_tail = L"^{}"sv;
	if (ref.starts_with(prefix))
	{
		auto shortname = ref.substr(prefix.length());
		if (shortname.ends_with(weird_tail))
			shortname.remove_suffix(weird_tail.length());
		return TGString{ shortname };
	}
	return {};
}


BOOL GetShortName(const CString& ref, CString& shortname, const CString& prefix)
{
	if (const auto str = GetShortName(tgit::wstr::StringView{ ref }, tgit::wstr::StringView{ prefix }); !str.empty())
	{
		shortname = str.c_str();
		return TRUE;
	}
	return FALSE;
}


CString GetShortName(const CString& ref, REF_TYPE* type);


bool LoadTextFile(const CString& filename, CString& msg);


CGit g_Git;


} // namespace TGitH

export using namespace TGitH;
export using TGitH::g_Git;