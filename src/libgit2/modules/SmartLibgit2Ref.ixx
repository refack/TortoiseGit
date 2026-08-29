module;
#include <git2.h>

export module SmartLibgit2;
import std;
import wstr;
import invarients;

export namespace SmartLibgit2 {
/**
 * \ingroup Utils
 * Helper classes for libgit2 references.
 */
template <typename HandleType, void FreeFunction(HandleType*)>
class CSmartBuffer
{
public:
	CSmartBuffer() = default;

	operator HandleType*()
	{
		return &m_Ref;
	}

	HandleType* operator->()
	{
		return &m_Ref;
	}

	~CSmartBuffer()
	{
		FreeFunction(&m_Ref);
	}

	CSmartBuffer(const CSmartBuffer&) = delete;
	CSmartBuffer& operator=(const CSmartBuffer&) = delete;

	inline operator std::string_view() const
		requires std::is_same_v<HandleType, git_buf>
	{
		return std::string_view(m_Ref.ptr, m_Ref.size);
	}

	// Out of the CSTRING_AVAILABLE gate below, along with the string_view
	// conversion above: with a std::vector<std::wstring> to fill, neither of them
	// names CString any more, so neither needs a flavor to be chosen for it.
	inline void AppendTo(std::vector<std::wstring>& list) const
		requires std::is_same_v<HandleType, git_strarray>
	{
		for (size_t i = 0; i < m_Ref.count; ++i)
			list.push_back(CUnicodeUtils::StdGetUnicode(m_Ref.strings[i]));
	}

	template<tgit::wstr::IsStupidString StrT>
	inline StrT ToString() const
		requires std::is_same_v<HandleType, git_buf>
	{
		return CUnicodeUtils::GetUnicode<StrT>(*this);
	}

private:
	HandleType m_Ref{};
};

using CAutoBuf		= CSmartBuffer<git_buf,			git_buf_dispose>;
using CAutoStrArray	= CSmartBuffer<git_strarray,	git_strarray_dispose>;

template <typename ReferenceType, void FreeFunction(ReferenceType*)>
class CSmartLibgit2Ref
{
public:
	CSmartLibgit2Ref() = default;
	~CSmartLibgit2Ref()
	{
		Free();
	}

	template <typename T, typename std::enable_if<std::is_base_of<CSmartLibgit2Ref<ReferenceType, FreeFunction>, T>::value, int>::type = 0>
	void Swap(T& tmp) noexcept
	{
		ReferenceType* p = m_Ref;
		m_Ref = tmp.m_Ref;
		tmp.m_Ref = p;
	}

	void Free()
	{
		FreeFunction(m_Ref);
		m_Ref = nullptr;
	}

	ReferenceType* Detach()
	{
		ReferenceType* p = m_Ref;
		m_Ref = nullptr;

		return p;
	}

	operator ReferenceType*() const
	{
		return m_Ref;
	}

	/**
	 * Free the wrapped object and return a pointer to the wrapped object ReferenceType*
	 */
	ReferenceType** GetPointer()
	{
		Free();
		return &m_Ref;
	}

	operator bool() const
	{
		return IsValid();
	}

	bool IsValid() const
	{
		return m_Ref != nullptr;
	}

	CSmartLibgit2Ref(const CSmartLibgit2Ref&) = delete;
	CSmartLibgit2Ref& operator=(const CSmartLibgit2Ref&) = delete;

protected:
	ReferenceType* m_Ref = nullptr;
};

using CAutoObject				= CSmartLibgit2Ref<git_object,				git_object_free>;
using CAutoSubmodule			= CSmartLibgit2Ref<git_submodule,			git_submodule_free>;
using CAutoBlob					= CSmartLibgit2Ref<git_blob,				git_blob_free>;
using CAutoTag					= CSmartLibgit2Ref<git_tag,					git_tag_free>;
using CAutoTreeEntry			= CSmartLibgit2Ref<git_tree_entry,			git_tree_entry_free>;
using CAutoDiff					= CSmartLibgit2Ref<git_diff,				git_diff_free>;
using CAutoPatch				= CSmartLibgit2Ref<git_patch,				git_patch_free>;
using CAutoDiffStats			= CSmartLibgit2Ref<git_diff_stats,			git_diff_stats_free>;
using CAutoIndex				= CSmartLibgit2Ref<git_index,				git_index_free>;
using CAutoRemote				= CSmartLibgit2Ref<git_remote,				git_remote_free>;
using CAutoReflog				= CSmartLibgit2Ref<git_reflog,				git_reflog_free>;
using CAutoRevwalk				= CSmartLibgit2Ref<git_revwalk,				git_revwalk_free>;
using CAutoBranchIterator		= CSmartLibgit2Ref<git_branch_iterator,		git_branch_iterator_free>;
using CAutoReferenceIterator	= CSmartLibgit2Ref<git_reference_iterator,	git_reference_iterator_free>;
using CAutoDescribeResult		= CSmartLibgit2Ref<git_describe_result,		git_describe_result_free>;
using CAutoStatusList			= CSmartLibgit2Ref<git_status_list,			git_status_list_free>;
using CAutoNote					= CSmartLibgit2Ref<git_note,				git_note_free>;
using CAutoSignature			= CSmartLibgit2Ref<git_signature,			git_signature_free>;
using CAutoMailmap				= CSmartLibgit2Ref<git_mailmap,				git_mailmap_free>;
using CAutoWorktree				= CSmartLibgit2Ref<git_worktree,			git_worktree_free>;

class CAutoRepository : protected CSmartLibgit2Ref<git_repository, git_repository_free>
{
public:
	CAutoRepository() = default;

