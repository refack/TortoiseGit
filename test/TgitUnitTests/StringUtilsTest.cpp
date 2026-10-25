// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2015-2017, 2021, 2023, 2025-2026 - TortoiseGit
// Copyright (C) 2003-2011 - TortoiseSVN

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
import str_utils;
import CmdLineParser;
import git_h;	// UnescapeGitQuotePath moved from StringUtils to TGitH

// The out-parameter shape the assertions below were written against; the function
// itself now returns { address, name }. An adapter, so the assertions stay as they were.
static void ParseEmailAddressInto(const std::wstring_view in, CString& mail, CString* name = nullptr) {
	const auto parsed = StringUtils::ParseEmailAddress(in);
	mail = parsed.address.c_str();
	if (name)
		*name = parsed.name.c_str();
}


TEST(CStringUtils, WordWrap) {
	CString longline = L"this is a test of how a string can be splitted into several lines";
	CString splittedline = StringUtils::WordWrap(longline, 10, true, false, 4);
	EXPECT_EQ(L"this is a\n test of\n how a\n string\n can be\n splitted\n into\n several\n lines", splittedline);
	longline = L"c:\\this_is_a_very_long\\path_on_windows and of course some other words added to make the line longer";
	splittedline = StringUtils::WordWrap(longline, 10, true, false, 4);
	EXPECT_EQ(L"...\\pat...\n and of\n course\n some\n other\n words\n added to\n make the\n line\n longer", splittedline);
	longline = L"Forced failure in https://myserver.com/a_long_url_to_split PROPFIND error";
	splittedline = StringUtils::WordWrap(longline, 20, true, false, 4);
	EXPECT_EQ(L"Forced failure in\n https://myserver.com/a_long_url_to_split\n PROPFIND error", splittedline);
	longline = L"Forced\nfailure in https://myserver.com/a_long_url_to_split PROPFIND\nerror";
	splittedline = StringUtils::WordWrap(longline, 40, true, false, 4);
	EXPECT_EQ(L"Forced\nfailure in\n https://myserver.com/a_long_url_to_split\n PROPFIND\nerror", splittedline);
	longline =
	L"Failed to add file\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO1.java\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO2.java";
	splittedline = StringUtils::WordWrap(longline, 80, true, false, 4);
	EXPECT_EQ(
		L"Failed to add\n file\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO1.java\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthing\n",
		splittedline);
}


TEST(CStringUtils, DISABLED_LinesWrap) {
	CString longline = L"this is a test of how a string can be splitted into several lines";
	CString splittedline = StringUtils::LinesWrap(longline, 10, true);
	EXPECT_EQ(L"this is a\n test of\n how a\n string\n can be\n splitted\n into\n several\n lines", splittedline);
	longline = L"c:\\this_is_a_very_long\\path_on_windows and of course some other words added to make the line longer";
	splittedline = StringUtils::LinesWrap(longline, 10);
	EXPECT_EQ(L"c:\\this_is_a_very_long\\path_on_windows\n and of\n course\n some\n other\n words\n added to\n make the\n line\n longer", splittedline);
	longline = L"Forced failure in https://myserver.com/a_long_url_to_split PROPFIND error";
	splittedline = StringUtils::LinesWrap(longline, 20, true);
	EXPECT_EQ(L"Forced failure in\n https://myserver.com/a_long_url_to_split\n PROPFIND error", splittedline);
	longline = L"Forced\nfailure in https://myserver.com/a_long_url_to_split PROPFIND\nerror";
	splittedline = StringUtils::LinesWrap(longline, 40);
	EXPECT_EQ(L"Forced\nfailure in\n https://myserver.com/a_long_url_to_split\n PROPFIND\nerror", splittedline);
	longline =
	L"Failed to add file\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO1.java\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO2.java";
	splittedline = StringUtils::LinesWrap(longline);
	EXPECT_EQ(
		L"Failed to add file\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO1.java\n\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO2.java",
		splittedline);
	longline =
	L"The commit comment is not properly formatted.\nFormat:\n  Field 1 : Field 2 : Field 3\nWhere:\nField 1 - Team Name|Triage|Merge|Goal\nField 2 - V1 Backlog Item ID|Triage Number|SVNBranch|Goal Name\nField 3 - Description of change\nExamples:\n\nTeam Gamma : B-12345 : Changed some code\n  Triage : 123 : Fixed production release bug\n  Merge : sprint0812 : Merged sprint0812 into prod\n  Goal : Implement Pre-Commit Hook : Commit message hook impl";
	splittedline = StringUtils::LinesWrap(longline, 80);
	EXPECT_EQ(
		L"The commit comment is not properly formatted.\nFormat:\n  Field 1 : Field 2 : Field 3\nWhere:\nField 1 - Team Name|Triage|Merge|Goal\nField 2 - V1 Backlog Item ID|Triage Number|SVNBranch|Goal Name\nField 3 - Description of change\nExamples:\n\nTeam Gamma : B-12345 : Changed some code\n  Triage : 123 : Fixed production release bug\n  Merge : sprint0812 : Merged sprint0812 into prod\n  Goal : Implement Pre-Commit Hook : Commit message hook impl",
		splittedline);
}


