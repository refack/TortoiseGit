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
// #include <vfw.h>		// unused
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
// And ATL base last
#include <atlbase.h>

#include <git2.h>

#include <algorithm>
using std::max;
using std::min;
#include <concepts>
#include <format>
#include <format>
#include <functional>
#include <functional>
#include <locale>
#include <map>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <vector>
#include <xlocale>
// The rest of the std headers project headers include, so that a later textual
// include finds them guarded rather than re-declaring them after `import std;`.
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <list>
#include <memory>
#include <mutex>
#include <new>			// std::nothrow (SimpleIni.h)
#include <numeric>
#include <optional>
#include <ostream>
#include <iostream>
#include <istream>
#include <queue>
#include <random>
#include <ranges>
#include <regex>
#include <shared_mutex>
#include <span>
#include <stack>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#define USE_GDI_GRADIENT
#define HISTORYCOMBO_WITH_SYSIMAGELIST

// #include <afxdhtml.h>	// unused

#ifdef _WIN64
#	define APP_X64_STRING	"x64"
#else
#	define APP_X64_STRING ""
#endif

#include <winrt/base.h>	// before the imports too: the wil shim's fragment includes it, and
						// RegisterWin11ContextMenu's later winrt/*.h would redefine it (C2766)
#include <gitdll.h>
#include <Utils/ProfilingInfo.h>

// Last
#include <sanity.h>		// SCOPE_EXIT; its scope_exit_helper comes with `import wil` below
