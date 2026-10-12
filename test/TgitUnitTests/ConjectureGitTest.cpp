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

// DRAFT, NOT YET BUILT. TDD assertions for conjectures about the git layer
// (git_h / Git.cpp), SmartLibgit2 and the wil shim (2026-09-28).
//   FAILS - a real bug; PASSES - a pin, or a spurious conjecture;
//   UB    - undefined behaviour: nondeterministic, reliable under ASan.

#include "stdafx.h"

import std;
import wil;
import str_utils;
import invarients;
import SmartLibgit2;
import git_h;

using namespace std::literals;
namespace fs = std::filesystem;

// Defined at global scope in src\libgit2\Git\Git.cpp; not exported by any module.
DWORD GetTortoiseGitTempPath(DWORD nBufferLength, LPWSTR lpBuffer);
CString FindFileOnPath(const CString& filename, LPCWSTR env, bool wantDirectory);

// ---------------------------------------------------------------------------
// ArgvData

// Conjecture: VectorToARGV reserved room for argc + 1 pointers but never stored
// them, and handed gitdll an argv without the argv[argc] == NULL that git's own
// main() guarantees. (The block copy also took &block - the unique_ptr's own
// address - and could not have compiled.)
// Old code: does not compile; the design it aimed at: FAILS the terminator check.
TEST(ConjectureGit, ArgvDataIsNullTerminatedAndPointsAtItsOwnStrings)
{
	auto argv = TGitH::ArgvData::VectorToARGV({ "", "-r", "--stat" });
	ASSERT_TRUE(static_cast<bool>(argv));
	ASSERT_EQ(3, argv.argc());
	EXPECT_STREQ("", argv.argv()[0]);
	EXPECT_STREQ("-r", argv.argv()[1]);
	EXPECT_STREQ("--stat", argv.argv()[2]);
	EXPECT_EQ(nullptr, argv.argv()[3]);
}

// Conjecture: moving an ArgvData keeps its pointers valid - a moved std::vector
// hands over its buffer, so the std::string objects (SSO characters included)
// do not move - while copying must be impossible, as a copy's pointers would
// still point into the original.
TEST(ConjectureGit, ArgvDataIsMoveOnlyAndMovingKeepsPointersValid)
{
	EXPECT_FALSE(std::is_copy_constructible_v<TGitH::ArgvData>);
	EXPECT_TRUE(std::is_move_constructible_v<TGitH::ArgvData>);

	auto original = TGitH::ArgvData::VectorToARGV({ "", "short", std::string(100, 'x') });
	const char* const* before = original.argv();
	const std::vector<const char*> pointers(before, before + original.argc());
	TGitH::ArgvData moved(std::move(original));
	for (int i = 0; i < moved.argc(); ++i)
		EXPECT_EQ(pointers[static_cast<size_t>(i)], moved.argv()[i]) << "argv[" << i << "] moved";
	EXPECT_STREQ("short", moved.argv()[1]);
}

// ---------------------------------------------------------------------------
// Git.cpp helpers

// Conjecture: GetTortoiseGitTempPath keeps the Win32 two-call contract - with
// no buffer it returns the size needed, and with that size it fills in
// "<temp>\TortoiseGit\". (The 13 it used to spell out is "TortoiseGit\" + NUL.)
TEST(ConjectureGit, GetTortoiseGitTempPathTwoCallContract)
{
	const DWORD needed = GetTortoiseGitTempPath(0, nullptr);
	ASSERT_GT(needed, 13u);
	std::wstring buffer(needed, L'?');
	const DWORD result = GetTortoiseGitTempPath(needed, buffer.data());
	EXPECT_EQ(needed, result);
	buffer.resize(wcslen(buffer.c_str()));
	EXPECT_TRUE(buffer.ends_with(L"\\TortoiseGit\\")) << buffer;
	EXPECT_EQ(needed - 1, buffer.size());

	std::wstring tooSmall(needed - 1, L'?');
	EXPECT_EQ(needed, GetTortoiseGitTempPath(needed - 1, tooSmall.data()));
	EXPECT_EQ(L'\0', tooSmall[0]);
}

// Conjecture: FindFileOnPath stops at the first *empty* PATH entry - a quirk of
// `while (... && *buf)` - and honours "quoted" entries. Pins both.
// Original: PASSES.
TEST(ConjectureGit, FindFileOnPathStopsAtAnEmptyEntryAndHonoursQuotes)
{
	wchar_t system32[MAX_PATH] = {};
	ASSERT_NE(0u, GetSystemDirectoryW(system32, MAX_PATH));
	const std::wstring dir = system32;

	const std::wstring quoted = L"\"" + dir + L"\";C:\\tgit-nope";
	EXPECT_STREQ((dir + L"\\notepad.exe").c_str(), FindFileOnPath(L"notepad.exe", quoted.c_str(), false));
	EXPECT_STREQ((dir + L"\\").c_str(), FindFileOnPath(L"notepad.exe", quoted.c_str(), true));

	const std::wstring emptyFirst = L"C:\\tgit-nope;;" + dir;
	EXPECT_STREQ(L"", FindFileOnPath(L"notepad.exe", emptyFirst.c_str(), false));
}

