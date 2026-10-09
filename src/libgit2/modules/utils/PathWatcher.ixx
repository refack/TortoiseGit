module;
#include <tgit_cstring.h>	// first: decides which CString this compile speaks
#define READ_DIR_CHANGE_BUFFER_SIZE 4096
#define MAX_CHANGED_PATHS  4000
#include <atlstr.h>
#include "sanity.h"


export module PathWatcher;
import std;
import RIAA;
import TGitPath;
import DebugOutput;

using AutoLocker = CComCritSecLock<CComCriticalSection>;

/**
 * Helper class: provides information about watched directories.
 */
struct CDirWatchInfo
{
	CAutoFile	m_hDir;				///< handle to the directory that we're watching
	CTGitPath	m_dirName{};		///< the directory that we're watching
	CHAR		m_buffer[READ_DIR_CHANGE_BUFFER_SIZE]{}; ///< buffer for ReadDirectoryChangesW
	OVERLAPPED	m_overlapped{};
	CString		m_dirPath{};		///< the directory name we're watching with a backslash at the end

	CDirWatchInfo() = delete;
	CDirWatchInfo & operator=(const CDirWatchInfo & rhs) = delete;

	CDirWatchInfo(CAutoFile&& hDir, const CTGitPath& directoryName)
		: m_hDir(std::move(hDir))
		, m_dirName(directoryName)
	{
		Expects(m_hDir && !directoryName.IsEmpty());
		m_buffer[0] = 0;
		m_dirPath = m_dirName.GetWinPathString().c_str();
		if (m_dirPath.GetAt(m_dirPath.GetLength() - 1) != '\\')
			m_dirPath += L'\\';
	}
};

/**
 * \ingroup Utils
 * Watches the file system for changes.
 *
 * When a CPathWatcher object is created, a new thread is started which
 * waits for file system change notifications.
 * To add folders to the list of watched folders, call \c AddPath().
 *
 * The folders are watched recursively. To prevent having too many folders watched,
 * children of already watched folders are automatically removed from watching.
 */
export class CPathWatcher
{
	CComAutoCriticalSection	m_critSec;
	CAutoGeneralHandle		m_hThread;
	CAutoGeneralHandle		m_hCompPort;
	volatile LONG			m_bRunning = TRUE;
	CTGitPathList			watchedPaths{};	///< list of watched paths.
	CTGitPathList			m_changedPaths{};	///< list of paths which got changed
	bool					m_bLimitReached = false;

	std::map<HANDLE, CDirWatchInfo *> watchInfoMap;

	static unsigned int __stdcall ThreadEntry(void* pContext)
	{
		static_cast<CPathWatcher*>(pContext)->WorkerThread();
		return 0;
	}

	void WorkerThread();
	void ClearInfoMap()
	{
		if (!watchInfoMap.empty())
		{
			[[maybe_unused]] AutoLocker lock(m_critSec);
			for (auto I : watchInfoMap)
			{
				delete I.second;
				I.second = nullptr;
			}
		}
		watchInfoMap.clear();
		m_hCompPort.CloseHandle();
	}
public:
	CPathWatcher(){
		// enable the required privileges for this process
		constexpr LPCWSTR arPrivelegeNames[] = {	SE_BACKUP_NAME,
										SE_RESTORE_NAME,
										SE_CHANGE_NOTIFY_NAME
									 };

		for (auto arPrivelegeName : arPrivelegeNames)
		{
			CAutoGeneralHandle hToken;
			if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, hToken.GetPointer()))
			{
				TOKEN_PRIVILEGES tp = { 1 };

				if (LookupPrivilegeValue(nullptr, arPrivelegeName, &tp.Privileges[0].Luid))
				{
					tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

					AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
				}
			}
		}

		unsigned int threadId = 0;
		m_hThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, ThreadEntry, this, 0, &threadId));
	}
	~CPathWatcher()
	{
		Stop();
		AutoLocker _(m_critSec);
		ClearInfoMap();
	}

	// prevent cloning
	CPathWatcher(const CPathWatcher&) = delete;
	CPathWatcher& operator=(const CPathWatcher&) = delete;

	/**
	 * Adds a new path to be watched. The path \b must point to a directory.
	 * If the path is already watched because a parent of that path is already
	 * watched recursively, then the new path is just ignored and the method
	 * returns false.
	 */
	bool AddPath(const CTGitPath& path);

	/**
	 * Removes a path and all its children from the watched list.
	 */
	bool RemovePathAndChildren(const CTGitPath& path)
	{
		bool bRemoved = false;
		AutoLocker _(m_critSec);
		repeat:
			for (int i=0; i<watchedPaths.GetCount(); ++i)
			{
				if (path.IsAncestorOf(watchedPaths[i]))
				{
					watchedPaths.RemovePath(watchedPaths[i]);
					bRemoved = true;
					goto repeat;
				}
			}
		return bRemoved;
	}

	/**
	 * Returns the number of recursively watched paths.
	 */
	[[nodiscard]] int GetNumberOfWatchedPaths() const { return watchedPaths.GetCount(); }

	/**
	 * Stops the watching thread.
	 */
	void Stop()
	{
		InterlockedExchange(&m_bRunning, FALSE);
		if (m_hCompPort)
		{
			PostQueuedCompletionStatus(m_hCompPort, 0, NULL, nullptr);
			m_hCompPort.CloseHandle();
		}

		if (m_hThread)
		{
			// the background thread sleeps for 200ms,
			// so lets wait for it to finish for 1000 ms.

			WaitForSingleObject(m_hThread, 1000);
			m_hThread.CloseHandle();
		}
	}

	/**
	 * Returns the number of changed paths up to now.
	 */
	int GetNumberOfChangedPaths()
	{
		AutoLocker _(m_critSec);
		return m_changedPaths.GetCount();
	}

	/**
	 * Returns the list of paths which maybe got changed, i.e., for which
	 * a change notification was received.
	 */
	CTGitPathList GetChangedPaths() { AutoLocker _(m_critSec); return m_changedPaths; }

	/**
	 * Clears the list of changed paths
	 */
	void ClearChangedPaths() { AutoLocker _(m_critSec); m_changedPaths.Clear(); }

	/**
	 * If the limit of changed paths has been reached, returns true.
	 * \remark the limit is necessary to avoid memory problems.
	 */
	[[nodiscard]] bool LimitReached() const { return m_bLimitReached; }
};


