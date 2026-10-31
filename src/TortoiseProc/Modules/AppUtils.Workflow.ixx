module;
#include "stdafx.h"
#include "std.h"

#include "TortoiseProc.h"
#include "ExportDlg.h"
#include "ProgressDlg.h"
#include "CreateBranchTagDlg.h"
#include "CreateWorktreeDlg.h"
#include "GitSwitchDlg.h"
#include "ResetDlg.h"
#include "DeleteConflictDlg.h"
#include "GitProgressDlg.h"
#include "PushDlg.h"
#include "CommitDlg.h"
#include "MergeDlg.h"
#include "MergeAbortDlg.h"
#include "Hooks.h"
#include "Settings/Settings.h"
#include "InputDlg.h"
#include "SVNDCommitDlg.h"
#include "requestpulldlg.h"
#include "PullFetchDlg.h"
#include "RebaseDlg.h"
#include "StashSave.h"
#include "IgnoreDlg.h"
#include "BisectStartDlg.h"
#include "SysProgressDlg.h"
#include "../../libgit2/Resources/Globals.h"
#include "ProgressCommands/ResetProgressCommand.h"
#include "ProgressCommands/FetchProgressCommand.h"
#include "SubmoduleResolveConflictDlg.h"
#include "GitDiff.h"


export module AppUtils.Workflow;
import AppUtils.Launch;
import GitRevLoglist;
import MessageBox;

/**
 * \ingroup TortoiseProc
 * Git operations a user drives through a dialog: the dialog collects the options, a
 * progress dialog runs git, and its post-command buttons offer the next operation.
 * Those buttons make the operations call each other (pull offers push, push offers
 * pull, merge offers stash, ...), which is why they share one module: a cycle cannot
 * cross a module boundary.
 */
export namespace CAppUtils {

bool UpdateBranchDescription(const CString& branch, CString description)
{
	if (branch.IsEmpty())
		return false;

	CString key;
	key.Format(L"branch.%s.description", static_cast<LPCWSTR>(branch));
	description.Remove(L'\r');
	description.Trim();
	if (description.IsEmpty())
		g_Git.UnsetConfigValue(key);
	else
		g_Git.SetConfigValue(key, description);

	return true;
}


bool CheckUserData(HWND hWnd)
{
	while (g_Git.GetUserNameW().IsEmpty() || g_Git.GetUserEmail().IsEmpty() || g_Git.GetCommitterName().IsEmpty() || g_Git.GetCommitterEmail().IsEmpty())
	{
		if (CMessageBox::Show(hWnd, IDS_PROC_NOUSERDATA, IDS_APPNAME, MB_YESNO | MB_ICONERROR) == IDYES)
		{
			CTGitPath path{ GitCall::m_CurrentDirStorage.native() };
			CSettings dlg(IDS_PROC_SETTINGS_TITLE, &path, CWnd::FromHandle(hWnd));
			dlg.SetTreeViewMode(TRUE, TRUE, TRUE);
			dlg.SetTreeWidth(220);
			dlg.m_DefaultPage = L"gitconfig";

			dlg.DoModal();
			dlg.HandleRestart();

		}
		else
			return false;
	}

	return true;
}

int SaveCommitUnicodeFile(const CString& filename, CString &message)
{
	try
	{
		CFile file(filename, CFile::modeReadWrite | CFile::modeCreate);
		const int cp = CUnicodeUtils::StdGetCPCode(tgit::str::StringView(g_Git.GetConfigValue(L"i18n.commitencoding")));

		const bool stripComments = (CRegDWORD(L"Software\\TortoiseGit\\StripCommentedLines", FALSE) == TRUE);
		CString commentCharValue;
		if (stripComments)
		{
			commentCharValue = g_Git.GetConfigValue(L"core.commentchar");
			if (commentCharValue.IsEmpty())
				commentCharValue = L"#";
		}

		bool sanitize = (CRegDWORD(L"Software\\TortoiseGit\\SanitizeCommitMsg", TRUE) == TRUE);
		if (sanitize)
			message.Trim(L" \r\n");

		const int len = message.GetLength();
		int start = 0;
		int emptyLineCnt = 0;
		while (start >= 0 && start < len)
		{
			int oldStart = start;
			start = message.Find(L'\n', oldStart);
			CString line = message.Mid(oldStart);
			if (start != -1)
			{
				line.Truncate(start - oldStart);
				++start; // move forward so we don't find the same char again
			}
			if (stripComments && (!line.IsEmpty() && StringUtils::StartsWith(line, commentCharValue)) || (start < 0 && line.IsEmpty()))
				continue;
			line.TrimRight(L" \r");
			if (sanitize)
			{
				if (line.IsEmpty())
				{
					++emptyLineCnt;
					continue;
				}
				if (emptyLineCnt) // squash multiple newlines
					file.Write("\n", 1);
				emptyLineCnt = 0;
			}
			CStringA lineA = CUnicodeUtils::GetMulti<CStringA>(line + L'\n', cp);
			file.Write(static_cast<LPCSTR>(lineA), lineA.GetLength());
		}
		file.Close();
		return 0;
	} catch (CFileException& e) {
		tgit::DebugOutput::Trace(L"[SetupDiffScripts] file issues {}", e.m_cause);
		e.Delete();
		return -1;
	}
}

bool MessageContainsConflictHints(HWND hWnd, const CString& message)
{
	bool stripComments = (CRegDWORD(L"Software\\TortoiseGit\\StripCommentedLines", FALSE) == TRUE);
	if (stripComments)
		return false;
	CString cleanupMode = g_Git.GetConfigValue(L"core.cleanup", L"default");
	if (cleanupMode == L"verbatim" || cleanupMode == L"whitespace" || cleanupMode == L"scissors")
		return false;

	CString commentCharValue = g_Git.GetConfigValue(L"core.commentchar");
	if (commentCharValue.IsEmpty())
		commentCharValue = L"#";

	CString conflictsHint;
	conflictsHint.Format(L"\n%s Conflicts:\n%s\t", static_cast<LPCWSTR>(commentCharValue), static_cast<LPCWSTR>(commentCharValue));

	if (message.Find(conflictsHint) <= 0)
		return false;

	BOOL dontaskagainchecked = FALSE;
	if (CMessageBox::ShowCheck(hWnd, IDS_CONFLICT_HINT_IN_COMMIT_MESSAGE, IDS_APPNAME, 2, IDI_QUESTION, IDS_IGNOREBUTTON, IDS_ABORTBUTTON, 0, L"CommitMessageContainsConflictHint", IDS_MSGBOX_DONOTSHOWAGAIN, &dontaskagainchecked) == 2)
	{
		if (dontaskagainchecked)
			CMessageBox::RemoveRegistryKey(L"CommitMessageContainsConflictHint");
		return true;
	}
	return false;
}

bool IsTGitRebaseActive(HWND hWnd)
{
	const auto adminDirMaybe = GitAdminDir::GetAdminDirPath(GitCall::m_CurrentDirStorage.native());
	if (!adminDirMaybe)
		return false;
	const CString adminDir{ adminDirMaybe->path.c_str() };

	if (!PathIsDirectory(adminDir + L"tgitrebase.active"))
		return false;

	if (CMessageBox::Show(hWnd, IDS_REBASELOCKFILEFOUND, IDS_APPNAME, 2, IDI_EXCLAMATION, IDS_REMOVESTALEBUTTON, IDS_ABORTBUTTON) == 2)
		return true;

	RemoveDirectory(adminDir + L"tgitrebase.active");

	return false;
}

bool LaunchRemoteSetting()
{
	CTGitPath path{ GitCall::m_CurrentDirStorage.native() };
	CSettings dlg(IDS_PROC_SETTINGS_TITLE, &path);
	dlg.SetTreeViewMode(TRUE, TRUE, TRUE);
	dlg.SetTreeWidth(220);
	dlg.m_DefaultPage = L"gitremote";

	dlg.DoModal();
	dlg.HandleRestart();
	return true;
}

void EditNote(HWND hWnd, GitRevLoglist* rev, ProjectProperties* projectProperties)
{
	if (!CheckUserData(hWnd))
		return;

	if (g_Git.GetGitNotes(rev->m_CommitHash, rev->m_Notes))
		MessageBox(hWnd, TGitH::GetLibGit2LastErr(CString((L"Could not load notes for commit " + rev->m_CommitHash.ToString() + L'.').c_str())), L"TortoiseGit", MB_OK | MB_ICONERROR);

	CInputDlg dlg(GetParentCWnd(hWnd));
	dlg.m_sHintText = CString(MAKEINTRESOURCE(IDS_PROGS_TITLE_EDITNOTES));
	dlg.m_sInputText = rev->m_Notes;
	dlg.m_sTitle = CString(MAKEINTRESOURCE(IDS_PROGS_TITLE_EDITNOTES));
	dlg.m_pProjectProperties = projectProperties;
	dlg.m_bUseLogWidth = true;
	if(dlg.DoModal() == IDOK)
	{
		if (g_Git.SetGitNotes(rev->m_CommitHash, dlg.m_sInputText))
		{
			CString err;
			err.LoadString(IDS_PROC_FAILEDSAVINGNOTES);
			MessageBox(hWnd, TGitH::GetLibGit2LastErr(err), L"TortoiseGit", MB_OK | MB_ICONERROR);
			return;
		}

		if (g_Git.GetGitNotes(rev->m_CommitHash, rev->m_Notes))
			MessageBox(hWnd, TGitH::GetLibGit2LastErr(CString((L"Could not load notes for commit " + rev->m_CommitHash.ToString() + L'.').c_str())), L"TortoiseGit", MB_OK | MB_ICONERROR);
	}
}

bool DeleteRef(CWnd* parent, const CString& ref)
{
	const auto safe_h_wnd = CWnd::GetSafeOwner()->GetSafeHwnd();
	CString shortname;
	if (TGitH::GetShortName(ref, shortname, L"refs/remotes/"))
	{
		CString msg;
		msg.Format(IDS_PROC_DELETEREMOTEBRANCH, static_cast<LPCWSTR>(ref));
		[[maybe_unused]]auto&& result = CMessageBox::Show(safe_h_wnd, msg, L"TortoiseGit", 3, IDI_QUESTION, CString(MAKEINTRESOURCE(IDS_PROC_DELETEREMOTEBRANCH_LOCALREMOTE)), CString(MAKEINTRESOURCE(IDS_PROC_DELETEREMOTEBRANCH_LOCAL)), CString(MAKEINTRESOURCE(IDS_ABORTBUTTON)));
		if (result == 1)
		{
			CString remoteName = shortname.Left(shortname.Find(L'/'));
			shortname = shortname.Mid(shortname.Find(L'/') + 1);
			CSysProgressDlg sysProgressDlg;
			sysProgressDlg.SetTitle(CString(MAKEINTRESOURCE(IDS_APPNAME)));
			sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_DELETING_REMOTE_REFS)));
			sysProgressDlg.SetLine(2, CString(MAKEINTRESOURCE(IDS_PROGRESSWAIT)));
			sysProgressDlg.SetShowProgressBar(false);
			sysProgressDlg.ShowModal(parent, true);
			std::vector<std::wstring> list;
			list.push_back(std::format(L"refs/heads/{}", shortname));
			if (g_Git.DeleteRemoteRefs(remoteName, list))
				CMessageBox::Show(safe_h_wnd, g_Git.GetGitLastErr(L"Could not delete remote ref.", TGitH::GIT_CMD_PUSH), L"TortoiseGit", MB_OK | MB_ICONERROR);
			sysProgressDlg.Stop();
			return true;
		}
		else if (result == 2)
		{
			if (g_Git.DeleteRef(ref))
			{
				CMessageBox::Show(safe_h_wnd, g_Git.GetGitLastErr(L"Could not delete reference.", TGitH::GIT_CMD_DELETETAGBRANCH), L"TortoiseGit", MB_OK | MB_ICONERROR);
				return false;
			}
			return true;
		}
		return false;
	}
	else if (TGitH::GetShortName(ref, shortname, L"refs/stash"))
	{
		CString err;
		std::vector<GitRevLoglist> stashList;
		const size_t count = !GitRevLoglist::GetRefLog(ref, stashList, err) ? stashList.size() : 0;
		CString msg;
		msg.Format(IDS_PROC_DELETEALLSTASH, count);
		const auto&& choose = CMessageBox::Show(safe_h_wnd, msg, L"TortoiseGit", 3, IDI_QUESTION, CString(MAKEINTRESOURCE(IDS_DELETEBUTTON)), CString(MAKEINTRESOURCE(IDS_DROPONESTASH)), CString(MAKEINTRESOURCE(IDS_ABORTBUTTON)));
		if (choose == 1)
		{
			CString out;
			if (GitCall::Run({ L"git.exe", L"stash", L"clear" }, &out, CP_UTF8)) // stderr merged into out, as upstream's Run(cmd, &out, code)
				CMessageBox::Show(safe_h_wnd, out, L"TortoiseGit", MB_OK | MB_ICONERROR);
			return true;
		}
		else if (choose == 2)
		{
			CString out;
			if (GitCall::Run({ L"git.exe", L"stash", L"drop", L"refs/stash@{0}" }, &out, CP_UTF8))
				CMessageBox::Show(safe_h_wnd, out, L"TortoiseGit", MB_OK | MB_ICONERROR);
			return true;
		}
		return false;
	}

	CString msg;
	msg.Format(IDS_PROC_DELETEBRANCHTAG, static_cast<LPCWSTR>(ref));
	// Check if branch is fully merged in HEAD
	if (TGitH::GetShortName(ref, shortname, L"refs/heads/") && !g_Git.IsFastForward(ref, L"HEAD"))
	{
		msg += L"\n\n";
		msg += CString(MAKEINTRESOURCE(IDS_PROC_BROWSEREFS_WARNINGUNMERGED));
	}
	if (CMessageBox::Show(safe_h_wnd, msg, L"TortoiseGit", 2, IDI_QUESTION, CString(MAKEINTRESOURCE(IDS_DELETEBUTTON)), CString(MAKEINTRESOURCE(IDS_ABORTBUTTON))) == 1)
	{
		if (g_Git.DeleteRef(ref))
		{
			CMessageBox::Show(safe_h_wnd, g_Git.GetGitLastErr(L"Could not delete reference.", TGitH::GIT_CMD_DELETETAGBRANCH), L"TortoiseGit", MB_OK | MB_ICONERROR);
			return false;
		}
		return true;
	}
	return false;
}