TEST(CStringUtils, RemoveAccelerators) {
	CString empty;
	StringUtils::RemoveAccelerators(empty);
	EXPECT_EQ(L"", empty);

	CString text1 = L"&Accellerator";
	StringUtils::RemoveAccelerators(text1);
	EXPECT_EQ(L"Accellerator", text1);

	CString text1a = L"Ac&cellerator";
	StringUtils::RemoveAccelerators(text1a);
	EXPECT_EQ(L"Accellerator", text1a);

	CString text2 = L"Accellerator&";
	StringUtils::RemoveAccelerators(text2);
	EXPECT_EQ(L"Accellerator", text2);

	CString text3 = L"Some & text";
	StringUtils::RemoveAccelerators(text3);
	EXPECT_EQ(L"Some & text", text3);

	CString text4 = L"&&Accellerator";
	StringUtils::RemoveAccelerators(text4);
	EXPECT_EQ(L"&Accellerator", text4);

	CString text5 = L"Acce&&&llerator";
	StringUtils::RemoveAccelerators(text5);
	EXPECT_EQ(L"Acce&llerator", text5);

	CString text6 = L"Some & te&xt";
	StringUtils::RemoveAccelerators(text6);
	EXPECT_EQ(L"Some & text", text6);
}


TEST(CStringUtils, GetAccellerator) {
	EXPECT_EQ(L'\0', StringUtils::GetAccellerator(L""));
	EXPECT_EQ(L'\0', StringUtils::GetAccellerator(L"&"));
	EXPECT_EQ(L'\0', StringUtils::GetAccellerator(L"NoAccellerator"));
	EXPECT_EQ(L'A', StringUtils::GetAccellerator(L"&Accellerator"));
	EXPECT_EQ(L'C', StringUtils::GetAccellerator(L"Ac&cellerator"));
	EXPECT_EQ(L'\0', StringUtils::GetAccellerator(L"Accellerator&"));
	EXPECT_EQ(L'\0', StringUtils::GetAccellerator(L"Accellerator&&"));
	EXPECT_EQ(L'\0', StringUtils::GetAccellerator(L"Some & text"));
	EXPECT_EQ(L'\0', StringUtils::GetAccellerator(L"&&Accellerator"));
	EXPECT_EQ(L'L', StringUtils::GetAccellerator(L"Acce&&&llerator"));
	EXPECT_EQ(L'X', StringUtils::GetAccellerator(L"Some & te&xt"));
}


