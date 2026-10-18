// std.h : include file for standard system modules

// ReSharper disable CppUnusedIncludeDirective
#pragma once

// What were src\Git and src\Utils headers, now the TGitLibgit2Mfc modules.
import std;
import gsl;

import invarients;
import str_utils;		// UnicodeUtils.h, WideString.h, StringUtils.h
import DebugOutput;
import RIAA;			// SmartHandle.h
import PathUtils;
import TGitPath;		// TGitPath.h, and GitAdminDir
import git_h;			// Git.h, and what gittype.h/tgittypes held for git
import GitIndex;		// git_wc_status_kind / git_wc_status2_t, which tgittypes held
import wil;				// also the libgit2 owners, wil::unique_git_* (SmartLibgit2Ref.h's CAuto*)

// Git.h used to drag these in transitively; an import does not re-export what it imports.
import GitHash;
import GitRev;
import CmdLineParser;
import LangDll;
import TempFile;
import CShellUpdater;
import DarkModeHelper;

import TGitCache.CacheInterface;
using namespace TGitCache;

import Registry;
using namespace Registry;

import DirFileEnum;
using namespace DirFileEnum;

import GitDataObject;

// Names that were global in the header era and live in a namespace now.
using wil::CFormatMessageWrapper;
using CUnicodeUtils::SafeSizeToInt;
namespace fs = std::filesystem;

