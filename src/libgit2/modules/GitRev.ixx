module;
#include <tgit_cstring.h>	// first: decides which CString this compile speaks
#include <gitdll.h>
#include <git2.h>
#include <atltime.h>

export module GitRev;
import std;
import gsl;

import str_utils;
import git_h;
import GitMailmap;
import wil_git2;
import GitHash;
import invarients;
import tgittypes;

// a using-declaration, not an alias: MSVC mangles CGitHash reached through a module-local
// alias as CGitHash[!GitHash] ($$_B) rather than CGitHash[GitHash] ($$_A), so every call
// into git_h taking a CGitHash came out unresolved at link time
using TGitHash::CGitHash;
using namespace TGitH;
using namespace SmartLibgit2;
using tgit::zstr::StringView;


#define LOG_REV_AUTHOR_NAME L'0'
#define LOG_REV_AUTHOR_EMAIL L'1'
#define LOG_REV_AUTHOR_DATE L'2'
#define LOG_REV_COMMIT_NAME L'3'
#define LOG_REV_COMMIT_EMAIL L'4'
#define LOG_REV_COMMIT_DATE L'5'
#define LOG_REV_COMMIT_SUBJECT L'6'
#define LOG_REV_COMMIT_BODY L'7'
#define LOG_REV_COMMIT_HASH L'8'
#define LOG_REV_COMMIT_PARENT L'9'
#define LOG_REV_COMMIT_FILE L'A'
#define LOG_REV_ITEM_BEGIN L'B'
#define LOG_REV_ITEM_END L'C'

export struct GitRev
{
	using StringType = CString;
	using TimeType = ATL::CTime;

protected:
	StringType m_AuthorName;
	StringType m_AuthorEmail;
	TimeType m_AuthorDate;
	StringType m_CommitterName;
	StringType m_CommitterEmail;
	TimeType m_CommitterDate;
	StringType m_Subject;
	StringType m_Body;
	git_object_t m_objectType = GIT_OBJECT_INVALID;

	StringType m_sErr;

public:
	// public for the unit tests, which build revisions by hand
	CGitHash m_CommitHash;
	GIT_REV_LIST m_ParentHash;

	GitRev() = default;
	virtual ~GitRev() = default;

	[[nodiscard]] StringType GetAuthorName() const { return m_AuthorName; }
	[[nodiscard]] StringType GetAuthorEmail() const { return m_AuthorEmail; }
	[[nodiscard]] TimeType GetAuthorDate() const { return m_AuthorDate; }
	[[nodiscard]] StringType GetCommitterName() const { return m_CommitterName; }
	[[nodiscard]] StringType GetCommitterEmail() const { return m_CommitterEmail; }
	[[nodiscard]] TimeType GetCommitterDate() const { return m_CommitterDate; }
	[[nodiscard]] StringType GetSubject() const { return m_Subject; }
	[[nodiscard]] StringType GetBody() const { return m_Body; }
	[[nodiscard]] int ParentsCount() const { return static_cast<int>(m_ParentHash.size()); }
	[[nodiscard]] bool IsCommit() const { return m_objectType == GIT_OBJECT_COMMIT; }
	[[nodiscard]] StringType GetLastErr() const { return m_sErr; }

	virtual void Clear();

	/*
	 * The pseudo-revision standing for "the working copy" rather than a commit.
	 * Not constexpr: its width follows the repository's object format, so it is only
	 * known once that has been latched (see GitObjectFormat.h).
	 */
	[[nodiscard]] static LPCWSTR GetWorkingCopyRef() noexcept { return tgit::invarients::ZeroRevSV().data(); }    // a literal: NUL-terminated

	/// refname is the working-copy sentinel, or an abbreviation of it at least 8 characters long
	[[nodiscard]] static bool IsWorkingCopyRef(const CString& refname) noexcept
	{
		return refname.GetLength() >= 8 && tgit::invarients::ZeroRevSV().starts_with(StringView{ refname });
	}


protected:
	int ParserFromCommit(const GIT_COMMIT* commit);
	int ParserParentFromCommit(const GIT_COMMIT* commit);