TEST(CStringUtils, ParseEmailAddress) {
	CString mail, name;
	ParseEmailAddressInto(L"", mail, &name);
	EXPECT_EQ(L"", mail);
	EXPECT_EQ(L"", name);

	ParseEmailAddressInto(L" ", mail, &name);
	EXPECT_EQ(L"", mail);
	EXPECT_EQ(L"", name);

	mail.Empty();
	ParseEmailAddressInto(L"test@example.com ", mail);
	EXPECT_EQ(L"test@example.com", mail);

	mail.Empty();
	ParseEmailAddressInto(L" test@example.com", mail);
	EXPECT_EQ(L"test@example.com", mail);

	mail.Empty();
	ParseEmailAddressInto(L"test@example.com", mail);
	EXPECT_EQ(L"test@example.com", mail);

	mail.Empty();
	ParseEmailAddressInto(L"John Doe <johndoe>", mail);
	EXPECT_EQ(L"johndoe", mail);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"test@example.com", mail, &name);
	EXPECT_EQ(L"test@example.com", mail);
	EXPECT_EQ(L"test@example.com", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"<test@example.com>", mail, &name);
	EXPECT_EQ(L"test@example.com", mail);
	EXPECT_EQ(L"test@example.com", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"John Doe <test@example.com>", mail, &name);
	EXPECT_EQ(L"test@example.com", mail);
	EXPECT_EQ(L"John Doe", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"\"John Doe\" <test@example.com>", mail, &name);
	EXPECT_EQ(L"test@example.com", mail);
	EXPECT_EQ(L"John Doe", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"<test@example.com", mail, &name);
	EXPECT_EQ(L"test@example.com", mail);
	EXPECT_EQ(L"test@example.com", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"test@example.com>", mail, &name);
	EXPECT_EQ(L"test@example.com", mail);
	EXPECT_EQ(L"test@example.com", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"John Doe <johndoe>", mail, &name);
	EXPECT_EQ(L"johndoe", mail);
	EXPECT_EQ(L"John Doe", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"john.doe@example.com (John Doe)", mail, &name);
	EXPECT_EQ(L"john.doe@example.com", mail);
	EXPECT_EQ(L"John Doe", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"John (zzz) Doe <john.doe@example.com> (Comment)", mail, &name);
	EXPECT_EQ(L"john.doe@example.com", mail);
	EXPECT_EQ(L"John (zzz) Doe (Comment)", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"\"john.doe@example.com >> John Doe\" <john.doe@example.com>", mail, &name);
	EXPECT_EQ(L"john.doe@example.com", mail);
	EXPECT_EQ(L"john.doe@example.com >> John Doe", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"\"John<something> Doe\" <john.doe@example.com>", mail, &name);
	EXPECT_EQ(L"john.doe@example.com", mail);
	EXPECT_EQ(L"John<something> Doe", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"\"John<something@example.com> Doe\" <john.doe@example.com>", mail, &name);
	EXPECT_EQ(L"john.doe@example.com", mail);
	EXPECT_EQ(L"John<something@example.com> Doe", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"\"something@example.com\" <john.doe@example.com>", mail, &name);
	EXPECT_EQ(L"john.doe@example.com", mail);
	EXPECT_EQ(L"something@example.com", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"\"John D\\\"o\\\"e\" <john.doe@example.com>", mail, &name);
	EXPECT_EQ(L"john.doe@example.com", mail);
	EXPECT_EQ(L"John D\"o\"e", name);

	mail.Empty();
	name.Empty();
	ParseEmailAddressInto(L"<test@example.com> \"John Doe\"", mail, &name);
	EXPECT_EQ(L"test@example.com", mail);
	EXPECT_EQ(L"John Doe", name);
}


TEST(CStringUtils, IsPlainReadableASCII) {
	EXPECT_TRUE(StringUtils::IsPlainReadableASCII(L""));
	EXPECT_TRUE(StringUtils::IsPlainReadableASCII(L" 1234,#+*.:-&!\\<>|\"$%&/()=567890abcdefSBDUDB[](!}~"));
	EXPECT_FALSE(StringUtils::IsPlainReadableASCII(L"\n"));
	EXPECT_FALSE(StringUtils::IsPlainReadableASCII(L"\u2302"));
	EXPECT_FALSE(StringUtils::IsPlainReadableASCII(L"é"));
	EXPECT_FALSE(StringUtils::IsPlainReadableASCII(L"\u2550"));
	EXPECT_FALSE(StringUtils::IsPlainReadableASCII(L"ä"));
	EXPECT_FALSE(StringUtils::IsPlainReadableASCII(L"€"));
	EXPECT_FALSE(StringUtils::IsPlainReadableASCII(L"\u570B"));
	EXPECT_FALSE(StringUtils::IsPlainReadableASCII(L"\u7ACB"));
}


