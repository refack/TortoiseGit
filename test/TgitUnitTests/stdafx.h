// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently,
// but are changed infrequently

#pragma once
#define XMESSAGEBOX_APPREGPATH "Software\\TortoiseGit\\"

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <regex>		// textually, BEFORE `import std;` below: FileTextLines.h includes it, and a
						// textual std header after the import redefines everything (C2011, C2995)
#include <set>
#include <string>
#include <vector>

// ATL, not MFC: the modules under test see only <atlstr.h>, so their CString is
// CStringT<wchar_t, StrTraitATL>. Under MFC this TU's CString would be the
// StrTraitMFC_DLL flavor - a different type that cannot bind to the modules'
// const CString& parameters (CLAUDE.md, "The CString ABI wall").
#include <atlbase.h>
#include <atlstr.h>
#include <atltime.h>

// Windows SDK
#include <SDKDDKVer.h>
#include <WinInet.h>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Wspiapi.h>

//libgit2
#include <git2.h>

// Header for gtest
#include <gmock/gmock.h>
#include <gtest/gtest.h>

// EXPECT_STREQ for std::basic_string. The migrated APIs return std::wstring where
// they returned CString, and gtest's CmpHelperSTREQ takes only C strings - a
// CString converted to one implicitly, a std::wstring does not. EXPECT_STREQ
// names ::testing::internal::CmpHelperSTREQ qualified, so overload resolution
// sees these alongside gtest's own; each forwards to the C-string comparison,
// so the semantics (and the failure message) are gtest's.
namespace testing::internal
{
	inline AssertionResult CmpHelperSTREQ(const char* e1, const char* e2, const wchar_t* s1, const std::wstring& s2) { return CmpHelperSTREQ(e1, e2, s1, s2.c_str()); }
	inline AssertionResult CmpHelperSTREQ(const char* e1, const char* e2, const std::wstring& s1, const wchar_t* s2) { return CmpHelperSTREQ(e1, e2, s1.c_str(), s2); }
	inline AssertionResult CmpHelperSTREQ(const char* e1, const char* e2, const std::wstring& s1, const std::wstring& s2) { return CmpHelperSTREQ(e1, e2, s1.c_str(), s2.c_str()); }
	inline AssertionResult CmpHelperSTREQ(const char* e1, const char* e2, const char* s1, const std::string& s2) { return CmpHelperSTREQ(e1, e2, s1, s2.c_str()); }
	inline AssertionResult CmpHelperSTREQ(const char* e1, const char* e2, const std::string& s1, const char* s2) { return CmpHelperSTREQ(e1, e2, s1.c_str(), s2); }
	inline AssertionResult CmpHelperSTREQ(const char* e1, const char* e2, const std::string& s1, const std::string& s2) { return CmpHelperSTREQ(e1, e2, s1.c_str(), s2.c_str()); }
}

// TGit/Utils
#include <sanity.h>		// SCOPE_EXIT
import std;
import str_utils;		// tgit::wstr, tgit::str::StringView (the CString bridge), CUnicodeUtils, StringUtils
import DebugOutput;
import RIAA;
import wil_git2;		// CAutoRepository & co., was Utils/SmartLibgit2Ref.h

#include "AutoTempDir.h"