bool Export(const HWND& hWnd, std::wstring&& BashHash, const CTGitPath* orgPath)
{
	// ask from where the export has to be done
	CExportDlg dlg(GetParentCWnd(hWnd));
	if(!BashHash.empty())
		dlg.m_initialRefName = BashHash;
	if (orgPath)
	{
		if (PathIsRelative(orgPath->GetWinPath()))
			dlg.m_orgPath = CTGitPath(g_Git.CombinePath(*orgPath).native());
		else
			dlg.m_orgPath = *orgPath;
	}

	if (dlg.DoModal() == IDOK)
	{
		CProgressDlg pro(GetParentCWnd(hWnd));
		if (GetExplorerHWND() == hWnd)
			theApp.m_pMainWnd = &pro;
		pro.m_GitCmd = { L"git.exe", L"archive", std::format(L"--output={}", dlg.m_strFile), L"--verbose", L"--end-of-options", std::wstring(g_Git.FixBranchName(dlg.m_VersionName)) };
		pro.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
		{
			if (status)
				return;
			postCmdList.emplace_back(IDI_EXPLORER, IDS_STATUSLIST_CONTEXT_EXPLORE, [&]{ ExploreTo(hWnd, dlg.m_strFile); });
		};

		CGit git;
		git.m_IsUseGitDLL = false;
		if (!dlg.m_bWholeProject && !dlg.m_orgPath.IsEmpty() && PathIsDirectory(dlg.m_orgPath.GetWinPathString().c_str()))
		{
			git.SetCurrentDirExact(dlg.m_orgPath.GetWinPathString().c_str());
			pro.m_Git = &git;
		}
		return (pro.DoModal() == IDOK);
	}
	return false;
}

bool CreateWorktree(HWND hWnd, const CString& target = {})
{
	CCreateWorktreeDlg dlg(GetParentCWnd(hWnd));
	if (!target.IsEmpty())
		dlg.m_sWorktreePath = target;
	if (dlg.DoModal() != IDOK)
		return FALSE;

	std::vector<std::wstring> cmd{ L"git.exe", L"worktree", L"add" };
	if (!dlg.m_bCheckout)
		cmd.emplace_back(L"--no-checkout"); // git defaults to --checkout
	if (dlg.m_bForce)
		cmd.emplace_back(L"--force");
	if (dlg.m_bDetach)
		cmd.emplace_back(L"--detach");
	else if (dlg.m_bBranch)
		cmd.insert(cmd.cend(), { L"-b", std::wstring(dlg.m_sNewBranch) });
	if (dlg.m_VersionName == L"HEAD")
		dlg.m_VersionName.Empty();

	cmd.insert(cmd.cend(), { L"--", std::wstring(dlg.m_sWorktreePath) });
	// The version used to interpolate as nothing when empty; as an element it
	// would be a real empty argument.
	if (!dlg.m_VersionName.IsEmpty())
		cmd.emplace_back(std::wstring(g_Git.FixBranchName(dlg.m_VersionName)));

	CProgressDlg progress(GetParentCWnd(hWnd));
	progress.m_GitCmd = cmd;
	progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList) {
		if (status)
			return;

		if (CTGitPath path{ dlg.m_sWorktreePath.GetString() }; path.HasSubmodules())
		{
			postCmdList.emplace_back(IDI_UPDATE, IDS_PROC_SUBMODULESUPDATE, [&] {
				CString sCmd;
				sCmd.Format(L"/command:subupdate /bkpath:%s", TCmdLineParser::EscapeValue(dlg.m_sWorktreePath).c_str());
				RunTortoiseGitProc(sCmd);
			});
		}
	};

	return progress.DoModal() == IDOK;
}

bool BisectStart(HWND hWnd, const CString& lastGood, const CString& firstBad)
{
	if (!g_Git.CheckCleanWorkTree())
	{
		if (CMessageBox::Show(hWnd, IDS_ERROR_NOCLEAN_STASH, IDS_APPNAME, 1, IDI_QUESTION, IDS_STASHBUTTON, IDS_ABORTBUTTON) == 1)
		{
			CSysProgressDlg sysProgressDlg;
			sysProgressDlg.SetTitle(CString(MAKEINTRESOURCE(IDS_APPNAME)));
			sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_PROC_STASHRUNNING)));
			sysProgressDlg.SetLine(2, CString(MAKEINTRESOURCE(IDS_PROGRESSWAIT)));
			sysProgressDlg.SetShowProgressBar(false);
			sysProgressDlg.SetCancelMsg(IDS_PROGRS_INFOFAILED);
			sysProgressDlg.ShowModeless(hWnd, true);

			CString out;
			if (GitCall::Run({ L"git.exe", L"stash" }, &out, CP_UTF8))
			{
				sysProgressDlg.Stop();
				MessageBox(hWnd, out, L"TortoiseGit", MB_OK | MB_ICONERROR);
				return false;
			}
			sysProgressDlg.Stop();
		}
		else
			return false;
	}

	CBisectStartDlg bisectStartDlg(GetParentCWnd(hWnd));

	if (!lastGood.IsEmpty())
		bisectStartDlg.m_sLastGood = lastGood;
	if (!firstBad.IsEmpty())
		bisectStartDlg.m_sFirstBad = firstBad;

	if (bisectStartDlg.DoModal() == IDOK)
	{
		CProgressDlg progress(GetParentCWnd(hWnd));
		if (GetExplorerHWND() == hWnd)
			theApp.m_pMainWnd = &progress;
		progress.m_GitCmdList.push_back({ L"git.exe", L"bisect", L"start" });
		progress.m_GitCmdList.push_back({ L"git.exe", L"bisect", L"good", std::wstring(bisectStartDlg.m_LastGoodRevision) });
		progress.m_GitCmdList.push_back({ L"git.exe", L"bisect", L"bad", std::wstring(bisectStartDlg.m_FirstBadRevision) });

		progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
		{
			if (status)
				return;

			CTGitPath path{ GitCall::m_CurrentDirStorage.native() };
			if (path.HasSubmodules())
			{
				postCmdList.emplace_back(IDI_UPDATE, IDS_PROC_SUBMODULESUPDATE, []
				{
					CString sCmd;
					sCmd.Format(L"/command:subupdate /bkpath:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
					RunTortoiseGitProc(sCmd);
				});
			}

			postCmdList.emplace_back(IDI_THUMB_UP, IDS_MENUBISECTGOOD, [] { RunTortoiseGitProc(L"/command:bisect /good"); });
			postCmdList.emplace_back(IDI_THUMB_DOWN, IDS_MENUBISECTBAD, [] { RunTortoiseGitProc(L"/command:bisect /bad"); });
			postCmdList.emplace_back(IDI_BISECT, IDS_MENUBISECTSKIP, [] { RunTortoiseGitProc(L"/command:bisect /skip"); });
			postCmdList.emplace_back(IDI_BISECT_RESET, IDS_MENUBISECTRESET, [] { RunTortoiseGitProc(L"/command:bisect /reset"); });
		};

		INT_PTR ret = progress.DoModal();
		return ret == IDOK;
	}

	return false;
}

