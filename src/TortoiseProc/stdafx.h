// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently,
// but are changed infrequently

#pragma once
import invarients;
#include <SDKDDKVer.h>

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit

#include <algorithm>
using std::max;
using std::min;

// turns off MFC's hiding of some common and often safely ignored warning messages
#define _AFX_ALL_WARNINGS

#include <afxwin.h>         // MFC core and standard components
#include <afxext.h>         // MFC extensions
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Wspiapi.h>

#include <afxdtctl.h>		// MFC support for Internet Explorer 4 Common Controls
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>			// MFC support for Windows Common Controls
#endif // _AFX_NO_AFXCMN_SUPPORT
#include <afxdlgs.h>
#include <afxctl.h>
#include <afxtempl.h>
#include <afxmt.h>
#include <afxext.h>         // MFC extensions
#include <afxcontrolbars.h>     // MFC support for ribbons and control bars
#include <afxtaskdialog.h>

#include <atlbase.h>

#include <git2.h>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <format>
#include <locale>
#include <xlocale>
#include <concepts>
#include <format>
#include <functional>
#include <map>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>

#include <vfw.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <shlguid.h>
#include <uxtheme.h>
#include <dlgs.h>
#include <wininet.h>
#include <assert.h>
#include <math.h>
#include <gdiplus.h>
#include <intsafe.h>

#define USE_GDI_GRADIENT
#define HISTORYCOMBO_WITH_SYSIMAGELIST

#include <afxdhtml.h>

#ifdef _WIN64
#	define APP_X64_STRING	"x64"
#else
#	define APP_X64_STRING ""
#endif

#include <Git/Git.h>
#include <Git/TGitPath.h>
import GitAdminDir;
#include <Git/gittype.h>

#include <Utils/DebugOutput.h>
#include <Utils/PathUtils.h>
#include <Utils/ProfilingInfo.h>
#include <Utils/scope_exit_noexcept.h>
#include <Utils/SmartHandle.h>
import SmartLibgit2;
#include <Utils/StringUtils.h>
#include <Utils/UnicodeUtils.h>
#include <Utils/WideString.h>

#include <gitdll.h>
