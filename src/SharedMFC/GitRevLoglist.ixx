module;

#include <windows.h>
#include <synchapi.h>
#include <gitdll.h>
#include <git2/diff.h>
#include <tgitlib/sanity.h>

export module GitRevLoglist;
import tgit;

import GitMailmap;
import TGitMessageBox;
import Registry;
import lanes;
import LogDlgFilter;
import HintCtrl;

class CLogDataVector;


namespace fs = std::filesystem;

using namespace misc_constants;
using namespace RegistryDep;
using namespace tgit;
using namespace TGitH;
using TGitPath::CTGitPath;
using TGitPath::CTGitPathList;
using tgit::str::StringView;
using tgit::wstr::CString;
using tgit::wstr::CTime;
using namespace tgit::libgit2;


using Locker = CComCritSecLock<CComCriticalSection>;


// NOLINTBEGIN(*-throw-by-value-catch-by-reference)
export namespace GitLogList {


#pragma pack (1)
struct SLogCacheIndexHeader {
	std::uint32_t m_Magic;
	std::uint32_t m_Version;
	std::int32_t m_ItemCount;
};



struct SLogCacheIndexItem {
	CGitHash m_Hash{};
	std::uint64_t m_Offset{};
};



struct SLogCacheIndexFile {
	SLogCacheIndexHeader m_Header{};
	SLogCacheIndexItem m_Item[1]; //dynmatic size
};



struct SLogCacheRevFileHeader {
	std::uint32_t m_Magic;
	std::uint32_t m_Action;
	std::uint32_t m_Stage; // TODO: unused, can be removed
	std::uint32_t m_ParentNo;
	std::uint32_t m_Add;
	std::uint32_t m_Del;
	std::uint32_t m_IsSubmodule;
	std::int32_t m_FileNameSize;
	std::int32_t m_OldFileNameSize;
	wchar_t m_FileName[1];
};



struct SLogCacheRevItemHeader {
	std::uint32_t m_Magic;
	std::int32_t m_FileCount;
};



struct SLogCacheDataFileHeader {
	std::uint32_t m_Magic;
	std::uint32_t m_Version;
};
# pragma pack ()




template <typename Cont, typename Pred>
void for_each(Cont& c, Pred&& p) {
	std::for_each(cbegin(c), cend(c), p);
}


template <typename Cont, typename Pred>
auto any_of(Cont& c, Pred&& p) {
	return std::any_of(cbegin(c), cend(c), p);
}



class SelectionHistory {

public:
	SelectionHistory() : lastselected{HISTORYLENGTH} {}


	void Add(const CGitHash& hash) {
		if (hash.IsEmpty())
			return;

		const size_t size = lastselected.size();

		// re-select last selected commit
		if (size > 0 && hash == lastselected[size - 1]) {
			// reset location
			if (location != size - 1)
				location = size - 1;
			return;
		}

		// go back and some commit was highlight
		if (size > 0 && location != size - 1) {
			// Re-select current one, it may be a forked point.
			if (hash == lastselected[location])
				// Discard it later.
				// That is that discarding forward history when a forked entry is really coming.
				// And user has the chance to Go Forward again in this situation.
				// IOW, (hash != lastselected[location]) means user wants a forked history,
				// and this change saves one step from old behavior.
				return;

			// Discard forward history if any
			while (lastselected.size() - 1 > location)
				lastselected.pop_back();
		}

		if (lastselected.size() >= HISTORYLENGTH)
			lastselected.erase(lastselected.cbegin());

		lastselected.push_back(hash);
		location = lastselected.size() - 1;
	}


	BOOL GoBack(CGitHash& historyEntry) {
		if (location < 1)
			return FALSE;

		this->location -= 1;
		historyEntry = this->lastselected[this->location];

		return TRUE;
	}


	BOOL GoForward(CGitHash& historyEntry) {
		if (this->location >= this->lastselected.size() - 1)
			return FALSE;

		this->location += 1;
		historyEntry = this->lastselected[this->location];

		return TRUE;
	}

private:
	std::vector<CGitHash> lastselected{};
	size_t location = 0;
};



int Compare(const void* p1, const void* p2) {
	return memcmp(p1, p2, static_cast<int>(tgit::invarients::GitFormatHolder::GitHashSize()));
}



void DrawTrackingRoundRect(const HDC& hdc, const CRect rect, const HBRUSH& brush, const COLORREF& darkColor) {
	constexpr POINT point = { 4, 4 };
	CRect rt2 = rect;
	rt2.DeflateRect(1, 1);
	rt2.OffsetRect(2, 2);

	const HPEN& nullPen = ::CreatePen(PS_NULL, 0, 0);
	const HPEN& oldpen = static_cast<HPEN>(::SelectObject(hdc, nullPen));
	const HBRUSH& darkBrush = ::CreateSolidBrush(darkColor);
	const HBRUSH& oldbrush = static_cast<HBRUSH>(::SelectObject(hdc, darkBrush));
	::RoundRect(hdc, rt2.left, rt2.top, rt2.right, rt2.bottom, point.x, point.y);

	::SelectObject(hdc, brush);
	rt2.OffsetRect(-2, -2);
	::RoundRect(hdc, rt2.left, rt2.top, rt2.right, rt2.bottom, point.x, point.y);
	::SelectObject(hdc, oldbrush);
	::SelectObject(hdc, oldpen);
	::DeleteObject(nullPen);
	::DeleteObject(darkBrush);
}


void DrawUpstream(const HDC& hdc, const CRect rect, const COLORREF& color, const int bold) {
	const HPEN& pen = ::CreatePen(PS_SOLID, bold, color);
	const HPEN& oldpen = static_cast<HPEN>(::SelectObject(hdc, pen));
	::MoveToEx(hdc, rect.left + 2 + bold, rect.top + 2 - bold, nullptr);
	::LineTo(hdc, rect.left + 2 + bold, rect.bottom + 1 - bold);
	::MoveToEx(hdc, rect.left + 3, rect.top + 1, nullptr);
	::LineTo(hdc, rect.left, rect.top + 4);
	::MoveToEx(hdc, rect.left + 2 + bold, rect.top + 1, nullptr);
	::LineTo(hdc, rect.right + 1 + bold, rect.top + 4);
	::MoveToEx(hdc, rect.left + 1, rect.top + 2 + bold, nullptr);
	::LineTo(hdc, rect.right + 1 + bold, rect.top + 2 + bold);
	::SelectObject(hdc, oldpen);
	::DeleteObject(pen);
}


Gdiplus::Color GetGdiColor(const COLORREF col) {
	return Gdiplus::Color(GetRValue(col),GetGValue(col),GetBValue(col));
}




class IAsyncDiffCB {};



class GitRevLoglistSharedFiles {
public:
	GitRevLoglistSharedFiles(const PSRWLOCK& lock, const TGitPath::CTGitPathList& files)
		: m_files(files)
		, m_lock(lock) {
		AcquireSRWLockShared(m_lock);
	}


