# Backend forks: `UsingLibGit2` / `m_IsUseLibGit2` / `m_IsUseGitDLL`

A map of every place the app chooses a git backend at runtime, so the switches
can be killed (user, 2026-11-14: *"so many `if (UseLibgit2)` forks in the code.
Yuck! Let's try to remember to kill it."*). Keyed by **function**, not line —
lines are hints from 2026-11-14 and will drift.

## Why the switches exist (one paragraph)

They are a 2012–13 staged-rollout feature flag that was never retired.
`UseLibgit2` arrived default-off (`6af32831e`, 2012-01), went default-on
(`25ccab029`, 2012-07); the per-command mask arrived with libgit2 clone
(`75e1c88d0`, 2013-01) and was inverted to an allow-list in `a065fc3a6`
(2013-02, *"Disable using of non mature libgit2 functionality"*). Upstream 2.18
still shipped 10 of the 18 `LIBGIT2_CMD` values **off**. `UsingGitDLL`
(gitdll, 2009, a log-speed hack) is the same idea for the other backend.
The test enum `config { LIBGIT2_ALL, LIBGIT2, LIBGIT, GIT_CLI }` is this
rollout state seen from the tests (`0f4d911ae`, 2015).

## The switches

| switch | registry | default | read in |
| --- | --- | --- | --- |
| `m_IsUseLibGit2` | `UseLibgit2` | on | `CGit::CGit` (Git.ixx) |
| `m_IsUseLibGit2_mask` | `UseLibgit2_mask` | `DEFAULT_USE_LIBGIT2_MASK` = GETONEFILE, GET_COMMIT, FOREACHREF | `CGit::CGit` |
| `LIBGIT2_ONLY_MASK` | — (constexpr) | DIFF, REVERT, MERGE_BASE, DELETETAGBRANCH, ADD, PUSH, CHECK_CLEAN_WT, CHECKCONFLICTS, BRANCH_CONTAINS, GETCONFLICTINFO | `CGit::UsingLibGit2` |
| `m_IsUseGitDLL` | `UsingGitDLL` | on upstream; **lost to off** in the wil-registry migration (`c240857ba`/`9425ce1f8`), restored by hand 2026-11-14 | `CGit::CGit` |

`UsingLibGit2(cmd)` = `cmd ∈ LIBGIT2_ONLY_MASK || (m_IsUseLibGit2 && cmd ∈ mask)`.
Five of the `LIBGIT2_ONLY_MASK` entries (DIFF, REVERT, PUSH, CHECK_CLEAN_WT,
BRANCH_CONTAINS) were **off by default upstream** — `2d61fbb07` promoted them;
PUSH via libgit2 does not run the repository's `pre-push` hook.

## The forks, by shape

The shape decides how a fork dies, so the map is grouped by shape.

### A. libgit2 ↔ git.exe — two implementations of one function

Collapse: keep the libgit2 branch, delete the git.exe branch (the CLI stays
available to the *user*, not as a twin).

| function | file | gate | git.exe twin |
| --- | --- | --- | --- |
| `CGit::GetHash` | Git.ixx ~2338 | `m_IsUseLibGit2` | `rev-parse --verify` |
| `CGit::GetTagList` | Git.ixx ~2501 | `m_IsUseLibGit2` | `tag -l` |
| `CGit::IsBranchTagNameUnique` | Git.ixx ~2536 | `m_IsUseLibGit2` | `show-ref --tags --heads` |
| `CGit::BranchTagExists` | Git.ixx ~2566 | `m_IsUseLibGit2` | `show-ref --heads/--tags` |
| `CGit::GetBranchList` | Git.ixx ~2637 | `m_IsUseLibGit2` | `branch --no-color` |
| `CGit::GetRemoteList` | Git.ixx ~2813 | `m_IsUseLibGit2` | `remote` |
| `CGit::GetRefList` | Git.ixx ~2950 | `m_IsUseLibGit2` | `git_show_ref` → **`git.exe show-ref -d`** (name says gitdll; it is not) |
| `CGit::GetMapHashToFriendName` | Git.ixx ~2988 | `m_IsUseLibGit2` | `git_show_ref` (git.exe) |
| `CGit::GetTagInfo` | Git.ixx ~3792 | `m_IsUseLibGit2` | `cat-file tag` |
| `CGit::GetRemoteRefs` | Git.ixx ~2850 | `UsingLibGit2(FETCH)` | `ls-remote` — pinned by FETCH's dialog fork (D) |
| `GetGitRevRefMap` | GitRev.ixx ~156 | `UsingLibGit2(FOREACHREF) && mergefilter <= 0` | `for-each-ref` — **not a fallback**: git.exe is primary when filtering on merge status (libgit2's `git_graph_descendant_of` is slow) |

### B. libgit2 ↔ gitdll

Collapse needs the gitdll decision first (keep gitdll, or retire it).

| function | file | gate | other branch |
| --- | --- | --- | --- |
| `CGit::GetOneFile` | Git.ixx ~3292 | `UsingLibGit2(GETONEFILE) \|\| !m_IsUseGitDLL` | gitdll `git_checkout_file` — runs configured smudge filters, which `git_blob_filter` does not (LFS) |
| `GitRev::GetCommit` | GitRev.ixx ~610 | `UsingLibGit2(GET_COMMIT)` | gitdll `git_get_commit_from_hash` |
| `GitRevLoglist::SafeFetchFullInfo` | GitRevLoglistCore.ixx ~238 | `UsingLibGit2(LOGLISTDIFF)` | gitdll diff |
| `GitRevLoglist::SafeGetSimpleList` | GitRevLoglistCore.ixx ~441 | `UsingLibGit2(LOGLISTDIFF)` | gitdll diff |
| `GetRefLog` | GitRevLoglistCore.ixx ~551/585 | `m_IsUseLibGit2`, then `m_IsUseGitDLL` | **three-way**: libgit2 → gitdll `git_for_each_reflog_ent` → `git.exe reflog show` |

### C. libgit2 ↔ nothing — a feature that only exists with libgit2

Collapse: delete the guard; the feature becomes unconditional.

| function | file | without libgit2 |
| --- | --- | --- |
| `CGit::GetSubmoduleHash` | Git.ixx ~2405 | returns 0 with no hash |
| `CGitTagCompareList::Fill` | GitTagCompareList.ixx ~148 | "Only available with libgit2 enabled" |
| `CSyncDlg::OnInitDialog` | SyncDlg.cpp ~924 | no ref-list tab |
| `CSyncDlg::FillNewRefMap` | SyncDlg.cpp ~1365 | no new-ref map |
| `CWorktreeListDlg::GetWorktreeNames` | WorktreeListDlg.cpp ~112 | returns -1 |
| `Command::CheckRepo` | Command.cpp ~439 | repository not validated |
| `CRebaseDlg::StartRebase` / `FinishRebase` / `OnBnClickedAbort` (×2) | RebaseDlg.cpp ~1059, ~1155, ~2618, ~2679 | **no reflog entries written** for the rebase |

### D. Whole-dialog / whole-flow selection

Collapse: pick one dialog per operation (the bigger change `2d61fbb07` deferred).

| op | sites |
| --- | --- |
| CLONE | `CloneCommand::Execute` (~148), `CloneProgressCommand::Run` (~28) |
| FETCH | `CAppUtils::DoFetch` (AppUtils.Workflow ~1225), `CSyncDlg::OnBnClickedButtonPull` (~407), `CSyncDlg::FetchComplete` (~529), `FetchProgressCommand::Run` (~27), + `GetRemoteRefs` (A) |
| RESET | `CAppUtils::Reset` (AppUtils.Workflow ~714), `ResetProgressCommand::Run` (~28) |
| COMMIT_UPDATE_INDEX | `CCommitDlg::PrepareIndexForCommitWithoutStagingSupport` (~1088) |
| GETONEFILE (cat) | `CatCommand::Execute` (~30): libgit2 blob write, else `g_Git.GetOneFile` (B), then git.exe `cat-file`/`show` for non-blobs |

### E. gitdll ↔ git.exe

Collapse with the gitdll decision; each has a libgit2 equivalent available.

| function | file | note |
| --- | --- | --- |
| `CGit::WriteConfigValue` | Git.ixx ~984 | gitdll config write vs `git.exe config` |
| `CGit::ReadConfigValue` | Git.ixx ~1988 | gitdll config read vs `git.exe config` |
| `CGit::RefreshGitIndex` | Git.ixx ~1194 | gitdll `git_update_index` **unless the repo has LFS** (issue #3220: gitdll fails running the filter) vs `update-index --refresh` |

### F. Forcing gitdll off — workarounds, not forks

gitdll is process-global (one repository latched per process, cf. `CheckAndInitDll`),
so a second `CGit` aimed at a submodule or subdirectory must not use it. These
sites set `m_IsUseGitDLL = false` on a local `CGit` (or flip `g_Git`'s and
restore it). They disappear with gitdll, or with per-repository libgit2 handles.

| function | file | why |
| --- | --- | --- |
| `CAppUtils::Export` | AppUtils.Workflow ~351 | local `CGit` for a subdirectory |
| `CAppUtils::ConflictEdit` | AppUtils.WorkingTree ~297, ~310 | `subgit` for a submodule |
| `CCommitDlg::OnOK` | CommitDlg.cpp ~699 | `subgit` for a dirty-submodule check |
| `CGitDiff::SubmoduleDiffNull` / `SubmoduleDiff` | GitDiff.cpp ~67, ~261 | `subgit` for a submodule |
| `ResolveProgressCommand::Run` | ~212 | `subgit` for a submodule |
| `CSettingGitCredential::Save` / `DeleteOtherKeys` / `SaveSimpleCredential` | SettingGitCredential.ixx ~686–855 | flips **`g_Git`** off and back: *"workaround gitdll bug // TODO: switch to libgit2"* |

### G. Plumbing — goes last, in one move

`CGit` members and registry reads (Git.ixx ~765–782), `UsingLibGit2` (~892),
`GetGitLastErr(msg, cmd)` choosing libgit2's vs git.exe's error (~1072, 36 call
sites), `SettingsAdvanced.cpp` `UseLibgit2` entry, the docs' `UseLibgit2`
advanced-setting entry, and in the tests the `config` enum, `CBasicGitFixture::SetUp`'s
switch (RepositoryFixtures.ixx), and the per-backend instantiations in GitTest.cpp /
libgitTest.cpp.

## Order that keeps every step buildable

1. **C** — delete guards; no behaviour lost with libgit2 on (the default).
2. **A** — delete git.exe twins, function by function (GetRemoteRefs waits for D/FETCH; GetGitRevRefMap keeps git.exe as its merge-filter implementation, without a switch).
3. **Decide gitdll**, then **B**, **E**, **F** together.
4. **D** — one dialog per operation.
5. **G** — the switches, the mask, `GetGitLastErr`'s parameter, the test matrix.
