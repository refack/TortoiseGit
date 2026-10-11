module;
#include <sanity.h>


export module tgittypes;

import std;
using namespace std::literals;
using DWORD = std::uint32_t;
using LPWSTR = wchar_t*;
using std::byte;
using string_terminator_t = std::integral_constant<std::byte, std::byte{0}>;
constexpr string_terminator_t NUL{};


import wstr;
import gsl;
import invarients;
import GitHash;



export namespace tgit::types {
template <zstr::IsTCompString Str>
constexpr size_t HasEnoughSpace(const Str& sv) noexcept {
	return zstr::StringView<Str>{ sv }.size() == 2 * tgit::invarients::GitFormatHolder::GitHashSize();
}



enum CacheType {
	none,
	exe,
	dll,
	dllFull, // same as dll except it uses commandline git tool with all status modes supported
};



enum: std::uint8_t {
	TGIT_GIT_SUCCESS = 0,
	TGIT_GIT_ERROR_OPEN_PIP,
	TGIT_GIT_ERROR_CREATE_PROCESS,
	TGIT_GIT_ERROR_GET_EXIT_CODE
};



struct CGitByteArray : std::vector<byte> {
	using value_type = byte;
	using super_type = std::vector<byte>;

	[[nodiscard]] size_t find(const byte data, const size_t start = 0) const {
		const size_t end = size();
		for (size_t i = start; i < end; ++i)
			if ((*this)[i] == data)
				return i;
		return npos;
	}


	[[nodiscard]] size_t RevertFind(const byte data, size_t start = npos) const {
		if (start == npos) {
			if (empty())
				return npos;
			start = size() - 1;
		}

		for (size_t i = start + 1; i-- > 0;)
			if ((*this)[i] == data)
				return i;
		return npos;
	}


	[[nodiscard]] size_t findNextString(const size_t start = 0) const {
		size_t pos = start;
		const size_t end = size();
		do {
			pos = find(NUL, pos);
			if (pos == npos)
				break;
			++pos;
			if (pos >= end)
				return npos;
		}
		while ((*this)[pos] == NUL);

		return pos;
	}


	// was gtest-only; a module cannot vary its API by importer
	void append(const char* data, size_t dataSize) {
		append(std::string_view(data, dataSize));
	}


	void append(const std::string_view v) {
		insert(this->end(), v.begin(), v.end());
	}


	void append(const std::span<unsigned char> v) {
		insert(this->end(), v.begin(), v.end());
	}


	/// Decodes the buffer as UTF-8 text, stopping at the first NUL.
	///
	/// This was an implicit `operator CString()`, which meant bytes became text
	/// wherever a BYTE_VECTOR happened to land in a CString context. Two things
	/// were invisible at such a call site and both are load-bearing: the buffer
	/// is *assumed* to be UTF-8, and everything from the first NUL on is
	/// discarded - which matters because git's own output is frequently
	/// NUL-separated (`-z`, `ls-files --stage -z`, `for-each-ref` with a NUL
	/// terminator), so an accidental decode of such a buffer silently yields
	/// only its first record. Naming it makes the assumption and the truncation
	/// visible; the behaviour is exactly what the operator did.
	///
	/// For a whole, NUL-containing buffer use the bytes directly - `data()` and
	/// `size()` - and decode each record separately.
	[[nodiscard]] std::wstring Decode() const {
		if (empty())
			return {};
		const auto max_items = std::ranges::find(*this, NUL) - this->begin();
		const auto char_view = std::string_view{ reinterpret_cast<std::string_view::const_pointer>(data()), gsl::narrow<size_t>(max_items) };
		return CUnicodeUtils::GetUnicode<std::wstring>(char_view);
	}


	static constexpr size_t npos = static_cast<size_t>(-1); // bad/missing length/position
	static_assert(-1 == static_cast<int>(npos), "NPOS must equal -1");