	int ParserFromCommit(const git_commit* commit);
	int ParserParentFromCommit(const git_commit* commit);
	int GetCommitFromHash(git_repository* repo, const CGitHash& hash);
	int GetCommit(git_repository* repo, const StringType& refname);
	/// a std::wstring ref (CGitHash::ToString()). Constrained, so a literal still
	/// binds the StringType overloads and stays unambiguous.
	template <typename T> requires std::same_as<std::remove_cvref_t<T>, std::wstring>
	int GetCommit(const T& refname) { return GetCommit(StringType(refname.c_str(), static_cast<int>(refname.size()))); }

public:
	int GetParentFromHash(const CGitHash& hash);
	int GetCommitFromHash(const CGitHash& hash)
	{
		[[maybe_unused]] CAutoLocker lock(g_Git.m_critGitDllSec);
		g_Git.CheckAndInitDll();
		return GetCommitFromHash_withoutLock(hash);
	}
	int GetCommit(const StringType& refname, bool allowTree = false);

	void ApplyMailmap(const GitMailmap::CGitMailmap& mailmap = CGitMailmap());

#if DEBUG
	void DbgPrint() const;
#endif

private:
	int GetCommitFromHash_withoutLock(const CGitHash& hash);
};

export class GitRevRefBrowser : public GitRev
{
public:
	using MAP_REF_GITREVREFBROWSER = std::map<StringType, GitRevRefBrowser>;

	GitRevRefBrowser() : GitRev()
	{}

	StringType m_Description;
	StringType m_UpstreamRef;
	using GitRev::m_objectType;


	void Clear() override
	{
		GitRev::Clear();
		m_Description.Empty();
		m_UpstreamRef.Empty();
	}

