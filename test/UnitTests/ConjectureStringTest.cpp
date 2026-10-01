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

// DRAFT, NOT YET BUILT. TDD assertions for conjectures about the string layer:
// wstr, StringUtils, PathUtils and invarients (2026-09-28). Verdicts are against
// the code before the change that each test's conjecture led to:
//   FAILS - a real bug; PASSES - a pin, or a spurious conjecture;
//   HANGS - the old code does not terminate (the test bounds it).

#include "stdafx.h"

import std;
import wstr;
import StringUtils;
import PathUtils;
import invarients;

using namespace std::literals;

// ---------------------------------------------------------------------------
// wstr

// Conjecture: tgit::wstr::Replace advances by the replacement's length and
// replaces that many characters, not the match's - so a shorter replacement
// (removing "\\?\" is the one CTGitPath needed) never terminates.
// Current wstr: HANGS. The test runs it on a detached thread so a hang is a
// failure, not a stuck suite - but a hung thread keeps spinning afterwards.
TEST(ConjectureString, WstrReplaceWithShorterReplacementTerminates)
{
	std::promise<std::wstring> result;
	auto done = result.get_future();
	std::thread([promise = std::move(result)]() mutable {
		std::wstring s = L"\\\\?\\C:\\x";
		tgit::wstr::Replace(s, L"\\\\?\\"sv, L""sv);
		promise.set_value(std::move(s));
	}).detach();
	ASSERT_EQ(std::future_status::ready, done.wait_for(2s)) << "Replace did not terminate";
	EXPECT_EQ(L"C:\\x", done.get());
}

// Conjecture: the same mismatch with a *longer* replacement overwrites
// characters after the match: replacing "-" with "--" in "a-b" consumes the "b".
// Current wstr: FAILS ("a--").
TEST(ConjectureString, WstrReplaceWithLongerReplacementKeepsTheRest)
{
	std::wstring s = L"a-b-c";
	tgit::wstr::Replace(s, L"-"sv, L"--"sv);
	EXPECT_EQ(L"a--b--c", s);
}

// Conjecture: IsRealString was `requires { a || b || ... }`, which only checks
// that the disjunction *compiles* - so the only real requirement was a nested
// value_type, and containers qualified.
// Old concept: FAILS (vector and optional satisfied it).
TEST(ConjectureString, IsRealStringMeansStdStringsOnly)
{
	EXPECT_TRUE(tgit::wstr::IsRealString<std::wstring>);
	EXPECT_TRUE(tgit::wstr::IsRealString<std::string>);
	EXPECT_TRUE(tgit::wstr::IsRealString<std::wstring_view>);
	EXPECT_FALSE(tgit::wstr::IsRealString<std::vector<int>>);
	EXPECT_FALSE(tgit::wstr::IsRealString<std::vector<wchar_t>>);
	EXPECT_FALSE(tgit::wstr::IsRealString<std::optional<int>>);
	EXPECT_FALSE(tgit::wstr::IsRealString<const wchar_t*>);
}

// Conjecture: the two concepts must be disjoint, or StringData/StringSize are
// ambiguous for a type in both.
// Old concept: PASSES for CString (it has no value_type), but only by luck.
TEST(ConjectureString, RealAndStupidStringsAreDisjoint)
{
	EXPECT_TRUE(tgit::wstr::IsStupidString<CStringW>);
	EXPECT_TRUE(tgit::wstr::IsStupidString<CStringA>);
	EXPECT_FALSE(tgit::wstr::IsRealString<CStringW>);
	EXPECT_FALSE(tgit::wstr::IsStupidString<std::wstring>);
}

