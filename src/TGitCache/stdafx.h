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

#include <atlbase.h>
#include <atlstr.h>

#include <conio.h>

using namespace ATL;

#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <functional>

#include <git2.h>
#include <sanity.h>		// SCOPE_EXIT (CreateProcessHelper.h)
import wil;				// scope_exit_helper, which SCOPE_EXIT expands to
import DebugOutput;
import GitIndex;			// git_wc_status_kind / git_wc_status2_t, which tgittypes held

using AutoLocker = CComCritSecLock<CComAutoCriticalSection>;

#ifdef _WIN64
#	define APP_X64_STRING	"x64"
#else
#	define APP_X64_STRING ""
#endif
