// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2019-2021, 2023, 2025-2026 - TortoiseGit

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

#include "stdafx.h"
#include "TortoiseProc.h"
#include "LFSSetLockedProgressCommand.h"
#include "AppUtils.h"
#include "TempFile.h"

using Git_WC_Notify_Action = CGitProgressList::WC_File_NotificationData::Git_WC_Notify_Action;

bool LFSSetLockedProgressCommand::Run(CGitProgressList* list, CString& sWindowTitle, int& m_itemCountTotal, int& m_itemCount)
{
	m_itemCountTotal = m_targetPathList.GetCount();
	m_itemCount = 0;

	list->SetWindowTitle(m_bIsLock ? IDS_PROGRS_TITLE_LFS_LOCK : IDS_PROGRS_TITLE_LFS_UNLOCK, g_Git.CombinePath(m_targetPathList.GetCommonRoot().GetUIPathString().c_str()), sWindowTitle);
	list->SetBackgroundImage(m_bIsLock ? IDI_LOCK_BKG : IDI_UNLOCK_BKG);
	list->ReportCmd(CString(MAKEINTRESOURCE(m_bIsLock ? IDS_PROGRS_CMD_LFS_LOCK : IDS_PROGRS_CMD_LFS_UNLOCK)));

	Git_WC_Notify_Action notifyAction = m_bIsLock ? Git_WC_Notify_Action::LFS_Lock : Git_WC_Notify_Action::LFS_Unlock;

	STRING_VECTOR cmdBase{ L"git.exe", L"lfs", m_bIsLock ? L"lock" : L"unlock" };
	if (m_bIsForce)
		cmdBase.emplace_back(L"--force");
	cmdBase.emplace_back(L"--");

	// PRESERVED, NOT FIXED: hasError is written and never read - the function
	// returns true whatever happened. Failure is not lost, because ReportError
	// sets the list's m_bErrorsOccurred, and that is what feeds both the taskbar
	// state and the post-command status; the only thing this bool would change
	// is the window title, which says "finished" after a partly failed run.
	// "return !hasError" looks like the intent, but it is a behaviour change and
	// does not belong in an argv conversion.
	bool hasError = false;

	m_PostCmdCallback = [this](DWORD status, PostCmdList& postCmdList)
	{
		if (status && !m_bIsLock)
		{
			postCmdList.emplace_back(IDI_LFSUNLOCK, IDS_PROGS_LFS_FORCEUNLOCK, [this]()
			{
				CString tempfilename = CTempFiles::Instance().GetTempFilePath(false).GetWinPathString().c_str();
				VERIFY(m_targetPathList.WriteToFile(tempfilename.GetString()));
				CString sCmd;
				sCmd.Format(L"/command:lfsunlock /force /pathfile:\"%s\" /deletepathfile", static_cast<LPCWSTR>(tempfilename));
				CAppUtils::RunTortoiseGitProc(sCmd);
			});
		}
		if (!status && m_bIsLock)
		{
			CString sCmd;
			sCmd.Format(L"/command:pull /path:\"%s\"", static_cast<LPCWSTR>(g_Git.m_CurrentDir));
			CAppUtils::RunTortoiseGitProc(sCmd);
		}
	};

	for (int i = 0; i < m_targetPathList.GetCount(); ++i)
	{
		STRING_VECTOR cmd(cmdBase);
		cmd.emplace_back(m_targetPathList[i].GetGitPathString());

		list->AddNotify(new CGitProgressList::WC_File_NotificationData(m_targetPathList[i], notifyAction));

		if (CString out; g_Git.Run(cmd, &out, CP_UTF8))
		{
			hasError = true;
			list->ReportError(out);
		}

		++m_itemCount;

		if (list->IsCancelled() == TRUE)
		{
			list->ReportUserCanceled();
			return true;
		}
	}

	return true;
}