	int GetGitRevRefMap(MAP_REF_GITREVREFBROWSER& map, int mergefilter, StringType& err, std::function<bool(const StringType& refName)> filterCallback)
	{
		err.Empty();
		tgit::types::MAP_STRING_STRING descriptions;
		TGitH::g_Git.GetBranchDescriptions(descriptions);

		CGitMailmap mailmap;
		if (g_Git.UsingLibGit2(TGitH::GIT_CMD_FOREACHREF) && mergefilter <= 0) // libgit2 implementation is significantly slower because of "git_graph_descendant_of", hence, only use it when merge status is not a filter
		{
			CAutoRepository repo = g_Git.GetGitRepository();
			if (!repo)
			{
				err = TGitH::GetLibGit2LastErr();
				return -1;
			}

			CAutoReference head;
			if (mergefilter > 0)
			{
				if (const auto ret = git_repository_head(head.GetPointer(), repo); ret == GIT_EUNBORNBRANCH)
					mergefilter = 0;
				else if (ret < 0)
				{
					err = TGitH::GetLibGit2LastErr();
					return -1;
				}
			}

			CAutoReferenceIterator iter;
			if (git_reference_iterator_new(iter.GetPointer(), repo) < 0)
			{
				err = TGitH::GetLibGit2LastErr();
				return -1;
			}

			CAutoReference ref;
			int error;
			while (!(error = git_reference_next(ref.GetPointer(), iter)))
			{
				StringType refName = CUnicodeUtils::Convert2Unicode<StringType>(git_reference_name(ref));
				if (filterCallback && !filterCallback(refName))
					continue;

				if (git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC)
				{
					CAutoReference target;
					if (git_reference_resolve(target.GetPointer(), ref))
					{
						err = TGitH::GetLibGit2LastErr();
						return -1;
					}
					ref.Swap(target);
				}

				CAutoTag tag;
				if (git_reference_is_tag(ref))
					git_tag_lookup(tag.GetPointer(), repo, git_reference_target(ref));

				if (mergefilter > 0)
				{
					const git_oid* oid = git_reference_target(ref);
					if (tag)
						oid = git_tag_target_id(tag);

					const int ret = git_graph_descendant_of(repo, git_reference_target(head), oid);
					if (ret < 0)
					{
						err = TGitH::GetLibGit2LastErr();
						return -1;
					}
					if (mergefilter == 1 && ret == 0 && git_oid_cmp(oid, git_reference_target(head)) != 0)
						continue;
					if (mergefilter == 2 && (ret == 1 || git_oid_cmp(oid, git_reference_target(head)) == 0))
						continue;
				}

				GitRevRefBrowser entry;
				entry.m_CommitHash = CGitHash(git_reference_target(ref)); // the git_oid* constructor is explicit
				if (git_reference_is_tag(ref) && tag)
				{
					entry.m_objectType = GIT_OBJECT_TAG;
					const char* msg = git_tag_message(tag);
					if (const char* body = strchr(msg, '\n'); !body)
						entry.m_Subject = CUnicodeUtils::Convert2Unicode<StringType>(msg);
					else
						entry.m_Subject = CUnicodeUtils::GetUnicode<StringType>(std::string_view(msg, body - msg));
					auto tagger = git_tag_tagger(tag);
					if (tagger)
					{
						entry.m_CommitterName = CUnicodeUtils::Convert2Unicode<StringType>(tagger->name);
						entry.m_CommitterEmail = CUnicodeUtils::Convert2Unicode<StringType>(tagger->email);
						entry.m_CommitterDate = tagger->when.time;
						if (mailmap)
							entry.m_CommitterEmail = mailmap.TranslateAuthor(entry.m_CommitterEmail, entry.m_AuthorEmail);
						entry.m_AuthorName = entry.m_CommitterName;
						entry.m_AuthorEmail = entry.m_CommitterEmail;
						entry.m_AuthorDate = entry.m_CommitterDate;
					}

					map.emplace(refName, entry);

					continue;
				}

				CAutoObject object;
				if (git_object_lookup(object.GetPointer(), repo, git_reference_target(ref), GIT_OBJECT_ANY) < 0)
				{
					err = TGitH::GetLibGit2LastErr();
					return -1;
				}

				entry.m_objectType = git_object_type(object);

				if (git_object_type(object) != GIT_OBJECT_COMMIT)
				{
					map.emplace(refName, entry);
					continue;
				}
				CAutoCommit commit{ std::move(object) };
				auto author = git_commit_author(commit);
				auto committer = git_commit_committer(commit);
				entry.m_Subject = CUnicodeUtils::Convert2Unicode<StringType>(git_commit_summary(commit));
				entry.m_AuthorName = CUnicodeUtils::Convert2Unicode<StringType>(author->name);
				entry.m_AuthorEmail = CUnicodeUtils::Convert2Unicode<StringType>(author->email);
				entry.m_AuthorDate = author->when.time;
				entry.m_CommitterName = CUnicodeUtils::Convert2Unicode<StringType>(committer->name);
				entry.m_CommitterEmail = CUnicodeUtils::Convert2Unicode<StringType>(committer->email);
				entry.m_CommitterDate = committer->when.time;
				if (mailmap)
				{
					entry.m_AuthorName = mailmap.TranslateAuthor(entry.m_AuthorName, entry.m_AuthorEmail);
					entry.m_CommitterName = mailmap.TranslateAuthor(entry.m_CommitterName, entry.m_CommitterEmail);
				}

				if (git_reference_is_branch(ref))
				{
					entry.m_Description = descriptions[std::wstring(refName.Mid(static_cast<int>(wcslen(L"refs/heads/"))))].c_str();

					CAutoBuf buf;
					if (const auto ret = git_branch_upstream_name(buf, repo, git_reference_name(ref)); ret == 0)
						entry.m_UpstreamRef = CUnicodeUtils::GetUnicode<StringType>(std::string{buf.ptr, buf.ptr + buf.size});
					else if (ret != GIT_ENOTFOUND)
					{
						err = TGitH::GetLibGit2LastErr();
						return -1;
					}
				}

				map.emplace(refName, entry);
			}
			if (error != GIT_ITEROVER)
			{
				err = TGitH::GetLibGit2LastErr();
				return -1;
			}

			return 0;
		}

		tgit::types::STRING_VECTOR cmd{ L"git.exe", L"for-each-ref" };
		switch (mergefilter)
		{
		case 1:
			cmd.insert(cmd.cend(), { L"--merged", L"HEAD" });
			break;
		case 2:
			cmd.insert(cmd.cend(), { L"--no-merged", L"HEAD" });
			break;
		}

		// One element. The %% doubling was Format's, and the surrounding quotes were
		// the command line's; neither is needed now.
		cmd.emplace_back(L"--format=%(refname)%04 %(objectname)%04 %(objecttype)%04 %(upstream)%04 %(subject)%04 %(authorname)%04 %(authoremail)%04 %(authordate:raw)%04 %(committername)%04 %(committeremail)%04 %(committerdate:raw)%04%(creator)%04 %(creatordate:raw)%03");

		StringType allRefs;
		if (g_Git.Run(cmd, &allRefs, &err, CP_UTF8))
			return -1;

		int linePos = 0;
		StringType singleRef;
		while (!(singleRef = allRefs.Tokenize(L"\03", linePos)).IsEmpty())
		{
			singleRef.TrimLeft(L"\r\n");
			int valuePos = 0;
			StringType refName = singleRef.Tokenize(L"\04", valuePos);

			if (refName.IsEmpty() || (filterCallback && !filterCallback(refName)))
				continue;

			GitRevRefBrowser ref;
			ref.m_CommitHash = CGitHash::FromHexStr(singleRef.Tokenize(L"\04", valuePos).Trim());
			if (valuePos < 0)
				continue;
			StringType objectType = singleRef.Tokenize(L"\04", valuePos).Trim();
			if (valuePos < 0)
				continue;
			if (objectType == L"commit")
				ref.m_objectType = GIT_OBJECT_COMMIT;
			else if (objectType == L"tag")
				ref.m_objectType = GIT_OBJECT_TAG;
			else if (objectType == L"tree")
				ref.m_objectType = GIT_OBJECT_TREE;
			ref.m_UpstreamRef = singleRef.Tokenize(L"\04", valuePos).Trim();
			if (valuePos < 0)
				continue;
			ref.m_Subject = singleRef.Tokenize(L"\04", valuePos).Trim();
			if (valuePos < 0)
				continue;
			ref.m_AuthorName = singleRef.Tokenize(L"\04", valuePos).Trim();
			if (valuePos < 0)
				continue;
			StringType email = singleRef.Tokenize(L"\04", valuePos).Trim().Trim(L"<>");
			if (valuePos < 0)
				continue;
			StringType date = singleRef.Tokenize(L"\04", valuePos).Trim();
			ref.m_AuthorDate = _wtoll(date);
			ref.m_CommitterName = singleRef.Tokenize(L"\04", valuePos).Trim();
			if (valuePos < 0)
				continue;
			StringType committerEmail = singleRef.Tokenize(L"\04", valuePos).Trim().Trim(L"<>");
			if (valuePos < 0)
				continue;
			ref.m_CommitterDate = _wtoll(singleRef.Tokenize(L"\04", valuePos).Trim());
			if (ref.m_AuthorName.IsEmpty())
			{
				ref.m_AuthorName = singleRef.Tokenize(L"\04", valuePos).Trim();
				if (valuePos < 0)
					continue;
				email = ref.m_AuthorName.Mid(ref.m_AuthorName.Find(L" <") + static_cast<int>(wcslen(L" <")));
				email.Truncate((std::max)(0, email.Find(L'>')));
				ref.m_AuthorName.Truncate((std::max)(0, ref.m_AuthorName.Find(L" <")));
				date = singleRef.Tokenize(L"\04", valuePos).Trim();
				ref.m_AuthorDate = _wtoll(date);
				if (ref.m_CommitterName.IsEmpty())
				{
					ref.m_CommitterName = ref.m_AuthorName;
					committerEmail = email;
					ref.m_CommitterDate = ref.m_AuthorDate;
				}
			}
			if (mailmap)
			{
				ref.m_AuthorName = mailmap.TranslateAuthor(ref.m_AuthorName, email);
				ref.m_CommitterName = mailmap.TranslateAuthor(ref.m_CommitterName, committerEmail);
			}

			if (tgit::wstr::StartsWith(StringView{ refName }, L"refs/heads/"))
				ref.m_Description = descriptions[std::wstring(refName.Mid(static_cast<int>(wcslen(L"refs/heads/"))))].c_str();

			map.emplace(refName, ref);
		}

		return 0;
	}
};
// TortoiseProc spells it unqualified, as the header-era typedef was
export using MAP_REF_GITREVREFBROWSER = GitRevRefBrowser::MAP_REF_GITREVREFBROWSER;

