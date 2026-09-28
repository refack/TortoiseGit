module;
#include <git2/oid.h>
#define GIT_HASH_MAX_HEXSIZE (2 * GIT_OID_MAX_SIZE)


export module TGitHash;
import std;
import invarients;
import wstr;
using namespace tgit::invarients;

/* also see gitdll.c */
static_assert(GIT_OID_MAX_SIZE <= sizeof(git_oid::id), "git_oid raw storage must be able to hold the largest id");


export namespace GitHash
{
class CGitHash;

template<> struct std::hash<CGitHash>;

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
#ifdef GIT_EXPERIMENTAL_SHA256
		m_oid.type = static_cast<unsigned char>(GitFormatHolder::GitActiveOidType());
#endif
	}

public:
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

	template <typename Str_Type>
		requires tgit::wstr::IsRealString<Str_Type> || tgit::wstr::IsStupidString<Str_Type>
	static CGitHash FromHexStr(const Str_Type& str, bool* isHash = nullptr)
	{
		static constexpr bool IsStupidString = tgit::wstr::IsStupidString<Str_Type>;
		if constexpr (IsStupidString)
		{
			if (str.GetLength() != 2 * GitFormatHolder::GitHashSize())
			{
				if (isHash)
					*isHash = false;
				return CGitHash();
			}
		}
		else
		{
			if (str.size() != 2 * GitFormatHolder::GitHashSize())
			{
				if (isHash)
					*isHash = false;
				return CGitHash();
			}
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
					return CGitHash();
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
	bool IsEmpty() const
	{
		static constexpr std::byte empty[GIT_OID_MAX_SIZE]{};
		return std::memcmp(m_oid.id, empty, GitFormatHolder::GitHashSize()) == 0;
	}

	std::wstring ToString() const
	{
		std::wstring str;
		str.reserve(GitFormatHolder::GitHashSize() * 2);
		for (int i = 0; i < GitFormatHolder::GitHashSize(); ++i)
			str.append(std::format(L"{:02x}", m_oid.id[i]));
		return str;
	}

	std::wstring ToString(int len) const
	{
		std::wstring str { ToString() };
		str.resize(len);
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
		if (std::memcmp(m_oid.id, hash.m_oid.id, prefixLen >> 1))
			return false;
		return prefixLen == 2 * GitFormatHolder::GitHashSize() || wcsncmp(ToString(), hashString, prefixLen) == 0;
	}

	friend struct std::hash<CGitHash>;
};


using GIT_REV_LIST = std::vector<GitHash::CGitHash>;


}

export template <>
struct std::hash<GitHash::CGitHash>
{
	/*
	 * Converts a cryptographic hash (such as SHA-1) into a hash value. Since cryptographic
	 * hashes are designed to be uniformly distributed, this function simply copies the first bytes.
	 * The resulting value depends on platform endianness, so it should not be stored
	 * or transmitted across systems.
	 */
	std::size_t operator()(const GitHash::CGitHash& k) const noexcept
	{
		static_assert(sizeof(size_t) <= GIT_OID_SHA1_SIZE);
		size_t hash;
		// this makes sure that all reads to the size_t value are aligned
		std::memcpy(&hash, k.m_oid.id, sizeof(hash));
		return hash;
	}
};


export using namespace GitHash;