	using std::vector<char>::begin;
	using std::vector<char>::end;
	using std::vector<char>::cbegin;
	using std::vector<char>::cend;
	using std::vector<char>::clear;
	using std::vector<char>::data;
	using std::vector<char>::empty;
	using std::vector<char>::erase;
	using std::vector<char>::pop_back;
	using std::vector<char>::push_back;
	using std::vector<char>::size;
	using std::vector<char>::operator[];
};



struct TGitRef {
	std::wstring name;
	CGitHash hash;
	// There was an `operator const CString&() const { return name; }` here. Same
	// disease as CGitByteArray's operator CString(): it let a whole ref stand in
	// for its name silently, so a call site could not be read without knowing the
	// overload set around it. Say `.name`.
};



// std::wstring rather than CString, for the reason WideString.h opens with:
// CString is a different type in an MFC project than in an ATL one, so an alias
// naming it cannot appear in a header both flavors compile. Values, not views -
// these containers own their strings and outlive whatever produced them.
using STRING_VECTOR = std::vector<std::wstring>;
// A list of *commands*, each one an argv vector. Named because the distinction
// was previously invisible: CProgressDlg's list of whole command lines was also
// spelled STRING_VECTOR, one alias away from a single command's arguments.
using ARGV_VECTOR = std::vector<STRING_VECTOR>;
using MAP_HASH_NAME = std::unordered_map<CGitHash, STRING_VECTOR>;
using MAP_STRING_STRING = std::map<std::wstring, std::wstring>;
using REF_VECTOR = std::vector<TGitRef>;
using BYTE_VECTOR = CGitByteArray;



template <typename D>
struct scope_exit_t {
	using func_type = D;


	explicit scope_exit_t(D&& f) :
		func(f) {}


	scope_exit_t(scope_exit_t&& s) noexcept :
		func(s.func) {}


	~scope_exit_t() { func(); }

	// Prohibit construction from lvalues.
	scope_exit_t(D&) = delete;

	// Prohibit copying.
	scope_exit_t(const scope_exit_t&) = delete;
	scope_exit_t& operator=(const scope_exit_t&) = delete;

