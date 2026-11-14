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

export namespace tgit {
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
}

export using namespace tgit;
