export module tgit;

export import std;
export import wil;
export import gsl;
export import wil.MFC;
export import str_utils;
export import git_h;
export import GitCall;
export import GitHash;
export import GitRev;
export import DebugOutput;
export import libgit2;
export import invarients;
export import Colors;
export import ThemeAPI;
export import TGitPath;
export import misc_constants;
export import RIAA;
export import Registry;
export import TempFiles;
export import PathUtils;
export import CmdLineParser;
export import LangDll;
export import DarkModeHelper;
export import DirFileEnum;
export import TGitDataObject;
export import TGitCache.CacheInterface;


export namespace tgit {
	namespace fs = std::filesystem;
	using namespace std::literals;

	using namespace TGitH;
	using namespace GitCall;
	using namespace DebugOutput;
	using namespace ThemeAPI;
	using namespace misc_constants;
	using namespace TGitHash;
	using namespace TGitPath;
	using namespace TGitRev;
	using namespace RIAA;
	using namespace RegistryDep;
	using namespace TempFiles;
	using namespace PathUtils;
	using namespace DirFileEnum;
	using namespace TGitDataObject;
	using namespace TGitCache;
}

export using namespace tgit;
