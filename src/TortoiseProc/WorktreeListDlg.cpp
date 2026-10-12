// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2022-2026 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// CWorktreeListDlg.cpp : implementation file
//

#include "stdafx.h"
#include "TortoiseProc.h"
#include "WorktreeListDlg.h"
#include "ProgressDlg.h"
#include "AppUtils.h"
#include "IconMenu.h"
import str_utils;
#include "SysImageList.h"
import PathUtils;
#include "MessageBox.h"

// CWorktreeListDlg dialog

IMPLEMENT_DYNAMIC(CWorktreeListDlg, CResizableStandAloneDialog)

BEGIN_MESSAGE_MAP(CWorktreeListDlg, CResizableStandAloneDialog)
	ON_WM_SETCURSOR()
	ON_BN_CLICKED(IDC_BUTTON_ADD, &CWorktreeListDlg::OnBnClickedButtonAdd)
	ON_BN_CLICKED(IDC_BUTTON_PRUNE, &CWorktreeListDlg::OnBnClickedButtonPrune)
	ON_NOTIFY(NM_DBLCLK, IDC_WORKTREE_LIST, OnNMDblclkWorktreeList)
END_MESSAGE_MAP()

// CWorktreeListDlg message handlers

BOOL CWorktreeListDlg::OnInitDialog()
{
	CResizableStandAloneDialog::OnInitDialog();
	CAppUtils::MarkWindowAsUnpinnable(m_hWnd);

	CAppUtils::SetWindowTitle(*this, g_Git.m_CurrentDir);

	AddAnchor(IDC_WORKTREE_LIST, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_BUTTON_ADD, BOTTOM_LEFT);
	AddAnchor(IDC_BUTTON_PRUNE, BOTTOM_LEFT);
	AddAnchor(IDHELP, BOTTOM_RIGHT);
	AddAnchor(IDOK, BOTTOM_RIGHT);

	const int columnWidth = CDPIAware::Instance().ScaleX(GetSafeHwnd(), 100);
	const ColumnDefinition columns[] = {
		{ eCol_Path, IDS_STATUSLIST_COLFILE, true, true, CDPIAware::Instance().ScaleX(GetSafeHwnd(), 150) },
		{ eCol_Hash, IDS_HASH, true, true, columnWidth },
		{ eCol_Branch, IDS_PROC_BRANCH, true, true, columnWidth },
		{ eCol_Locked, IDS_LOCKED, true, true, columnWidth },
		{ eCol_Reason, IDS_REASON, true, true, columnWidth },
	};
	constexpr int columnVersion = 0; // adjust when changing number/names/etc. of columns
	m_WorktreeList.m_ColumnManager.ReadSettings(columns, L"WorktreeList", columnVersion);
	m_WorktreeList.m_ColumnManager.SetRightAlign(eCol_Reason);

	// set up the list control
	// set the extended style of the list control
	m_WorktreeList.Init();
	m_WorktreeList.SetExtendedStyle(m_WorktreeList.GetExtendedStyle() | LVS_EX_DOUBLEBUFFER | LVS_EX_SUBITEMIMAGES);
	m_WorktreeList.SetImageList(&SYS_IMAGE_LIST(), LVSIL_SMALL);
	m_WorktreeList.SetListContextMenuHandler([&](CPoint point) { OnContextMenu_WorktreeList(point); });
	m_nIconFolder = SYS_IMAGE_LIST().GetDirIconIndex();

	CAppUtils::SetListCtrlBackgroundImage(m_WorktreeList.GetSafeHwnd(), IDI_REPOBROWSER_BKG);

	EnableSaveRestore(L"WorktreeList");
	SetTheme(CTheme::Instance().IsDarkTheme());

	Refresh();

	m_WorktreeList.SetFocus();

	return FALSE;
}

void CWorktreeListDlg::OnOK()
{
	CResizableStandAloneDialog::OnOK();
}

void CWorktreeListDlg::Refresh()
{
	BeginWaitCursor();
	m_WorktreeList.DeleteAllItems();
	m_Worktrees.clear();

	if (CString git_err; FillListCtrlWithWorktreeList(git_err) < 0)
		MessageBox(git_err, L"TortoiseGit", MB_OK | MB_ICONERROR);
	EndWaitCursor();
}

