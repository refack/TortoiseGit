export module tgittypes;
import std;
import wstr;
import gsl;
import GitHash;

export namespace tgit::types
{

enum CacheType
{
	none,
	exe,
	dll,
	dllFull, // same as dll except it uses commandline git tool with all status modes supported
};

enum: std::uint8_t
{
	TGIT_GIT_SUCCESS=0,
	TGIT_GIT_ERROR_OPEN_PIP,
	TGIT_GIT_ERROR_CREATE_PROCESS,
	TGIT_GIT_ERROR_GET_EXIT_CODE
};

struct CGitByteArray : std::vector<char>
{
	[[nodiscard]] size_t find(const char data, const size_t start = 0) const
	{
		const size_t end = size();
		for (size_t i = start; i < end; ++i)
			if ((*this)[i] == data)
				return i;
		return npos;
	}
	[[nodiscard]] size_t RevertFind(const char data, size_t start = npos) const
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
	[[nodiscard]] size_t findNextString(const size_t start = 0) const
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

	// was gtest-only; a module cannot vary its API by importer
	void append(const char* data, size_t dataSize)
	{
		append(std::string_view(data, dataSize));
	}
	void append(const std::string_view v) {
		insert(this->end(), v.begin(), v.end());
	}

	void append(const std::span<unsigned char> v)
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
		const auto max_items = std::ranges::find(*this, '\0') - this->begin();
		const auto char_view = std::string_view{data(), gsl::narrow<size_t>(max_items)};
		return CUnicodeUtils::GetUnicode<std::wstring>(char_view);
	}

	static constexpr size_t npos = static_cast<size_t>(-1); // bad/missing length/position
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

// std::wstring rather than CString, for the reason WideString.h opens with:
// CString is a different type in an MFC project than in an ATL one, so an alias
// naming it cannot appear in a header both flavors compile. Values, not views -
// these containers own their strings and outlive whatever produced them.
using STRING_VECTOR = std::vector<std::wstring>;
// A list of *commands*, each one an argv vector. Named because the distinction
// was previously invisible: CProgressDlg's list of whole command lines was also
// spelled STRING_VECTOR, one alias away from a single command's arguments.
using ARGV_VECTOR = std::vector<STRING_VECTOR>;
using MAP_HASH_NAME = std::unordered_map<CGitHash, STRING_VECTOR>;
using MAP_STRING_STRING = std::map<std::wstring, std::wstring>;
using REF_VECTOR = std::vector<TGitRef>;
using BYTE_VECTOR = CGitByteArray;



template <typename D>
struct scope_exit_t
{
	using func_type = D;
	explicit scope_exit_t(D&& f) : func(f) {}
	scope_exit_t(scope_exit_t&& s) noexcept : func(s.func) {}

	~scope_exit_t() { func(); }

	// Prohibit construction from lvalues.
	scope_exit_t(D&) = delete;

	// Prohibit copying.
	scope_exit_t(const scope_exit_t&) = delete;
	scope_exit_t& operator=(const scope_exit_t&) = delete;

	// Prohibit new/delete.
	void* operator new(size_t) = delete;
	void* operator new[](size_t) = delete;
	void operator delete(void*) = delete;
	void operator delete[](void*) = delete;

private:
	const func_type func;
};


struct scope_exit_helper
{
	template <typename D>
	scope_exit_t<D> operator<< (D&& f) {
		return scope_exit_t<D>(std::forward<D>(f));
	}
};


enum git_wc_status_kind
{
	git_wc_status_none,
	git_wc_status_unversioned,
	git_wc_status_ignored,
	git_wc_status_normal,
	git_wc_status_deleted,
	git_wc_status_modified,
	git_wc_status_added,
	git_wc_status_conflicted,
	git_wc_status_unknown, // should be last, see TGitCache/CacheInterface.h
};


struct git_wc_status2_t
{
	git_wc_status_kind status = git_wc_status_none;

