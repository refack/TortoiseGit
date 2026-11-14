// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently,
// but are changed infrequently
//
// Textual includes first (stdafx_pch.h, also the target's precompiled header),
// imports last. The imports stay out of the PCH: see stdafx_pch.h.

#pragma once
#include "stdafx_pch.h"

import std;
import str_utils;		// tgit::wstr, tgit::str::StringView (the CString bridge), CUnicodeUtils, StringUtils
import DebugOutput;
import RIAA;
import wil;				// the libgit2 owners (wil::unique_git_*), scope_exit_helper for SCOPE_EXIT
import AutoTempDir;

namespace fs = std::filesystem;
using tgit::wstr::CString;
using CStringA = tgit::zstr::CString;