	// Prohibit new/delete.
	void* operator new(size_t) = delete;
	void* operator new[](size_t) = delete;
	void operator delete(void*) = delete;
	void operator delete[](void*) = delete;

private:
	const func_type func;
};



struct scope_exit_helper {
	template <typename D>
	scope_exit_t<D> operator<<(D&& f) {
		return scope_exit_t<D>(std::forward<D>(f));
	}
};



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



enum class TGitContextMenuEntries : unsigned __int64 {
	None = 0x0000000000000000,
	Sync = 0x0000000000000002,
	Commit = 0x0000000000000004,
	Add = 0x0000000000000008,
	Revert = 0x0000000000000010,
	Cleanup = 0x0000000000000020,
	Resolve = 0x0000000000000040,
	Switch = 0x0000000000000080,
	Sendmail = 0x0000000000000100,
	Export = 0x0000000000000200,
	CreateRepo = 0x0000000000000400,
	Branch = 0x0000000000000800,
	Merge = 0x0000000000001000,
	Remove = 0x0000000000002000,
	Rename = 0x0000000000004000,
	SubmoduleUpdate = 0x0000000000008000,
	Diff = 0x0000000000010000,
	Log = 0x0000000000020000,
	Conflicteditor = 0x0000000000040000,
	RefBrowser = 0x0000000000080000,
	ShowChanged = 0x0000000000100000,
	Ignore = 0x0000000000200000,
	RefLog = 0x0000000000400000,
	Blame = 0x0000000000800000,
	RepoBrowser = 0x0000000001000000,
	ApplyPatch = 0x0000000002000000,
	// retired with TortoiseGitMerge; the bit stays reserved so existing user menu masks keep their meaning
	RemoveKeep = 0x0000000004000000,
	SVNRebase = 0x0000000008000000,
	SVNDCommit = 0x0000000010000000,
	SVNIgnore = 0x0000000040000000,
	LogSubmodule = 0x0000000100000000,
	PrevDiff = 0x0000000200000000,
	ClipPaste = 0x0000000400000000,
	Pull = 0x0000000800000000,
	Push = 0x0000001000000000,
	Clone = 0x0000002000000000,
	Tag = 0x0000004000000000,
	FormatPatch = 0x0000008000000000,
	ImportPatch = 0x0000010000000000,
	DiffLater = 0x0000020000000000,
	Fetch = 0x0000040000000000,
	Rebase = 0x0000080000000000,
	StashSave = 0x0000100000000000,
	StashApply = 0x0000200000000000,
	StashList = 0x0000400000000000,
	SubmoduleAdd = 0x0000800000000000,
	SubmoduleSync = 0x0001000000000000,
	StashPop = 0x0002000000000000,
	DiffTwo = 0x0004000000000000,
	Bisect = 0x0008000000000000,
	Inaccessible = 0x0010000000000000,
	SVNFetch = 0x0080000000000000,
	RevisionGraph = 0x0100000000000000,
	Daemon = 0x0200000000000000,
	Worktree = 0x0400000000000000,
	LFS = 0x1000000000000000,
	Settings = 0x2000000000000000,
	Help = 0x4000000000000000,
	About = 0x8000000000000000,
};



constexpr TGitContextMenuEntries operator|(TGitContextMenuEntries a, TGitContextMenuEntries b) noexcept {
	return static_cast<TGitContextMenuEntries>(static_cast<std::underlying_type_t<TGitContextMenuEntries>>(a) |
		static_cast<std::underlying_type_t<TGitContextMenuEntries>>(b));
}


constexpr TGitContextMenuEntries operator&(TGitContextMenuEntries a, TGitContextMenuEntries b) noexcept {
	return static_cast<TGitContextMenuEntries>(static_cast<std::underlying_type_t<TGitContextMenuEntries>>(a) &
		static_cast<std::underlying_type_t<TGitContextMenuEntries>>(b));
}


constexpr TGitContextMenuEntries& operator|=(TGitContextMenuEntries& self, const TGitContextMenuEntries a) noexcept {
	self = self | a;
	return self;
}


template <typename E>
constexpr E from_underlying(std::underlying_type_t<E> value) noexcept {
	static_assert(std::is_enum_v<E>);
	return static_cast<E>(value);
}


constexpr TGitContextMenuEntries to_TGitContextMenuEntries(const std::uint32_t high, const std::uint32_t low) noexcept {
	static_assert(sizeof(std::underlying_type_t<TGitContextMenuEntries>) == 2 * sizeof(std::uint32_t));
	const auto tmp = (static_cast<std::uint64_t>(high) << 32) | low;
	return from_underlying<TGitContextMenuEntries>(tmp);
}


/**
 * \ingroup TortoiseShell
 * Since we need an own COM-object for every different
 * Icon-Overlay implemented this enum defines which class
 * is used.
 */
enum FileState {
	FileStateUncontrolled,
	FileStateVersioned,
	FileStateModified,
	FileStateConflict,
	FileStateDeleted,
	FileStateReadOnly,
	FileStateLockedOverlay,
	FileStateAddedOverlay,
	FileStateIgnoredOverlay,
	FileStateUnversionedOverlay,
	FileStateDropHandler,
	FileStateInvalid
};



/// git's version as one comparable int, 8 bits per field. It returned std::byte,
/// where `major << 24` shifts every bit out - so each version compared by its
/// build number alone. Exported: the unit tests gate on it.
consteval int ConvertVersionToInt(const int major, const int minor, const int patchlevel, const int build = 0) {
	return (major << 24) | (minor << 16) | (patchlevel << 8) | build;
}


using BITS32 = std::uint32_t;


constexpr auto REG_MSYSGIT_PATH = L"MSysGit"sv;
constexpr auto REG_SYSTEM_GITCONFIGPATH = L"SystemConfig"sv;
constexpr auto REG_MSYSGIT_EXTRA_PATH = L"MSysGitExtra"sv;

constexpr auto G_OK = 0;
constexpr auto GFail = -1;
constexpr auto GIT_EXE_BUFFER_LIMIT = 50uz * 1024 * 1024;
// arbitrary chosen 50 MiB limit that should be large enough for all real life situations in remote situations
constexpr auto GIT_EXE_STDERR_BUFFER_LIMIT = 1024uz;
// arbitrary chosen 1 KiB limit that should be large enough for all real life situations in remote situations

constexpr auto DiffSimilarityIndexThreshold = L"DiffSimilarityIndexThreshold"sv;
constexpr auto CygwinHack = L"CygwinHack"sv;
constexpr auto Msys2Hack = L"Msys2Hack"sv;

#define REG_SOFTWARE_ROOT          L"SOFTWARE"
#define REG_SOFTWARE32_ROOT        REG_SOFTWARE_ROOT LR"(\Wow6432Node)"
#define REG_UNINSTALL_TGIT_SUBKEY  LR"(\Microsoft\Windows\CurrentVersion\Uninstall\Git_is1)"
constexpr auto UNINSTALL_REG_VALUE = L"InstallLocation";
constexpr auto UNINSTALL_REG_SUBKEY = std::wstring_view{ REG_SOFTWARE_ROOT REG_UNINSTALL_TGIT_SUBKEY };
constexpr auto UNINSTALL_RGSK_WOW64 = std::wstring_view{ REG_SOFTWARE32_ROOT REG_UNINSTALL_TGIT_SUBKEY };


constexpr auto CALL_OUTPUT_READ_CHUNK_SIZE = 1024u;



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



struct ArgvData {
	ArgvData() = default;
	ArgvData(ArgvData&&) noexcept = default;
	ArgvData(const ArgvData&) = default;
	ArgvData& operator=(const ArgvData&) = default;
	ArgvData& operator=(ArgvData&&) noexcept = default;

