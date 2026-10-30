module;
#include <git2/errors.h>
#include <git2/index.h>
#include <git2/sys/errors.h>
#include <git2/sys/transport.h>



export module libgit2;
import std;
import str_utils;


// NOLINTBEGIN(*-pro-type-vararg, bugprone-suspicious-stringview-data-usage)
export namespace tgit::libgit2 {
using ::git_error_clear;	// the function itself, re-exported
using ::git_error_last;
using ::git_error;			// types work the same way
using str::CStringA;
using str::CStringW;


constexpr auto TGIT_INDEX_ENTRY_STAGEMASK = GIT_INDEX_ENTRY_STAGEMASK;
constexpr auto TGIT_INDEX_ENTRY_SKIP_WORKTREE = GIT_INDEX_ENTRY_SKIP_WORKTREE;


void SetLibGit2SearchPath(const int level, const CStringW& value) {
	const auto valueA = CUnicodeUtils::StdGetUTF8(value);
	git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, level, valueA.c_str());
}


void SetLibGit2SearchPath(const int level, const std::string_view value) {
	git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, level, value.data());
}


void SetLibGit2TemplatePath(const CStringW& value) {
	const auto valueA = CUnicodeUtils::StdGetUTF8(value);
	git_libgit2_opts(GIT_OPT_SET_TEMPLATE_PATH, valueA.c_str());
}


void libgit2_transport_register(const std::string_view prefix, git_smart_subtransport_cb callback) {
	static auto def = git_smart_subtransport_definition{ .callback = callback };
	git_transport_register(prefix.data(), git_transport_smart, &def);
}


void SetLibGit2UserAgent() {
	git_libgit2_opts(GIT_OPT_SET_USER_AGENT, "TortoiseGit libgit2");
}


}
// NOLINTEND(*-pro-type-vararg, bugprone-suspicious-stringview-data-usage)
