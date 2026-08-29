module;
#include <TGitCache/CacheInterface.h>
#include "Git.h"
#include "Utils/scope_exit_noexcept.h"

#include <shlobj_core.h>

export module CShellUpdater;
import RIAA;
import TGitPath;
import DebugOutput;

/**
 * \ingroup Utils
 * This singleton class contains a list of items which require a shell-update notification
 * This update is done lazily at the end of a run of Git operations
 */
export class CShellUpdater
{
	CShellUpdater() : m_hInvalidationEvent{ CreateEvent(nullptr, FALSE, FALSE, L"TortoiseGitCacheInvalidationEvent") } {}
	~CShellUpdater()
	{
		Flush();

		CloseHandle(m_hInvalidationEvent);
	}

	void UpdateShell();

	// The list of paths which will need updating
	CTGitPathList m_pathsForUpdating;
	// A handle to an event which, when set, tells the ShellExtension to purge its status cache
	HANDLE m_hInvalidationEvent;

  public:
	static CShellUpdater &Instance();
	// prevent cloning
	CShellUpdater(const CShellUpdater &) = delete;
	CShellUpdater &operator=(const CShellUpdater &) = delete;

	/**
	 * Add a single path for updating.
	 * The update will happen at some suitable time in the future
	 */
	void AddPathForUpdate(const CTGitPath &path);
	/**
	 * Add a list of paths for updating.
	 * The update will happen at some suitable time in the future
	 */
	void AddPathsForUpdate(const CTGitPathList &pathList);
	/**
	 * Do the update, and clear the list of items waiting
	 */
	void Flush();

	static bool RebuildIcons();
};


CShellUpdater &CShellUpdater::Instance()
{
	static CShellUpdater instance;
	return instance;
}

/**
 * Add a single path for updating.
 * The update will happen at some suitable time in the future
 */
void CShellUpdater::AddPathForUpdate(const CTGitPath &path)
{
	// Tell the shell extension to purge its cache - we'll redo this when
	// we actually do the shell-updates, but sometimes there's an earlier update, which
	// might benefit from cache invalidation
	SetEvent(m_hInvalidationEvent);

	m_pathsForUpdating.AddPath(path);
}
/**
 * Add a list of paths for updating.
 * The update will happen when the list is destroyed, at the end of execution
 */
void CShellUpdater::AddPathsForUpdate(const CTGitPathList &pathList)
{
	for (int nPath = 0; nPath < pathList.GetCount(); ++nPath) {
		AddPathForUpdate(pathList[nPath]);
	}
}

void CShellUpdater::Flush()
{
	if (m_pathsForUpdating.IsEmpty())
		return;

	CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Flushing shell update list\n");

	UpdateShell();
	m_pathsForUpdating.Clear();
}

