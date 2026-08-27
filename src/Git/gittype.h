// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2017, 2019-2021, 2023, 2026 - TortoiseGit

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
#include "GitHash.h"
#include "UnicodeUtils.h"
#include <unordered_map>
#include <span>
#include <string>

enum
{
	TGIT_GIT_SUCCESS=0,
	TGIT_GIT_ERROR_OPEN_PIP,
	TGIT_GIT_ERROR_CREATE_PROCESS,
	TGIT_GIT_ERROR_GET_EXIT_CODE
};

class CGitByteArray : private std::vector<char>
{
public:
	size_t find(const char data, const size_t start = 0) const
	{
		const size_t end = size();
		for (size_t i = start; i < end; ++i)
			if ((*this)[i] == data)
				return i;
		return npos;
	}
	size_t RevertFind(const char data, size_t start = npos) const
	{
		if (start == npos)
		{
			if (empty())
				return npos;
			start = size() - 1;
		}

		for (size_t i = start + 1; i-- > 0;)
			if ((*this)[i] == data)
				return i;
		return npos;
	}
	size_t findNextString(const size_t start = 0) const
	{
		size_t pos = start;
		const size_t end = size();
		do
		{
			pos=find(0,pos);
			if (pos == npos)
				break;
			++pos;
			if (pos >= end)
				return npos;

		} while ((*this)[pos] == 0);

		return pos;
	}

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
	inline void append(const char* data, size_t dataSize)
	{
		append(std::span<const char>(data, dataSize));
	}
#endif

	inline void append(const std::span<const char> v)
	{
		insert(this->end(), v.begin(), v.end());
	}

	/// Decodes the buffer as UTF-8 text, stopping at the first NUL.
	///
	/// This was an implicit `operator CString()`, which meant bytes became text
	/// wherever a BYTE_VECTOR happened to land in a CString context. Two things
	/// were invisible at such a call site and both are load-bearing: the buffer
	/// is *assumed* to be UTF-8, and everything from the first NUL on is
	/// discarded - which matters because git's own output is frequently
	/// NUL-separated (`-z`, `ls-files --stage -z`, `for-each-ref` with a NUL
	/// terminator), so an accidental decode of such a buffer silently yields
	/// only its first record. Naming it makes the assumption and the truncation
	/// visible; the behaviour is exactly what the operator did.
	///
	/// For a whole, NUL-containing buffer use the bytes directly - `data()` and
	/// `size()` - and decode each record separately.
	[[nodiscard]] std::wstring Decode() const
	{
		if (empty())
			return {};

		return CUnicodeUtils::StdGetUnicode(std::string_view(data(), strnlen_s(data(), size())));
	}

	static const size_t npos = static_cast<size_t>(-1); // bad/missing length/position
	static_assert(MAXSIZE_T == npos, "NPOS must equal MAXSIZE_T");
	static_assert(-1 == static_cast<int>(npos), "NPOS must equal -1");

	using std::vector<char>::begin;
	using std::vector<char>::end;
	using std::vector<char>::cbegin;
	using std::vector<char>::cend;
	using std::vector<char>::clear;
	using std::vector<char>::data;
	using std::vector<char>::empty;
	using std::vector<char>::erase;
	using std::vector<char>::pop_back;
	using std::vector<char>::push_back;
	using std::vector<char>::size;
	using std::vector<char>::operator[];
};

struct TGitRef
{
	std::wstring name;
	CGitHash hash;
	// There was an `operator const CString&() const { return name; }` here. Same
	// disease as CGitByteArray's operator CString(): it let a whole ref stand in
	// for its name silently, so a call site could not be read without knowing the
	// overload set around it. Say `.name`.
};

using STRING_VECTOR = std::vector<CString>;
using MAP_HASH_NAME = std::unordered_map<CGitHash, STRING_VECTOR>;
// std::wstring rather than CString, for the reason WideString.h opens with:
// CString is a different type in an MFC project than in an ATL one, so an alias
// naming it cannot appear in a header both flavors compile. Values, not views -
// these containers own their strings and outlive whatever produced them.
using MAP_STRING_STRING = std::map<std::wstring, std::wstring>;
using REF_VECTOR = std::vector<TGitRef>;
using BYTE_VECTOR = CGitByteArray;