TEST(CStringUtils, StartsWith) {
	EXPECT_TRUE(StringUtils::StartsWith(L"", L""));
	EXPECT_FALSE(StringUtils::StartsWith(L"", L"sometest"));

	CString heystack = L"sometest";
	EXPECT_TRUE(StringUtils::StartsWith(heystack, L"sometest"));
	EXPECT_TRUE(StringUtils::StartsWith(heystack, L""));
	EXPECT_TRUE(StringUtils::StartsWith(heystack, L"sometes"));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, L"sometEs"));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, L"someteste"));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, L"sometess"));

	CString empty;
	CString sometest = L"sometest";
	CString sometes = L"sometes";
	CString sometEs = L"sometEs";
	CString someteste = L"someteste";
	CString sometess = L"sometess";
	EXPECT_TRUE(StringUtils::StartsWith(heystack, sometest));
	EXPECT_TRUE(StringUtils::StartsWith(heystack, empty));
	EXPECT_TRUE(StringUtils::StartsWith(heystack, sometes));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, sometEs));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, someteste));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, sometess));
	EXPECT_FALSE(StringUtils::StartsWith(empty, sometess));
}


TEST(CStringUtils, StartsWithI) {
	EXPECT_TRUE(StringUtils::StartsWithI(L"", L""));
	EXPECT_FALSE(StringUtils::StartsWithI(L"", L"sometest"));

	CString heystack = L"someTest";
	EXPECT_TRUE(StringUtils::StartsWithI(heystack, L"sometest"));
	EXPECT_TRUE(StringUtils::StartsWithI(heystack, L"someTest"));
	EXPECT_TRUE(StringUtils::StartsWithI(heystack, L"sOmetest"));
	EXPECT_TRUE(StringUtils::StartsWithI(heystack, L""));
	EXPECT_TRUE(StringUtils::StartsWithI(heystack, L"sometEs"));
	EXPECT_FALSE(StringUtils::StartsWithI(heystack, L"sometesTe"));
	EXPECT_FALSE(StringUtils::StartsWithI(heystack, L"someteSs"));

	CString empty;
	CString sometest = L"someteSt";
	CString sometes = L"soMetes";
	CString someteste = L"sOmeteste";
	CString sometess = L"sometesS";
	EXPECT_TRUE(StringUtils::StartsWithI(heystack, sometest));
	EXPECT_TRUE(StringUtils::StartsWithI(heystack, empty));
	EXPECT_TRUE(StringUtils::StartsWithI(heystack, sometes));
	EXPECT_FALSE(StringUtils::StartsWithI(heystack, someteste));
	EXPECT_FALSE(StringUtils::StartsWithI(heystack, sometess));
	EXPECT_FALSE(StringUtils::StartsWithI(empty, sometess));
}


TEST(CStringUtils, StartsWithA) {
	EXPECT_TRUE(StringUtils::StartsWith("", ""));
	EXPECT_FALSE(StringUtils::StartsWith("", "sometest"));

	CStringA heystack = "sometest";
	EXPECT_TRUE(StringUtils::StartsWith(heystack, "sometest"));
	EXPECT_TRUE(StringUtils::StartsWith(heystack, ""));
	EXPECT_TRUE(StringUtils::StartsWith(heystack, "sometes"));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, "someTe"));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, "someteste"));
	EXPECT_FALSE(StringUtils::StartsWith(heystack, "sometess"));
}


TEST(CStringUtils, EndsWith) {
	EXPECT_TRUE(StringUtils::EndsWith(L"", L""));
	EXPECT_FALSE(StringUtils::EndsWith(L"", L"sometest"));

	CString heystack = L"sometest";
	EXPECT_TRUE(StringUtils::EndsWith(heystack, L"sometest"));
	EXPECT_TRUE(StringUtils::EndsWith(heystack, L"test"));
	EXPECT_TRUE(StringUtils::EndsWith(heystack, L"st"));
	EXPECT_TRUE(StringUtils::EndsWith(heystack, L"t"));
	EXPECT_TRUE(StringUtils::EndsWith(heystack, L""));
	EXPECT_FALSE(StringUtils::EndsWith(heystack, L"teSt"));
	EXPECT_FALSE(StringUtils::EndsWith(heystack, L"esometest"));
	EXPECT_FALSE(StringUtils::EndsWith(heystack, L"someteste"));
	EXPECT_FALSE(StringUtils::EndsWith(heystack, L"text"));
	EXPECT_FALSE(StringUtils::EndsWith(heystack, L"xt"));

	EXPECT_TRUE(StringUtils::EndsWith(heystack, L't'));
	EXPECT_FALSE(StringUtils::EndsWith(heystack, L'T'));
	EXPECT_FALSE(StringUtils::EndsWith(heystack, L'x'));
}


