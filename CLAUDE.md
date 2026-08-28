# TortoiseGit — working plan

The live document: goals, the design decisions that still govern, cross-cutting
rules, and what is still to do. Begun 2026-08-03 as a plan for two intertwined
changes (experimental SHA256 object ids, and de-fragmenting the build); it has
since grown a third strand, making `src\` cohesive.

**History lives in [`doc/Remodel2026.md`](doc/Remodel2026.md)** — the commit
trail, per-phase status notes, the superseded architecture, and the full account
of every lesson stated here in one line. Consult it before re-deriving anything;
most rules below are the residue of a wrong turn recorded there.

## Where this stands (2026-08-27)

- **SHA256**: Phases 0–3 complete. Phase 4 works end to end for log, diff and
  commit — a SHA256 repository opens, lists full 64-character ids, diffs against
  the working copy, and commits. Phase 5 (MSIX) has not started.
- **The tree**: `.gitmodules` is deleted, `ext\build\` is gone, and `ext\` is
  three first-party directories — `gitdll`, `TortoiseOverlays`, `vcpkg-ports`.
  Everything third-party comes from vcpkg. Eight applications or dependencies
  were removed outright; the Revision Graph was considered and **kept**.
- **`src\`**: **all six spanning-base string conversions are done.** `Git.h` was
  the design one and its six decisions have each landed; what is left there is
  consumer migration against a settled API, not design.
- **Tests**: 597/597, unattended, about 40 seconds via `test\Run-Tests.ps1`
  (125s single-process). 590 → 597 is +2 `Decode`, +1 `CEnvironmentBlockLayout`,
  +4 `SerializeArgv`.

## Goals

- [x] Add experimental SHA256 object-id support to TortoiseGit's libgit2 use.
- [x] De-fragment the build. The hand-maintained `ext\build\*.vcxproj` files are
  gone; third-party code is a vcpkg port, a port overlay, or nothing. Patches to
  third-party libraries go in an overlay or get refactored out — never as
  vendored source edits.
- [x] Drop all Win32 (x86) build targets. *(One residue: the WiX x86
  conditionals, see "Deferred sweeps".)*
- [x] Retire the `ext\*` git submodules.
- [ ] **One MFC exe/dll, one ATL exe/dll, one static lib** (user direction,
  2026-08-26). See "Making `src\` cohesive".
- [ ] Prefer delegating to git over reimplementing it. TortoiseGit's value is the
  Explorer integration, not a second implementation of what `git.exe` already
  does well. Where a built-in duplicates a git feature the user has already
  configured, hand the work to git and keep a primitive floor (`notepad.exe`) so
  the chain always terminates.

## Design decisions that still govern

**"This whole solution is one meta-application. It does not export any valuable
API."** (user, 2026-08-26.) Nothing here is consumed by anyone else, so no
signature is frozen by compatibility. That is what makes the string migration
tractable at all.

**"We are in 2026. Move ATL / CString to STL."** (user, 2026-08-26.)
`std::wstring`, not `std::string`, for the first pass: a `CString` →
`std::wstring` change is *syntactic* and the compiler verifies every site, while
a `CString` → UTF-8 `std::string` change makes every site an *encoding* decision,
and encoding bugs are silent rather than red. UTF-8 at the git/libgit2 boundary
is worth doing separately, after the ABI is unblocked.

**Order by new degrees of freedom, not by churn** (user, 2026-08-26: *"those are
all compiler validated. Big if you measure churn, tiny if you measure new DoF."*)
A type change makes every stale call site a compile error, so 2,095 edits
carrying one decision is cheap and 50 edits carrying 50 decisions is not.

**The recurring test: what does this actually add over the thing Windows or git
already ships?** For TortoiseGitMerge the answer was "nothing git's difftool
doesn't do"; for TortoiseGitPlink it was "a GUI prompt", which `SshAskPass.exe`
already provides for OpenSSH; for Hunspell it was a language list Windows'
own checker mostly covers — and *"if Windows is not usable (no lang pack) we
can't save the day by spell checking commit messages"*. Ask it before adding, too.

**Delegate first, remove second.** Every application retired so far landed its
fallback in one commit and was deleted in the next, so there was never a window
where the feature was simply missing. `notepad.exe` is the floor by design, not
by default: it is always present, and users routinely redirect it through Image
File Execution Options. **Do not "improve" that to TortoiseGitUDiff** — the more
generic completion is the point.

**Object format is process-level state, not per-hash.** TortoiseGit runs one
process per working copy, so `g_gitObjectFormat` is latched once and
`GIT_HASH_SIZE` is the runtime `GitHashSize()`; fixed buffers use the
compile-time `GIT_HASH_MAX_SIZE`. Sizes reuse libgit2's `GIT_OID_*` macros —
**do not add duplicate size macros.** Hashes stay **hex** (64 chars), the
universal convention across git, GitHub and GitLab — **never base64**.

**`#include "../"` is a build dependency the build system cannot see.** Communal
sources and headers are configured through `AdditionalIncludeDirectories`, in
metadata. Relatedly (user): *"a code path that breaks abstraction so blatantly is
broken by definition; any greenness is an unstable equilibrium."*

