module;
#include <git2.h>

export module SmartLibgit2;
import std;
import wil;
import wstr;
import invarients;

// Owners for libgit2 handles. Every one is wil's, so there is no class here to
// get wrong - the hand-rolled CSmartLibgit2Ref re-exposed its conversion
// operator through a using-declaration that MSVC refuses across a module
// boundary (C2248), which is how it broke.
//
// Spelling, old -> new:
//   CAutoX x;                   wil::unique_git_x x;
//   x.GetPointer()              x.put()            (frees first, as before)
//   x.Detach() / x.Free()       x.release() / x.reset()
//   x.IsValid(), (bool)x        x.is_valid(), (bool)x   (bool is explicit now)
//   passing x as git_x*         x.get()            (no implicit conversion)
//   a.Swap(b)                   a.swap(b)
//   CAutoBuf buf; f(buf)        wil::unique_git_buf buf; f(&buf)   (it IS a git_buf)
//   CAutoCommit(std::move(obj)) wil::unique_git_commit{ reinterpret_cast<git_commit*>(obj.release()) }
//   CAutoConfig(repo)           git_repository_config(cfg.put(), repo.get())
//   CAutoConfig(true), New()    git_config_new(cfg.put())
//   CAutoRepository(path)       wil::OpenGitRepository(path)   (latches the object format)
export namespace wil
{
template <typename T, void Free(T*)>
using unique_git = unique_any<T*, decltype(Free), Free>;

using unique_git_object           = unique_git<git_object,             git_object_free>;
using unique_git_commit           = unique_git<git_commit,             git_commit_free>;
using unique_git_tree             = unique_git<git_tree,               git_tree_free>;
using unique_git_blob             = unique_git<git_blob,               git_blob_free>;
using unique_git_tag              = unique_git<git_tag,                git_tag_free>;
using unique_git_tree_entry       = unique_git<git_tree_entry,         git_tree_entry_free>;
using unique_git_reference        = unique_git<git_reference,          git_reference_free>;
using unique_git_repository       = unique_git<git_repository,         git_repository_free>;
using unique_git_config           = unique_git<git_config,             git_config_free>;
using unique_git_index            = unique_git<git_index,              git_index_free>;
using unique_git_submodule        = unique_git<git_submodule,          git_submodule_free>;
using unique_git_diff             = unique_git<git_diff,               git_diff_free>;
using unique_git_patch            = unique_git<git_patch,              git_patch_free>;
using unique_git_diff_stats       = unique_git<git_diff_stats,         git_diff_stats_free>;
using unique_git_remote           = unique_git<git_remote,             git_remote_free>;
using unique_git_reflog           = unique_git<git_reflog,             git_reflog_free>;
using unique_git_revwalk          = unique_git<git_revwalk,            git_revwalk_free>;
using unique_git_branch_iterator  = unique_git<git_branch_iterator,    git_branch_iterator_free>;
using unique_git_reference_iterator = unique_git<git_reference_iterator, git_reference_iterator_free>;
using unique_git_describe_result  = unique_git<git_describe_result,    git_describe_result_free>;
using unique_git_status_list      = unique_git<git_status_list,        git_status_list_free>;
using unique_git_note             = unique_git<git_note,               git_note_free>;
using unique_git_signature        = unique_git<git_signature,          git_signature_free>;
using unique_git_mailmap          = unique_git<git_mailmap,            git_mailmap_free>;
using unique_git_worktree         = unique_git<git_worktree,           git_worktree_free>;

// Structs libgit2 fills in place and disposes of by address; unique_struct
// derives from the struct, so &buf is a git_buf* and buf.ptr just works.
using unique_git_buf      = unique_struct<git_buf,      decltype(&git_buf_dispose),      git_buf_dispose>;
using unique_git_strarray = unique_struct<git_strarray, decltype(&git_strarray_dispose), git_strarray_dispose>;

/// The one thing CAutoRepository did that is not ownership: TGitCache and the
/// shell extension never initialize gitdll, so opening a repository here is the
/// only place their object format is ever established. Check the result with
/// is_valid(); on failure it is empty and git_error_last() says why.
// unique_git_repository OpenGitRepository(const char* gitDirUtf8)
// {
// 	unique_git_repository repo;
// 	if (!git_repository_open(repo.put(), gitDirUtf8))
// 		LatchObjectFormat(git_repository_oid_type(repo.get()));
// 	return repo;
// }
//
// unique_git_repository OpenGitRepository(const std::wstring_view gitDir)
// {
// 	return OpenGitRepository(CUnicodeUtils::StdGetUTF8(gitDir).c_str());
// }
//
// /// What CAutoBuf's operator std::string_view was.
// std::string_view View(const git_buf& buf)
// {
// 	return { buf.ptr, buf.size };
// }
//
// /// What CAutoStrArray::AppendTo was.
// void AppendTo(const git_strarray& array, std::vector<std::wstring>& list)
// {
// 	for (size_t i = 0; i < array.count; ++i)
// 		list.push_back(CUnicodeUtils::StdGetUnicode(array.strings[i]));
// }
}
