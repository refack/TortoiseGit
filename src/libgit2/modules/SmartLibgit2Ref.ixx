module;
#include <git2.h>

export module wil_git2;
import std;
// Re-exported for the bridge's lifetime: the libgit2 owners (wil::unique_git_*)
// and OpenGitRepository moved into wil, and TUs that import only wil_git2 (test
// stdafx.h, TGitPathTest.cpp, TGitCache, GitWCRev) still spell them.
export import wil;
import wstr;
import invarients;

using tgit::zstr::IsStupidString;
using tgit::zstr::StringView;

// The owners for libgit2 handles (wil::unique_git_*, unique_git_buf,
// unique_git_strarray) and wil::OpenGitRepository live in module wil now.
//
// Spelling, old -> new:
//   CAutoX x;                   wil::unique_git_x x;
//   x.GetPointer()              x.put()            (frees first, as before)
//   x.Detach() / x.Free()       x.release() / x.reset()
//   x.IsValid(), (bool)x        x.is_valid(), (bool)x   (bool is explicit now)
//   passing x as git_x*         x.get()            (no implicit conversion)
//   a.Swap(b)                   a.swap(b)
//   CAutoBuf buf; f(buf)        wil::unique_git_buf buf; f(&buf)   (it IS a git_buf)
//   CAutoCommit(std::move(obj)) wil::unique_git_commit{ reinterpret_cast<git_commit*>(obj.release()) }
//   CAutoConfig(repo)           git_repository_config(cfg.put(), repo.get())
//   CAutoConfig(true), New()    git_config_new(cfg.put())
//   CAutoRepository(path)       wil::OpenGitRepository(utf8)   (latches the object format;
//                               a wide path keeps one StdGetUTF8 overload, see wil.ixx)
//
// Still to place when the bridge retires (they need CUnicodeUtils, so not wil):
// /// What CAutoBuf's operator std::string_view was.
// std::string_view View(const git_buf& buf)
// {
// 	return { buf.ptr, buf.size };
// }
//
// /// What CAutoStrArray::AppendTo was.
// void AppendTo(const git_strarray& array, std::vector<std::wstring>& list)
// {
// 	for (size_t i = 0; i < array.count; ++i)
// 		list.push_back(CUnicodeUtils::StdGetUnicode(array.strings[i]));
// }


// MIGRATION BRIDGE - the old CAuto* spellings over wil's owners, so the
// ~110 existing call sites compile while they move to the table at the top of
// this file. Retire a name here once nothing spells it.
//
// What made the old classes fail was not their API but how they exposed it:
// protected inheritance plus `using Base::operator T*;`, which MSVC rejects
// across a module boundary (C2248). Here everything is public and defined in
// the class itself, with no using-declarations of members.
export namespace SmartLibgit2
{
template <typename Owner>
struct CAutoGitRef : Owner
{
	using pointer = decltype(std::declval<const Owner&>().get());

	CAutoGitRef() = default;
	explicit CAutoGitRef(pointer&& h) noexcept : Owner(h) {}

	operator pointer() const noexcept { return this->get(); }
	pointer* GetPointer() noexcept { return this->put(); } // frees first, as it always did
	pointer Detach() noexcept { return this->release(); }
	void Free() noexcept { this->reset(); }
	[[nodiscard]] bool IsValid() const noexcept { return this->is_valid(); }
	void Swap(CAutoGitRef& other) noexcept { this->swap(other); }
};

using CAutoObject				= CAutoGitRef<wil::unique_git_object>;
using CAutoSubmodule			= CAutoGitRef<wil::unique_git_submodule>;
using CAutoBlob					= CAutoGitRef<wil::unique_git_blob>;
using CAutoTag					= CAutoGitRef<wil::unique_git_tag>;
using CAutoTreeEntry			= CAutoGitRef<wil::unique_git_tree_entry>;
using CAutoDiff					= CAutoGitRef<wil::unique_git_diff>;
using CAutoPatch				= CAutoGitRef<wil::unique_git_patch>;
using CAutoDiffStats			= CAutoGitRef<wil::unique_git_diff_stats>;
using CAutoIndex				= CAutoGitRef<wil::unique_git_index>;
using CAutoRemote				= CAutoGitRef<wil::unique_git_remote>;
using CAutoReflog				= CAutoGitRef<wil::unique_git_reflog>;
using CAutoRevwalk				= CAutoGitRef<wil::unique_git_revwalk>;
using CAutoBranchIterator		= CAutoGitRef<wil::unique_git_branch_iterator>;
using CAutoReferenceIterator	= CAutoGitRef<wil::unique_git_reference_iterator>;
using CAutoDescribeResult		= CAutoGitRef<wil::unique_git_describe_result>;
using CAutoStatusList			= CAutoGitRef<wil::unique_git_status_list>;
using CAutoNote					= CAutoGitRef<wil::unique_git_note>;
using CAutoSignature			= CAutoGitRef<wil::unique_git_signature>;
using CAutoMailmap				= CAutoGitRef<wil::unique_git_mailmap>;
using CAutoWorktree				= CAutoGitRef<wil::unique_git_worktree>;
using CAutoReference			= CAutoGitRef<wil::unique_git_reference>;

/// Opening a repository is the one thing this does beyond ownership: TGitCache
/// and the shell extension never initialize gitdll, so this is the only place
/// their object format is ever established.
struct CAutoRepository : CAutoGitRef<wil::unique_git_repository>
{
	CAutoRepository() = default;
	explicit CAutoRepository(git_repository*&& h) noexcept : CAutoGitRef(std::move(h)) {}
	/// From the wil owner - what CGit::GetGitRepository returns now.
	CAutoRepository(wil::unique_git_repository&& owner) noexcept : CAutoGitRef(owner.release()) {}
	explicit CAutoRepository(const char* gitDirUtf8) { Open(gitDirUtf8); }
	explicit CAutoRepository(const std::wstring_view gitDir) { Open(gitDir); }
	template <IsStupidString TStr>
	explicit CAutoRepository(const TStr& gitDir) { Open(StringView{ gitDir }); }