TEST(CStringUtils, EndsWithI) {
	EXPECT_TRUE(StringUtils::EndsWithI(L"", L""));
	EXPECT_FALSE(StringUtils::EndsWithI(L"", L"sometest"));

	CString heystack = L"sometest";
	EXPECT_TRUE(StringUtils::EndsWithI(heystack, L"someteSt"));
	EXPECT_TRUE(StringUtils::EndsWithI(heystack, L"tEst"));
	EXPECT_TRUE(StringUtils::EndsWithI(heystack, L"sT"));
	EXPECT_TRUE(StringUtils::EndsWithI(heystack, L"T"));
	EXPECT_TRUE(StringUtils::EndsWithI(heystack, L""));
	EXPECT_FALSE(StringUtils::EndsWithI(heystack, L"esometEst"));
	EXPECT_FALSE(StringUtils::EndsWithI(heystack, L"someteSte"));
	EXPECT_FALSE(StringUtils::EndsWithI(heystack, L"text"));
	EXPECT_FALSE(StringUtils::EndsWithI(heystack, L"xt"));
}


TEST(CStringUtils, UnescapeGitQuotePath) {
	EXPECT_EQ(L"", TGitH::UnescapeGitQuotePath(L""));

	EXPECT_EQ(L"ascii.txt", TGitH::UnescapeGitQuotePath(L"ascii.txt"));
	EXPECT_EQ(L"ümlauts.txt", TGitH::UnescapeGitQuotePath(L"\\303\\274mlauts.txt"));
	EXPECT_EQ(L"umläütß", TGitH::UnescapeGitQuotePath(L"uml\\303\\244\\303\\274t\\303\\237"));

	// taken from Git tests:
	EXPECT_EQ(L"\u6FF1\u91CE/file", TGitH::UnescapeGitQuotePath(L"\\346\\277\\261\\351\\207\\216/file"));
	EXPECT_EQ(L"\u6FF1\u91CE\u7D14", TGitH::UnescapeGitQuotePath(L"\\346\\277\\261\\351\\207\\216\\347\\264\\224"));
}


TEST(CStringUtils, EnsureCRLF) {
	EXPECT_EQ(L"", StringUtils::EnsureCRLF(L""));
	EXPECT_EQ(L"\r\n", StringUtils::EnsureCRLF(L"\n"));
	EXPECT_EQ(L"\r\n", StringUtils::EnsureCRLF(L"\r\n"));
	EXPECT_EQ(L"\r\n", StringUtils::EnsureCRLF(L"\r"));
	EXPECT_EQ(L"Some\r\nthing", StringUtils::EnsureCRLF(L"Some\nthing"));
	EXPECT_EQ(L"Some\r\nthing", StringUtils::EnsureCRLF(L"Some\rthing"));
	EXPECT_EQ(L"Some\r\nthing\r\n", StringUtils::EnsureCRLF(L"Some\nthing\n"));
	EXPECT_EQ(L"Some\\nthing", StringUtils::EnsureCRLF(L"Some\\nthing"));
	EXPECT_EQ(L"Some\r\n\r\nthing\r\n", StringUtils::EnsureCRLF(L"Some\n\nthing\n"));
	EXPECT_EQ(L"Some\r\n\r\nthing\r\n", StringUtils::EnsureCRLF(L"Some\r\r\nthing\r"));
	EXPECT_EQ(L"Some\r\nthing\r\n", StringUtils::EnsureCRLF(L"Some\r\nthing\n"));
	EXPECT_EQ(L"Some\r\nthing\r\n", StringUtils::EnsureCRLF(L"Some\r\nthing\r"));
	EXPECT_EQ(L"\r\nSome\r\n\r\nthing\r\n", StringUtils::EnsureCRLF(L"\nSome\r\n\nthing\n"));
	EXPECT_EQ(L"\r\nSome\r\n\r\n\r\nthing\r\n", StringUtils::EnsureCRLF(L"\nSome\r\r\n\nthing\n"));
	EXPECT_EQ(L"\r\nSome\r\n\r\n\r\nthing\r\n", StringUtils::EnsureCRLF(L"\nSome\r\r\n\rthing\n"));
	EXPECT_EQ(L"\r\nSome\r\n\r\nthing\r\n", StringUtils::EnsureCRLF(L"\nSome\r\n\r\nthing\n"));
	EXPECT_EQ(L"\r\nSome\r\n\r\nthing\r\n", StringUtils::EnsureCRLF(L"\nSome\r\n\rthing\r"));
	EXPECT_EQ(L"\r\nSome\r\n\r\n\r\nthing\r\n", StringUtils::EnsureCRLF(L"\nSome\n\r\rthing\n"));
}


