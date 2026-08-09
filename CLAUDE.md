# TortoiseGit — libgit2 SHA256 + build de-fragmentation plan

Working plan for two intertwined changes to this repo's libgit2 integration.
Written 2026-08-03, consulted with Fable; status last validated against the
repo 2026-08-04.

**Status:** work has started and deliberately ran out of written phase order.
Landed so far (see per-phase notes below for what remains):

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
- In flight in separate sessions as of 2026-08-04, not yet merged into this
  branch — verify with `git log`/`git branch` before assuming done: the
  remaining six unflagged libgit2 consumers (Phase 3's sharpest open item)
  and the `GitWCRevStatus` hardcoded-`"master"` test fix (pre-existing bug,
  unrelated to the SHA256 work).
- (uncommitted) Phase 1 milestones 1a–1c: the vcpkg overlay port
  (`ext\vcpkg-ports\libgit2\`), and **all ten libgit2 consumers migrated**
  off `ext\build\libgit2.vcxproj` onto it (that project and its `.filters`
  are now deleted). Whole solution builds clean; `TortoiseGitProc.exe` is
  smoke-tested on SHA1 and SHA256 repos per the build+run gate, the other
  nine on build-only per the nice-to-have bar. See Phase 1 status notes for
  detail and the validation-scope policy below.

**Validation scope (set 2026-08-09, per user direction):** this migration
touches ten libgit2 consumers plus the installer and translation build. Not
all of them carry equal weight, and re-verifying all ten by hand every
milestone doesn't pay for itself. The bar going forward:

- **`TortoiseGitProc.exe` is the only target that must build *and* pass a
  runtime smoke test** (opens a SHA1 repo and a SHA256 repo without
  crashing) before a milestone counts as done.
- The other nine consumers (TortoiseMerge, TortoiseShell, TGitCache,
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

- [ ] **Phase 1 — Migrate to vcpkg (de-fragmentation).** *(Revised
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
  consumer) to actually link `pcre2-8.lib`: it already imports
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
  consumer (compiles minizip contrib sources vcpkg's zlib port doesn't ship).*

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

- [ ] **Phase 2 — Hash-size hygiene (pre-req for Phase 3, define still OFF).**
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
  member behind the unchanged public API. Still open: the `GIT_HASH_SIZE`
  call-site audit and the `GitLogCache` format-version bump
  (`LOG_INDEX_VERSION` is still `0x11`) — currently harmless because
  `GIT_HASH_SIZE` is still 20, but both must land before hash size can vary.*

- [ ] **Phase 3 — Flip `GIT_EXPERIMENTAL_SHA256`.** Define it everywhere a TU
includes `git2/oid.h` (libgit2 project, TortoiseProc, TortoiseMerge, tests).
Fix fallout. Patch `ext\libgit2` via the `git am` pattern only if the
experimental headers/sources themselves need a TortoiseGit-specific fix.
Exit: SHA1 repos fully regression-clean; libgit2-backed operations can open
a SHA256 repo.
  *Status: partially done. `a7b376d76` flipped the define for libgit2 +
  TortoiseProc + TortoiseMerge (fallout fixed: `git_index_new` gained an
  `opts` param); `df40b91a9` adds UnitTests + Cache (fallout fixed:
  `git_odb_hashfile`/`git_odb_hash` gained an oid-type param —
  `PatchTest.cpp`, `GitIndex.cpp` via a `tgit_odb_hash` shim — and
  `GitWCRev.h`'s `HeadHashReadable` buffer is now `GIT_OID_MAX_HEXSIZE`-sized
  with its "SHA2 is not available" static_assert removed). **Sharpest open
  item: the other six consumers (TortoiseShell, TGitCache, TortoiseGitBlame,
  TortoiseIDiff, GitWCRev, GitWCRevCOM) still link the flagged DLL without
  the define — a live ABI mismatch in shipping binaries** (a fix is in
  flight in a separate session as of 2026-08-04 — check `git log` before
  re-doing it). Exit criteria not met: `GIT_HASH_SIZE` is still 20, so
  SHA256 repos cannot be opened yet.*

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
  Remaining gaps: log column header + revision filter still say "SHA-1"
  (from `IDS_HASH`/`IDS_LOG_FILTER_REVS` resource strings — product call,
  churns translations); `GIT_REV_ZERO` still a 40-char literal (empty-row
  rendering is fine via `ToString()`, direct string compares would break);
  other workflows (status/commit/blame/diff dialogs) untested on SHA256;
  and see the sharpened unflagged-consumer risk below.*

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

## Known risk areas (watch list, not exhaustive)

- ~~`CGitHash` reinterpret-cast corruption~~ — fixed in `a7b376d76`.
- **Unflagged libgit2 consumers** — any project linking `libgit2_tgit.dll`
  without `GIT_EXPERIMENTAL_SHA256` sees the old 20-byte `git_oid` layout
  while the DLL uses the tagged 33-byte one: silent corruption on any oid
  crossing the boundary. Six of the ten consumers are still unflagged (see
  Phase 3 status). Converting to a static lib (Phase 1) does NOT remove this
  constraint — the define must still match per-executable. **Sharpened by
  `f8064ea8e`:** gitdll no longer refuses SHA256 repos, and `gitdll.dll` is
  shared by all consumers — but in unflagged binaries `GitHashSize()`
  compiles to a hard 20, so TortoiseShell/TGitCache/etc. would now open a
  SHA256 repo and silently truncate ids instead of failing loudly.
- `GitLogCache` on-disk cache format — needs versioning or it'll misread old
  caches as corrupt (or worse, as valid).
- `GIT_REV_ZERO` and any other 40-char hex literal compared against a
  64-char SHA256 zero id.
- `gitdll.c`'s independent 20-byte assumption (the `GitHash.h` comment
  explicitly flags it).
- Any raw `memcmp`/`memcpy` on oids outside `GitHash.h` — grep `GIT_OID_`,
  fixed `20`/`40` literals near hash-looking variables.

## Critical files

- `ext\build\libgit2.vcxproj` — the hand-maintained build to retire in favor
  of a vcpkg port overlay (see Phase 1); its `PreprocessorDefinitions` line
  is the source of truth for feature flags the overlay must replicate.
  Win32 already dropped from it.
- `src\TortoiseGitSetup\WiXSetup.wixproj`, `TortoiseGIT.wxs` — the WiX v3
  MSI packaging project to migrate to MSIX (Phase 5); audit its
  `CustomAction`/`RegistrySearch` entries for MSIX-incompatible mechanics
  before assuming coverage.
- `src\Git\GitHash.h` — `CGitHash`, the 20-byte assumption, the static_assert.
- `src\TortoiseGitSetup\StructureFragment.wxi` — MSI packaging entry for the DLL to remove.
- `src\TortoiseProc\GitLogCache.h`/`.cpp` — on-disk cache format to version.
- `ext\gitdll\gitdll.c` — second, independent 20-byte hash assumption.
- `ext\libgit2\cmake\ExperimentalFeatures.cmake`, `ext\libgit2\include\git2\oid.h` — upstream reference for what the define actually changes.