**The libgit2 patch lives in `ext\vcpkg-ports\libgit2\`**, as `.diff` files
listed in the portfile's `PATCHES` block. The old answer — root-level
`ext\libgit2-*.patch` applied by a `git am --3way` loop — is gone with the
submodule. **Do not reintroduce it**: a `PATCHES` list is ordered by
construction, a shell glob is ordered by whatever the filenames sort to, and that
difference was a live defect for years.

**A port emits its flags into a header, so consumers cannot disagree with the
library they link.** `git2\experimental.h` is generated by CMake and carries
`GIT_EXPERIMENTAL_SHA256` itself, which is why the old "six unflagged consumers"
ABI hazard dissolved rather than being fixed. **Do not go re-flag those
vcxprojs — there is nothing to flag.**

## Cross-cutting concerns

### The CString ABI wall

`CString` is **not one type**. MFC-Dynamic projects get
`CStringT<wchar_t, StrTraitMFC_DLL<...>>`; ATL-only projects get
`CStringT<wchar_t, StrTraitATL<...>>`. Same source file, different mangled name —
verified with `dumpbin /SYMBOLS` on the *same* file compiled by both:

```
TortoiseGitProc  ?GetWinPathString@CTGitPath@@QEBAAEBV?$CStringT@_WV?$StrTraitMFC_DLL@...
TortoiseShell    ?GetWinPathString@CTGitPath@@QEBAAEBV?$CStringT@_WV?$StrTraitATL@...
```

So **one static library cannot serve both flavors while `CString` appears in its
headers** — not as a parameter, not as a return, not as a member. This is not a
style preference; it is a link error waiting for whoever tries the merge without
checking, and it is why "move to STL" is the unlock rather than a tidy-up.

### Four failure modes of a type migration — every one of which compiled

Learned converting `CTGitPath` and `CPathUtils`. Re-read before converting `Git.h`:

| # | shape | why it is silent |
| --- | --- | --- |
| 1 | `a.c_str() == b.c_str()` | both sides `std::wstring` → **pointer** compare. Sweeps must skip lines containing `==` or `!=`. |
| 2 | `x.c_str() + L'.'` | `const wchar_t* + wchar_t` is **pointer arithmetic**, not concatenation. `GetMergeTempFile` was silently returning the path tail with 46 characters removed. |
| 3 | `SetFromGit(cstring, &oldPath)` | **re-bound to a different overload.** A CString can no longer reach the `wstring_view` overload, and pointer→`bool` is a *standard* conversion, so it fell through to `SetFromGit(const wchar_t*, bool)` and read the out-parameter as a directory flag. No error, and no diff to review — the line never changed, only the overload set around it. |
| 4 | `return MAKEINTRESOURCE(IDS_…)` | `CStringT`'s constructor checks `IS_INTRESOURCE` and calls `LoadString`; **`std::wstring` hands the pseudo-pointer to `wcslen`**. Compiled, linked, passed 587 tests, survived an adversarial diff review, then crashed painting the first Action cell. |

(3) and (4) share a moral worth stating plainly: **changing a type does not only
break call sites loudly.** It can re-bind a call to a different overload, or
change what a constructor *means*, and in both cases no line of the diff changed,
so no review can see it. Only the test suite caught (3); only running the
program caught (4). **`std::format` is the fix for the whole of (2)** — each
argument is formatted independently, so there is no `operator+` to resolve, which
is why `std::formatter<CStringT>` exists in `WideString.h`.

**Widening an overload set needs a constraint, not a new parameter type.** Adding
`CombinePath(std::wstring_view)` beside the `CString` one makes every existing
`CombinePath(L"literal")` **ambiguous** — a `const wchar_t*` reaches both by one
user-defined conversion. Prefer *replacing* the signature; where an overload
must be added, constrain it with
`requires std::same_as<std::remove_cvref_t<T>, std::wstring>`, which never enters
overload resolution for a literal.

**Bridge visibly.** A call site still holding a `CString` says so, with
`tgit::wstr::View(x)` for a view parameter and `std::wstring(x)` (direct-init —
copy-init would need two user-defined conversions) for an owning one. Making the
core's parameters `LPCWSTR` instead would erase most of those edits and was
rejected for exactly that reason: a CString converts to `LPCWSTR` implicitly, so
the boundary would compile silently and nothing would ever pressure it to move.

**Do not remove the `#define CSTRING_AVAILABLE` lines** from
`src\TGitCache\stdafx.h`, `src\TortoiseShell\stdafx.h` and `test\Cache\stdafx.h`.
`StringUtils.h`, `CmdLineParser.h`, `SmartLibgit2Ref.h`, `UnicodeUtils.h` and
`WideString.h` still gate on it; only PathUtils' own `#ifdef` died.