	explicit CAutoRepository(git_repository*&& h) noexcept
	{
		m_Ref = h;
	}

	explicit CAutoRepository(CAutoRepository&& that) noexcept
	{
		m_Ref = that.Detach();
	}

	explicit CAutoRepository(const auto& gitDir)
	{
		Open(gitDir);
	}


	int Open(const auto gitDir)
	{
		return Open(CUnicodeUtils::GetUTF8<decltype(gitDir)>(gitDir));
	}

	int Open(const char* gitDirA)
	{
		// TODO: figure this thing out
		const int ret = git_repository_open(GetPointer(), gitDirA);
		if (!ret)
		{
			// TGitCache and the shell extension never initialize gitdll, so this is the
			// only place their object format is ever established; cf. GitObjectFormat.h.
			LatchObjectFormat(git_repository_oid_type(m_Ref));
		}
		return ret;
	}

	CAutoRepository(const CAutoRepository&) = delete;
	CAutoRepository& operator=(const CAutoRepository&) = delete;

	using CSmartLibgit2Ref::GetPointer;
	using CSmartLibgit2Ref::Detach;
	using CSmartLibgit2Ref::Free;
	using CSmartLibgit2Ref::IsValid;
	using CSmartLibgit2Ref::operator git_repository*;
	using CSmartLibgit2Ref::operator bool;
};

class CAutoCommit : protected CSmartLibgit2Ref<git_commit, git_commit_free>
{
public:
	CAutoCommit() = default;

	explicit CAutoCommit(git_commit*&& h) noexcept
	{
		m_Ref = h;
	}

	explicit CAutoCommit(CAutoObject&& h) noexcept
	{
		m_Ref = reinterpret_cast<git_commit*>(h.Detach());
	}

	CAutoCommit(const CAutoCommit&) = delete;
	CAutoCommit& operator=(const CAutoCommit&) = delete;

	using CSmartLibgit2Ref::GetPointer;
	using CSmartLibgit2Ref::Detach;
	using CSmartLibgit2Ref::Free;
	using CSmartLibgit2Ref::IsValid;
	using CSmartLibgit2Ref::operator git_commit*;
	using CSmartLibgit2Ref::operator bool;
};

class CAutoReference : protected CSmartLibgit2Ref<git_reference, git_reference_free>
{
public:
	CAutoReference() = default;

	explicit CAutoReference(git_reference*&& h) noexcept
	{
		m_Ref = h;
	}

	CAutoReference(const CAutoReference&) = delete;
	CAutoReference& operator=(const CAutoReference&) = delete;

	using CSmartLibgit2Ref::GetPointer;
	using CSmartLibgit2Ref::Swap;
	using CSmartLibgit2Ref::Detach;
	using CSmartLibgit2Ref::Free;
	using CSmartLibgit2Ref::IsValid;
	using CSmartLibgit2Ref::operator git_reference*;
	using CSmartLibgit2Ref::operator bool;
	friend CSmartLibgit2Ref<git_reference, git_reference_free>;
};

class CAutoTree : protected CSmartLibgit2Ref<git_tree, git_tree_free>
{
public:
	CAutoTree() = default;

	void ConvertFrom(CAutoObject&& h)
	{
		if (m_Ref != reinterpret_cast<git_tree*>(static_cast<git_object*>(h)))
		{
			Free();
			m_Ref = reinterpret_cast<git_tree*>(h.Detach());
		}
	}

	CAutoTree(const CAutoTree&) = delete;
	CAutoTree& operator=(const CAutoTree&) = delete;

	using CSmartLibgit2Ref::GetPointer;
	using CSmartLibgit2Ref::Detach;
	using CSmartLibgit2Ref::Free;
	using CSmartLibgit2Ref::IsValid;
	using CSmartLibgit2Ref::operator git_tree*;
	using CSmartLibgit2Ref::operator bool;
};

class CAutoConfig : protected CSmartLibgit2Ref<git_config, git_config_free>
{
public:
	CAutoConfig() = default;

	explicit CAutoConfig(const CAutoRepository& repo)
	{
		git_repository_config(&m_Ref, repo);
	}

	explicit CAutoConfig(const bool init)
	{
		if (init)
			New();
	}

	void New()
	{
		Free();
		git_config_new(&m_Ref);
	}

	int GetBOOL(const auto key, bool &b) const
	{
		if (!IsValid())
			return -1;

		return git_config_get_bool(&b, m_Ref, CUnicodeUtils::GetUTF8<decltype(key)>(key));
	}

	CAutoConfig(const CAutoConfig&) = delete;
	CAutoConfig& operator=(const CAutoConfig&) = delete;

	using CSmartLibgit2Ref::GetPointer;
	using CSmartLibgit2Ref::Detach;
	using CSmartLibgit2Ref::Free;
	using CSmartLibgit2Ref::IsValid;
	using CSmartLibgit2Ref::operator git_config*;
	using CSmartLibgit2Ref::operator bool;
};

}

export using namespace SmartLibgit2;