	int Open(const char* gitDirUtf8)
	{
		const int ret = git_repository_open(GetPointer(), gitDirUtf8);
		if (!ret)
			tgit::invarients::LatchObjectFormat(git_repository_oid_type(get()));
		return ret;
	}
	int Open(const std::wstring_view gitDir) { return Open(CUnicodeUtils::StdGetUTF8(gitDir).c_str()); }
	int Open(const std::string_view gitDirUtf8) { return Open(std::string(gitDirUtf8).c_str()); }
	template <IsStupidString TStr>
	int Open(const TStr& gitDir) { return Open(StringView{ gitDir }); }
};

struct CAutoCommit : CAutoGitRef<wil::unique_git_commit>
{
	CAutoCommit() = default;
	explicit CAutoCommit(git_commit*&& h) noexcept : CAutoGitRef(std::move(h)) {}
	/// A git_object known to be a commit (a git_object_lookup with GIT_OBJECT_COMMIT, say).
	explicit CAutoCommit(CAutoObject&& object) noexcept : CAutoGitRef(reinterpret_cast<git_commit*>(object.Detach())) {}
};

struct CAutoTree : CAutoGitRef<wil::unique_git_tree>
{
	/// Takes over a git_object known to be a tree.
	void ConvertFrom(CAutoObject&& object) noexcept
	{
		if (get() != reinterpret_cast<git_tree*>(object.get()))
			reset(reinterpret_cast<git_tree*>(object.Detach()));
	}
};

struct CAutoConfig : CAutoGitRef<wil::unique_git_config>
{
	CAutoConfig() = default;
	explicit CAutoConfig(const bool init) { if (init) New(); }
	explicit CAutoConfig(const CAutoRepository& repo) { git_repository_config(GetPointer(), repo); }

	void New() { git_config_new(GetPointer()); }

	/// The three readers the pre-module header had, with its semantics: -1 without
	/// a config, libgit2's error otherwise. GetBOOL takes Win32's BOOL, i.e. int.
	int GetBOOL(const std::wstring_view key, int& value) const
	{
		if (!IsValid())
			return -1;
		return git_config_get_bool(&value, get(), CUnicodeUtils::StdGetUTF8(key).c_str());
	}

	int GetBool(const std::wstring_view key, bool& value) const
	{
		int out = 0;
		if (const int ret = GetBOOL(key, out); ret < 0)
			return ret;
		value = out != 0;
		return 0;
	}

	/// \a value is emptied on failure. TOut is anything that assigns from a
	/// const wchar_t* - a CString or a std::wstring.
	/// A CString key - the MFC UI's. Constrained, so a literal binds the view one.
	template <tgit::zstr::IsStupidString TKey, typename TOut>
	int GetString(const TKey& key, TOut& value) const { return GetString(std::wstring_view(tgit::zstr::StringView{ key }), value); }

	template <typename TOut>
	int GetString(const std::wstring_view key, TOut& value) const
	{
		if (!IsValid())
			return -1;
		wil::unique_git_buf buf;
		if (const int ret = git_config_get_string_buf(&buf, get(), CUnicodeUtils::StdGetUTF8(key).c_str()); ret)
		{
			value = L"";
			return ret;
		}
		value = CUnicodeUtils::GetUnicode<std::wstring>(std::string_view(buf.ptr, buf.size)).c_str();
		return 0;
	}
};

/// A git_buf libgit2 fills in place: pass the object where a git_buf* is wanted.
struct CAutoBuf : wil::unique_git_buf
{
	operator git_buf*() noexcept { return this; }
	git_buf* operator->() noexcept { return this; }
	const git_buf* operator->() const noexcept { return this; }
	operator std::string_view() const noexcept { return { ptr, size }; }
};

/// A git_strarray libgit2 fills in place.
struct CAutoStrArray : wil::unique_git_strarray
{
	operator git_strarray*() noexcept { return this; }
	git_strarray* operator->() noexcept { return this; }
	void AppendTo(std::vector<std::wstring>& list) const
	{
		for (size_t i = 0; i < count; ++i) {
			list.push_back(CUnicodeUtils::Convert2Unicode<std::wstring>(strings[i]));
		}
	}
};
}

export using namespace SmartLibgit2;
