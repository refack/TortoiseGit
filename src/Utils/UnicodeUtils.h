// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2009-2013, 2016, 2020, 2023, 2025-2026 - TortoiseGit
// Copyright (C) 2003-2007 - TortoiseSVN

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

#include <string>
#include <stdexcept>
#include <intsafe.h>

// Safely convert from size_t to int.
// Throws a std::overflow_error exception if the conversion is impossible.
inline int SafeSizeToInt(size_t sizeValue)
{
	constexpr int kIntMax = (std::numeric_limits<int>::max)();
	if (sizeValue >= static_cast<size_t>(kIntMax))
		throw std::overflow_error("size_t value is too big to fit into an int.");

	return static_cast<int>(sizeValue);
}

inline int SafeIntMult(int val1, int val2)
{
	int bufferSize;
	if (IntMult(val1, val2, &bufferSize) != S_OK)
		throw std::overflow_error("int multiplication result too big for an int.");
	return bufferSize;
}

/**
 * \ingroup Utils
 * Class to convert strings from/to UTF8 and UTF16.
 */
class CUnicodeUtils
{
public:
	CUnicodeUtils() = delete;

	// The std::string / std::wstring API is the real one, and the only one the
	// shared library may expose: CString is CStringT<..., StrTraitMFC_DLL> in an
	// MFC project and CStringT<..., StrTraitATL> in an ATL one, so it mangles
	// differently and cannot cross a library boundary that serves both.
	static std::string StdGetMulti(const std::wstring_view wide, int acp);
	static inline std::string StdGetUTF8(const std::wstring_view wide) { return StdGetMulti(wide, CP_UTF8); }
	static std::wstring StdGetUnicode(const std::string_view multibyte, int acp = CP_UTF8);
	static int StdGetCPCode(const std::wstring_view codename);

#if defined(_MFC_VER) || defined(CSTRING_AVAILABLE)
	// Thin adapters over the above, kept while call sites move across. They
	// used to carry their own copy of the WideCharToMultiByte /
	// MultiByteToWideChar dance, which meant a fix to one encoding edge case
	// had two places to land and no way to notice the second. Delete each of
	// these once nothing calls it; nothing here needs porting first.
	static inline CStringA GetMulti(const CStringW& string, int acp)
	{
		const std::string converted = StdGetMulti(std::wstring_view(string, string.GetLength()), acp);
		return CStringA(converted.data(), SafeSizeToInt(converted.size()));
	}
	static inline CStringA GetUTF8(const CStringW& string) { return GetMulti(string, CP_UTF8); }
	static inline CString GetUnicodeLength(const char* string, int len, int acp = CP_UTF8)
	{
		const std::wstring converted = StdGetUnicode(std::string_view(string, static_cast<size_t>(len)), acp);
		return CString(converted.data(), SafeSizeToInt(converted.size()));
	}
	static inline CString GetUnicode(const CStringA& string, int acp = CP_UTF8) { return GetUnicodeLength(string, string.GetLength(), acp); };
	static inline CString GetUnicode(const char* string, int acp = CP_UTF8) { return GetUnicode(std::string_view(string), acp); };
	static inline CString GetUnicode(const std::string_view string, int acp = CP_UTF8) { return GetUnicodeLength(string.data(), SafeSizeToInt(string.size()), acp); };
	static inline int GetCPCode(const CString& codename) { return StdGetCPCode(std::wstring_view(codename, codename.GetLength())); }
#endif
};

/* only used in TortoiseGitShell\ContextMenu.h */
std::string WideToMultibyte(const std::wstring_view wide);
std::wstring MultibyteToWide(const std::string_view multibyte);

int LoadStringEx(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer, int nBufferMax, WORD wLanguage);