bool CPathWatcher::AddPath(const CTGitPath& path)
{
	AutoLocker _(m_critSec);
	for (int i=0; i<watchedPaths.GetCount(); ++i)
	{
		if (watchedPaths[i].IsAncestorOf(path))
			return false;		// already watched (recursively)
	}

	// now check if with the new path we might have a new root
	CTGitPath newRoot;
	for (int i = 0; i < watchedPaths.GetCount(); ++i)
	{
		const CString& watched = watchedPaths[i].GetWinPathString().c_str();
		const CString& sPath   = path.GetWinPathString().c_str();
		int minLen = std::min(sPath.GetLength(), watched.GetLength());

		int len = 0;
		for (; len < minLen; ++len)
		{
			if (watched.GetAt(len) != sPath.GetAt(len))
			{
				if ((len > 1) && (len < minLen))
				{
					if (sPath.GetAt(len) == '\\')
					{
						newRoot = CTGitPath(sPath.Left(len));
					}
					else if (watched.GetAt(len) == '\\')
					{
						newRoot = CTGitPath(watched.Left(len));
					}
				}
				break;
			}
		}
		if (len == minLen)
		{
			if (sPath.GetLength() == minLen)
			{
				if (watched.GetLength() > minLen)
				{
					if (watched.GetAt(len) == '\\' || sPath.GetLength() == 3 && sPath[1] == ':')
					{
						newRoot = path;
					}
				}
			}
			else
			{
				if (sPath.GetLength() > minLen)
				{
					if (sPath.GetAt(len) == '\\' || watched.GetLength() == 3 && watched[1] == ':')
					{
						newRoot = CTGitPath(watched);
					}
				}
			}
		}
	}
	if (!newRoot.IsEmpty())
	{
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(TEXT(__FUNCTION__) L": add path to watch %s\n", newRoot.GetWinPath());
		watchedPaths.AddPath(newRoot);
		watchedPaths.RemoveChildren();
		m_hCompPort.CloseHandle();
		return true;
	}
	tgit::DebugOutput::CTraceToOutputDebugString::Instance()(TEXT(__FUNCTION__) L": add path to watch %s\n", path.GetWinPath());
	watchedPaths.AddPath(path);
	m_hCompPort.CloseHandle();
	return true;
}


