module;
#include <git2/config.h>
#include <git2/errors.h>
#include <git2/index.h>
#include <git2/sys/errors.h>
#include <git2/sys/transport.h>
#include <git2.h>



export module libgit2;
import std;
import str_utils;


// NOLINTBEGIN(*-pro-type-vararg, bugprone-suspicious-stringview-data-usage)
export namespace tgit::libgit2 {
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


using ::git_cert;
using ::git_cert_t;
using ::git_cert_x509;
using ::git_commit;
using ::git_commit_lookup;
using ::git_commit_committer;
using ::git_commit_message;
using ::git_commit_message_encoding;
using ::git_commit_tree;
using ::git_commit_parentcount;
using ::git_commit_parent;
using ::git_config_add_file_ondisk;
using ::git_config_delete_entry;
using ::git_config_entry;
using ::git_config_foreach_match;
using ::git_config_get_entry;
using ::git_config_level_t;
using ::git_config_new;
using ::git_config_set_bool;
using ::git_config_set_string;
using ::git_credential;
using ::git_credential_userpass_plaintext_new;
using ::git_diff_delta;
using ::git_diff_num_deltas;
using ::git_diff_tree_to_tree;
using ::git_diff_find_options;
using ::git_diff_find_similar;
using ::git_error;
using ::git_error_clear;
using ::git_error_code;
using ::git_error_last;
using ::git_error_set_str;
using ::git_error_t;
using ::git_graph_ahead_behind;
using ::git_indexer_progress;
using ::git_object_t;
using ::git_object_id;
using ::git_patch_get_delta;
using ::git_patch_from_diff;
using ::git_patch_line_stats;
using ::git_repository;
using ::git_reflog_entry_byindex;
using ::git_reflog_entry_committer;
using ::git_reflog_entry_id_new;
using ::git_reflog_entry_message;
using ::git_reflog_entrycount;
using ::git_reflog_read;
using ::git_submodule;
using ::git_submodule_foreach;
using ::git_submodule_path;
using ::git_time_t;

// Overload
int git_config_add_file_ondisk(
	git_config *cfg,
	const std::filesystem::path& path,
	const git_config_level_t level,
	const git_repository *repo,
	const int force
) {
	return ::git_config_add_file_ondisk(cfg, path.string().c_str(), level, repo, force);
}


}

// NOLINTEND(*-pro-type-vararg, bugprone-suspicious-stringview-data-usage)
