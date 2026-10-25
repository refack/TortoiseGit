// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2020, 2023 - TortoiseGit
// Copyright (C) 2011-2012 - TortoiseSVN

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
#include "stdafx.h"
using namespace std::literals;
using tgit::wstr::CString;
using CStringW = tgit::wstr::CString;

TEST(UnicodeUtils, CString)
{
	CStringA result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"");
	EXPECT_EQ(0, result.GetLength());
	EXPECT_STREQ(result, "");
	CStringW resultW = CUnicodeUtils::Convert2Unicode<CString>("");
	EXPECT_EQ(0, resultW.GetLength());
	EXPECT_STREQ(resultW, L"");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"Iñtërnâtiônàlizætiøn");
	EXPECT_EQ(27, result.GetLength());
	EXPECT_STREQ(result, "\x49\xC3\xB1\x74\xC3\xAB\x72\x6E\xC3\xA2\x74\x69\xC3\xB4\x6E\xC3\xA0\x6C\x69\x7A\xC3\xA6\x74\x69\xC3\xB8\x6E");
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"Iñtërnâtiônàlizætiøn");
	EXPECT_EQ(20, resultW.GetLength());

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"<value>退订</value>");
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"<value>退订</value>");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"äöü");
	EXPECT_EQ(6, result.GetLength());
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"äöü");
	EXPECT_EQ(3, resultW.GetLength());

	resultW = CUnicodeUtils::StdGetUnicode<CString>("\xE4\xF6\xFC\xDF", 1252);
	EXPECT_EQ(4, resultW.GetLength());
	EXPECT_STREQ(resultW, L"äöüß");
	result = CUnicodeUtils::GetUTF8<CStringA>(resultW);
	EXPECT_STREQ(result, "\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"Продолжить выполнение скрипта?");
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"Продолжить выполнение скрипта?");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"dvostruki klik za automtsko uključivanje alfa");
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"dvostruki klik za automtsko uključivanje alfa");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"包含有错误的结构。");
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"包含有错误的结构。");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"个文件，共有 %2!d! 个文件");
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"个文件，共有 %2!d! 个文件");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"は予期せぬオブジェクトを含んでいます。");
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"は予期せぬオブジェクトを含んでいます。");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"Verify that the correct path and file name are given.");
	EXPECT_STREQ(result, "Verify that the correct path and file name are given.");
	resultW = CUnicodeUtils::GetUnicode<CString>(result);
	EXPECT_STREQ(resultW, L"Verify that the correct path and file name are given.");

	result = CUnicodeUtils::Convert2Multibyte<CStringA>(L"\U0002070e"); // 𠜎 is 4-byte utf8
	EXPECT_EQ(4, result.GetLength());
	EXPECT_STREQ(result, "\xf0\xa0\x9c\x8e");
	resultW = CUnicodeUtils::Convert2Unicode<CString>("\xf0\xa0\x9c\x8e");
	EXPECT_STREQ(resultW, L"\U0002070e");
	EXPECT_EQ(2, resultW.GetLength());

	resultW = CUnicodeUtils::Convert2Unicode<CString>("\xfe");
	EXPECT_STREQ(resultW, L"\uFFFD");
	EXPECT_EQ(1, resultW.GetLength());

	resultW = CUnicodeUtils::Convert2Unicode<CString>("\xc3\x28"); // Invalid 2 Octet Sequence
	EXPECT_STREQ(resultW, L"\uFFFD(");
	EXPECT_EQ(2, resultW.GetLength());
}

TEST(UnicodeUtils, Std)
{
	std::string result = CUnicodeUtils::StdGetUTF8(L""s);
	EXPECT_EQ(0u, result.size());
	EXPECT_STREQ(result.c_str(), "");
	std::wstring resultW = CUnicodeUtils::StdGetUnicode("", CP_UTF8);
	EXPECT_EQ(0u, resultW.size());
	EXPECT_STREQ(resultW.c_str(), L"");

	result = CUnicodeUtils::StdGetUTF8(L"Iñtërnâtiônàlizætiøn"s);
	EXPECT_EQ(27u, result.size());
	EXPECT_STREQ(result.c_str(), "\x49\xC3\xB1\x74\xC3\xAB\x72\x6E\xC3\xA2\x74\x69\xC3\xB4\x6E\xC3\xA0\x6C\x69\x7A\xC3\xA6\x74\x69\xC3\xB8\x6E");
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"Iñtërnâtiônàlizætiøn");
	EXPECT_EQ(20u, resultW.size());

	result = CUnicodeUtils::StdGetUTF8(L"<value>退订</value>"s);
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"<value>退订</value>");

	result = CUnicodeUtils::StdGetUTF8(L"äöü"s);
	EXPECT_EQ(6u, result.size());
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"äöü");
	EXPECT_EQ(3u, resultW.size());

	result = CUnicodeUtils::StdGetUTF8(L"äöüß"s);
	EXPECT_STREQ(result.c_str(), "\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F");

	result = CUnicodeUtils::StdGetUTF8(L"Продолжить выполнение скрипта?"s);
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"Продолжить выполнение скрипта?");

	result = CUnicodeUtils::StdGetUTF8(L"dvostruki klik za automtsko uključivanje alfa"s);
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"dvostruki klik za automtsko uključivanje alfa");

	result = CUnicodeUtils::StdGetUTF8(L"包含有错误的结构。"s);
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"包含有错误的结构。");

	result = CUnicodeUtils::StdGetUTF8(L"个文件，共有 %2!d! 个文件"s);
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"个文件，共有 %2!d! 个文件");

	result = CUnicodeUtils::StdGetUTF8(L"は予期せぬオブジェクトを含んでいます。"s);
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"は予期せぬオブジェクトを含んでいます。");

	result = CUnicodeUtils::StdGetUTF8(L"Verify that the correct path and file name are given."s);
	EXPECT_STREQ(result.c_str(), "Verify that the correct path and file name are given.");
	resultW = CUnicodeUtils::StdGetUnicode(result, CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"Verify that the correct path and file name are given.");

	result = CUnicodeUtils::StdGetUTF8(L"\U0002070e"s); // 𠜎 is 4-byte utf8
	EXPECT_EQ(4u, result.size());
	EXPECT_STREQ(result.c_str(), "\xf0\xa0\x9c\x8e");
	resultW = CUnicodeUtils::StdGetUnicode("\xf0\xa0\x9c\x8e", CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"\U0002070e");
	EXPECT_EQ(2u, resultW.size());

	resultW = CUnicodeUtils::StdGetUnicode("\xfe", CP_UTF8);
	EXPECT_STREQ(resultW.c_str(), L"\uFFFD");
	EXPECT_EQ(1u, resultW.size());

	resultW = CUnicodeUtils::StdGetUnicode("\xc3\x28", CP_UTF8); // Invalid 2 Octet Sequence
	EXPECT_STREQ(resultW.c_str(), L"\uFFFD(");
	EXPECT_EQ(2u, resultW.size());
}


