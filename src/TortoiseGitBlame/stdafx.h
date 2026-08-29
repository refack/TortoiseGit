
// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently,
// but are changed infrequently

#pragma once

#ifndef _SECURE_ATL
#define _SECURE_ATL 1
#endif

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define USE_GDI_GRADIENT
#define HISTORYCOMBO_WITH_SYSIMAGELIST
#define REGSTRING_DARKTHEME L"Software\\TortoiseGit\\TortoiseGitBlame\\Settings\\DarkTheme"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
using std::max;
using std::min;

// some CString constructors will be explicit
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS

// turns off MFC's hiding of some common and often safely ignored warning messages
#define _AFX_ALL_WARNINGS

#include <afxcmn.h>         // MFC support for Windows Common Controls
#include <afxcontrolbars.h> // MFC support for ribbons and control bars
#include <afxdisp.h>        // MFC Automation classes
#include <afxdtctl.h>       // MFC support for Internet Explorer 4 Common Controls
#include <afxext.h>         // MFC extensions
#include <afxstr.h>
#include <afxwin.h>         // MFC core and standard components
#include <Commctrl.h>
#include <SDKDDKVer.h>

// Git stuff
#include <git2.h>
import DebugOutput;
#include <Utils/scope_exit_noexcept.h>
import RIAA;
import SmartLibgit2;

#ifdef _UNICODE
#ifdef _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif


