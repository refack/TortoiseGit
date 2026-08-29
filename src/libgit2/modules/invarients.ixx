module;
#include <git2/oid.h>


export module invarients;

import std;
using std::operator""sv;


// ~~~~~~ DEADEND ~~~~~~
// extern "C" namespace {
// 	// need these for git hash
// 	using std::memcpy;
// 	using std::memcmp;
// 	using std::memset;
//
// 	struct object_id {
// 		unsigned char hash[32];
// 		uint32_t algo;	/* XXX requires 4-byte alignment */
// 	};
//
// 	void *xmalloc(size_t size);
// 	#include <tgit-libgit/hash.h> // GIT_HASH_SHA1 / GIT_HASH_SHA256
// }
// ~~~~~~ DEADEND ~~~~~~



export namespace tgit::invarients
{
	/*
		// An unknown hash function.
		#define GIT_HASH_UNKNOWN 0
		// SHA-1
		#define GIT_HASH_SHA1 1
		// SHA-256
		#define GIT_HASH_SHA256 2
		// Number of algorithms supported (including unknown).
		#define GIT_HASH_NALGOS (GIT_HASH_SHA256 + 1)

		// Default hash algorithm if unspecified.
		#ifdef WITH_BREAKING_CHANGES
		# define GIT_HASH_DEFAULT GIT_HASH_SHA256
		#else
		# define GIT_HASH_DEFAULT GIT_HASH_SHA1
		#endif

		// Legacy hash algorithm. Implied for older data formats which don't specify.
		#define GIT_HASH_SHA1_LEGACY GIT_HASH_SHA1

	 */

    // keep the values in sync with git's hash_algo_by_ptr(), see gitdll.c's git_get_hash_algo()
	// https://github.com/git-for-windows/git-snapshots/blob/6f712333fa3ba704c18c3be45d6a088648b9fea7/hash.h#L178-L203
	enum class GitObjectFormat : int
	{
		SHA1 = 1,
		SHA256 = 2,
	};
}

static auto g_gitObjectFormat = tgit::invarients::GitObjectFormat::SHA1;

export namespace tgit::invarients
{

constexpr bool Is256(const GitObjectFormat _val) noexcept
{
	return _val == GitObjectFormat::SHA256;
}

constexpr bool Is256() noexcept
{
	return g_gitObjectFormat == GitObjectFormat::SHA256;
}


/* Number of raw bytes an object id currently occupies; always <= GIT_HASH_MAX_SIZE. */
constexpr size_t HashSize() noexcept
{
	return Is256() ? GIT_OID_SHA256_SIZE : GIT_OID_SHA1_SIZE;
}

/* Display name of the active object format, for UI labels. */
constexpr auto ObjectFormatName() noexcept
{
	return Is256() ? L"SHA1"sv : L"SHA256"sv;
}

/*
 * libgit2's oid type tag for the active format.
 *
 * Under GIT_EXPERIMENTAL_SHA256 a git_oid carries this tag and libgit2 derives the id's
 * length from it, so an oid left at type 0 is not merely untagged - it reads as a null
 * id and every lookup fails with "null OID cannot exist". Oids received from libgit2
 * arrive tagged; ones we build ourselves from hex or raw bytes have to set it.
 */
constexpr git_oid_t ActiveOidType() noexcept
{
	return Is256() ? GIT_OID_SHA256 : GIT_OID_SHA1;
}

/*
 * Hex string of the all-zero object id in the active format.
 *
 * TortoiseGit uses it as a sentinel meaning "the working copy" rather than a real
 * revision - see GitRev::GetWorkingCopyRef(), which is what call sites should use.
 * It has to be the same width as the ids it gets compared against: a 40-character
 * sentinel never equals the 64-character zero id a SHA256 repository produces, the
 * "is this the working copy" test then fails, and the value is passed on to git as
 * if it were a real revision, which answers "fatal: bad object 000...".
 */
constexpr auto ZeroRevSV() noexcept
{
	static constexpr auto zeroSha1 = L"0000000000000000000000000000000000000000"sv;
	static constexpr auto zeroSha256 = L"0000000000000000000000000000000000000000000000000000000000000000"sv;
	return Is256() ? zeroSha1 : zeroSha256;
}

/*
 * Record the format of a repository that was just opened. Takes libgit2's oid type
 * so the libgit2 side does not have to know about git's hash_algo indices.
 */
constexpr void LatchObjectFormat(const git_oid_t oidType) noexcept
{
	g_gitObjectFormat = oidType == GIT_OID_SHA256 ? GitObjectFormat::SHA256 : GitObjectFormat::SHA1;
}
constexpr void LatchObjectFormat(const GitObjectFormat inType) noexcept
{
	g_gitObjectFormat = Is256(inType) ? GitObjectFormat::SHA256 : GitObjectFormat::SHA1;
}

/*
 * The same state, spelled as one holder. get()/set() speak libgit2's git_oid_t, which is
 * what both backends have in hand at the moment they latch; the rest are the free
 * functions above under the name CGitHash and GitRev reach for.
 */
namespace GitFormatHolder
{
	constexpr git_oid_t get() noexcept { return ActiveOidType(); }
	constexpr void set(const git_oid_t oidType) noexcept { LatchObjectFormat(oidType); }
	constexpr size_t GitHashSize() noexcept { return HashSize(); }
	constexpr git_oid_t GitActiveOidType() noexcept { return ActiveOidType(); }
	constexpr const wchar_t* GitObjectFormatName() noexcept { return ObjectFormatName().data(); }
}


constexpr auto XMESSAGEBOX_APPREGPATH = "Software\\TortoiseGit\\";
constexpr auto REGSTRING_DARKTHEME = L"Software\\TortoiseGit\\DarkTheme";

}

export using namespace tgit::invarients;