### A green build proves less than it looks like it proves

- It cannot see strings or metadata. After deleting a component, grep for its
  name across sources, **resource scripts, `.wxi`/`.wxs`, `.registry` files,
  `.rc`/`.rc2` and CI YAML** — then run the thing. One removal left behind two
  `CreateProcess` calls naming a deleted exe, a CI target list, `.filters`
  entries and a Settings radio button, all invisible to a compiler.
- **A grep over a project's own directory is not a consumer sweep.** A project
  compiles files from elsewhere: `grep CPathUtils:: src\TortoiseUDiff` returns
  nothing, yet it consumes `GetVersionFromFile` through `DarkModeHelper`,
  `LangDll` and `Theme`, which live in `src\Utils`. Sweep the files a project
  **compiles**, not the files it contains.
- **Configurations differ in what they contain.** `TortoiseLangs`, `TortoisePot`
  and the WiX projects are Release-only. **Build Release, not just Debug, before
  calling a removal done** — eight commits of "full-solution Debug/x64 clean"
  once hid a Makefile naming two deleted applications.
- **A migrated dependency that starts failing is usually reporting a defect that
  was always there.** vcpkg's Detours defines `DETOUR_DEBUG`, so an unbalanced
  hook that had silently returned an unread error started calling
  `DETOUR_BREAK()`. Suspect the latent defect, not the migration.
- **When a parameter goes unreferenced after a removal, ask what it used to
  guarantee** before deleting it. `bDeleteBaseTheirsMineOnClose` looked dead and
  was a temp-file leak.
- **Do not label a failure "pre-existing" without isolating the whole feature.**
  29 failures were described that way on the strength of a revert that backed out
  a single commit rather than the whole stack; 16 of them turned out to be caused
  by this work. A revert proves only what it reverts.

### Validation gates

Set 2026-08-09, per user direction. Not every target carries equal weight:

- **`TortoiseGitProc.exe` is the only target that must build *and* pass a runtime
  smoke test** (opens a SHA1 repo and a SHA256 repo without crashing) before a
  milestone counts as done.
  *Scripting that gate:* `TortoiseGitProc.exe` opens a GUI window the user might browse and click "Ok" so it exits with 0. That's a smoke test pass.
- **Everything else is "nice to have": the bar is a clean build, not a runtime
  check.** If one breaks in a way that isn't a quick fix, it can lag a milestone
  without blocking the others.
- **WiX packaging is out of scope** — the WiX v3 toolchain is not installed here,
  so `WixSetup.wixproj` and the 38 `Languages\Lang_*.wixproj` MSI projects fail on
  `WixTargetsPath`. That is 38 pre-existing Release errors; not a regression
  signal.
- **The Languages build is *not* assumed working** — that assumption was wrong
  once and cost eight commits.
- **The suite must not shrink silently.** 590 today; account for every move, in
  both directions. A suite that shrinks looks exactly like a suite that got
  greener.