// Conjecture: one StringView over concept-specific getters serves both kinds;
// two class templates differing only by constraint were a redeclaration.
// Old code: does not compile. Includes an embedded NUL, which a view built from
// a const wchar_t* would cut.
TEST(ConjectureString, StringViewServesBothKinds)
{
	const CStringW cstring(L"a\0b", 3);
	EXPECT_EQ(3u, tgit::wstr::StringView{ cstring }.size());
	EXPECT_EQ(L"a\0b"sv, std::wstring_view{ tgit::wstr::StringView{ cstring } });
	const std::wstring stdString = L"cd";
	EXPECT_EQ(L"cd"sv, std::wstring_view{ tgit::wstr::StringView{ stdString } });
	const CStringA narrow("ef");
	EXPECT_EQ("ef"sv, std::string_view{ tgit::wstr::StringView{ narrow } });
}

// Conjecture: CopyTruncating keeps StringCchCopy's contract - truncate, always
// terminate, report whether it fit - which the wcscpy_s calls it replaced did
// not (they went to the invalid-parameter handler instead).
TEST(ConjectureString, CopyTruncatingTruncatesAndTerminates)
{
	wchar_t buffer[4] = { L'x', L'x', L'x', L'x' };
	EXPECT_TRUE(tgit::wstr::CopyTruncating(buffer, L"abc"));
	EXPECT_EQ(L"abc"sv, std::wstring_view(buffer));
	EXPECT_FALSE(tgit::wstr::CopyTruncating(buffer, L"abcd"));
	EXPECT_EQ(L"abc"sv, std::wstring_view(buffer));
	EXPECT_TRUE(tgit::wstr::CopyTruncating(buffer, L""));
	EXPECT_EQ(L'\0', buffer[0]);
	EXPECT_FALSE(tgit::wstr::CopyTruncating(std::span<wchar_t>(), L"a"));
}