TEST(CStringUtils, TrimRightA) {
	std::string_view sv1{};
	StringUtils::TrimRight(sv1);
	EXPECT_EQ(0u, sv1.size());

	std::string_view sv2{ "Hallo " };
	StringUtils::TrimRight(sv2);
	EXPECT_EQ(5u, sv2.size());
	EXPECT_TRUE(sv2 == "Hallo");

	std::string_view sv3{ " Hallo " };
	StringUtils::TrimRight(sv3);
	EXPECT_EQ(6u, sv3.size());
	EXPECT_TRUE(sv3 == " Hallo");

	std::string_view sv4{ "Hallo \r\n " };
	StringUtils::TrimRight(sv4);
	EXPECT_EQ(5u, sv4.size());
	EXPECT_TRUE(sv4 == "Hallo");

	std::string_view sv5{ "   \n " };
	StringUtils::TrimRight(sv5);
	EXPECT_EQ(0u, sv5.size());
}


TEST(CStringUtils, TrimRightA_Custom) {
	std::string_view sv1{};
	sv1 = StringUtils::TrimRight(sv1, "\r\n");
	EXPECT_EQ(0u, sv1.size());

	std::string_view sv2{ "Hallo " };
	sv2 = StringUtils::TrimRight(sv2, "\r\n");
	EXPECT_EQ(6u, sv2.size());
	EXPECT_TRUE(sv2 == "Hallo ");
	sv2 = StringUtils::TrimRight(sv2, " \r\n");
	EXPECT_EQ(5u, sv2.size());
	EXPECT_TRUE(sv2 == "Hallo");

	std::string_view sv4{ "\nHallo\r\n" };
	sv4 = StringUtils::TrimRight(sv4, "\r\n");
	EXPECT_EQ(6u, sv4.size());
	EXPECT_TRUE(sv4 == "\nHallo");
}


TEST(CStringUtils, TrimRightW) {
	std::wstring_view sv1{};
	StringUtils::TrimRight(sv1);
	EXPECT_EQ(0u, sv1.size());

	std::wstring_view sv2{ L"Hallo " };
	StringUtils::TrimRight(sv2);
	EXPECT_EQ(5u, sv2.size());
	EXPECT_TRUE(sv2 == L"Hallo");

	std::wstring_view sv3{ L" Hallo " };
	StringUtils::TrimRight(sv3);
	EXPECT_EQ(6u, sv3.size());
	EXPECT_TRUE(sv3 == L" Hallo");

	std::wstring_view sv4{ L"Hallo \r\n " };
	StringUtils::TrimRight(sv4);
	EXPECT_EQ(5u, sv4.size());
	EXPECT_TRUE(sv4 == L"Hallo");

	std::wstring_view sv5{ L"   \n " };
	StringUtils::TrimRight(sv5);
	EXPECT_EQ(0u, sv5.size());
}


TEST(CStringUtils, TrimLeftA) {
	std::string_view sv1{};
	StringUtils::TrimLeft(sv1);
	EXPECT_EQ(0u, sv1.size());

	std::string_view sv2{ " Hallo" };
	StringUtils::TrimLeft(sv2);
	EXPECT_EQ(5u, sv2.size());
	EXPECT_TRUE(sv2 == "Hallo");

	std::string_view sv3{ " Hallo " };
	StringUtils::TrimLeft(sv3);
	EXPECT_EQ(6u, sv3.size());
	EXPECT_TRUE(sv3 == "Hallo ");

	std::string_view sv4{ " \t Hallo" };
	StringUtils::TrimLeft(sv4);
	EXPECT_EQ(5u, sv4.size());
	EXPECT_TRUE(sv4 == "Hallo");

	std::string_view sv5{ "   \n " };
	StringUtils::TrimLeft(sv5);
	EXPECT_EQ(0u, sv5.size());
}


CString escaper(const CString& s) {
	const std::wstring e = TCmdLineParser::EscapeValue(s);
	return CString(e.data(), static_cast<int>(e.size()));
}


