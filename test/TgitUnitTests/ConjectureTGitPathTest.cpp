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

// DRAFT, NOT YET BUILT. TDD assertions for conjectures made while untangling
// src\libgit2\modules\TGitPath.ixx (2026-09-28). Each test states the
// conjecture and its expected verdict against the code *before* the refactor:
//   FAILS   - the conjecture is a real bug; the old code violates this test
//   PASSES  - a pin of intended/quirky behaviour, or a conjecture that turned
//             out spurious; the old code already satisfies it
//   CRASHES - the old code terminates rather than failing an expectation
// "old module" means src\libgit2\modules\TGitPath.ixx at 8e2117f3e; "original"
// means src\Git\*.cpp at 8e2117f3e~1, which the refactor restored.

#include "stdafx.h"

import std;
import str_utils;
import tgittypes;
import TGitPath;

using namespace std::literals;
namespace fs = std::filesystem;

namespace
{
fs::path Root(const CAutoTempDir& dir)
{
	return fs::path(dir.GetTempDir().GetString());
}

void WriteBytes(const fs::path& file, const std::string_view bytes)
{
	fs::create_directories(file.parent_path());
	std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/// The minimum GitAdminDir::HasAdminDir accepts outside TGitCache: HEAD, config,
/// objects\ and refs\, with refs\heads a *directory* (a file means reftable).
void MakeAdminDir(const fs::path& gitDir)
{
	fs::create_directories(gitDir / "objects");
	fs::create_directories(gitDir / "refs" / "heads");
	WriteBytes(gitDir / "HEAD", "ref: refs/heads/main\n");
	WriteBytes(gitDir / "config", "");
}

template <typename Path, typename String, typename Old>
constexpr bool CanPassAPointerAsTheDirectoryFlag = requires(Path p, String s, Old* old) { p.SetFromGit(s, old); };

/// Restores the default (empty) host on scope exit, so one test's host cannot leak into another.
struct HostGuard
{
	~HostGuard() { TGitPath::InstallHost({}); }
};
}

// ---------------------------------------------------------------------------
// GitAdminDir

// Conjecture: the old module's IsAdminDirPath had its test inverted - it
// returned false as soon as "\.git" *was* found.
// Old module: FAILS. Original: PASSES.
TEST(ConjectureTGitPath, IsAdminDirPathFindsDotGit)
{
	EXPECT_TRUE(GitAdminDir::IsAdminDirPath(L"C:\\repo\\.git"));
	EXPECT_TRUE(GitAdminDir::IsAdminDirPath(L"C:\\repo\\.git\\objects"));
	EXPECT_FALSE(GitAdminDir::IsAdminDirPath(L"C:\\repo\\.gitignore"));
	EXPECT_FALSE(GitAdminDir::IsAdminDirPath(L"C:\\repo\\src"));
}

// Conjecture: the old module wrote through `found` unconditionally, and it
// defaults to null - so any call without it that reached a match crashed.
// Old module: CRASHES (only once the inversion above is fixed). Original: PASSES.
TEST(ConjectureTGitPath, IsAdminDirPathWithoutFoundPointerDoesNotDereferenceIt)
{
	EXPECT_TRUE(GitAdminDir::IsAdminDirPath(L"C:\\repo\\.git\\x"));
	const std::wstring path = L"C:\\repo\\.git\\x";
	const wchar_t* found = nullptr;
	EXPECT_TRUE(GitAdminDir::IsAdminDirPath(path, &found));
	EXPECT_EQ(path.data() + 7, found);
}

// Conjecture: the original only ever examined the *first* "\.git" (wcsstr), so
// a path that passes through ".github" first is not an admin path even when it
// ends in "\.git". A pin of that quirk - flip it if the quirk is judged a bug.
// Original: PASSES.
TEST(ConjectureTGitPath, IsAdminDirPathOnlyExaminesTheFirstDotGit)
{
	EXPECT_FALSE(GitAdminDir::IsAdminDirPath(L"C:\\.github\\x\\.git"));
}

// Conjecture: the old module's IsBareRepo had the reftable test inverted -
// refs\heads being a *directory* marked the repository invalid.
// Old module: FAILS. Original: PASSES.
TEST(ConjectureTGitPath, IsBareRepoAcceptsRefsHeadsDirectoryAndRejectsReftable)
{
	CAutoTempDir tmp;
	const fs::path bare = Root(tmp) / "bare.git";
	MakeAdminDir(bare);
	bool invalidFormat = false;
	EXPECT_TRUE(GitAdminDir::IsBareRepo(bare.wstring(), &invalidFormat));
	EXPECT_FALSE(invalidFormat);

	const fs::path reftable = Root(tmp) / "reftable.git";
	MakeAdminDir(reftable);
	fs::remove(reftable / "refs" / "heads");
	WriteBytes(reftable / "refs" / "heads", "this is a file under reftable\n");
	EXPECT_FALSE(GitAdminDir::IsBareRepo(reftable.wstring(), &invalidFormat));
	EXPECT_TRUE(invalidFormat);
}

// Conjecture: the old module replaced the UNC guard with has_root_directory()
// and fs::exists, so "\\server" was probed over the network (and fs::exists can
// throw). Only the absence of a probe distinguishes them, which this cannot see:
// the probe of a nonexistent server also answers false, just slowly.
// Old module: PASSES (the conjecture is real but not observable here).
TEST(ConjectureTGitPath, GitPathFileExistsRefusesABareUncServer)
{
	EXPECT_FALSE(GitAdminDir::GitPathFileExists(L"\\\\tgit-no-such-server"));
	EXPECT_FALSE(GitAdminDir::GitPathFileExists(L"\\\\tgit-no-such-server\\"));
}

// Conjecture: the original GetSuperProjectRoot stops at the *first* .git it
// meets, answering "" when that repository has no .gitmodules; the old module
// kept walking up and returned an outer superproject instead.
// Old module: FAILS. Original: PASSES.
TEST(ConjectureTGitPath, GetSuperProjectRootStopsAtTheFirstRepository)
{
	CAutoTempDir tmp;
	const fs::path outer = Root(tmp) / "outer";
	MakeAdminDir(outer / ".git");
	WriteBytes(outer / ".gitmodules", "");
	const fs::path inner = outer / "inner";
	MakeAdminDir(inner / ".git");
	fs::create_directories(inner / "sub");
	fs::create_directories(outer / "plain");

	EXPECT_EQ(L"", GitAdminDir::GetSuperProjectRoot((inner / "sub").wstring()));
	EXPECT_EQ(outer.wstring(), GitAdminDir::GetSuperProjectRoot((outer / "plain").wstring()));
}

// Conjecture: the old module's one-argument HasAdminDir(path) forwarded as
// HasAdminDir(path, path) - a path where the directory flag belongs.
// Old module: FAILS (or does not compile). Original: PASSES.
TEST(ConjectureTGitPath, HasAdminDirOneArgumentFindsTheWorkingTree)
{
	CAutoTempDir tmp;
	const fs::path root = Root(tmp) / "wt";
	MakeAdminDir(root / ".git");
	fs::create_directories(root / "sub");

	std::wstring top;
	EXPECT_TRUE(GitAdminDir::HasAdminDir((root / "sub").wstring(), &top));
	EXPECT_EQ(root.wstring(), top);
	EXPECT_TRUE(GitAdminDir::IsWorkingTreeOrBareRepo(root.wstring()));
}

// Conjecture: without a host, the safe.directory gate on a rooted gitdir: link
// fails *closed*. The old module had the whole gate commented out, so the link
// fell through to the "rooted path on this drive" branch and was followed -
// the NTLM-leak shape the gate exists to stop.
// Old module: FAILS. Original: PASSES (its config came from g_Git instead).
TEST(ConjectureTGitPath, ReadGitLinkRefusesRootedLinkWithoutSafeDirectory)
{
	HostGuard guard;
	TGitPath::InstallHost({});
	CAutoTempDir tmp;
	const fs::path top = Root(tmp) / "wt";
	WriteBytes(top / ".git", "gitdir: \\\\tgit-no-such-server\\share\\repo\n");
	EXPECT_EQ(L"", GitAdminDir::ReadGitLink(top.wstring(), (top / ".git").wstring()));
}

// Conjecture: with the worktree listed in safe.directory (via the host's global
// config), the same link is followed. The positive half of the test above.
// Old module: n/a (no host). Original: PASSES through g_Git's config.
TEST(ConjectureTGitPath, ReadGitLinkFollowsRootedLinkListedInSafeDirectory)
{
	HostGuard guard;
	CAutoTempDir tmp;
	const fs::path top = Root(tmp) / "wt";
	WriteBytes(top / ".git", "gitdir: \\\\tgit-no-such-server\\share\\repo\n");
	const fs::path globalConfig = Root(tmp) / "global.gitconfig";
	std::wstring safe = top.wstring();
	std::ranges::replace(safe, L'\\', L'/');
	WriteBytes(globalConfig, "[safe]\n\tdirectory = " + CUnicodeUtils::StdGetUTF8(safe) + "\n");
	TGitPath::InstallHost({ .globalConfigPath = [globalConfig] { return globalConfig.wstring(); } });

	EXPECT_EQ(L"\\\\tgit-no-such-server\\share\\repo", GitAdminDir::ReadGitLink(top.wstring(), (top / ".git").wstring()));
}

// Conjecture (unreported until now): HasStash's packed-refs scan computes
// `fileSize.LowPart - strlen("refs/stash")` in DWORD, which wraps for a file
// shorter than 10 bytes, so the bounds test passes and the scan reads past the
// buffer. Kept verbatim from the original by the refactor.
// Original: UB - passes normally, reports under AddressSanitizer.
TEST(ConjectureTGitPath, HasStashOnATinyPackedRefsDoesNotReadPastTheBuffer)
{
	CAutoTempDir tmp;
	const fs::path gitDir = Root(tmp) / ".git";
	WriteBytes(gitDir / "packed-refs", "a b\n");
	EXPECT_FALSE(GitAdminDir::HasStash(gitDir.wstring() + L"\\"));
}

// ---------------------------------------------------------------------------
// CTGitPath

// Conjecture: the old module's IsValidOnWindows compared the "file:\\" prefix
// case-sensitively; the original used StartsWithI.
// Old module: FAILS. Original: PASSES.
TEST(ConjectureTGitPath, IsValidOnWindowsFileUrlPrefixIsCaseInsensitive)
{
	EXPECT_TRUE(TGitPath::CTGitPath(L"file:\\\\C:\\dir\\x.txt").IsValidOnWindows());
	EXPECT_TRUE(TGitPath::CTGitPath(L"FILE:\\\\C:\\dir\\x.txt").IsValidOnWindows());
}

// Conjecture: the old module sorted with towlower-based CompareNoCase where the
// original used StringUtils::FastCompareNoCase (ASCII folding, then _wcsicmp).
// Likely *spurious*: both fold ASCII the same way, and in the "C" locale neither
// folds beyond it. Pins that the two orders agree in sign.
// Old module: PASSES (probably). Original: PASSES.
TEST(ConjectureTGitPath, CompareAgreesWithWcsicmp)
{
	const std::vector<std::wstring> paths = { L"C:\\a", L"C:\\B", L"C:\\_", L"C:\\ab", L"C:\\AB", L"C:\\\u00E4", L"C:\\\u00C4", L"C:\\z", L"C:\\[" };
	const auto sign = [](const int v) { return (v > 0) - (v < 0); };
	for (const auto& l : paths)
		for (const auto& r : paths)
			EXPECT_EQ(sign(_wcsicmp(l.c_str(), r.c_str())), sign(TGitPath::CTGitPath::Compare(TGitPath::CTGitPath(l), TGitPath::CTGitPath(r)))) << l << L" vs " << r;
}

// Conjecture: SetFromGit(path, &oldPath) must not compile. With a plain bool
// parameter a pointer binds by a standard conversion and the out-parameter is
// read as "is a directory" (CLAUDE.md failure mode 3). std::same_as<bool> auto
// is meant to close that for every overload at once.
// Old module: FAILS (for std::wstring first arguments the deleted poison
// overload did not apply). Original: FAILS the same way.
TEST(ConjectureTGitPath, SetFromGitCannotTakeAPointerAsTheDirectoryFlag)
{
	EXPECT_FALSE((CanPassAPointerAsTheDirectoryFlag<TGitPath::CTGitPath, std::wstring, std::wstring>));
	EXPECT_FALSE((CanPassAPointerAsTheDirectoryFlag<TGitPath::CTGitPath, const wchar_t*, std::wstring>));
	EXPECT_FALSE((CanPassAPointerAsTheDirectoryFlag<TGitPath::CTGitPath, std::wstring, int>));
}

// Conjecture: GetActionName must load its string through the host, because the
// module lives in TGitLibgit2.dll while IDS_PATHACTIONS_* live in the
// application's (translated) resources - an ATL CString::LoadString from inside
// the DLL would find neither.
// Old module: FAILS (no host; loaded via this module's CString). Original: n/a.
TEST(ConjectureTGitPath, GetActionNameLoadsThroughTheHost)
{
	HostGuard guard;
	std::vector<unsigned int> asked;
	TGitPath::InstallHost({ .loadString = [&asked](const unsigned int id) { asked.push_back(id); return std::format(L"#{}", id); } });
	const unsigned int id = TGitPath::CTGitPath::GetActionNameResourceId(TGitPath::CTGitPath::LOGACTIONS_ADDED);
	EXPECT_EQ(std::format(L"#{}", id), TGitPath::CTGitPath::GetActionName(TGitPath::CTGitPath::LOGACTIONS_ADDED));
	EXPECT_EQ(std::vector<unsigned int>{ id }, asked);
}

// Conjecture: a *relative* path of 248+ characters is made absolute through the
// host (g_Git.CombinePath) before the \\?\ prefix is added - the prefix requires
// an absolute path, and the working tree is not the process's current directory.
// Old module: does not compile (g_Git undeclared). Original: PASSES via g_Git.
TEST(ConjectureTGitPath, LongRelativePathIsResolvedThroughTheHost)
{
	HostGuard guard;
	std::vector<std::wstring> combined;
	TGitPath::InstallHost({ .combinePath = [&combined](const std::wstring_view relative) {
		combined.emplace_back(relative);
		return L"C:\\tgit-no-such-working-tree\\" + std::wstring(relative);
	} });
	const std::wstring relative = L"dir\\" + std::wstring(260, L'x');
	EXPECT_FALSE(TGitPath::CTGitPath(relative).Exists());
	ASSERT_EQ(1u, combined.size());
	EXPECT_EQ(relative, combined.front());
}

// ---------------------------------------------------------------------------
// CTGitPathList

// Conjecture: the old module's ParserFromLsFile length check had decayed into
// `if (endpos + 2 * GitHashSize() + 3)` - a constant true - so every line failed.
// Old module: FAILS. Original: PASSES.
TEST(ConjectureTGitPath, ParserFromLsFileParsesAWellFormedLine)
{
	const std::string line = "H 100644 " + std::string(40, 'a') + " 0\tdir/file.txt";
	tgit::types::BYTE_VECTOR out;
	out.append(line);
	out.push_back('\0');
	TGitPath::CTGitPathList list;
	ASSERT_EQ(0, list.ParserFromLsFile(out));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(L"dir/file.txt", list[0].GetGitPathString());
}

// Conjecture: a numstat-only entry is "known, not a directory" in the original
// (SetFromGit with an int flag of FALSE); the old module left it unknown, so the
// first IsDirectory() probed the disk.
// Old module: FAILS. Original: PASSES.
TEST(ConjectureTGitPath, ParserFromLogNumstatEntryIsKnownNotADirectory)
{
	tgit::types::BYTE_VECTOR out;
	out.append("1\t2\tg.txt"sv);
	out.push_back('\0');
	TGitPath::CTGitPathList list;
	ASSERT_EQ(0, list.ParserFromLog(out));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_TRUE(list[0].IsDirectoryKnown());
	EXPECT_FALSE(list[0].IsDirectory());
}

// Conjecture: FillUnRev with no filter runs git once, with no pathspec; the old
// module iterated an empty span and never ran it at all.
// Old module: FAILS. Original: PASSES.
TEST(ConjectureTGitPath, FillUnRevWithoutFilterRunsGitOnce)
{
	std::vector<tgit::types::STRING_VECTOR> calls;
	const TGitPath::GitRunner git = [&calls](const tgit::types::STRING_VECTOR& argv, tgit::types::BYTE_VECTOR* out, tgit::types::BYTE_VECTOR*) {
		calls.push_back(argv);
		out->append("a.txt\0"sv);
		return 0;
	};
	TGitPath::CTGitPathList list;
	ASSERT_EQ(0, list.FillUnRev(git, TGitPath::CTGitPath::LOGACTIONS_UNVER));
	ASSERT_EQ(1u, calls.size());
	EXPECT_EQ((tgit::types::STRING_VECTOR{ L"git.exe", L"ls-files", L"--exclude-standard", L"--full-name", L"--others", L"-z" }), calls[0]);
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(L"a.txt", list[0].GetGitPathString());
}

// Conjecture: with a filter, each entry gets its *own* command line; the old
// module kept appending to one argv, so the second call also carried the first
// entry's "-i -- path".
// Old module: FAILS. Original: PASSES.
TEST(ConjectureTGitPath, FillUnRevBuildsAFreshCommandPerFilterEntry)
{
	std::vector<tgit::types::STRING_VECTOR> calls;
	const TGitPath::GitRunner git = [&calls](const tgit::types::STRING_VECTOR& argv, tgit::types::BYTE_VECTOR*, tgit::types::BYTE_VECTOR*) {
		calls.push_back(argv);
		return 0;
	};
	TGitPath::CTGitPathList filter;
	filter.AddPath(TGitPath::CTGitPath(L"d1"));
	filter.AddPath(TGitPath::CTGitPath(L"d2"));
	TGitPath::CTGitPathList list;
	ASSERT_EQ(0, list.FillUnRev(git, TGitPath::CTGitPath::LOGACTIONS_IGNORE, &filter));
	ASSERT_EQ(2u, calls.size());
	EXPECT_EQ(1, std::ranges::count(calls[1], L"-i"s));
	EXPECT_EQ(0, std::ranges::count(calls[1], L"d1"s));
	EXPECT_EQ(L"d2", calls[1].back());
}

// Conjecture: LoadFromFile reproduces CStdioFile(typeBinary)::ReadString on a
// Unicode build - raw UTF-16LE, split on L'\n', and a preceding L'\r' is *kept*.
// Pins that reading; if the conjecture about MFC is wrong, this is where it shows.
// Original: PASSES if the conjecture is right.
TEST(ConjectureTGitPath, LoadFromFileKeepsTheCarriageReturnLikeCStdioFile)
{
	CAutoTempDir tmp;
	const fs::path file = Root(tmp) / "list.txt";
	const std::wstring content = L"one\r\ntwo\n";
	WriteBytes(file, std::string_view(reinterpret_cast<const char*>(content.data()), content.size() * sizeof(wchar_t)));

	TGitPath::CTGitPathList list;
	ASSERT_TRUE(list.LoadFromFile(TGitPath::CTGitPath(file.wstring())));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(L"one\r", list[0].GetGitPathString());
	EXPECT_EQ(L"two", list[1].GetGitPathString());
}

// Conjecture: WriteToFile(bUTF8 = true) never wrote UTF-8 - CStringA(wide)
// converts to the thread's ANSI code page, and typeText turns '\n' into "\r\n".
// The name lies; nothing calls it. Pins the byte format.
// Original: PASSES if the conjecture is right.
TEST(ConjectureTGitPath, WriteToFileUtf8FlagActuallyWritesAnsiWithCrlf)
{
	CAutoTempDir tmp;
	const fs::path file = Root(tmp) / "list.txt";
	TGitPath::CTGitPathList list;
	list.AddPath(TGitPath::CTGitPath(L"a\\b"));
	ASSERT_TRUE(list.WriteToFile(file.wstring(), true));
	std::ifstream in(file, std::ios::binary);
	const std::string bytes((std::istreambuf_iterator<char>(in)), {});
	EXPECT_EQ("a/b\r\n", bytes);
}
