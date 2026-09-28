module;
#include <atlstr.h>
#include <git2/config.h>

export module GitAdminDir;
import std;
import gsl;
import wil;
import wstr;

import RIAA;
import PathUtils;
import DebugOutput;
import TGitPath;
import StringUtils;
import Registry;
import SmartLibgit2;

using std::operator""sv;
using std::operator""s;
using std::operator+;
using std::wstring_view;
using std::wstring;
namespace fs = std::filesystem;
using tgit::wstr::StringView;


