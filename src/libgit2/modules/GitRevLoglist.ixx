module;
#include <gitdll.h>
#include <git2.h>
#include <synchapi.h>
#include <sanity.h>


export module GitRevLoglist;
import std;
import lanes;

import git_h;
import wstr;
import GitRev;
import TGitPath;
import GitMailmap;
import tgittypes;
import SmartLibgit2;
import DebugOutput;
import GitHash;

namespace fs = std::filesystem;

using TGitPath::CTGitPath;
using TGitPath::CTGitPathList;
using namespace SmartLibgit2;
using tgit::wstr::StringView;

class CLogCache;
class IAsyncDiffCB;

export struct GitRevLoglist : GitRev
{
	using CString = GitRev::StringType;
	using CTime = GitRev::TimeType;
	using CALL_UPDATE_DIFF_ASYNC = void(GitRevLoglist* pRev, IAsyncDiffCB* data);

	friend class CLogCache;

	GitRevLoglist();
	~GitRevLoglist();

	class GitRevLoglistSharedFiles
	{
	public:
		GitRevLoglistSharedFiles(PSRWLOCK lock, const TGitPath::CTGitPathList& files)
			: m_lock(lock)
			, m_files(files)
		{
			AcquireSRWLockShared(m_lock);
		}

		~GitRevLoglistSharedFiles() { ReleaseSRWLockShared(m_lock); }

		inline int GetCount() const { return m_files.GetCount(); }
		inline const CTGitPath& operator[](INT_PTR index) const { return m_files[index]; }

		const CTGitPathList& m_files;

	private:
		PSRWLOCK m_lock;
	};

	class GitRevLoglistSharedFilesWriter
	{
	public:
		GitRevLoglistSharedFilesWriter(PSRWLOCK lock, CTGitPathList& files, CTGitPathList& unrevfiles)
			: m_lock(lock)
			, m_files(files)
			, m_UnRevFiles(unrevfiles)
		{
			AcquireSRWLockExclusive(m_lock);
		}

		~GitRevLoglistSharedFilesWriter() { ReleaseSRWLockExclusive(m_lock); }

		CTGitPathList& m_files;
		CTGitPathList& m_UnRevFiles;

	private:
		PSRWLOCK m_lock;
	};

protected:
	int				m_RebaseAction = 0;
	unsigned int	m_Action = 0;
	CTGitPathList	m_Files;
	CTGitPathList	m_UnRevFiles;

	SRWLOCK m_lock;

public:
	CString m_Notes;

	wchar_t m_Mark = '\0';
	bool m_RolledUp = false; // Parent commits up to the next merge, fork or ref are not shown
	bool m_RolledUpIsForced = false; // for CGitLogList::ContextMenuAction()/ID_TOGGLE_ROLLUP
	CString m_Ref; // for Refloglist
	CString m_RefAction; // for Refloglist

	// Show version tree Graphic
	std::vector<Lanes::LaneType> m_Lanes;

	static std::atomic<std::shared_ptr<GitMailmap::CGitMailmap>> s_Mailmap;

	volatile LONG m_IsDiffFiles = FALSE;

	CALL_UPDATE_DIFF_ASYNC* m_CallDiffAsync = nullptr;

	int CheckAndDiff()
	{
		if (!m_IsDiffFiles && !m_CommitHash.IsEmpty())
		{
			const auto ret = SafeFetchFullInfo(&g_Git);
			if (ret != 0)
				InterlockedExchange(&m_IsDiffFiles, 2);
			else
				InterlockedExchange(&m_IsDiffFiles, TRUE);
			return ret;
		}
		return 1;
	}

	unsigned int& GetAction(IAsyncDiffCB* data)
	{
		if (!m_IsDiffFiles && m_CallDiffAsync)
			m_CallDiffAsync(this, data);
		else
			CheckAndDiff();
		return m_Action;
	}

	int& GetRebaseAction()
	{
		return m_RebaseAction;
	}

	GitRevLoglistSharedFiles GetFiles(IAsyncDiffCB* data)
	{
		// data might be nullptr when instant data is requested, cf. CGitLogListAction
		if (data && !m_IsDiffFiles && m_CallDiffAsync)
			m_CallDiffAsync(this, data);
		else
			CheckAndDiff();
		return GitRevLoglistSharedFiles(&m_lock, m_Files);
	}

