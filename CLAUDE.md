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
- Uncommitted follow-up (this session): same define added to
  `test\UnitTests\UnitTests.vcxproj` and `test\Cache\Cache.vcxproj` (they
  link the flagged libgit2 DLL — without the define they'd be an ABI
  mismatch), plus their leftover Win32 configs removed.

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

- [ ] **Phase 1 — Static-lib conversion (de-fragmentation).** Convert
`ext\build\libgit2.vcxproj` from `DynamicLibrary` to `StaticLibrary`,
matching the existing `libgit.vcxproj` pattern. Keep it in `ext\build\`
(don't fork the 180-file list across TortoiseProc and TortoiseMerge — that
would double the maintenance burden the restructuring is meant to reduce).
Both exes keep their `ProjectReference`; the linker drops unused objects per
exe. Remove `libgit2_tgit.dll` from `StructureFragment.wxi`. Grep `src\` and
installer scripts for any remaining path/LoadLibrary reference to the DLL
name. Exit: x64/ARM64 build, installer has no libgit2 DLL, app smoke-tests
pass, behavior unchanged.

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
  `opts` param); the uncommitted follow-up adds UnitTests + Cache (fallout
  fixed: `git_odb_hashfile`/`git_odb_hash` gained an oid-type param —
  `PatchTest.cpp`, `GitIndex.cpp` via a `tgit_odb_hash` shim — and
  `GitWCRev.h`'s `HeadHashReadable` buffer is now `GIT_OID_MAX_HEXSIZE`-sized
  with its "SHA2 is not available" static_assert removed). **Sharpest open
  item: the other six consumers (TortoiseShell, TGitCache, TortoiseGitBlame,
  TortoiseIDiff, GitWCRev, GitWCRevCOM) still link the flagged DLL without
  the define — a live ABI mismatch in shipping binaries.** Exit criteria not
  met: `GIT_HASH_SIZE` is still 20, so SHA256 repos cannot be opened yet.*

- [ ] **Phase 4 — App-level SHA256 UX + gitdll.** 64-char hash display/parsing
throughout the UI, `GIT_REV_ZERO` (currently a 40-char literal) needs a
64-char counterpart, gitdll/`ext\tgit` backend support (real git already has
`the_hash_algo` internally — a later step can surface it through
`gitdll.c`, which today shares the 20-byte assumption but not libgit2's
`git_oid` type). Mark the feature experimental in the UI. Exit: core
workflows (log, status, commit, blame, diff) work end-to-end on a SHA256
test repo.

## Known risk areas (watch list, not exhaustive)

- ~~`CGitHash` reinterpret-cast corruption~~ — fixed in `a7b376d76`.
- **Unflagged libgit2 consumers** — any project linking `libgit2_tgit.dll`
  without `GIT_EXPERIMENTAL_SHA256` sees the old 20-byte `git_oid` layout
  while the DLL uses the tagged 33-byte one: silent corruption on any oid
  crossing the boundary. Six of the ten consumers are still unflagged (see
  Phase 3 status). Converting to a static lib (Phase 1) does NOT remove this
  constraint — the define must still match per-executable.
- `GitLogCache` on-disk cache format — needs versioning or it'll misread old
  caches as corrupt (or worse, as valid).
- `GIT_REV_ZERO` and any other 40-char hex literal compared against a
  64-char SHA256 zero id.
- `gitdll.c`'s independent 20-byte assumption (the `GitHash.h` comment
  explicitly flags it).
- Any raw `memcmp`/`memcpy` on oids outside `GitHash.h` — grep `GIT_OID_`,
  fixed `20`/`40` literals near hash-looking variables.

## Critical files

- `ext\build\libgit2.vcxproj` — the project to convert to StaticLibrary and drop Win32 from.
- `src\Git\GitHash.h` — `CGitHash`, the 20-byte assumption, the static_assert.
- `src\TortoiseGitSetup\StructureFragment.wxi` — MSI packaging entry for the DLL to remove.
- `src\TortoiseProc\GitLogCache.h`/`.cpp` — on-disk cache format to version.
- `ext\gitdll\gitdll.c` — second, independent 20-byte hash assumption.
- `ext\libgit2\cmake\ExperimentalFeatures.cmake`, `ext\libgit2\include\git2\oid.h` — upstream reference for what the define actually changes.
