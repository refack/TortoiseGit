# TortoiseGit's patches to git

Three patches, `0004`-`0006`, against **git-for-windows/git at
`7bda9d349370004e1aec464249d784d18e58c001`** (`Merge tag 'v2.56.1'`).

Verified 2026-11-15, in the strong form: applying the three to a pristine
checkout of that commit produces tree `1c245f995e8e5df22df43bfc74f0c40bc6518a13`,
**identical to the tree of the fork branch `tgit-server-three-patches`**. The
series is not an approximation of the fork; it is the fork.

The base is **git-for-windows/git**, not git/git. The Windows compat layer —
`compat/win32/fscache.c` and friends — is not upstream, and TortoiseGit compiles
it.

The numbering starts at `0004` on purpose. `0001`-`0003` were deleted (see
below), and keeping the surviving numbers means every older reference to
"patch 0005" still means the same patch.

## Why this is a series and not a branch

`ext/tgit` pointed at `gitlab.com/tortoisegit/tgit.git`, a long-lived
rebase-on-upstream branch whose contents nothing but the branch itself recorded.
Every version bump was a rebase whose conflicts had to be re-resolved from
memory. The 2.55 bump showed the cost: three separate breakages, one a segfault
that no reading of the commit list would have predicted.

Each patch below is grouped by **what would have to become true for it to be
deleted** — not by file, not by date, because neither answers that question.
`0001`-`0003` are the first time that grouping paid out: one architectural
decision met all three exit conditions at once.

---

## Deleted: 0001-0003, because git moved out of process

Decision 2026-11-15: gitdll stops hosting git in TortoiseGit's processes. The
libgit consumer is now **`tgit-gitserver.exe`**, a real git program: it defines
`main()` the way `common-main.c` does, links `libgit.lib`, and serves **one
working copy per process**, bound at spawn and never re-pointed. `ext\gitdll`
becomes a proxy that keeps the `gitdll.h` ABI and forwards over a pipe.
`git_init()` is the "restart the server" trap door — git keeps no state that is
not cache, so a restart is a complete reset. See `doc\GitServer.md`.

The last series containing them is the fork tag `tgit-six-patches-baseline`
(`b53e2cd095`); the dropped commits are `e32f021b0c` (0001), `c159a9d754`
(0002) and `61ce14921a` (0003).

### 0001 — We are a DLL, not a process

git assumes it owns the process: it may page output, fork, write to stdout, and
exit without unwinding. Inside TortoiseGit's processes all four were wrong, so
this patch added the gitdll exports, pager suppression, the `exit()` →
`vc_exit()` hook, open-handle tracking, scheduled cleanup, critical-section and
tracing init, and chdir-notify teardown.

**Why it could go:** in its own process, git owns the process again. `die()`
ending the server is fine; the proxy sees the pipe close. The one consequence
the server must design around: `show_log()` and friends print commits to
**stdout** again, so the protocol never runs over the server's stdout.

The old note called this "an honest exit condition, unlikely route", on the
assumption that the alternative was shelling out to `git.exe` per call. A
long-lived server per working copy is the route that note did not consider.

### 0002 — One process, many repositories

Everything here existed because gitdll re-pointed a live process at a different
working copy: decoration cache reset, refstore reset/free, environment reset, a
separate libgit environment, UTF-8 environment hardening, accepting
TortoiseGit's environment, closing open pack files, and the 2.55 forward-port of
all of it. It was where every 2.55 rebase conflict landed.

**Why it could go:** its stated exit condition was "spawn per repository
instead of switching". One server per working copy is exactly that. Switching a
working copy is a server restart, not an in-process reset. With 0002 gone,
`compat/mingw.c`'s own `wmain` and UTF-8 environment handling are upstream's
again.

### 0003 — Finding the user's git installation

TortoiseGit is configured with a path to *some* git — Git for Windows, Msys2 or
Cygwin — and gitdll had to read the same system config that git itself would.
The patch rewrote `system_prefix()` to read `HKCU\Software\TortoiseGit\MSysGit`,
probe `mingw64`/`mingw32`, and strip `usr` under the `Msys2Hack` registry value,
plus the `APPDATA` rules and the ≥ 2.47 floor.

**Why it could go:** the old note said "nothing plausible" would delete it,
because the *problem* is TortoiseGit's. The problem still is; the *patch* is not
needed for it. A git process does not need its source edited to be told where
its configuration lives: TortoiseGit already knows the user's git and can say so
when it spawns the server. The registry read inside git, and the Msys2
predicate with it, were the in-process answer to a question a process launcher
answers from outside.

---

## 0004 — MSVC build enablement

Making git's sources compile as a library under MSVC: compile fixes,
`ALTERNATE` colliding with `<wingdi.h>`, variable initialisation, `.tgitconfig`
and icon, and making `RUNTIME_PREFIX` a CMake option (upstream sets it
unconditionally). The port turns `RUNTIME_PREFIX` off, because
`tgit-gitserver.exe` lives in TortoiseGit's `bin\`, not in a git installation,
and a prefix derived from its own path would point at TortoiseGit.

The comment 0004 puts above that option still describes gitdll living inside
`TortoiseGitProc.exe` and cites the deleted 0003. The conclusion (off for us)
still holds; the reason given is out of date. It is left alone here rather than
folded into this refresh — fix it the next time 0004 is re-derived.

**Deletable when:** the port builds through
`contrib/buildsystems/CMakeLists.txt` with no source edits. It now builds
*through* that CMake, so what is left is the code that build still rejects.
Re-derive it against CMake instead of carrying it forward unread.

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
reaches a second setup, and 2.55 is the release that added both flags. The
exposure was 0002's re-pointing; a server that is never re-pointed should not
reach a second setup either, which makes this patch a plain upstream bug fix
with no TortoiseGit caller left.

**Send this to git.** It is `git format-patch` output and needs no adaptation.
Do not fold it into anything else: the whole point is that it leaves this
series.

---

## Refreshing

There is no branch to rebase. To move to a newer git:

1. Bump `REF` in `portfile.cmake`.
2. `vcpkg install`, and read which patch fails.
3. Fix *the patch*, not a source tree — there is no source tree to fix.

If a patch stops applying because upstream did the same thing, delete it. That is
the intended way for this series to shrink, and the reason it is grouped the way
it is.

Regenerate from a fork branch with
`git format-patch --zero-commit -k --start-number 4 <base>..<branch>`:
`--zero-commit` keeps the `From` line stable, and `-k` keeps the subjects free
of a `[PATCH]` prefix, which is the format the files here are in.