	~GitRevLoglistSharedFiles() { ReleaseSRWLockShared(m_lock); }

	inline int GetCount() const { return m_files.GetCount(); }
	inline const CTGitPath& operator[](const INT_PTR index) const { return m_files[index]; }

	const CTGitPathList& m_files;

private:
	PSRWLOCK m_lock;
};



class GitRevLoglistSharedFilesWriter {
public:
	GitRevLoglistSharedFilesWriter(const PSRWLOCK& lock, CTGitPathList& files, CTGitPathList& unrevfiles)
		: m_files(files)
		, m_UnRevFiles(unrevfiles)
		, m_lock(lock) {
		AcquireSRWLockExclusive(m_lock);
	}


	~GitRevLoglistSharedFilesWriter() { ReleaseSRWLockExclusive(m_lock); }

	CTGitPathList& m_files;
	CTGitPathList& m_UnRevFiles;

private:
	PSRWLOCK m_lock;
};



struct GitRevLoglist : GitRev {

	using CALL_UPDATE_DIFF_ASYNC = void(GitRevLoglist* pRev, IAsyncDiffCB* data);


	GitRevLoglist() :
		GitRev(), m_lock(SRWLOCK_INIT) {}


	// ~GitRevLoglist() override;

	CTGitPathList m_Files{};

protected:
	int m_RebaseAction = 0;
	CTGitPathList m_UnRevFiles{};

	SRWLOCK m_lock;

public:
	unsigned int m_Action = 0;

	CString m_Notes{};

	wchar_t m_Mark = '\0';
	bool m_RolledUp = false; // Parent commits up to the next merge, fork or ref are not shown
	bool m_RolledUpIsForced = false; // for CGitLogList::ContextMenuAction()/ID_TOGGLE_ROLLUP
	CString m_Ref{}; // for Refloglist
	CString m_RefAction{}; // for Refloglist

	// Show version tree Graphic
	std::vector<Lanes::LaneType> m_Lanes;

	static std::atomic<std::shared_ptr<GitMailmap::CGitMailmap>> s_Mailmap;

	volatile LONG m_IsDiffFiles = FALSE;

	CALL_UPDATE_DIFF_ASYNC* m_CallDiffAsync = nullptr;


	volatile LONG m_IsSimpleListReady = FALSE;
	std::vector<std::wstring> m_SimpleFileList{}; /* use for find and filter, no rename detection and line num stat info */


	CString& GetAuthorName() { return m_AuthorName; }

	CString& GetAuthorEmail() { return m_AuthorEmail; }

	CTime& GetAuthorDate() { return m_AuthorDate; }

	CString& GetCommitterName() { return m_CommitterName; }

	CString& GetCommitterEmail() { return m_CommitterEmail; }

	CTime& GetCommitterDate() { return m_CommitterDate; }

	int& GetRebaseAction() { return m_RebaseAction; }

	GitRevLoglistSharedFilesWriter GetFilesWriter() { return GitRevLoglistSharedFilesWriter(&m_lock, m_Files, m_UnRevFiles); }

	CString& GetSubject() { return m_Subject; }

	CString& GetBody() { return m_Body; }

	[[nodiscard]] BOOL IsBoundary() const { return m_Mark == L'-'; }

	CTGitPathList& GetUnRevFiles() { return m_UnRevFiles; }


	int CheckAndDiff() {
		if (!m_IsDiffFiles && !m_CommitHash.IsEmpty()) {
			const auto ret = SafeFetchFullInfo(&g_Git);
			if (ret != 0)
				InterlockedExchange(&m_IsDiffFiles, 2);
			else
				InterlockedExchange(&m_IsDiffFiles, TRUE);
			return ret;
		}
		return 1;
	}


	unsigned int& GetAction(IAsyncDiffCB* data) {
		if (!m_IsDiffFiles && m_CallDiffAsync)
			m_CallDiffAsync(this, data);
		else
			CheckAndDiff();
		return m_Action;
	}


	GitRevLoglistSharedFiles GetFiles(IAsyncDiffCB* data) {
		// data might be nullptr when instant data is requested, cf. CGitLogListAction
		if (data && !m_IsDiffFiles && m_CallDiffAsync)
			m_CallDiffAsync(this, data);
		else
			CheckAndDiff();
		return GitRevLoglistSharedFiles(&m_lock, m_Files);
	}


	void Parse(const GIT_COMMIT* commit, const GitMailmap::CGitMailmap* mailmap) {
		ParserParentFromCommit(commit);
		ParserFromCommit(commit);
		// no caching here, because mailmap might have changed
		if (mailmap)
			ApplyMailmap(*mailmap);
	}


	[[nodiscard]] CString GetSubjectBody(const bool crlf = false) const {
		CString ret(this->m_Subject);
		if (!crlf) {
			ret += L"\n";
			ret += this->m_Body;
		} else {
			ret.TrimRight();
			ret += L"\r\n";
			CString body(this->m_Body);
			body.Replace(L"\n", L"\r\n");
			ret += body.TrimRight();
		}
		return ret;
	}