bool BisectOperation(HWND hWnd, const CString& op, const CString& ref = L"")
{
	std::vector<std::wstring> cmd{ L"git.exe", L"bisect", std::wstring(op) };
	if (!ref.IsEmpty())
		cmd.emplace_back(std::wstring(ref));

	CProgressDlg progress(GetParentCWnd(hWnd));
	if (GetExplorerHWND() == hWnd)
		theApp.m_pMainWnd = &progress;
	progress.m_GitCmd = cmd;

	progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
	{
		CTGitPath path{ GitCall::m_CurrentDirStorage.native() };
		if (status)
		{
			if (path.IsBisectActive())
				postCmdList.emplace_back(IDI_BISECT_RESET, IDS_MENUBISECTRESET, [] { RunTortoiseGitProc(L"/command:bisect /reset"); });
			return;
		}

		if (path.HasSubmodules())
		{
			postCmdList.emplace_back(IDI_UPDATE, IDS_PROC_SUBMODULESUPDATE, []
			{
				CString sCmd;
				sCmd.Format(L"/command:subupdate /bkpath:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
				RunTortoiseGitProc(sCmd);
			});
		}

		if (!path.IsBisectActive())
			return;
		postCmdList.emplace_back(IDI_THUMB_UP, IDS_MENUBISECTGOOD, [] { RunTortoiseGitProc(L"/command:bisect /good"); });
		postCmdList.emplace_back(IDI_THUMB_DOWN, IDS_MENUBISECTBAD, [] { RunTortoiseGitProc(L"/command:bisect /bad"); });
		postCmdList.emplace_back(IDI_BISECT, IDS_MENUBISECTSKIP, [] { RunTortoiseGitProc(L"/command:bisect /skip"); });
		postCmdList.emplace_back(IDI_BISECT_RESET, IDS_MENUBISECTRESET, [] { RunTortoiseGitProc(L"/command:bisect /reset"); });
	};

	return progress.DoModal() == IDOK;
}

class CIgnoreFile : public CStdioFile
{
public:
	std::vector<std::wstring> m_Items;
	std::string m_eol{ "\n" };

	BOOL ReadString(CString& rString) override
	{
		if (GetPosition() == 0)
		{
			constexpr unsigned char utf8bom[] = { 0xEF, 0xBB, 0xBF };
			char buf[3] = { 0, 0, 0 };
			Read(buf, 3);
			if (memcpy(buf, utf8bom, sizeof(utf8bom)))
				SeekToBegin();
		}

		CStringA strA;
		char lastChar = '\0';
		for (char c = '\0'; Read(&c, 1) == 1; lastChar = c)
		{
			if (c == '\r')
				continue;
			if (c == '\n')
			{
				m_eol = lastChar == L'\r' ? "\r\n" : "\n";
				break;
			}
			strA.AppendChar(c);
		}
		if (strA.IsEmpty())
			return FALSE;

		rString = CUnicodeUtils::GetUnicode<CString>(strA);
		return TRUE;
	}

	void ResetState()
	{
		m_Items.clear();
		m_eol = { "\n" };
	}
};

bool OpenIgnoreFile(HWND hWnd, CIgnoreFile &file, const CString& filename)
{
	file.ResetState();
	if (!file.Open(filename, CFile::modeCreate | CFile::modeReadWrite | CFile::modeNoTruncate | CFile::typeBinary))
	{
		MessageBox(hWnd, filename + L" Open Failure", L"TortoiseGit", MB_OK | MB_ICONERROR);
		return false;
	}

	if (file.GetLength() > 0)
	{
		CString fileText;
		while (file.ReadString(fileText))
			file.m_Items.push_back(std::wstring(fileText));
		file.Seek(gsl::narrow<long long>(file.GetLength()) - 1, 0);
		char lastchar[1] = {};
		file.Read(lastchar, 1);
		file.SeekToEnd();
		if (lastchar[0] != '\n')
			file.Write(file.m_eol.c_str(), static_cast<UINT>(file.m_eol.size()));
	}
	else
		file.SeekToEnd();

	return true;
}

bool IgnoreFile(HWND hWnd, const CTGitPathList& path,bool IsMask)
{
	CIgnoreDlg ignoreDlg(GetParentCWnd(hWnd));
	if (ignoreDlg.DoModal() == IDOK)
	{
		CString ignorefile = GitCall::m_CurrentDirStorage + L'\\';

		switch (ignoreDlg.m_IgnoreFile)
		{
		case 0:
			ignorefile += L".gitignore";
			break;

		case 2:
			if (const auto adminDir = GitAdminDir::GetAdminDirPath(GitCall::m_CurrentDirStorage.native()))
				ignorefile = adminDir->path.c_str();
			ignorefile += L"info";
			if (!PathFileExists(ignorefile))
				CreateDirectory(ignorefile, nullptr);
			ignorefile += L"\\exclude";
			break;

		default:
			std::unreachable();
		}

		CIgnoreFile file;
		try
		{
			if (ignoreDlg.m_IgnoreFile != 1 && !OpenIgnoreFile(hWnd, file, ignorefile))
				return false;

			for (int i = 0; i < path.GetCount(); ++i)
			{
				if (ignoreDlg.m_IgnoreFile == 1)
				{
					ignorefile = g_Git.CombinePath(path[i].GetContainingDirectory()) + L"\\.gitignore";
					if (!OpenIgnoreFile(hWnd, file, ignorefile))
						return false;
				}

				CString ignorePattern;
				if (ignoreDlg.m_IgnoreType == 0)
				{
					if (ignoreDlg.m_IgnoreFile != 1 && !path[i].GetContainingDirectory().GetGitPathString().empty())
						ignorePattern += std::format(L"/{}", path[i].GetContainingDirectory().GetGitPathString()).c_str();

					ignorePattern += L'/';
				}
				if (IsMask)
				{
					if (path[i].GetFileExtension().empty())
						continue;
					ignorePattern += std::format(L"*{}", path[i].GetFileExtension()).c_str();
				}
				else
					ignorePattern += path[i].GetFileOrDirectoryName().c_str();

				// escape [ and ] so that files get ignored correctly
				ignorePattern.Replace(L"[", L"\\[");
				ignorePattern.Replace(L"]", L"\\]");

				bool found = false;
				for (size_t j = 0; j < file.m_Items.size(); ++j)
				{
					if (file.m_Items[j] == tgit::str::StringView(ignorePattern))
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					file.m_Items.push_back(std::wstring(ignorePattern));
					CStringA ignorePatternA = CUnicodeUtils::GetUTF8<CStringA>(ignorePattern);
					ignorePatternA += file.m_eol.c_str();
					file.Write(ignorePatternA, ignorePatternA.GetLength());
				}

				if (ignoreDlg.m_IgnoreFile == 1)
					file.Close();
			}

			if (ignoreDlg.m_IgnoreFile != 1)
				file.Close();
		}
		catch(...)
		{
			file.Abort();
			return false;
		}

		return true;
	}
	return false;
}

void DescribeConflictFile(bool mode, bool base,CString &descript)
{
	if(mode == FALSE)
	{
		descript.LoadString(IDS_SVNACTION_DELETE);
		return;
	}
	if(base)
	{
		descript.LoadString(IDS_SVNACTION_MODIFIED);
		return;
	}
	descript.LoadString(IDS_PROC_CREATED);
}

void GetConflictTitles(CString* baseText, CString& mineText, CString& theirsText, bool rebaseActive)
{
	if (baseText)
		baseText->LoadString(IDS_PROC_DIFF_BASE);
	static bool guessBranch = (CRegDWORD(L"Software\\TortoiseGit\\ConflictDontGuessBranchNames", FALSE) == FALSE);
	if (rebaseActive)
	{
		CString adminDir;
		if (const auto adminDirMaybe = GitAdminDir::GetAdminDirPath(GitCall::m_CurrentDirStorage.native()))
			adminDir = adminDirMaybe->path.c_str();
		mineText = L"Branch being rebased onto";
		if (!StringUtils::ReadStringFromTextFile(adminDir + L"tgitrebase.active\\onto", mineText))
		{
			if (CGitHash hash; guessBranch && !g_Git.GetHash(hash, L"rebase-apply/onto"))
			{
				if (g_Git.GuessRefForHash(mineText, hash) == 0)
					mineText.AppendFormat(L", %s", (hash.ToString(TGitH::CGit::GetShortHASHLength())).c_str());
			}
		}
		theirsText = L"Branch being rebased";
		if (!StringUtils::ReadStringFromTextFile(adminDir + L"tgitrebase.active\\head-name", theirsText))
		{
			if (StringUtils::ReadStringFromTextFile(adminDir + L"rebase-apply/head-name", theirsText))
				theirsText = TGitH::StripRefName(theirsText);
		}
		return;
	}

	constexpr struct {
		const wchar_t*	headref;
		bool			guessRef;
		UINT			theirstext;
	} infotexts[] = { { L"MERGE_HEAD", true, IDS_CONFLICT_INFOTEXT }, { L"CHERRY_PICK_HEAD", false, IDS_CONFLICT_INFOTEXT }, { L"REVERT_HEAD", false, IDS_CONFLICT_REVERT } };
	mineText = L"HEAD";
	theirsText.LoadString(IDS_CONFLICT_REFTOBEMERGED);
	for (const auto& infotext : infotexts)
	{
		CGitHash hash;
		if (!g_Git.GetHash(hash, infotext.headref))
		{
			CString guessedRef;
			if (!guessBranch || !infotext.guessRef)
				guessedRef = (hash.ToString(TGitH::CGit::GetShortHASHLength())).c_str();
			else
			{
				if (g_Git.GuessRefForHash(guessedRef, hash) == 0)
					guessedRef.AppendFormat(L", %s", (hash.ToString(TGitH::CGit::GetShortHASHLength())).c_str());
			}
			theirsText.FormatMessage(infotext.theirstext, infotext.headref, static_cast<LPCWSTR>(guessedRef));
			break;
		}
	}
}

bool ConflictEdit(HWND hWnd, CTGitPath& path, bool bAlternativeTool = false, bool isRebase = false, HWND resolveMsgHwnd = nullptr)
{
	CTGitPath merge = path;

	const std::wstring mergeGitPathString = merge.GetGitPathString();

	std::string vector;
	if (g_Git.Run({ L"git.exe", L"ls-files", L"-u", L"-t", L"-z", L"--", mergeGitPathString }, &vector))
		return FALSE;

	CString baseTitle, mineTitle, theirsTitle;
	CGitHash mineHash, theirsHash;
	GetConflictTitles(&baseTitle, mineTitle, theirsTitle, isRebase);

	CGitHash baseHash, localHash, remoteHash;
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	if (TGitH::ParseConflictHashesFromLsFile(vector, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile))
		return FALSE;

	if (!baseIsFile || !localIsFile || !remoteIsFile)
	{
		CTGitPath fullMergePath;
		fullMergePath.SetFromWin(g_Git.CombinePath(merge));
		if (fullMergePath.IsWCRoot())
		{
			CGit subgit;
			subgit.m_IsUseGitDLL = false;
			subgit.SetCurrentDirExact(fullMergePath.GetWinPathString().c_str());
			subgit.GetHash(baseHash, L"HEAD");
		}

		CGitDiff::ChangeType changeTypeMine = CGitDiff::ChangeType::Unknown;
		CGitDiff::ChangeType changeTypeTheirs = CGitDiff::ChangeType::Unknown;

		bool baseOK = false, mineOK = false, theirsOK = false;
		CString baseSubject, mineSubject, theirsSubject;
		if (fullMergePath.IsWCRoot())
		{
			CGit subgit;
			subgit.m_IsUseGitDLL = false;
			subgit.SetCurrentDirExact(fullMergePath.GetWinPathString().c_str());
			CGitDiff::GetSubmoduleChangeType(subgit, baseHash, localHash, baseOK, mineOK, changeTypeMine, baseSubject, mineSubject);
			CGitDiff::GetSubmoduleChangeType(subgit, baseHash, remoteHash, baseOK, theirsOK, changeTypeTheirs, baseSubject, theirsSubject);
		}
		else if (baseHash.IsEmpty() && localHash.IsEmpty() && !remoteHash.IsEmpty()) // merge conflict with no submodule, but submodule in merged revision (not initialized)
		{
			changeTypeMine = CGitDiff::ChangeType::Identical;
			changeTypeTheirs = CGitDiff::ChangeType::NewSubmodule;
			baseSubject.LoadString(IDS_CONFLICT_NOSUBMODULE);
			mineSubject = baseSubject;
			theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
		}
		else if (baseHash.IsEmpty() && !localHash.IsEmpty() && remoteHash.IsEmpty()) // merge conflict with no submodule initialized, but submodule exists in base and folder with no submodule is merged
		{
			baseHash = localHash;
			baseSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			mineSubject = baseSubject;
			theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			changeTypeMine = CGitDiff::ChangeType::Identical;
			changeTypeTheirs = CGitDiff::ChangeType::DeleteSubmodule;
		}
		else if (!baseHash.IsEmpty() && !localHash.IsEmpty() && !remoteHash.IsEmpty()) // base has submodule, mine has submodule and theirs also, but not initialized
		{
			baseSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			mineSubject = baseSubject;
			theirsSubject = baseSubject;
			if (baseHash == localHash)
				changeTypeMine = CGitDiff::ChangeType::Identical;
		}
		else if (baseHash.IsEmpty() && !localHash.IsEmpty() && !remoteHash.IsEmpty())
		{
			baseOK = true;
			mineSubject = baseSubject;
			if (remoteIsFile)
			{
				theirsSubject.LoadString(IDS_CONFLICT_NOTASUBMODULE);
				changeTypeMine = CGitDiff::ChangeType::NewSubmodule;
			}
			else
				theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			if (localIsFile)
			{
				mineSubject.LoadString(IDS_CONFLICT_NOTASUBMODULE);
				changeTypeTheirs = CGitDiff::ChangeType::NewSubmodule;
			}
			else
				mineSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
		}
		else if (!baseHash.IsEmpty() && (localHash.IsEmpty() || remoteHash.IsEmpty()))
		{
			baseSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			if (localHash.IsEmpty())
			{
				mineSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
				changeTypeMine = CGitDiff::ChangeType::DeleteSubmodule;
			}
			else
			{
				mineSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
				if (localHash == baseHash)
					changeTypeMine = CGitDiff::ChangeType::Identical;
			}
			if (remoteHash.IsEmpty())
			{
				theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
				changeTypeTheirs = CGitDiff::ChangeType::DeleteSubmodule;
			}
			else
			{
				theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
				if (remoteHash == baseHash)
					changeTypeTheirs = CGitDiff::ChangeType::Identical;
			}
		}
		else
			return FALSE;

		CSubmoduleResolveConflictDlg resolveSubmoduleConflictDialog(GetParentCWnd(hWnd));
		resolveSubmoduleConflictDialog.SetDiff(merge.GetGitPathString().c_str(), isRebase, baseTitle, mineTitle, theirsTitle, baseHash, baseSubject, baseOK, localHash, mineSubject, mineOK, changeTypeMine, remoteHash, theirsSubject, theirsOK, changeTypeTheirs);
		resolveSubmoduleConflictDialog.DoModal();
		if (resolveSubmoduleConflictDialog.m_bResolved && resolveMsgHwnd)
		{
			static UINT WM_REVERTMSG = RegisterWindowMessage(L"GITSLNM_NEEDSREFRESH");
			::PostMessage(resolveMsgHwnd, WM_REVERTMSG, NULL, NULL);
		}

		return TRUE;
	}

	CTGitPath theirs;
	CTGitPath mine;
	CTGitPath base;

	const auto [mine_name, ther_name] = isRebase ? std::pair{L"REMOTE", L"LOCAL"} : std::pair{L"LOCAL", L"REMOTE"};
	mine.SetFromGit(GetMergeTempFile(mine_name, merge).GetString());
	theirs.SetFromGit(GetMergeTempFile(ther_name, merge).GetString());
	base.SetFromGit(GetMergeTempFile(L"BASE",merge).GetString());

	CFile tempfile;
	//create a empty file, incase stage is not three
	tempfile.Open(mine.GetWinPathString().c_str(),CFile::modeCreate|CFile::modeReadWrite);
	tempfile.Close();
	tempfile.Open(theirs.GetWinPathString().c_str(),CFile::modeCreate|CFile::modeReadWrite);
	tempfile.Close();
	tempfile.Open(base.GetWinPathString().c_str(),CFile::modeCreate|CFile::modeReadWrite);
	tempfile.Close();

	auto prepareFile = [&hWnd, &mergeGitPathString](int stage, const CString& outfile) {
		CString output, err;
		if (!GitCall::Run({ L"git.exe", L"checkout-index", L"--temp", std::format(L"--stage={}", stage), L"--", mergeGitPathString }, &output, &err, CP_UTF8))
		{
			int start = 0;
			CString file = output.Tokenize(L"\t", start);
			::MoveFileEx(file, outfile, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
		}
		else
			CMessageBox::Show(hWnd, output + L'\n' + err, L"TortoiseGit", MB_OK | MB_ICONERROR);
	};
	if (!baseHash.IsEmpty())
		prepareFile(1, base.GetWinPathString().c_str());
	if (!localHash.IsEmpty())
		prepareFile(2, mine.GetWinPathString().c_str());
	if (!remoteHash.IsEmpty())
		prepareFile(3, theirs.GetWinPathString().c_str());

	if (!localHash.IsEmpty() && !remoteHash.IsEmpty())
	{
		merge.SetFromWin(g_Git.CombinePath(merge));
		if (isRebase)
			return !!StartExtMerge(bAlternativeTool, base, mine, theirs, merge, baseTitle, mineTitle, theirsTitle, CString(), resolveMsgHwnd, true);

		return !!StartExtMerge(bAlternativeTool, base, theirs, mine, merge, baseTitle, theirsTitle, mineTitle, CString(), resolveMsgHwnd, true);
	}
	else
	{
		::DeleteFile(mine.GetWinPathString().c_str());
		::DeleteFile(theirs.GetWinPathString().c_str());
		if (baseHash.IsEmpty())
			::DeleteFile(base.GetWinPathString().c_str());

		SCOPE_EXIT{
			if (!baseHash.IsEmpty())
				::DeleteFile(base.GetWinPathString().c_str());
		};

		CDeleteConflictDlg dlg(GetParentCWnd(hWnd));
		if (!isRebase)
		{
			DescribeConflictFile(!localHash.IsEmpty(), !baseHash.IsEmpty(), dlg.m_LocalStatus);
			DescribeConflictFile(!remoteHash.IsEmpty(), !baseHash.IsEmpty(), dlg.m_RemoteStatus);
			dlg.m_LocalHash = mineHash;
			dlg.m_RemoteHash = theirsHash;
			dlg.m_LocalRef = mineTitle;
			dlg.m_RemoteRef = theirsTitle;
			dlg.m_bDiffMine = !localHash.IsEmpty();
		}
		else
		{
			DescribeConflictFile(!localHash.IsEmpty(), !baseHash.IsEmpty(), dlg.m_RemoteStatus);
			DescribeConflictFile(!remoteHash.IsEmpty(), !baseHash.IsEmpty(), dlg.m_LocalStatus);
			dlg.m_LocalHash = theirsHash;
			dlg.m_RemoteHash = mineHash;
			dlg.m_LocalRef = theirsTitle;
			dlg.m_RemoteRef = mineTitle;
			dlg.m_bDiffMine = localHash.IsEmpty();
		}
		dlg.m_bShowModifiedButton = !baseHash.IsEmpty();
		dlg.m_File = merge;
		dlg.m_FileBaseVersion = base;
		if(dlg.DoModal() == IDOK)
		{
			CString out;
			if (GitCall::Run({ L"git.exe", dlg.m_bIsDelete ? L"rm" : L"add", L"--", mergeGitPathString }, &out, CP_UTF8))
			{
				MessageBox(hWnd, out, L"TortoiseGit", MB_OK | MB_ICONERROR);
				return FALSE;
			}
			if (!dlg.m_bIsDelete)
			{
				path.m_Action |= CTGitPath::LOGACTIONS_ADDED;
				path.m_Action &= ~CTGitPath::LOGACTIONS_UNMERGED;
			}
			return TRUE;
		}
		return FALSE;
	}
}

BOOL SVNDCommit(HWND hWnd)
{
	CSVNDCommitDlg dcommitdlg(GetParentCWnd(hWnd));
	CString gitSetting = g_Git.GetConfigValue(L"svn.rmdir");
	if (gitSetting.IsEmpty()) {
		if (dcommitdlg.DoModal() != IDOK)
			return false;
		else
		{
			if (dcommitdlg.m_remember)
			{
				if (dcommitdlg.m_rmdir)
					gitSetting = L"true";
				else
					gitSetting = L"false";
				if (g_Git.SetConfigValue(L"svn.rmdir", gitSetting))
				{
					CString msg;
					msg.FormatMessage(IDS_PROC_SAVECONFIGFAILED, L"svn.rmdir", static_cast<LPCWSTR>(gitSetting));
					MessageBox(hWnd, msg, L"TortoiseGit", MB_OK | MB_ICONERROR);
				}
			}
		}
	}

	BOOL IsStash = false;
	if(!g_Git.CheckCleanWorkTree())
	{
		if (CMessageBox::Show(hWnd, IDS_ERROR_NOCLEAN_STASH, IDS_APPNAME, 1, IDI_QUESTION, IDS_STASHBUTTON, IDS_ABORTBUTTON) == 1)
		{
			CSysProgressDlg sysProgressDlg;
			sysProgressDlg.SetTitle(CString(MAKEINTRESOURCE(IDS_APPNAME)));
			sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_PROC_STASHRUNNING)));
			sysProgressDlg.SetLine(2, CString(MAKEINTRESOURCE(IDS_PROGRESSWAIT)));
			sysProgressDlg.SetShowProgressBar(false);
			sysProgressDlg.SetCancelMsg(IDS_PROGRS_INFOFAILED);
			sysProgressDlg.ShowModeless(hWnd, true);

			CString out;
			if (GitCall::Run({ L"git.exe", L"stash" }, &out, CP_UTF8))
			{
				sysProgressDlg.Stop();
				MessageBox(hWnd, out, L"TortoiseGit", MB_OK | MB_ICONERROR);
				return false;
			}
			sysProgressDlg.Stop();

			IsStash =true;
		}
		else
			return false;
	}

	CProgressDlg progress(GetParentCWnd(hWnd));
	progress.m_GitCmd = { L"git.exe", L"svn", L"dcommit" };
	if (dcommitdlg.m_rmdir)
		progress.m_GitCmd.emplace_back(L"--rmdir");
	if(progress.DoModal()==IDOK && progress.m_GitStatus == 0)
	{
		if( IsStash)
		{
			if (CMessageBox::Show(hWnd, IDS_DCOMMIT_STASH_POP, IDS_APPNAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
			{
				CSysProgressDlg sysProgressDlg;
				sysProgressDlg.SetTitle(CString(MAKEINTRESOURCE(IDS_APPNAME)));
				sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_PROC_STASHRUNNING)));
				sysProgressDlg.SetLine(2, CString(MAKEINTRESOURCE(IDS_PROGRESSWAIT)));
				sysProgressDlg.SetShowProgressBar(false);
				sysProgressDlg.SetCancelMsg(IDS_PROGRS_INFOFAILED);
				sysProgressDlg.ShowModeless(hWnd, true);

				CString out;
				if (GitCall::Run({ L"git.exe", L"stash", L"pop" }, &out, CP_UTF8))
				{
					sysProgressDlg.Stop();
					MessageBox(hWnd, out, L"TortoiseGit", MB_OK | MB_ICONERROR);
					return false;
				}
				sysProgressDlg.Stop();
			}
			else
				return false;
		}
		return TRUE;
	}
	return FALSE;
}

