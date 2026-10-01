// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2016, 2023 - TortoiseGit

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
#include "FileTextLines.h"

TEST(CFileTextLines, CheckUnicodeType)
{
	constexpr char ascii[] = "some text";
	// This text contains the umlaut "ä".
	constexpr unsigned char ansi[] = { 0x54, 0x68, 0x69, 0x73, 0x20, 0x74, 0x65, 0x78, 0x74, 0x20, 0x63, 0x6F, 0x6E, 0x74, 0x61, 0x69, 0x6E, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x75, 0x6D, 0x6C, 0x61, 0x75, 0x74, 0x20, 0x22, 0xE4, 0x22, 0x2E };
	constexpr unsigned char utf8[] = { 0x54, 0x68, 0x69, 0x73, 0x20, 0x74, 0x65, 0x78, 0x74, 0x20, 0x63, 0x6F, 0x6E, 0x74, 0x61, 0x69, 0x6E, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x75, 0x6D, 0x6C, 0x61, 0x75, 0x74, 0x20, 0x22, 0xC3, 0xA4, 0x22, 0x2E };
	constexpr unsigned char utf8bom[] = { 0xEF, 0xBB, 0xBF, 0x54, 0x68, 0x69, 0x73, 0x20, 0x74, 0x65, 0x78, 0x74, 0x20, 0x63, 0x6F, 0x6E, 0x74, 0x61, 0x69, 0x6E, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x75, 0x6D, 0x6C, 0x61, 0x75, 0x74, 0x20, 0x22, 0xC3, 0xA4, 0x22, 0x2E };
	constexpr unsigned char utf16be[] = { 0x00, 0x54, 0x00, 0x68, 0x00, 0x69, 0x00, 0x73, 0x00, 0x20, 0x00, 0x74, 0x00, 0x65, 0x00, 0x78, 0x00, 0x74, 0x00, 0x20, 0x00, 0x63, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6E, 0x00, 0x73, 0x00, 0x20, 0x00, 0x74, 0x00, 0x68, 0x00, 0x65, 0x00, 0x20, 0x00, 0x75, 0x00, 0x6D, 0x00, 0x6C, 0x00, 0x61, 0x00, 0x75, 0x00, 0x74, 0x00, 0x20, 0x00, 0x22, 0x00, 0xE4, 0x00, 0x22, 0x00, 0x2E };
	constexpr unsigned char utf16bebom[] = { 0xFE, 0xFF, 0x00, 0x54, 0x00, 0x68, 0x00, 0x69, 0x00, 0x73, 0x00, 0x20, 0x00, 0x74, 0x00, 0x65, 0x00, 0x78, 0x00, 0x74, 0x00, 0x20, 0x00, 0x63, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6E, 0x00, 0x73, 0x00, 0x20, 0x00, 0x74, 0x00, 0x68, 0x00, 0x65, 0x00, 0x20, 0x00, 0x75, 0x00, 0x6D, 0x00, 0x6C, 0x00, 0x61, 0x00, 0x75, 0x00, 0x74, 0x00, 0x20, 0x00, 0x22, 0x00, 0xE4, 0x00, 0x22, 0x00, 0x2E };
	constexpr unsigned char utf16le[] = { 0x54, 0x00, 0x68, 0x00, 0x69, 0x00, 0x73, 0x00, 0x20, 0x00, 0x74, 0x00, 0x65, 0x00, 0x78, 0x00, 0x74, 0x00, 0x20, 0x00, 0x63, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6E, 0x00, 0x73, 0x00, 0x20, 0x00, 0x74, 0x00, 0x68, 0x00, 0x65, 0x00, 0x20, 0x00, 0x75, 0x00, 0x6D, 0x00, 0x6C, 0x00, 0x61, 0x00, 0x75, 0x00, 0x74, 0x00, 0x20, 0x00, 0x22, 0x00, 0xE4, 0x00, 0x22, 0x00, 0x2E, 0x00 };
	constexpr unsigned char utf16lebom[] = { 0xFF, 0xFE, 0x54, 0x00, 0x68, 0x00, 0x69, 0x00, 0x73, 0x00, 0x20, 0x00, 0x74, 0x00, 0x65, 0x00, 0x78, 0x00, 0x74, 0x00, 0x20, 0x00, 0x63, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6E, 0x00, 0x73, 0x00, 0x20, 0x00, 0x74, 0x00, 0x68, 0x00, 0x65, 0x00, 0x20, 0x00, 0x75, 0x00, 0x6D, 0x00, 0x6C, 0x00, 0x61, 0x00, 0x75, 0x00, 0x74, 0x00, 0x20, 0x00, 0x22, 0x00, 0xE4, 0x00, 0x22, 0x00, 0x2E, 0x00 };

	CFileTextLines ftl;
	EXPECT_EQ(CFileTextLines::UnicodeType::ASCII, ftl.CheckUnicodeType(ascii, sizeof(ascii) - 1));
	EXPECT_EQ(CFileTextLines::UnicodeType::ASCII, ftl.CheckUnicodeType(ansi, sizeof(ansi)));
	EXPECT_EQ(CFileTextLines::UnicodeType::UTF8, ftl.CheckUnicodeType(utf8, sizeof(utf8)));
	EXPECT_EQ(CFileTextLines::UnicodeType::UTF8BOM, ftl.CheckUnicodeType(utf8bom, sizeof(utf8bom)));
	EXPECT_EQ(CFileTextLines::UnicodeType::UTF16_BE, ftl.CheckUnicodeType(utf16be, sizeof(utf16be)));
	EXPECT_EQ(CFileTextLines::UnicodeType::UTF16_BEBOM, ftl.CheckUnicodeType(utf16bebom, sizeof(utf16bebom)));
	EXPECT_EQ(CFileTextLines::UnicodeType::UTF16_LE, ftl.CheckUnicodeType(utf16le, sizeof(utf16le)));
	EXPECT_EQ(CFileTextLines::UnicodeType::UTF16_LEBOM, ftl.CheckUnicodeType(utf16lebom, sizeof(utf16lebom)));
}

