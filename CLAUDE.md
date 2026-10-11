# TortoiseGit — working plan

## PRIME DIRECTIVE

### this was a 20y/o ball of mud application. the goal is to normalize it to 2023, so it can be a valuatble FOSS player.

#### Strings

**STL is the lingua franca.** New code and all new signatures use `std::wstring`,
`std::string`, and their `_view` types. `winrt::` and `wil::` helpers are welcome.
Do not introduce `CString` in new interfaces.

**The shim owns the MFC/ATL split.** `CString` exists only through the `str_utils`
module (`tgit::wstr::CString`, `tgit::zstr::CString`), to carry existing call sites.
- Never include `afx.h`, `afxstr.h`, or `atlstr.h` outside the shim.
- Never write `#ifdef TGIT_MFC` outside the shim. If behavior must differ between
  the MFC and non-MFC builds, add it to the shim and call that.
- `TGIT_MFC` is set per target, never per source file: the shim is compiled once
  and its choice is frozen for every importer.

**Narrow/wide is explicit.** `tgit::wstr` is wide, `tgit::zstr` is narrow.
- A file that uses one side may write `using namespace tgit::wstr;` (or `zstr`).
- A file that uses both writes no using-directives and qualifies every name
  (`wstr::CString`, `zstr::CString`). An ambiguous-`CString` error means the file
  crossed the boundary: qualify, don't work around it.
- Convert between narrow and wide only through the shim's named conversion
  functions, never through implicit `CString` constructors.

#### WIN32

for the forseable future this is a Windows app and shelx.
let's make this a fun ride with `wil`, `WinRT` and the fantastic work by STL to make the STL a livving standard.


## History

**History lives in [`doc/Remodel2026.md`](doc/Remodel2026.md)** — the commit
trail, per-phase status notes, the superseded architecture, and the full account
of every lesson stated here in one line. Consult it before re-deriving anything;
most rules below are the residue of a wrong turn recorded there.

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

### Five failure modes of a type migration — every one of which compiled

Learned converting `CTGitPath` and `PathUtils`. Re-read before converting `Git.h`:

| # | shape | why it is silent |
| --- | --- | --- |
| 1 | `a.c_str() == b.c_str()` | both sides `std::wstring` → **pointer** compare. Sweeps must skip lines containing `==` or `!=`. |
| 2 | `x.c_str() + L'.'` | `const wchar_t* + wchar_t` is **pointer arithmetic**, not concatenation. `GetMergeTempFile` was silently returning the path tail with 46 characters removed. |
| 3 | `SetFromGit(cstring, &oldPath)` | **re-bound to a different overload.** A CString can no longer reach the `wstring_view` overload, and pointer→`bool` is a *standard* conversion, so it fell through to `SetFromGit(const wchar_t*, bool)` and read the out-parameter as a directory flag. No error, and no diff to review — the line never changed, only the overload set around it. |
| 4 | `return MAKEINTRESOURCE(IDS_…)` | `CStringT`'s constructor checks `IS_INTRESOURCE` and calls `LoadString`; **`std::wstring` hands the pseudo-pointer to `wcslen`**. Compiled, linked, passed 587 tests, survived an adversarial diff review, then crashed painting the first Action cell. |
| 5 | `std::wstring(parser.GetVal(L"path2"))` | **a total constructor became a partial one.** `CString(nullptr)` is a defined empty string; `std::wstring(nullptr)` is UB. Nothing at the call site changed shape and no overload re-bound — only the set of accepted values shrank, and the removed value (`GetVal` returns `nullptr` for an absent key) is the one the parser returns most often. Crashed **every** `TortoiseGitProc /command:diff`, which reaches for an optional `/path2` unconditionally. |

(3), (4) and (5) share a moral worth stating plainly: **changing a type does not only
break call sites loudly.** It can re-bind a call to a different overload, or
change what a constructor *means*, and in every case no line of the diff changed,
so no review can see it. Only the test suite caught (3); only running the
program caught (4) and (5) — (5) survived a full green suite for a week,
because nothing in `test\` launches a command by its command line. **Where a
conversion narrows what a parameter accepts, the sweep is over the callers'
*values*, not their spellings**: grep the new type's constructor against every
API that can hand it a null (`CCmdLineParser::GetVal` is the one here, and
`GetValStr` now exists so no call site has to spell that trap again). **`std::format` is the fix for the whole of (2)** — each
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
`tgit::wstr::StringView(x)` for a view parameter and `std::wstring(x)` (direct-init —
copy-init would need two user-defined conversions) for an owning one. Making the
core's parameters `LPCWSTR` instead would erase most of those edits and was
rejected for exactly that reason: a CString converts to `LPCWSTR` implicitly, so
the boundary would compile silently and nothing would ever pressure it to move.

**Do not remove the `#define CSTRING_AVAILABLE` lines** from
`src\TGitCache\stdafx.h`, `src\TortoiseShell\stdafx.h` and `test\Cache\stdafx.h`.
`StringUtils.h`, `CmdLineParser.h`, `SmartLibgit2Ref.h`, `UnicodeUtils.h` and
`WideString.h` still gate on it; only PathUtils' own `#ifdef` died.

### The three backends, and why `LIBGIT2_CMD` is shrinking

TortoiseGit reaches git three ways: `git.exe` as a subprocess, `gitdll.dll`
(real git code, gated by the single bool `m_IsUseGitDLL`), and libgit2 (gated
per operation by `CGit::UsingLibGit2(LIBGIT2_CMD)` against a registry bitmask).

**`LIBGIT2_CMD` was a duplication mechanism, not a replacement one.** Because
the mask is a *runtime* toggle, every gated operation carried a libgit2
implementation *and* a `git.exe` one forever; widening the enum could only ever
add code. **"libgit2 is the app's pathway, freeform cli calls are always
available for users"** (user, 2026-08-28) is the direction: where libgit2 does
the job, the `git.exe` twin goes.

Ten values lost their fallback in `2d61fbb07` and live in `LIBGIT2_ONLY_MASK`,
for which `UsingLibGit2` answers true unconditionally. Six survive, and the
reasons are the taxonomy to apply to anything new:

| survivor | why |
| --- | --- |
| `GETONEFILE`, `GET_COMMIT`, `LOGLISTDIFF` | the other branch is **gitdll**, not `git.exe`. gitdll is a backend to use *more* of, so these keep their fork. |
| `FOREACHREF` | the `git.exe` path is the **primary** implementation when Browse References filters on merge status. Not a fallback. |
| `CLONE`, `FETCH`, `RESET`, `COMMIT_UPDATE_INDEX` | they select between two whole **dialogs**, not two implementations of one function. |

`FETCH` also pins `CGit::GetRemoteRefs`: that one function *could* go
libgit2-only, but the enum value is still switchable elsewhere, and
`GetGitLastErr(msg, cmd)` would then report the wrong side's error.

**The enum has a second job**, which is why it survives its gates:
`GetGitLastErr(msg, cmd)` uses it at 36 call sites to choose between libgit2's
error and `gitLastErr`. **That is the machinery step, and it is separate**:
when `LIBGIT2_ONLY_MASK` has swallowed the enum, `UsingLibGit2`, the registry
mask and that parameter go in one move — and two of the four
`RepositoryFixtures` parameters collapse with them.