- **`afxwin1.inl(24) : Assertion failed!` in the test output is signal, not
  noise** (user, 2026-08-27: *"It means magic was invoked in an unaccounted
  (real-not-mocked) code path"*). Fifteen come from
  `CTGitPath.GetActionNameDoesNotDereferenceAResourceId`, which calls the real
  `LoadString` path with no `CWinApp` to supply a resource handle — precisely the
  path that was broken. **Do not silence it.**

### Build and tooling

- Build through `Build-Nice.ps1` (caps `/m:8`, BelowNormal). Never bare `/m`.
- Run the suite through **`test\Run-Tests.ps1`** — 125s single-process, ~40s
  across 8. The suite has **no cross-process interference** (checked: 597/597
  with real process isolation), but gtest's own `GTEST_TOTAL_SHARDS` distributes
  round-robin *by index*, so it balances test counts rather than time and gets
  only 2×. The script bin-packs by measured duration instead, caching timings
  next to `Tests.exe`; the first run is round-robin, every run after is balanced.
  **The floor is one test**: `GetWorkingTreeChanges/3` (`LIBGIT2_ALL`) alone is
  35s — see "Open risk areas".
- vcpkg root is `E:\.vcpkg-clion`. `vcpkg install --triplet x64-windows-static-md`
  is the restore step; `TortoiseGit.vcpkg-base.props` carries paths only and
  `TortoiseGit.vcpkg.props` adds the libgit2 bundle, so a single-package consumer
  is not forced to link the rest. **`libgit.lib` and `git2.lib` must never share
  a link line** — two implementations of odb/refs/packfiles, first-match
  collisions.
- **ARM64 is unverified** — this machine has no `Hostx64\arm64` cross-compiler.
  Best-effort, not a blocker.
- **clangd works here, but only on `.cpp` files, and only with background
  indexing off** (`.clangd`). Indexing ~590 entries measured 7.9 GB and ~55
  CPU-minutes *without finishing*; `Index: Background: Skip` makes the same
  session 0.76 GB and 5 CPU-seconds. What is lost is project-wide
  `findReferences`/`workspaceSymbol`, which grep covers here. Two defects it
  makes visible for free: `src\Git\TGitPath.cpp` has six entries in
  `compile_commands.json`, one per consuming project, so hovering a signature
  shows whichever flavor's `CString` came first — the ABI split without
  `dumpbin`; and opening a `src\Git` or `src\Utils` **header** alone leaves
  `CString` undefined, which is the not-self-contained-TU defect, not a clangd
  bug. `ms2cc .\src\TortoiseGit.sln --configuration=Debug --platform=x64`
  regenerates the database after a vcxproj change.
- **sccache is shelved**, switch left in the tree and off by default: precompiled
  headers are load-bearing for include correctness here, and sccache cannot cache
  TUs that consume a PCH.
- **`appveyor.yml` is stale and deliberately not maintained** — everyone builds on
  GitHub Actions now. Whoever writes that workflow should treat it as a stale
  reference, not a spec.

## Still to do

### Phase 4 — App-level SHA256 UX (remaining)

Log, diff and commit work end to end. Left:

- `GIT_REV_ZERO_C` is deliberately still SHA1-width and narrow — it is GitWCRev's
  unborn-HEAD output, a separate question from the wide sentinel.
- Mark the feature experimental in the UI.
- Exit: log, status, commit, blame and diff all work end-to-end on a SHA256 repo.

### Phase 5 — Switch the WiX packager from MSI to MSIX

*(Added 2026-08-04, per user direction.)* Replace `WiXSetup.wixproj`'s MSI output
with an MSIX package. **Not a trivial packer swap:**

- The pinned WiX v3 toolchain does not emit MSIX at all — that needs WiX v4/v5's
  MSIX authoring or a separate `makeappx` step, i.e. a real toolchain migration.
- `TortoiseGIT.wxs` leans on classic MSI mechanics with no MSIX equivalent:
  elevated `CustomAction` DLL entry points for shell-extension COM registration,
  `RegisterSparsePackage`/`UnregisterSparsePackage` for the Windows 11 context
  menu, `RestartExplorer`, and per-machine `HKLM`/`HKCU` `RegistrySearch`. MSIX
  installs run in a constrained context. **Audit
  `TortoiseGIT.wxs` custom-action-by-custom-action; don't assume parity.**
- Two inputs already decided: **the new installer should perform a full uninstall
  of the old version rather than an in-place upgrade** (user, 2026-08-25), and it
  is where the `.ppk`→OpenSSH migration guidance belongs — surfaced where the
  user is already being told what changed. The "Choose SSH Client" page should
  collapse here rather than being rewired now.
- Exit: installs/uninstalls/upgrades cleanly, the shell extension registers and
  works under MSIX's packaged-app model, nothing silently dropped.

### Making `src\` cohesive — the three-binary target

Target: **one MFC exe/dll, one ATL exe/dll, one static lib.** Three boundaries
are proven and must not be crossed:

| | why |
| --- | --- |
| `gitdll.dll` stays a DLL | `libgit.lib` and `git2.lib` must never share a link line. |
| `TortoiseGitStub.dll` stays separate | static CRT by design, two source files, zero dependencies. It is loaded into `explorer.exe`; that hygiene is the whole point. |
| `src\libgit2\TGitLibgit2` stays C | compiled as C against libgit2's private headers, `CharacterSet=NotSet`. It can join the static lib, but not by being made C++ or Unicode. |

**Phase A — purge `CString` from the flavor-neutral core.** Only what the ATL
side *and* the MFC side both compile has to be neutral — the union of the
`Git`/`Utils` sources listed by `TortoiseShell`, `TGitCache` and `GitWCRev`, which
is **20 files**:

> `CmdLineParser`, `DebugOutput`, `Git`, `GitAdminDir`, `GitFolderStatus`,
> `GitIndex`, `GitMailmap`, `GitRev`, `GitStatus`, `IconBitmapUtils`, `LangDll`,
> `LoadIconEx`, `MassiveGitTaskBase`, `PathUtils`, `ReaderWriterLock`, `Registry`,
> `StringUtils`, `SysInfo`, `TGitPath`, `UnicodeUtils`

**Everything else in `src\Utils` can keep `CString` indefinitely** —
`MessageBox.h` (73 includers), `StandAloneDlg.h` (59), `SciEdit.h`,
`HistoryCombo.h`, `GitStatusListCtrl.h`, all of `MiscUI` and `TreePropSheet` are
MFC-only UI that lives in the MFC binary and is never compiled by the ATL side.
**Do not convert them "for consistency"; it is pure cost.**

The spanning base — one conversion per distinct kind of string boundary, after
which the rest is mechanical:

| # | boundary | vector | status |
| --- | --- | --- | --- |
| 1 | bytes ↔ wide | `UnicodeUtils` | **done**, `51919c55e` |
| 2 | OS state | `registry.h` | **already spanned** — templated on the string type; converting a call site is choosing the `Std` spelling |
| 3 | text idioms | `WideString.h` | **done**, `a483e6f4b` |
| 4 | filesystem paths | `PathUtils` | **done**, `94e4dc92d` |
| 5 | the path value type | `CTGitPath`/`CTGitPathList` | **done**, `c63032fbd` |
| 6 | subprocess / CLI | `Git.h` | **done** — see the six decisions below |

**The cascade follows the data, not the includes.** Ranking headers by includer
count suggests converting leaves first, and that is wrong: `GitMailmap.h` is a
textbook leaf, but `Translate(CString&, CString&)` writes straight into
`GitRev`'s members, so converting it drags `GitRev`, `GitRevLoglist` and
`GitRevRefBrowser` with it. **The unit of work is a connected component of the
data graph.** Remaining header `CString` counts: `Git.h` 135, `gitindex.h` 80,
`StringUtils.h` 39, `GitStatus.h` 19, `GitAdminDir.h`/`GitRev.h` 16 each.

**`Git.h` last, and deliberately.** Its 135 `CString` were six distinct decisions,
only one of which was a retype. All six have landed (user direction, 2026-08-27,
executed smallest-first — 2, 3, 6, 4, 5, 1):

| the CString in question | the actual decision | landed as |
| --- | --- | --- |
| `Run(const CString& cmd, …)`, `CGitCall`, `RunAsync`, `RunLogFile` | flat command line versus **`std::vector<std::wstring>` argv**. `GetLogCmd` *already* returned `std::vector<std::string>` for the dll path, so the codebase was half-converted and did not know it; argv moves quoting to one chokepoint instead of `QuoteParameter` at every call site. **This was the headline DoF** — retyping `cmd` to `std::wstring` buys nothing and forecloses it. The tell that it is design: `Run` has five overloads differing only in *what shape the output takes*, over one flat input. | `CGit::SerializeArgv` + argv overloads; **API done, call sites in progress** — see "The argv tail" |
| `CGitByteArray::operator CString()` (`gittype.h:88`) | an **implicit decode** — `strnlen_s` plus UTF-8 → wide, fired wherever a `BYTE_VECTOR` landed in a `CString` context, and silently truncating at the first NUL. The decision is whether bytes → text stays invisible. | `BYTE_VECTOR::Decode()`, `265dc2ec9` |
| `m_CurrentDir` | a **privatization** problem, not a retype: `SetCurrentDir()` performs admin-dir discovery and latches the object format, direct assignment does neither. Changing the type does not fix that. | read-only reference to a private member, two named setters, `cb959b4df` |
| `STRING_VECTOR` / `MAP_STRING_STRING` / `TGitRef` (`gittype.h:114-125`) | one typedef, whole-codebase fan-out; `TGitRef` even had `operator const CString&()`. Its own slice, and it is `gittype.h` work that `Git.h` merely surfaces. | `819a6c518` + `b5f8a4420` |
| `CEnvironment` | a double-NUL-terminated `std::vector<wchar_t>` block for `CreateProcess`. Its `GetEnv`/`SetEnv`/`AddToPath` CString API is *incidental* to a structure that is not a string. | a `std::map` with a serializer, `daa98ca5a` |
| `GetConfigValue(name, def, wantBool)` | git config values are bytes with a type the caller asserts. `GetConfigValueBool`/`GetConfigValueInt32` already exist; the question is whether the untyped one should. | typed readers public, untyped one kept for tri-state settings pages, `27df019dd` |

**The argv tail.** The API exists, is oracle-tested against `CommandLineToArgvW`,
and **all of `src\Git` invokes git through it** — the core is argv-only.
`RebaseDlg`, `GitDiff` and `GitLogListAction` are converted too. Measure progress
by counting flat command builders, `git grep -c -E '\.Format\(L"(git|bash)|Run\(L"(git|bash)' -- 'src/*.cpp' 'src/*.h'`:
**208 → 101** so far. What is left is consumer migration — mechanical per-cluster
edits against a settled API — roughly: `CommitDlg` (10), `FileDiffDlg` (10),
`SyncDlg` (7), the `ProgressCommands\` and `Commands\` directories, then the
long tail of one- and two-site files. Three things are **not** part of that tail
and should not be counted into it:

- **`AppUtils.cpp`'s ~44 `QuoteParameter` uses are mostly a different
  serializer** — `StartExtDiff`/`StartExtMerge`/`ExpandPlaceholdersForCmd` build
  *external tool* command lines from user-configured `%`-placeholder templates.
  A flat string is the product there, not an accident. Its own slice.
- **`CMassiveGitTaskBase`'s command prefix** (`L"push -- " + QuoteParameter(…)`)
  is a design question of the same shape as `Run` was, not a conversion.
- **`GitTest.cpp`'s ~67** are `QuoteParameter`'s own pin tests, which retire when
  it does.

**Do not `[[deprecated]]` the flat overloads** while ~250 sites remain: it is a
warning flood, and `QuoteParameter` and `illegal_git_parameter` cannot go until
their last consumer does.

**`SerializeArgv` does not rewrite argument content**, unlike non-relaxed
`QuoteParameter`, which converted `\` → `/`. Call sites that hand git a
*Windows* path must convert it themselves — `GetGitPathString()` already does,
and `ApplyPatchToIndex` has an explicit `AsGitPath` for the temp file it is
given. Native git takes backslashes either way; this only bites msys2/cygwin.

**Two shapes recur when converting, and both are invisible in a flat string:**
a `CString` that only ever holds `"--flag "` or nothing is a **bool**
(`--allow-empty`, `--no-commit`, `-x` were all this), and a format argument
quoted *inside* the command line — `--pretty=format:"%s"`, `rev-list "A...B"`,
`for-each-ref --format="…"` — is **one element with no quotes**, because the
quotes existed only to survive being parsed back into argv. Leaving them makes
them literal. Watch also for `x.IsEmpty() ? L"" : …`: an empty interpolation is
*omission*, but an empty argv element is a real empty argument.

**Phase B — one `TGitCore.lib`**: `src\Git` + `src\Utils` + AsyncFramework +
ResizableLib + TGitLibgit2, compiled **ATL-flavor**. With `std::wstring` at the
boundary it links into MFC consumers too, so the shell extension never has to
take an MFC dependency to get the shared library. `ATL::CStringW` may remain
*inside* `.cpp` files indefinitely — it never crosses a signature.

Two prerequisites, both already diagnosed:

- **`src\Git` and `src\Utils` have no project at all.** Every consumer lists the
  subset it wants — 1 file to 68 — and compiles it itself. `TGitPath.obj` is
  built 13 times.
- **Those 88 `.cpp` files are not self-contained.** All open with
  `#include "stdafx.h"` and neither directory contains one, so under `/Yu` the
  include is *replaced* by whichever PCH the consuming project built. Giving each
  directory a real `stdafx.h` is a prerequisite and is invisible to existing
  consumers. Expect the same class of breakage `PathUtils` hit when the gate went.
- **`TGITCACHE` is the only per-consumer conditional in `src\Git`**, and it
  selects whole function bodies rather than changing any class layout — except in
  `GitAdminDir.cpp`, where `HasAdminDir` genuinely behaves differently. That one
  is a real semantic fork and needs a runtime flag, not a merge. Note
  **`TortoiseShell` does not define `TGITCACHE`** — the two ATL consumers do not
  agree with each other today.
- **Resource IDs bind `Utils\MiscUI` to its consumer** — `MessageBox.cpp` alone
  has 93 `IDS_`/`IDD_` references. MiscUI is the last thing to move, not the first.

**Phase C** — drain the remaining `ATL::CStringW` from lib internals at leisure.

**Phase D — applet merge.** `TortoiseGitBlame`, `TortoiseGitUDiff` and
`SshAskPass` fold into `TortoiseGitProc.exe` as busybox-style applets: a fork in
`wmain` on `argv[1]` dispatching to `<app>_wmain()`. **`TortoiseGitStub` is
excluded by category** — an in-proc COM DLL with a static CRT, not an exe applet.
The real cost is **resource-ID unification**, not the dispatch. The win is one
binary, one Scintilla link, one `TortoiseLangs` target instead of three. On the
ATL side the symmetric move is to reduce `TGitCache.exe` to a thin host that
loads `TortoiseGit.dll` and calls an exported entry point.

### TortoiseGitUDiff — chalklined, not cut

Marked for removal (user direction, 2026-08-25), and the case is materially
weaker than for TortoiseGitMerge: **Scintilla does not fall out with it**
(TortoiseGitBlame and TortoiseProc's commit editor both use it), so cutting it
frees 1,519 lines and **zero** dependencies. There is no mature FOSS replacement
to vendor — the good unified-diff colourisers are terminal programs and
TortoiseGitProc has no console; the GUI options are general editors whose diff
support *is* a Scintilla lexer. If it goes, the honest floor is the existing
`PatchViewer` registry key → `notepad.exe` chain.

What cutting it involves, so it need not be rediscovered:

- 1,519 lines in `src\TortoiseUDiff\`, plus `TortoiseUDiffLang` and its Settings
  page (`SettingsTUDiff`).
- It is the registered handler for `.diff`/`.patch`: installer `UDiffAssoc`
  feature, `C__TortoiseUDiff`, `C__TortoiseUDiffMetaData`,
  `C__TortoiseUDiffAssoc`. Removing it orphans that association.
- It is what `CAppUtils::StartUnifiedDiffViewer` launches, and the shell's "Diff"
  for a patch file.
- **Three places argue *for* it in prose** and would become wrong: `MenuInfo.cpp`'s
  comment where ApplyPatch used to be, and the ApplyPatch/PuTTY commit messages.
- It compiles `PathUtils.cpp`, `DarkModeHelper`, `LangDll` and `Theme` from
  `src\Utils` — see the consumer-sweep rule above before assuming it is isolated.

### Open risk areas

- **`GetWorkingTreeChanges` under `LIBGIT2_ALL` is ~350× slower than the CLI.**
  Measured 2026-08-27 while parallelising the suite. The four fixture params run
  the same 192 calls; `GIT_CLI`, `LIBGIT` and `LIBGIT2` each take ~90ms total,
  `LIBGIT2_ALL` (mask `0xffffffff`) takes **33s** — about 174ms per call against
  ~0.5ms. That one test is a quarter of the whole suite and is now the floor on
  how fast a parallel run can be. It has never been investigated because a
  single-process suite hides it in an aggregate. Suspect a per-call repository
  open or index reload rather than the diff itself. **Not diagnosed.**
- **Last-open-wins on the libgit2 object-format latch.** `CAutoRepository::Open()`
  re-latches `g_gitObjectFormat`, so opening a submodule re-latches from it.
  Harmless while everything is SHA1 and correct for a uniform SHA256 working
  copy, but a **mixed-format superproject/submodule pair would end up with
  whichever was opened last. Not handled.**
- **`m_CurrentDir` is public and assigned directly in ~45 places**, and
  `SetCurrentDir()` does not latch the format either, so the invalid state
  (directory and format disagreeing) is still *representable* — just
  self-corrected on the next `CheckAndInitDll()`. Measured 2026-08-22: 455
  occurrences across 106 files, and the ~45 writes are **not** mechanically
  convertible, because `SetCurrentDir()` performs admin-dir *discovery* which most
  direct assignments do not want. `GetCurrentDir()` and `CombinePath()` give new
  code a const path so the count should not grow.
- **Raw `memcmp`/`memcpy` on oids outside `GitHash.h`** — grep `GIT_OID_` and
  fixed `20`/`40` literals near hash-looking variables. Audited 2026-08-22:
  nothing in `src\` bypasses the accessors, but new code can reintroduce it.
- **`CGit::GetConfiguredSshClient()` drops a stale path only when it names
  TortoiseGit's own removed plink *and* the file is missing. Do not relax that
  into a name-only test** — TortoiseSVN ships a working `TortoisePlink.exe`.
- **`~/.ssh/config` now applies to TortoiseGit and did not before.** `Host *` with
  `RequestTTY force` makes every fetch fail with "PTY allocation request failed";
  `ForceCommand`, `RemoteCommand` and `LogLevel` under `Host *` misbehave the same
  way. Scope such settings to the hosts that want them.

### Deferred sweeps

Each needs review, not a mechanical strip:

- **WiX x86 conditionals** in `src\TortoiseGitSetup\{StructureFragment,Includes}.wxi`
  — deferred from Phase 0. They guard real MSI components (gitdll32.dll,
  puttygen-x86.exe, TortoiseGitStub32.dll).
- **TortoiseMerge doc and assets** — the manual, `HTMLHelpfiles.wxi` components,
  `CheckIDD`, `LanguagePack.wxs` entries, the `.po` translations, and the orphaned
  `src\Resources\TortoiseMerge*` assets all still reference a deleted application.
  Left alone on purpose: the `Software\TortoiseGitMerge\*` registry key *names*,
  which are live user state.
- **Question the long-lived-process pattern itself** (user, 2026-08-26). The
  TortoiseGit-only half of the git fork — `odb_close_tgit()`, `reset_setup()`,
  `reset_git_env()` — exists only because gitdll re-points one process at
  different repositories, which is what TGitCache does to stay entangled with
  `explorer.exe`. That mattered for 2000s-era SVN, where every operation was a
  network round trip; modern git is fast and local. **If TGitCache spawned
  per-repository instead of switching, that patch bucket could be deleted
  outright** and the fork would shrink to what is upstreamable. Worth measuring.
- **Donate upstream**: the two OGDF patch hunks (`ext\vcpkg-ports\ogdf\`) are
  genuine upstream bugs, and `repo_clear()` leaving `->initialized` set on freed
  state is a git bug, not TortoiseGit behaviour.

## Critical files

- `ext\vcpkg-ports\` — the `libgit2`, `ogdf`, `scintilla` and `tgit-libgit`
  ports. These, not any vcxproj, are the source of truth for third-party feature
  flags. `tgit-libgit` generates its defines from `compile_commands.json` so they
  cannot drift from the library that was actually built.
- `src\Utils\WideString.h` — `tgit::wstr::*` (CString-contract helpers,
  differentially tested against the real `CString`), `tgit::wstr::View` (the
  migration bridge), and `std::formatter<CStringT>`.
- `src\Utils\GitObjectFormat.h` — the process-level object format:
  `g_gitObjectFormat`, `GitHashSize()`, `GitZeroRevString()`,
  `GitLatchObjectFormat()`. In `Utils` rather than `Git` because `GitWCRev` and
  the shell use libgit2 without `src\Git` on their include path.
- `src\Utils\SmartLibgit2Ref.h` — `CAutoRepository::Open()` is the single
  chokepoint where the libgit2 side latches the format.
- `src\Git\GitHash.h` — `CGitHash`, `GIT_HASH_MAX_SIZE` for fixed buffers, and
  `GIT_HASH_SIZE` as the runtime length.
- `src\TortoiseProc\AppUtils.cpp` — `StartExtDiff`/`StartExtMerge`/`StartExtPatch`
  are the single chokepoint where TortoiseGit decides which external tool runs.
  Every fallback to git lives here; nothing else should launch a diff or merge
  tool by name.
- `src\Utils\FileTextLines.{h,cpp}`, `src\Utils\EOL.h` — shared by TortoiseProc,
  TortoiseGitBlame and the tests. They live in `Utils` because they are not owned
  by any one application; they used to live in `src\TortoiseMerge\` and silently
  picked up that project's `resource.h`.
- `test\UnitTests\RepositoryFixtures.h` — fixtures copy `resources\<name>\` in at
  SetUp; `git-sha256-repo` is the SHA256 one.
- `test\ShellBench\` — the icon-overlay benchmark. Builds `ShellTest.exe` (that
  name is load-bearing; see the history doc).
- `src\TortoiseGitSetup\WiXSetup.wixproj`, `TortoiseGIT.wxs` — the WiX v3 MSI
  packaging to migrate in Phase 5.
- `ext\gitdll\gitdll.c` — the second, independent git backend.
