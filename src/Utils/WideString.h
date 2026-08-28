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

// Helpers for carrying src\Git and src\Utils from CString to std::wstring.
//
// Why this exists at all: CString is not one type. In an MFC-Dynamic project it
// is CStringT<wchar_t, StrTraitMFC_DLL<...>>; in an ATL-only project it is
// CStringT<wchar_t, StrTraitATL<...>>. Those mangle differently, so a single
// compiled library cannot expose CString in its headers and serve both
// TortoiseGitProc and TortoiseGit.dll. std::wstring is one type in both, and on
// Windows it is already what LPCWSTR points at, so the boundary stays free.
//
// Three of the CString idioms this code uses do NOT translate by renaming, and
// each one fails silently rather than at compile time. They are the reason this
// header exists rather than a search-and-replace:
//
//   CString::Find returns -1 when absent; std::wstring::find returns npos,
//   which is SIZE_MAX. So `if (s.Find(L"x") >= 0)` becomes always-true when
//   renamed to find(). Every one of those is a live bug that compiles.
//
//   CString::GetLength returns int; size() returns size_t. `s.GetLength() - 1`
//   on an empty string is -1, while `s.size() - 1` is SIZE_MAX, and a signed
//   loop counter compared against size() changes meaning under promotion.
//
//   CString::Mid and ::Left clamp their arguments; substr throws
//   std::out_of_range past the end. Code that relies on the clamp turns a
//   correct result into an exception.
//
// So the functions below deliberately keep CString's contract - int offsets,
// -1 for absent, clamping - so that converting a call site is a rename and
// nothing more. That is the whole argument for choosing std::wstring over
// std::u16string: the migration stays mechanical and the compiler checks it.
// NEW code should prefer std::wstring's own members and npos; these are for
// carrying existing call sites across without re-deciding each one.

#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cwctype>
#include <format>

