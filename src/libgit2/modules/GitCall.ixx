module;
#include <windows.h>
#include <sanity.h>			// Expects: a macro, which no import carries

#include <gtest/internal/gtest-port.h>

// g_Git's constructor (git_h) runs CheckMsysGitDir, which writes the globals
// below - ms_LastMsysGitDir, m_CurrentDirStorage, ms_bCygwinGit. When this module
// and git_h were one Git.cpp, appearance order initialized them first. C++20 says
// an imported module's variables still come first, but MSVC orders dynamic
// initializers across objects by link order, so they could run *after* g_Git's
// constructor and reset what it wrote: Run then launched "\git.exe". The lib
// segment (.CRT$XCL) runs before every user one (.CRT$XCU), whatever the link order.
#pragma init_seg(lib)

/**
 * Running git.exe: the process, its pipes, and what is done with its output.
 *
 * Split out of git_h, which keeps only what needs a CGit - the msys/cygwin
 * command-line rewriting, the environment block and the working directory - and
 * hands the rest down here as plain values.
 *
 * Output is collected as *bytes* and decoded once the process has finished, by
 * whoever asked for text. Decoding each ReadFile chunk as it arrived split any
 * UTF-8 sequence that straddled a 1024-byte boundary, and it could not serve the
 * -z callers at all, whose output is NUL-separated records rather than text.
 */
export module GitCall;

import std;
import gsl;
import wil;
import str_utils;

import Registry;
import DebugOutput;
import ShellEnvironment;

using namespace std::literals;
namespace fs = std::filesystem;
using namespace tgit;
using DebugOutput::Trace;
using CStringW = wstr::CString;


namespace {

/// GetLastError() as text, without FormatMessage's trailing "\r\n". Call it
/// first thing after the failure, before anything else can reset the error.
std::wstring LastErrorText() {
	std::wstring err{ wil::CFormatMessageWrapper().c_str() };
	while (!err.empty() && std::iswspace(err.back()))
		err.pop_back();
	return err;
}

}



export namespace GitCall {

enum : std::uint8_t {
	TGIT_GIT_SUCCESS = 0,
	TGIT_GIT_ERROR_OPEN_PIP,
	TGIT_GIT_ERROR_CREATE_PROCESS,
	TGIT_GIT_ERROR_GET_EXIT_CODE,
};

constexpr auto G_OK = 0;
constexpr auto GFail = -1;

constexpr auto KB_2_10 = 1024UZ;

// arbitrary chosen 50 MiB limit that should be large enough for all real life situations in remote situations
constexpr auto GIT_EXE_BUFFER_LIMIT = 50UZ * KB_2_10 * KB_2_10;
// arbitrary chosen 1 KiB limit that should be large enough for all real life situations in remote situations
constexpr auto GIT_EXE_STDERR_BUFFER_LIMIT = KB_2_10;

constexpr auto CALL_OUTPUT_READ_CHUNK_SIZE = KB_2_10;


/**
 * The stderr reader of every git call in flight, keyed by the thread waiting on it.
 *
 * It exists for KillRelated: a dialog tearing down a worker that is blocked in a
 * git call terminates that worker, and the worker's reader has to go with it.
 */
class StdErrReaders {
	std::mutex m_lock; // a leaf lock: every holder only touches the map
	std::map<DWORD, HANDLE> m_byWaiter;

public:
	void Register(const DWORD waiter, const HANDLE reader) {
		std::scoped_lock _(m_lock);
		m_byWaiter[waiter] = reader;
	}


	void Unregister(const DWORD waiter) {
		std::scoped_lock _(m_lock);
		m_byWaiter.erase(waiter);
	}


	/// Takes the two things it reads from a CWinThread rather than the CWinThread:
	/// the core library has no business including MFC's UI (afxwin.h).
	void KillRelated(const DWORD threadId, const HANDLE threadHandle) {
		std::scoped_lock _(m_lock);
		if (const auto it = m_byWaiter.find(threadId); it != m_byWaiter.cend()) {
			TerminateThread(it->second, static_cast<DWORD>(-1));
			m_byWaiter.erase(it);
		}
		TerminateThread(threadHandle, static_cast<DWORD>(-1));
	}
};



/**
 * What one git.exe invocation does with its output.
 *
 * stdout goes to exactly one of: a byte sink, a line receiver, or nowhere.
 * stderr goes to a byte sink or nowhere. A null sink still drains its pipe - a
 * child blocked on a full pipe would never exit.
 *
 * Optional sinks are pointers (Core Guidelines F.60): a reference cannot be
 * absent, and std::optional<T&> is C++26, which MSVC's STL does not ship yet.
 */
class CGitCall {
public:
	/// Called once per line of stdout, without its '\n'.
	using LineReceiver = std::move_only_function<void(std::string_view)>;


