// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2015-2016, 2018, 2023, 2026 - TortoiseGit

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

// git's raw output buffer used to be a class of its own (a std::vector<byte>
// with find/RevertFind/findNextString/Decode). It is a std::string now, and the
// two operations std::string does not have are free functions in str_utils:
// tgit::zstr::FindNextString and CUnicodeUtils::DecodeUtf8UpToNul. These tests
// are the old class's, case for case, retargeted:
//  - find(0, p) is find('\0', p): a literal 0 is ambiguous between
//    find(char, size_type) and find(const char*, size_type);
//  - RevertFind(c, p) is rfind(c, p), which clamps a p past the end where
//    RevertFind read one element past it (RevertFind(0, 11) on 11 bytes).

#include "stdafx.h"
import str_utils;

TEST(GitByteBuffer, Empty)
{
	std::string byteArray;
	EXPECT_TRUE(byteArray.empty());
	EXPECT_EQ(0U, byteArray.size());
}

TEST(GitByteBuffer, AppendByteArray)
{
	constexpr char inputByteArray[] = { "12345789" };
	std::string byteArray;
	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(sizeof(inputByteArray), byteArray.size());

	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(2 * sizeof(inputByteArray), byteArray.size());

	byteArray.clear();
	EXPECT_TRUE(byteArray.empty());
	EXPECT_EQ(0U, byteArray.size());
}

TEST(GitByteBuffer, AppendByteArrayStdStringView)
{
	constexpr char inputByteArray[] = { "12345789" };
	std::string byteArray;
	byteArray.append(std::string_view(inputByteArray, sizeof(inputByteArray)));
	EXPECT_EQ(sizeof(inputByteArray), byteArray.size());

	byteArray.append(std::string_view(inputByteArray, sizeof(inputByteArray)));
	EXPECT_EQ(2 * sizeof(inputByteArray), byteArray.size());

	byteArray.clear();
	EXPECT_TRUE(byteArray.empty());
	EXPECT_EQ(0U, byteArray.size());
}

TEST(GitByteBuffer, AppendByteArrayWithNulls)
{
	constexpr char inputByteArray[] = { "1234\0""5789\0" };
	std::string byteArray;

	byteArray.append(inputByteArray, 0);
	EXPECT_EQ(0U, byteArray.size());

	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(sizeof(inputByteArray), byteArray.size());

	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(2 * sizeof(inputByteArray), byteArray.size());
}

TEST(GitByteBuffer, AppendByteArrayWithNoNulls)
{
	constexpr char inputByteArray[] = { '1', '2', '3', '4' };
	std::string byteArray;

	byteArray.append(inputByteArray, 0);
	EXPECT_EQ(0U, byteArray.size());

	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(sizeof(inputByteArray), byteArray.size());

	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(2 * sizeof(inputByteArray), byteArray.size());
}

TEST(GitByteBuffer, Decode)
{
	std::string byteArray;
	EXPECT_TRUE(CUnicodeUtils::DecodeUtf8UpToNul(byteArray).empty());

	byteArray.append(std::string_view("1234"));
	EXPECT_STREQ(L"1234", CUnicodeUtils::DecodeUtf8UpToNul(byteArray).c_str());

	// UTF-8 is assumed, never detected: five bytes, four characters.
	byteArray.clear();
	byteArray.append(std::string_view("caf\xC3\xA9"));
	EXPECT_EQ(5U, byteArray.size()); // written as escapes so the test does not depend on this file's own encoding
	EXPECT_STREQ(L"café", CUnicodeUtils::DecodeUtf8UpToNul(byteArray).c_str());
}

TEST(GitByteBuffer, DecodeStopsAtFirstNul)
{
	// git's output is frequently NUL-separated (-z, ls-files --stage -z), which is
	// what made the old implicit operator CString() a trap: decoding such a buffer
	// silently yields only its first record. DecodeUtf8UpToNul keeps that behaviour
	// exactly - naming the conversion was meant to make it visible, not to change
	// it - so this test is here to notice if anyone ever "fixes" it by accident.
	constexpr char inputByteArray[] = { "1234\0""5789\0" };
	std::string byteArray;
	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(11U, byteArray.size());
	EXPECT_STREQ(L"1234", CUnicodeUtils::DecodeUtf8UpToNul(byteArray).c_str());

	// A lone NUL is not empty(); it has to decode to an empty string.
	byteArray.clear();
	byteArray.append(std::string_view("\0", 1));
	EXPECT_EQ(1U, byteArray.size());
	EXPECT_TRUE(CUnicodeUtils::DecodeUtf8UpToNul(byteArray).empty());
}

