// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include <SDKDDKVer.h>

#include <algorithm>
using std::max;
using std::min;

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <string>

#include <combaseapi.h>
#include <windows.h>
#include <Commdlg.h>

#ifdef _M_X64
#include <emmintrin.h>
#elifdef _M_ARM64
//#include ".\sse2neon\emmintrin.h"
#else
#error Unsupported architecture
#endif


#define COMMITMONITOR_FINDMSGPREV		(WM_APP+1)
#define COMMITMONITOR_FINDMSGNEXT		(WM_APP+2)
#define COMMITMONITOR_FINDEXIT			(WM_APP+3)
#define COMMITMONITOR_FINDRESET			(WM_APP+4)

#define REGSTRING_DARKTHEME L"Software\\TortoiseGit\\UDiffDarkTheme"

#include <tgitlib/sanity.h>		// SCOPE_EXIT; textual includes before the imports
// tgitlib-level modules only: `tgit` is SharedMFC's MFC-side aggregate, and UDiff links tgitlib alone.
// The .cpp files import what they use themselves (ThemeAPI, Registry, str_utils, DarkModeHelper, ...);
// this header supplies only what every UDiff TU leans on: wil:: helpers and RIAA's CAutoIcon/CAutoFile.
import std;
import wil;
import RIAA;

#ifdef _WIN64
#   define APP_X64_STRING "x64"
#else
#   define APP_X64_STRING ""
#endif
