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

// These are differential tests, deliberately. tgit::wstr exists so that moving
// src\Git and src\Utils off CString is a rename rather than a re-decision at
// every call site, and that claim is only worth anything if the helper and the
// real CString actually agree - including on the edges where the obvious STL
// spelling does not: Find's -1 versus npos, Mid and Left clamping instead of
// throwing, Tokenize keeping empty tokens.
//
// So each test asserts helper == CString on the same input rather than asserting
// against a hand-written expectation. A hand-written expectation would encode
// what I believed CString does; this encodes what it does.

#include "stdafx.h"
#include "WideString.h"

namespace
{
// The inputs worth checking: empty, no match, match at both ends, repeats,
// out-of-range offsets, and something outside the BMP so a surrogate pair is
// present while offsets are counted in code units, exactly as CString counts.
const wchar_t* const kSubjects[] = {
	L"",
	L"a",
	L"abc",
	L"aaa",
	L"a/b/c",
	L"/leading",
	L"trailing/",
	L"  padded\t\r\n",
	L"עברית",	 // Hebrew, BMP
	L"x\U0001F600y",					 // emoji: one code point, two code units
	L"\U0001D400\U0001D401",			 // math bold capitals, both astral
};
} // namespace

TEST(WideString, FindMatchesCString)
{
	const wchar_t* const needles[] = { L"", L"a", L"bc", L"/", L"zz", L"\U0001F600" };
	for (const wchar_t* subject : kSubjects)
	{
		const CString cs(subject);
		const std::wstring ws(subject);
		for (const wchar_t* needle : needles)
		{
			for (int from = 0; from <= 4; ++from)
			{
				if (from > cs.GetLength())
					continue;
				EXPECT_EQ(cs.Find(needle, from), tgit::wstr::Find(ws, needle, from))
					<< "subject=" << subject << " needle=" << needle << " from=" << from;
			}
		}
		for (const wchar_t c : { L'a', L'/', L'z' })
			EXPECT_EQ(cs.Find(c), tgit::wstr::Find(ws, c)) << "subject=" << subject;
	}
}

TEST(WideString, ReverseFindAndFindOneOfMatchCString)
{
	for (const wchar_t* subject : kSubjects)
	{
		const CString cs(subject);
		const std::wstring ws(subject);
		for (const wchar_t c : { L'a', L'/', L'z' })
			EXPECT_EQ(cs.ReverseFind(c), tgit::wstr::ReverseFind(ws, c)) << "subject=" << subject;
		for (const wchar_t* set : { L"ab", L"/", L"xyz" })
			EXPECT_EQ(cs.FindOneOf(set), tgit::wstr::FindOneOf(ws, set)) << "subject=" << subject;
	}
}

TEST(WideString, LeftRightMidClampLikeCString)
{
	// The point of this one: substr() throws where Mid() clamps, so every
	// negative and past-the-end offset here is a case that would have become an
	// exception under a naive conversion.
	for (const wchar_t* subject : kSubjects)
	{
		const CString cs(subject);
		const std::wstring ws(subject);
		for (int n = -2; n <= 6; ++n)
		{
			EXPECT_STREQ(cs.Left(n), tgit::wstr::Left(ws, n).c_str()) << "subject=" << subject << " n=" << n;
			EXPECT_STREQ(cs.Right(n), tgit::wstr::Right(ws, n).c_str()) << "subject=" << subject << " n=" << n;
			EXPECT_STREQ(cs.Mid(n), tgit::wstr::Mid(ws, n).c_str()) << "subject=" << subject << " n=" << n;
			for (int count = -1; count <= 4; ++count)
				EXPECT_STREQ(cs.Mid(n, count), tgit::wstr::Mid(ws, n, count).c_str())
					<< "subject=" << subject << " n=" << n << " count=" << count;
		}
	}
}

TEST(WideString, ReplaceMatchesCStringIncludingCount)
{
	struct { const wchar_t* subject; const wchar_t* from; const wchar_t* to; } cases[] = {
		{ L"aaa", L"a", L"b" },
		{ L"aaa", L"aa", L"a" },
		{ L"abc", L"zz", L"y" },
		{ L"a/b/c", L"/", L"\\" },
		{ L"", L"a", L"b" },
		{ L"abc", L"b", L"" },
		{ L"aaa", L"a", L"aa" },	// replacement contains the needle
	};
	for (const auto& c : cases)
	{
		CString cs(c.subject);
		std::wstring ws(c.subject);
		const int csCount = cs.Replace(c.from, c.to);
		const int wsCount = tgit::wstr::Replace(ws, c.from, c.to);
		EXPECT_EQ(csCount, wsCount) << "subject=" << c.subject << " from=" << c.from;
		EXPECT_STREQ(cs, ws.c_str()) << "subject=" << c.subject << " from=" << c.from;
	}

	CString csChar(L"a/b/c");
	std::wstring wsChar(L"a/b/c");
	EXPECT_EQ(csChar.Replace(L'/', L'\\'), tgit::wstr::Replace(wsChar, L'/', L'\\'));
	EXPECT_STREQ(csChar, wsChar.c_str());
}