	explicit CGitCall(std::wstring cmd, std::string* out = nullptr, std::string* err = nullptr) :
		m_Cmd(std::move(cmd)),
		m_pOut(out),
		m_pErr(err) {}


	CGitCall(std::wstring cmd, LineReceiver recv, std::string* err = nullptr) :
		m_Cmd(std::move(cmd)),
		m_recv(std::move(recv)),
		m_pErr(err) {}


	[[nodiscard]] const std::wstring& GetCmd() const noexcept { return m_Cmd; }

	/// Unlimited unless asked: only remote-facing calls opt in.
	void setLimit(const bool enableLimit) noexcept { m_maxLengthLimit = enableLimit; }
	[[nodiscard]] bool hasLimit() const noexcept { return m_maxLengthLimit; }
	/// Whether stdout was cut short. Set only when data was actually refused, so
	/// an output that merely ends near the limit does not read as truncated.
	[[nodiscard]] bool wasLimited() const noexcept { return m_outLimited; }
	[[nodiscard]] bool errWasLimited() const noexcept { return m_errLimited; }


	// Called when command output data is available. When these return 'true'
	// further output is ignored (but still drained).
	bool OnOutputData(const std::string_view data) {
		Expects(data.size() <= CALL_OUTPUT_READ_CHUNK_SIZE); // the read loop's buffer size
		// A rough limit, checked before appending, so the chunk that crosses it
		// still lands: it is a safety measure against remote servers, not a quota.
		if (m_maxLengthLimit && m_received >= GIT_EXE_BUFFER_LIMIT) {
			m_outLimited = true;
			return true;
		}
		m_received += data.size();

		if (m_recv) {
			m_pending.append(data);
			DeliverCompleteLines();
		} else if (m_pOut) {
			m_pOut->append(data);
		}
		return false;
	}


	/// Runs on the stderr reader thread, concurrently with OnOutputData; the two
	/// share no state.
	bool OnOutputErrData(const std::string_view data) {
		Expects(data.size() <= CALL_OUTPUT_READ_CHUNK_SIZE);
		if (!m_pErr)
			return false;
		if (m_maxLengthLimit && m_pErr->size() > GIT_EXE_STDERR_BUFFER_LIMIT) {
			m_errLimited = true;
			return true;
		}
		m_pErr->append(data);
		return false;
	}


	/**
	 * stdout reached EOF without OnOutputData having asked to stop.
	 *
	 * A last line without a '\n' is a whole line - --pretty=format: output ends
	 * that way - so it is delivered. Upstream dropped it when the output limit
	 * went in, to keep a truncated line from reaching a receiver; but a call
	 * that hit the limit was aborted, and an aborted call is never ended, so the
	 * only lines that ever reach this point are complete ones.
	 */
	void OnEnd() {
		if (m_recv && !m_pending.empty())
			m_recv(m_pending);
		m_pending.clear();
	}


	/// Tells whoever reads stderr that something was dropped. Only meaningful once
	/// both pipes are at EOF; upstream appended these before the process had even
	/// started, when neither condition could yet be true, so they never appeared.
	void AppendTruncationNotices() {
		if (!m_pErr)
			return;
		if (m_errLimited)
			m_pErr->append(std::format("\n\n[Git.exe error output truncated by TortoiseGit at about {} KiB]", GIT_EXE_STDERR_BUFFER_LIMIT / KB_2_10));
		if (m_outLimited)
			m_pErr->append(std::format("\n\n[Git.exe's output truncated by TortoiseGit at about {} MiB]", GIT_EXE_BUFFER_LIMIT / KB_2_10 / KB_2_10));
	}

private:
	std::wstring m_Cmd;
	std::string* m_pOut = nullptr;
	LineReceiver m_recv;
	std::string m_pending; // the receiver's incomplete last line
	std::string* m_pErr = nullptr;

