# TortoiseGit patches in this overlay

The `tortoisegit-*.diff` files are regenerated from the `git am` series kept at
`ext/libgit2-*.patch`. They exist separately because vcpkg applies patches with
plain `git apply` (see `scripts/cmake/z_vcpkg_apply_patches.cmake`), which has
no `--3way` fallback, whereas the `ext/libgit2-*.patch` series only applies to
stock v1.9.4 with `git am --3way`.

## Series order

The five upstream-format patches must be applied in this order:

1. `libgit2-Add-support-for-wildcard-in-safe-directory.patch`
2. `libgit2-repo-Guess-better-path-to-set-an-exception-for.patch`
3. `libgit2-repo-simplify-safe.directory-checks.patch`
4. `libgit2-Extend-safe.directory-admin-owner-check-to-also-cove.patch`
5. `libgit2-Use-explicit-casts-for-implicit-enum-type-conversions.patch`

Note that this is **not** alphabetical order. `build.txt` documents applying them
with `for %%G in (..\libgit2-*.patch)`, which enumerates them alphabetically and
therefore runs 3 before 2 — that ordering leaves conflict markers in
`src/libgit2/repository.c` even with `--3way`. Patches 4 and 5 touch disjoint
files and can go anywhere in the sequence.

## Mapping to the diffs here

| diff | source patches | files touched |
| --- | --- | --- |
| `tortoisegit-safe-directory.diff` | 1, 2, 3 | `src/libgit2/repository.c` |
| `tortoisegit-admin-owner-check.diff` | 4 | `src/util/fs_path.c` |
| `tortoisegit-enum-casts.diff` | 5 | `src/libgit2/diff_generate.h`, `src/libgit2/submodule.c` |

Patches 1–3 are collapsed into one diff because all three edit the same
`validate_ownership_cb` region; 3 is a refactor of the code 1 introduces, so they
are not separable as context diffs.

## Patches authored here

These have no `ext/libgit2-*.patch` ancestor; they were written against the
extracted source and are maintained as ordinary context diffs.

| diff | files touched | why |
| --- | --- | --- |
| `tortoisegit-no-experimental-rename.diff` | `cmake/ExperimentalFeatures.cmake` | stop `EXPERIMENTAL` renaming the library *and* the installed header directory |
| `tortoisegit-reverse-workdir-oid.diff` | `src/libgit2/diff_generate.c` | `GIT_DIFF_REVERSE` on a working-copy diff emitted `index 0000000..` and `--- /dev/null` |

`tortoisegit-reverse-workdir-oid.diff` is an upstream bug, not a TortoiseGit
preference. `diff_delta__from_two` marks `old_file` `GIT_DIFF_FLAG_VALID_ID`
unconditionally, while `new_file` two lines below only gets it when its id is
non-zero. A working-copy entry's id *is* zero until the content is loaded — the
loader fills it in, but only for a file that is not already claiming a valid id.
Ordinarily the workdir side lands in `new_file` and the asymmetry never shows;
under `GIT_DIFF_REVERSE` the swap immediately above puts it in `old_file`, and
the header comes out naming a null blob, which `diff_print.c` then renders as
`/dev/null`. The patch applies the `new_file` test to `old_file` as well.

This is what `CGit::GetUnifiedDiff` needs for the `-R` case (a working copy
diffed as the *old* side), and it is the one thing on the "Donate upstream" list
that is a straight bug fix with no TortoiseGit flavour to it.

## Regenerating after a libgit2 bump

```bash
git -C ext/libgit2 worktree add --detach /tmp/lg2wt v<NEW_VERSION>
cd /tmp/lg2wt
for n in Add-support-for-wildcard-in-safe-directory \
         repo-Guess-better-path-to-set-an-exception-for \
         repo-simplify-safe.directory-checks \
         Extend-safe.directory-admin-owner-check-to-also-cove \
         Use-explicit-casts-for-implicit-enum-type-conversions; do
  git apply --3way "$OLDPWD/ext/libgit2-$n.patch"
done
# resolve any conflicts, then:
git diff HEAD -- src/libgit2/repository.c > .../tortoisegit-safe-directory.diff
git diff HEAD -- src/util/fs_path.c > .../tortoisegit-admin-owner-check.diff
git diff HEAD -- src/libgit2/diff_generate.h src/libgit2/submodule.c > .../tortoisegit-enum-casts.diff
```

Keep the output LF-only; `git apply` in vcpkg runs with `core.autocrlf=false`.

## Upstreaming

This overlay is a temporary bridge. Each diff should be pushed upstream and
dropped from here once it lands; `tortoisegit-enum-casts.diff` (MSVC C5287
warnings) and `tortoisegit-admin-owner-check.diff` look independently
upstreamable.