void GitRev::Clear()
{
	this->m_ParentHash.clear();
	m_AuthorName.Empty();
	m_AuthorEmail.Empty();
	m_AuthorDate = 0;
	m_CommitterName.Empty();
	m_CommitterEmail.Empty();
	m_CommitterDate = 0;
	m_Body.Empty();
	m_Subject.Empty();
	m_CommitHash.Empty();
	m_objectType = GIT_OBJECT_INVALID;
	m_sErr.Empty();
}

int GitRev::ParserParentFromCommit(const GIT_COMMIT* commit)
{
	ATLASSERT(commit);
	this->m_ParentHash.clear();
	GIT_COMMIT_LIST list;
	GIT_HASH   parent;

	git_get_commit_first_parent(commit,&list);
	while(git_get_commit_next_parent(&list,parent)==0)
		m_ParentHash.emplace_back(CGitHash::FromRaw(parent));
	return 0;
}



int GitRev::ParserFromCommit(const GIT_COMMIT *commit)
{
	ATLASSERT(commit);
	auto get_value1 = [](const char* str, const int len, std::optional<bool> encode = std::nullopt) {
		std::string_view multibyte{str, gsl::narrow<unsigned long long>(len)};
		if (encode) {
			return CUnicodeUtils::StdGetUnicode<StringType>(multibyte, *encode);
		} else {
			return CUnicodeUtils::GetUnicode<StringType>(multibyte);
		}
	};

	int encode = (commit->m_Encode != 0 && commit->m_EncodeSize != 0) ?
		CUnicodeUtils::StdGetCPCode(StringView{get_value1(commit->m_Encode, commit->m_EncodeSize)}) :
		CP_UTF8;

	this->m_CommitHash = CGitHash::FromRaw(commit->m_hash);

	this->m_AuthorDate = commit->m_Author.Date;
	this->m_AuthorEmail = get_value1(commit->m_Author.Email, commit->m_Author.EmailSize, encode);
	this->m_AuthorName = get_value1(commit->m_Author.Name, commit->m_Author.NameSize, encode);

	this->m_Body = get_value1(commit->m_Body, commit->m_BodySize, encode);

	this->m_CommitterDate = commit->m_Committer.Date;
	this->m_CommitterEmail = get_value1(commit->m_Committer.Email, commit->m_Committer.EmailSize, encode);
	this->m_CommitterName = get_value1(commit->m_Committer.Name, commit->m_Committer.NameSize, encode);

	this->m_Subject = get_value1(commit->m_Subject, commit->m_SubjectSize, encode);

	return 0;
}

