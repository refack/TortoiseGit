// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2023, 2025-2026 - TortoiseGit

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
#define GIT_HASH_SIZE 20

/* also see gitdll.c */
static_assert(GIT_HASH_SIZE <= sizeof(git_oid::id), "git_oid raw storage must be able to hold GIT_HASH_SIZE bytes");

#define GIT_REV_ZERO_C "0000000000000000000000000000000000000000"
#define GIT_REV_ZERO _T(GIT_REV_ZERO_C)

class CGitHash;
template<>
struct std::hash<CGitHash>;

class CGitHash
{
private:
	git_oid m_oid{};

public:
	CGitHash() = default;
	CGitHash(const git_oid* oid)
	{
		git_oid_cpy(&m_oid, oid);
	}
	CGitHash(const git_oid& oid)
	{
		git_oid_cpy(&m_oid, &oid);
	}
	CGitHash& operator = (const git_oid* oid)
	{
		git_oid_cpy(&m_oid, oid);
		return *this;
	}
	CGitHash& operator = (const git_oid& oid)
	{
		git_oid_cpy(&m_oid, &oid);
		return *this;
	}

#ifdef TGIT_TESTS_ONLY
	static CGitHash FromHexStr(const wchar_t* str, bool* isHash = nullptr)
	{
		return FromHexStr(std::wstring_view(str), isHash);
	}
#endif

	template <typename T>
	static CGitHash FromHexStr(const T& str, bool* isHash = nullptr)
	{
		static_assert(std::is_assignable_v<T, const CString&> || std::is_assignable_v<T, const CStringA&> || std::is_assignable_v<T, const std::string_view&> || std::is_assignable_v<T, const std::wstring_view&>, "only applicable to 'const CString&', 'const CStringA&', 'std::string_view&' and 'std::string_view&'");
		if constexpr (!(std::is_assignable_v<T, const std::string_view&> || std::is_assignable_v<T, const std::wstring_view&>))
		{
			if (str.GetLength() != 2 * GIT_HASH_SIZE)
			{
				if (isHash)
					*isHash = false;
				return CGitHash();
			}
		}
		else
		{
			if (str.size() != 2 * GIT_HASH_SIZE)
			{
				if (isHash)
					*isHash = false;
				return CGitHash();
			}
		}

		CGitHash hash;
		for (int i = 0; i < GIT_HASH_SIZE; ++i)
		{
			unsigned char a = 0;
			for (int j = 2 * i; j <= 2 * i + 1; ++j)
			{
				a = a << 4;

				const auto ch = str[j];
				static_assert('_' == L'_', "This method expects that char and wchar_t literals are comparable for ASCII characters");
				if (ch >= '0' && ch <= '9')
					a |= (ch - '0') & 0xF;
				else if (ch >= 'A' && ch <= 'F')
					a |= ((ch - 'A') & 0xF) + 10;
				else if (ch >= 'a' && ch <= 'f')
					a |= ((ch - 'a') & 0xF) + 10;
				else
				{
					if (isHash)
						*isHash = false;
					return CGitHash();
				}
			}
			hash.m_oid.id[i] = a;
		}
		if (isHash)
			*isHash = true;
		return hash;
	}

	static CGitHash FromRaw(const unsigned char* raw)
	{
		CGitHash hash;
		memcpy(hash.m_oid.id, raw, GIT_HASH_SIZE);
		return hash;
	}

	void Empty()
	{
		memset(m_oid.id, 0, GIT_HASH_SIZE);
	}
	inline bool IsEmpty() const
	{
		static const unsigned char empty[GIT_HASH_SIZE]{};
		return memcmp(m_oid.id, empty, GIT_HASH_SIZE) == 0;
	}

	CString ToString() const
	{
		CString str;
		str.Preallocate(GIT_HASH_SIZE * 2);
		for (int i = 0; i < GIT_HASH_SIZE; ++i)
			str.AppendFormat(L"%02x", m_oid.id[i]);
		return str;
	}

	CString ToString(int len) const
	{
		ASSERT(len >= 0 && len <= GIT_HASH_SIZE * 2);
		CString str { ToString() };
		str.Truncate(len);
		return str;
	}

	operator bool() = delete;

	operator const git_oid*() const
	{
		return &m_oid;
	}

	const unsigned char* ToRaw() const
	{
		return m_oid.id;
	}

	bool operator == (const CGitHash &hash) const
	{
		return memcmp(m_oid.id, hash.m_oid.id, GIT_HASH_SIZE) == 0;
	}

	bool operator<(const CGitHash& other) const
	{
		return memcmp(m_oid.id, other.m_oid.id, GIT_HASH_SIZE) < 0;
	}

	bool operator>(const CGitHash& other) const
	{
		return memcmp(m_oid.id, other.m_oid.id, GIT_HASH_SIZE) > 0;
	}

	bool operator!=(const CGitHash& other) const
	{
		return memcmp(m_oid.id, other.m_oid.id, GIT_HASH_SIZE) != 0;
	}

	bool MatchesPrefix(const CGitHash& hash, const CString& hashString, size_t prefixLen) const
	{
		if (memcmp(m_oid.id, hash.m_oid.id, prefixLen >> 1))
			return false;
		return prefixLen == 2 * GIT_HASH_SIZE || wcsncmp(ToString(), hashString, prefixLen) == 0;
	}

	friend struct std::hash<CGitHash>;
};

namespace std
{
	template <>
	struct hash<CGitHash>
	{
		/*
		 * Converts a cryptographic hash (such as SHA-1) into a hash value. Since cryptographic
		 * hashes are designed to be uniformly distributed, this function simply copies the first bytes.
		 * The resulting value depends on platform endianness, so it should not be stored
		 * or transmitted across systems.
		 */
		std::size_t operator()(const CGitHash& k) const
		{
			static_assert(sizeof(size_t) <= GIT_HASH_SIZE);
			size_t hash;
			// this makes sure that all reads to the size_t value are aligned
			memcpy(&hash, k.m_oid.id, sizeof(hash));
			return hash;
		}
	};
}