	/// Callers pass argv[0] themselves (usually ""), as they always have.
	/// Empty when the count would not fit an int argc.
	ArgvData(const std::initializer_list<std::string> args) :
		_args{ args } {
		fill_argv();
	}


	ArgvData(const std::initializer_list<const char*> args) {
		for (auto s : args) {
			_args.emplace_back(s);
		}
		fill_argv();
	}


	explicit operator bool() const noexcept { return !_args.empty(); }
	[[nodiscard]] int argc() const noexcept { return gsl::narrow<int>(_args); }
	[[nodiscard]] auto argv() noexcept { return _argv.data(); }

private:
	void fill_argv() {
		this->_argv.reserve(_args.size() + 1);
		for (auto& s : _args) {
			_argv.push_back(s.data());
		}
		_argv.push_back(nullptr); // Null-terminated array
	}


	std::vector<std::string> _args{};
	std::vector<const char*> _argv{};
};



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



class CGitCall {
	std::wstring m_Cmd{};

public:
	CGitCall() = default;


	explicit CGitCall(const std::wstring& cmd) :
		m_Cmd(cmd) {}


	explicit CGitCall(const STRING_VECTOR& argv);
	// Out of line because it needs CGit::SerializeArgv, and CGit is declared below.
	virtual ~CGitCall() = default;

	[[nodiscard]] const std::wstring& GetCmd() const { return m_Cmd; }

	// These functions are called when command output data is available.
	// When this function returns 'true' further output is ignored.
	virtual bool OnOutputData(std::string_view data) = 0;
	virtual bool OnOutputErrData(std::string_view data) = 0;
	virtual void OnEnd() {}


	void setLimit(const bool enableLimit) {
		m_maxLength = enableLimit ? GIT_EXE_BUFFER_LIMIT : std::numeric_limits<size_t>::max();
	}


	[[nodiscard]] virtual bool wasLimited() const = 0;

protected:
	// Unlimited unless setLimit(true): only remote-facing calls opt in.
	size_t m_maxLength = std::numeric_limits<size_t>::max();
};



class CGitCall_ByteVector : public CGitCall {
public:
	CGitCall_ByteVector(const std::wstring& cmd, BYTE_VECTOR* pvector, BYTE_VECTOR* pvectorErr = nullptr) :
		CGitCall(cmd)
		, m_pvector(pvector)
		, m_pvectorErr(pvectorErr) {}


	bool OnOutputData(const std::string_view data) override {
		Expects(data.size() <= 1024); // data.size() cannot be larger than 1024, as RunAsync uses a buffer of that size
		if (!m_pvector)
			return true;
		if (m_pvector->size() >= m_maxLength)
			// this simple check is fine as it is intended to be a rough length limit for remote connections as a safety measure
			return true;
		m_pvector->append(data);
		return false;
	}


	bool OnOutputErrData(const std::string_view data) override {
		Expects(data.size() <= 1024); // data.size() cannot be larger than 1024, as RunAsync uses a buffer of that size
		if (!m_pvectorErr)
			return true;
		if (m_maxLength != std::numeric_limits<size_t>::max() && m_pvectorErr->size() > GIT_EXE_STDERR_BUFFER_LIMIT)
			// arbitrary length limit for remote connections as a safety measure
			return true;
		m_pvectorErr->append(data);
		return false;
	}


	BYTE_VECTOR* m_pvector{};
	BYTE_VECTOR* m_pvectorErr{};


	bool wasLimited() const override {
		if (!m_pvector)
			return false;
		return m_pvector->size() >= m_maxLength;
	}
};



template <typename GitReceiverFunc>
	requires std::is_convertible_v<GitReceiverFunc, std::function<void(std::string_view)>>
class CGitCallCb : public CGitCall {
	size_t m_received = 0;
	GitReceiverFunc m_recv;
	BYTE_VECTOR m_buffer{};
	BYTE_VECTOR* m_pvectorErr{};

public:
	using CallbackType = GitReceiverFunc;


	CGitCallCb(const std::wstring& cmd, const GitReceiverFunc recv, BYTE_VECTOR* p_VectorErr = nullptr) :
		CGitCall(cmd)
		, m_recv(recv)
		, m_pvectorErr(p_VectorErr) {}


	bool OnOutputData(const std::string_view data) override {
		Expects(data.size() <= 1024); // data.size() cannot be larger than 1024, as RunAsync uses a buffer of that size
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
		for (std::string line; std::getline(reader, line);) {
			if (line.empty())
				continue;
			m_recv(line);
		}
		m_buffer.clear();
		return false;
	}


