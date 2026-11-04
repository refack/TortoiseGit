//
// Created by refac on 03/11/2026.
//

#pragma once
#include "stdafx_pch.h"

import wil;		// the libgit2 owners (wil::unique_git_*), scope_exit_helper for SCOPE_EXIT
import SysInfo;
import DebugOutput;
import RIAA;
import PreserveChdir;

using namespace RIAA;
using namespace RegistryDep;

namespace fs = std::filesystem;