int CWorktreeListDlg::GetWorktreeNames(std::vector<std::wstring>& list, CString& error)
{
	if (!g_Git.m_IsUseLibGit2)
		return -1;

	const wil::unique_git_repository repo(g_Git.GetGitRepository());
	if (!repo)
	{
		error = TGitH::GetLibGit2LastErr();
		return -1;
	}

	wil::unique_git_strarray worktrees;
	if (git_worktree_list(&worktrees, repo.get()) < 0)
	{
		error = TGitH::GetLibGit2LastErr();
		return -1;
	}

	TGitH::AppendTo(worktrees, list);

	return 0;
}

int CWorktreeListDlg::FillListCtrlWithWorktreeList(CString& error)
{
	std::vector<std::wstring> list;
	if (GetWorktreeNames(list, error) < 0)
		return -1;

	const wil::unique_git_repository repo(g_Git.GetGitRepository());
	if (!repo)
	{
		error = TGitH::GetLibGit2LastErr();
		return -1;
	}

	// Base repo (not worktree)
	{
		git_repository* baseRepo = repo.get();
		wil::unique_git_repository autoBaseRepo;
		if (git_repository_is_worktree(repo.get()))
		{
			// Open()'s result was ignored here, and still is: on failure baseRepo is null
			// and git_repository_head below reports the error.
			autoBaseRepo = wil::OpenGitRepository(git_repository_commondir(repo.get()));
			baseRepo = autoBaseRepo.get();
		}

		wil::unique_git_reference nonWorktreeHead;
		if (git_repository_head(nonWorktreeHead.put(), baseRepo) < 0)
		{
			error = TGitH::GetLibGit2LastErr();
			return -1;
		}
		CString workDirPath;
		if (git_repository_is_bare(baseRepo))
			workDirPath = CUnicodeUtils::Convert2Unicode<CString>(git_repository_commondir(baseRepo));
		else
			workDirPath = CUnicodeUtils::Convert2Unicode<CString>(git_repository_workdir(baseRepo));
		workDirPath.Replace(L'/', L'\\');
		workDirPath.TrimRight(L'\\');
		WorktreeDetails worktreeDetails(L"", workDirPath, CGitHash(git_reference_target(nonWorktreeHead.get())),CUnicodeUtils::Convert2Unicode<CString>(git_reference_shorthand(nonWorktreeHead.get())));
		worktreeDetails.m_isBaseRepo = TRUE;
		m_Worktrees.push_back(worktreeDetails);
	}

	for (size_t i = 0; i < list.size(); ++i)
	{
		wil::unique_git_worktree worktree;
		if (git_worktree_lookup(worktree.put(), repo.get(), CUnicodeUtils::StdGetUTF8(list[i]).c_str()) < 0)
		{
			error = TGitH::GetLibGit2LastErr();
			return -1;
		}
		CString worktreePath = CUnicodeUtils::Convert2Unicode<CString>(git_worktree_path(worktree.get()));
		worktreePath.Replace(L'/', L'\\');

		wil::unique_git_buf reason;
		unsigned int locked = git_worktree_is_locked(&reason, worktree.get());
		if (locked < 0)
		{
			error = TGitH::GetLibGit2LastErr();
			return -1;
		}

		git_worktree_prune_options prune_opts = GIT_WORKTREE_PRUNE_OPTIONS_INIT;
		// Don't need to let it check for locked worktree, we can check that ourself
		prune_opts.flags = GIT_WORKTREE_PRUNE_LOCKED;
		if (int pruneable = git_worktree_is_prunable(worktree.get(), &prune_opts); pruneable < 0)
		{
			error = TGitH::GetLibGit2LastErr();
			return -1;
		}
		// Can't get information about HEAD and so on because worktree checkout doesn't exist
		else if (pruneable == 1)
		{
			WorktreeDetails worktreeDetails(list[i].c_str(), worktreePath, CGitHash(), L"");
			worktreeDetails.m_isBaseRepo = FALSE;
			worktreeDetails.m_IsLocked = locked;
			// `locked && reason` was always `locked`: the buffer converted to its own
			// (never null) git_buf*. ToString() was the bytes, UTF-8 decoded.
			if (locked)
				worktreeDetails.m_LockedReason = CUnicodeUtils::GetUnicode<CString>(std::string_view(reason.ptr, reason.size));
			m_Worktrees.push_back(worktreeDetails);
			continue;
		}

		wil::unique_git_reference head;
		if (git_repository_head_for_worktree(head.put(), repo.get(), CUnicodeUtils::StdGetUTF8(list[i]).c_str()) < 0)
		{
			error = TGitH::GetLibGit2LastErr();
			return -1;
		}

		CString branch = CUnicodeUtils::Convert2Unicode<CString>(git_reference_shorthand(head.get()));
		if (branch == L"HEAD")
		{
			// `git worktree list` command says "detached HEAD", so we should say the same
			branch = L"detached HEAD";
		}

		WorktreeDetails worktreeDetails(list[i].c_str(), worktreePath, CGitHash(git_reference_target(head.get())), branch);
		worktreeDetails.m_isBaseRepo = FALSE;
		worktreeDetails.m_IsLocked = locked;
		if (locked)
			worktreeDetails.m_LockedReason = CUnicodeUtils::GetUnicode<CString>(std::string_view(reason.ptr, reason.size));
		m_Worktrees.push_back(worktreeDetails);
	}

	for (const auto& zfb : m_Worktrees)
	{
		const int indexItem = m_WorktreeList.InsertItem(m_WorktreeList.GetItemCount(), zfb.m_Path, m_nIconFolder);
		if (zfb.m_Hash.IsEmpty())
			m_WorktreeList.SetItemText(indexItem, eCol_Hash, L"");
		else
			m_WorktreeList.SetItemText(indexItem, eCol_Hash, (zfb.m_Hash.ToString()).c_str());
		m_WorktreeList.SetItemText(indexItem, eCol_Branch, zfb.m_Branch);

		if (zfb.m_isBaseRepo)
			continue;

		CString lockStr;
		if (zfb.m_IsLocked)
		{
			lockStr.LoadString(IDS_LOCKED);
			m_WorktreeList.SetItemText(indexItem, eCol_Reason, zfb.m_LockedReason);
		}
		else
			lockStr.LoadString(IDS_UNLOCKED);
		m_WorktreeList.SetItemText(indexItem, eCol_Locked, lockStr);
	}

	return 0;
}