int GitRev::ParserParentFromCommit(const git_commit* commit)
{
	ATLASSERT(commit);
	m_ParentHash.clear();
	const unsigned int parentCount = git_commit_parentcount(commit);
	for (unsigned int i = 0; i < parentCount; ++i)
		m_ParentHash.emplace_back(git_commit_parent_id(commit, i));

	return 0;
}

int GitRev::ParserFromCommit(const git_commit* commit)
{
	ATLASSERT(commit);
	Clear();
	m_objectType = GIT_OBJECT_COMMIT;

	int encode = CP_UTF8;

	const char* encodingstr = git_commit_message_encoding(commit);
	if (encodingstr) {
		encode = CUnicodeUtils::StdGetCPCode(CUnicodeUtils::Convert2Unicode<std::wstring>(encodingstr));
	}

	m_CommitHash = git_commit_id(commit);

	const git_signature* author = git_commit_author(commit);
	m_AuthorDate = author->when.time;
	m_AuthorEmail = CUnicodeUtils::StdGetUnicode<StringType>(std::string_view{author->email}, encode);
	m_AuthorName = CUnicodeUtils::StdGetUnicode<StringType>(std::string_view{author->name}, encode);

	const git_signature* committer = git_commit_committer(commit);
	m_CommitterDate = committer->when.time;
	m_CommitterEmail = CUnicodeUtils::StdGetUnicode<StringType>(std::string_view{committer->email}, encode);
	m_CommitterName = CUnicodeUtils::StdGetUnicode<StringType>(std::string_view{committer->name}, encode);

	const char* msg = git_commit_message_raw(commit);
	const char* body = strchr(msg, '\n');
	if (!body)
		m_Subject = CUnicodeUtils::StdGetUnicode<StringType>(std::string_view{msg}, encode);
	else
	{
		m_Subject = CUnicodeUtils::StdGetUnicode<StringType>(std::string_view(msg, body - msg), encode);
		m_Body = CUnicodeUtils::StdGetUnicode<StringType>(std::string_view{body + 1}, encode);
	}

	return 0;
}