	GitRevLoglistSharedFilesWriter GetFilesWriter()
	{
		return GitRevLoglistSharedFilesWriter(&m_lock, m_Files, m_UnRevFiles);
	}

	CTGitPathList& GetUnRevFiles()
	{
		return m_UnRevFiles;
	}

	void Parse(GIT_COMMIT* commit, const GitMailmap::CGitMailmap* mailmap)
	{
		ParserParentFromCommit(commit);
		ParserFromCommit(commit);
		// no caching here, because mailmap might have changed
		if (mailmap)
			ApplyMailmap(*mailmap);
	}

	CString& GetAuthorName()
	{
		return m_AuthorName;
	}

	CString& GetAuthorEmail()
	{
		return m_AuthorEmail;
	}

	CTime& GetAuthorDate()
	{
		return m_AuthorDate;
	}

	CString& GetCommitterName()
	{
		return m_CommitterName;
	}

	CString& GetCommitterEmail()
	{
		return m_CommitterEmail;
	}

	CTime& GetCommitterDate()
	{
		return m_CommitterDate;
	}

	CString& GetSubject()
	{
		return m_Subject;
	}

	CString& GetBody()
	{
		return m_Body;
	}

	CString GetSubjectBody(bool crlf = false) const
	{
		CString ret(m_Subject);
		if (!crlf)
		{
			ret += L"\n";
			ret += m_Body;
		}
		else
		{
			ret.TrimRight();
			ret += L"\r\n";
			CString body(m_Body);
			body.Replace(L"\n", L"\r\n");
			ret += body.TrimRight();
		}
		return ret;
	}

	BOOL IsBoundary() const { return m_Mark == L'-'; }

	void Clear() override;

	int SafeFetchFullInfo(CGit* git);

	int SafeGetSimpleList(CGit* git);
	volatile LONG m_IsSimpleListReady = FALSE;
	tgit::types::STRING_VECTOR m_SimpleFileList;  /* use for find and filter, no rename detection and line num stat info */

	static int GetRefLog(const CString& ref_, std::vector<GitRevLoglist>& refloglist, CString& error);
};

std::atomic<std::shared_ptr<GitMailmap::CGitMailmap>> GitRevLoglist::s_Mailmap = nullptr;

GitRevLoglist::GitRevLoglist() : GitRev(), m_lock(SRWLOCK_INIT) { }

GitRevLoglist::~GitRevLoglist()
{
}

void GitRevLoglist::Clear()
{
	GitRev::Clear();
	m_Action = 0;
	m_Files.Clear();
	m_UnRevFiles.Clear();
	m_Ref.Empty();
	m_RefAction.Empty();
	m_Mark = 0;
	m_IsDiffFiles = FALSE;
	m_CallDiffAsync = nullptr;
	m_IsSimpleListReady = FALSE;
}