	size_t m_received = 0;
	bool m_maxLengthLimit = false;
	bool m_outLimited = false;
	bool m_errLimited = false; // written by the stderr reader, read after it is joined

	void DeliverCompleteLines() {
		auto consumed = 0UZ;
		for (const auto& [line, next] : str::lazy_tokenize_lines(m_pending)) {
			m_recv(line);
			consumed = next;
		}
		m_pending.erase(0, consumed);
	}
};



namespace details {


/**
 * Starts \a cmdline with stdin closed and stdout/stderr on pipes.
 *
 * \a cmdline is final: whatever rewriting the git flavour needs has been done.
 * \a environment is a CREATE_UNICODE_ENVIRONMENT block, or nullptr to inherit.
 * Without \a hErrReadOut stderr shares the stdout pipe; with \a stdioFile stdout
 * goes to that file (truncated) instead of a pipe. On success the caller owns
 * \a piOut's handles and whatever was written to the two out-pointers.
 */
int LaunchProcess(
	std::wstring cmdline,
	const wchar_t* const environment,
	const wchar_t* const currentDirectory,
	PROCESS_INFORMATION& piOut,
	HANDLE* const hReadOut,
	HANDLE* const hErrReadOut,
	const wchar_t* const stdioFile = nullptr
) {
	wil::unique_handle hRead, hWrite, hReadErr, hWriteErr, hWriteIn, hReadIn;

	SECURITY_ATTRIBUTES sa{ .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE };
	if (!CreatePipe(hReadIn.put(), hWriteIn.put(), &sa, 0)) {
		Trace(__FUNCTION__ L": could not open stdin pipe: {}\n", LastErrorText());
		return TGIT_GIT_ERROR_OPEN_PIP;
	}
	if (!CreatePipe(hRead.put(), hWrite.put(), &sa, 0)) {
		Trace(__FUNCTION__ L": could not open stdout pipe: {}\n", LastErrorText());
		return TGIT_GIT_ERROR_OPEN_PIP;
	}
	if (hErrReadOut && !CreatePipe(hReadErr.put(), hWriteErr.put(), &sa, 0)) {
		Trace(__FUNCTION__ L": could not open stderr pipe: {}\n", LastErrorText());
		return TGIT_GIT_ERROR_OPEN_PIP;
	}

	// unique_hfile: CreateFile fails with INVALID_HANDLE_VALUE, not null. A failed
	// open has always meant an invalid stdout handle for the child, not an error;
	// get() hands the child the same value either way.
	wil::unique_hfile hStdioFile;
	if (stdioFile)
		hStdioFile.reset(CreateFileW(stdioFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.hStdInput = hReadIn.get();
	si.hStdError = hErrReadOut ? hWriteErr.get() : hWrite.get();
	si.hStdOutput = stdioFile ? hStdioFile.get() : hWrite.get();
	si.wShowWindow = SW_HIDE;
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

	constexpr DWORD dwFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;

	Trace(__FUNCTION__ L": executing {}\n", cmdline);
	PROCESS_INFORMATION pi{};
	// lpCommandLine is the one parameter CreateProcessW writes to; lpEnvironment
	// is only read, its LPVOID notwithstanding.
	if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE, dwFlags, const_cast<wchar_t*>(environment), currentDirectory, &si, &pi)) {
		Trace(__FUNCTION__ L": error while executing command: {}\n", LastErrorText());
		return TGIT_GIT_ERROR_CREATE_PROCESS;
	}

	// The child has its own copy of the stdin read end; closing ours of the write
	// end is what gives it EOF. The parent's copies of the write ends close with
	// this scope, so the parent's reads see EOF once the child exits.
	hWriteIn.reset();

	piOut = pi;
	if (hReadOut)
		*hReadOut = hRead.release();
	if (hErrReadOut)
		*hErrReadOut = hReadErr.release();
	return TGIT_GIT_SUCCESS;
}


struct StdErrReaderArgs {
	HANDLE pipe;
	CGitCall* call;
};


DWORD WINAPI ReadStdErr(const LPVOID param) {
	const auto& args = *static_cast<const StdErrReaderArgs*>(param);

	std::array<char, CALL_OUTPUT_READ_CHUNK_SIZE> data{};
	DWORD read = 0;
	bool dropMode = false;
	while (ReadFile(args.pipe, data.data(), CALL_OUTPUT_READ_CHUNK_SIZE, &read, nullptr)) {
		if (!dropMode && args.call->OnOutputErrData({ data.data(), read }))
			dropMode = true;
	}
	return 0;
}

}



/**
 * Drives a launched process to completion: feeds \a call from both pipes, waits
 * for the process, and returns its exit code - or GFail when stdout was cut
 * short by the limit, or one of the TGIT_GIT_ERROR codes.
 *
 * Takes ownership of every handle it is given. \a hRead may be empty, when
 * stdout went to a file; \a hReadErr may be empty, when stderr shares stdout.
 * stdout is read on the calling thread, stderr on a second one, so that neither
 * pipe can fill while the other is being waited on.
 */
int CollectCall(
	CGitCall& call,
	const PROCESS_INFORMATION& pi,
	const wil::unique_handle hRead,
	const wil::unique_handle hReadErr,
	StdErrReaders& readers
) {
	const wil::unique_handle piThread{ pi.hThread };
	const wil::unique_handle piProcess{ pi.hProcess };

	details::StdErrReaderArgs args{ .pipe = hReadErr.get(), .call = &call };
	wil::unique_handle reader;
	if (hReadErr) {
		reader.reset(CreateThread(nullptr, 0, details::ReadStdErr, &args, 0, nullptr));
		if (reader)
			readers.Register(GetCurrentThreadId(), reader.get());
	}

	bool aborted = false;
	if (hRead) {
		std::array<char, CALL_OUTPUT_READ_CHUNK_SIZE> data{};
		DWORD read = 0;
		while (ReadFile(hRead.get(), data.data(), CALL_OUTPUT_READ_CHUNK_SIZE, &read, nullptr)) {
			// TODO: when OnOutputData() returns 'true', abort git-command. Send CTRL-C signal?
			if (!aborted && call.OnOutputData({ data.data(), read }))
				aborted = true; // keep draining: a child blocked on a full pipe never exits
		}
	}
	if (!aborted)
		call.OnEnd();

	if (reader) {
		WaitForSingleObject(reader.get(), INFINITE);
		readers.Unregister(GetCurrentThreadId());
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	call.AppendTruncationNotices();

	DWORD exitcode = 0;
	if (!GetExitCodeProcess(pi.hProcess, &exitcode)) {
		Trace(__FUNCTION__ L": could not get exit code: {}\n", LastErrorText());
		return TGIT_GIT_ERROR_GET_EXIT_CODE;
	}
	Trace(__FUNCTION__ L": process exited: {}\n", exitcode);

	if (call.wasLimited())
		return GFail;

	return static_cast<int>(exitcode);
}

#pragma region The invoke API (from Git.ixx)
/**
 * The invoke API, in two spellings of the same thing.
 *
 * The argv overloads are the ones to write new code against: an element is
 * an element, and quoting happens once, in SerializeArgv. The CStringW
 * overloads take a line that is *already* a command line - the caller has
 * done its own quoting, historically with QuoteParameter at every site - and
 * they exist because ~280 call sites still spell it that way. They are the
 * legacy half of this migration, not a second supported style.
 *
 * The two cannot be collapsed by conversion: splitting a flat line back into
 * argv is not something you can do correctly after the fact, which is the
 * reason the flat form has to be retired site by site rather than adapted.
 */

constexpr auto CygwinHack = L"CygwinHack"sv;
constexpr auto Msys2Hack = L"Msys2Hack"sv;
constexpr wchar_t DiffSimilarityIndexThreshold[] = L"DiffSimilarityIndexThreshold";



bool ms_bCygwinGit = wil::reg::get_bool(HKEY_CURRENT_USER, Registry::TortoiseGitKey, CygwinHack.data(), false);
bool ms_bMsys2Git = wil::reg::get_bool(HKEY_CURRENT_USER, Registry::TortoiseGitKey, Msys2Hack.data(), false);
std::wstring gitLastErr;
StdErrReaders m_stdErrReaders;
// this is a HACK to protect again excessive input from remote servers
bool s_limitGitExeOutput = false;

PROCESS_INFORMATION m_CurrentGitPi{};

inline int m_LogEncode = CP_UTF8;
void set_log_encoding(const int encoding) {
	m_LogEncode = encoding;
}


// Process-level state, like the latched object format (GitHash.ixx): TortoiseGit
// runs one process per working copy, and there is one git installation per
// process. Namespace scope is what every importer already spells
// (ms_iSimilarityIndexThreshold in GitRevLoglist and DiffCommand), and
// it keeps the values reachable from the static members of CGit.
fs::path ms_LastMsysGitDir{}; // the last msysgitdir added to the path, blank if none
fs::path ms_MsysGitRootDir{};
size_t ms_iSimilarityIndexThreshold = []{
	// absent is 50, as upstream's CRegDWORD default was: 0 switched rename detection off
	const size_t index = wil::reg::try_get_dword(HKEY_CURRENT_USER, Registry::TortoiseGitKey, DiffSimilarityIndexThreshold).value_or(50);
	return (index <= 100) ? index : 50;
}();
// Only the two setters above may name this. Everything else - some 400 read
// sites - goes through the reference below.
fs::path m_CurrentDirStorage{};


bool has_weird_paths() { return ms_bMsys2Git || ms_bCygwinGit; }


constexpr auto TG_SUBDIRECTORY_NAME = L"TortoiseGit"sv;


std::expected<fs::path, std::error_code> GetTortoiseGitTempPath() {
	auto lpPathBuffer = std::array<wchar_t, MAX_PATH>{};
	const size_t result_sz = ::GetTempPath2(win32_cast(lpPathBuffer.size()), lpPathBuffer.data());
	if (result_sz == 0)
		return std::unexpected(std::error_code{win32_cast(::GetLastError()), std::system_category()});
	const auto tmp_root = fs::path{ std::wstring_view{lpPathBuffer.data(), result_sz} };
	auto ret = tmp_root / TG_SUBDIRECTORY_NAME;
	std::error_code ec;
	fs::create_directory(ret, ec);
	if (ec && ec.value() != ERROR_ALREADY_EXISTS)
		return std::unexpected(ec);
	return ret;
}


/// Upstream's two-call contract, which TempFiles, TortoiseShell and the settings
/// pages size their buffers by: the length *with* the NUL, both when it fits and
/// when it does not - GetTempPath(0, nullptr) style - and the path ends with "\".
/// Returning 0 for a short buffer made every sizing call read as a failure.
DWORD GetTortoiseGitTempPath(const DWORD nBufferLength, LPWSTR lpBuffer) {
	const auto exp_tmp_dir = GetTortoiseGitTempPath();
	if (!exp_tmp_dir) {
		if (lpBuffer && nBufferLength)
			lpBuffer[0] = L'\0';
		return 0;
	}
	auto tmp_dir = exp_tmp_dir->native();
	if (!tmp_dir.ends_with(L'\\'))
		tmp_dir += L'\\';
	const auto needed = gsl::narrow<DWORD>(tmp_dir.size() + 1);
	if (!lpBuffer || needed > nBufferLength) {
		if (lpBuffer && nBufferLength)
			lpBuffer[0] = L'\0';
		return needed;
	}

	*std::ranges::copy(tmp_dir, lpBuffer).out = L'\0';
	return needed;
}



CStringW GetTempFile() {
	auto lpPathBuffer = std::array<wchar_t, MAX_PATH>{};
	auto szTempName = std::array<wchar_t, MAX_PATH>{};

	const auto dwRetVal = GetTortoiseGitTempPath(win32_cast(lpPathBuffer.size()), lpPathBuffer.data());
	if (dwRetVal > lpPathBuffer.size() || (dwRetVal == 0))
		return L"";

	// Create a temporary file.
	auto&& ret = GetTempFileName(
		lpPathBuffer.data(),	// directory for tmp files
		TEXT("Patch"),			// temp file name prefix
		0,						// create unique name
		szTempName.data()		// buffer for name
	);
	if (!ret)
		return L"";

	return CStringW(szTempName.data());
}


[[deprecated("Yuck! Use `CStringW GetTempFile()`")]]
void GetTempPath(CStringW& path) {
	path = GetTempFile();
}


/**
 * The CGit half of launching git: turning a git command line into one this
 * machine can execute. Everything after that - pipes, CreateProcess - is
 * GitCall's LaunchProcess, which needs nothing from CGit but its environment
 * block and working directory.
 */
int RunAsync(
	const std::wstring_view cmd_in,
	PROCESS_INFORMATION& piOut,
	HANDLE* hReadOut,
	HANDLE* hErrReadOut,
	const std::wstring_view StdioFile = {}
) {
	auto cmd_tmp = std::wstring{cmd_in};

	const bool startsWithGit = cmd_tmp.starts_with(L"git");
	if (has_weird_paths() && startsWithGit) {
		Trace(__FUNCTION__ L": executing via bash: {}\n", cmd_tmp);
		CStringW tmpFile = GetTempFile();
		if (ms_bMsys2Git)
			cmd_tmp = L"/usr/bin/" + cmd_tmp;
		else if (ms_bCygwinGit)
			cmd_tmp = L"/bin/" + cmd_tmp;
		StringUtils::WriteStringToTextFile(tmpFile, cmd_tmp.c_str());
		tmpFile.Replace(L'\\', L'/');
		// std::format rather than operator+: a wchar_t* + wchar_t is pointer arithmetic
		cmd_tmp = std::format(L"\"{}\\bash.exe\" \"{}\"", ms_LastMsysGitDir.native(), tmpFile.GetString());
	} else if (startsWithGit || cmd_tmp.starts_with(L"bash")) {
		const std::wstring_view view{ cmd_tmp };
		const auto firstSpace = view.find(L' ');
		if (firstSpace != view.npos && firstSpace > 0)
			cmd_tmp = std::format(L"\"{}\\{}\"{}", ms_LastMsysGitDir.native(), view.substr(0, firstSpace), view.substr(firstSpace));
		else
			cmd_tmp = std::format(L"\"{}\\{}\"", ms_LastMsysGitDir.native(), view);
	}

	m_CurrentGitPi = {};
	auto&& ret = details::LaunchProcess(
		std::move(cmd_tmp),
		Environment::g_Environment.get(),
		m_CurrentDirStorage.c_str(),
		m_CurrentGitPi,
		hReadOut,
		hErrReadOut,
		StdioFile.data()
	);
	if (ret)
		return ret;

	piOut = m_CurrentGitPi;
	return TGIT_GIT_SUCCESS;
}

int RunAsync(const wil::ArgvData& argv, PROCESS_INFORMATION& pi, HANDLE* hRead, HANDLE* hErrReadOut, const CStringW* StdioFile = nullptr) {
	if (StdioFile)
		return RunAsync(argv.SerializeArgv(has_weird_paths()), pi, hRead, hErrReadOut, str::StringView{*StdioFile});
	else
		return RunAsync(argv.SerializeArgv(has_weird_paths()), pi, hRead, hErrReadOut);
}

/// The one that runs. Every overload below builds a CGitCall and lands here;
/// s_limitGitExeOutput is applied here so that none of them can forget it.
int Run(CGitCall& call) {
	// OR, not assignment: a call that opted in on its own keeps its limit.
	if (s_limitGitExeOutput)
		call.setLimit(true);

	PROCESS_INFORMATION pi{};
	wil::unique_handle hRead;
	wil::unique_handle hReadErr;
	if (RunAsync(call.GetCmd(), pi, hRead.put(), hReadErr.put()))
		return TGIT_GIT_ERROR_CREATE_PROCESS;

	return CollectCall(call, pi, std::move(hRead), std::move(hReadErr), m_stdErrReaders);
}


/// Raw bytes, for -z output and anything else that is not text.
int Run(const wil::ArgvData& argv, std::string* out = nullptr, std::string* err = nullptr) {
	CGitCall call{ argv.SerializeArgv(has_weird_paths()), out, err };
	return Run(call);
}


/// Line by line, each as raw bytes without its '\n'. stderr is decoded as UTF-8.
template <std::invocable<std::string_view> Receiver, str::TStrOfWide ErrStr = std::wstring>
int RunReceiver(const wil::ArgvData& argv, Receiver&& recv, ErrStr* outputErr = {}) {
	std::string err;
	CGitCall call{ argv.SerializeArgv(has_weird_paths()), CGitCall::LineReceiver{ std::forward<Receiver>(recv) }, &err };
	auto&& ret = Run(call);
	if (outputErr)
		if constexpr (std::is_convertible_v<ErrStr, CStringW>)
			StringAppend(*outputErr, err, CP_UTF8);
		else
			*outputErr = CUnicodeUtils::StdGetUnicode(err);
	else
		gitLastErr = CUnicodeUtils::StdGetUnicode(err);

	return ret;
}


/*
 * Text, decoded from \a code once the process has finished - never chunk by
 * chunk, which would split a multi-byte sequence at a read boundary.
 *
 * The CStringW pair is two overloads rather than one with a defaulted err,
 * because Run(argv, &out) has to pick exactly one of them: with both
 * parameters defaulted, (out, code) and (out, err, code) would tie.
 */
template <wstr::WideSink Sink1, wstr::WideSink Sink2>
int RunText(const wil::ArgvData& argv, Sink1* out, Sink2* err, const int code = CP_UTF8) {
	std::string outBytes;
	std::string errBytes;
	auto&& ret = Run(argv, out ? &outBytes : nullptr, err ? &errBytes : nullptr);
	if (out)
		out->append(til::MultiByteToWideChar(outBytes, { .code_page = code }));
	if (err)
		err->append(til::MultiByteToWideChar(errBytes, { .code_page = code }));
	return ret;
}

/// Upstream's Run(cmd, output, code): stderr follows stdout in \a out, after a
/// '\n' when both have text - the form for showing a command's result to a user.
template <wstr::WideSink Sink>
int RunTextMerged(const wil::ArgvData& argv, Sink* out, const int code) {
	std::string outBytes;
	std::string errBytes;
	auto&& ret = Run(argv, &outBytes, &errBytes);
	if (out) {
		out->append(til::MultiByteToWideChar(outBytes, { .code_page = code }));
		if (!errBytes.empty()) {
			if (!outBytes.empty())
				out->append(L"\n");
			out->append(til::MultiByteToWideChar(errBytes, { .code_page = code }));
		}
	}
	return ret;
}

// Two overloads per string type, as upstream had, because "no err argument" and
// "err is nullptr" mean different things: the first merges stderr into out, the
// second keeps them apart and a nullptr err discards stderr (GetFullRefName parses
// out and must not see "warning: refname is ambiguous"). One overload with a
// defaulted err made them the same call.
int Run(const wil::ArgvData& argv, std::wstring* const out, const int code = CP_UTF8) {
	return RunTextMerged(argv, out, code);
}
int Run(const wil::ArgvData& argv, std::wstring* const out, std::wstring* const err, const int code = CP_UTF8) {
	return RunText(argv, out, err, code);
}
int Run(const wil::ArgvData& argv, CStringW* out, const int code = CP_UTF8) {
	wstr::AtlSink outSink{ out };
	return RunTextMerged(argv, out ? &outSink : nullptr, code);
}
int Run(const wil::ArgvData& argv, CStringW* out, CStringW* err, const int code = CP_UTF8) {
	wstr::AtlSink outSink{ out };
	wstr::AtlSink errSink{ err };
	return RunText(argv, out ? &outSink : nullptr, err ? &errSink : nullptr, code);
}


/// stdout goes to \a filename, stderr (decoded as UTF-8) to \a stdErr. Unlimited:
/// the output lands on disk, not in memory, so s_limitGitExeOutput does not apply.
int RunLogFile(const std::wstring cmd, const std::wstring_view filename, std::string* const errBytes = nullptr) {
	PROCESS_INFORMATION pi{};
	wil::unique_handle hReadErr;
	if (RunAsync(cmd, pi, nullptr, hReadErr.put(), filename))
		return TGIT_GIT_ERROR_CREATE_PROCESS;

	auto call = CGitCall{ cmd, nullptr, errBytes };
	auto&& ret = CollectCall(call, pi, {}, std::move(hReadErr), m_stdErrReaders);
	return ret;
}
int RunLogFile(const CStringW& atl_cmd, const CStringW& filename, CStringW* const stdErr = nullptr) {
	std::string errBytes;
	auto&& ret = RunLogFile(std::wstring{atl_cmd}, str::StringView{filename}, &errBytes);
	if (stdErr)
		*stdErr += CUnicodeUtils::GetUnicode<CStringW>(errBytes);
	return ret;
}

int RunLogFile(const wil::ArgvData& argv, const CStringW& filename, CStringW* stdErr = nullptr) {
	return RunLogFile(argv.SerializeArgv(has_weird_paths()).c_str(), filename, stdErr);
}

/// Callers pass m_pThread->m_nThreadID, ->m_hThread; see StdErrReaders::KillRelated.
void KillRelatedThreads(const DWORD threadId, const HANDLE threadHandle) {
	m_stdErrReaders.KillRelated(threadId, threadHandle);
}

#pragma endregion

}