int GitRev::GetCommitFromHash(git_repository* repo, const CGitHash& hash)
{
	ATLASSERT(repo);
	CAutoCommit commit;
	if (git_commit_lookup(commit.GetPointer(), repo, hash) < 0)
	{
		m_sErr = TGitH::GetLibGit2LastErr();
		return -1;
	}

	return ParserFromCommit(commit);
}

int GitRev::GetCommit(git_repository* repo, const CString& refname)
{
	ATLASSERT(repo);
	if (IsWorkingCopyRef(refname))
	{
		Clear();
		m_Subject = L"Working Tree";
		return 0;
	}

	CGitHash hash;
	if (CGit::GetHash(repo, hash, std::wstring(StringView{ refname }) + L"^{}")) // add ^{} in order to dereference signed tags
	{
		m_sErr = TGitH::GetLibGit2LastErr();
		return -1;
	}

	return GetCommitFromHash(repo, hash);
}

#if DEBUG
void GitRev::DbgPrint() const {
	ATLTRACE(L"Commit %s\r\n", this->m_CommitHash.ToString().c_str());
	for (unsigned int i = 0; i < this->m_ParentHash.size(); ++i)
	{
		ATLTRACE(L"Parent %i %s\r\n", i, m_ParentHash[i].ToString().c_str());
	}
	ATLTRACE(L"\r\n\r\n");
}
#endif

int GitRev::GetParentFromHash(const CGitHash& hash)
{
	CAutoLocker _(g_Git.m_critGitDllSec);

	GIT_COMMIT commit;
	try
	{
		g_Git.CheckAndInitDll();

		if (git_get_commit_from_hash(&commit, hash.ToRaw()))
		{
			m_sErr = (L"git_get_commit_from_hash failed for " + hash.ToString()).c_str();
			return -1;
		}
	}
	catch (const char* msg)
	{
		m_sErr = std::format(L"Could not get parents of commit \"{}\".\nlibgit reports:\n{}", hash.ToString(), CUnicodeUtils::Convert2Unicode<std::wstring>(msg)).c_str();
		return -1;
	}

	this->ParserParentFromCommit(&commit);
	git_free_commit(&commit);

	this->m_CommitHash=hash;

	return 0;
}