int GitRevLoglist::SafeGetSimpleList(CGit* git)
{
	Expects(git);
	m_SimpleFileList.clear();
	if (git->UsingLibGit2(CGit::GIT_CMD_LOGLISTDIFF))
	{
		CAutoRepository repo(git->GetGitRepository());
		if (!repo)
			return -1;
		CAutoCommit commit;
		if (git_commit_lookup(commit.GetPointer(), repo, m_CommitHash) < 0)
			return -1;

		CAutoTree commitTree;
		if (git_commit_tree(commitTree.GetPointer(), commit) < 0)
			return -1;

		bool isRoot = git_commit_parentcount(commit) == 0;
		for (unsigned int parentId = 0; isRoot || parentId < git_commit_parentcount(commit); ++parentId)
		{
			CAutoTree parentTree;
			if (!isRoot)
			{
				CAutoCommit parentCommit;
				if (git_commit_parent(parentCommit.GetPointer(), commit, parentId) < 0)
					return -1;

				if (git_commit_tree(parentTree.GetPointer(), parentCommit) < 0)
					return -1;
			}
			isRoot = false;

			CAutoDiff diff;
			if (git_diff_tree_to_tree(diff.GetPointer(), repo, parentTree, commitTree, nullptr) < 0)
				return -1;

			size_t deltas = git_diff_num_deltas(diff);
			for (size_t i = 0; i < deltas; ++i)
			{
				const git_diff_delta* delta = git_diff_get_delta(diff, i);
				m_SimpleFileList.push_back(CUnicodeUtils::StdGetUnicode(delta->new_file.path));
			}
		}

		std::sort(m_SimpleFileList.begin(), m_SimpleFileList.end());
		m_SimpleFileList.erase(std::unique(m_SimpleFileList.begin(), m_SimpleFileList.end()), m_SimpleFileList.end());

		InterlockedExchange(&m_IsSimpleListReady, TRUE);
		return 0;
	}
	GIT_COMMIT commit = {};
	GIT_COMMIT_LIST list;
	GIT_HASH parent;

	[[maybe_unused]]CAutoLocker lock(g_Git.m_critGitDllSec);
	git->CheckAndInitDll();

	try
	{
		if (!git->GetGitSimpleListDiff())
			return -1;

		if (git_get_commit_from_hash(&commit, m_CommitHash.ToRaw()))
			return -1;
	}
	catch (const char*)
	{
		return -1;
	}

	git_get_commit_first_parent(&commit, &list);
	bool isRoot = git_commit_is_root(&commit) == 0;
	while (git_get_commit_next_parent(&list, parent) == 0 || isRoot)
	{
		GIT_FILE file = {};
		int count = 0;
		try
		{
			if (isRoot)
				git_root_diff(git->GetGitSimpleListDiff(), commit.m_hash, &file, &count, 0);
			else
				git_do_diff(git->GetGitSimpleListDiff(), parent, commit.m_hash, &file, &count, 0);
		}
		catch (const char*)
		{
			return -1;
		}

		isRoot = false;

		for (int j = 0; j < count; ++j)
		{
			char* newname;
			char* oldname;
			char status;
			int isBin, inc, dec, isDir;

			try
			{
				git_get_diff_file(git->GetGitSimpleListDiff(), file, j, &newname, &oldname, &isDir, &status, &isBin, &inc, &dec);
			}
			catch (const char*)
			{
				return -1;
			}

			m_SimpleFileList.push_back(CUnicodeUtils::StdGetUnicode(newname));
		}

		git_diff_flush(git->GetGitSimpleListDiff());
	}

	std::ranges::sort(m_SimpleFileList.begin(), m_SimpleFileList.end());


	auto [first, last] = std::ranges::unique(m_SimpleFileList.begin(), m_SimpleFileList.end());
	m_SimpleFileList.erase(first, last);

	auto leftovers_range = std::ranges::unique(m_SimpleFileList.begin(), m_SimpleFileList.end());
	m_SimpleFileList.erase(leftovers_range.begin(), leftovers_range.end());

	auto leftovers = std::ranges::unique(m_SimpleFileList);
	std::erase(m_SimpleFileList, leftovers);


	InterlockedExchange(&m_IsSimpleListReady, TRUE);
	git_free_commit(&commit);

	return 0;
}

