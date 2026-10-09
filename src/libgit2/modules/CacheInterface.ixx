module;
#include <tgit_cstring.h>	// first: decides which CString this compile speaks
#include <minwindef.h>
#include <atlstr.h>


export module TGitCache.CacheInterface;
import std;

import RIAA;
import TGitStatus;
import tgittypes;
import DebugOutput;

using std::operator""sv;
using tgit::DebugOutput::CTraceToOutputDebugString;
using namespace tgit::types;


export namespace TGitCache
{
// The name of the named-pipe for the cache
constexpr auto TGIT_CACHE_PIPE_NAME = L"\\\\.\\pipe\\TGitCache";
constexpr auto TGIT_CACHE_COMMANDPIPE_NAME = L"\\\\.\\pipe\\TGitCacheCommand";
constexpr auto TGIT_CACHE_WINDOW_NAME = L"TGitCacheWindow";
constexpr auto TGIT_CACHE_MUTEX_NAME = L"TGitCacheMutex";

constexpr auto TGITCACHECOMMAND_END = 0;		///< ends the thread handling the pipe communication
constexpr auto TGITCACHECOMMAND_CRAWL = 1;		///< start crawling the specified path for changes
constexpr auto TGITCACHECOMMAND_REFRESHALL = 2;	///< Refreshes the whole cache, usually necessary after the "treat unversioned files as modified" option changed.
constexpr auto TGITCACHECOMMAND_RELEASE = 3;	///< Releases all open handles for the specified path and all paths below
constexpr auto TGITCACHECOMMAND_BLOCK = 4;		///< Blocks a path from getting crawled for a specific amount of time or until the TGITCACHECOMMAND_UNBLOCK command is sent for that path
constexpr auto TGITCACHECOMMAND_UNBLOCK = 5;	///< Removes a path from the list of paths blocked from getting crawled

/// Set this flag if you already know whether or not the item is a folder
constexpr auto TGITCACHE_FLAGS_FOLDERISKNOWN = 0x01;
/// Set this flag if the item is a folder
constexpr auto TGITCACHE_FLAGS_ISFOLDER = 0x02;
/// Set this flag if you want recursive folder status (safely ignored for file paths)
constexpr auto TGITCACHE_FLAGS_RECUSIVE_STATUS = 0x04;
/// all of the above flags or-gated:
constexpr auto TGITCACHE_FLAGS_MASK = 0x0f;



// std::wstring GetCachePipeName();
// std::wstring GetCacheCommandPipeName();
// std::wstring GetCacheMutexName();
// std::wstring GetCacheID();
// bool	SendCacheCommand(BYTE command, std::wstring_view path = {});


/**
 * \ingroup TGitCache
 * A structure passed as a request from the shell (or other client) to the external cache
 */
struct TGITCacheRequest
{
	DWORD flags;
	WCHAR path[MAX_PATH];
};

// CustomActions will use this header but does not need nor understand the SVN types ...

/**
 * \ingroup TGitCache
 * The structure returned as a response
 */
struct TGITCacheResponse
{
	UINT8 m_status;
	bool m_bAssumeValid;
	bool m_bSkipWorktree;
};
static_assert(static_cast<UINT8>(tgit::types::git_wc_status_unknown) == tgit::types::git_wc_status_unknown, "type git_wc_status_kind fits into UINT8");
static_assert(sizeof(TGITCacheResponse) == 3 && offsetof(TGITCacheResponse, m_status) == 0 && offsetof(TGITCacheResponse, m_bAssumeValid) == 1 && offsetof(TGITCacheResponse, m_bSkipWorktree) == 2, "Cross platform compatibility");

/**
 * \ingroup TGitCache
 * a cache command
 */
struct TGITCacheCommand
{
	BYTE command;				///< the command to execute
	WCHAR path[MAX_PATH];		///< path to do the command for
};

CString GetCacheID()
{
	CString t;
	CAutoGeneralHandle token;
	auto&& result = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.GetPointer());
	if(result)
	{
		DWORD len = 0;
		GetTokenInformation(token, TokenStatistics, nullptr, 0, &len);
		if (len >= sizeof (TOKEN_STATISTICS))
		{
			const auto data = std::make_unique<BYTE[]>(len);
			GetTokenInformation(token, TokenStatistics, data.get(), len, &len);
			const auto [LowPart, HighPart] = reinterpret_cast<PTOKEN_STATISTICS>(data.get())->AuthenticationId;
			t.Format(L"-%08x%08x", HighPart, LowPart);
		}
	}
	return t;
}

