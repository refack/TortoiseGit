export module tgit;

import std;
import wil;
import gsl;
import wil.MFC;
import str_utils;
import git_h;
import GitCall;


export namespace tgit {
	namespace std {
		using namespace std;
	}

	using namespace TGitH;
	using namespace GitCall;
	namespace wil {
		using namespace wil;
	}
	namespace gsl {
		using namespace gsl;
	}
}

export using namespace tgit;