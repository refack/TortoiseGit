# TortoiseGit's patches to git

Rendered 2026-08-26 from the `ext/tgit` fork, against **git-for-windows/git at
`f661476ee4698cf7bfa734b91330048a54d7ee11`** (`Merge tag 'v2.55.0.windows.5'`).
All 31 verified to apply cleanly to a pristine checkout of that commit.

**This series replaces the fork.** `ext/tgit` pointed at
`gitlab.com/tortoisegit/tgit.git`, a long-lived rebase-on-upstream branch whose
contents nothing but the fork itself recorded. A patch series says the same
thing in a form that can be read, reviewed, reordered and — the point — *deleted
one file at a time*. Note the base is **git-for-windows/git**, not git/git: the
Windows compat layer (`compat/win32/fscache.c` and friends) is not upstream and
TortoiseGit compiles it.

## Why each patch exists, and what would let it go

Grouped by what has to be true for the patch to become unnecessary. Nothing here
is grouped by file or by date, because neither tells you whether you can drop it.

### A. We are a DLL, not a process (9)

git assumes it owns the process: it may page output, fork, write to stdout, and
exit without unwinding. gitdll is loaded into TortoiseGit's processes, where all
four are wrong.

    0002-Define-exports-of-gitdll
    0003-Do-not-use-pager-also-caused-crashes-in-gitdll.dll-i
    0004-Add-exit-hook-for-gitdll
    0005-Track-all-open-file-handles
    0012-Do-not-print-commits-on-STDOUT
    0013-Do-not-try-to-fork-or-start-sub-processes-exit-early
    0014-Allow-to-schedule-cleanup-tasks
    0018-Initialize-critical-sections-and-tracing
    0020-chdir-notify-needs-cleanup

*Goes away if:* TortoiseGit shells out to `git.exe` instead of hosting a library.
That is a large change and loses the reason gitdll exists, but it is the honest
exit condition.

### B. One process, many repositories (8)

Everything here exists because gitdll re-points a live process at a different
working copy. git never does: it sets up one repository and exits.

    0008-Reset-decoration-cache
    0009-Allow-to-reset-and-free-refstore
    0010-Allow-to-reset-git-environment
    0016-Use-a-separate-environment-in-libgit
    0017-UTF-8-environment-be-a-little-bit-more-defensive
    0026-Allow-to-accept-separate-environment-from-TortoiseGi
    0028-Allow-to-close-open-pack-files
    0030-Carry-the-TortoiseGit-repository-switch-hooks-forwar

*Goes away if:* **TGitCache spawns per repository instead of switching.** This is
the bucket most worth attacking. It exists to keep one cache process entangled
with `explorer.exe`, which was decisive for 2000s-era SVN where every operation
was a network round trip. Git is local and fast; the assumption deserves
re-measuring. See CLAUDE.md, "The git 2.55 rebase".

This bucket is also where every 2.55 rebase conflict landed, because upstream is
actively consolidating exactly this state into `struct repository`.

### C. Finding the user's git installation (4)

TortoiseGit is configured with a path to *some* git, which may be Git for
Windows, Msys2 or Cygwin, and has to read the same system config that git would.

    0007-Use-Git-system-config-based-on-the-configured-git-pa
    0015-Add-full-support-for-Msys2-Git-and-Cygwin-Git-config
    0027-Restrict-APPDATA-config-to-Git-for-Windows-2.46-envi
    0029-Drop-support-for-Git-2.47

*Goes away if:* nothing plausible. This is genuinely TortoiseGit's problem.

### D. Build enablement (5)

Making git's sources compile as a library in this build, rather than through
git's own Makefile.

    0001-Make-libgit-generally-compile
    0011-Add-.tgitconfig-and-icon
    0019-Initialize-variables
    0021-Undefine-ALTERNATE-as-it-is-already-defined-in-wingd
    0023-Drop-.github-folder

*Goes away if:* the port builds through `contrib/buildsystems/CMakeLists.txt`,
git's own MSVC-supported CMake build. Several of these are likely already
unnecessary there — `0023` in particular is housekeeping, not a code change.
**Check each against the CMake build before carrying it forward.**

### E. Behaviour we deliberately differ on (4)

    0006-make-follow-work-with-gitdll.dll
    0022-Temporarily-disable-the-use-of-commit-graph-file
    0024-Skip-conversion-filters
    0025-No-need-to-load-mailmap-for-showing-the-log

`0006` is load-bearing: `--follow` rename-following log is the log dialog's
headline feature and libgit2's revwalk has no equivalent. `0022` says
"temporarily" and has outlived that word; worth re-testing whether commit-graph
can simply be enabled.

### F. Upstreamable (1)

    0031-repository-make-repo_clear-reset-the-flags-that-guar

`repo_clear()` frees repository state but leaves `->initialized` and
`->worktree_initialized` set, so a second setup of the same `struct repository`
walks into freed memory: the first `BUG()`s, the second segfaults in
`set_git_work_tree()` on a `FREE_AND_NULL`'d `->worktree`. Not TortoiseGit
behaviour — only TortoiseGit *exposure*, since nothing upstream reaches a second
setup. **Send this to git; do not carry it.** `git format-patch` output is ready
as-is.

## Refreshing the series

The fork is gone, so there is no branch to rebase. To move to a newer git:

1. Bump the `REF` in `portfile.cmake`.
2. `vcpkg install` and read which patch fails.
3. Fix that patch, not the source tree — there is no source tree to fix.

If a patch stops applying because upstream did the same thing, delete it. That
is the intended way for this series to shrink.