	bool OnOutputErrData(const std::string_view data) override {
		Expects(data.size() <= 1024); // data.size() cannot be larger than 1024, as RunAsync uses a buffer of that size
		if (!m_pvectorErr)
			return true;
		if (m_maxLength != std::numeric_limits<size_t>::max() && m_pvectorErr->size() > GIT_EXE_STDERR_BUFFER_LIMIT)
			// arbitrary length limit for remote connections as a safety measure
			return true;
		m_pvectorErr->append(data);
		return false;
	}


	void OnEnd() override {
		// do not send any incomplete lines
		m_buffer.clear(); // Just for sure
	}


	[[nodiscard]] bool wasLimited() const override {
		return m_received >= m_maxLength;
	}
};



/// Ordering for environment variable names. Windows resolves them
/// case-insensitively - PATH and Path are one variable - so that has to be a
/// property of the container rather than something every lookup remembers to do.
/// It also gives the serialized block the case-insensitive name order Windows'
/// own environment blocks come in, which the flat block lost the moment anything
/// was added to it.
struct EnvNameLess {
	using is_transparent = void;


	[[nodiscard]] bool operator()(const std::wstring_view lhs, const std::wstring_view rhs) const noexcept {
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
class CEnvironment {
public:
	CEnvironment() = default;


	CEnvironment(const CEnvironment& env) :
		m_vars(env.m_vars) {
		Serialize();
	}


	CEnvironment& operator=(const CEnvironment& env) {
		if (this != &env) {
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

	/// The block itself, for CreateProcess and git_init: nullptr when empty, which
	/// CreateProcess reads as "inherit" (see above).
	operator LPWSTR() const noexcept { return m_baseptr; }
	/// The stable address the ssh subtransport, the filter driver and gitdll's
	/// wget_msysgit_etc retain and dereference later (see above).
	operator const LPWSTR*() const noexcept { return &m_baseptr; }
	[[nodiscard]] LPWSTR get_cp() const noexcept { return m_baseptr; }
	/// get_cpp() used to be reinterpret_cast<wchar_t**>(m_block.data()) - the
	/// block itself posing as a pointer-to-pointer, so every consumer's *pEnv read
	/// the first characters of "NAME=value" as an address. It is &m_baseptr.
	[[nodiscard]] LPWSTR* get_cpp() noexcept { return &m_baseptr; }

private:
	void Serialize();

	MAP_ENVIRONMENT m_vars{};
	/// The serialized "name=value\0...\0\0" block, rebuilt on every mutation.
	std::vector<wchar_t> m_block{};
	/// Repointed at m_block by every Serialize(); its *address* is what is handed out.
	LPWSTR m_baseptr = nullptr;
};



void CEnvironment::Serialize() {
	m_block.clear();
	if (m_vars.empty()) {
		m_baseptr = nullptr; // an empty environment is nullptr, not an empty block
		return;
	}

	for (const auto& [name, value] : m_vars) {
		m_block.insert(m_block.end(), name.cbegin(), name.cend());
		m_block.push_back(L'=');
		m_block.insert(m_block.end(), value.cbegin(), value.cend());
		m_block.push_back(L'\0');
	}
	m_block.push_back(L'\0'); // the block's own terminator, after the last entry's
	m_baseptr = m_block.data();
}


void CEnvironment::clear() {
	m_vars.clear();
	Serialize(); // it cleared m_vars twice and never this, so the block kept the old environment
}


void CEnvironment::CopyProcessEnvironment() {
	wchar_t* porig = GetEnvironmentStrings();
	for (const wchar_t* p = porig; *p; p += wcslen(p) + 1) {
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


std::wstring CEnvironment::GetEnv(const std::wstring_view name) const {
	const auto it = m_vars.find(name);
	return it == m_vars.cend() ? std::wstring() : it->second;
}


void CEnvironment::SetEnv(const std::wstring_view name, const std::wstring_view value) {
	// Erase before inserting rather than insert_or_assign, so the *passed* spelling
	// of the name survives when it differs in case from what is already stored. The
	// map is case-insensitive, so insert_or_assign would keep the older spelling,
	// and the cygwin/msys builds hand this block to a shell that is not as relaxed
	// about that as Windows is.
	m_vars.erase(std::wstring(name));
	m_vars.emplace(name, value);
	Serialize();
}


void CEnvironment::UnsetEnv(const std::wstring_view name) {
	if (m_vars.erase(std::wstring(name)) == 0)
		return; // nothing removed, so the block cannot have changed
	Serialize();
}


void CEnvironment::AddToPath(std::wstring value) {
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
}

export using namespace tgit::types;
export using tgit::types::scope_exit_helper;