TEST(CStringUtils, ExpandPlaceholdersForCmd) {
	EXPECT_EQ(
		L"",
		StringUtils::ExpandPlaceholdersForCmd(L"", {
			{ L"1", L"sth" },
			{ L"2", L"sth" },
			{ L"3", L"sth" },
			{ L"4", L"sth" },
		}, escaper));
	EXPECT_EQ(
		L"%5",
		StringUtils::ExpandPlaceholdersForCmd(L"%5", {
			{ L"1", L"sth" },
			{ L"2", L"sth" },
			{ L"3", L"sth" },
			{ L"4", L"sth" },
		}, escaper));
	EXPECT_EQ(
		L"\"arg1\"",
		StringUtils::ExpandPlaceholdersForCmd(L"%1", {
			{ L"1", L"arg1" },
			{ L"2", L"Arg2" },
			{ L"3", L"aRg3" },
			{ L"4", L"arG4" },
		}, escaper));
	EXPECT_EQ(
		L"\"arg1\"",
		StringUtils::ExpandPlaceholdersForCmd(L"\"%1\"", {
			{ L"1", L"arg1" },
			{ L"2", L"Arg2" },
			{ L"3", L"aRg3" },
			{ L"4", L"arG4" },
		}, escaper));
	EXPECT_EQ(
		L"/to:\"arg1\" /from:\"Arg2\"",
		StringUtils::ExpandPlaceholdersForCmd(L"/to:%1 /from:%2", {
			{ L"1", L"arg1" },
			{ L"2", L"Arg2" },
			{ L"3", L"aRg3" },
			{ L"4", L"arG4" },
		}, escaper));
	EXPECT_EQ(
		L"/to:\"arg1\" /from:\"Arg2\"",
		StringUtils::ExpandPlaceholdersForCmd(L"/to:\"%1\" /from:%2", {
			{ L"1", L"arg1" },
			{ L"2", L"Arg2" },
			{ L"3", L"aRg3" },
			{ L"4", L"arG4" },
		}, escaper));
	EXPECT_EQ(
		L"/to:\"ar%2g1\" /from:\"Ar\"\"g2\"",
		StringUtils::ExpandPlaceholdersForCmd(L"/to:\"%1\" /from:%2", {
			{ L"1", L"ar%2g1" },
			{ L"2", L"Ar\"g2" },
			{ L"3", L"aRg3" },
			{ L"4", L"arG4" },
		}, escaper));

	EXPECT_EQ(
		L"/to:\"Arg2\" /from:\"aRRRRRRg\" /empty:\"\" /random:\"aRRRRRRg\" bla",
		StringUtils::ExpandPlaceholdersForCmd(L"/to:\"%to\" /from:%from /empty:%empty /random:%from bla", {
			{ L"to", L"Arg2" },
			{ L"from", L"aRRRRRRg" },
			{ L"empty", L""},
			{ L"something", L"arG4" },
		}, escaper));
}


TEST(CStringUtils, EscapeWindowsCliArguments) {
	EXPECT_EQ(L"\"\"", StringUtils::EscapeWindowsCliArguments(L""));
	EXPECT_EQ(L"\" \"", StringUtils::EscapeWindowsCliArguments(L" "));
	EXPECT_EQ(L"\"hello\"", StringUtils::EscapeWindowsCliArguments(L"hello"));
	EXPECT_EQ(L"\"some\\path with\\spaces\"", StringUtils::EscapeWindowsCliArguments(L"some\\path with\\spaces"));
	EXPECT_EQ(L"\"\\\\\"", StringUtils::EscapeWindowsCliArguments(L"\\"));
	EXPECT_EQ(L"\"D:\\path\"", StringUtils::EscapeWindowsCliArguments(L"D:\\path"));
	EXPECT_EQ(L"\"D:\\path\\\\\"", StringUtils::EscapeWindowsCliArguments(L"D:\\path\\"));
	EXPECT_EQ(L"\"argument\\\"2\"", StringUtils::EscapeWindowsCliArguments(L"argument\"2"));
	EXPECT_EQ(L"\"\\\\\\\"\"", StringUtils::EscapeWindowsCliArguments(L"\\\""));
	EXPECT_EQ(L"\"\\\\\\\\\\\"\"", StringUtils::EscapeWindowsCliArguments(L"\\\\\""));
}