TEST(GitByteBuffer, Find)
{
	constexpr char inputByteArray[] = { "1234\0""5789\0" };
	std::string byteArray;

	EXPECT_EQ(std::string::npos, byteArray.find('\0'));
	EXPECT_EQ(std::string::npos, byteArray.find('3'));
	EXPECT_EQ(std::string::npos, byteArray.find('\0', 15));

	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(sizeof(inputByteArray), byteArray.size());

	EXPECT_EQ(4U, byteArray.find('\0'));
	EXPECT_EQ(4U, byteArray.find('\0', 4));
	EXPECT_EQ(9U, byteArray.find('\0', 5));
	EXPECT_EQ(std::string::npos, byteArray.find('\0', 15));

	EXPECT_EQ(0U, byteArray.find('1'));
	EXPECT_EQ(1U, byteArray.find('2'));

	EXPECT_EQ(std::string::npos, byteArray.find('F'));

	byteArray.append("1", 1);
	EXPECT_EQ(0U, byteArray.find('1'));
	EXPECT_EQ(11U, byteArray.find('1', 2));

	EXPECT_EQ(4U, byteArray.find('\0'));
	EXPECT_EQ(4U, byteArray.find('\0', 4));
	EXPECT_EQ(9U, byteArray.find('\0', 5));
	EXPECT_EQ(9U, byteArray.find('\0', 9));
	EXPECT_EQ(10U, byteArray.find('\0', 10));
	EXPECT_EQ(std::string::npos, byteArray.find('\0', 11));
	EXPECT_EQ(std::string::npos, byteArray.find('\0', 15));
}

TEST(GitByteBuffer, RevertFind)
{
	constexpr char inputByteArray[] = { "1234\0""5789\0" };
	std::string byteArray;

	EXPECT_EQ(std::string::npos, byteArray.rfind('\0'));
	EXPECT_EQ(std::string::npos, byteArray.rfind('3'));

	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(sizeof(inputByteArray), byteArray.size());

	EXPECT_EQ(10U, byteArray.rfind('\0'));
	EXPECT_EQ(10U, byteArray.rfind('\0', 10));
	EXPECT_EQ(9U, byteArray.rfind('\0', 9));
	EXPECT_EQ(4U, byteArray.rfind('\0', 4));
	EXPECT_EQ(4U, byteArray.rfind('\0', 5));
	EXPECT_EQ(std::string::npos, byteArray.rfind('\0', 2));

	EXPECT_EQ(0U, byteArray.rfind('1'));
	EXPECT_EQ(1U, byteArray.rfind('2'));

	EXPECT_EQ(std::string::npos, byteArray.rfind('F'));

	byteArray.append("1", 1);
	EXPECT_EQ(11U, byteArray.rfind('1'));
	EXPECT_EQ(11U, byteArray.rfind('1', 11));
	EXPECT_EQ(0U, byteArray.rfind('1', 10));
	EXPECT_EQ(0U, byteArray.rfind('1', 2));
	EXPECT_EQ(0U, byteArray.rfind('1', 1));
	EXPECT_EQ(0U, byteArray.rfind('1', 0));

	EXPECT_EQ(10U, byteArray.rfind('\0'));
	EXPECT_EQ(std::string::npos, byteArray.rfind('\0', 0));
	EXPECT_EQ(std::string::npos, byteArray.rfind('\0', 2));
	EXPECT_EQ(4U, byteArray.rfind('\0', 4));
	EXPECT_EQ(4U, byteArray.rfind('\0', 5));
	EXPECT_EQ(9U, byteArray.rfind('\0', 9));
	EXPECT_EQ(10U, byteArray.rfind('\0', 10));
	EXPECT_EQ(10U, byteArray.rfind('\0', 11));
}

TEST(GitByteBuffer, FindNextString)
{
	constexpr char inputByteArray[] = { "1234\0""5789\0" };
	std::string byteArray;

	EXPECT_EQ(std::string::npos, tgit::zstr::FindNextString(byteArray, 0));
	EXPECT_EQ(std::string::npos, tgit::zstr::FindNextString(byteArray, 5));

	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(sizeof(inputByteArray), byteArray.size());

	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 0));
	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 2));
	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 4));
	EXPECT_EQ(std::string::npos, tgit::zstr::FindNextString(byteArray, 5));
	EXPECT_EQ(std::string::npos, tgit::zstr::FindNextString(byteArray, 9));
	EXPECT_EQ(std::string::npos, tgit::zstr::FindNextString(byteArray, 10));

	byteArray.append("0", 1);
	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 0));
	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 2));
	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 4));
	EXPECT_EQ(11U, tgit::zstr::FindNextString(byteArray, 5));
	EXPECT_EQ(11U, tgit::zstr::FindNextString(byteArray, 9));
	EXPECT_EQ(11U, tgit::zstr::FindNextString(byteArray, 10));
	EXPECT_EQ(std::string::npos, tgit::zstr::FindNextString(byteArray, 11));

	byteArray.clear();
	byteArray.append(inputByteArray, sizeof(inputByteArray));
	EXPECT_EQ(sizeof(inputByteArray), byteArray.size());

	byteArray.append("1", 1);
	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 0));
	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 2));
	EXPECT_EQ(5U, tgit::zstr::FindNextString(byteArray, 4));
	EXPECT_EQ(11U, tgit::zstr::FindNextString(byteArray, 5));
	EXPECT_EQ(11U, tgit::zstr::FindNextString(byteArray, 9));
	EXPECT_EQ(11U, tgit::zstr::FindNextString(byteArray, 10));
	EXPECT_EQ(std::string::npos, tgit::zstr::FindNextString(byteArray, 11));
}