namespace
{
template <size_t N>
std::string_view Bytes(const unsigned char (&bytes)[N])
{
	return { reinterpret_cast<const char*>(bytes), N };
}

// "A", U+1F600 (outside the BMP, so a surrogate pair in UTF-16), "B".
// Note the string concatenation: L"\xDE00B" would be one hex escape.
const std::wstring kAEmojiB = L"A\xD83D\xDE00" L"B";
constexpr unsigned char kAEmojiBUtf32le[] = { 0x41, 0x00, 0x00, 0x00, 0x00, 0xF6, 0x01, 0x00, 0x42, 0x00, 0x00, 0x00 };
constexpr unsigned char kAEmojiBUtf32be[] = { 0x00, 0x00, 0x00, 0x41, 0x00, 0x01, 0xF6, 0x00, 0x00, 0x00, 0x00, 0x42 };
}

// The decoder used to report the UTF-32 code point count as the text's length,
// though every code point outside the BMP takes two UTF-16 units - so the text
// lost one unit off its end per such character. Here the result was "A" and the
// surrogate pair, without the "B".
TEST(CFileTextLines, Utf32DecodeKeepsTheTailAfterNonBmpCharacters)
{
	const CUtf32leFilter le(nullptr);
	const auto decodedLe = le.Decode(Bytes(kAEmojiBUtf32le));
	ASSERT_TRUE(decodedLe.has_value());
	EXPECT_EQ(4u, decodedLe->size());
	EXPECT_EQ(kAEmojiB, *decodedLe);

	const CUtf32beFilter be(nullptr);
	const auto decodedBe = be.Decode(Bytes(kAEmojiBUtf32be));
	ASSERT_TRUE(decodedBe.has_value());
	EXPECT_EQ(kAEmojiB, *decodedBe);
}

