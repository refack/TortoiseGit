// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once


#include <algorithm>
using std::min;
using std::max;

#define CSTRING_AVAILABLE

#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Wspiapi.h>

#include <windows.h>

#include <ShlObj.h>
#include <Shlwapi.h>
#include <initguid.h>
#include <ioevent.h>
#include <shellapi.h>

// ATL base only (never <atlstr.h>: CString comes from str_utils). Supplies _T (via
// <tchar.h>), ATLASSERT, ATL::CAtlException and ATL::CComHeapPtr; the names TGitCache
// uses are pulled in by the using-declarations below instead of `using namespace ATL`.
#define _ATL_NO_AUTOMATIC_NAMESPACE
#include <atlbase.h>

#include <conio.h>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <functional>
#include <mutex>		// std::recursive_mutex, which TGitH::CAutoLocker (AutoLocker below) locks
// Every std header a TGitCache TU names must be textually included here, before the
// first `import`: a std #include that follows an import re-defines what the imported
// modules' GMFs already merged (C2011/C2995). ShellUpdater.cpp includes <format>,
// DirectoryWatcher.cpp includes <list>, CachedDirectory.cpp's <gsl/narrow> pulls in
// <array> and <span>; this makes their late includes no-ops.
#include <format>
#include <list>
#include <array>
#include <span>

#include <git2.h>
#include <tgitlib/sanity.h>		// SCOPE_EXIT (CreateProcessHelper.h)

#ifdef _WIN64
#	define APP_X64_STRING	"x64"
#else
#	define APP_X64_STRING ""
#endif


import wil;				// scope_exit_helper, which SCOPE_EXIT expands to
import DebugOutput;
import GitIndex;			// git_wc_status_kind / git_wc_status2_t, which tgittypes held
import PathUtils;
import git_h;		// was "Git.h"; also TGitH::OpenGitRepository and the config readers
import RIAA;
import Registry;	// namespace RegistryDep
import str_utils;	// tgit::wstr::CString; TGitCache is wide-only (was <atlstr.h> + using namespace ATL)

using CString = tgit::wstr::CString;
using AutoLocker = TGitH::CAutoLocker;
using ATL::CAtlException;
using ATL::CComHeapPtr;

using namespace RIAA;
using namespace RegistryDep;

namespace fs = std::filesystem;