module;
#include <windows.h>

export module RollingFile;

import std;

namespace {

std::string ToUtf8(std::wstring_view s)
{
	std::string out(WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr), '\0');
	WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
	return out;
}

}

export namespace tgit {

// A size-rolled log file that several processes append to at once: the shell extension
// lives in every explorer.exe, alongside TGitCache and TortoiseGitProc. Appends are
// atomic (FILE_APPEND_DATA); rolling over is serialized by a session-wide named mutex.
// Rolled-over files are kept as <path>.1 (newest) … <path>.<maxFiles>.
class CRollingFile
{
public:
	CRollingFile(std::filesystem::path path, std::uintmax_t maxBytes, unsigned maxFiles)
		: m_path(std::move(path))
		, m_maxBytes(maxBytes)
		, m_maxFiles(maxFiles)
	{
		auto key = m_path.wstring();
		std::ranges::transform(key, key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		m_hRollLock = CreateMutexW(nullptr, FALSE, std::format(L"Local\\TortoiseGit.DebugLog.{:x}", std::hash<std::wstring>{}(key)).c_str());

		wchar_t exe[MAX_PATH]{};
		GetModuleFileNameW(nullptr, exe, _countof(exe));
		m_process = std::filesystem::path(exe).stem().wstring();

		std::error_code ec;
		std::filesystem::create_directories(m_path.parent_path(), ec);
		Open();
	}

	~CRollingFile()
	{
		if (m_hFile != INVALID_HANDLE_VALUE)
			CloseHandle(m_hFile);
		if (m_hRollLock)
			CloseHandle(m_hRollLock);
	}

	CRollingFile(const CRollingFile&) = delete;
	CRollingFile& operator=(const CRollingFile&) = delete;

	// Appends one line stamped with time, process and thread; trailing newlines are dropped.
	void Append(std::wstring_view message)
	{
		while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r'))
			message.remove_suffix(1);

		SYSTEMTIME t;
		GetLocalTime(&t);
		const auto line = ToUtf8(std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{} {}:{}] {}\r\n",
			t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
			m_process, GetCurrentProcessId(), GetCurrentThreadId(), message));

		std::scoped_lock lock(m_mutex);
		if (m_maxBytes && Size(m_hFile) >= m_maxBytes && GetTickCount64() >= m_nextRollAttempt)
			Roll();
		if (m_hFile == INVALID_HANDLE_VALUE)
			return;
		DWORD written;
		WriteFile(m_hFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
	}

private:
	void Open()
	{
		m_hFile = CreateFileW(m_path.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	}

	static std::uintmax_t Size(HANDLE h)
	{
		LARGE_INTEGER size{};
		return h != INVALID_HANDLE_VALUE && GetFileSizeEx(h, &size) ? size.QuadPart : 0;
	}

	std::filesystem::path Numbered(unsigned i) const
	{
		return i ? std::filesystem::path(m_path.native() + L'.' + std::to_wstring(i)) : m_path;
	}

	void Roll()
	{
		// Whatever goes wrong — the lock times out, or someone holds the file without
		// FILE_SHARE_DELETE (an editor, say) — keep appending to the oversized file and
		// don't pay for a failed roll on every line.
		m_nextRollAttempt = GetTickCount64() + 10000;
		const DWORD wait = m_hRollLock ? WaitForSingleObject(m_hRollLock, 1000) : WAIT_FAILED;
		if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
			return;

		// Another process may have rolled already, leaving our handle on <file>.1; then the
		// path itself is small again and all that is needed is to reopen it.
		WIN32_FILE_ATTRIBUTE_DATA attr;
		bool rolled = true;
		if (GetFileAttributesExW(m_path.c_str(), GetFileExInfoStandard, &attr)
			&& ((std::uintmax_t{ attr.nFileSizeHigh } << 32) | attr.nFileSizeLow) >= m_maxBytes)
		{
			CloseHandle(m_hFile);
			m_hFile = INVALID_HANDLE_VALUE;
			if (m_maxFiles == 0)
				rolled = DeleteFileW(m_path.c_str());
			for (unsigned i = m_maxFiles; i > 0; --i)
				rolled = (MoveFileExW(Numbered(i - 1).c_str(), Numbered(i).c_str(), MOVEFILE_REPLACE_EXISTING) || GetLastError() == ERROR_FILE_NOT_FOUND) && rolled;
		}
		if (m_hFile != INVALID_HANDLE_VALUE)
			CloseHandle(m_hFile);
		Open();
		ReleaseMutex(m_hRollLock);

		if (rolled)
			m_nextRollAttempt = 0;
	}

	const std::filesystem::path m_path;
	const std::uintmax_t m_maxBytes;
	const unsigned m_maxFiles;
	std::wstring m_process;
	HANDLE m_hFile = INVALID_HANDLE_VALUE;
	HANDLE m_hRollLock = nullptr;
	ULONGLONG m_nextRollAttempt = 0;
	std::mutex m_mutex;
};

}
