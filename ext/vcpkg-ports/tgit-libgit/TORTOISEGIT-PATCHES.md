# TortoiseGit's patches to git

Six patches against **git-for-windows/git at
`f661476ee4698cf7bfa734b91330048a54d7ee11`** (`Merge tag 'v2.55.0.windows.5'`).

Verified 2026-08-26, and verified in the strong form: applying all six to a
pristine checkout of that commit produces tree `52bd6550ed64d12335f50ca808c88b5c649a5094`,
**identical to the fork tip's tree**. The series is not an approximation of the
fork; it is the fork.

The base is **git-for-windows/git**, not git/git. The Windows compat layer —
`compat/win32/fscache.c` and friends — is not upstream, and TortoiseGit compiles
it.

## Why this is a series and not a branch

`ext/tgit` pointed at `gitlab.com/tortoisegit/tgit.git`, a long-lived
rebase-on-upstream branch whose contents nothing but the branch itself recorded.
Every version bump was a rebase whose conflicts had to be re-resolved from
memory. The 2.55 bump showed the cost: three separate breakages, one a segfault
that no reading of the commit list would have predicted.

Each patch below is grouped by **what would have to become true for it to be
deleted** — not by file, not by date, because neither answers that question.

---

## 0001 — We are a DLL, not a process

git assumes it owns the process: it may page output, fork, write to stdout, and
exit without unwinding. gitdll is loaded *into* TortoiseGit's processes, where
all four are wrong. Covers the gitdll exports, pager suppression, the exit hook,
open-handle tracking, scheduled cleanup, critical-section and tracing init, and
chdir-notify teardown.

**Deletable when:** TortoiseGit shells out to `git.exe` instead of hosting a
library — which loses the reason gitdll exists. Honest exit condition, unlikely
route.

## 0002 — One process, many repositories

The largest bucket, and the one worth attacking. Everything here exists because
gitdll re-points a live process at a different working copy: decoration cache
reset, refstore reset/free, environment reset, a separate libgit environment,
UTF-8 environment hardening, accepting TortoiseGit's environment, closing open
pack files, and the 2.55 forward-port of all of it.

**Deletable when:** **TGitCache spawns per repository instead of switching.**
The pattern exists to keep one cache process entangled with `explorer.exe`,
which was decisive for 2000s-era SVN where every operation was a network round
trip. Git is local and fast; the assumption deserves re-measuring before the
vcpkg port freezes it in.

This is also where *every* 2.55 rebase conflict landed — upstream is actively
consolidating exactly this state into `struct repository`, which is the same
motion that produced the bug in `0006`.

## 0003 — Finding the user's git installation

TortoiseGit is configured with a path to *some* git — Git for Windows, Msys2 or
Cygwin — and has to read the same system config that git itself would, including
the `APPDATA` rules and the ≥ 2.47 floor.

**Deletable when:** nothing plausible. This is genuinely TortoiseGit's problem
and always will be.

## 0004 — MSVC build enablement

Making git's sources compile as a library under MSVC rather than through git's
own Makefile: compile fixes, `ALTERNATE` colliding with `<wingdi.h>`, variable
initialisation, `.tgitconfig` and icon, dropping `.github`.

**Deletable when:** the port builds through
`contrib/buildsystems/CMakeLists.txt`, git's own MSVC-supported CMake build.
**Expect this patch to shrink a lot on contact** — much of it is doing by hand
what that build already does, and the `.github` removal is housekeeping rather
than a code change. Re-derive it against CMake instead of carrying it forward
unread.

## 0005 — Behaviour we deliberately differ on

`--follow` support through gitdll, commit-graph disabled, conversion filters
skipped, mailmap not loaded for log display.

**Deletable when:** partially, now. `--follow` is load-bearing — rename-following
log is the log dialog's headline feature and libgit2's revwalk has no equivalent.
But the commit-graph hunk has said "temporarily" for years and has outlived the
word; re-test whether it can simply be enabled.

## 0006 — Upstreamable

`repo_clear()` frees repository state but leaves `->initialized` and
`->worktree_initialized` set, so a second setup of the same `struct repository`
walks into freed memory: the first `BUG()`s, the second segfaults in
`set_git_work_tree()` on a `FREE_AND_NULL`'d `->worktree`.

Not TortoiseGit behaviour — only TortoiseGit *exposure*, since nothing upstream
reaches a second setup, and 2.55 is the release that added both flags.

**Send this to git.** It is `git format-patch` output and needs no adaptation.
Do not fold it into `0002`, however tempting the subject-matter overlap: the
whole point is that it leaves this series.

---

## Refreshing

There is no branch to rebase. To move to a newer git:

1. Bump `REF` in `portfile.cmake`.
2. `vcpkg install`, and read which patch fails.
3. Fix *the patch*, not a source tree — there is no source tree to fix.

If a patch stops applying because upstream did the same thing, delete it. That is
the intended way for this series to shrink, and the reason it is grouped the way
it is.
