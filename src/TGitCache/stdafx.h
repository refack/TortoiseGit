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

#include <conio.h>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <functional>

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

using AutoLocker = TGitH::CAutoLocker;

using namespace RIAA;
using namespace RegistryDep;

namespace fs = std::filesystem;