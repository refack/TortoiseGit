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
#include <set>
#include <string>
#include <vector>

#include <afxcmn.h>			// MFC support for Windows Common Controls
#include <afxcontrolbars.h> // MFC support for ribbons and control bars
#include <afxctl.h>
#include <afxdlgs.h>
#include <afxdtctl.h>		// MFC support for Internet Explorer 4 Common Controls
#include <afxext.h>         // MFC extensions
#include <afxext.h>         // MFC extensions
#include <afxmt.h>
#include <afxtempl.h>
#include <afxwin.h>         // MFC core and standard components

// ATL
#include <atlbase.h>

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

// TGit/Utils
import DebugOutput;
#include <Utils/scope_exit_noexcept.h>
import RIAA;
#include <Utils/SmartLibgit2Ref.h>

#include "AutoTempDir.h"