namespace tgit::wstr
{
/// Whitespace CString::Trim() removes, so a converted call site keeps behaving.
inline constexpr std::wstring_view kDefaultTrimChars = L" \t\r\n";

/// CString::Find contract: byte offset, or -1 when absent.
inline int Find(const std::wstring_view s, const std::wstring_view needle, const int from = 0) noexcept
{
	if (from < 0 || static_cast<size_t>(from) > s.size())
		return -1;
	const size_t at = s.find(needle, static_cast<size_t>(from));
	return at == std::wstring_view::npos ? -1 : static_cast<int>(at);
}

/// CString::Find contract: byte offset, or -1 when absent.
inline int Find(const std::wstring_view s, const wchar_t needle, const int from = 0) noexcept
{
	if (from < 0 || static_cast<size_t>(from) > s.size())
		return -1;
	const size_t at = s.find(needle, static_cast<size_t>(from));
	return at == std::wstring_view::npos ? -1 : static_cast<int>(at);
}

/// CString::ReverseFind contract: byte offset, or -1 when absent.
inline int ReverseFind(const std::wstring_view s, const wchar_t needle) noexcept
{
	const size_t at = s.rfind(needle);
	return at == std::wstring_view::npos ? -1 : static_cast<int>(at);
}

/// CString::FindOneOf contract: byte offset, or -1 when absent.
inline int FindOneOf(const std::wstring_view s, const std::wstring_view chars) noexcept
{
	const size_t at = s.find_first_of(chars);
	return at == std::wstring_view::npos ? -1 : static_cast<int>(at);
}

inline std::wstring Left(const std::wstring_view s, const size_t count) {
	return std::wstring{ s.substr(0, count) };
}

/// CString::Left, including its clamping: substr() would give the full str.
inline std::wstring Left(const std::wstring_view s, const int _count)
{
	if (_count < 0)
		return {};
	const auto count = static_cast<size_t>(_count);
	return Left(s, count);
}

/// CString::Right, including its clamping.
inline std::wstring Right(const std::wstring_view s, const long _count)
{
	if (_count < 0)
		return {};
	auto count = static_cast<std::wstring_view::size_type>(_count);
	const size_t take = std::min(count, s.size());
	return std::wstring(s.substr(s.size() - take));
}

inline std::wstring Mid(const std::wstring_view s, const std::wstring_view::size_type from, const std::wstring_view::size_type count = 0) {
	if (from > s.size())
		return {};
	return std::wstring{ s.substr(from, count) };
}

/// CString::Mid(from, count = 0), including its clamping.
inline std::wstring Mid(const std::wstring_view s, const int _from, const int _count = 0)
{
	if (_count < 0)
		return {};
	auto count = static_cast<std::wstring_view::size_type>(_count);
	if (_from < 0)
		return Left(s, count);
	auto from = static_cast<std::wstring_view::size_type>(_from);
	return Mid(s, from, count);
}

/// CString::Replace contract, including its return value: how many were replaced.
inline int Replace(std::wstring& s, const std::wstring_view from, const std::wstring_view to)
{
	if (from.empty())
		return 0;
	int replaced = 0;
	for (size_t at = s.find(from); at != std::wstring::npos; at = s.find(from, at + to.size()))
	{
		s.replace(at, from.size(), to);
		++replaced;
	}
	return replaced;
}

/// CString::Replace(wchar_t, wchar_t) contract.
inline int Replace(std::wstring& s, const wchar_t from, const wchar_t to) noexcept
{
	const int replaced = static_cast<int>(std::count(s.begin(), s.end(), from));
	if (replaced)
		std::replace(s.begin(), s.end(), from, to);
	return replaced;
}

inline void TrimLeft(std::wstring& s, const std::wstring_view chars = kDefaultTrimChars)
{
	s.erase(0, (std::min)(s.find_first_not_of(chars), s.size()));
}

inline void TrimRight(std::wstring& s, const std::wstring_view chars = kDefaultTrimChars)
{
	const size_t last = s.find_last_not_of(chars);
	s.erase(last == std::wstring::npos ? 0 : last + 1);
}

inline void Trim(std::wstring& s, const std::wstring_view chars = kDefaultTrimChars)
{
	TrimRight(s, chars);
	TrimLeft(s, chars);
}

inline void MakeLower(std::wstring& s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
}

inline void MakeUpper(std::wstring& s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
}

/// CString::CompareNoCase contract: <0, 0 or >0.
inline int CompareNoCase(const std::wstring_view lhs, const std::wstring_view rhs) noexcept
{
	const size_t common = (std::min)(lhs.size(), rhs.size());
	for (size_t i = 0; i < common; ++i)
	{
		const wint_t l = std::towlower(lhs[i]);
		const wint_t r = std::towlower(rhs[i]);
		if (l != r)
			return l < r ? -1 : 1;
	}
	if (lhs.size() == rhs.size())
		return 0;
	return lhs.size() < rhs.size() ? -1 : 1;
}

inline bool StartsWith(const std::wstring_view s, const std::wstring_view prefix) noexcept
{
	return s.starts_with(prefix);
}

inline bool EndsWith(const std::wstring_view s, const std::wstring_view suffix) noexcept
{
	return s.ends_with(suffix);
}

/// CString::Tokenize's contract, which IS strtok-like: leading delimiters are
/// skipped, so a run of them yields one token and never an empty one, and
/// "a//b" is two tokens rather than three. Returns the token and updates pos;
/// pos < 0 means exhausted, and the token returned with it is empty.
///
/// This is the one helper here whose behaviour I guessed wrong: the
/// differential test in WideStringTest.cpp caught it, which is the argument
/// for testing these against CString rather than against an expectation.
inline std::wstring Tokenize(const std::wstring_view s, const std::wstring_view delimiters, int& pos)
{
	if (pos < 0 || delimiters.empty() || static_cast<size_t>(pos) >= s.size())
	{
		pos = -1;
		return {};
	}
	const size_t start = s.find_first_not_of(delimiters, static_cast<size_t>(pos));
	if (start == std::wstring_view::npos)
	{
		pos = -1;
		return {};
	}
	size_t end = s.find_first_of(delimiters, start);
	if (end == std::wstring_view::npos)
		end = s.size();
	pos = static_cast<int>(end) + 1;
	return std::wstring(s.substr(start, end - start));
}

/// The GetBuffer/ReleaseBuffer pair, for the Win32 calls that fill a caller's
/// buffer. std::wstring guarantees contiguous storage, so this needs no copy;
/// what it does need is the resize back down, which ReleaseBuffer did.
inline void ReleaseBuffer(std::wstring& s, const int newLength = -1)
{
	if (newLength >= 0)
		s.resize((std::min)(static_cast<size_t>(newLength), s.size()));
	else
		s.resize(wcsnlen(s.c_str(), s.size()));
}
#if defined(_MFC_VER) || defined(CSTRING_AVAILABLE)
/// Non-owning view of a CString, for calling the std::wstring core from a call
/// site that has not migrated yet.
///
/// It exists to be *visible*. A CString reaches std::wstring_view only through
/// two user-defined conversions, so the compiler rejects it and each such call
/// has to say something; this is the shortest true thing it can say, and one
/// grep for it lists every place still holding a CString on the wrong side of
/// the boundary. Resist the temptation to make the core's parameters LPCWSTR
/// instead - a CString converts to that implicitly, so the boundary would
/// compile silently and nothing would ever pressure it to move.
///
/// O(1): CString knows its length, so this does not walk the string the way
/// std::wstring_view(cstring) would.
template <typename CharT, typename TraitsT>
[[nodiscard]] inline std::basic_string_view<CharT> View(const ATL::CStringT<CharT, TraitsT>& s) noexcept
{
	return { s.GetString(), static_cast<size_t>(s.GetLength()) };
}
#endif
} // namespace tgit::wstr

#if defined(_MFC_VER) || defined(CSTRING_AVAILABLE)
// Lets std::format take a CString wherever it already takes a std::wstring.
//
// This is what makes std::format the right fix for a concatenation chain
// rather than a cosmetic rewrite of one. The chains in this codebase mix
// CString, std::wstring, wchar_t and const wchar_t*, and every conversion
// between them is a place to get it wrong - appending .c_str() to two of them
// silently produces "const wchar_t* + const wchar_t*", which is pointer
// arithmetic that compiles. std::format has no such failure mode: each
// argument is formatted independently, so there is no operator+ to resolve and
// no mixed-type expression to reason about.
//
// Specialized on the traits parameter rather than on CString, deliberately:
// CString is CStringT<wchar_t, StrTraitMFC_DLL<...>> in an MFC project and
// CStringT<wchar_t, StrTraitATL<...>> in an ATL one, so naming either would
// only cover half the tree. This covers both, and CStringA too if a narrow
// format context ever needs it.
template <typename CharT, typename TraitsT>
struct std::formatter<ATL::CStringT<CharT, TraitsT>, CharT> : std::formatter<std::basic_string_view<CharT>, CharT>
{
	template <typename FormatContext>
	auto format(const ATL::CStringT<CharT, TraitsT>& value, FormatContext& ctx) const
	{
		return std::formatter<std::basic_string_view<CharT>, CharT>::format(
			std::basic_string_view<CharT>(value.GetString(), static_cast<size_t>(value.GetLength())), ctx);
	}
};
#endif