void CShellUpdater::UpdateShell()
{
	// Tell the shell extension to purge its cache
	CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Setting cache invalidation event %I64u\n", GetTickCount64());
	SetEvent(m_hInvalidationEvent);

	// We use the SVN 'notify' call-back to add items to the list
	// Because this might call-back more than once per file (for example, when committing)
	// it's possible that there may be duplicates in the list.
	// There's no point asking the shell to do more than it has to, so we remove the duplicates before
	// passing the list on
	m_pathsForUpdating.RemoveDuplicates();

	// if we use the external cache, we tell the cache directly that something
	// has changed, without the detour via the shell.
	const RIAA::CAutoFile hPipe = CreateFile(
		GetCacheCommandPipeName().c_str(), // pipe name
		GENERIC_READ | // read and write access
			GENERIC_WRITE,
		0, // no sharing
		nullptr, // default security attributes
		OPEN_EXISTING, // opens existing pipe
		FILE_FLAG_OVERLAPPED, // default attributes
		nullptr); // no template file

	if (!hPipe)
		return;

	// The pipe connected; change to message-read mode.
	DWORD dwMode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(
			hPipe, // pipe handle
			&dwMode, // new pipe mode
			nullptr, // don't set maximum bytes
			nullptr)) // don't set maximum time
	{
		CTraceToOutputDebugString::Instance()(__FUNCTION__ ": SetNamedPipeHandleState failed\n");
		return;
	}

	CTGitPath path;
	for (int nPath = 0; nPath < m_pathsForUpdating.GetCount(); ++nPath) {
		path.SetFromWin(g_Git.CombinePath(m_pathsForUpdating[nPath]));
		CTraceToOutputDebugString::Instance()(
			_T(__FUNCTION__) L": Cache Item Update for %s (%I64u)\n",
			path.GetWinPathString().c_str(),
			GetTickCount64()
		);
		if (!path.IsDirectory()) {
			// send notifications to the shell for changed files - folders are updated by the cache itself.
			SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH | SHCNF_FLUSHNOWAIT, path.GetWinPath(), nullptr);
		}
		DWORD cbWritten;
		TGITCacheCommand cmd;
		cmd.command = TGITCACHECOMMAND_CRAWL;
		wcsncpy_s(cmd.path, path.GetDirectory().GetWinPath(), _countof(cmd.path) - 1);
		const BOOL fSuccess = WriteFile(
			hPipe, // handle to pipe
			&cmd, // buffer to write from
			sizeof(cmd), // number of bytes to write
			&cbWritten, // number of bytes written
			nullptr); // not overlapped I/O

		if (!fSuccess || sizeof(cmd) != cbWritten) {
			DisconnectNamedPipe(hPipe);
			return;
		}
	}

	// now tell the cache we don't need it's command thread anymore
	DWORD cbWritten;
	TGITCacheCommand cmd;
	cmd.command = TGITCACHECOMMAND_END;
	WriteFile(
		hPipe, // handle to pipe
		&cmd, // buffer to write from
		sizeof(cmd), // number of bytes to write
		&cbWritten, // number of bytes written
		nullptr
	); // not overlapped I/O
	DisconnectNamedPipe(hPipe);
}

bool CShellUpdater::RebuildIcons()
{
	constexpr auto sRegValueName = L"Shell Icon Size";
	constexpr int BUFFER_SIZE = 1024;
	wchar_t buf[BUFFER_SIZE] = {};

	HKEY hRegKey = nullptr;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", 0, KEY_READ | KEY_WRITE, &hRegKey) != ERROR_SUCCESS)
		return false;

	SCOPE_EXIT
	{
		RegCloseKey(hRegKey);
	};

	// we're going to change the Shell Icon Size value

	// Read registry value
	DWORD dwSize = BUFFER_SIZE;
	if (
		const LONG lRegResult = RegQueryValueEx(hRegKey, sRegValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &dwSize);
		lRegResult != ERROR_FILE_NOT_FOUND
	) {
		// If registry key doesn't exist create it using system current setting
		int iDefaultIconSize = GetSystemMetrics(SM_CXICON);
		if (0 == iDefaultIconSize)
			iDefaultIconSize = 32;
		_snwprintf_s(buf, BUFFER_SIZE, BUFFER_SIZE, L"%d", iDefaultIconSize);
	} else if (lRegResult != ERROR_SUCCESS)
		return false;

	// Change registry value
	const DWORD dwRegValue = _wtoi(buf);
	const DWORD dwRegValueTemp = dwRegValue - 1;

	dwSize = _snwprintf_s(buf, BUFFER_SIZE, BUFFER_SIZE, L"%lu", dwRegValueTemp) + sizeof(wchar_t);
	if (RegSetValueEx(hRegKey, sRegValueName, 0, REG_SZ, reinterpret_cast<LPBYTE>(buf), dwSize) != ERROR_SUCCESS)
		return false;

	// Update all windows
	DWORD_PTR dwResult;
	SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, 0, SMTO_ABORTIFHUNG, 5000, &dwResult);

	// Reset registry value
	dwSize = _snwprintf_s(buf, BUFFER_SIZE, BUFFER_SIZE, L"%lu", dwRegValue) + sizeof(wchar_t);
	if (RegSetValueEx(hRegKey, sRegValueName, 0, REG_SZ, reinterpret_cast<LPBYTE>(buf), dwSize) != ERROR_SUCCESS)
		return false;

	// Update all windows
	SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, 0, SMTO_ABORTIFHUNG, 5000, &dwResult);

	return true;
}