bool StashApply(HWND hWnd, CString ref, bool showChanges = true)
{
	if (StringUtils::StartsWith(ref, L"refs/"))
		ref = ref.Mid(static_cast<int>(wcslen(L"refs/")));
	if (StringUtils::StartsWith(ref, L"stash{"))
		ref = L"stash@" + ref.Mid(static_cast<int>(wcslen(L"stash")));
	std::vector<std::wstring> argv{ L"git.exe", L"stash", L"apply" };
	// The ref used to interpolate as nothing when empty; as an element it would
	// be a real empty argument.
	if (!ref.IsEmpty())
		argv.emplace_back(std::wstring(ref));

	CSysProgressDlg sysProgressDlg;
	sysProgressDlg.SetTitle(CString(MAKEINTRESOURCE(IDS_APPNAME)));
	sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_PROC_STASHRUNNING)));
	sysProgressDlg.SetLine(2, CString(MAKEINTRESOURCE(IDS_PROGRESSWAIT)));
	sysProgressDlg.SetShowProgressBar(false);
	sysProgressDlg.SetCancelMsg(IDS_PROGRS_INFOFAILED);
	sysProgressDlg.ShowModeless(hWnd, true);

	std::string out;
	const int ret = g_Git.Run(argv, &out);

	sysProgressDlg.Stop();

	const bool hasConflicts = (out.find("CONFLICT") != std::string::npos);
	if (ret && !(ret == 1 && hasConflicts))
		CMessageBox::Show(hWnd, CString(MAKEINTRESOURCE(IDS_PROC_STASHAPPLYFAILED)) + L'\n' + out.c_str(), L"TortoiseGit", MB_OK | MB_ICONERROR);
	else
	{
		CString message;
		message.LoadString(IDS_PROC_STASHAPPLYSUCCESS);
		if (hasConflicts)
			message.LoadString(IDS_PROC_STASHAPPLYFAILEDCONFLICTS);
		if (showChanges)
		{
			if (CMessageBox::Show(hWnd, message + L'\n' + CString(MAKEINTRESOURCE(IDS_SEECHANGES)), L"TortoiseGit", MB_YESNO | (hasConflicts ? MB_ICONEXCLAMATION : MB_ICONINFORMATION)) == IDYES)
			{
				CString cmd;
				cmd.Format(L"/command:repostatus /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
				RunTortoiseGitProc(cmd);
			}
			return true;
		}
		else
		{
			MessageBox(hWnd, message ,L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
			return true;
		}
	}
	return false;
}

/** Execute "stash pop"
 * showChanges
 *              0: only show info on error (and allow to diff on error)
 *              1: allow to open diff dialog on error or success
 *              2: only show error or success message
 */
bool StashPop(HWND hWnd, int showChanges = 1)
{
	CSysProgressDlg sysProgressDlg;
	sysProgressDlg.SetTitle(CString(MAKEINTRESOURCE(IDS_APPNAME)));
	sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_PROC_STASHRUNNING)));
	sysProgressDlg.SetLine(2, CString(MAKEINTRESOURCE(IDS_PROGRESSWAIT)));
	sysProgressDlg.SetShowProgressBar(false);
	sysProgressDlg.SetCancelMsg(IDS_PROGRS_INFOFAILED);
	sysProgressDlg.ShowModeless(hWnd, true);

	std::string out;
	const int ret = g_Git.Run({ L"git.exe", L"stash", L"pop" }, &out);

	sysProgressDlg.Stop();

	const bool hasConflicts = (out.find("CONFLICT") != std::string::npos);
	if (ret && !(ret == 1 && hasConflicts)) {
		const auto msg = CString(MAKEINTRESOURCE(IDS_PROC_STASHPOPFAILED)) + L'\n' + out.c_str();
		CMessageBox::Show(hWnd, msg, L"TortoiseGit", MB_OK | MB_ICONERROR);
	} else {
		CString message;
		message.LoadString(IDS_PROC_STASHPOPSUCCESS);
		if (hasConflicts)
			message.LoadString(IDS_PROC_STASHPOPFAILEDCONFLICTS);
		if (showChanges == 1 || (showChanges == 0 && hasConflicts))
		{
			if (CMessageBox::ShowCheck(hWnd, message + L'\n' + CString(MAKEINTRESOURCE(IDS_SEECHANGES)), L"TortoiseGit", MB_YESNO | (hasConflicts ? MB_ICONEXCLAMATION : MB_ICONINFORMATION), hasConflicts ? L"StashPopShowConflictChanges" : L"StashPopShowChanges") == IDYES)
			{
				CString cmd;
				cmd.Format(L"/command:repostatus /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
				RunTortoiseGitProc(cmd);
			}
			return true;
		}
		else if (showChanges > 1)
		{
			MessageBox(hWnd, message, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
			return true;
		}
		else if (showChanges == 0)
			return true;
	}
	return false;
}

BOOL MergeAbort(HWND hWnd);

bool Reset(HWND hWnd, const CString& resetTo, int resetType)
{
	CString type;
	switch (resetType)
	{
	case 0:
		type = L"--soft";
		break;
	case 1:
		type = L"--mixed";
		break;
	case 2:
		type = L"--hard";
		break;
	case 3:
	{
		CProgressDlg progress(GetParentCWnd(hWnd));
		progress.m_GitCmd = { L"git.exe", L"reset", L"--merge" };
		progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
		{
			if (status)
			{
				postCmdList.emplace_back(IDI_REFRESH, IDS_MSGBOX_RETRY, [&hWnd] { MergeAbort(hWnd); });
				return;
			}

			CTGitPath gitPath{ GitCall::m_CurrentDirStorage.native() };
			if (gitPath.HasSubmodules() && resetType == 2)
			{
				postCmdList.emplace_back(IDI_UPDATE, IDS_PROC_SUBMODULESUPDATE, [&]
				{
					CString sCmd;
					sCmd.Format(L"/command:subupdate /bkpath:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
					RunTortoiseGitProc(sCmd);
				});
			}
		};
		return progress.DoModal() == IDOK;
	}
	default:
		ATLASSERT(false);
		resetType = 1;
		type = L"--mixed";
		break;
	}

	CProgressDlg progress(GetParentCWnd(hWnd));
	progress.m_GitCmd = { L"git.exe", L"reset", std::wstring(type), L"--end-of-options", std::wstring(resetTo), L"--" };

	progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
	{
		if (status)
		{
			postCmdList.emplace_back(IDI_REFRESH, IDS_MSGBOX_RETRY, [&]{ Reset(hWnd, resetTo, resetType); });
			return;
		}

		CTGitPath gitPath{ GitCall::m_CurrentDirStorage.native() };
		if (gitPath.HasSubmodules() && resetType == 2)
		{
			postCmdList.emplace_back(IDI_UPDATE, IDS_PROC_SUBMODULESUPDATE, [&]
			{
				CString sCmd;
				sCmd.Format(L"/command:subupdate /bkpath:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
				RunTortoiseGitProc(sCmd);
			});
		}

		if (gitPath.IsBisectActive())
		{
			postCmdList.emplace_back(IDI_THUMB_UP, IDS_MENUBISECTGOOD, [] { RunTortoiseGitProc(L"/command:bisect /good"); });
			postCmdList.emplace_back(IDI_THUMB_DOWN, IDS_MENUBISECTBAD, [] { RunTortoiseGitProc(L"/command:bisect /bad"); });
			postCmdList.emplace_back(IDI_BISECT, IDS_MENUBISECTSKIP, [] { RunTortoiseGitProc(L"/command:bisect /skip"); });
			postCmdList.emplace_back(IDI_BISECT_RESET, IDS_MENUBISECTRESET, [] { RunTortoiseGitProc(L"/command:bisect /reset"); });
		}

		if (resetType == 2)
		{
			postCmdList.emplace_back(IDI_CLEANUP, IDS_MENUCLEANUP, [&] {
				CString sCmd;
				sCmd.Format(L"/command:cleanup /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
				RunTortoiseGitProc(sCmd);
			});
		}
	};

	INT_PTR ret;
	if (g_Git.UsingLibGit2(TGitH::GIT_CMD_RESET))
	{
		CGitProgressDlg gitdlg(GetParentCWnd(hWnd));
		ResetProgressCommand resetProgressCommand;
		gitdlg.SetCommand(&resetProgressCommand);
		resetProgressCommand.m_PostCmdCallback = progress.m_PostCmdCallback;
		resetProgressCommand.SetRevision(resetTo);
		resetProgressCommand.SetResetType(resetType);
		ret = gitdlg.DoModal();
	}
	else
		ret = progress.DoModal();

	return ret == IDOK;
}

BOOL MergeAbort(HWND hWnd)
{
	CMergeAbortDlg dlg(GetParentCWnd(hWnd));
	if (dlg.DoModal() == IDOK)
		return Reset(hWnd, L"HEAD", (dlg.m_ResetType == 0) ? 3 : dlg.m_ResetType);

	return FALSE;
}

bool GitReset(HWND hWnd, const CString& ref, const int type = 1)
{
	CResetDlg dlg(GetParentCWnd(hWnd));
	dlg.m_ResetType=type;
	dlg.m_ResetToVersion = ref;
	dlg.m_initialRefName = ref;
	if (dlg.DoModal() == IDOK)
		return Reset(hWnd, dlg.m_ResetToVersion, dlg.m_ResetType);

	return false;
}

// The post-command buttons below reach these before they are defined.
bool Pull(HWND hWnd, bool showPush = false, bool showStashPop = false);
bool Fetch(HWND hWnd, const CString& remoteName = {}, bool allRemotes = false);
bool Push(HWND hWnd, const CString& selectLocalBranch = {}, int pushAll = BST_INDETERMINATE);
BOOL Merge(HWND hWnd, const CString* commit = nullptr, bool showStashPop = false);
BOOL Commit(HWND hWnd, const CString& bugid, BOOL bWholeProject, CString& sLogMsg, const CTGitPathList& pathList, bool bSelectFilesForCommit);

bool StashSave(HWND hWnd, const CString& msg = CString(), bool showPull = false, bool pullShowPush = false, bool showMerge = false, const CString& mergeRev = CString())
{
	if (!CheckUserData(hWnd))
		return false;

	CStashSaveDlg dlg(GetParentCWnd(hWnd));
	dlg.m_sMessage = msg;
	if (dlg.DoModal() == IDOK)
	{
		CGitHash oldStash;
		g_Git.GetHash(oldStash, L"refs/stash");

		std::vector<std::wstring> cmd{ L"git.exe", L"stash", L"push" };

		if (dlg.m_bIncludeUntracked)
			cmd.emplace_back(L"--include-untracked");
		else if (dlg.m_bAll)
			cmd.emplace_back(L"--all");

		if (!dlg.m_sMessage.IsEmpty())
			cmd.insert(cmd.cend(), { L"-m", std::wstring(dlg.m_sMessage) });

		CProgressDlg progress(GetParentCWnd(hWnd));
		progress.m_GitCmd = cmd;
		progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
		{
			if (status)
				return;

			if (showPull)
				postCmdList.emplace_back(IDI_PULL, IDS_MENUPULL, [&]{ Pull(hWnd, pullShowPush, true); });
			if (showMerge)
				postCmdList.emplace_back(IDI_MERGE, IDS_MENUMERGE, [&]{ Merge(hWnd, &mergeRev, true); });
			CGitHash newStash;
			g_Git.GetHash(newStash, L"refs/stash");
			if (newStash == oldStash)
				return;
			postCmdList.emplace_back(IDI_UNSHELVE, IDS_MENUSTASHPOP, [&hWnd] { StashPop(hWnd); });
			postCmdList.emplace_back(IDI_UNSHELVE, IDS_MENUSTASHAPPLY, [&hWnd] { StashApply(hWnd, L""); });
		};
		return (progress.DoModal() == IDOK);
	}
	return false;
}

bool PerformSwitch(
	HWND hWnd,
	const CString& ref,
	bool bForce = false,
	const CString& sNewBranch = {},
	bool bBranchOverride = false,
	BOOL bTrack = 2,
	bool bMerge = false
) {
	CProgressDlg progress(GetParentCWnd(hWnd));
	{
		std::vector<std::wstring> cmd{ L"git.exe", L"checkout" };
		if (bForce)
			cmd.emplace_back(L"-f");
		if (!sNewBranch.IsEmpty()) {
			if (bTrack == BST_CHECKED)
				cmd.emplace_back(L"--track");
			else if (bTrack == BST_UNCHECKED)
				cmd.emplace_back(L"--no-track");
			cmd.insert(cmd.cend(), { bBranchOverride ? L"-B" : L"-b", std::wstring(sNewBranch) });
		}
		if (bMerge)
			cmd.emplace_back(L"--merge");

		cmd.insert(cmd.cend(), { L"--end-of-options", std::wstring(g_Git.FixBranchName(ref)), L"--" });
		progress.m_GitCmd = std::move(cmd);
	}

	CString currentBranch;
	const bool hasBranch = CGit::GetCurrentBranchFromFile(GitCall::m_CurrentDirStorage, currentBranch) == 0;
	progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList) {
		if (!status) {
			CTGitPath gitPath{ GitCall::m_CurrentDirStorage.native() };
			if (gitPath.HasSubmodules()) {
				postCmdList.emplace_back(IDI_UPDATE,
				                         IDS_PROC_SUBMODULESUPDATE,
				                         [&] {
					                         CString sCmd;
					                         sCmd.Format(L"/command:subupdate /bkpath:\"%s\"",
					                                     GitCall::m_CurrentDirStorage.c_str());
					                         RunTortoiseGitProc(sCmd);
				                         });
			}
			if (hasBranch)
				postCmdList.emplace_back(IDI_MERGE, IDS_MENUMERGE, [&] { Merge(hWnd, &currentBranch); });


			CString newBranch;
			if (!CGit::GetCurrentBranchFromFile(GitCall::m_CurrentDirStorage, newBranch))
				postCmdList.emplace_back(IDI_PULL, IDS_MENUPULL, [&hWnd] { Pull(hWnd); });

			postCmdList.emplace_back(IDI_COMMIT,
			                         IDS_MENUCOMMIT,
			                         [&] {
				                         CTGitPathList pathlist;
				                         pathlist.AddPath(CTGitPath());
				                         const bool bSelectFilesForCommit = static_cast<DWORD>(CRegStdDWORD(
					                         L"Software\\TortoiseGit\\SelectFilesForCommit",
					                         TRUE)) != 0;
				                         CString str;
				                         Commit(hWnd, CString(), false, str, pathlist, bSelectFilesForCommit);
			                         });
		}
		else {
			if (bMerge && g_Git.HasWorkingTreeConflicts() > 0) {
				postCmdList.emplace_back(IDI_RESOLVE,
				                         IDS_PROGRS_CMD_RESOLVE,
				                         [] {
					                         CString sCmd;
					                         sCmd.Format(L"/command:commit /path:\"%s\"",
					                                     GitCall::m_CurrentDirStorage.c_str());
					                         RunTortoiseGitProc(sCmd);
				                         });
			}
			if (!bMerge)
				postCmdList.emplace_back(IDI_SHELVE, IDS_MENUSTASHSAVE, [&hWnd] { StashSave(hWnd, L"", true); });
			postCmdList.emplace_back(IDI_REFRESH,
			                         IDS_MSGBOX_RETRY,
			                         [&] {
				                         PerformSwitch(hWnd, ref, bForce, sNewBranch, bBranchOverride, bTrack, bMerge);
			                         });
			if (!bMerge)
				postCmdList.emplace_back(IDI_SWITCH,
				                         IDS_SWITCH_WITH_MERGE,
				                         [&] {
					                         PerformSwitch(hWnd,
					                                       ref,
					                                       bForce,
					                                       sNewBranch,
					                                       bBranchOverride,
					                                       bTrack,
					                                       true);
				                         });
		}
	};
	progress.m_PostExecCallback = [&](HWND /* hWnd */, DWORD& exitCode, CString& extraMsg) {
		if (bMerge && !exitCode && g_Git.HasWorkingTreeConflicts() > 0) {
			exitCode = 1; // Treat it as failure
			extraMsg = L"Has merge conflict";
		}
	};

	return progress.DoModal() == IDOK;
}

bool Switch(HWND hWnd, const CString& initialRefName = {})
{
	CGitSwitchDlg dlg(GetParentCWnd(hWnd));
	if(!initialRefName.IsEmpty())
		dlg.m_initialRefName = initialRefName;

	if (dlg.DoModal() == IDOK)
	{
		CString branch;
		if (dlg.m_bBranch)
			branch = dlg.m_NewBranch;

		// if refs/heads/ is not stripped, checkout will detach HEAD
		// checkout prefers branches on name clashes (with tags)
		if (StringUtils::StartsWith(dlg.m_VersionName, L"refs/heads/") && dlg.m_bBranchOverride != TRUE)
			dlg.m_VersionName = dlg.m_VersionName.Mid(static_cast<int>(wcslen(L"refs/heads/")));

		return PerformSwitch(hWnd, dlg.m_VersionName, dlg.m_bForce == TRUE, branch, dlg.m_bBranchOverride == TRUE, dlg.m_bTrack, dlg.m_bMerge == TRUE);
	}
	return FALSE;
}

// rebase = 1: ask user what to do, rebase = 2: run autorebase
bool RebaseAfterFetch(HWND hWnd, const CString& upstream = L"", int rebase = 0, bool preserveMerges = false)
{
	while (true)
	{
		CRebaseDlg dlg(GetParentCWnd(hWnd));
		if (!upstream.IsEmpty())
			dlg.m_Upstream = upstream;
		dlg.m_PostButtonTexts.Add(CString(MAKEINTRESOURCE(IDS_MENULOG)));
		dlg.m_PostButtonTexts.Add(CString(MAKEINTRESOURCE(IDS_MENUPUSH)));
		dlg.m_PostButtonTexts.Add(CString(MAKEINTRESOURCE(IDS_MENUREBASE)));
		dlg.m_bRebaseAutoStart = (rebase == 2);
		dlg.m_bPreserveMerges = preserveMerges;
		const INT_PTR response = dlg.DoModal();
		if (response == IDOK)
			return true;
		else if (response == IDC_REBASE_POST_BUTTON)
		{
			CString cmd = L"/command:log";
			cmd += L" /path:\"" + GitCall::m_CurrentDirStorage + L'"';
			RunTortoiseGitProc(cmd);
			return true;
		}
		else if (response == IDC_REBASE_POST_BUTTON + 1)
			return Push(hWnd);
		// The "email patches" button used to sit at +2; removing it renumbers
		// "rebase" from +3 down to +2. The offsets are positions in
		// m_PostButtonTexts, so the two lists have to be edited together.
		else if (response == IDC_REBASE_POST_BUTTON + 2)
			continue;
		else if (response == IDCANCEL)
			return false;
		return false;
	}
}

namespace details {
	bool DoPull(HWND hWnd, const CString& url, BOOL bFetchTags, bool bNoFF, bool bFFonly, bool bSquash, bool bNoCommit, int* nDepth, BOOL bPrune, const CString& remoteBranchName, bool showPush, bool showStashPop, bool bUnrelated)
	{
		CGitHash hashOld;
		if (g_Git.GetHash(hashOld, L"HEAD"))
		{
			MessageBox(hWnd, g_Git.GetGitLastErr(L"Could not get HEAD hash."), L"TortoiseGit", MB_ICONERROR);
			return false;
		}

		std::vector<std::wstring> args;
		if (bFetchTags == BST_UNCHECKED)
			args.emplace_back(L"--no-tags");
		else if (bFetchTags == BST_CHECKED)
			args.emplace_back(L"--tags");

		if (bNoFF)
			args.emplace_back(L"--no-ff");

		if (bFFonly)
			args.emplace_back(L"--ff-only");

		if (bSquash)
			args.emplace_back(L"--squash");

		if (bNoCommit)
			args.emplace_back(L"--no-commit");

		if (nDepth)
			args.insert(args.cend(), { L"--depth", std::to_wstring(*nDepth) });

		if (bPrune == BST_CHECKED)
			args.emplace_back(L"--prune");
		else if (bPrune == BST_UNCHECKED)
			args.emplace_back(L"--no-prune");

		if (bUnrelated)
			args.emplace_back(L"--allow-unrelated-histories");

		CProgressDlg progress(GetParentCWnd(hWnd));
		{
			std::vector<std::wstring> cmd{ L"git.exe", L"pull", L"--progress", L"-v", L"--no-rebase" };
			cmd.insert(cmd.cend(), args.cbegin(), args.cend());
			cmd.insert(cmd.cend(), { L"--", std::wstring(url) });
			if (!remoteBranchName.IsEmpty())
				cmd.emplace_back(std::wstring(remoteBranchName));
			progress.m_GitCmd = std::move(cmd);
		}
		CGitHash hashNew; // declare outside lambda, because it is captured by reference
		progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
		{
			if (status)
			{
				int hasConflicts = g_Git.HasWorkingTreeConflicts();
				if (hasConflicts < 0)
					CMessageBox::Show(hWnd, g_Git.GetGitLastErr(L"Checking for conflicts failed.", TGitH::GIT_CMD_CHECKCONFLICTS), L"TortoiseGit", MB_ICONEXCLAMATION);
				else if (hasConflicts)
				{
					CMessageBox::ShowCheck(hWnd, IDS_NEED_TO_RESOLVE_CONFLICTS_HINT, IDS_APPNAME, MB_ICONINFORMATION, L"MergeConflictsNeedsCommit", IDS_MSGBOX_DONOTSHOWAGAIN);
					postCmdList.emplace_back(IDI_RESOLVE, IDS_PROGRS_CMD_RESOLVE, []
					{
						CString sCmd;
						sCmd.Format(L"/command:resolve /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
						RunTortoiseGitProc(sCmd);
					});

					postCmdList.emplace_back(IDI_COMMIT, IDS_MENUCOMMIT, []
					{
						CString sCmd;
						sCmd.Format(L"/command:commit /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
						RunTortoiseGitProc(sCmd);
					});
					return;
				}

				std::vector<std::wstring> remotes;
				g_Git.GetRemoteList(remotes);
				if (std::find(remotes.begin(), remotes.end(), tgit::str::StringView(url)) != remotes.end())
				{
					CString currentBranch;
					if (g_Git.GetCurrentBranchFromFile(GitCall::m_CurrentDirStorage, currentBranch))
						currentBranch.Empty();
					CString remoteRef = L"remotes/" + url + L"/" + remoteBranchName;
					if (!currentBranch.IsEmpty() && remoteBranchName.IsEmpty())
					{
						CString pullRemote, pullBranch;
						g_Git.GetRemoteTrackedBranch(currentBranch, pullRemote, pullBranch);
						if (!pullRemote.IsEmpty() && !pullBranch.IsEmpty())
							remoteRef = L"remotes/" + pullRemote + L"/" + pullBranch;
					}
					CGitHash common;
					g_Git.IsFastForward(L"HEAD", remoteRef, &common);
					if (common.IsEmpty())
						postCmdList.emplace_back(IDI_MERGE, IDS_MERGE_UNRELATED, [=, &hWnd] { DoPull(hWnd, url, bFetchTags, bNoFF, bFFonly, bSquash, bNoCommit, nDepth, bPrune, remoteBranchName, showPush, showStashPop, true); });
				}

				postCmdList.emplace_back(IDI_PULL, IDS_MENUPULL, [&hWnd]{ Pull(hWnd); });
				postCmdList.emplace_back(IDI_SHELVE, IDS_MENUSTASHSAVE, [&hWnd]{ StashSave(hWnd, L"", true); });
				postCmdList.emplace_back(IDI_RESET, IDS_PROC_RESET, [&hWnd] {
					CString pullRemote, pullBranch;
					g_Git.GetRemoteTrackedBranchForHEAD(pullRemote, pullBranch);
					CString defaultUpstream;
					if (!pullRemote.IsEmpty() && !pullBranch.IsEmpty())
						defaultUpstream.Format(L"remotes/%s/%s", static_cast<LPCWSTR>(pullRemote), static_cast<LPCWSTR>(pullBranch));
					GitReset(hWnd, defaultUpstream, 2);
				});
				return;
			}

			if (showStashPop)
				postCmdList.emplace_back(IDI_UNSHELVE, IDS_MENUSTASHPOP, [&hWnd]{ StashPop(hWnd); });

			if (g_Git.GetHash(hashNew, L"HEAD"))
				MessageBox(hWnd, g_Git.GetGitLastErr(L"Could not get HEAD hash after pulling."), L"TortoiseGit", MB_ICONERROR);
			else
			{
				postCmdList.emplace_back(IDI_DIFF, IDS_PROC_PULL_DIFFS, [&]
				{
					CString sCmd;
					sCmd.Format(L"/command:showcompare /path:\"%s\" /revision1:%s /revision2:%s", GitCall::m_CurrentDirStorage.c_str(), (hashOld.ToString()).c_str(), (hashNew.ToString()).c_str());
					RunTortoiseGitProc(sCmd);
				});
				postCmdList.emplace_back(IDI_LOG, IDS_PROC_PULL_LOG, [&]
				{
					CString sCmd;
					sCmd.Format(L"/command:log /path:\"%s\" /range:%s", GitCall::m_CurrentDirStorage.c_str(), (hashOld.ToString() + L".." + hashNew.ToString()).c_str());
					RunTortoiseGitProc(sCmd);
				});
			}

			if (showPush)
				postCmdList.emplace_back(IDI_PUSH, IDS_MENUPUSH, [&hWnd]{ Push(hWnd); });

			CTGitPath gitPath{ GitCall::m_CurrentDirStorage.native() };
			if (gitPath.HasSubmodules())
			{
				postCmdList.emplace_back(IDI_UPDATE, IDS_PROC_SUBMODULESUPDATE, []
				{
					CString sCmd;
					sCmd.Format(L"/command:subupdate /bkpath:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
					RunTortoiseGitProc(sCmd);
				});
			}
		};

		const INT_PTR ret = progress.DoModal();

		if (ret == IDOK && progress.m_GitStatus == 1 && progress.m_LogText.Find(L"CONFLICT") >= 0 && CMessageBox::Show(hWnd, IDS_SEECHANGES, IDS_APPNAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
		{
			CString cmd;
			cmd.Format(L"/command:repostatus /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
			RunTortoiseGitProc(cmd);

			return true;
		}

		return ret == IDOK;
	}

	bool DoFetch(HWND hWnd, const CString& url, const bool fetchAllRemotes, const int prune, const bool bDepth, const int nDepth, const int fetchTags, const CString& remoteBranch, int runRebase, const bool rebasePreserveMerges)
	{
		CString upstream = L"FETCH_HEAD";
		CGitHash oldUpstreamHash;
		if (runRebase)
		{
			std::vector<std::wstring> list;
			g_Git.GetRemoteList(list);
			for (auto it = list.cbegin(); it != list.cend(); ++it)
			{
				if (url == it->c_str())
				{
					upstream.Empty();
					if (remoteBranch.IsEmpty()) // pulldlg might clear remote branch if its the default tracked branch
					{
						CString currentBranch;
						if (g_Git.GetCurrentBranchFromFile(GitCall::m_CurrentDirStorage, currentBranch))
							currentBranch.Empty();
						if (!currentBranch.IsEmpty() && remoteBranch.IsEmpty())
						{
							CString pullRemote, pullBranch;
							g_Git.GetRemoteTrackedBranch(currentBranch, pullRemote, pullBranch);
							if (!pullRemote.IsEmpty() && !pullBranch.IsEmpty() && pullRemote == url) // pullRemote == url is just another safety-check and should not be needed
								upstream = std::format(L"remotes/{}/{}", *it, pullBranch).c_str();
						}
					}
					else
						upstream = std::format(L"remotes/{}/{}", *it, remoteBranch).c_str();

					g_Git.GetHash(oldUpstreamHash, upstream);
					break;
				}
			}
		}

		std::vector<std::wstring> cmd{ L"git.exe", L"fetch" };
		if (fetchAllRemotes)
			cmd.emplace_back(L"--all");

		cmd.emplace_back(L"--progress");

		if (CRegDWORD(L"Software\\TortoiseGit\\FetchVerbose", TRUE) == TRUE)
			cmd.emplace_back(L"-v");

		if (bDepth)
			cmd.insert(cmd.cend(), { L"--depth", std::to_wstring(nDepth) });

		if (prune == TRUE)
			cmd.emplace_back(L"--prune");
		else if (prune == FALSE)
			cmd.emplace_back(L"--no-prune");

		if (fetchTags == 1)
			cmd.emplace_back(L"--tags");
		else if (fetchTags == 0)
			cmd.emplace_back(L"--no-tags");

		if (!fetchAllRemotes)
		{
			cmd.insert(cmd.cend(), { L"--", std::wstring(url) });
			if (!remoteBranch.IsEmpty())
				cmd.emplace_back(std::wstring(remoteBranch));
		}

		CProgressDlg progress(GetParentCWnd(hWnd));
		progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
		{
			if (status)
			{
				postCmdList.emplace_back(IDI_REFRESH, IDS_MSGBOX_RETRY, [&]{ DoFetch(hWnd, url, fetchAllRemotes, prune, bDepth, nDepth, fetchTags, remoteBranch, runRebase, rebasePreserveMerges); });
				if (fetchAllRemotes)
					postCmdList.emplace_back(IDI_LOG, IDS_MENULOG, []
					{
						CString cmd = L"/command:log";
						cmd += L" /path:\"" + GitCall::m_CurrentDirStorage + L'"';
						RunTortoiseGitProc(cmd);
					});
				return;
			}

			postCmdList.emplace_back(IDI_LOG, IDS_MENULOG, []
			{
				CString cmd = L"/command:log";
				cmd += L" /path:\"" + GitCall::m_CurrentDirStorage + L'"';
				RunTortoiseGitProc(cmd);
			});

			postCmdList.emplace_back(IDI_RESET, IDS_PROC_RESET, [&hWnd]
			{
				CString pullRemote, pullBranch;
				g_Git.GetRemoteTrackedBranchForHEAD(pullRemote, pullBranch);
				CString defaultUpstream;
				if (!pullRemote.IsEmpty() && !pullBranch.IsEmpty())
					defaultUpstream.Format(L"remotes/%s/%s", static_cast<LPCWSTR>(pullRemote), static_cast<LPCWSTR>(pullBranch));
				GitReset(hWnd, defaultUpstream, 2);
			});

			postCmdList.emplace_back(IDI_UPDATE, IDS_MENUFETCH, [&hWnd]{ Fetch(hWnd); });

			if (!runRebase && !GitAdminDir::IsBareRepo(GitCall::m_CurrentDirStorage))
				postCmdList.emplace_back(IDI_REBASE, IDS_MENUREBASE, [&]{ runRebase = false; RebaseAfterFetch(hWnd); });

			postCmdList.emplace_back(IDI_SWITCH, IDS_MENUSWITCH, [&hWnd] { Switch(hWnd); });
		};

		progress.m_GitCmd = cmd;

		if (g_Git.UsingLibGit2(TGitH::GIT_CMD_FETCH))
		{
			CGitProgressDlg gitdlg(GetParentCWnd(hWnd));
			FetchProgressCommand fetchProgressCommand;
			if (!fetchAllRemotes)
				fetchProgressCommand.SetUrl(url);
			gitdlg.SetCommand(&fetchProgressCommand);
			fetchProgressCommand.m_PostCmdCallback = progress.m_PostCmdCallback;
			fetchProgressCommand.SetAutoTag(fetchTags == 1 ? GIT_REMOTE_DOWNLOAD_TAGS_ALL : fetchTags == 2 ? GIT_REMOTE_DOWNLOAD_TAGS_AUTO : GIT_REMOTE_DOWNLOAD_TAGS_NONE);
			fetchProgressCommand.SetPrune(prune == BST_CHECKED ? GIT_FETCH_PRUNE : prune == BST_INDETERMINATE ? GIT_FETCH_PRUNE_UNSPECIFIED : GIT_FETCH_NO_PRUNE);
			if (!fetchAllRemotes)
				fetchProgressCommand.SetRefSpec(remoteBranch);
			return gitdlg.DoModal() == IDOK;
		}

		progress.m_PostExecCallback = [&upstream, &runRebase, &oldUpstreamHash, &rebasePreserveMerges](HWND hWnd_lam, const DWORD& exitCode, CString&)
		{
			if (exitCode || !runRebase)
				return;

			CGitHash remoteBranchHash;
			g_Git.GetHash(remoteBranchHash, upstream);

			if (runRebase == 1)
			{
				CGitHash headHash, commonAcestor;
				if (!g_Git.GetHash(headHash, L"HEAD") && (remoteBranchHash == headHash || (g_Git.IsFastForward(upstream, L"HEAD", &commonAcestor) && commonAcestor == remoteBranchHash)) && CMessageBox::ShowCheck(hWnd_lam, IDS_REBASE_CURRENTBRANCHUPTODATE, IDS_APPNAME, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, L"OpenRebaseRemoteBranchEqualsHEAD", IDS_MSGBOX_DONOTSHOWAGAIN) == IDNO)
					return;

				if (remoteBranchHash == oldUpstreamHash && remoteBranchHash == headHash && !oldUpstreamHash.IsEmpty() && CMessageBox::ShowCheck(hWnd_lam, IDS_REBASE_BRANCH_UNCHANGED, IDS_APPNAME, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, L"OpenRebaseRemoteBranchUnchanged", IDS_MSGBOX_DONOTSHOWAGAIN) == IDNO)
					return;
			}

			if (runRebase == 1 && g_Git.IsFastForward(L"HEAD", upstream))
			{
				UINT ret = CMessageBox::ShowCheck(hWnd_lam, IDS_REBASE_BRANCH_FF, IDS_APPNAME, 2, IDI_QUESTION, IDS_MERGEBUTTON, IDS_REBASEBUTTON, IDS_ABORTBUTTON, L"OpenRebaseRemoteBranchFastForwards", IDS_MSGBOX_DONOTSHOWAGAIN);
				if (ret == 3)
					return;
				if (ret == 1)
				{
					CProgressDlg mergeProgress(CWnd::FromHandle(hWnd_lam));
					mergeProgress.m_GitCmd = { L"git.exe", L"merge", L"--ff-only", L"--", std::wstring(upstream) };
					mergeProgress.m_AutoClose = GitProgressAutoClose::AUTOCLOSE_IF_NO_ERRORS;
					mergeProgress.m_PostCmdCallback = [](DWORD status, PostCmdList& postCmdList)
					{
						if (status && g_Git.HasWorkingTreeConflicts())
						{
							// there are conflict files
							postCmdList.emplace_back(IDI_RESOLVE, IDS_PROGRS_CMD_RESOLVE, []
							{
								CString sCmd;
								sCmd.Format(L"/command:commit /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
								RunTortoiseGitProc(sCmd);
							});
						}
					};
					mergeProgress.DoModal();
					return;
				}
			}

			RebaseAfterFetch(hWnd_lam, upstream, runRebase, rebasePreserveMerges);
		};

		return progress.DoModal() == IDOK;
	}
}

bool Pull(HWND hWnd, bool showPush, bool showStashPop)
{
	if (IsTGitRebaseActive(hWnd))
		return false;

	CPullFetchDlg dlg(GetParentCWnd(hWnd));
	dlg.m_IsPull = TRUE;
	if (dlg.DoModal() == IDOK)
	{
		// "git.exe pull --rebase" is not supported, never and ever. So, adapting it to Fetch & Rebase.
		if (dlg.m_bRebase)
			return details::DoFetch(hWnd,
							dlg.m_RemoteURL,
							FALSE, // Fetch all remotes
							dlg.m_bPrune,
							dlg.m_bDepth == BST_CHECKED,
							dlg.m_nDepth,
							dlg.m_bFetchTags,
							dlg.m_RemoteBranchName,
							dlg.m_bRebaseActivatedInConfigForPull ? 2 : 1, // Rebase after fetching
							dlg.m_bRebasePreserveMerges == TRUE); // Preserve merges on rebase

		return details::DoPull(hWnd, dlg.m_RemoteURL, dlg.m_bFetchTags, dlg.m_bNoFF == BST_CHECKED, dlg.m_bFFonly == BST_CHECKED, dlg.m_bSquash == BST_CHECKED, dlg.m_bNoCommit == BST_CHECKED, dlg.m_bDepth ? &dlg.m_nDepth : nullptr, dlg.m_bPrune, dlg.m_RemoteBranchName, showPush, showStashPop, false);
	}

	return false;
}

bool Fetch(HWND hWnd, const CString& remoteName /* = {} */, const bool allRemotes /* = false */)
{
	CPullFetchDlg dlg(GetParentCWnd(hWnd));
	dlg.m_PreSelectRemote = remoteName;
	dlg.m_IsPull=FALSE;
	dlg.m_bAllRemotes = allRemotes;

	if(dlg.DoModal()==IDOK)
		return details::DoFetch(
			hWnd,
			dlg.m_RemoteURL,
			dlg.m_bAllRemotes == BST_CHECKED,
			dlg.m_bPrune,
			dlg.m_bDepth == BST_CHECKED,
			dlg.m_nDepth,
			dlg.m_bFetchTags,
			dlg.m_RemoteBranchName,
			dlg.m_bRebase == BST_CHECKED ? 1 : 0,
			FALSE
		);

	return false;
}

bool RequestPull(HWND hWnd, const CString& endrevision = {}, const CString& repositoryUrl = {})
{
	CRequestPullDlg dlg(GetParentCWnd(hWnd));
	dlg.m_RepositoryURL = repositoryUrl;
	dlg.m_EndRevision = endrevision;
	if (dlg.DoModal()==IDOK)
	{
		const std::vector<std::wstring> cmd{ L"git.exe", L"request-pull", L"--", std::wstring(dlg.m_StartRevision), std::wstring(dlg.m_RepositoryURL), std::wstring(dlg.m_EndRevision) };

		CSysProgressDlg sysProgressDlg;
		sysProgressDlg.SetTitle(CString(MAKEINTRESOURCE(IDS_APPNAME)));
		sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_PROC_CREATINGPULLREUQEST)));
		sysProgressDlg.SetLine(2, CString(MAKEINTRESOURCE(IDS_PROGRESSWAIT)));
		sysProgressDlg.SetShowProgressBar(false);
		sysProgressDlg.ShowModeless(hWnd, true);

		CString tempFileName = GitCall::GetTempFile();
		if (tempFileName.IsEmpty())
		{
			sysProgressDlg.Stop();
			::MessageBox(hWnd, L"Could not create temp file.", L"TortoiseGit", MB_OK | MB_ICONERROR);
			return false;
		}
		DeleteFile(tempFileName);
		CreateDirectory(tempFileName, nullptr);
		tempFileName += L"\\pullrequest.txt";
		if (CString err; GitCall::RunLogFile(cmd, tempFileName, &err))
		{
			sysProgressDlg.Stop();
			CString msg;
			msg.LoadString(IDS_ERR_PULLREUQESTFAILED);
			MessageBox(hWnd, msg + L'\n' + err, L"TortoiseGit", MB_OK | MB_ICONERROR);
			return false;
		}

		if (sysProgressDlg.HasUserCancelled())
		{
			sysProgressDlg.Stop();
			CMessageBox::Show(hWnd, IDS_USERCANCELLED, IDS_APPNAME, MB_OK);
			::DeleteFile(tempFileName);
			return false;
		}

		sysProgressDlg.Stop();

		LaunchAlternativeEditor(tempFileName);
	}
	return true;
}

bool DoPush(HWND hWnd, bool tags, bool allRemotes, bool allBranches, bool force, bool forceWithLease, const CString& localBranch, const CString& remote, const CString& remoteBranch, bool setUpstream, int recurseSubmodules, const CString& pushOption)
{
	CString error;
	DWORD exitcode = 0xFFFFFFFF;
	ProjectProperties pp;
	pp.ReadProps();
	CHooks::Instance().SetProjectProperties(CTGitPath(GitCall::m_CurrentDirStorage.native()), pp);
	if (CHooks::Instance().PrePush(hWnd, CString(GitCall::m_CurrentDirStorage.c_str()), exitcode, error))
	{
		if (exitcode)
		{
			CString sErrorMsg;
			sErrorMsg.Format(IDS_HOOK_ERRORMSG, static_cast<LPCWSTR>(error));
			CTaskDialog taskdlg(sErrorMsg, CString(MAKEINTRESOURCE(IDS_HOOKFAILED_TASK2)), L"TortoiseGit", 0, TDF_ENABLE_HYPERLINKS | TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT);
			taskdlg.AddCommandControl(101, CString(MAKEINTRESOURCE(IDS_HOOKFAILED_TASK3)));
			taskdlg.AddCommandControl(102, CString(MAKEINTRESOURCE(IDS_HOOKFAILED_TASK4)));
			taskdlg.SetDefaultCommandControl(101);
			taskdlg.SetMainIcon(TD_ERROR_ICON);
			if (taskdlg.DoModal(hWnd) != 102)
				return false;
		}
	}

	int iRecurseSubmodules = 0;
	CString sRecurseSubmodules = g_Git.GetConfigValue(L"push.recurseSubmodules");
	if (sRecurseSubmodules == L"check")
		iRecurseSubmodules = 1;
	else if (sRecurseSubmodules == L"on-demand")
		iRecurseSubmodules = 2;

	std::vector<std::wstring> arg;
	if (tags && !allBranches)
		arg.emplace_back(L"--tags");
	if (force)
		arg.emplace_back(L"--force");
	if (forceWithLease)
		arg.emplace_back(L"--force-with-lease");
	if (setUpstream)
		arg.emplace_back(L"--set-upstream");
	if (recurseSubmodules == 0 && recurseSubmodules != iRecurseSubmodules)
		arg.emplace_back(L"--recurse-submodules=no");
	if (recurseSubmodules == 1 && recurseSubmodules != iRecurseSubmodules)
		arg.emplace_back(L"--recurse-submodules=check");
	if (recurseSubmodules == 2 && recurseSubmodules != iRecurseSubmodules)
		arg.emplace_back(L"--recurse-submodules=on-demand");
	if (!pushOption.IsEmpty())
		arg.emplace_back(std::format(L"--push-option={}", pushOption));

	arg.emplace_back(L"--progress");

	CProgressDlg progress(GetParentCWnd(hWnd));

	std::vector<std::wstring> remotesList;
	if (allRemotes)
		g_Git.GetRemoteList(remotesList);
	else
		remotesList.push_back(std::wstring(remote));

	for (unsigned int i = 0; i < remotesList.size(); ++i)
	{
		// "git.exe push <what> <arg...> -- <remote> [<refspec>]"
		const auto pushTo = [&](const wchar_t* what) {
			std::vector<std::wstring> cmd{ L"git.exe", L"push" };
			if (what)
				cmd.emplace_back(what);
			cmd.insert(cmd.cend(), arg.cbegin(), arg.cend());
			cmd.insert(cmd.cend(), { L"--", remotesList[i] });
			return cmd;
		};

		if (allBranches)
		{
			progress.m_GitCmdList.push_back(pushTo(L"--all"));
			if (tags)
				progress.m_GitCmdList.push_back(pushTo(L"--tags"));
		}
		else
		{
			CString branch = localBranch;
			if (!remoteBranch.IsEmpty())
			{
				branch += L":";
				branch += remoteBranch;
			}
			std::vector<std::wstring> cmd = pushTo(nullptr);
			// The refspec used to interpolate as nothing when empty.
			if (!branch.IsEmpty())
				cmd.emplace_back(std::wstring(branch));
			progress.m_GitCmdList.push_back(std::move(cmd));
		}

		if (!allBranches && !!CRegDWORD(L"Software\\TortoiseGit\\ShowBranchRevisionNumber", FALSE))
			progress.m_GitCmdList.push_back({ L"git.exe", L"rev-list", L"--count", L"--first-parent", L"--end-of-options", std::wstring(localBranch), L"--" });
	}

	// the working tree *around* this one (the old bDir=false lookup started from the
	// parent): from the current dir itself it would find its own .git
	std::wstring superprojectRoot;
	if (const fs::path& currentDir = GitCall::m_CurrentDirStorage; currentDir.has_relative_path())
		superprojectRoot = GitAdminDir::HasAdminDir(currentDir.parent_path()).native();
	progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
	{
		// need to execute hooks as those might be needed by post action commands
		DWORD exitcode_ = 0xFFFFFFFF;
		CString error_;
		ProjectProperties pp_;
		pp_.ReadProps();
		CHooks::Instance().SetProjectProperties(CTGitPath(GitCall::m_CurrentDirStorage.native()), pp_);
		if (CHooks::Instance().PostPush(hWnd, CString(GitCall::m_CurrentDirStorage.c_str()), exitcode_, error_))
		{
			if (exitcode_)
			{
				CString temp;
				temp.Format(IDS_ERR_HOOKFAILED, static_cast<LPCWSTR>(error_));
				MessageBox(hWnd, temp, L"TortoiseGit", MB_OK | MB_ICONERROR);
			}
		}

		if (status)
		{
			bool rejected = progress.GetLogText().Find(L"! [rejected]") > 0;
			if (rejected)
			{
				postCmdList.emplace_back(IDI_PULL, IDS_MENUPULL, [&hWnd]{ Pull(hWnd, true); });
				postCmdList.emplace_back(IDI_UPDATE, IDS_MENUFETCH, [&]{ Fetch(hWnd, allRemotes ? CString() : remote, allRemotes); });
			}
			postCmdList.emplace_back(IDI_PUSH, IDS_MENUPUSH, [&]{ Push(hWnd, localBranch); });
			return;
		}

		postCmdList.emplace_back(IDS_PROC_REQUESTPULL, [&]{ RequestPull(hWnd, remoteBranch); });
		postCmdList.emplace_back(IDI_PUSH, IDS_MENUPUSH, [&]{ Push(hWnd, localBranch); });
		postCmdList.emplace_back(IDI_SWITCH, IDS_MENUSWITCH, [&hWnd]{ Switch(hWnd); });
		if (!superprojectRoot.empty())
		{
			postCmdList.emplace_back(IDI_COMMIT, IDS_PROC_COMMIT_SUPERPROJECT, [&]
			{
				CString sCmd;
				sCmd.Format(L"/command:commit /path:\"%s\"", superprojectRoot.c_str());
				RunTortoiseGitProc(sCmd);
			});
		}
	};

	return progress.DoModal() == IDOK;
}

bool Push(HWND hWnd, const CString& selectLocalBranch /* = {} */, const int pushAll /* = BST_INDETERMINATE */)
{
	CPushDlg dlg(GetParentCWnd(hWnd));
	dlg.m_BranchSourceName = selectLocalBranch;
	if (pushAll == BST_CHECKED)
		dlg.m_bPushAllBranches = pushAll;

	if (dlg.DoModal() == IDOK)
		return DoPush(hWnd, !!dlg.m_bTags, !!dlg.m_bPushAllRemotes, !!dlg.m_bPushAllBranches, !!dlg.m_bForce, !!dlg.m_bForceWithLease, dlg.m_BranchSourceName, dlg.m_URL, dlg.m_BranchRemoteName, !!dlg.m_bSetUpstream, dlg.m_RecurseSubmodules, dlg.m_sPushOption);

	return FALSE;
}

bool CreateBranchTag(HWND hWnd, const bool isTag = true, const CString* ref = nullptr, const bool switchNewBranch = false, const LPCWSTR& name = nullptr)
{
	CCreateBranchTagDlg dlg(GetParentCWnd(hWnd));
	dlg.m_bIsTag = isTag;
	dlg.m_bSwitch = switchNewBranch;

	if (ref)
		dlg.m_initialRefName = *ref;

	if (name)
		dlg.m_BranchTagName = name;

	if(dlg.DoModal()==IDOK)
	{
		std::vector<std::wstring> args;
		if(dlg.m_bForce)
			args.emplace_back(L"-f");

		if (isTag)
		{
			if(dlg.m_bSign)
				args.emplace_back(L"-s");

			if (!dlg.m_Message.Trim().IsEmpty())
			{
				CString tempfile = GitCall::GetTempFile();
				if (tempfile.IsEmpty() || SaveCommitUnicodeFile(tempfile, dlg.m_Message))
				{
					MessageBox(hWnd, L"Could not save tag message", L"TortoiseGit", MB_OK | MB_ICONERROR);
					return FALSE;
				}
				// The temp path is a Windows path, and the QuoteParameter this
				// replaces was converting its backslashes.
				args.insert(args.cend(), { std::wstring(L"-F"), CTGitPath(tempfile.GetString()).GetGitPathString() });
			}
		}
		else
		{
			if (dlg.m_bTrack == TRUE)
				args.emplace_back(L"--track");
			else if (dlg.m_bTrack == FALSE)
				args.emplace_back(L"--no-track");
		}

		std::vector<std::wstring> cmd{ L"git.exe", isTag ? L"tag" : L"branch" };
		cmd.insert(cmd.cend(), args.cbegin(), args.cend());
		cmd.insert(cmd.cend(), { std::wstring(L"--"), std::wstring(dlg.m_BranchTagName), std::wstring(g_Git.FixBranchName(dlg.m_VersionName)) });

		CString out;
		if (GitCall::Run(cmd, &out, CP_UTF8))
		{
			MessageBox(hWnd, out, L"TortoiseGit", MB_OK | MB_ICONERROR);
			return FALSE;
		}
		if (!isTag && dlg.m_bSwitch)
		{
			// it is a new branch and the user has requested to switch to it
			PerformSwitch(hWnd, dlg.m_BranchTagName);
		}
		if (!isTag && !dlg.m_Message.IsEmpty())
			UpdateBranchDescription(dlg.m_BranchTagName, dlg.m_Message);
		if (isTag && dlg.m_bPush == BST_CHECKED)
			Push(hWnd, L"refs/tags/" + dlg.m_BranchTagName);

		return TRUE;
	}
	return FALSE;
}

BOOL Commit(
	HWND hWnd,
	const CString& bugid,
	BOOL bWholeProject,
	CString& sLogMsg,
	const CTGitPathList& pathList,
	bool bSelectFilesForCommit
) {
	bool bFailed = true;

	if (!CheckUserData(hWnd))
		return false;

	while (bFailed)
	{
		bFailed = false;
		CCommitDlg dlg(GetParentCWnd(hWnd));
		dlg.m_sBugID = bugid;

		dlg.m_bWholeProject = bWholeProject;

		dlg.m_sLogMessage = sLogMsg;
		dlg.m_pathList = pathList;
		dlg.m_bSelectFilesForCommit = bSelectFilesForCommit;
		if (dlg.DoModal() == IDOK)
		{
			if (dlg.m_pathList.IsEmpty())
				return false;
			// if the user hasn't changed the list of selected items
			// we don't use that list. Because if we would use the list
			// of pre-checked items, the dialog would show different
			// checked items on the next startup: it would only try
			// to check the parent folder (which might not even show)
			// instead, we simply use an empty list and let the
			// default checking do its job.
			sLogMsg = dlg.m_sLogMessage;
			bSelectFilesForCommit = true;

			// explorer might have been closed
			if (!::IsWindow(hWnd))
				hWnd = nullptr;

			switch (dlg.m_PostCmd)
			{
			case Git_PostCommit_Cmd::DCommit:
				SVNDCommit(hWnd);
				break;
			case Git_PostCommit_Cmd::Push:
				Push(hWnd);
				break;
			case Git_PostCommit_Cmd::CreateTag:
				CreateBranchTag(hWnd, TRUE);
				break;
			case Git_PostCommit_Cmd::Pull:
				Pull(hWnd, true);
				break;
			default:
				break;
			}

			//			CGitProgressDlg progDlg;
			//			progDlg.SetChangeList(dlg.m_sChangeList, !!dlg.m_bKeepChangeList);
			//			if (parser.HasVal(L"closeonend"))
			//				progDlg.SetAutoClose(parser.GetLongVal(L"closeonend"));
			//			progDlg.SetCommand(CGitProgressDlg::GitProgress_Commit);
			//			progDlg.SetPathList(dlg.m_pathList);
			//			progDlg.SetCommitMessage(dlg.m_sLogMessage);
			//			progDlg.SetDepth(dlg.m_bRecursive ? Git_depth_infinity : svn_depth_empty);
			//			progDlg.SetSelectedList(dlg.m_selectedPathList);
			//			progDlg.SetItemCount(dlg.m_itemsCount);
			//			progDlg.SetBugTraqProvider(dlg.m_BugTraqProvider);
			//			progDlg.DoModal();
			//			CRegDWORD err = CRegDWORD(L"Software\\TortoiseGit\\ErrorOccurred", FALSE);
			//			err = static_cast<DWORD>(progDlg.DidErrorsOccur());
			//			bFailed = progDlg.DidErrorsOccur();
			//			bRet = progDlg.DidErrorsOccur();
			//			CRegDWORD bFailRepeat = CRegDWORD(L"Software\\TortoiseGit\\CommitReopen", FALSE);
			//			if (DWORD(bFailRepeat)==0)
			//				bFailed = false;		// do not repeat if the user chose not to in the settings.
		}
	}
	return true;
}

bool DoMerge(HWND hWnd, bool noFF, bool ffOnly, bool squash, bool noCommit, const int* log, bool unrelated, const CString& mergeStrategy, const CString& strategyOption, const CString& strategyParam, const CString& logMessage, const CString& version, bool isBranch, bool showStashPop)
{
	std::vector<std::wstring> cmd{ L"git.exe", L"merge" };
	if (noFF)
		cmd.emplace_back(L"--no-ff");
	else if (ffOnly)
		cmd.emplace_back(L"--ff-only");

	if (squash)
		cmd.emplace_back(L"--squash");

	if (noCommit)
		cmd.emplace_back(L"--no-commit");

	if (unrelated)
		cmd.emplace_back(L"--allow-unrelated-histories");

	if (log)
		cmd.emplace_back(std::format(L"--log={}", *log));

	if (!mergeStrategy.IsEmpty())
	{
		cmd.emplace_back(std::format(L"--strategy={}", mergeStrategy));
		if (!strategyOption.IsEmpty())
		{
			// "--strategy-option=<opt>" or "--strategy-option=<opt>=<param>", one element either way
			CString option = L"--strategy-option=" + strategyOption;
			if (!strategyParam.IsEmpty())
				option += L'=' + strategyParam;
			cmd.emplace_back(std::wstring(option));
		}
	}

	if (!logMessage.IsEmpty())
	{
		CString logmsg = logMessage;
		CString tempfile = GitCall::GetTempFile();
		if (tempfile.IsEmpty() || SaveCommitUnicodeFile(tempfile, logmsg))
		{
			MessageBox(hWnd, L"Could not save merge message", L"TortoiseGit", MB_OK | MB_ICONERROR);
			return FALSE;
		}
		cmd.insert(cmd.cend(), { L"-F", std::wstring(tempfile) });
	}

	CString mergeVersion = g_Git.FixBranchName(version);
	cmd.insert(cmd.cend(), { L"--", std::wstring(mergeVersion) });

	CProgressDlg Prodlg(GetParentCWnd(hWnd));
	Prodlg.m_GitCmd = cmd;

	Prodlg.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList)
	{
		if (status)
		{
			int hasConflicts = g_Git.HasWorkingTreeConflicts();
			if (hasConflicts < 0)
				CMessageBox::Show(hWnd, g_Git.GetGitLastErr(L"Checking for conflicts failed.", TGitH::GIT_CMD_CHECKCONFLICTS), L"TortoiseGit", MB_ICONEXCLAMATION);
			else if (hasConflicts)
			{
				// there are conflict files
				CMessageBox::ShowCheck(hWnd, IDS_NEED_TO_RESOLVE_CONFLICTS_HINT, IDS_APPNAME, MB_ICONINFORMATION, L"MergeConflictsNeedsCommit", IDS_MSGBOX_DONOTSHOWAGAIN);
				postCmdList.emplace_back(IDI_RESOLVE, IDS_PROGRS_CMD_RESOLVE, []
				{
					CString sCmd;
					sCmd.Format(L"/command:resolve /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
					RunTortoiseGitProc(sCmd);
				});

				postCmdList.emplace_back(IDI_COMMIT, IDS_MENUCOMMIT, []
				{
					CString sCmd;
					sCmd.Format(L"/command:commit /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
					RunTortoiseGitProc(sCmd);
				});
			}

			CGitHash common;
			g_Git.IsFastForward(L"HEAD", mergeVersion, &common);
			if (common.IsEmpty())
				postCmdList.emplace_back(IDI_MERGE, IDS_MERGE_UNRELATED, [=, &hWnd] { DoMerge(hWnd, noFF, ffOnly, squash, noCommit, log, true, mergeStrategy, strategyOption, strategyParam, logMessage, version, isBranch, showStashPop); });

			postCmdList.emplace_back(IDI_SHELVE, IDS_MENUSTASHSAVE, [mergeVersion, &hWnd]{ StashSave(hWnd, L"", false, false, true, mergeVersion); });

			return;
		}

		if (showStashPop)
			postCmdList.emplace_back(IDI_UNSHELVE, IDS_MENUSTASHPOP, [&hWnd]{ StashPop(hWnd); });

		if (noCommit || squash)
		{
			postCmdList.emplace_back(IDI_COMMIT, IDS_MENUCOMMIT, []
			{
				CString sCmd;
				sCmd.Format(L"/command:commit /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
				RunTortoiseGitProc(sCmd);
			});
			return;
		}

		if (isBranch && !StringUtils::StartsWith(version, L"remotes/")) // do not ask to remove remote branches
		{
			postCmdList.emplace_back(IDI_DELETE, IDS_PROC_REMOVEBRANCH, [&]
			{
				CString msg;
				msg.Format(IDS_PROC_DELETEBRANCHTAG, static_cast<LPCWSTR>(version));
				if (CMessageBox::Show(hWnd, msg, L"TortoiseGit", 2, IDI_QUESTION, CString(MAKEINTRESOURCE(IDS_DELETEBUTTON)), CString(MAKEINTRESOURCE(IDS_ABORTBUTTON))) == 1)
				{
					CString out;
					if (GitCall::Run({ L"git.exe", L"branch", L"-D", L"--", std::wstring(version) }, &out, CP_UTF8))
						MessageBox(hWnd, out, L"TortoiseGit", MB_OK);
				}
			});
		}
		if (isBranch)
			postCmdList.emplace_back(IDI_PUSH, IDS_MENUPUSH, [&hWnd]{ Push(hWnd); });

		BOOL hasGitSVN = CTGitPath(GitCall::m_CurrentDirStorage.native()).GetAdminDirMask() & ITEMIS_GITSVN;
		if (hasGitSVN)
			postCmdList.emplace_back(IDI_COMMIT, IDS_MENUSVNDCOMMIT, [&hWnd]{ SVNDCommit(hWnd); });
	};

	if (Prodlg.DoModal() == IDCANCEL && g_Git.HasWorkingTreeConflicts() == 1)
	{
		MergeAbort(hWnd);
		return false;
	}
	return !Prodlg.m_GitStatus;
}


BOOL Merge(HWND hWnd, const CString* commit, bool showStashPop)
{
	if (!CheckUserData(hWnd))
		return FALSE;

	if (IsTGitRebaseActive(hWnd))
		return FALSE;

	CMergeDlg dlg(GetParentCWnd(hWnd));
	if (commit)
		dlg.m_initialRefName = *commit;

	if (dlg.DoModal() == IDOK)
		return DoMerge(hWnd, dlg.m_bNoFF == BST_CHECKED, dlg.m_bFFonly == BST_CHECKED, dlg.m_bSquash == BST_CHECKED, dlg.m_bNoCommit == BST_CHECKED, dlg.m_bLog ? &dlg.m_nLog : nullptr, false, dlg.m_MergeStrategy, dlg.m_StrategyOption, dlg.m_StrategyParam, dlg.m_strLogMesage, dlg.m_VersionName, dlg.m_bIsBranch, showStashPop);

	return FALSE;
}

}