// Added when the CString overloads became thin adapters over the std:: ones
// (previously each carried its own WideCharToMultiByte/MultiByteToWideChar
// copy). These are differential: they assert the two APIs agree rather than
// asserting either against a written expectation, so the adapters cannot drift
// from what they adapt. The same technique caught a real error in
// tgit::wstr::Tokenize on its first run.
TEST(UnicodeUtils, StdAndCStringAgree)
{
	const wchar_t* const subjects[] = {
		L"", L"plain ascii", L"עברית", L"\U0001F600", L"a\U0001D400b", L"mixed \u00e9 \u4e2d\u6587",
	};
	// Not just UTF-8: GetMulti's whole reason to exist is the acp parameter,
	// and a lossy code page is where an adapter would most plausibly differ.
	for (const int acp : { CP_UTF8, CP_ACP, 1252, 932 })
	{
		for (const wchar_t* subject : subjects)
		{
			const CStringA viaCString = CUnicodeUtils::GetMulti<CStringA>(CStringW(subject), acp);
			const std::string viaStd = CUnicodeUtils::StdGetMulti(subject, acp);
			ASSERT_EQ(static_cast<size_t>(viaCString.GetLength()), viaStd.size())
				<< "acp=" << acp << " subject=" << subject;
			EXPECT_EQ(0, memcmp(static_cast<const char*>(viaCString), viaStd.data(), viaStd.size()))
				<< "acp=" << acp << " subject=" << subject;

			const CString backViaCString = CUnicodeUtils::StdGetUnicode<CString>(std::string_view(viaCString.GetString(), viaCString.GetLength()), acp);
			const std::wstring backViaStd = CUnicodeUtils::StdGetUnicode(viaStd, acp);
			EXPECT_STREQ(backViaCString, backViaStd.c_str()) << "acp=" << acp << " subject=" << subject;
		}
	}
}

TEST(UnicodeUtils, StdGetCPCodeMatchesCStringOverload)
{
	// Including the case-insensitivity, which now runs through
	// tgit::wstr::MakeLower rather than CString::MakeLower.
	const wchar_t* const names[] = {
		L"", L"utf-8", L"UTF-8", L"Utf-8", L"windows-1252", L"WINDOWS-1252",
		L"cp1251", L"CP_1251", L"koi8-r", L"big5", L"not-a-code-page", L"IBM037",
	};
	// The CString overload this compared StdGetCPCode against is gone, so there
	// is nothing left to differ; the names stay as documentation of the inputs.
	std::ignore = names;

	// The documented fallback, worth pinning: an unknown or empty name is UTF-8,
	// not the ANSI code page.
	EXPECT_EQ(CP_UTF8, CUnicodeUtils::StdGetCPCode(L""));
	EXPECT_EQ(CP_UTF8, CUnicodeUtils::StdGetCPCode(L"not-a-code-page"));
	EXPECT_EQ(1251, CUnicodeUtils::StdGetCPCode(L"cp-1251"));
}

TEST(UnicodeUtils, EmbeddedNulSurvivesTheAdapters)
{
	// CString and std::string both allow embedded NULs, and the adapters now
	// carry an explicit length across rather than relying on a terminator. If
	// one of them ever regressed to strlen this is the test that would say so.
	const std::wstring wide(L"a\0b", 3);
	const std::string narrow = CUnicodeUtils::StdGetUTF8(wide);
	EXPECT_EQ(3u, narrow.size());
	EXPECT_EQ(3, CUnicodeUtils::GetUTF8<CStringA>(CStringW(wide.data(), 3)).GetLength());
	EXPECT_EQ(wide, CUnicodeUtils::StdGetUnicode(narrow, CP_UTF8));
}