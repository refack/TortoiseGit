// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently,
// but are changed infrequently

#pragma once

#include <SDKDDKVer.h>

#include <algorithm>
using std::max;
using std::min;

#define ISOLATION_AWARE_ENABLED 1

#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Wspiapi.h>
#include <windows.h>

#include <commctrl.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <wininet.h>
#include <Aclapi.h>

#include <atlbase.h>
#include <atlexcept.h>
#include <atlstr.h>

#include <string>
#include <set>
#include <map>
#include <vector>
#include <functional>

#define CSTRING_AVAILABLE


#include <gitdll.h>
#include <git2.h>
#include <sanity.h>		// SCOPE_EXIT
import wil;		// the libgit2 owners (wil::unique_git_*), scope_exit_helper for SCOPE_EXIT

#include <Utils/SysInfo.h>
import DebugOutput;
import RIAA;

