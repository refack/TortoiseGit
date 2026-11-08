export module tgit;

// The dialogs' one-stop aggregation: re-export the modules themselves, so an
// importer's `std::wstring` means ::std. (The previous shape - nested
// `namespace std { using namespace std; }` inside an exported `namespace tgit`
// plus a global using-directive - made every `std`, `wil` and `gsl` ambiguous
// (C2872): inside tgit::std, the unqualified `std` names tgit::std itself.)
import std;	// NOT re-exported: the dialogs' stdafx brings std textually, and a
			// re-export would collide with every textual <map>/<memory> after it
export import wil;
export import gsl;
export import wil.MFC;
export import str_utils;
export import git_h;
export import GitCall;


export namespace tgit {
	using namespace TGitH;
	using namespace GitCall;
}

export using namespace tgit;