// Conjecture: the module port of LoadStringEx stepped from bImage (the start of
// the string block) instead of from the current entry, so every string past
// the second in a block of 16 came from the wrong place. Differential against
// ::LoadStringW over user32's own string table, which has many full blocks.
// Old port: FAILS for any id with (id & 0xf) >= 2 whose block has varied lengths.
TEST(ConjectureString, LoadStringExAgreesWithLoadString)
{
	const HINSTANCE module = GetModuleHandleW(L"user32.dll");
	ASSERT_NE(nullptr, module);
	int compared = 0;
	for (UINT id = 1; id < 4096; ++id)
	{
		const wchar_t* expected = nullptr;
		const int expectedLength = ::LoadStringW(module, id, reinterpret_cast<LPWSTR>(&expected), 0);
		if (expectedLength <= 0)
			continue;
		wchar_t actual[1024] = {};
		const int actualLength = CUnicodeUtils::LoadStringEx(module, id, actual, static_cast<int>(std::size(actual)), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
		const auto want = std::wstring_view(expected, static_cast<size_t>(expectedLength)).substr(0, std::size(actual) - 1);
		EXPECT_EQ(want, std::wstring_view(actual, static_cast<size_t>(actualLength))) << "id " << id;
		++compared;
	}
	EXPECT_GT(compared, 16) << "user32 exposed too few strings for this to mean anything";
}

// ---------------------------------------------------------------------------
// StringUtils

// Conjecture: PipesToNulls(buffer) - a default argument of length 0 on the
// two-argument form - never converted anything: that loop starts at buffer + 0
// and stops at buffer. SelectFileFilter and TortoiseUDiff call it that way.
// Old StringUtils: FAILS.
TEST(ConjectureString, PipesToNullsOneArgumentConvertsEveryPipe)
{
	wchar_t filter[] = L"Text|*.txt|All|*.*|";
	StringUtils::PipesToNulls(filter);
	const std::wstring_view converted(filter, std::size(filter) - 1);
	EXPECT_EQ(L"Text\0*.txt\0All\0*.*\0"sv, converted);
}

// Conjecture: the regex ParseEmailAddress keeps the 21-case contract of the
// mailinfo.c port (StringUtilsTest's table, restated against the new signature).
TEST(ConjectureString, ParseEmailAddressKeepsTheMailinfoContract)
{
	struct Case { std::wstring_view input, address, name; };
	const Case cases[] = {
		{ L"", L"", L"" },
		{ L" ", L"", L"" },
		{ L"test@example.com", L"test@example.com", L"test@example.com" },
		{ L"<test@example.com>", L"test@example.com", L"test@example.com" },
		{ L"John Doe <test@example.com>", L"test@example.com", L"John Doe" },
		{ L"\"John Doe\" <test@example.com>", L"test@example.com", L"John Doe" },
		{ L"<test@example.com", L"test@example.com", L"test@example.com" },
		{ L"test@example.com>", L"test@example.com", L"test@example.com" },
		{ L"John Doe <johndoe>", L"johndoe", L"John Doe" },
		{ L"john.doe@example.com (John Doe)", L"john.doe@example.com", L"John Doe" },
		{ L"John (zzz) Doe <john.doe@example.com> (Comment)", L"john.doe@example.com", L"John (zzz) Doe (Comment)" },
		{ L"\"john.doe@example.com >> John Doe\" <john.doe@example.com>", L"john.doe@example.com", L"john.doe@example.com >> John Doe" },
		{ L"\"John<something@example.com> Doe\" <john.doe@example.com>", L"john.doe@example.com", L"John<something@example.com> Doe" },
		{ L"\"John D\\\"o\\\"e\" <john.doe@example.com>", L"john.doe@example.com", L"John D\"o\"e" },
		{ L"<test@example.com> \"John Doe\"", L"test@example.com", L"John Doe" },
	};
	for (const auto& c : cases)
	{
		const auto parsed = StringUtils::ParseEmailAddress(c.input);
		EXPECT_EQ(c.address, parsed.address) << c.input;
		EXPECT_EQ(c.name, parsed.name) << c.input;
	}
}

// Conjecture: a deliberate deviation - for `"Name" <addr-without-@>` the quoted
// name is kept, where the port's parse_bogus_from overwrote it with "" and then
// fell back to the address.
// Old port: FAILS (name == L"johndoe").
TEST(ConjectureString, ParseEmailAddressKeepsQuotedNameForBogusAddress)
{
	const auto parsed = StringUtils::ParseEmailAddress(L"\"John Doe\" <johndoe>");
	EXPECT_EQ(L"johndoe", parsed.address);
	EXPECT_EQ(L"John Doe", parsed.name);
}

// ---------------------------------------------------------------------------
// PathUtils and invarients

// Conjecture: PathUtils::GetLongPathname became fs::absolute, which does not
// restore the on-disk case - so CTGitPath::UpdateCase no longer updates case.
// Current PathUtils: FAILS.
TEST(ConjectureString, GetLongPathnameRestoresOnDiskCase)
{
	wchar_t windowsDir[MAX_PATH] = {};
	ASSERT_NE(0u, GetWindowsDirectoryW(windowsDir, MAX_PATH));
	std::wstring lowered = windowsDir;
	tgit::wstr::MakeLower(lowered);
	ASSERT_NE(std::wstring(windowsDir), lowered) << "the Windows directory has no upper-case letters to lose";
	EXPECT_EQ(std::wstring(windowsDir), PathUtils::GetLongPathname(lowered).wstring());
}

// Conjecture: invarients' ObjectFormatName and ZeroRevSV have their branches
// swapped - SHA1 says "SHA256" and gets the 64-character zero id.
// Current invarients: FAILS.
TEST(ConjectureString, ObjectFormatNameAndZeroRevMatchTheLatchedFormat)
{
	tgit::invarients::LatchObjectFormat(tgit::invarients::GitObjectFormat::SHA1);
	EXPECT_EQ(L"SHA1"sv, tgit::invarients::ObjectFormatName());
	EXPECT_EQ(40u, tgit::invarients::ZeroRevSV().size());

	tgit::invarients::LatchObjectFormat(tgit::invarients::GitObjectFormat::SHA256);
	EXPECT_EQ(L"SHA256"sv, tgit::invarients::ObjectFormatName());
	EXPECT_EQ(64u, tgit::invarients::ZeroRevSV().size());

	tgit::invarients::LatchObjectFormat(tgit::invarients::GitObjectFormat::SHA1); // process-level state: put it back
}