	bool assumeValid = false;
	bool skipWorktree = false;
};


enum class TGitContextMenuEntries : unsigned __int64
{
	None			= 0x0000000000000000,
	Sync			= 0x0000000000000002,
	Commit			= 0x0000000000000004,
	Add				= 0x0000000000000008,
	Revert			= 0x0000000000000010,
	Cleanup			= 0x0000000000000020,
	Resolve			= 0x0000000000000040,
	Switch			= 0x0000000000000080,
	Sendmail		= 0x0000000000000100,
	Export			= 0x0000000000000200,
	CreateRepo		= 0x0000000000000400,
	Branch			= 0x0000000000000800,
	Merge			= 0x0000000000001000,
	Remove			= 0x0000000000002000,
	Rename			= 0x0000000000004000,
	SubmoduleUpdate	= 0x0000000000008000,
	Diff			= 0x0000000000010000,
	Log				= 0x0000000000020000,
	Conflicteditor	= 0x0000000000040000,
	RefBrowser		= 0x0000000000080000,
	ShowChanged		= 0x0000000000100000,
	Ignore			= 0x0000000000200000,
	RefLog			= 0x0000000000400000,
	Blame			= 0x0000000000800000,
	RepoBrowser		= 0x0000000001000000,
	ApplyPatch		= 0x0000000002000000, // retired with TortoiseGitMerge; the bit stays reserved so existing user menu masks keep their meaning
	RemoveKeep		= 0x0000000004000000,
	SVNRebase		= 0x0000000008000000,
	SVNDCommit		= 0x0000000010000000,
	SVNIgnore		= 0x0000000040000000,
	LogSubmodule	= 0x0000000100000000,
	PrevDiff		= 0x0000000200000000,
	ClipPaste		= 0x0000000400000000,
	Pull			= 0x0000000800000000,
	Push			= 0x0000001000000000,
	Clone			= 0x0000002000000000,
	Tag				= 0x0000004000000000,
	FormatPatch		= 0x0000008000000000,
	ImportPatch		= 0x0000010000000000,
	DiffLater		= 0x0000020000000000,
	Fetch			= 0x0000040000000000,
	Rebase			= 0x0000080000000000,
	StashSave		= 0x0000100000000000,
	StashApply		= 0x0000200000000000,
	StashList		= 0x0000400000000000,
	SubmoduleAdd	= 0x0000800000000000,
	SubmoduleSync	= 0x0001000000000000,
	StashPop		= 0x0002000000000000,
	DiffTwo			= 0x0004000000000000,
	Bisect			= 0x0008000000000000,
	Inaccessible	= 0x0010000000000000,
	SVNFetch		= 0x0080000000000000,
	RevisionGraph	= 0x0100000000000000,
	Daemon			= 0x0200000000000000,
	Worktree		= 0x0400000000000000,
	LFS				= 0x1000000000000000,
	Settings		= 0x2000000000000000,
	Help			= 0x4000000000000000,
	About			= 0x8000000000000000,
};

constexpr TGitContextMenuEntries operator|(TGitContextMenuEntries a, TGitContextMenuEntries b) noexcept
{
	return static_cast<TGitContextMenuEntries>(static_cast<std::underlying_type_t<TGitContextMenuEntries>>(a) | static_cast<std::underlying_type_t<TGitContextMenuEntries>>(b));
}
constexpr TGitContextMenuEntries operator&(TGitContextMenuEntries a, TGitContextMenuEntries b) noexcept
{
	return static_cast<TGitContextMenuEntries>(static_cast<std::underlying_type_t<TGitContextMenuEntries>>(a) & static_cast<std::underlying_type_t<TGitContextMenuEntries>>(b));
}
constexpr TGitContextMenuEntries& operator|=(TGitContextMenuEntries& self, const TGitContextMenuEntries a) noexcept
{
	self = self | a;
	return self;
}

template <typename E>
constexpr E from_underlying(std::underlying_type_t<E> value) noexcept
{
	static_assert(std::is_enum_v<E>);
	return static_cast<E>(value);
}

constexpr TGitContextMenuEntries to_TGitContextMenuEntries(const std::uint32_t high, const std::uint32_t low) noexcept
{
	static_assert(sizeof(std::underlying_type_t<TGitContextMenuEntries>) == 2 * sizeof(std::uint32_t));
	const auto tmp = (static_cast<std::uint64_t>(high) << 32) | low;
	return from_underlying<TGitContextMenuEntries>(tmp);
}

/**
 * \ingroup TortoiseShell
 * Since we need an own COM-object for every different
 * Icon-Overlay implemented this enum defines which class
 * is used.
 */
enum FileState
{
	FileStateUncontrolled,
	FileStateVersioned,
	FileStateModified,
	FileStateConflict,
	FileStateDeleted,
	FileStateReadOnly,
	FileStateLockedOverlay,
	FileStateAddedOverlay,
	FileStateIgnoredOverlay,
	FileStateUnversionedOverlay,
	FileStateDropHandler,
	FileStateInvalid
};

}

export using namespace tgit::types;
export using tgit::types::scope_exit_helper;