// The surrogate arithmetic itself, on a code point that needs every bit of both
// halves: U+10FFFF is the last one, lead 0xDBFF, trail 0xDFFF.
TEST(CFileTextLines, Utf32DecodeSurrogatesAtTheTopOfTheRange)
{
	constexpr unsigned char lastCodePoint[] = { 0xFF, 0xFF, 0x10, 0x00 };
	constexpr unsigned char pastTheEnd[] = { 0x00, 0x00, 0x11, 0x00 }; // 0x110000: not a code point
	const CUtf32leFilter le(nullptr);
	EXPECT_EQ(std::wstring(L"\xDBFF\xDFFF"), le.Decode(Bytes(lastCodePoint)).value_or(L"<nullopt>"));
	EXPECT_EQ(std::wstring(L"\xFFFD"), le.Decode(Bytes(pastTheEnd)).value_or(L"<nullopt>"));
}

// A *spurious* conjecture, kept as a pin: the decoder masked the trail surrogate
// with 0x7ff instead of 0x3ff, which looked like it would leak bit 10 of the
// code point into the unit. It cannot: 0xDC00 already has bit 10 set. U+1F600
// has that bit set in its low ten-plus-one bits (0xF600 & 0x400) and still
// decodes to DE00 either way. Old code: PASSES (for this character).
TEST(CFileTextLines, Utf32DecodeTrailSurrogateMaskWasHarmless)
{
	constexpr unsigned char grinning[] = { 0x00, 0xF6, 0x01, 0x00 }; // U+1F600
	const CUtf32leFilter le(nullptr);
	EXPECT_EQ(std::wstring(L"\xD83D\xDE00"), le.Decode(Bytes(grinning)).value_or(L"<nullopt>"));
}

// The encoder used to size its buffer as 2 bytes per UTF-16 unit and then write
// 4 bytes per code point. CBuffer rounded allocations up by about 1K, which hid
// the overrun for short lines; a line of 2000 characters wrote ~4000 bytes past
// the block. Before the fix this corrupts the heap - the debug CRT reports it
// when the buffer is freed - rather than failing an expectation.
TEST(CFileTextLines, Utf32EncodeLongLineIsFourBytesPerCharacter)
{
	const std::wstring line(2000, L'x');
	CUtf32leFilter le(nullptr);
	const std::string encoded = le.Encode(line); // copy: Encode's result is only valid until the next call
	ASSERT_EQ(line.size() * 4, encoded.size());
	for (size_t i = 0; i < encoded.size(); i += 4)
	{
		ASSERT_EQ('x', encoded[i]) << "at byte " << i;
		ASSERT_EQ('\0', encoded[i + 1]);
		ASSERT_EQ('\0', encoded[i + 2]);
		ASSERT_EQ('\0', encoded[i + 3]);
	}
}

TEST(CFileTextLines, Utf32EncodeJoinsSurrogatePairsAndRoundTrips)
{
	CUtf32leFilter le(nullptr);
	EXPECT_EQ(Bytes(kAEmojiBUtf32le), le.Encode(kAEmojiB));
	EXPECT_EQ(kAEmojiB, le.Decode(le.Encode(kAEmojiB)).value_or(L"<nullopt>"));

	CUtf32beFilter be(nullptr);
	EXPECT_EQ(Bytes(kAEmojiBUtf32be), be.Encode(kAEmojiB));
	EXPECT_EQ(kAEmojiB, be.Decode(be.Encode(kAEmojiB)).value_or(L"<nullopt>"));

	// an unpaired surrogate is not a code point
	constexpr unsigned char replacement[] = { 0xFD, 0xFF, 0x00, 0x00 };
	EXPECT_EQ(Bytes(replacement), le.Encode(L"\xD83D"));
	EXPECT_EQ(Bytes(replacement), le.Encode(L"\xDE00"));
}

// Not a deduced bug: a guard for the rewrite that replaced the hand-rolled
// quad-word byte swapping with std::byteswap. An odd trailing byte is dropped.
TEST(CFileTextLines, Utf16beRoundTrips)
{
	constexpr unsigned char be[] = { 0x00, 0x41, 0xD8, 0x3D, 0xDE, 0x00, 0x00, 0x42, 0x7F };
	CUtf16beFilter filter(nullptr);
	EXPECT_EQ(kAEmojiB, filter.Decode(Bytes(be)).value_or(L"<nullopt>"));
	EXPECT_EQ(Bytes(be).substr(0, 8), filter.Encode(kAEmojiB));
}