	void Clear() override {
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


	int SafeFetchFullInfo(CGit* git) {
		Expects(git);
		AcquireSRWLockExclusive(&m_lock);
		SCOPE_EXIT { ReleaseSRWLockExclusive(&m_lock); };
		m_Files.Clear();
		if (git->UsingLibGit2(GIT_CMD_LOGLISTDIFF)) {
			const wil::unique_git_repository repo(git->GetGitRepository());
			if (!repo) {
				m_sErr = TGitH::GetLibGit2LastErr();
				return -1;
			}
			wil::unique_git_commit commit;
			if (git_commit_lookup(commit.put(), repo.get(), m_CommitHash) < 0) {
				m_sErr = TGitH::GetLibGit2LastErr();
				return -1;
			}

			wil::unique_git_tree commitTree;
			if (git_commit_tree(commitTree.put(), commit.get()) < 0) {
				m_sErr = TGitH::GetLibGit2LastErr();
				return -1;
			}

			bool isRoot = git_commit_parentcount(commit.get()) == 0;
			for (unsigned int parentId = 0; isRoot || parentId < git_commit_parentcount(commit.get()); ++parentId) {
				wil::unique_git_tree parentTree;
				if (!isRoot) {
					wil::unique_git_commit parentCommit;
					if (git_commit_parent(parentCommit.put(), commit.get(), parentId) < 0) {
						m_sErr = TGitH::GetLibGit2LastErr();
						return -1;
					}

					if (git_commit_tree(parentTree.put(), parentCommit.get()) < 0) {
						m_sErr = TGitH::GetLibGit2LastErr();
						return -1;
					}
				}
				isRoot = false;

				wil::unique_git_diff diff;
				if (git_diff_tree_to_tree(diff.put(), repo.get(), parentTree.get(), commitTree.get(), nullptr) < 0) {
					m_sErr = TGitH::GetLibGit2LastErr();
					return -1;
				}

				// cf. CGit::GetGitDiff()
				if (GitCall::ms_iSimilarityIndexThreshold > 0) {
					git_diff_find_options diffopts = GIT_DIFF_FIND_OPTIONS_INIT;
					diffopts.flags = GIT_DIFF_FIND_COPIES | GIT_DIFF_FIND_RENAMES;
					diffopts.rename_threshold = diffopts.copy_threshold = static_cast<uint16_t>(GitCall::ms_iSimilarityIndexThreshold);
					if (git_diff_find_similar(diff.get(), &diffopts) < 0) {
						m_sErr = TGitH::GetLibGit2LastErr();
						return -1;
					}
				}

				const git_diff_delta* lastDelta = nullptr;
				int oldAction = 0;
				size_t deltas = git_diff_num_deltas(diff.get());
				for (size_t i = 0; i < deltas; ++i) {
					wil::unique_git_patch patch;
					if (git_patch_from_diff(patch.put(), diff.get(), i) < 0) {
						m_sErr = TGitH::GetLibGit2LastErr();
						return -1;
					}

					const git_diff_delta* delta = git_patch_get_delta(patch.get());

					// the directory *bit*, as upstream tested it: a submodule (gitlink,
					// 0160000) carries it too and is listed as a directory; == TREE is not
					const auto hasDirBit = [](const std::uint32_t mode) { return (mode & GIT_FILEMODE_TREE) == GIT_FILEMODE_TREE; };
					bool isDir = hasDirBit(delta->new_file.mode);
					if (delta->status == GIT_DELTA_DELETED)
						isDir = hasDirBit(delta->old_file.mode);

					if (lastDelta && strcmp(lastDelta->new_file.path, delta->new_file.path) == 0 && (lastDelta->status == GIT_DELTA_DELETED && delta->status ==
						GIT_DELTA_ADDED || delta->status == GIT_DELTA_DELETED && lastDelta->status == GIT_DELTA_ADDED)) {
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
					else {
						const auto newname = CUnicodeUtils::StdGetUnicode(delta->new_file.path);
						const auto oldname = CUnicodeUtils::StdGetUnicode(delta->old_file.path);
						path.SetFromGit(newname, oldname, isDir);
					}
					oldAction = win32_cast(m_Action);
					m_Action |= path.ParseAndUpdateStatus(delta->status);
					path.m_ParentNo = win32_cast(parentId);

					if (delta->flags & GIT_DIFF_FLAG_BINARY) {
						path.m_StatAdd = L"-";
						path.m_StatDel = L"-";
					} else {
						size_t adds, dels;
						if (git_patch_line_stats(nullptr, &adds, &dels, patch.get()) < 0) {
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

		[[maybe_unused]] CAutoLocker _(g_Git.m_critGitDllSec);
		git->CheckAndInitDll();

		try {
			if (git_get_commit_from_hash(&commit, m_CommitHash.ToRaw())) {
				m_sErr = std::format(L"git_get_commit_from_hash failed for {}", m_CommitHash.ToString()).data();
				tgit::DebugOutput::TraceRaw(std::wstring(__FUNCTION__ L": " + m_sErr + L'\n'));
				return -1;
			}
		} catch (std::exception& e) {
			m_sErr = std::format(L"Could not get commit \"{}\".\nlibgit reports:\n{}", m_CommitHash.ToString(), CUnicodeUtils::StdGetUnicode(e.what())).data();
			tgit::DebugOutput::TraceRaw(std::wstring(__FUNCTION__ L": " + m_sErr + L'\n'));
			return -1;
		}

		int i = 0;
		git_get_commit_first_parent(&commit, &list);
		bool isRoot = (list == nullptr);

		while (git_get_commit_next_parent(&list, parent) == 0 || isRoot) {
			GIT_FILE file = {};
			int count = 0;

			try {
				if (!git->GetGitDiff()) {
					git_free_commit(&commit);
					return -1;
				}

				if (isRoot)
					git_root_diff(git->GetGitDiff(), m_CommitHash.ToRaw(), &file, &count, 1);
				else
					git_do_diff(git->GetGitDiff(), parent, commit.m_hash, &file, &count, 1);
			} catch (const std::exception& ex) {
				m_sErr.Format(L"Could do diff for \"%ls\".\nlibgit reports:\n%s",
				              m_CommitHash.ToString().data(),
				              CUnicodeUtils::GetUnicode<CString>(ex.what()));
				tgit::DebugOutput::TraceRaw(std::wstring(__FUNCTION__ L": " + m_sErr + L'\n'));
				git_free_commit(&commit);
				return -1;
			}
			isRoot = false;

			CTGitPath path;
			for (int j = 0; j < count; ++j) {
				char* newname;
				char* oldname;

				char status;
				int isBin, inc, dec, isDir;
				git_get_diff_file(git->GetGitDiff(), file, j, &newname, &oldname, &isDir, &status, &isBin, &inc, &dec);

				auto strnewname = CUnicodeUtils::StdGetUnicode(newname);
				// SetFromGit resets the path
				if (strcmp(newname, oldname) == 0)
					path.SetFromGit(strnewname, !!isDir);
				else {
					auto stroldname = CUnicodeUtils::StdGetUnicode(oldname);
					path.SetFromGit(strnewname, stroldname, !!isDir);
				}
				path.ParseAndUpdateStatus(status);
				path.m_ParentNo = i;

				m_Action |= path.m_Action;

				path.m_StatAdd = isBin ? L"-" : std::to_wstring(inc);
				path.m_StatDel = isBin ? L"-" : std::to_wstring(dec);
				m_Files.AddPath(path);
			}
			git_diff_flush(git->GetGitDiff());
			++i;
		}

		git_free_commit(&commit);

		return 0;
	}


	int SafeGetSimpleList(CGit* git) {
		Expects(git);
		m_SimpleFileList.clear();
		if (git->UsingLibGit2(GIT_CMD_LOGLISTDIFF)) {
			const wil::unique_git_repository repo(git->GetGitRepository());
			if (!repo)
				return -1;
			wil::unique_git_commit commit;
			if (git_commit_lookup(commit.put(), repo.get(), m_CommitHash) < 0)
				return -1;

			wil::unique_git_tree commitTree;
			if (git_commit_tree(commitTree.put(), commit.get()) < 0)
				return -1;

			bool isRoot = git_commit_parentcount(commit.get()) == 0;
			for (unsigned int parentId = 0; isRoot || parentId < git_commit_parentcount(commit.get()); ++parentId) {
				wil::unique_git_tree parentTree;
				if (!isRoot) {
					wil::unique_git_commit parentCommit;
					if (git_commit_parent(parentCommit.put(), commit.get(), parentId) < 0)
						return -1;

					if (git_commit_tree(parentTree.put(), parentCommit.get()) < 0)
						return -1;
				}
				isRoot = false;

				wil::unique_git_diff diff;
				if (git_diff_tree_to_tree(diff.put(), repo.get(), parentTree.get(), commitTree.get(), nullptr) < 0)
					return -1;

				const size_t deltas = git_diff_num_deltas(diff.get());
				for (size_t i = 0; i < deltas; ++i) {
					const git_diff_delta* delta = git_diff_get_delta(diff.get(), i);
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

		[[maybe_unused]] CAutoLocker _(g_Git.m_critGitDllSec);
		git->CheckAndInitDll();

		try {
			if (!git->GetGitSimpleListDiff())
				return -1;

			if (git_get_commit_from_hash(&commit, m_CommitHash.ToRaw()))
				return -1;
		} catch (const std::exception&) {
			return -1;
		}

		git_get_commit_first_parent(&commit, &list);
		bool isRoot = git_commit_is_root(&commit) == 0;
		while (git_get_commit_next_parent(&list, parent) == 0 || isRoot) {
			GIT_FILE file = {};
			int count = 0;
			try {
				if (isRoot)
					git_root_diff(git->GetGitSimpleListDiff(), commit.m_hash, &file, &count, 0);
				else
					git_do_diff(git->GetGitSimpleListDiff(), parent, commit.m_hash, &file, &count, 0);
			} catch (const std::exception&) {
				return -1;
			}

			isRoot = false;

			for (int j = 0; j < count; ++j) {
				char* newname;
				char* oldname;
				char status;
				int isBin, inc, dec, isDir;

				try {
					git_get_diff_file(git->GetGitSimpleListDiff(), file, j, &newname, &oldname, &isDir, &status, &isBin, &inc, &dec);
				} catch (const std::exception&) {
					return -1;
				}

				m_SimpleFileList.push_back(CUnicodeUtils::StdGetUnicode(newname));
			}

			git_diff_flush(git->GetGitSimpleListDiff());
		}

		std::sort(m_SimpleFileList.begin(), m_SimpleFileList.end());

		// auto leftovers = std::ranges::unique(m_SimpleFileList);
		// std::erase(m_SimpleFileList, leftovers);
		m_SimpleFileList.erase(std::unique(m_SimpleFileList.begin(), m_SimpleFileList.end()), m_SimpleFileList.end());

		InterlockedExchange(&m_IsSimpleListReady, TRUE);
		git_free_commit(&commit);

		return 0;
	}


	// Just for the namespace
	static int GetRefLog(const CString& ref_, std::vector<GitRevLoglist>& refloglist, CString& error) {
		const auto ref = str::as_view(ref_);
		refloglist.clear();
		if (g_Git.m_IsUseLibGit2) {
			const wil::unique_git_repository repo(g_Git.GetGitRepository());
			if (!repo) {
				error = GetLibGit2LastErr();
				return -1;
			}

			wil::unique_git_reflog reflog;
			if (git_reflog_read(reflog.put(), repo.get(), CUnicodeUtils::StdGetUTF8(ref).c_str()) < 0) {
				error = GetLibGit2LastErr();
				return -1;
			}

			for (size_t i = 0; i < git_reflog_entrycount(reflog.get()); ++i) {
				const git_reflog_entry* entry = git_reflog_entry_byindex(reflog.get(), i);
				if (!entry)
					continue;

				GitRevLoglist rev;
				rev.m_CommitHash = git_reflog_entry_id_new(entry);
				rev.m_Ref.Format(L"%s@{%zu}", ref.data(), i);
				rev.GetCommitterDate() = CTime(git_reflog_entry_committer(entry)->when.time);
				if (git_reflog_entry_message(entry) != nullptr) {
					CString one = CUnicodeUtils::GetUnicode<CString>(git_reflog_entry_message(entry));
					const int message = one.Find(L": ");
					if (message > 0) {
						rev.m_RefAction = one.Left(message);
						rev.GetSubject() = one.Mid(message + 2);
					} else
						rev.m_RefAction = one;
				}
				refloglist.push_back(rev);
			}
			return 0;
		} else if (g_Git.m_IsUseGitDLL) {
			[[maybe_unused]] CAutoLocker _(g_Git.m_critGitDllSec);
			g_Git.CheckAndInitDll();
			std::vector<GitRevLoglist> tmp;
			const auto callback = static_cast<each_reflog_ent_fn*>([](
				const char*,						// refname
				GIT_OBJECT_OID*,					// old_oid
				// ReSharper disable once CppParameterMayBeConstPtrOrRef
				GIT_OBJECT_OID* new_oid,
				const char*,						// committer
				const std::uint64_t time,
				const int,							// sz
				const char* msg,
				void* data
			) {
				auto& vector = *static_cast<std::vector<GitRevLoglist>*>(data); // a reference!
				GitRevLoglist rev;
				rev.m_CommitHash = CGitHash::FromRaw(new_oid->hash);
				rev.GetCommitterDate() = CTime(win32_cast(time));

				CString one = CUnicodeUtils::GetUnicode<CString>(msg);
				const int message = one.Find(L": ");
				if (message > 0) {
					rev.m_RefAction = one.Left(message);
					rev.GetSubject() = one.Mid(message + 2).TrimRight(L'\n');
				} else
					rev.m_RefAction = one.TrimRight(L'\n');
				vector.push_back(rev);

				return 0;
			});
			// no error checking, because the only error which could occur is file not found
			git_for_each_reflog_ent(CUnicodeUtils::StdGetUTF8(ref).c_str(), callback, &tmp);

			for (size_t i = tmp.size(), id = 0; i > 0; --i, ++id) {
				GitRevLoglist rev = tmp[i - 1];
				rev.m_Ref.Format(L"%s@{%zu}", ref.data(), id);
				refloglist.push_back(rev);
			}
			return 0;
		}

		const auto dotGitDirMaybe = GitAdminDir::GetAdminDirPath(GitCall::m_CurrentDirStorage.wstring());
		if (!dotGitDirMaybe.has_value()) {
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
		if (GitCall::Run({ L"git.exe", L"reflog", L"show", L"--pretty=%H %gD: %gs", L"--date=raw", L"--end-of-options", std::wstring(ref) }, &out, &error))
			return -1;

		int i = 0;
		const CString prefix = ref_ + L"@{";
		int pos = 0;
		while (pos >= 0) {
			CString one = out.Tokenize(L"\n", pos);
			const int refPos = one.Find(L' ');
			if (refPos < 0)
				continue;

			CString str = one.Left(refPos);
			GitRevLoglist rev{};
			rev.m_CommitHash = CGitHash::FromHexStr(str);
			rev.m_Ref.Format(L"%s@{%d}", ref_, i++);
			const int prefixPos = one.Find(prefix, refPos + 1);
			if (prefixPos != refPos + 1)
				continue;

			const int spacePos = one.Find(L' ', prefixPos + prefix.GetLength() + 1);
			if (spacePos < 0)
				continue;

			CString timeStr = one.Mid(prefixPos + prefix.GetLength(), spacePos - prefixPos - prefix.GetLength());
			rev.GetCommitterDate() = CTime(_wtoi(timeStr));
			if (int action = one.Find(L"}: ", spacePos + 1); action > 0) {
				action += 2;
				const int message = one.Find(L": ", action);
				if (message > 0) {
					rev.m_RefAction = one.Mid(action + 1, message - action - 1);
					rev.GetSubject() = one.Right(one.GetLength() - message - 2);
				} else
					rev.m_RefAction = one.Mid(action + 1);
			}

			refloglist.push_back(rev);
		}
		return 0;
	}

};



std::atomic<std::shared_ptr<GitMailmap::CGitMailmap>> GitRevLoglist::s_Mailmap = nullptr;



class CGitHashMap : private std::unordered_map<CGitHash, GitRevLoglist> {
public:
	using std::unordered_map<CGitHash, GitRevLoglist>::begin;
	using std::unordered_map<CGitHash, GitRevLoglist>::end;
	using std::unordered_map<CGitHash, GitRevLoglist>::cbegin;
	using std::unordered_map<CGitHash, GitRevLoglist>::cend;
	using std::unordered_map<CGitHash, GitRevLoglist>::clear;
	using std::unordered_map<CGitHash, GitRevLoglist>::emplace;
	using std::unordered_map<CGitHash, GitRevLoglist>::empty;
	using std::unordered_map<CGitHash, GitRevLoglist>::find;
	using std::unordered_map<CGitHash, GitRevLoglist>::size;
	using std::unordered_map<CGitHash, GitRevLoglist>::operator[];
	using std::unordered_map<CGitHash, GitRevLoglist>::contains;
};



class CThreadSafePtrArray : private std::vector<GitRevLoglist*> {
	CComCriticalSection* m_critSec = nullptr;

public:
	CThreadSafePtrArray(CComCriticalSection* section) :
		m_critSec(section) {
		ATLASSERT(m_critSec);
	}


	GitRevLoglist* SafeGetAt(const size_t i) {
		Locker _(*m_critSec);
		if (i >= size())
			return nullptr;

		return (*this)[i];
	}


	void SafeAdd(GitRevLoglist* newElement) {
		Locker _(*m_critSec);
		push_back(newElement);
	}


	void SafeRemoveAt(const difference_type i) {
		Locker _(*m_critSec);
		if (i >= std::ssize(*this))
			return;

		erase(begin() + i);
	}


	void SafeAddFront(GitRevLoglist* newElement) {
		Locker _(*m_critSec);
		insert(cbegin(), newElement);
	}


	void SafeRemoveAll() {
		Locker _(*m_critSec);
		clear();
	}


	using std::vector<GitRevLoglist*>::begin;
	using std::vector<GitRevLoglist*>::end;
	using std::vector<GitRevLoglist*>::cbegin;
	using std::vector<GitRevLoglist*>::cend;
	using std::vector<GitRevLoglist*>::clear;
	using std::vector<GitRevLoglist*>::empty;
	using std::vector<GitRevLoglist*>::erase;
	using std::vector<GitRevLoglist*>::push_back;
	using std::vector<GitRevLoglist*>::size;
	using std::vector<GitRevLoglist*>::operator[];
};



class CLogCache {
	CString m_GitDir;

	BOOL m_bEnabled = TRUE;

	HANDLE m_IndexFile = INVALID_HANDLE_VALUE;
	HANDLE m_IndexFileMap = nullptr;
	SLogCacheIndexFile* m_pCacheIndex = nullptr;

	std::set<CGitHash> m_shallowAnchors{};

	HANDLE m_DataFile = INVALID_HANDLE_VALUE;
	HANDLE m_DataFileMap = nullptr;
	BYTE* m_pCacheData = nullptr;
	size_t m_DataFileLength = 0;


	void CloseDataHandles() {
		if (m_pCacheData) {
			UnmapViewOfFile(m_pCacheData);
			m_pCacheData = nullptr;
		}
		if (m_DataFileMap) {
			CloseHandle(m_DataFileMap);
			m_DataFileMap = nullptr;
		}
		if (m_DataFile != INVALID_HANDLE_VALUE) {
			CloseHandle(m_DataFile);
			m_DataFile = INVALID_HANDLE_VALUE;
		}
	}


	void CloseIndexHandles() {
		if (m_pCacheIndex) {
			UnmapViewOfFile(m_pCacheIndex);
			m_pCacheIndex = nullptr;
		}

		if (m_IndexFileMap) {
			CloseHandle(m_IndexFileMap);
			m_IndexFileMap = nullptr;
		}

		if (m_IndexFile != INVALID_HANDLE_VALUE) {
			CloseHandle(m_IndexFile);
			m_IndexFile = INVALID_HANDLE_VALUE;
		}
	}


	static BOOL CheckHeader(const SLogCacheIndexHeader* header) {
		if (header->m_Magic != LOG_INDEX_MAGIC)
			return FALSE;

		if (header->m_Version != LOG_INDEX_VERSION)
			return FALSE;

		return TRUE;
	}


	static BOOL CheckHeader(const SLogCacheRevFileHeader* header) {
		if (header->m_Magic != LOG_DATA_FILE_MAGIC)
			return FALSE;

		return TRUE;
	}


	static BOOL CheckHeader(const SLogCacheRevItemHeader* header) {
		if (header->m_Magic != LOG_DATA_ITEM_MAGIC)
			return FALSE;

		return TRUE;
	}


	static BOOL CheckHeader(const SLogCacheDataFileHeader* header) {
		if (header->m_Magic != LOG_DATA_MAGIC)
			return FALSE;

		if (header->m_Version != LOG_INDEX_VERSION)
			return FALSE;

		return TRUE;
	}


	int SaveOneItem(const GitRevLoglist& Rev, const LARGE_INTEGER offset) {
		if (!Rev.m_IsDiffFiles || Rev.m_IsDiffFiles == 2)
			return -1;

		if (!SetFilePointerEx(m_DataFile, offset, nullptr, FILE_BEGIN))
			return -1;

		SLogCacheRevItemHeader header{
			.m_Magic = LOG_DATA_ITEM_MAGIC,
			.m_FileCount = Rev.m_Files.GetCount(),
		};

		DWORD dwWritten = 0;
		if (!WriteFile(this->m_DataFile, &header, sizeof(header), &dwWritten, nullptr))
			return -1;

		for (int i = 0; i < Rev.m_Files.GetCount(); ++i) {
			SLogCacheRevFileHeader revfileheader{};
			revfileheader.m_Magic = LOG_DATA_FILE_MAGIC;
			revfileheader.m_IsSubmodule = Rev.m_Files[i].IsDirectory() ? 1 : 0;
			revfileheader.m_Action = Rev.m_Files[i].m_Action;
			revfileheader.m_ParentNo = Rev.m_Files[i].m_ParentNo;
			CString name = Rev.m_Files[i].GetGitPathString().c_str();
			revfileheader.m_FileNameSize = name.GetLength();
			CString oldname = Rev.m_Files[i].GetGitOldPathString().c_str();
			revfileheader.m_OldFileNameSize = oldname.GetLength();

			CString stat = Rev.m_Files[i].m_StatAdd.c_str();
			revfileheader.m_Add = (stat == L"-") ? 0xFFFFFFFF : _wtol(stat);
			stat = Rev.m_Files[i].m_StatDel.c_str();
			revfileheader.m_Del = (stat == L"-") ? 0xFFFFFFFF : _wtol(stat);

			if (!WriteFile(this->m_DataFile, &revfileheader, sizeof(revfileheader) - sizeof(wchar_t), &dwWritten, nullptr))
				return -1;

			if (!name.IsEmpty()) {
				if (!WriteFile(this->m_DataFile, name, name.GetLength() * sizeof(wchar_t), &dwWritten, nullptr))
					return -1;
			}
			if (!oldname.IsEmpty()) {
				if (!WriteFile(this->m_DataFile, oldname, oldname.GetLength() * sizeof(wchar_t), &dwWritten, nullptr))
					return -1;
			}

		}
		return 0;
	}


	int RebuildCacheFile() {
		constexpr SLogCacheIndexHeader Indexheader{
			.m_Magic = LOG_INDEX_MAGIC,
			.m_Version = LOG_INDEX_VERSION,
			.m_ItemCount = 0,
		};

		constexpr SLogCacheDataFileHeader dataheader{
			.m_Magic = LOG_DATA_MAGIC,
			.m_Version = LOG_INDEX_VERSION,
		};

		constexpr LARGE_INTEGER start{};
		SetFilePointerEx(m_DataFile, start, nullptr, FILE_BEGIN);
		SetFilePointerEx(m_IndexFile, start, nullptr, FILE_BEGIN);

		DWORD dwWritten = 0;
		WriteFile(m_IndexFile, &Indexheader, sizeof(SLogCacheIndexHeader), &dwWritten, nullptr);
		SetEndOfFile(this->m_IndexFile);
		WriteFile(m_DataFile, &dataheader, sizeof(SLogCacheDataFileHeader), &dwWritten, nullptr);
		SetEndOfFile(this->m_DataFile);
		return 0;
	}

public:
	GitLogList::CGitHashMap m_HashMap;


	CLogCache() { m_bEnabled = CRegDWORD(L"Software\\TortoiseGit\\EnableLogCache", TRUE); }


	~CLogCache() {
		CloseIndexHandles();
		CloseDataHandles();
	}


	GitRevLoglist* GetCacheData(const CGitHash& hash) {
		m_HashMap[hash].m_CommitHash = hash;
		return &m_HashMap[hash];
	}


	ULONGLONG GetOffset(const CGitHash& hash, const SLogCacheIndexFile* pData = nullptr) const {
		if (!pData)
			pData = m_pCacheIndex;

		if (!pData)
			return 0;

		const SLogCacheIndexItem* p = reinterpret_cast<SLogCacheIndexItem*>(
			bsearch(hash.ToRaw(), pData->m_Item, pData->m_Header.m_ItemCount, sizeof(SLogCacheIndexItem), Compare)
		);

		if (p)
			return p->m_Offset;
		else
			return 0;
	}


	int FetchCacheIndex(const CString& GitDir) {
		if (!m_bEnabled)
			return 0;

		if (!GitAdminDir::GetWorktreeAdminDirPath(GitDir, m_GitDir))
			return -1;

		if (PathFileExists(m_GitDir + L"\\shallow")) {
			try {
				CString strLine;
				CStdioFile file(m_GitDir + L"\\shallow", CFile::typeText | CFile::modeRead | CFile::shareDenyWrite);
				while (file.ReadString(strLine)) {
					CGitHash hash = CGitHash::FromHexStr(strLine);
					if (hash.IsEmpty())
						continue;

					m_shallowAnchors.insert(hash);
				}
			} catch (CFileException* pE) {
				tgit::DebugOutput::TracePrintf(__FUNCTION__ ": CFileException loading shallow file commits list\n");
				pE->Delete();
			}
		}

		int ret = -1;
		do {
			if (m_IndexFile == INVALID_HANDLE_VALUE) {
				const CString file = m_GitDir + INDEX_FILE_NAME;
				m_IndexFile = CreateFile(file,
				                         GENERIC_READ,
				                         FILE_SHARE_READ | FILE_SHARE_DELETE,
				                         nullptr,
				                         OPEN_EXISTING,
				                         FILE_ATTRIBUTE_NORMAL,
				                         nullptr);

				if (m_IndexFile == INVALID_HANDLE_VALUE)
					break;
			}

			if (!m_IndexFileMap) {
				m_IndexFileMap = CreateFileMapping(m_IndexFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
				if (!m_IndexFileMap)
					break;
			}

			if (!m_pCacheIndex) {
				m_pCacheIndex = static_cast<SLogCacheIndexFile*>(MapViewOfFile(m_IndexFileMap, FILE_MAP_READ, 0, 0, 0));
				if (!m_pCacheIndex)
					break;
			}

			LARGE_INTEGER indexFileSize;
			if (!GetFileSizeEx(m_IndexFile, &indexFileSize))
				break;
			if (indexFileSize.QuadPart >= SIZE_T_MAX) {
				CloseIndexHandles();
				CloseDataHandles();
				return -1;
			}
			if (indexFileSize.QuadPart < sizeof(SLogCacheIndexHeader))
				break;

			if (!CheckHeader(&m_pCacheIndex->m_Header))
				break;

			if (size_t len; SizeTMult(sizeof(SLogCacheIndexItem), m_pCacheIndex->m_Header.m_ItemCount, &len) != S_OK ||
				SizeTAdd(len, sizeof(SLogCacheIndexHeader), &len) != S_OK || static_cast<size_t>(indexFileSize.QuadPart) != len)
				break;

			if (m_DataFile == INVALID_HANDLE_VALUE) {
				const CString file = m_GitDir + DATA_FILE_NAME;
				m_DataFile = CreateFile(file,
				                        GENERIC_READ,
				                        FILE_SHARE_READ | FILE_SHARE_DELETE,
				                        nullptr,
				                        OPEN_EXISTING,
				                        FILE_ATTRIBUTE_NORMAL,
				                        nullptr);

				if (m_DataFile == INVALID_HANDLE_VALUE)
					break;
			}

			if (!m_DataFileMap) {
				m_DataFileMap = CreateFileMapping(m_DataFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
				if (!m_DataFileMap)
					break;
			}
			if (LARGE_INTEGER fileLength; !GetFileSizeEx(m_DataFile, &fileLength) || fileLength.QuadPart < sizeof(SLogCacheDataFileHeader))
				break;
			else if (fileLength.QuadPart >= SIZE_T_MAX) {
				CloseIndexHandles();
				CloseDataHandles();
				return -1;
			} else
				m_DataFileLength = static_cast<size_t>(fileLength.QuadPart);

			if (!m_pCacheData) {
				m_pCacheData = static_cast<BYTE*>(MapViewOfFile(m_DataFileMap, FILE_MAP_READ, 0, 0, 0));
				if (!m_pCacheData)
					break;
			}

			if (!CheckHeader(reinterpret_cast<SLogCacheDataFileHeader*>(m_pCacheData)))
				break;

			if (size_t length; SizeTMult(m_pCacheIndex->m_Header.m_ItemCount, sizeof(SLogCacheRevItemHeader), &length) != S_OK || SizeTAdd(
				sizeof(SLogCacheDataFileHeader),
				length,
				&length) != S_OK || m_DataFileLength < length)
				break;

			ret = 0;
		}
		while (false);

		if (ret) {
			CloseIndexHandles();
			CloseDataHandles();
			::DeleteFile(m_GitDir + INDEX_FILE_NAME);
			::DeleteFile(m_GitDir + DATA_FILE_NAME);
		}
		return ret;
	}


	int LoadOneItem(GitRevLoglist& Rev, const ULONGLONG ullOffset) const {
		if (!m_pCacheData)
			return -1;

		if (ullOffset >= m_DataFileLength || ullOffset < sizeof(SLogCacheDataFileHeader))
			return -2;

		auto offset = ullOffset;
		SLogCacheRevItemHeader* header = reinterpret_cast<SLogCacheRevItemHeader*>(m_pCacheData + offset);
		if (SizeTAdd(offset, sizeof(SLogCacheRevItemHeader), &offset) != S_OK || offset > m_DataFileLength)
			return -2;

		if (!CheckHeader(header))
			return -2;

		Rev.m_Action = 0;

		for (const auto _: std::views::iota(header->m_FileCount)) {
			const auto fileheader = reinterpret_cast<SLogCacheRevFileHeader*>(m_pCacheData + offset);
			if (SizeTAdd(offset, sizeof(SLogCacheRevFileHeader) - sizeof(wchar_t), &offset) != S_OK || offset >= m_DataFileLength) {
				Rev.m_Action = 0;
				Rev.m_Files.Clear();
				return -2;
			}

			if (!CheckHeader(fileheader) || !fileheader->m_FileNameSize || fileheader->m_FileNameSize >= INT_MAX || fileheader->m_OldFileNameSize >= INT_MAX) {
				Rev.m_Action = 0;
				Rev.m_Files.Clear();
				return -2;
			}

			if (size_t recordLength; SizeTAdd(fileheader->m_FileNameSize, fileheader->m_OldFileNameSize, &recordLength) != S_OK
				|| SizeTMult(recordLength, sizeof(wchar_t), &recordLength) != S_OK
				|| SizeTAdd(offset, recordLength, &offset) != S_OK
				|| offset > m_DataFileLength) {
				Rev.m_Action = 0;
				Rev.m_Files.Clear();
				return -2;
			}

			CString oldfile;
			CString file(fileheader->m_FileName, fileheader->m_FileNameSize);
			if (fileheader->m_OldFileNameSize)
				oldfile = CString(fileheader->m_FileName + fileheader->m_FileNameSize, fileheader->m_OldFileNameSize);
			CTGitPath path;
			// an empty old path is "no rename", as the nullptr it used to be passed as
			path.SetFromGit(tgit::str::StringView(file), tgit::str::StringView(oldfile), fileheader->m_IsSubmodule != 0);

			path.m_ParentNo = fileheader->m_ParentNo;
			path.m_Action = fileheader->m_Action & ~(CTGitPath::LOGACTIONS_HIDE | CTGitPath::LOGACTIONS_GRAY);
			Rev.m_Action |= path.m_Action;

			if (fileheader->m_Add == 0xFFFFFFFF)
				path.m_StatAdd = L"-";
			else
				path.m_StatAdd = std::format(L"{}", fileheader->m_Add);

			if (fileheader->m_Del == 0xFFFFFFFF)
				path.m_StatDel = L"-";
			else
				path.m_StatDel = std::format(L"{}", fileheader->m_Del);

			Rev.m_Files.AddPath(path);
		}
		return 0;
	}


	int SaveCache() {
		if (!m_bEnabled)
			return 0;

		BOOL bIsRebuild = false;

		if (m_HashMap.empty() || (m_HashMap.size() == 1 && m_HashMap.contains({})))
			return 0;

		if (this->m_GitDir.IsEmpty())
			return 0;

		SLogCacheIndexFile* pIndex = nullptr;
		if (m_pCacheIndex && m_pCacheIndex->m_Header.m_ItemCount > 0) {
			size_t len;
			if (SizeTMult(sizeof(SLogCacheIndexItem), m_pCacheIndex->m_Header.m_ItemCount - 1, &len) != S_OK || SizeTAdd(len, sizeof(SLogCacheIndexFile), &len)
				!= S_OK)
				return -1;
			pIndex = static_cast<SLogCacheIndexFile*>(malloc(len));
			if (!pIndex)
				return -1;

			memcpy(pIndex, m_pCacheIndex, len);
		}

		this->CloseDataHandles();
		this->CloseIndexHandles();

		SLogCacheIndexHeader header{};
		CString file = this->m_GitDir + INDEX_FILE_NAME;
		int ret = -1;
		do {
			m_IndexFile = CreateFile(file,
			                         GENERIC_READ | GENERIC_WRITE,
			                         0,
			                         nullptr,
			                         OPEN_ALWAYS,
			                         FILE_ATTRIBUTE_NORMAL,
			                         nullptr);

			if (m_IndexFile == INVALID_HANDLE_VALUE)
				break;

			file = m_GitDir + DATA_FILE_NAME;

			m_DataFile = CreateFile(file,
			                        GENERIC_READ | GENERIC_WRITE,
			                        0,
			                        nullptr,
			                        OPEN_ALWAYS,
			                        FILE_ATTRIBUTE_NORMAL,
			                        nullptr);

			if (m_DataFile == INVALID_HANDLE_VALUE)
				break;

			{
				memset(&header, 0, sizeof(SLogCacheIndexHeader));
				DWORD num = 0;
				if ((!ReadFile(m_IndexFile, &header, sizeof(SLogCacheIndexHeader), &num, nullptr)) || num != sizeof(SLogCacheIndexHeader) ||
					!CheckHeader(&header)
				) {
					RebuildCacheFile();
					bIsRebuild = true;
				}
			}
			if (!bIsRebuild) {
				SLogCacheDataFileHeader datafileheader{};
				DWORD num = 0;
				if ((!ReadFile(m_DataFile, &datafileheader, sizeof(SLogCacheDataFileHeader), &num, nullptr) || num != sizeof(SLogCacheDataFileHeader) ||
					!CheckHeader(&datafileheader))) {
					RebuildCacheFile();
					bIsRebuild = true;
				}
			}

			if (bIsRebuild)
				header.m_ItemCount = 0;

			{
				constexpr LARGE_INTEGER start{};
				SetFilePointerEx(m_DataFile, start, nullptr, FILE_END);
				SetFilePointerEx(m_IndexFile, start, nullptr, FILE_END);
			}

			const bool isShallow = !m_shallowAnchors.empty();
			for (auto& val : m_HashMap | std::views::values) {
				if (!bIsRebuild && GetOffset(val.m_CommitHash, pIndex) != 0)
					continue;

				if (!val.m_IsDiffFiles || val.m_IsDiffFiles == 2 || val.m_CommitHash.IsEmpty() || (isShallow && m_shallowAnchors.
					contains(val.m_CommitHash)))
					continue;

				LARGE_INTEGER offset{};
				constexpr LARGE_INTEGER start{};
				SetFilePointerEx(m_DataFile, start, &offset, FILE_CURRENT);
				if (SaveOneItem(val, offset)) {
					tgit::DebugOutput::TracePrintf(__FUNCTION__ ": Save one item error\n");
					if (!SetFilePointerEx(m_DataFile, offset, &offset, FILE_BEGIN))
						break;
					continue;
				}

				SLogCacheIndexItem item;
				item.m_Hash = val.m_CommitHash;
				item.m_Offset = offset.QuadPart;

				DWORD dwWritten = 0;
				if (!WriteFile(m_IndexFile, &item, sizeof(SLogCacheIndexItem), &dwWritten, nullptr))
					break;
				++header.m_ItemCount;
			}
			FlushFileBuffers(m_IndexFile);

			m_IndexFileMap = CreateFileMapping(m_IndexFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
			if (!m_IndexFileMap)
				break;

			m_pCacheIndex = static_cast<SLogCacheIndexFile*>(MapViewOfFile(m_IndexFileMap, FILE_MAP_WRITE, 0, 0, 0));
			if (!m_pCacheIndex)
				break;

			m_pCacheIndex->m_Header.m_ItemCount = header.m_ItemCount;

			std::qsort(m_pCacheIndex->m_Item, m_pCacheIndex->m_Header.m_ItemCount, sizeof(SLogCacheIndexItem), Compare);

			FlushViewOfFile(m_pCacheIndex, 0);
			ret = 0;
		}
		while (false);

		this->CloseDataHandles();
		this->CloseIndexHandles();
		if (ret) {
			::DeleteFile(m_GitDir + INDEX_FILE_NAME);
			::DeleteFile(m_GitDir + DATA_FILE_NAME);
		}

		free(pIndex);
		return ret;
	}


	int ClearAllParent() {
		for (auto& [_, val] : this->m_HashMap) {
			val.m_ParentHash.clear();
			val.m_Lanes.clear();
		}
		return 0;
	}


	void ClearAllLanes() {
		for (auto& val : this->m_HashMap | std::views::values)
			val.m_Lanes.clear();
	}
};

}
// NOLINTEND(*-throw-by-value-catch-by-reference)

export using namespace GitLogList;
