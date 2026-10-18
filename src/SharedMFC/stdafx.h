// std.h : include file for standard system include files,
// or project specific include files that are used frequently,
// but are changed infrequently

// ReSharper disable CppUnusedIncludeDirective
#pragma once
// Every textual include first, every import last: a textual std header after an
// `import std;` redefines what the import brought (C2011/C2995), and the module
// imports below all import std.
#include <SDKDDKVer.h>

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit NOLINT(*-reserved-identifier)

// turns off MFC's hiding of some common and often safely ignored warning messages
#define _AFX_ALL_WARNINGS	// NOLINT(*-reserved-identifier)

#include <afxwin.h>         // MFC core and standard components
#include <afxext.h>         // MFC extensions
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Wspiapi.h>
#include <comdef.h>		// _COM_SMARTPTR_TYPEDEF (AnimationManager.h); the old PCH supplied it
#include <shlobj.h>
#include <Shlwapi.h>
#include <shlguid.h>
#include <uxtheme.h>
#include <dlgs.h>
#include <wininet.h>
#include <math.h>
#include <gdiplus.h>
#include <intsafe.h>


#include <afxdtctl.h>			// MFC support for Internet Explorer 4 Common Controls

#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>				// MFC support for Windows Common Controls
#endif							// _AFX_NO_AFXCMN_SUPPORT

#include <afxdlgs.h>
#include <afxctl.h>
#include <afxtempl.h>
#include <afxmt.h>
#include <afxext.h>				// MFC extensions
#include <afxcontrolbars.h>     // MFC support for ribbons and control bars
#include <afxtaskdialog.h>

#include <atlcom.h>				// ATL COM support <CFileDlgEventHandler>


#define USE_GDI_GRADIENT
#define HISTORYCOMBO_WITH_SYSIMAGELIST

#ifdef _WIN64
#	define APP_X64_STRING	"x64"
#else
#	define APP_X64_STRING ""
#endif
