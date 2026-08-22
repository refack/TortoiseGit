// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2026 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#pragma once
#include <git2/oid.h>

/*
 * Which object format the repository this process works on uses.
 *
 * TortoiseGit runs one process per working copy, so this is process-level state
 * rather than something carried per hash. It has to be latched from the repository
 * before any object id is read or rendered, or ids get sized for the wrong
 * algorithm and are silently truncated or padded.
 *
 * There are two backends and both latch it, because either can be the first (or
 * only) one to open a repository:
 *   - gitdll  -- CGit::CheckAndInitDll(), from git_get_hash_algo()
 *   - libgit2 -- CAutoRepository::Open(), from git_repository_oid_type()
 * TGitCache and the shell extension deliberately never initialize gitdll, so for
 * them the libgit2 path is the only one that ever runs.
 *
 * This lives in Utils rather than next to CGitHash because GitWCRev and the shell
 * use libgit2 without having src\Git on their include path, and because it must
 * not depend on CString.
 */

/* keep the values in sync with git's hash_algo_by_ptr(), see gitdll.c's git_get_hash_algo() */
enum class GitObjectFormat : int
{
	SHA1 = 1,
	SHA256 = 2,
};

inline GitObjectFormat g_gitObjectFormat = GitObjectFormat::SHA1;

/* Number of raw bytes an object id currently occupies; always <= GIT_HASH_MAX_SIZE. */
inline int GitHashSize() noexcept
{
#ifdef GIT_EXPERIMENTAL_SHA256
	return g_gitObjectFormat == GitObjectFormat::SHA256 ? GIT_OID_SHA256_SIZE : GIT_OID_SHA1_SIZE;
#else
	// without libgit2's experimental headers git_oid cannot hold a SHA256 id at all
	return GIT_OID_SHA1_SIZE;
#endif
}

/* Display name of the active object format, for UI labels. */
inline const wchar_t* GitObjectFormatName() noexcept
{
	return GitHashSize() == GIT_OID_SHA1_SIZE ? L"SHA-1" : L"SHA-256";
}

/*
 * Hex string of the all-zero object id in the active format.
 *
 * TortoiseGit uses it as a sentinel meaning "the working copy" rather than a real
 * revision - see GitRev::GetWorkingCopyRef(), which is what call sites should use.
 * It has to be the same width as the ids it gets compared against: a 40-character
 * sentinel never equals the 64-character zero id a SHA256 repository produces, the
 * "is this the working copy" test then fails, and the value is passed on to git as
 * if it were a real revision, which answers "fatal: bad object 000...".
 */
inline const wchar_t* GitZeroRevString() noexcept
{
	static constexpr wchar_t zeroSha1[] = L"0000000000000000000000000000000000000000";
	static constexpr wchar_t zeroSha256[] = L"0000000000000000000000000000000000000000000000000000000000000000";
	static_assert(sizeof(zeroSha1) / sizeof(zeroSha1[0]) - 1 == 2 * GIT_OID_SHA1_SIZE, "the SHA1 zero id must be exactly 40 hex characters");
	static_assert(sizeof(zeroSha256) / sizeof(zeroSha256[0]) - 1 == 64, "the SHA256 zero id must be exactly 64 hex characters");
	return GitHashSize() == GIT_OID_SHA1_SIZE ? zeroSha1 : zeroSha256;
}

/*
 * Record the format of a repository that was just opened. Takes libgit2's oid type
 * so the libgit2 side does not have to know about git's hash_algo indices.
 */
inline void GitLatchObjectFormat(git_oid_t oidType) noexcept
{
#ifdef GIT_EXPERIMENTAL_SHA256
	g_gitObjectFormat = (oidType == GIT_OID_SHA256) ? GitObjectFormat::SHA256 : GitObjectFormat::SHA1;
#else
	// only SHA1 is representable without the experimental headers; ignore whatever we were told
	static_cast<void>(oidType);
	g_gitObjectFormat = GitObjectFormat::SHA1;
#endif
}