int GitRevLoglist::SafeFetchFullInfo(CGit* git)
{
	Expects(git);
	AcquireSRWLockExclusive(&m_lock);
	SCOPE_EXIT { ReleaseSRWLockExclusive(&m_lock); };
	m_Files.Clear();
	if (git->UsingLibGit2(CGit::GIT_CMD_LOGLISTDIFF))
	{
		CAutoRepository repo(git->GetGitRepository());
		if (!repo)
		{
			m_sErr = TGitH::GetLibGit2LastErr();
			return -1;
		}
		CAutoCommit commit;
		if (git_commit_lookup(commit.GetPointer(), repo, m_CommitHash) < 0)
		{
			m_sErr = TGitH::GetLibGit2LastErr();
			return -1;
		}

		CAutoTree commitTree;
		if (git_commit_tree(commitTree.GetPointer(), commit) < 0)
		{
			m_sErr = TGitH::GetLibGit2LastErr();
			return -1;
		}

		bool isRoot = git_commit_parentcount(commit) == 0;
		for (unsigned int parentId = 0; isRoot || parentId < git_commit_parentcount(commit); ++parentId)
		{
			CAutoTree parentTree;
			if (!isRoot)
			{
				CAutoCommit parentCommit;
				if (git_commit_parent(parentCommit.GetPointer(), commit, parentId) < 0)
				{
					m_sErr = TGitH::GetLibGit2LastErr();
					return -1;
				}

				if (git_commit_tree(parentTree.GetPointer(), parentCommit) < 0)
				{
					m_sErr = TGitH::GetLibGit2LastErr();
					return -1;
				}
			}
			isRoot = false;

			CAutoDiff diff;
			if (git_diff_tree_to_tree(diff.GetPointer(), repo, parentTree, commitTree, nullptr) < 0)
			{
				m_sErr = TGitH::GetLibGit2LastErr();
				return -1;
			}

			// cf. CGit::GetGitDiff()
			if (TGitH::ms_iSimilarityIndexThreshold > 0)
			{
				git_diff_find_options diffopts = GIT_DIFF_FIND_OPTIONS_INIT;
				diffopts.flags = GIT_DIFF_FIND_COPIES | GIT_DIFF_FIND_RENAMES;
				diffopts.rename_threshold = diffopts.copy_threshold = static_cast<uint16_t>(TGitH::ms_iSimilarityIndexThreshold);
				if (git_diff_find_similar(diff, &diffopts) < 0)
				{
					m_sErr = TGitH::GetLibGit2LastErr();
					return -1;
				}
			}

			const git_diff_delta* lastDelta = nullptr;
			int oldAction = 0;
			size_t deltas = git_diff_num_deltas(diff);
			for (size_t i = 0; i < deltas; ++i)
			{
				CAutoPatch patch;
				if (git_patch_from_diff(patch.GetPointer(), diff, i) < 0)
				{
					m_sErr = TGitH::GetLibGit2LastErr();
					return -1;
				}

				const git_diff_delta* delta = git_patch_get_delta(patch);

				bool isDir = (delta->new_file.mode == GIT_FILEMODE_TREE);
				if (delta->status == GIT_DELTA_DELETED)
					isDir = (delta->old_file.mode == GIT_FILEMODE_TREE);

				if (lastDelta && strcmp(lastDelta->new_file.path, delta->new_file.path) == 0 && (lastDelta->status == GIT_DELTA_DELETED && delta->status == GIT_DELTA_ADDED || delta->status == GIT_DELTA_DELETED && lastDelta->status == GIT_DELTA_ADDED))
				{
					// rename handling
					CTGitPath path = m_Files[m_Files.GetCount() - 1];
					m_Files.RemoveItem(path);
					if (path.IsDirectory() && !isDir)
						path.UnsetDirectoryStatus();
					path.m_StatAdd = L"-";
					path.m_StatDel = L"-";
					path.m_Action = CTGitPath::LOGACTIONS_MODIFIED;
					m_Action = oldAction | CTGitPath::LOGACTIONS_MODIFIED;
					m_Files.AddPath(path);
					lastDelta = nullptr;
					continue;
				}
				lastDelta = delta;
				CTGitPath path;
				if (delta->new_file.path == delta->old_file.path)
					path.SetFromGit(delta->new_file.path, isDir != FALSE);
				else
				{
					const std::wstring newname = CUnicodeUtils::StdGetUnicode(delta->new_file.path);
					std::wstring oldname = CUnicodeUtils::StdGetUnicode(delta->old_file.path);
					path.SetFromGit(newname, oldname, isDir);
				}
				oldAction = m_Action;
				m_Action |= path.ParseAndUpdateStatus(delta->status);
				path.m_ParentNo = parentId;

				if (delta->flags & GIT_DIFF_FLAG_BINARY)
				{
					path.m_StatAdd = L"-";
					path.m_StatDel = L"-";
				}
				else
				{
					size_t adds, dels;
					if (git_patch_line_stats(nullptr, &adds, &dels, patch) < 0)
					{
						m_sErr = TGitH::GetLibGit2LastErr();
						return -1;
					}
					path.m_StatAdd = std::format(L"{}", adds);
					path.m_StatDel = std::format(L"{}", dels);
				}
				m_Files.AddPath(path);
			}
		}
		return 0;
	}

	GIT_COMMIT commit = { 0 };
	GIT_COMMIT_LIST list;
	GIT_HASH parent;

	[[maybe_unused]]CAutoLocker lock(g_Git.m_critGitDllSec);
	git->CheckAndInitDll();

	try
	{
		if (git_get_commit_from_hash(&commit, m_CommitHash.ToRaw()))
		{
			m_sErr = std::format(L"git_get_commit_from_hash failed for {}", m_CommitHash.ToString()).data();
			tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ L": " + m_sErr + L'\n');
			return -1;
		}
	}
	catch (std::exception e)
	{
		m_sErr = std::format(L"Could not get commit \"{}\".\nlibgit reports:\n{}", m_CommitHash.ToString(), CUnicodeUtils::StdGetUnicode(e.what())).data();
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ L": " + m_sErr + L'\n');
		return -1;
	}

	int i = 0;
	git_get_commit_first_parent(&commit, &list);
	bool isRoot = (list == nullptr);

	while (git_get_commit_next_parent(&list, parent) == 0 || isRoot)
	{
		GIT_FILE file = 0;
		int count = 0;

		try
		{
			if (!git->GetGitDiff())
			{
				git_free_commit(&commit);
				return -1;
			}

			if (isRoot)
				git_root_diff(git->GetGitDiff(), m_CommitHash.ToRaw(), &file, &count, 1);
			else
				git_do_diff(git->GetGitDiff(), parent, commit.m_hash, &file, &count, 1);
		}
		catch (const char* msg)
		{
			m_sErr.Format(L"Could do diff for \"%ls\".\nlibgit reports:\n%s", m_CommitHash.ToString().data(), CUnicodeUtils::GetUnicode<CString>(msg));
			tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ L": " + m_sErr + L'\n');
			git_free_commit(&commit);
			return -1;
		}
		isRoot = false;

		CTGitPath path;
		for (int j = 0; j < count; ++j)
		{
			char* newname;
			char* oldname;

			char status;
			int isBin, inc, dec, isDir;
			git_get_diff_file(git->GetGitDiff(), file, j, &newname, &oldname, &isDir, &status, &isBin, &inc, &dec);

			auto strnewname = CUnicodeUtils::StdGetUnicode(newname);
			// SetFromGit resets the path
			if (strcmp(newname, oldname) == 0)
				path.SetFromGit(strnewname, !!isDir);
			else
			{
				auto stroldname = CUnicodeUtils::StdGetUnicode(oldname);
				path.SetFromGit(strnewname, stroldname, !!isDir);
			}
			path.ParseAndUpdateStatus(status);
			path.m_ParentNo = i;

			m_Action |= path.m_Action;

			if (isBin)
			{
				path.m_StatAdd = L"-";
				path.m_StatDel = L"-";
			}
			else
			{
				path.m_StatAdd = std::format(L"{}", inc);
				path.m_StatDel = std::format(L"{}", dec);
			}
			m_Files.AddPath(path);
		}
		git_diff_flush(git->GetGitDiff());
		++i;
	}

	git_free_commit(&commit);

	return 0;
}

