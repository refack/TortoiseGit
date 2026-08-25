# TortoiseGit — libgit2 SHA256 + build de-fragmentation plan

Working plan for two intertwined changes to this repo's libgit2 integration.
Written 2026-08-03, consulted with Fable; status last validated against the
repo 2026-08-25.

**Where this stands (2026-08-25).** Phases 0–3 are complete and Phase 4 works
end to end for the log, diff and commit paths: a SHA256 repository opens,
lists full 64-character ids, diffs against the working copy, and commits.
Phase 5 (MSIX) has not started. The phases were deliberately taken out of
written order, so read the per-phase status notes rather than inferring
sequence from the numbering.

**Test health is done, and is no longer the focus.** The suite runs unattended
and green — 574/574 in about 175 seconds, down from 557/29 and roughly 28
minutes of clicking. See "Test health" below for what that number means.

**Current focus is shrinking the tree.** Gone so far: CrashServer,
TortoiseGitMerge, TortoiseGitPlink with the bundled PuTTY binaries,
`tgittouch`, and the `simpleini`, `Detours`, `apr` and `apr-util` submodules.
The standing goal (per user direction, 2026-08-22) is to keep going — every
remaining `ext\*` submodule should become a public vcpkg port, a port overlay,
or nothing at all. Eleven are left. See "Dependency pruning".

The recurring test: **what does this actually add over the thing Windows or
git already ships?** For TortoiseGitMerge the answer was "nothing git's
difftool doesn't do"; for TortoiseGitPlink it was "a GUI prompt", which
`SshAskPass.exe` already provides for OpenSSH. Ask it before adding, too.

Historical landing order, kept because several phases were taken out of
sequence and the commit trail is otherwise hard to follow:

- `ecdcdbff1` — Phase 0 (drop Win32) for `src\`, `ext\build\`, `ext\gitdll`,
  `Languages\`; test\ vcxprojs and WiX x86 conditionals were deferred.
- `a7b376d76` — a slice of Phases 2+3 taken before Phase 1: `CGitHash` now
  owns a real `git_oid` member (the reinterpret-cast corruption risk is
  fixed), and `GIT_EXPERIMENTAL_SHA256` is defined for `libgit2.vcxproj`,
  `TortoiseProc.vcxproj`, `TortoiseMerge.vcxproj`. `GIT_HASH_SIZE` stays 20,
  so app behavior is still SHA1-only.
- `df40b91a9` — same define added to `test\UnitTests\UnitTests.vcxproj` and
  `test\Cache\Cache.vcxproj` (they link the flagged libgit2 DLL — without the
  define they'd be an ABI mismatch), plus their leftover Win32 configs
  removed. This doc was committed separately as `5f3e184f8`.
- Phase 1 milestones 1a–1c: the vcpkg overlay port
  (`ext\vcpkg-ports\libgit2\`), and **all ten libgit2 consumers migrated**
  off `ext\build\libgit2.vcxproj` onto it (that project and its `.filters`
  are now deleted). Whole solution builds clean; `TortoiseGitProc.exe` is
  smoke-tested on SHA1 and SHA256 repos per the build+run gate, the other
  nine on build-only per the nice-to-have bar. See Phase 1 status notes for
  detail and the validation-scope policy below.
- `3bd517e84`, `21a8e1628`, `ba1754250` — test health: the suite became
  unattended, then green. The middle commit is the important one; it fixed a
  real SHA256 bug that 16 failures had been blamed on the environment.
- `dfe20a669`, `f35d1fbf7`, `f02a2a6a4`, `6c9395cb4` — dependency pruning:
  `simpleini` and `Detours` moved to vcpkg, the last reach into libgit2's
  private headers was replaced, and cross-project includes now resolve through
  the include path instead of `..\`.
- `306aca834` — the Doctor Dump crash reporter removed (−78,112 lines).
- `6d3a6d4d7`, `0784142fc`, `c9149bc5e` — TortoiseGitMerge removed, after
  teaching TortoiseGit to fall back to git's own diff and merge tools; the
  third commit cleared the references a green build cannot see.
- `3e0a0e71f` — an unbalanced Detours hook, exposed by the vcpkg migration.
- `bf2029b99`, `7defc0e95`, `26c13b9cf`, `ddd23a544` — PuTTY carved out:
  OpenSSH became the default, then the key-management UI, then TortoiseGitPlink
  and the bundled binaries, then `tgittouch`.

**Validation scope (set 2026-08-09, per user direction):** this migration
touches ten libgit2 consumers plus the installer and translation build. Not
all of them carry equal weight, and re-verifying all ten by hand every
milestone doesn't pay for itself. The bar going forward:

- **`TortoiseGitProc.exe` is the only target that must build *and* pass a
  runtime smoke test** (opens a SHA1 repo and a SHA256 repo without
  crashing) before a milestone counts as done.
- The other nine consumers (TortoiseMerge — since removed, `0784142fc` —
  TortoiseShell, TGitCache,
  TortoiseGitBlame, TortoiseIDiff, GitWCRev, GitWCRevCOM, UnitTests, Cache),
  plus whatever of `ext\zlib` / `ext\build\pcre2.vcxproj` follow libgit2 to
  vcpkg, are **"nice to have": migrate and defragment all of them, but the
  bar is a clean build, not a runtime check.** If one breaks in a way that
  isn't a quick fix, it can lag a milestone without blocking the others.
- **WiX packaging (`WixSetup.wixproj`) and the Languages build
  (`TortoiseLang.vcxproj`) are assumed working and out of scope for this
  effort's validation.** Both already fail in this dev environment for
  reasons unrelated to libgit2/vcpkg (WiX v3 toolchain not installed;
  `build-lang.cmd` tool missing from PATH — see Phase 0/Phase 5 status).
  Don't treat their failure as a regression signal; don't spend time getting
  them building locally as part of this plan.

## Goals

- [ ] Add experimental SHA256 object-id support to TortoiseGit's libgit2 use.
- [ ] De-fragment the build: `ext\build\libgit2.vcxproj` currently builds
   libgit2 as its own standalone DLL project. Break it apart into a thin
   piece that merges into the consuming project(s) instead of staying a
   separate DLL — this is essentially a single-application codebase and the
   current fragmentation (a dozen+ small `ext\build\*.vcxproj` files) buys
   nothing. Patches to 3rd party libraries should be made into vcpkg port overlays or refactored out completly.
- [x] Drop all Win32 (x86) build targets. It's 2026; nobody runs a headed
   workstation on 32-bit Windows 11. Removing Win32 configs simplifies every
   step below (no `libgit232_tgit.dll` naming variant, no `GIT_ARCH_32`
   branch, one fewer platform to carry through the SHA256 ABI change).
- [ ] Retire the `ext\*` git submodules. *(Added 2026-08-22, per user
   direction: "the goal is to replace them all with vcpkg ports, public ports,
   or overlays if necessary.")* A vendored submodule plus a hand-maintained
   `ext\build\*.vcxproj` is the fragmentation this plan exists to remove; a
   port declares the same thing once, in a form the package manager can
   reproduce. 15 at the start, 11 today. See "Dependency pruning".
- [ ] Prefer delegating to git over reimplementing it. TortoiseGit's value is
   the Explorer integration, not a second implementation of what `git.exe`
   already does well. Where a built-in duplicates a git feature the user has
   already configured, hand the work to git and keep a primitive floor
   (`notepad.exe`) so the chain always terminates. TortoiseGitMerge was the
   first application retired on this basis.

## Current architecture (as verified by reading the repo)

- A vcpkg-built `git2-experimental.dll` was tried as a throwaway experiment but it didn't work without changes.
  **Correction (2026-08-04, per user direction): this is no longer read as
  "vcpkg is untrustworthy" — Phase 1's goal has reversed to adopting vcpkg,
  bridged by a port overlay. Find out *why* that attempt failed (missing
  feature flags? backend selection?) before repeating it, see Phase 1.**
  `ext\*` subtrees are vendored via git submodules — that's the trusted, higher-quality architecture.
  Any libgit2 source fork/patch work happens **inside `ext\libgit2`**
  (the submodule), following the existing pattern: TortoiseGit-specific
  patches live as `ext\libgit2-*.patch` at the repo root and get applied via
  `git am --3way` (see `build.txt`). Five such patches already exist.
- **Integration/consumption changes belong in `src\`.**
- TortoiseGit does not use libgit2's own CMake build on Windows — `ext\build\libgit2.vcxproj`
  is a hand-maintained MSBuild project that manually lists ~180 `.c` files
  out of `ext\libgit2\src\*` plus three TortoiseGit-own sources at
  `src\libgit2\{filter-filter,ssh-wintunnel,system-call}.c`.
  The goal is to refactor patches and external sources out, so the a vcpkg port could be used.
- `ext\build\libgit2.vcxproj` (DynamicLibrary) → `libgit2_tgit.dll`.
  **Correction (2026-08-04): consumed via `ProjectReference` by TEN projects,
  not two** — `src\`: TortoiseProc, TortoiseMerge, TortoiseShell, TGitCache,
  TortoiseGitBlame, TortoiseIDiff, GitWCRev, GitWCRevCOM; `test\`: UnitTests,
  Cache (grep `libgit2\.vcxproj` across `**\*.vcxproj`). It is a genuine
  multi-consumer shared library today, not pure fragmentation — factor this
  into the restructuring, don't just inline it into one exe and strand the
  others. This also widens the `GIT_EXPERIMENTAL_SHA256` blast radius: every
  one of these must define the flag or it has an ABI-mismatched view of
  `git_oid` across the DLL boundary.
- A **second, independent** git backend exists: `ext\build\libgit.vcxproj`
  (StaticLibrary, compiles real git.exe source from the `ext\tgit`
  submodule) feeds `ext\gitdll\gitdll.vcxproj` → `gitdll.dll`/`gitdll32.dll`.
  It has its own hash handling, unrelated to libgit2's `git_oid`, but the
  same 20-byte assumption today.
- `src\TortoiseGitSetup\StructureFragment.wxi` lists `libgit2_tgit.dll`
  explicitly for MSI packaging — needs updating once it's no longer a
  standalone DLL.

## Why SHA256 is not a drop-in flag

Upstream libgit2's `EXPERIMENTAL_SHA256` CMake option (see
`ext\libgit2\cmake\ExperimentalFeatures.cmake`) sets `GIT_EXPERIMENTAL_SHA256=1`,
which (`ext\libgit2\include\git2\oid.h`) changes the `git_oid` struct itself:
it gains a `type` field and its `id[]` buffer grows from 20 (`GIT_OID_SHA1_SIZE`)
to 32 (`GIT_OID_MAX_SIZE`/`GIT_OID_SHA256_SIZE`). This is an ABI/header
change, not an additive feature.

TortoiseGit hard-codes the 20-byte assumption in `src\Git\GitHash.h`:

```cpp
#define GIT_HASH_SIZE 20
static_assert(sizeof(git_oid) == GIT_HASH_SIZE, "hash size needs to be the same as in libgit2");
// comment: "also see gitdll.c"
```

`CGitHash` in that same header used to store `unsigned char m_hash[20]` and
`reinterpret_cast` it to `git_oid*` before calling `git_oid_cpy` — under
experimental mode that writes up to 33 bytes into a 20-byte buffer. **Silent
memory corruption, not a compile error**, if the define is flipped without
fixing this first. `CGitHash` is used everywhere (logs, blame, revision
graphs, refs) — not a narrow type. *Fixed in `a7b376d76`: `CGitHash` now
owns a real `git_oid m_oid` member; the same silent-mismatch mechanism still
applies to any libgit2 consumer built without the define (see the ten-project
list above).*

A hard cutover to SHA256-only is not viable; existing users have SHA1 repos.
The experimental struct is itself dual-capable (tagged by `type`), so the
plan is: make `CGitHash`/TortoiseGit dual-hash-capable, not a replacement.

## Where the libgit2 patch actually lives for the time being (mostly: nowhere)

The Win32 SHA256 backend (`hash\win32.c`, `GIT_SHA256_WIN32`) is **already**
compiled into `libgit2.vcxproj` today. `ExperimentalFeatures.cmake` only
adds the `GIT_EXPERIMENTAL_SHA256=1` define — enabling it is purely a
vcxproj-side change (the define needs to reach every TU that includes
`git2/oid.h`: libgit2 itself, TortoiseProc, TortoiseMerge). A new
`ext\libgit2-*.patch` is only needed if upstream source or
`src\libgit2\{filter-filter,ssh-wintunnel,system-call}.c` need fixes under
the define — create it then, via the existing `git am` pattern.

## Phased plan

- [X] **Phase 0 — Drop Win32.** Remove the Win32/x86 platform configuration from
the solution and every `.vcxproj` under `src\` and `ext\build\` (grep
`Platform">Win32` / `'$(Platform)'=='Win32'`). Removes the
`libgit232_tgit.dll` naming branch, `GIT_ARCH_32`, and Win32-only WiX
component entries before anything else touches these files. Exit: x64 and
ARM64 build clean; no Win32 config left in `TortoiseGit.sln`/`.slnx`.
  *Status: done in `ecdcdbff1` for the stated scope (sln + `src\` +
  `ext\build\` + gitdll + Languages; verified 2026-08-04: zero `Win32` refs
  in the sln, only inert `<Keyword>Win32Proj</Keyword>` tags remain in
  vcxprojs). `test\*.vcxproj` cleanup landed with the test-define follow-up.
  Still open: WiX `$(var.Platform) = "x86"` conditionals in
  `src\TortoiseGitSetup\{StructureFragment,Includes}.wxi` — these guard real
  MSI components (gitdll32.dll, puttygen-x86.exe, TortoiseGitStub32.dll) and
  need careful review, not a mechanical strip.*

- [X] **Phase 1 — Migrate to vcpkg (de-fragmentation).** *(Revised
2026-08-04, per user direction.)* The goal is no longer "keep
hand-maintaining `ext\build\libgit2.vcxproj`'s ~180-file list, just as a
static lib instead of a DLL" — it's to stop hand-maintaining libgit2's build
at all and consume it through vcpkg like any other third-party dependency.
Bridge: add a vcpkg **port overlay** (not a change to upstream vcpkg) that
layers TortoiseGit's five `ext\libgit2-*.patch` files and the feature-flag
set the current `libgit2.vcxproj` `PreprocessorDefinitions` encode
(`GIT_HTTPS`, `GIT_WINHTTP`, `GIT_SHA256_WIN32`, `GIT_EXPERIMENTAL_SHA256`,
`GIT_NTLM`, `GIT_REGEX_PCRE2`, `GIT_THREADS`, ... — that vcxproj line is the
source of truth for what the overlay must replicate) on top of the stock
vcpkg `libgit2` port. The overlay is a temporary bridge, dropped once/if
these patches are upstreamed. Revisit *why* the earlier throwaway
`git2-experimental.dll` vcpkg attempt failed "without changes" (see
"Current architecture" above) rather than repeating the same failure — most
likely a missing feature flag or backend-selection mismatch, not a
fundamental vcpkg incompatibility. All ten `ProjectReference` consumers
move to the vcpkg-provided package; `ext\build\libgit2.vcxproj` and the
vendored `ext\libgit2` submodule (for *build* purposes only — patch
authorship can stay wherever's convenient) retire once the overlay is
proven equivalent. Remove `libgit2_tgit.dll` from `StructureFragment.wxi`
either way — a shared DLL disappears under vcpkg static linkage too. Exit
(revised 2026-08-09, see validation-scope policy above): x64 build via vcpkg
manifest mode; `TortoiseGitProc.exe` builds and passes a runtime smoke test
on a SHA1 and a SHA256 repo; the other nine consumers build clean (no
runtime check required); installer/Languages assumed working, not gated on;
ARM64 best-effort; the patch surface needed on top of stock libgit2 lives in
the overlay port, not scattered root-level `.patch` files.
  *Status: milestone 1a done — the overlay port exists and builds.
  `ext\vcpkg-ports\libgit2\` + root `vcpkg.json` /
  `vcpkg-configuration.json`; `vcpkg install --triplet x64-windows-static-md`
  produces `lib\git2.lib` + `include\git2\`. Verified: the generated
  `git2_features.h` matches `libgit2.vcxproj`'s defines exactly, and installed
  `git2\experimental.h` carries `GIT_EXPERIMENTAL_SHA256 1`.*

  *Status: milestone 1b done — **TortoiseProc is the first consumer migrated**,
  one of ten. The other nine still `ProjectReference`
  `ext\build\libgit2.vcxproj`; both worlds coexist and the whole solution builds
  clean (x64/Debug). New pieces:*
  - *`src\TortoiseGit.vcpkg.props` — triplet/include/lib wiring for consumers,
    plus a `VerifyVcpkgRestore` target that fails with "run vcpkg install"
    instead of an unresolved-external wall.*
  - *`src\libgit2\TGitLibgit2.vcxproj` — static lib holding
    `{filter-filter,ssh-wintunnel,system-call}.c`, which used to be compiled
    inside the libgit2 DLL. It carries **no** feature defines: the overlay now
    installs libgit2's private header trees plus the cmake-generated
    `git2_features.h` under `include\libgit2-private\{libgit2,util}\`, so the
    flags cannot drift from the library that was actually built.*
  - *`ssh-wintunnel.c`'s hardcoded
    `#include "../../ext/libgit2/src/libgit2/transports/smart.h"` became
    `#include "transports/smart.h"`, resolved from the installed private headers
    — one less reach into the submodule.*
  - *TortoiseProc dropped `..\..\ext\libgit2\include` from its include path and
    `GIT_EXPERIMENTAL_SHA256` from its defines. **Both removals matter:** the
    in-tree `git2\experimental.h` is a no-op stub, so leaving that path would
    silently rebuild the project against the 20-byte `git_oid`.*

  *Verified three ways: `dumpbin /DEPENDENTS` shows `libgit2_tgit.dll` gone from
  `TortoiseGitProc.exe` (replaced by winhttp/rpcrt4/crypt32/ole32/secur32, exactly
  the port's pkgconfig `Libs:` line); `CommitDlg.cpp:1161` calls the two-argument
  `git_index_new`, which only exists under `GIT_EXPERIMENTAL_SHA256`, so a clean
  compile proves the flag survived the switch to header-supplied propagation; and
  `/command:log` opens on the SHA256 test repo without crashing. Not verified:
  the log rows' rendering (Windows-MCP was disconnected), and `UnitTests` still
  links the old DLL so it does not exercise this path.*

  *Why the earlier `git2-experimental.dll` attempt failed (now answered): with
  `EXPERIMENTAL_SHA256=ON`, upstream's `ExperimentalFeatures.cmake` appends
  `-experimental` to `LIBGIT2_FILENAME`, which renames the **installed header
  directory** to `include\git2-experimental\` and rewrites the umbrella
  header's own includes — breaking every `#include "git2.h"` in `src\`.
  `tortoisegit-no-experimental-rename.diff` drops that append.*

  *Two findings worth carrying forward:*
  - *`experimental.h` is generated by CMake and carries the define itself. Once
    consumers include the vcpkg headers, `GIT_EXPERIMENTAL_SHA256` propagates
    automatically — this structurally removes Phase 3's "six unflagged
    consumers" ABI hazard rather than requiring ten vcxproj edits.*
  - *`GIT_QSORT_S` in `libgit2.vcxproj` is a **stale no-op**: libgit2 renamed it
    to `GIT_QSORT_MSC`, so the hand-maintained build has silently been using
    libgit2's bundled qsort instead of MSVC `qsort_s`. The vcpkg build gets
    `GIT_QSORT_MSC` correctly.*

  *Blocker resolved for the next milestone:
  `src\libgit2\{filter-filter,ssh-wintunnel,system-call}.c` call ~20
  libgit2-**internal** symbols
  (`git__calloc`, `git_str_*`, `git_net_url_*`, `git_filter_buffered_stream_new`,
  `git_repository_config__weakptr`, `git_utf8_to_16_alloc`, ...). These work
  today only because those files compile **inside** the DLL. Verified via
  `dumpbin /LINKERMEMBER` that the vcpkg **static** `git2.lib` exposes all of
  them (the three that don't appear — `git__free`, `git_str_find`,
  `git_process__is_cmdline_option` — are `GIT_INLINE` header inlines). So the
  three files can keep compiling against `ext\libgit2`'s private headers and
  link the vcpkg lib. **This is why the migration must be static, not a
  vcpkg-built DLL** — a DLL exports none of these.*

  *Also fixed in passing: the `ext\libgit2-*.patch` series is applied in
  filename order by `build.txt`'s `for %%G in (..\libgit2-*.patch)` loop, but
  the correct series order is wildcard -> Guess-better-path -> simplify.
  Alphabetical order runs simplify before Guess-better-path and leaves conflict
  markers in `repository.c` even under `git am --3way`. The overlay's
  regenerated diffs encode the right order; `build.txt` still documents the
  wrong one. Note the submodule checkout is currently stock v1.9.4 with **none**
  of the five patches applied, so local builds today are unpatched.*

  *Status: milestone 1c done (2026-08-09) — **all ten consumers migrated,
  `ext\build\libgit2.vcxproj` retired.** TortoiseMerge, TortoiseShell,
  TGitCache, TortoiseGitBlame, TortoiseIDiff, GitWCRev, GitWCRevCOM,
  `test\UnitTests`, `test\Cache` all got the same four edits as TortoiseProc:
  import `TortoiseGit.vcpkg.props`, swap the `libgit2.vcxproj`
  `ProjectReference` for `TGitLibgit2.vcxproj`, drop
  `..\..\ext\libgit2\include`, drop manual `GIT_EXPERIMENTAL_SHA256` where
  present (TortoiseMerge, UnitTests, Cache — the other six never had it).
  Two of the nine (`TortoiseShell`, `TGitCache`) had
  `<ReferenceOutputAssembly>false</ReferenceOutputAssembly>` on the old DLL
  reference, left over from "the DLL just needs to exist on disk, nothing to
  link"; that had to be dropped too, or the static lib's symbols never reach
  the linker and it fails with unresolved externals instead of a diagnosable
  error. `ext\build\libgit2.vcxproj` + its `.filters` are deleted, plus the
  sln's `Project`/`ProjectConfigurationPlatforms`/`NestedProjects`/
  `WixSetup`-dependency entries referencing its GUID; `libgit2_tgit.dll`
  dropped from `StructureFragment.wxi`'s x64/ARM64 branch (the x86 branch's
  `libgit232_tgit.dll` line is untouched — that's the pre-existing deferred
  Win32 WiX cleanup from Phase 0, a separate concern). A stale doc comment in
  `TortoiseGit.common.props` pointing at the now-deleted file's PCH-pinning
  example was generalized rather than left dangling.
  Verified: `grep -rn "ext\\build\\libgit2\|ext\\libgit2\\include"
  **/*.vcxproj` returns nothing outside comments; one full-solution
  `Build-Nice.ps1` Debug/x64 build is clean (all ten consumers link, WixSetup/
  TortoiseLang aren't part of the Debug config so their pre-existing,
  unrelated failures don't show up here); `TortoiseGitProc.exe` re-smoke-
  tested on both the SHA1 `E:\3party\TortoiseGit` repo and the SHA256
  `test\sha256wc` repo post-rebuild, per the build+run gate. The other nine
  got build-only validation per the nice-to-have bar — not individually
  smoke-tested.*

  *Status: milestone 1d done (2026-08-09) — **`pcre2` retired from the
  hand-maintained build**, `ext\build\pcre2.vcxproj` deleted. Its only
  remaining consumer was `ext\build\editorconfig.vcxproj` (libgit2 was the
  other one; already gone). `editorconfig.vcxproj` now imports
  `TortoiseGit.vcpkg.props` for `pcre2.h`'s location, dropping the vendored
  `ext\build\pcre2\` include dir and the `ProjectReference` to `pcre2.vcxproj`.
  **No change was needed in `TortoiseMerge.vcxproj`** (editorconfig's only
  consumer *at the time*; since TortoiseMerge was removed in `0784142fc`,
  editorconfig's consumer is TortoiseProc — which is why `ext\editorconfig`
  must **not** be pruned) to actually link `pcre2-8.lib`: it already imports
  `TortoiseGit.vcpkg.props` from the libgit2 migration, and that props file's
  `AdditionalDependencies` already lists `pcre2-8$(VcpkgLibSuffix).lib` — a
  static-lib `ProjectReference` chain (TortoiseMerge -> editorconfig; a
  StaticLibrary project has no link step of its own) only ever affected
  build order, never linkage, so the fix belongs where the real Link step
  happens. `ext\build\pcre2\config.h` (defines `PCRE2_STATIC` and
  `PCRE2_CODE_UNIT_WIDTH 8` before `pcre2.h` is included from
  `ext\editorconfig\src\lib\global.h`) is untouched — vcpkg's installed
  `pcre2.h` is the same upstream header with the same contract, so nothing
  about that convention needed to change. `ext\pcre2` submodule is now
  unreferenced by any vcxproj (left vendored, same as `ext\libgit2` — build
  wiring retired, submodule checkout untouched). Removed from the sln
  (`Project`/`ProjectConfigurationPlatforms`/`NestedProjects`; pcre2 never
  had a `WixSetup`-dependency edge, unlike libgit2). Verified: `grep -rn
  "build\\pcre2\.vcxproj\|build\\pcre2\\" **/*.vcxproj` empty; one
  full-solution `Build-Nice.ps1` Debug/x64 build clean, `TortoiseGitMerge.exe`
  links.*

  *Status: milestone 1e done (2026-08-09) — **`zlib` retired from the
  hand-maintained build**, `ext\build\zlib.vcxproj` deleted. Root `vcpkg.json`
  now lists `pcre2` and `zlib` as direct dependencies (they were only
  transitive via libgit2's features before; harmless no-op on `vcpkg install`
  since both were already installed, but now honest about who actually
  consumes them).*

  *This one didn't fit the existing `TortoiseGit.vcpkg.props`: that file
  bundles libgit2 + pcre2 + zlib's `AdditionalDependencies` as one blob, but
  `ext\gitdll\gitdll.vcxproj` (the **second, independent git backend** — see
  "Current architecture" above) needs zlib and must not link libgit2 —
  putting `libgit.lib` (real git.exe C sources) and `git2.lib` on the same
  link line risks first-match symbol collisions between two implementations
  of the same concepts (odb, refs, packfiles). Split the props file:*
  - *`TortoiseGit.vcpkg-base.props` — triplet/include/lib paths and
    `VerifyVcpkgRestore` only, no `AdditionalDependencies`. Package-neutral;
    import this directly for a single vcpkg package.*
  - *`TortoiseGit.vcpkg.props` — now just imports the base file and adds the
    libgit2-bundle `AdditionalDependencies` (`git2.lib;pcre2-8...;zs...;
    winhttp.lib;...`). **Zero edits needed to any of the ten already-migrated
    libgit2 consumers** — same net effect through the new indirection.*
  - *`gitdll.vcxproj` and `ext\build\libgit.vcxproj` (which feeds it) both
    import the base file only, and `gitdll.vcxproj` adds
    `zs$(VcpkgLibSuffix).lib` to its own `AdditionalDependencies` explicitly.*

  *TortoiseShell, TortoiseMerge, TGitCache, TortoiseProc dropped their
  `..\..\ext\zlib` include dir and (the first three) their `ProjectReference`
  to `zlib.vcxproj`. **No new link wiring needed for any of them**: all four
  already import `TortoiseGit.vcpkg.props` from the libgit2 migration, which
  already lists `zs$(VcpkgLibSuffix).lib`. (The actual zlib consumer inside
  TortoiseMerge is `libsvn_diff\adler32.c`, which does `#include <zlib.h>` —
  confirmed by grep before assuming the include-dir removal was safe.)*

  *`ext\build\zlib.vcxproj` + `.filters` deleted; removed from the sln
  (`Project`/`ProjectConfigurationPlatforms`/`NestedProjects`, plus **two**
  `ProjectDependencies` edges — `WixSetup`'s, same pattern as libgit2, and
  also `TortoiseMerge`'s own solution-level dependency entry mirroring its
  `ProjectReference`). `zlib1_tgit.dll` dropped from `StructureFragment.wxi`'s
  x64/ARM64 branch (x86's `zlib132_tgit.dll` line untouched, same precedent
  as libgit2/pcre2 — the deferred Win32 WiX cleanup from Phase 0).
  `ext\CrashServer\CommonLibs\Zlib\Zlib.vcxproj` is a separate, untouched
  consumer (compiles minizip contrib sources vcpkg's zlib port doesn't ship).
  **Since resolved:** that project was the only thing keeping `ext\zlib`
  vendored, and it went with CrashServer in `306aca834`.*

  *Verified: repo-wide grep for `zlib1_tgit` (checked before starting) found
  only `CLAUDE.md` and the `.wxi` — no delay-load/LoadLibrary surprise. One
  full-solution `Build-Nice.ps1` Debug/x64 build clean; `dumpbin /DEPENDENTS`
  on `gitdll.dll` shows no more zlib DLL dependency; `TortoiseGitProc.exe`
  re-smoke-tested on both the SHA1 and SHA256 repos, per the build+run gate
  (this one re-entered the gate because `gitdll.dll`, which `TortoiseGitProc`
  loads, changed its zlib linkage).*

  *Phase 1's original "de-fragment the build" goal is now complete for all
  three packages that had genuine vcpkg equivalents (libgit2, pcre2, zlib).
  ARM64 is still unverified — this machine has no `Hostx64\arm64`
  cross-compiler, so `arm64-windows-static-md` fails in stock `pcre2` before
  reaching libgit2 (needs the VS "C++ ARM64 build tools" component); treat as
  best-effort, not a blocker.*

- [X] **Phase 2 — Hash-size hygiene (pre-req for Phase 3, define still OFF).**
Refactor `CGitHash` to own real storage sized for the eventual 32-byte case
instead of reinterpret-casting a 20-byte buffer, behind an unchanged public
API. Audit all `GIT_HASH_SIZE` call sites (fixed-length hex parsing at
`2*GIT_HASH_SIZE`, abbreviation logic, buffers — known files include
`Git.cpp`, `GitIndex.cpp`, `GitDiff.cpp`, `LogDlg.cpp`,
`TortoiseGitBlameData.cpp`, `ContextMenu.cpp`, `GitLogCache.cpp`). Version
`GitLogCache`'s on-disk format (bump its magic/version) so stale caches are
discarded rather than misread once hash size can vary. Exit: builds and
behaves identically to today, define still off — this phase is pure safety
margin for Phase 3.
  *Status: partially done in `a7b376d76` (and out of order — the define went
  on at the same time, see Phase 3): `CGitHash` now owns a real `git_oid`
  member behind the unchanged public API. **Completed** (out of order, with the
  define already on): the `GIT_HASH_SIZE` call-site audit found ~40 sites across
  12 files and every one goes through the macro — no raw `20`/`40` literals
  bypass it, so making it runtime fixed them all at once; and `e644fb421` bumped
  `LOG_INDEX_VERSION` `0x11` -> `0x12`. That bump was not merely precautionary:
  `SLogCacheIndexItem` embeds `CGitHash` by value in a `#pragma pack(1)` struct,
  so once the define was on, the on-disk item stride had already changed and a
  pre-flag cache was being misread.*

- [X] **Phase 3 — Flip `GIT_EXPERIMENTAL_SHA256`.** Define it everywhere a TU
includes `git2/oid.h` (libgit2 project, TortoiseProc, TortoiseMerge, tests).
Fix fallout. Patch `ext\libgit2` via the `git am` pattern only if the
experimental headers/sources themselves need a TortoiseGit-specific fix.
Exit: SHA1 repos fully regression-clean; libgit2-backed operations can open
a SHA256 repo.
  *Status: done. `a7b376d76` flipped the define for libgit2 +
  TortoiseProc + TortoiseMerge (fallout fixed: `git_index_new` gained an
  `opts` param); `df40b91a9` adds UnitTests + Cache (fallout fixed:
  `git_odb_hashfile`/`git_odb_hash` gained an oid-type param —
  `PatchTest.cpp`, `GitIndex.cpp` via a `tgit_odb_hash` shim — and
  `GitWCRev.h`'s `HeadHashReadable` buffer is now `GIT_OID_MAX_HEXSIZE`-sized
  with its "SHA2 is not available" static_assert removed).
  **The "six unflagged consumers" hazard is gone, and not because anyone
  flagged them.** The vcpkg migration (`f78e98aaf`) installs the
  cmake-generated `git2\experimental.h`, which carries `GIT_EXPERIMENTAL_SHA256`
  itself, so the define now propagates through the headers to every consumer.
  A hand-maintained build let the flag drift per project; a port emits it into
  a header, so consumers cannot disagree with the library they link. Do not go
  re-flag those six vcxprojs — there is nothing to flag.
  Exit criteria met: `GIT_HASH_SIZE` is the runtime `GitHashSize()`, and SHA256
  repositories open and work (see Phase 4).*

- [ ] **Phase 4 — App-level SHA256 UX + gitdll.** 64-char hash display/parsing
throughout the UI, `GIT_REV_ZERO` (currently a 40-char literal) needs a
64-char counterpart, gitdll/`ext\tgit` backend support (real git already has
`the_hash_algo` internally — a later step can surface it through
`gitdll.c`, which today shares the 20-byte assumption but not libgit2's
`git_oid` type). Mark the feature experimental in the UI. Exit: core
workflows (log, status, commit, blame, diff) work end-to-end on a SHA256
test repo.
  *Status: started out of order in `f8064ea8e` — `/command:log` works
  end-to-end on a SHA256 repo (`test\sha256wc`, untracked local test repo:
  full 64-char id in list + detail pane, diff resolves, context menu works).
  Design (per user direction): object format is **process-level state**, not
  per-hash — TortoiseGit runs one process per working copy, so
  `CGit::CheckAndInitDll()` latches `g_gitObjectFormat` from the newly
  exported `git_get_hash_algo()` (gitdll), and `GIT_HASH_SIZE` is now the
  runtime function `GitHashSize()` (20 or 32); fixed-size buffers use
  compile-time `GIT_HASH_MAX_SIZE` (40) so there is always room for both
  modes, and API calls fork on the format where needed. Sizes reuse
  libgit2's `GIT_OID_*` macros — do not add duplicate size macros.
  gitdll fixes: 2019 `die("Only SHA1...")` guard narrowed to unknown
  formats; three hardcoded `GIT_SHA1_RAWSZ` copies → `hashcpy` with the
  repo's algo; four `oid.algo = 0` → real algo index (required or
  `lookup_commit` fails). Hashes stay **hex** (64 chars) — that is the
  universal convention (git, GitHub, GitLab); never base64.
  The working-copy pseudo-revision follows the format too: `GitZeroRevString()`
  sizes the all-zero sentinel, `GitRev::GetWorkingCopyRef()` returns it, and
  `GIT_REV_ZERO` is defined in terms of it, which fixed ~45 call sites without
  touching them. Diff-against-working-copy is verified by hand on both a SHA256
  and a SHA1 working copy.
  `IDS_HASH`/`IDS_LOG_FILTER_REVS` now say "SHA" rather than "SHA-1", since they
  label a column and a filter in repositories that may be either format; where
  there is room to be specific the log detail pane names the actual algorithm.
  The Changed Files dialog offers Commit when one side of the diff is the
  working copy, which closes log -> compare to worktree -> review -> commit.
  Remaining gaps: `GIT_REV_ZERO_C` is deliberately still SHA1-width and narrow,
  since it is GitWCRev's unborn-HEAD output; and see the unflagged-consumer
  risk below. Note the status/commit/blame dialogs are **not** unexercised —
  the user ran a SHA256 working copy for roughly two weeks (2026-08-08 to
  2026-08-22) on a Release build predating most of this work, and the only
  defect that surfaced was the zero-width sentinel fixed in `468e9c14a`.*

- [ ] **Phase 5 — Switch the WiX packager from MSI to MSIX.** *(Added
2026-08-04, per user direction. Out of scope for Phases 1–4's validation —
see the 2026-08-09 validation-scope note near the top: WiX packaging is
assumed working until this phase is actually taken up.)* Replace
`src\TortoiseGitSetup\WiXSetup.wixproj`'s MSI output
(`OutputType>Package`, WiX v3 `Wix.targets`, `TortoiseGIT.wxs`) with an
MSIX package. **Not a trivial packer swap:**
  - The WiX v3 toolchain this repo pins (`WixTargetsPath` →
    `Microsoft\WiX\v3.x\Wix.targets`) does not emit MSIX at all — that needs
    WiX v4/v5's MSIX authoring, or a separate MSIX Packaging Tool/`makeappx`
    step, i.e. a real toolchain migration, not a project-property flip.
  - `TortoiseGIT.wxs` leans on classic MSI mechanics with no MSIX
    equivalent: elevated `CustomAction` DLL entry points
    (`CustomActions.dll`/`CustomActions11.dll`) for shell-extension COM
    registration, `RegisterSparsePackage`/`UnregisterSparsePackage` for the
    Windows 11 context menu, `RestartExplorer`, and per-machine
    `HKLM`/`HKCU` `RegistrySearch` for upgrade/repair/detection logic. MSIX
    runs installs in a constrained context — no arbitrary elevated custom
    actions — so each of these has to move into the packaged app's own
    registration path, or be dropped where MSIX's package identity /
    registry virtualization already covers the same need. Audit
    `TortoiseGIT.wxs` custom-action-by-custom-action before assuming
    coverage; don't assume parity.
  Exit: MSIX package installs/uninstalls/upgrades cleanly, the shell
  extension (Explorer context menu, icon overlays) registers and works
  under MSIX's packaged-app model, no functionality silently dropped
  relative to the MSI installer.

## Work outside the phased plan

Three things landed that the phases above never had a slot for. They share a
premise with Phase 1 — stop hand-maintaining what someone else already
maintains — but they subtract code rather than move it.

### Dependency pruning (15 submodules -> 11)

The goal, per user direction: every `ext\*` submodule becomes a public vcpkg
port, an overlay, or nothing. Done so far, beyond Phase 1's libgit2/pcre2/zlib:

- `simpleini` (`dfe20a669`) and `Detours` (`f35d1fbf7`) -> stock vcpkg ports,
  and `ext\build\Detours.vcxproj` deleted with the second one.
- `apr` and `apr-util` -> deleted outright. They existed only for
  TortoiseMerge's vendored `libsvn_diff`, so they left with it. **They were
  never SVN interoperability** — that question came up and the answer is no.

Remaining, by how they should end:

| Submodule | Route |
| --- | --- |
| `hunspell`, `json`, `googletest`, `lexilla` | stock vcpkg ports exist; migrate |
| `OGDF` | needs an overlay port, none upstream |
| `editorconfig` | **stays** — TortoiseProc consumes it (`AppUtils.cpp`), and no vcpkg port carries this build |
| `tgit`, `spell` | permanent — `tgit` is the second git backend, `spell` is dictionary data |
| `libgit2`, `pcre2`, `zlib` | build wiring already retired; checkouts left vendored, safe to drop when convenient |

Two lessons from the first, failed attempt at this:

- **A grep over `*.vcxproj`/`*.props` does not find every consumer.** It missed
  a relative path (`..\..\..\zlib\` inside CrashServer's project) and a
  source-level `#include` reaching into a submodule's private headers. Grep the
  *sources* too.
- **A red build from removing a hidden dependency is the removal working.** Per
  user direction: "a code path that breaks abstraction so blatantly is broken by
  definition, any greenness is an unstable equilibrium." `6c9395cb4` acted on
  that — cross-project includes now resolve through `AdditionalIncludeDirectories`
  rather than `..\`, because `#include "../"` is a build dependency that the
  build system cannot see.
- **A port can differ from the hand-rolled build in what it *asserts*, not just
  what it compiles.** `3e0a0e71f`: `DarkModeHelper::AllowDarkModeForApp(FALSE)`
  detached a Detours hook that had never been attached, which is a real bug and
  always was. `ext\build\Detours.vcxproj` did not define `DETOUR_DEBUG`, so the
  library returned an error nobody read; vcpkg's debug build does, so it calls
  `DETOUR_BREAK()` and Debug `TortoiseGitProc.exe` died with STATUS_BREAKPOINT
  before showing a window. When a migrated dependency starts crashing, suspect
  a latent defect it now reports — not the migration.

**A green solution build proves less than it looks like it proves.** It found
none of `c9149bc5e`: two `CreateProcess` calls naming a deleted exe, a CI
target list, `.filters` entries pointing at deleted files, and a Settings radio
button advertising a removed application. All strings and metadata, none of
them visible to a compiler or linker. After deleting a component, grep for its
name across sources, resource scripts, project metadata and CI config — then
run the thing.

### CrashServer removed (`306aca834`, −78,112 lines)

Doctor Dump crash reporting, four solution projects. Removed because nobody
uploads dumps and nobody analyses them, so it was pure carrying cost. It also
unblocked `ext\zlib`: its `Zlib_static` project was the last consumer of the
minizip contrib sources that vcpkg's zlib port does not ship.

### TortoiseGitMerge removed (`6d3a6d4d7`, `0784142fc`)

**Delegation first, removal second** — `6d3a6d4d7` landed the fallbacks and
`0784142fc` deleted the application, so there was never a window where the
feature was simply missing. The chain in `src\TortoiseProc\AppUtils.cpp`:

- **Diff** — the user's `Software\TortoiseGit\Diff` value still wins; otherwise
  `git.exe difftool --no-prompt --no-index -- %base %mine`.
- **Merge** — the user's value wins; otherwise `merge.tool` is read and its
  `mergetool.<tool>.cmd` recipe translated (`$BASE`/`$LOCAL`/`$REMOTE`/`$MERGED`
  -> `%base`/`%mine`/`%theirs`/`%merged`); otherwise `notepad.exe %merged`.
- **Patch** — `Software\TortoiseGit\PatchViewer`, else `notepad.exe %patchfile`.
  Git has no patch *viewer* to delegate to: it applies patches, it does not
  display them.

`notepad.exe` is the floor by design, not by default: it is always present, and
users routinely redirect it through Image File Execution Options, so the
fallback stays customisable without TortoiseGit knowing. Do not "improve" this
to TortoiseGitUDiff — the more generic completion is the point.

What went with it, and what did not:

- `src\TortoiseMerge\` including the vendored `libsvn_diff` and `svninclude`,
  `GitPatch`, `PatchTest.cpp`, the `TortoiseMergeLang`, `libapr` and
  `libaprutil` projects, and the installer components and shortcuts.
- **`FileTextLines.{h,cpp}` and `EOL.h` survived**, moved to `src\Utils\`. They
  are compiled into TortoiseProc, TortoiseGitBlame and the tests — never
  TortoiseMerge-specific, just resident there.
- That move exposed a resource bug worth remembering: `FileTextLines.cpp`
  included a bare `"resource.h"`, and **MSVC resolves a quoted include relative
  to the including file first**, so the file silently bound to TortoiseMerge's
  resource ids no matter which of the three projects compiled it. Its four
  `IDS_ERR_FILE_*` strings now live in `LoglistCommonResource.h`, which in turn
  surfaced a duplicate `IDS_ERR_FILE_TOOBIG` in TortoiseProc. Same defect class
  as the `#include "../"` sweep, one layer deeper.
- **"Review/apply single patch" is gone** from the shell context menu, and so is
  TortoiseGitUDiff's "Apply Patch..." item. Both were pure launchers for the
  deleted patch UI. Applying a patch is `ImportPatch` (git am) or `git apply`;
  viewing one is TortoiseGitUDiff, which owns the `.diff`/`.patch` association.
  Rerouting them to `importpatch` was considered and rejected: `git am` creates
  commits where TortoiseGitMerge patched the working tree, and a silent
  semantic swap is worse than an honest removal.
- The Settings pages for Diff and Merge relabelled their built-in radio button
  from "TortoiseGitMerge" to "Git default", which is what it now selects.
- Retired ids are annotated, not renumbered: `TGitContextMenuEntries::ApplyPatch`
  keeps its bit because user menu masks in the registry are persisted state.

### PuTTY carved out (`bf2029b99`, `7defc0e95`, `26c13b9cf`, `ddd23a544`)

Same pattern as TortoiseMerge — delegate first, remove second — in four
buildable steps: flip the default, drop the UI, drop the binaries, drop what
the binaries needed.

**What TortoisePlink actually added was a GUI, and nothing else.**
`src\TortoisePlink\` was a 312-file vendored fork of PuTTY 0.84 carried
in-tree (not even a submodule), of which exactly three upstream files
diverged: `version.h` and `Windows\plink.c` for branding, and
`Windows\console.c` for the real change — host-key confirmation, weak-crypto
warnings and the passphrase prompt turned into `MessageBox`/`LoginDialog`,
because plink is a console program and TortoiseGitProc gives it no console.
Nothing cryptographic, nothing protocol-level.

**That gap was already closed for OpenSSH before this work started.**
`CGit::CheckMsysGitDir` sets `SSH_ASKPASS`, `GIT_ASKPASS`, `GIT_ASK_YESNO`,
`DISPLAY` and `SSH_ASKPASS_REQUIRE=force` (the last one added explicitly for
Win32-OpenSSH), pointing at the shipped `SshAskPass.exe`. The installer had
offered the choice for years. Only the *default* still pointed at plink. So
the fork was a second SSH implementation solving a problem the shipping one
already solved.

**Why the key UI went rather than being translated** (user direction,
2026-08-25: "This is a use-case engineered for vendor lock-in. Both
.ssh/config and ssh-agent provide all the needed DoF"): `remote.<name>.puttykeyfile`
was a TortoiseGit-invented config key honoured by nothing else — not git CLI,
not any other client. `IdentityFile` per `Host` in `~/.ssh/config` is
per-host rather than per-remote, needs no UI, and applies everywhere.
`ssh-agent` replaces Pageant. And Windows OpenSSH supports the `sk-` key types
(verified `ssh -Q key` on OpenSSH_for_Windows_9.5p2: `sk-ssh-ed25519@openssh.com`,
`sk-ecdsa-sha2-nistp256@openssh.com`), so key material can sit in the TPM
behind Windows Hello — a trust loop Pageant structurally cannot close.

Notes worth keeping:

- **`GIT_SSH` must name something.** `ssh-wintunnel.c` hard-errors with "No
  GIT_SSH tool configured" if it is unset, so the default changed target
  rather than disappearing.
- **plink *support* stayed.** `ssh-wintunnel.c` still translates `-P` and
  `-batch` for a plink-shaped client, because the SSH-client setting is
  free-form and someone may point it at their own PuTTY. Removing the bundled
  copy is not a reason to refuse the tool. Only the `tortoiseplink` exemption
  from `-batch` went.
- **`tgittouch` fell out.** It creates an empty file and exits; its only
  purpose was that `pageant.exe` does not block, so `LaunchPAgent` used it as
  pageant's `-c` command and polled for the file. Nothing invoked it once
  `LaunchPAgent` was gone.
- **`ext\putty` was never source** — a `download.bat` fetching signed
  `pageant.exe`/`puttygen.exe` per architecture with SHA256 checks.
- The installer's "Choose SSH Client" page stopped being a choice. It explains
  instead of asking; it should collapse entirely in Phase 5 rather than being
  rewired now, since the WiX v3 toolchain is not installed here and its Next/Back
  chain cannot be tested.

**Migration cost, stated plainly:** `.ppk` keys need a one-time conversion
(`puttygen` exports OpenSSH format), Pageant autostart entries stop meaning
anything, PuTTY saved-session names stop resolving as hostnames (use
`~/.ssh/config` `Host` entries), and PuTTY's registry host-key cache is
abandoned in favour of `~/.ssh/known_hosts`, so first contact re-confirms.
One further sharp edge: **`ssh-agent` is Disabled by default on Windows** and
needs a one-time elevated `Set-Service ssh-agent -StartupType Automatic`.
Pageant just ran. This is the one place the OpenSSH floor is lower out of
the box.

**`~/.ssh/config` now applies to TortoiseGit, and it did not before.** plink
ignores that file entirely; OpenSSH honours it. So a setting written for
interactive shells can newly break git-over-SSH from inside TortoiseGit —
`Host *` with `RequestTTY force` is the one actually hit here, which makes
every fetch fail with "PTY allocation request failed" because the pack
protocol needs a clean channel. Scope such settings to the hosts that want
them. This is a class, not one instance: `ForceCommand`, `RemoteCommand` and
`LogLevel` under `Host *` misbehave the same way.

*Upgrade path (`662b0d8df`):* an existing install keeps its SSH client
setting, and Plink was the installer default for years, so most upgrades carry
a path to a binary that no longer exists. `CGit::GetConfiguredSshClient()` is
the single reader for all three call sites and drops the value when it names
TortoiseGit's own removed plink **and** the file is missing. Do not relax that
existence check into a name-only test — TortoiseSVN ships a working
`TortoisePlink.exe`.

**Deferred, deliberately:** the TortoiseMerge manual (`doc\source\en\TortoiseMerge\`),
its `HTMLHelpfiles.wxi` components, `CheckIDD`, the `LanguagePack.wxs` entries,
the `.po` translations, and the orphaned assets under `src\Resources\`
(`TortoiseMergeENG.rc`, `.rc2`, the ribbon XML, icons). One coherent doc-and-assets
sweep, same "needs review, not a mechanical strip" bucket as the WiX x86
conditionals. Also left alone on purpose: the `Software\TortoiseGitMerge\*`
registry key *names* (`UseUTF8`, `DiffLater`, the colour keys) — those are live
user state, and renaming them loses settings.

## Known risk areas (watch list, not exhaustive)

Resolved, kept only so they are not re-investigated:

- ~~`CGitHash` reinterpret-cast corruption~~ — fixed in `a7b376d76`.
- ~~Unflagged libgit2 consumers~~ — dissolved by the vcpkg migration, which
  propagates the define through the installed `experimental.h`. See Phase 3.
- ~~`GitLogCache` on-disk format~~ — versioned in `e644fb421`.
- ~~`GIT_REV_ZERO` compared against a 64-char zero id~~ — the sentinel follows
  the object format as of `468e9c14a`.
- ~~`gitdll.c`'s independent 20-byte assumption~~ — `hashcpy` with the
  repository's algorithm, and real `oid.algo` values, as of `f8064ea8e`.

### SHA256 correctness edge cases

- **Last-open-wins on the libgit2 latch.** `g_gitObjectFormat` is latched by
  both backends —
  `CGit::CheckAndInitDll()` from `git_get_hash_algo()`, and
  `CAutoRepository::Open()` from `git_repository_oid_type()`. The second one
  matters because TGitCache and the shell extension deliberately never
  initialize gitdll (`CheckAndInitDll` even asserts under `TGITCACHE`), so
  libgit2 is the only path that ever establishes their format. The state and
  both helpers live in `src\Utils\GitObjectFormat.h` rather than next to
  `CGitHash`, because `GitWCRev` and the shell use libgit2 without `src\Git`
  on their include path.
  *Remaining sharp edge:* the libgit2 latch is last-open-wins, so opening a
  submodule (or any other repository) re-latches from it. Harmless while
  everything is SHA1, and correct for a uniform SHA256 working copy, but a
  mixed-format superproject/submodule pair would end up with whichever was
  opened last. Not handled.
- **`m_CurrentDir` is public and assigned directly in ~45 places** across
  `src\` and `test\`, and `SetCurrentDir()` does not latch the format either,
  so the invalid state (directory and format disagreeing) is still
  *representable* — just self-corrected on the next `CheckAndInitDll()`.
  Privatizing it and routing every assignment through a setter that latches is
  the follow-up that makes it unrepresentable. Measured 2026-08-22: 455
  occurrences across 106 files, so this is a real refactor, and the ~45 writes
  are not mechanically convertible — `SetCurrentDir()` performs admin-dir
  *discovery*, which most direct assignments do not want. `GetCurrentDir()` and
  `CombinePath()` give new code a const path so the count should not grow.
- **`GIT_REV_ZERO_C` is deliberately still SHA1-width and narrow** — it is
  GitWCRev's unborn-HEAD output, a separate question from the wide sentinel.
- Any raw `memcmp`/`memcpy` on oids outside `GitHash.h` — grep `GIT_OID_`,
  fixed `20`/`40` literals near hash-looking variables. Audited 2026-08-22:
  nothing in `src\` bypasses the accessors, but new code can reintroduce it.

### Build and dev-ops

- **WiX x86 conditionals** in `src\TortoiseGitSetup\{StructureFragment,Includes}.wxi`
  — deferred from Phase 0. They guard real MSI components (gitdll32.dll,
  puttygen-x86.exe, TortoiseGitStub32.dll), so this needs review rather than a
  mechanical strip.
- **TortoiseMerge doc and asset sweep** — deferred with the WiX x86 work, and
  for the same reason. The manual, help-file components, `CheckIDD`,
  `LanguagePack.wxs`, the `.po` translations and the orphaned
  `src\Resources\TortoiseMerge*` assets all still reference a deleted
  application. None of it breaks a build in scope; all of it wants one coherent
  pass rather than a grep-and-delete.
- **Phase 5, MSIX packaging** — not started; WiX v3 cannot emit MSIX at all.
- **ARM64 unverified** — this machine has no `Hostx64\arm64` cross-compiler.
  Best-effort, not a blocker.
- **sccache is shelved**, switch left in the tree and off by default. Two
  configuration faults plus one structural blocker; see the experiment report
  rather than re-deriving: precompiled headers are load-bearing for include
  correctness here, and sccache cannot cache TUs that consume a PCH.

### Test health

The suite runs **unattended and green**: **574/574** in about 175 seconds. It
was 557 passed / 29 failed and roughly 28 minutes of clicking.

574 rather than 586 because `PatchTest.cpp`'s twelve cases went with `GitPatch`
in `0784142fc`. Check that arithmetic when the number moves — a suite that
shrinks silently looks exactly like a suite that got greener.

- ~~GUI-coupled suite~~ — the one dialog that actually fired came from
  `CTortoiseGitBlameData::ParseBlameOutput()` calling `MessageBox` from a
  parser. It now returns errors to its caller and the *view* reports them.
  **The pattern, not the instance, is the lesson:** roughly 50 other
  `MessageBox`/`CMessageBox` call sites exist in code linked into `Tests.exe`,
  and any new test reaching one will block again. Library code must report
  through its caller. WinDbg (`bm user32!MessageBox*W "kb 16; g"`) finds the
  culprit in one run — far faster than reading the call sites.
- ~~Host `init.defaultBranch` leaking into fixtures~~ — six `git.exe init`
  sites now pass `-b master`. Pin at creation; do **not** rewrite assertions to
  `"main"`, which just moves the failure to differently-configured machines.
- ~~The last two failures, cause unknown~~ — both fixed in `ba1754250`, and
  neither was mysterious once isolated. `libgit2.ConfigSnaphot` was the same
  `init.defaultBranch` leak as above reaching through the *other* backend: its
  config uses `includeIf "onbranch:master"`, and `git_repository_init` honours
  `init.defaultBranch` exactly as `git.exe` does, so on a host defaulting to
  `main` the include never matched. `CTGitPath.ParserFromLog_..._UTF8` spelled
  its non-ASCII filenames as literal mojibake, which only yields the intended
  UTF-8 bytes if the compiler decodes the source as CP1252 — but
  `TGitPathTest.cpp` carries a UTF-8 BOM, so MSVC re-encoded them and the array
  held doubly-encoded bytes. They are octal escapes now, which makes the data
  independent of how the file is decoded.
  **Both are the same shape:** a test that passed only because the machine
  happened to agree with it. Pin what the test depends on, at the point the
  test creates it.

> **Do not label a failure "pre-existing" without isolating the whole feature.**
> 29 failures were described that way in this document and in commit messages,
> on the strength of a revert that backed out a single commit rather than the
> SHA256 stack. 16 of them turned out to be caused by this work — one missing
> line in `CGitHash` (see `21a8e1628`). A revert proves only what it reverts.

## Critical files

- `ext\vcpkg-ports\libgit2\` — the port overlay that replaced
  `ext\build\libgit2.vcxproj` (deleted in `f78e98aaf`). It, not a vcxproj, is
  now the source of truth for libgit2's feature flags.
- `src\Utils\GitObjectFormat.h` — the process-level object format:
  `g_gitObjectFormat`, `GitHashSize()`, `GitZeroRevString()`,
  `GitLatchObjectFormat()`. In `Utils` rather than `Git` because `GitWCRev`
  and the shell use libgit2 without `src\Git` on their include path.
- `src\Utils\SmartLibgit2Ref.h` — `CAutoRepository::Open()` is the single
  chokepoint where the libgit2 side latches the format.
- `test\UnitTests\RepositoryFixtures.h` — fixtures copy
  `resources\<name>\` in at SetUp; `git-sha256-repo` is the SHA256 one.
- `src\TortoiseGitSetup\WiXSetup.wixproj`, `TortoiseGIT.wxs` — the WiX v3
  MSI packaging project to migrate to MSIX (Phase 5); audit its
  `CustomAction`/`RegistrySearch` entries for MSIX-incompatible mechanics
  before assuming coverage.
- `src\Git\GitHash.h` — `CGitHash`, `GIT_HASH_MAX_SIZE` for fixed buffers, and
  `GIT_HASH_SIZE` as the runtime length.
- `src\TortoiseProc\AppUtils.cpp` — `StartExtDiff`/`StartExtMerge`/`StartExtPatch`
  are the single chokepoint where TortoiseGit decides which external tool runs.
  Every fallback to git lives here; nothing else should launch a diff or merge
  tool by name.
- `src\Utils\FileTextLines.{h,cpp}`, `src\Utils\EOL.h` — shared by TortoiseProc,
  TortoiseGitBlame and the tests. They live in `Utils` because they are not
  owned by any one application; they used to live in `src\TortoiseMerge\` and
  silently picked up that project's `resource.h`.
- `src\TortoiseGitSetup\StructureFragment.wxi` — what the MSI ships. The
  libgit2/pcre2/zlib DLL entries are gone; its x86 branch is the deferred Phase 0
  cleanup.
- `src\TortoiseProc\GitLogCache.h`/`.cpp` — on-disk cache format to version.
- `ext\gitdll\gitdll.c` — second, independent 20-byte hash assumption.
- `ext\libgit2\cmake\ExperimentalFeatures.cmake`, `ext\libgit2\include\git2\oid.h` — upstream reference for what the define actually changes.