CString GetCachePipeName()
{
	return TGIT_CACHE_PIPE_NAME + GetCacheID();
}

CString GetCacheCommandPipeName()
{
	return TGIT_CACHE_COMMANDPIPE_NAME + GetCacheID();
}

CString GetCacheMutexName()
{
	return TGIT_CACHE_MUTEX_NAME + GetCacheID();
}

bool SendCacheCommand(const BYTE command, const WCHAR* path = nullptr)	// the default was only a comment
{
	CAutoFile hPipe;
	const CString pipeName = GetCacheCommandPipeName();
	for (int retry = 0; retry < 2; ++retry)
	{
		if (retry > 0)
			WaitNamedPipe(pipeName, 50);

		hPipe = CreateFile(
			pipeName,						// pipe name
			GENERIC_READ |					// read and write access
			GENERIC_WRITE,
			0,								// no sharing
			nullptr,						// default security attributes
			OPEN_EXISTING,					// opens existing pipe
			FILE_FLAG_OVERLAPPED,			// default attributes
			nullptr);						// no template file

		if (!hPipe && GetLastError() == ERROR_PIPE_BUSY)
		{
			// TGitCache is running but is busy connecting a different client.
			// Do not give up immediately but wait for a few milliseconds until
			// the server has created the next pipe instance
			continue;
		}
		break;
	}

	if (!hPipe)
	{
		CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Could not connect to pipe\n");
		return false;
	}

	// The pipe connected; change to message-read mode.
	DWORD dwMode = PIPE_READMODE_MESSAGE;
	if (SetNamedPipeHandleState(
		hPipe,		// pipe handle
		&dwMode,	// new pipe mode
		nullptr,	// don't set maximum bytes
		nullptr))	// don't set maximum time
	{
		DWORD cbWritten;
		TGITCacheCommand cmd{};
		cmd.command = command;
		if (path)
			wcsncpy_s(cmd.path, path, _TRUNCATE);

		int retrycount = 2;
		bool fSuccess;
		do
		{
			fSuccess = WriteFile(
				hPipe,			// handle to pipe
				&cmd,			// buffer to write from
				sizeof(cmd),	// number of bytes to write
				&cbWritten,		// number of bytes written
				nullptr);		// not overlapped I/O
			retrycount--;
			if (! fSuccess || sizeof(cmd) != cbWritten)
				Sleep(10);
		} while ((retrycount) && (! fSuccess || sizeof(cmd) != cbWritten));

		if (! fSuccess || sizeof(cmd) != cbWritten)
		{
			CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Could not write to pipe\n");
			DisconnectNamedPipe(hPipe);
			return false;
		}
		// now tell the cache we don't need it's command thread anymore
		SecureZeroMemory(&cmd, sizeof(TGITCacheCommand));
		cmd.command = TGITCACHECOMMAND_END;
		WriteFile(
			hPipe,			// handle to pipe
			&cmd,			// buffer to write from
			sizeof(cmd),	// number of bytes to write
			&cbWritten,		// number of bytes written
			nullptr);		// not overlapped I/O
		DisconnectNamedPipe(hPipe);
	}
	else
	{
		CTraceToOutputDebugString::Instance()(__FUNCTION__ ": SetNamedPipeHandleState failed\n");
		return false;
	}

	return true;
}

/**
 * \ingroup TGitCache
 * RIAA class that temporarily disables updates for the given path
 * by sending TGITCACHECOMMAND_BLOCK and TGITCACHECOMMAND_UNBLOCK.
 */
class CBlockCacheForPath
{
	WCHAR path[MAX_PATH]{};
	bool m_bBlocked = false;

public:
	explicit CBlockCacheForPath(const WCHAR * aPath)
	{
		wcsncpy_s(path, aPath, _countof(path) - 1);

		if (!SendCacheCommand(TGITCACHECOMMAND_BLOCK, path))
			return;

		// Wait a short while to make sure the cache has
		// processed this command. Without this, we risk
		// executing the svn command before the cache has
		// blocked the path and already gets change notifications.
		Sleep(20);
		m_bBlocked = true;
	}

	~CBlockCacheForPath()
	{
		if (!m_bBlocked)
			return;

		for (int retry = 0; retry < 3; ++retry)
		{
			if (retry > 0)
				Sleep(10);

			if (SendCacheCommand(TGITCACHECOMMAND_UNBLOCK, path))
				break;
		}
	}
};


}
export using namespace TGitCache;