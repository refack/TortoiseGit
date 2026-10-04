module;
#include <git2/errors.h>
#include <git2/index.h>
#include <git2/sys/errors.h>


export module libgit2;
import std;

export namespace tgit::libgit2
{
	using ::git_error_clear;   // the function itself, re-exported
	using ::git_error_last;
	using ::git_error;         // types work the same way

	constexpr auto TGIT_INDEX_ENTRY_STAGEMASK = GIT_INDEX_ENTRY_STAGEMASK;
	constexpr auto TGIT_INDEX_ENTRY_SKIP_WORKTREE = GIT_INDEX_ENTRY_SKIP_WORKTREE;
}
