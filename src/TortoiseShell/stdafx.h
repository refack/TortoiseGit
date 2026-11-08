//
// Created by refac on 03/11/2026.
//

#pragma once
#include "stdafx_pch.h"

import std;
import wil;		// the libgit2 owners (wil::unique_git_*), scope_exit_helper for SCOPE_EXIT
import Registry;	// RegistryDep
import SysInfo;
import DebugOutput;
import RIAA;
// no `import PreserveChdir;` here: this header reaches PreserveChdir.ixx itself
// (via the PCH), and a module unit importing its own module is a dyndep cycle.
// The two users (ContextMenu.cpp, IconOverlay.cpp) import it directly.

using namespace RIAA;
using namespace RegistryDep;

namespace fs = std::filesystem;