// ---------------------------------------------------------------------------
// CGitCallCb

namespace
{
using LineReceiver = std::function<void(std::string_view)>;

struct LimitedCall : TGitH::CGitCallCb<LineReceiver>
{
	LimitedCall(LineReceiver receiver, const size_t limit)
		: CGitCallCb(L"", std::move(receiver))
	{
		m_maxLength = limit;
	}
};
}

// Conjecture: OnOutputData builds std::istringstream(m_buffer.data()) - a
// const char* constructor, which reads to a NUL - from a std::vector<char> that
// has no NUL after its last byte. Present before the module port too.
// Old and current code: UB - the lines usually come out right, until the
// allocation happens to be followed by non-NUL bytes; ASan reports it every time.
TEST(ConjectureGit, CallbackOutputIsSplitWithoutReadingPastTheChunk)
{
	std::vector<std::string> lines;
	LimitedCall call([&lines](const std::string_view line) { lines.emplace_back(line); }, std::numeric_limits<size_t>::max());
	std::string chunk(1000, 'a');
	chunk[500] = '\n';
	EXPECT_FALSE(call.OnOutputData(chunk));
	ASSERT_EQ(2u, lines.size());
	EXPECT_EQ(500u, lines[0].size());
	EXPECT_EQ(499u, lines[1].size());
}

// Conjecture: the byte limit lets the chunk that crosses it through and stops
// the next one; the rewrite clamps the count instead of SizeTAdd-checking it.
// Both behave the same for any real input (a wrap needs 2^64 bytes), so this
// pins the observable half. Old and current code: PASSES.
TEST(ConjectureGit, CallbackLimitLetsTheCrossingChunkThroughThenStops)
{
	std::vector<std::string> lines;
	LimitedCall call([&lines](const std::string_view line) { lines.emplace_back(line); }, 4);
	EXPECT_FALSE(call.OnOutputData("abcdef"sv));
	EXPECT_TRUE(call.wasLimited());
	EXPECT_TRUE(call.OnOutputData("x"sv));
	EXPECT_EQ(std::vector<std::string>{ "abcdef" }, lines);
}

// ---------------------------------------------------------------------------
// SmartLibgit2 and wil

// Conjecture: the hand-rolled CAuto* classes re-exposed their base's conversion
// operator with a using-declaration that MSVC rejects across a module boundary
// (C2248); the wil aliases replace the implicit conversion with get(), so a raw
// pointer never escapes by accident. Old classes: FAILS (they converted, where
// they compiled at all).
TEST(ConjectureGit, LibGit2OwnersDoNotConvertImplicitly)
{
	EXPECT_FALSE((std::is_convertible_v<wil::unique_git_repository, git_repository*>));
	EXPECT_FALSE((std::is_convertible_v<wil::unique_git_config, git_config*>));
	EXPECT_TRUE((std::is_convertible_v<wil::unique_git_buf*, git_buf*>)); // unique_struct *is* the struct
	wil::unique_git_repository empty;
	EXPECT_FALSE(empty.is_valid());
}

// Conjecture: OpenGitRepository - what CAutoRepository::Open did beyond owning
// the handle - latches the repository's object format; for TGitCache and the
// shell it is the only place that ever happens.
TEST(ConjectureGit, OpenGitRepositoryLatchesSha256)
{
	CAutoTempDir tmp;
	const std::string path = CUnicodeUtils::StdGetUTF8(std::wstring_view(tmp.GetTempDir().GetString()));
	git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
	options.oid_type = GIT_OID_SHA256;
	git_repository* created = nullptr;
	ASSERT_EQ(0, git_repository_init_ext(&created, path.c_str(), &options));
	git_repository_free(created);

	tgit::invarients::LatchObjectFormat(tgit::invarients::GitObjectFormat::SHA1);
	const auto repo = wil::OpenGitRepository(path.c_str());
	ASSERT_TRUE(repo.is_valid());
	EXPECT_TRUE(tgit::invarients::Is256());
	tgit::invarients::LatchObjectFormat(tgit::invarients::GitObjectFormat::SHA1); // process-level state: put it back
}

// Conjecture: a *default-constructed* wil::unique_close_clipboard_call calls
// CloseClipboard when destroyed (unique_call's default_value is true), which is
// why open_clipboard_retrying must return an explicit {false} on failure.
// A pin of wil's behaviour: PASSES if the conjecture about wil is right.
TEST(ConjectureGit, DefaultConstructedClipboardCallWouldClose)
{
	wil::unique_close_clipboard_call call;
	const bool wouldClose = static_cast<bool>(call);
	call.release(); // do not actually close anything
	EXPECT_TRUE(wouldClose);
	EXPECT_FALSE(static_cast<bool>(wil::unique_close_clipboard_call{ false }));
}