int GitRevLoglist::GetRefLog(const CString& ref_, std::vector<GitRevLoglist>& refloglist, CString& error)
{
	const auto ref = StringView{ref_};
	refloglist.clear();
	if (g_Git.m_IsUseLibGit2)
	{
		CAutoRepository repo(g_Git.GetGitRepository());
		if (!repo)
		{
			error = GetLibGit2LastErr();
			return -1;
		}

		CAutoReflog reflog;
		if (git_reflog_read(reflog.GetPointer(), repo, CUnicodeUtils::StdGetUTF8(ref).c_str()) < 0)
		{
			error = GetLibGit2LastErr();
			return -1;
		}

		for (size_t i = 0; i < git_reflog_entrycount(reflog); ++i)
		{
			const git_reflog_entry *entry = git_reflog_entry_byindex(reflog, i);
			if (!entry)
				continue;

			GitRevLoglist rev;
			rev.m_CommitHash = git_reflog_entry_id_new(entry);
			rev.m_Ref.Format(L"%s@{%zu}", ref.data(), i);
			rev.GetCommitterDate() = CTime(git_reflog_entry_committer(entry)->when.time);
			if (git_reflog_entry_message(entry) != nullptr)
			{
				CString one = CUnicodeUtils::GetUnicode<CString>(git_reflog_entry_message(entry));
				int message = one.Find(L": ");
				if (message > 0)
				{
					rev.m_RefAction = one.Left(message);
					rev.GetSubject() = one.Mid(message + 2);
				}
				else
					rev.m_RefAction = one;
			}
			refloglist.push_back(rev);
		}
		return 0;
	}
	else if (g_Git.m_IsUseGitDLL)
	{
		[[maybe_unused]]CAutoLocker lock(g_Git.m_critGitDllSec);
		g_Git.CheckAndInitDll();
		std::vector<GitRevLoglist> tmp;
		const auto callback = [](
			const char* /*refname*/,
			struct GIT_OBJECT_OID* /*old_oid*/,
			struct GIT_OBJECT_OID* new_oid,
			const char* /*committer*/,
			unsigned long long time,
			int /*sz*/,
			const char* msg,
			void* data
		)
		{
			auto vector = *static_cast<std::vector<GitRevLoglist>*>(data);
			GitRevLoglist rev;
			rev.m_CommitHash = CGitHash::FromRaw(new_oid->hash);
			rev.GetCommitterDate() = CTime(time);

			CString one = CUnicodeUtils::GetUnicode<CString>(msg);
			int message = one.Find(L": ");
			if (message > 0)
			{
				rev.m_RefAction = one.Left(message);
				rev.GetSubject() = one.Mid(message + 2).TrimRight(L'\n');
			}
			else
				rev.m_RefAction = one.TrimRight(L'\n');
			vector.push_back(rev);

			return 0;
		};
		// no error checking, because the only error which could occur is file not found
		git_for_each_reflog_ent(CUnicodeUtils::StdGetUTF8(StringView{ref}).c_str(), callback, &tmp);

		for (size_t i = tmp.size(), id = 0; i > 0; --i, ++id)
		{
			GitRevLoglist rev = tmp[i - 1];
			rev.m_Ref.Format(L"%s@{%zu}", ref.data(), id);
			refloglist.push_back(rev);
		}
		return 0;
	}

	const auto dotGitDirMaybe = GitAdminDir::GetAdminDirPath(StringView{g_Git.m_CurrentDir});
	if (!dotGitDirMaybe.has_value())
	{
		error = L".git directory not found";
		return -1;
	}

	error.Empty();
	// git.exe would fail with "branch not known"

	const auto dotGitDir = fs::path(dotGitDirMaybe.value().path);

	if (!fs::exists(dotGitDir / ref.data()))
		return 0;

	CString out;
	// The --pretty format was quoted inside the command line because it contains
	// spaces; as an argv element it is one string and needs no quotes of its own.
	if (g_Git.Run({ L"git.exe", L"reflog", L"show", L"--pretty=%H %gD: %gs", L"--date=raw", L"--end-of-options", std::wstring(ref) }, &out, &error))
		return -1;

	int i = 0;
	CString prefix = ref_ + L"@{";
	int pos = 0;
	while (pos >= 0)
	{
		CString one = out.Tokenize(L"\n", pos);
		const int refPos = one.Find(L' ');
		if (refPos < 0)
			continue;

		CString str = one.Left(refPos);
		GitRevLoglist rev = {
		.m_CommitHash = CGitHash::FromHexStr(str),
		};
		rev.m_Ref.Format(L"%s@{%d}", ref_, i++);
		const int prefixPos = one.Find(prefix, refPos + 1);
		if (prefixPos != refPos + 1)
			continue;

		int spacePos = one.Find(L' ', prefixPos + prefix.GetLength() + 1);
		if (spacePos < 0)
			continue;

		CString timeStr = one.Mid(prefixPos + prefix.GetLength(), spacePos - prefixPos - prefix.GetLength());
		rev.GetCommitterDate() = CTime(_wtoi(timeStr));
		if (int action = one.Find(L"}: ", spacePos + 1); action > 0)
		{
			action += 2;
			int message = one.Find(L": ", action);
			if (message > 0)
			{
				rev.m_RefAction = one.Mid(action + 1, message - action - 1);
				rev.GetSubject() = one.Right(one.GetLength() - message - 2);
			}
			else
				rev.m_RefAction = one.Mid(action + 1);
		}

		refloglist.push_back(rev);
	}
	return 0;
}