void CPathWatcher::WorkerThread()
{
	DWORD numBytes = 0;
	CDirWatchInfo* pdi = nullptr;
	LPOVERLAPPED lpOverlapped = nullptr;
	const int bufferSize = MAX_PATH * 4;
	while (m_bRunning)
	{
		if (!watchedPaths.IsEmpty())
		{
			if (!m_hCompPort || !GetQueuedCompletionStatus(m_hCompPort,
														   &numBytes,
														   reinterpret_cast<PULONG_PTR>(&pdi),
														   &lpOverlapped,
														   INFINITE))
			{
				// Error retrieving changes
				// Clear the list of watched objects and recreate that list
				if (!m_bRunning)
					return;
				{
					AutoLocker _(m_critSec);
					ClearInfoMap();
				}
				DWORD lastErr = GetLastError();
				if ((m_hCompPort) && (lastErr != ERROR_SUCCESS) && (lastErr != ERROR_INVALID_HANDLE))
				{
					m_hCompPort.CloseHandle();
				}
				for (int i = 0; i < watchedPaths.GetCount(); ++i)
				{
					CAutoFile hDir = CreateFile(watchedPaths[i].GetWinPath(),
												FILE_LIST_DIRECTORY,
												FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
												nullptr, //security attributes
												OPEN_EXISTING,
												FILE_FLAG_BACKUP_SEMANTICS | //required privileges: SE_BACKUP_NAME and SE_RESTORE_NAME.
													FILE_FLAG_OVERLAPPED,
												nullptr);
					if (!hDir)
					{
						// this could happen if a watched folder has been removed/renamed
						m_hCompPort.CloseHandle();
						AutoLocker _(m_critSec);
						watchedPaths.RemovePath(watchedPaths[i]);
						i--;
						// if (i < 0)
						// 	i = 0;
						break;
					}

					auto pDirInfo = std::make_unique<CDirWatchInfo>(std::move(hDir), watchedPaths[i]); // the new CDirWatchInfo object owns the handle now
					m_hCompPort = CreateIoCompletionPort(pDirInfo->m_hDir, m_hCompPort, reinterpret_cast<ULONG_PTR>(pDirInfo.get()), 0);
					if (m_hCompPort == NULL)
					{
						AutoLocker _(m_critSec);
						ClearInfoMap();
						watchedPaths.RemovePath(watchedPaths[i]);
						i--;
						// if (i < 0)
						// 	i = 0;
						break;
					}
					if (!ReadDirectoryChangesW(pDirInfo->m_hDir,
												pDirInfo->m_buffer,
												bufferSize,
												TRUE,
												FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
												&numBytes, // not used
												&pDirInfo->m_overlapped,
												nullptr)) //no completion routine!
					{
						AutoLocker _(m_critSec);
						ClearInfoMap();
						watchedPaths.RemovePath(watchedPaths[i]);
						i--;
						// if (i < 0)
						// 	i = 0;
						break;
					}
					AutoLocker _(m_critSec);
					tgit::DebugOutput::CTraceToOutputDebugString::Instance()(TEXT(__FUNCTION__) L": watching path %s\n", pDirInfo->m_dirName.GetWinPath());
					watchInfoMap[pDirInfo->m_hDir] = pDirInfo.get();
					pDirInfo.release();
				}
			}
			else
			{
				if (!m_bRunning)
					return;
				// NOTE: the longer this code takes to execute until ReadDirectoryChangesW
				// is called again, the higher the chance that we miss some
				// changes in the file system!
				if (pdi)
				{
					if (numBytes != 0)
					{
						auto pNotify = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(pdi->m_buffer);
						if (reinterpret_cast<ULONG_PTR>(pNotify) - reinterpret_cast<ULONG_PTR>(pdi->m_buffer) <= bufferSize)
						{
							DWORD nOffset;
							while ((nOffset = pNotify->NextEntryOffset) != 0)
							{
								// The watched directory plus the notification's name, which is
								// FileNameLength *bytes* long and not NUL-terminated. A path longer
								// than the fixed buffer this used to be built in is skipped, which is
								// what the old STRUNCATE test meant to do - but wcsncat_s only returns
								// STRUNCATE for a _TRUNCATE count, so it went to the invalid-parameter
								// handler instead.
								std::wstring changedPath(pdi->m_dirPath.GetString(), static_cast<size_t>(pdi->m_dirPath.GetLength()));
								changedPath.append(pNotify->FileName, pNotify->FileNameLength / sizeof(wchar_t));
								pNotify = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(reinterpret_cast<LPBYTE>(pNotify) + nOffset);
								if (changedPath.size() >= static_cast<size_t>(bufferSize))
									continue;
								tgit::DebugOutput::CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": change notification: %s\n", changedPath.c_str());
								{
									AutoLocker _(m_critSec);
									if (m_changedPaths.GetCount() < MAX_CHANGED_PATHS)
										m_changedPaths.AddPath(CTGitPath(changedPath));
									else
										m_bLimitReached = true;
								}
								if (reinterpret_cast<ULONG_PTR>(pNotify) - reinterpret_cast<ULONG_PTR>(pdi->m_buffer) > bufferSize)
									break;
							}
						}
					}
					{
						AutoLocker _(m_critSec);
						m_changedPaths.RemoveDuplicates();
					}
					SecureZeroMemory(pdi->m_buffer, sizeof(pdi->m_buffer));
					SecureZeroMemory(&pdi->m_overlapped, sizeof(OVERLAPPED));
					if (!ReadDirectoryChangesW(pdi->m_hDir,
											   pdi->m_buffer,
											   bufferSize,
											   TRUE,
											   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
											   &numBytes, // not used
											   &pdi->m_overlapped,
											   nullptr)) //no completion routine!
					{
						// Since the call to ReadDirectoryChangesW failed, just
						// wait a while. We don't want to have this thread
						// running using 100% CPU if something goes completely
						// wrong.
						Sleep(200);
					}
				}
			}
		} // if (watchedPaths.GetCount())
		else
			Sleep(200);
	} // while (m_bRunning)
}
