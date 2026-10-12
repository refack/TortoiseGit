module;
// git_oid_fromraw and the GIT_OID_* size macros: macros do not cross the
// invarients import, so take them from the header.
#include <git2/oid.h>


export module GitHash;
import std;

import invarients;
using namespace tgit::invarients;

import wstr;
using tgit::zstr::IsTCompString;
using tgit::zstr::StringViewTraits;


/* also see gitdll.c */
static_assert(OID_MAX_SIZE <= sizeof(git_oid::id), "git_oid raw storage must be able to hold the largest id");


namespace TGitHash
{
/*
 * Whether str is exactly as long as a full hex id in the active object format.
 * Not exported: CGitHash::FromHexStr is its only caller. Module linkage, not
 * internal, because that caller is an exported template instantiated by importers.
 */
template <IsTCompString Str>
bool IsFullHexLength(const Str& str) noexcept
{
	return tgit::zstr::StringView<Str>{ str }.size() == 2 * GitFormatHolder::GitHashSize();
}
}


export namespace TGitHash
{

class CGitHash
{
	git_oid m_oid{};

	/*
	 * Tag the raw storage with the active object format. git_oid_cpy() carries the tag
	 * over for oids that come from libgit2, but an id assembled here from hex or raw
	 * bytes starts at type 0, which libgit2 reads as a null id rather than as an
	 * untagged one -- lookups then fail with "null OID cannot exist". See
	 * GitActiveOidType().
	 */
	void StampOidType() noexcept
	{
		m_oid.type = GitFormatHolder::GitActiveOidType();
	}

public:
	explicit operator bool() = delete;

	CGitHash() noexcept
	{
		StampOidType();
	}
	explicit CGitHash(const git_oid* oid)
	{
		git_oid_cpy(&m_oid, oid);
	}
	explicit CGitHash(const git_oid& oid)
	{
		git_oid_cpy(&m_oid, &oid);
	}
	CGitHash& operator=(const git_oid* oid)
	{
		git_oid_cpy(&m_oid, oid);
		return *this;
	}
	CGitHash& operator=(const git_oid& oid)
	{
		git_oid_cpy(&m_oid, &oid);
		return *this;
	}

	// was TGIT_TESTS_ONLY; a module cannot vary its API by importer
	static CGitHash FromHexStr(const wchar_t* str, bool* isHash = nullptr)
	{
		return FromHexStr(std::wstring_view(str), isHash);
	}

	template <IsTCompString Str>
	static CGitHash FromHexStr(const Str& str, bool* isHash = nullptr)
	{
		if (!IsFullHexLength(str))
		{
			if (isHash)
				*isHash = false;
			return {};
		}

		CGitHash hash;
		for (int i = 0; i < GitFormatHolder::GitHashSize(); ++i)
		{
			unsigned char a = 0;
			for (int j = 2 * i; j <= 2 * i + 1; ++j)
			{
				a = a << 4;

				const auto ch = str[j];
				static_assert('_' == L'_', "This method expects that char and wchar_t literals are comparable for ASCII characters");
				if (ch >= '0' && ch <= '9')
					a |= ch - '0' & 0xF;
				else if (ch >= 'A' && ch <= 'F')
					a |= (ch - 'A' & 0xF) + 10;
				else if (ch >= 'a' && ch <= 'f')
					a |= (ch - 'a' & 0xF) + 10;
				else
				{
					if (isHash)
						*isHash = false;
					return {};
				}
			}
			hash.m_oid.id[i] = a;
		}
		hash.StampOidType();
		if (isHash)
			*isHash = true;
		return hash;
	}

	static CGitHash FromRaw(const unsigned char* raw)
	{
		CGitHash hash;
		std::ignore = git_oid_fromraw(&hash.m_oid, raw, GitFormatHolder::get());
		const std::span src{ raw, GitFormatHolder::GitHashSize() };
		std::span dst{ hash.m_oid.id };
		std::ranges::copy(src, dst.data());
		hash.StampOidType();
		return hash;
	}

	void Empty()
	{
		// clear the whole raw buffer, not just the active length, so the unused tail
		// stays zeroed if the object format changes underneath us
		std::memset(m_oid.id, 0, sizeof(m_oid.id));
		StampOidType();
	}
	[[nodiscard]] bool IsEmpty() const
	{
		static constexpr std::byte empty[GIT_OID_MAX_SIZE]{};
		return std::memcmp(m_oid.id, empty, GitFormatHolder::GitHashSize()) == 0;
	}

	[[nodiscard]] std::wstring ToString() const
	{
		std::wstring str;
		str.reserve(GitFormatHolder::GitHashSize() * 2);
		for (int i = 0; i < GitFormatHolder::GitHashSize(); ++i)
			str.append(std::format(L"{:02x}", m_oid.id[i]));
		return str;
	}

	[[nodiscard]] std::wstring ToString(const int len) const
	{
		std::wstring str { ToString() };
		str.resize(len);
		return str;
	}

	operator const git_oid*() const
	{
		return &m_oid;
	}

	[[nodiscard]] const unsigned char* ToRaw() const
	{
		return m_oid.id;
	}

	bool operator == (const CGitHash &hash) const
	{
		return std::memcmp(m_oid.id, hash.m_oid.id, GitFormatHolder::GitHashSize()) == 0;
	}

	bool operator<(const CGitHash& other) const
	{
		return std::memcmp(m_oid.id, other.m_oid.id, GitFormatHolder::GitHashSize()) < 0;
	}

	bool operator>(const CGitHash& other) const
	{
		return std::memcmp(m_oid.id, other.m_oid.id, GitFormatHolder::GitHashSize()) > 0;
	}

	bool operator!=(const CGitHash& other) const
	{
		return std::memcmp(m_oid.id, other.m_oid.id, GitFormatHolder::GitHashSize()) != 0;
	}

	bool MatchesPrefix(const CGitHash& hash, const auto& hashString, size_t prefixLen) const
	{
		if (std::memcmp(m_oid.id, hash.m_oid.id, prefixLen >> 1) != 0)
			return false;
		if (prefixLen == 2 * GitFormatHolder::GitHashSize())
			return true;
		// ToString() is a std::wstring now, so no wcsncmp; and hashString may be a
		// CString, a std string or a raw pointer. A shorter hashString mismatches,
		// as wcsncmp's NUL did.
		const std::wstring_view prefix = [&]() -> std::wstring_view {
			if constexpr (std::is_convertible_v<decltype(hashString), const wchar_t*>)
				return static_cast<const wchar_t*>(hashString);
			else
				return hashString;
		}();
		return std::wstring_view(ToString()).substr(0, prefixLen) == prefix.substr(0, prefixLen);
	}

	friend struct std::hash<CGitHash>;
};

}
export using namespace TGitHash;
export using TGitHash::CGitHash;
export using GIT_REV_LIST = std::vector<CGitHash>;


export template <>
struct std::hash<TGitHash::CGitHash>
{
	/*
	 * Converts a cryptographic hash (such as SHA-1) into a hash value. Since cryptographic
	 * hashes are designed to be uniformly distributed, this function simply copies the first bytes.
	 * The resulting value depends on platform endianness, so it should not be stored
	 * or transmitted across systems.
	 */
	std::size_t operator()(const TGitHash::CGitHash& k) const noexcept
	{
		static_assert(sizeof(size_t) <= GIT_OID_SHA1_SIZE);
		size_t hash;
		// this makes sure that all reads to the size_t value are aligned
		std::memcpy(&hash, k.m_oid.id, sizeof(hash));
		return hash;
	}
};