void CWorktreeListDlg::OnContextMenu_WorktreeList(CPoint point)
{
	std::vector<int> indexes;

	POSITION pos = m_WorktreeList.GetFirstSelectedItemPosition();
	while (pos)
	{
		int index = m_WorktreeList.GetNextSelectedItem(pos);
		indexes.push_back(index);
	}

	ShowContextMenu(point, indexes);
}

void CWorktreeListDlg::OnNMDblclkWorktreeList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	int selIndex = pNMLV->iItem;
	if (selIndex < 0)
		return;
	if (selIndex >= static_cast<int>(m_Worktrees.size()))
		return;

	CAppUtils::ExploreTo(GetSafeHwnd(), m_Worktrees[selIndex].m_Path);
}

void CWorktreeListDlg::ShowContextMenu(CPoint point, std::vector<int>& indexes)
{
	// When selecting multiple worktrees, allow any modification action
	bool showLock = true;
	bool showUnlock =true;
	bool showRemove = true;

	if (indexes.empty())
		return;

	// If there's only one and we can't operate on it, fail.
	// If there are multiple and some of them can't be operated on, just ignore those.
	if (indexes.size() == 1)
	{
		auto& worktree = m_Worktrees[indexes[0]];
		showUnlock = worktree.m_IsLocked;
		showLock = !showUnlock;

		// Can't delete or lock the base repo, only worktrees
		if (worktree.m_isBaseRepo)
			showRemove = false;
	}

	CIconMenu popupMenu;
	popupMenu.CreatePopupMenu();

	if (indexes.size() == 1)
	{
		popupMenu.AppendMenuIcon(eCmd_Open, IDS_STATUSLIST_CONTEXT_EXPLORE, IDI_EXPLORER);
		popupMenu.SetDefaultItem(eCmd_Open, FALSE);
	}
	if (showLock)
		popupMenu.AppendMenuIcon(eCmd_Lock, IDS_MENULFSLOCK, IDI_LFSLOCK);
	if (showUnlock)
		popupMenu.AppendMenuIcon(eCmd_Unlock, IDS_MENULFSUNLOCK, IDI_LFSUNLOCK);
	if (showRemove)
	{
		popupMenu.AppendMenuIcon(eCmd_Remove, IDS_REMOVEBUTTON, IDI_DELETE);
		popupMenu.AppendMenuIcon(eCmd_RemoveWithForce, IDS_REMOVEWITHFORCE, IDI_DELETE);
	}

	eCmd cmd = static_cast<eCmd>(popupMenu.TrackPopupMenuEx(TPM_LEFTALIGN | TPM_RETURNCMD, point.x, point.y, this, nullptr));
	switch (cmd)
	{
		case eCmd_Open:
		{
			if (indexes.size() != 1)
				return;
			CAppUtils::ExploreTo(GetSafeHwnd(), m_Worktrees[indexes[0]].m_Path);
			break;
		}
		case eCmd_Remove:
		case eCmd_RemoveWithForce:
		{
			if (CMessageBox::Show(GetSafeHwnd(), IDS_PROC_DELETE_WORKTREE, IDS_APPNAME, 2, IDI_QUESTION, IDS_MSGBOX_YES, IDS_MSGBOX_NO) == 2)
				return;
			for (int index : indexes)
			{
				auto& zf = m_Worktrees[index];

				if (zf.m_isBaseRepo)
				{
					// Silently skip it. It shouldn't be selectable by itself.
					// If included in a multi-selection, just silently skip it.
					continue;
				}

				if (!RemoveWorktree(zf.m_Path, cmd == eCmd_RemoveWithForce))
					break; // Don't continue with other worktrees
			}

			Refresh();
			break;
		}
		case eCmd_Lock:
		{
			// TODO: Show dialog to enter reason
			int lockedSuccessfully = 0;
			for (int index : indexes)
			{
				auto& leaf = m_Worktrees[index];
				if (leaf.m_isBaseRepo)
					continue;

				wil::unique_git_worktree worktree;
				if (git_worktree_lookup(worktree.put(), g_Git.GetGitRepository().get(), CUnicodeUtils::GetUTF8<CStringA>(leaf.m_WorktreeName)) < 0 || git_worktree_lock(worktree.get(), nullptr) < 0)
				{
					if (CMessageBox::Show(GetSafeHwnd(), TGitH::GetLibGit2LastErr(L"Failed to lock worktree \"" + leaf.m_WorktreeName + L"\"."), IDS_APPNAME, 1, IDI_ERROR, IDS_MSGBOX_CONTINUE, IDS_MSGBOX_ABORT) == 2)
						break;
				}
				else
					++lockedSuccessfully;
			}
			CString msg;
			msg.Format(IDS_PROC_LOCKED_WORKTREES, lockedSuccessfully);
			MessageBox(msg, L"TortoiseGit", MB_OK| MB_ICONINFORMATION);
			Refresh();
			break;
		}
		case eCmd_Unlock:
		{
			int unlockedSuccessfully = 0;
			for (int index : indexes)
			{
				auto& leaf = m_Worktrees[index];
				if (leaf.m_isBaseRepo)
					continue;

				wil::unique_git_worktree worktree;
				if (git_worktree_lookup(worktree.put(), g_Git.GetGitRepository().get(), CUnicodeUtils::GetUTF8<CStringA>(leaf.m_WorktreeName)) < 0 || git_worktree_unlock(worktree.get()) < 0)
				{
					if (CMessageBox::Show(GetSafeHwnd(), TGitH::GetLibGit2LastErr(L"Failed to unlock worktree \"" + leaf.m_WorktreeName + L"\"."), IDS_APPNAME, 1, IDI_ERROR, IDS_MSGBOX_CONTINUE, IDS_MSGBOX_ABORT) == 2)
						break;
				}
				else
					++unlockedSuccessfully;
			}
			CString msg;
			msg.Format(IDS_PROC_UNLOCKED_WORKTREES, unlockedSuccessfully);
			MessageBox(msg, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
			Refresh();
			break;
		}
	}
}

BOOL CWorktreeListDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		switch (pMsg->wParam)
		{
			case VK_F5:
				Refresh();
				break;
		}
	}

	return CResizableStandAloneDialog::PreTranslateMessage(pMsg);
}

void CWorktreeListDlg::OnBnClickedButtonAdd()
{
	if (CAppUtils::CreateWorktree(GetSafeHwnd()))
		Refresh();
}

bool CWorktreeListDlg::RemoveWorktree(const CString& path, bool force)
{
	std::vector<std::wstring> cmd{ L"git.exe", L"worktree", L"remove" };
	if (force)
		cmd.emplace_back(L"--force");
	cmd.insert(cmd.cend(), { L"--", std::wstring(path) });

	CProgressDlg progress;
	progress.m_GitCmd = cmd;
	progress.m_PostCmdCallback = [&](DWORD status, PostCmdList& postCmdList) {
		if (status && !force)
			postCmdList.emplace_back(IDI_DELETE, IDS_REMOVEWITHFORCE, [&] { RemoveWorktree(path, true); });
	};

	return progress.DoModal() == IDOK;
}

bool CWorktreeListDlg::PruneWorktrees()
{
	CProgressDlg progress;
	progress.m_GitCmd = { L"git.exe", L"worktree", L"prune" };
	return progress.DoModal() == IDOK;
}

void CWorktreeListDlg::OnBnClickedButtonPrune()
{
	if (PruneWorktrees())
		Refresh();
}