int GitRev::GetCommitFromHash_withoutLock(const CGitHash& hash)
{
	GIT_COMMIT commit;
	try
	{
		if (git_get_commit_from_hash(&commit, hash.ToRaw()))
		{
			m_sErr = (L"git_get_commit_from_hash failed for " + hash.ToString()).c_str();
			return -1;
		}
	}
	catch (const char * msg)
	{
		m_sErr = std::format(L"Could not get commit \"{}\".\nlibgit reports:\n{}", hash.ToString(), CUnicodeUtils::Convert2Unicode<std::wstring>(msg)).c_str();
		return -1;
	}

	m_objectType = GIT_OBJECT_COMMIT;
	this->ParserFromCommit(&commit);
	git_free_commit(&commit);

	m_sErr.Empty();

	return 0;
}

int GitRev::GetCommit(const CString& refname, bool allowTree /* false */)
{
	if (g_Git.UsingLibGit2(GIT_CMD_GET_COMMIT))
	{
		CAutoRepository repo(g_Git.GetGitRepository());
		if (!repo)
		{
			m_sErr = TGitH::GetLibGit2LastErr();
			return -1;
		}
		if (int ret = GetCommit(repo, refname); ret == 0 || ret < 0 && !allowTree)
			return ret;

		CGitHash hash;
		if (g_Git.GetHash(repo, hash, StringView{ refname }))
		{
			m_sErr = TGitH::GetLibGit2LastErr();
			return -1;
		}
		CAutoTree tree;
		if (git_tree_lookup(tree.GetPointer(), repo, hash) < 0)
		{
			m_sErr = TGitH::GetLibGit2LastErr();
			return -1;
		}

		Clear();
		m_CommitHash = hash;
		m_objectType = GIT_OBJECT_TREE;

		return 0;
	}

	[[maybe_unused]] CAutoLocker lock(g_Git.m_critGitDllSec);

	try
	{
		g_Git.CheckAndInitDll();
	}
	catch (const char* msg)
	{
		m_sErr = L"Could not initiate libgit.\nlibgit reports:\n" + CUnicodeUtils::Convert2Unicode<CStringW>(msg);
		return -1;
	}

	if (IsWorkingCopyRef(refname))
	{
		this->m_CommitHash.Empty();
		this->m_Subject = L"Working Tree";
		m_sErr.Empty();
		return 0;
	}
	CStringA rev = CUnicodeUtils::GetUTF8<CStringA>(g_Git.FixBranchName(refname + L"^{}")); // add ^{} in order to dereference signed tags
	GIT_HASH sha;

	try
	{
		if (git_get_sha1(rev.GetBuffer(), sha))
		{
			m_sErr = L"Could not get SHA-1 of ref \"" + g_Git.FixBranchName(refname);
			return -1;
		}
	}
	catch (const char* msg)
	{
		m_sErr = L"Could not get SHA-1 of ref \"" + g_Git.FixBranchName(refname) + L"\".\nlibgit reports:\n" + CUnicodeUtils::Convert2Unicode<CStringW>(msg);
		return -1;
	}

	CGitHash hash = CGitHash::FromRaw(sha);
	if (int ret = GetCommitFromHash_withoutLock(hash); ret == 0 || ret < 0 && !allowTree)
		return ret;

	CStringA revTree = CUnicodeUtils::GetUTF8<CStringA>(g_Git.FixBranchName(refname + L"^{tree}"));
	try
	{
		if (git_get_sha1(revTree, sha))
		{
			m_sErr = L"Ref \"" + g_Git.FixBranchName(refname) + L"\" is neither a commit nor a tree.";
			return -1;
		}
	}
	catch (const char* msg)
	{
		m_sErr = L"Could not parse ref.\nlibgit reports:\n" + CUnicodeUtils::Convert2Unicode<CStringW>(msg);
		return -1;
	}

	Clear();
	m_CommitHash = hash;
	m_objectType = GIT_OBJECT_TREE;

	return 0;
}

void GitRev::ApplyMailmap(const CGitMailmap& mailmap)
{
	if (!mailmap)
		return;
	mailmap.Translate(m_AuthorName, m_AuthorEmail);
	mailmap.Translate(m_CommitterName, m_CommitterEmail);
}