TEST(WideString, TrimMatchesCString)
{
	for (const wchar_t* subject : kSubjects)
	{
		CString csL(subject), csR(subject), csB(subject);
		std::wstring wsL(subject), wsR(subject), wsB(subject);
		csL.TrimLeft();
		csR.TrimRight();
		csB.Trim();
		tgit::wstr::TrimLeft(wsL);
		tgit::wstr::TrimRight(wsR);
		tgit::wstr::Trim(wsB);
		EXPECT_STREQ(csL, wsL.c_str()) << "subject=" << subject;
		EXPECT_STREQ(csR, wsR.c_str()) << "subject=" << subject;
		EXPECT_STREQ(csB, wsB.c_str()) << "subject=" << subject;
	}
}

TEST(WideString, TokenizeSkipsDelimiterRunsLikeCString)
{
	// CString::Tokenize skips leading delimiters, so "a//b" is two tokens and
	// no token is ever empty. This test is why WideString.h is right about
	// that: the first version of the helper produced three and was corrected
	// here rather than in review.
	const wchar_t* const subjects[] = { L"a/b/c", L"a//b", L"/a", L"a/", L"", L"abc", L"///", L"//a//b//" };
	for (const wchar_t* subject : subjects)
	{
		const CString cs(subject);
		const std::wstring ws(subject);

		std::vector<CString> csTokens;
		for (int pos = 0;;)
		{
			const CString token = cs.Tokenize(L"/", pos);
			if (pos < 0)
				break;
			csTokens.push_back(token);
		}

		std::vector<std::wstring> wsTokens;
		for (int pos = 0;;)
		{
			const std::wstring token = tgit::wstr::Tokenize(ws, L"/", pos);
			if (pos < 0)
				break;
			wsTokens.push_back(token);
		}

		ASSERT_EQ(csTokens.size(), wsTokens.size()) << "subject=" << subject;
		for (size_t i = 0; i < csTokens.size(); ++i)
			EXPECT_STREQ(csTokens[i], wsTokens[i].c_str()) << "subject=" << subject << " i=" << i;
	}
}

TEST(WideString, CaseHelpersMatchCString)
{
	for (const wchar_t* subject : kSubjects)
	{
		CString csLower(subject), csUpper(subject);
		std::wstring wsLower(subject), wsUpper(subject);
		csLower.MakeLower();
		csUpper.MakeUpper();
		tgit::wstr::MakeLower(wsLower);
		tgit::wstr::MakeUpper(wsUpper);
		EXPECT_STREQ(csLower, wsLower.c_str()) << "subject=" << subject;
		EXPECT_STREQ(csUpper, wsUpper.c_str()) << "subject=" << subject;

		for (const wchar_t* other : kSubjects)
		{
			const int cs = CString(subject).CompareNoCase(other);
			const int ws = tgit::wstr::CompareNoCase(subject, other);
			// CompareNoCase is only specified by sign, not magnitude.
			EXPECT_EQ(cs < 0, ws < 0) << "a=" << subject << " b=" << other;
			EXPECT_EQ(cs > 0, ws > 0) << "a=" << subject << " b=" << other;
		}
	}
}

TEST(WideString, AstralCharactersSurviveRoundTrip)
{
	// Not a CString comparison - a statement about what UTF-16 does and does
	// not cost us. Offsets are code units, so an astral character occupies two,
	// exactly as CString counts them. Nothing here is lossy; what would break
	// is splitting *between* the two, which is why Mid/Left offsets in
	// converted code must keep coming from Find rather than from arithmetic.
	const std::wstring emoji = L"x\U0001F600y";
	ASSERT_EQ(4u, emoji.size());
	EXPECT_EQ(4, CString(L"x\U0001F600y").GetLength());
	EXPECT_EQ(3, tgit::wstr::Find(emoji, L'y'));
	EXPECT_EQ(1, tgit::wstr::Find(emoji, L"\U0001F600"));
	EXPECT_EQ(emoji, tgit::wstr::Mid(emoji, 0));
	EXPECT_EQ(L"\U0001F600y", tgit::wstr::Mid(emoji, 1));
}

TEST(WideString, ReleaseBufferTrimsToTheNulLikeCString)
{
	std::wstring buffer(MAX_PATH, L'\0');
	const DWORD len = ::GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
	ASSERT_GT(len, 0u);
	tgit::wstr::ReleaseBuffer(buffer, static_cast<int>(len));
	EXPECT_EQ(len, buffer.size());
	EXPECT_EQ(std::wstring::npos, buffer.find(L'\0'));

	// The no-length overload has to find the NUL itself, as ReleaseBuffer(-1) does.
	std::wstring implicitLen(MAX_PATH, L'\0');
	const DWORD len2 = ::GetWindowsDirectoryW(implicitLen.data(), static_cast<UINT>(implicitLen.size()));
	ASSERT_GT(len2, 0u);
	tgit::wstr::ReleaseBuffer(implicitLen);
	EXPECT_EQ(buffer, implicitLen);
}
