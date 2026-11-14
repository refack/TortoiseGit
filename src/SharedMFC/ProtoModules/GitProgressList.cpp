// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2026 - TortoiseGit
// Copyright (C) 2003-2008 - TortoiseSVN

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

#include "stdafx.h"
#include <deque>

#include <Mmsystem.h>

#include "ProtoModules/GitProgressList.h"
#include "LogFile.h"
#include "LoglistUtils.h"
#include "git2/sys/errors.h"

#include <Resources/LoglistCommonResource.h>
#include "../../TortoiseShell/resource.h"

import tgit;
import TortoiseProc;
import DPIAware;
import AppUtils.Launch;
import AppUtils.Window;
import ThemeAPI;
import CmdLineParser;
import TempFiles;

BOOL	CGitProgressList::m_bAscending = FALSE;
int		CGitProgressList::m_nSortedColumn = -1;

#define TRANSFERTIMER	100
#define VISIBLETIMER	101
// CGitProgressList

IMPLEMENT_DYNAMIC(CGitProgressList, CListCtrl)

CGitProgressList::CGitProgressList():CListCtrl()
{
	m_columnbuf[0] = L'\0';
}

CGitProgressList::~CGitProgressList()
{
	for (size_t i = 0; i < m_arData.size(); ++i)
		delete m_arData[i];
	delete m_pThread;
}


BEGIN_MESSAGE_MAP(CGitProgressList, CListCtrl)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnNMCustomdrawSvnprogress)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNMDblclkSvnprogress)
	ON_NOTIFY_REFLECT(HDN_ITEMCLICK, OnHdnItemclickSvnprogress)
	ON_NOTIFY_REFLECT(LVN_BEGINDRAG, OnLvnBegindragSvnprogress)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetdispinfoSvnprogress)
	ON_MESSAGE(WM_SHOWCONFLICTRESOLVER, OnShowConflictResolver)
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_CONTEXTMENU()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

void CGitProgressList::Cancel()
{
	m_bCancelled = TRUE;
}



// CGitProgressList message handlers


LRESULT CGitProgressList::OnShowConflictResolver(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	return 0;
}
void CGitProgressList::AddItemToList()
{
	const int totalcount = GetItemCount();

	SetItemCountEx(totalcount+1, LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);
	// make columns width fit
	if (iFirstResized < 30)
	{
		// only resize the columns for the first 30 or so entries.
		// after that, don't resize them anymore because that's an
		// expensive function call and the columns will be sized
		// close enough already.
		ResizeColumns();
		++iFirstResized;
	}

	// Make sure the item is *entirely* visible even if the horizontal
	// scroll bar is visible.
	const int count = GetCountPerPage();
	if (totalcount <= (GetTopIndex() + count + nEnsureVisibleCount + 2))
	{
		++nEnsureVisibleCount;
		m_bLastVisible = true;
	}
	else
	{
		nEnsureVisibleCount = 0;
		if (IsIconic() == 0)
			m_bLastVisible = false;
	}
}

CString CGitProgressList::BuildInfoString()
{
	CString infotext;
	m_Command->ShowInfo(infotext);

	return infotext;
}

void CGitProgressList::ResizeColumns()
{
	SetRedraw(FALSE);

	wchar_t textbuf[MAX_PATH] = { 0 };

	auto pHeaderCtrl = GetHeaderCtrl();
	if (pHeaderCtrl)
	{
		const int maxcol = pHeaderCtrl->GetItemCount()-1;
		for (int col = 0; col <= maxcol; ++col)
		{
			// find the longest width of all items
			const int count = std::min(static_cast<int>(m_arData.size()), GetItemCount());
			HDITEM hdi = {0};
			hdi.mask = HDI_TEXT;
			hdi.pszText = textbuf;
			hdi.cchTextMax = _countof(textbuf);
			pHeaderCtrl->GetItem(col, &hdi);
			int cx = GetStringWidth(hdi.pszText) + CDPIAware::Instance().ScaleX(GetSafeHwnd(), 20); // 20 pixels for col separator and margin

			for (int index = 0; index<count; ++index)
			{
				// get the width of the string and add 12 pixels for the column separator and margins
				int linewidth = cx;
				switch (col)
				{
				case 0:
					linewidth = GetStringWidth(m_arData[index]->sActionColumnText) + CDPIAware::Instance().ScaleX(GetSafeHwnd(), 12);
					break;
				case 1:
					linewidth = GetStringWidth(m_arData[index]->sPathColumnText) + CDPIAware::Instance().ScaleX(GetSafeHwnd(), 12);
					break;
				}
				if (cx < linewidth)
					cx = linewidth;
			}
			SetColumnWidth(col, cx);
		}
	}

	SetRedraw(TRUE);
}

bool CGitProgressList::SetBackgroundImage(UINT nID)
{
	return CAppUtils::SetListCtrlBackgroundImage(GetSafeHwnd(), nID);
}

void CGitProgressList::ReportGitError()
{
	ReportError(TGitH::GetLibGit2LastErr());
}

void CGitProgressList::ReportUserCanceled()
{
	ReportError(CString(MAKEINTRESOURCE(IDS_USERCANCELLED)));
}

void CGitProgressList::ReportError(const CString& sError)
{
	if (CRegDWORD(L"Software\\TortoiseGit\\NoSounds", FALSE) == FALSE)
		PlaySound(reinterpret_cast<LPCWSTR>(SND_ALIAS_SYSTEMEXCLAMATION), nullptr, SND_ALIAS_ID | SND_ASYNC);
	ReportString(sError, CString(MAKEINTRESOURCE(IDS_ERR_ERROR)), true, g_Colors.GetColor(CColors::Conflict));
	m_bErrorsOccurred = true;
}

void CGitProgressList::ReportWarning(const CString& sWarning)
{
	if (CRegDWORD(L"Software\\TortoiseGit\\NoSounds", FALSE) == FALSE)
		PlaySound(reinterpret_cast<LPCWSTR>(SND_ALIAS_SYSTEMDEFAULT), nullptr, SND_ALIAS_ID | SND_ASYNC);
	ReportString(sWarning, CString(MAKEINTRESOURCE(IDS_WARN_WARNING)), true, g_Colors.GetColor(CColors::Conflict));
}

void CGitProgressList::ReportNotification(const CString& sNotification)
{
	if (CRegDWORD(L"Software\\TortoiseGit\\NoSounds", FALSE) == FALSE)
		PlaySound(reinterpret_cast<LPCWSTR>(SND_ALIAS_SYSTEMDEFAULT), nullptr, SND_ALIAS_ID | SND_ASYNC);
	ReportString(sNotification, CString(MAKEINTRESOURCE(IDS_WARN_NOTE)), false);
}

void CGitProgressList::ReportCmd(const CString& sCmd)
{
	ReportString(sCmd, CString(MAKEINTRESOURCE(IDS_PROGRS_CMDINFO)), true, g_Colors.GetColor(CColors::Cmd));
}

void CGitProgressList::ReportString(CString sMessage, const CString& sMsgKind, bool colorIsDirect, COLORREF color)
{
	// instead of showing a dialog box with the error message or notification,
	// just insert the error text into the list control.
	// that way the user isn't 'interrupted' by a dialog box popping up!

	// the message may be split up into different lines
	// so add a new entry for each line of the message
	while (!sMessage.IsEmpty())
	{
		NotificationData * data = new NotificationData();
		data->bAuxItem = true;
		data->sActionColumnText = sMsgKind;
		if (sMessage.Find('\n')>=0)
			data->sPathColumnText = sMessage.Left(sMessage.Find('\n'));
		else
			data->sPathColumnText = sMessage;
		data->sPathColumnText.Trim(L"\n\r");
		data->color = color;
		data->colorIsDirect = colorIsDirect;
		if (sMessage.Find('\n')>=0)
		{
			sMessage = sMessage.Mid(sMessage.Find('\n'));
			sMessage.Trim(L"\n\r");
		}
		else
			sMessage.Empty();
		AddNotify(data);
	}
}

UINT CGitProgressList::ProgressThreadEntry(LPVOID pVoid)
{
	return static_cast<CGitProgressList*>(pVoid)->ProgressThread();
}

UINT CGitProgressList::ProgressThread()
{
	// The SetParams function should have loaded something for us

	CString temp;
	CString sWindowTitle;
	bool bSuccess = false;

	if(m_pPostWnd)
		m_pPostWnd->PostMessage(WM_PROG_CMD_START, reinterpret_cast<WPARAM>(m_Command));

	if(m_pProgressLabelCtrl)
	{
		m_pProgressLabelCtrl->ShowWindow(SW_SHOW);
		m_pProgressLabelCtrl->SetWindowText(L"");
	}

//	SetAndClearProgressInfo(m_hWnd);
	m_itemCount = m_itemCountTotal;

	InterlockedExchange(&m_bThreadRunning, TRUE);
	iFirstResized = 0;
	bSecondResized = FALSE;
	m_bFinishedItemAdded = false;
	auto startTime = GetTickCount64();

	if (m_pTaskbarList && m_pPostWnd)
		m_pTaskbarList->SetProgressState(m_pPostWnd->GetSafeHwnd(), TBPF_INDETERMINATE);

	m_TotalBytesTransferred = 0;
	if (m_Command)
		bSuccess = m_Command->Run(this, sWindowTitle, m_itemCountTotal, m_itemCount);
	else
		bSuccess = false;

	if (!bSuccess)
		temp.LoadString(IDS_PROGRS_TITLEFAILED);
	else
		temp.LoadString(IDS_PROGRS_TITLEFIN);
	sWindowTitle = sWindowTitle + L' ' + temp;
	if (m_bSetTitle && m_pPostWnd)
		::SetWindowText(m_pPostWnd->GetSafeHwnd(), sWindowTitle);

	KillTimer(TRANSFERTIMER);
	KillTimer(VISIBLETIMER);

	if (m_pTaskbarList && m_pPostWnd)
	{
		if (DidErrorsOccur())
		{
			m_pTaskbarList->SetProgressState(m_pPostWnd->GetSafeHwnd(), TBPF_ERROR);
			m_pTaskbarList->SetProgressValue(m_pPostWnd->GetSafeHwnd(), 100, 100);
		}
		else
			m_pTaskbarList->SetProgressState(m_pPostWnd->GetSafeHwnd(), TBPF_NOPROGRESS);
	}

	if (m_pInfoCtrl)
	{
		CString info;
		if (!bSuccess)
			info.LoadString(IDS_PROGRS_INFOFAILED);
		else // this implies that command is not nullptr
			info = BuildInfoString();
		m_pInfoCtrl->SetWindowText(info);
	}

	ResizeColumns();

	auto time = GetTickCount64() - startTime;

	CString sFinalInfo;
	if (!m_sTotalBytesTransferred.IsEmpty())
	{
		temp.FormatMessage(IDS_PROGRS_TIME, static_cast<DWORD>(time / 1000) / 60, static_cast<DWORD>(time / 1000) % 60);
		sFinalInfo.FormatMessage(IDS_PROGRS_FINALINFO, m_sTotalBytesTransferred, temp);
		if (m_pProgressLabelCtrl)
			m_pProgressLabelCtrl->SetWindowText(sFinalInfo);
	}
	else
	{
		if (m_pProgressLabelCtrl)
			m_pProgressLabelCtrl->ShowWindow(SW_HIDE);
	}

	if (m_pProgControl)
		m_pProgControl->ShowWindow(SW_HIDE);

	if (!m_bFinishedItemAdded)
	{
		CString log, str;
		COLORREF color;
		if (bSuccess)
		{
			str.LoadString(IDS_SUCCESS);
			if (ThemeAPI::CTheme::Instance().IsHighContrastMode())
				color = ::GetSysColor(COLOR_WINDOWTEXT);
			else
				color = ThemeAPI::CTheme::Instance().IsDarkTheme() ? RGB(0, 178, 255) : RGB(0, 0, 255);
		}
		else
		{
			str.LoadString(IDS_FAIL);
			color = ThemeAPI::CTheme::Instance().IsDarkTheme() ? RGB(207, 47, 47) : RGB(255, 0, 0);
		}
		log.Format(L"%s (%I64u ms @ %s)", str, time, CLoglistUtils::FormatDateAndTime(CTime::GetCurrentTime(), DATE_SHORTDATE, true, false));

		// there's no "finished: xxx" line at the end. We add one here to make
		// sure the user sees that the command is actually finished.
		ReportString(log, CString(MAKEINTRESOURCE(IDS_PROGRS_FINISHED)), !(ThemeAPI::CTheme::Instance().IsHighContrastMode() && bSuccess), color);
	}

	const int count = GetItemCount();
	if ((count > 0)&&(m_bLastVisible))
		EnsureVisible(count-1, FALSE);

	CLogFile logfile(GitCall::m_CurrentDirStorage.c_str());
	if (logfile.Open())
	{
		logfile.AddTimeLine();
		for (size_t i = 0; i < m_arData.size(); ++i)
		{
			NotificationData * data = m_arData[i];
			temp.Format(L"%-20s : %s", data->sActionColumnText, data->sPathColumnText);
			logfile.AddLine(temp);
		}
		if (!sFinalInfo.IsEmpty())
			logfile.AddLine(sFinalInfo);
		logfile.Close();
	}

	m_bCancelled = TRUE;
	InterlockedExchange(&m_bThreadRunning, FALSE);

	if (m_pPostWnd)
		m_pPostWnd->PostMessage(WM_PROG_CMD_FINISH, reinterpret_cast<WPARAM>(m_Command), 0L);

	//Don't do anything here which might cause messages to be sent to the window
	//The window thread is probably now blocked in OnOK if we've done an auto close
	return 0;
}

void CGitProgressList::OnLvnGetdispinfoSvnprogress(NMHDR *pNMHDR, LRESULT *pResult)
{
	NMLVDISPINFO *pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);

	if (pDispInfo)
	{
		if (pDispInfo->item.mask & LVIF_TEXT)
		{
			if (pDispInfo->item.iItem < static_cast<int>(m_arData.size()))
			{
				const NotificationData * data = m_arData[pDispInfo->item.iItem];
				switch (pDispInfo->item.iSubItem)
				{
				case 0:
					lstrcpyn(m_columnbuf, data->sActionColumnText, MAX_PATH);
					break;
				case 1:
					lstrcpyn(m_columnbuf, data->sPathColumnText, pDispInfo->item.cchTextMax - 1);
					if (!data->bAuxItem)
					{
						int cWidth = GetColumnWidth(1);
						cWidth = std::max(12, cWidth-12);
						CDC * pDC = GetDC();
						if (pDC)
						{
							CFont * pFont = pDC->SelectObject(GetFont());
							PathCompactPath(pDC->GetSafeHdc(), m_columnbuf, cWidth);
							// ConvertToSlash in place: the buffer is the list's, not a std::wstring
							std::replace(m_columnbuf, m_columnbuf + wcsnlen(m_columnbuf, std::size(m_columnbuf)), L'\\', L'/');
							pDC->SelectObject(pFont);
							ReleaseDC(pDC);
						}
					}
					break;
				default:
					m_columnbuf[0] = L'\0';
				}
				pDispInfo->item.pszText = m_columnbuf;
			}
		}
	}
	*pResult = 0;
}

void CGitProgressList::OnNMCustomdrawSvnprogress(NMHDR *pNMHDR, LRESULT *pResult)
{
	NMLVCUSTOMDRAW* pLVCD = reinterpret_cast<NMLVCUSTOMDRAW*>( pNMHDR );

	// Take the default processing unless we set this to something else below.
	*pResult = CDRF_DODEFAULT;

	// First thing - check the draw stage. If it's the control's prepaint
	// stage, then tell Windows we want messages for every item.

	if ( CDDS_PREPAINT == pLVCD->nmcd.dwDrawStage )
	{
		*pResult = CDRF_NOTIFYITEMDRAW;
	}
	else if ( CDDS_ITEMPREPAINT == pLVCD->nmcd.dwDrawStage )
	{
		// This is the prepaint stage for an item. Here's where we set the
		// item's text color. Our return value will tell Windows to draw the
		// item itself, but it will use the new color we set here.

		// Tell Windows to paint the control itself.
		*pResult = CDRF_DODEFAULT;

		ASSERT(pLVCD->nmcd.dwItemSpec <  m_arData.size());
		if(pLVCD->nmcd.dwItemSpec >= m_arData.size())
		{
			return;
		}
		const NotificationData * data = m_arData[pLVCD->nmcd.dwItemSpec];
		ASSERT(data);
		if (!data)
			return;

		// Store the color back in the NMLVCUSTOMDRAW struct.
		pLVCD->clrText = ThemeAPI::CTheme::Instance().GetThemeColor(data->color, data->colorIsDirect);
	}
}

void CGitProgressList::OnNMDblclkSvnprogress(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	*pResult = 0;
	if (pNMLV->iItem < 0)
		return;
	if (m_options & ProgOptDryRun || m_bThreadRunning)
		return;	//don't do anything in a dry-run.

	const NotificationData* data = m_arData[pNMLV->iItem];
	if (!data)
		return;

	data->HandleDblClick();
}

void CGitProgressList::OnHdnItemclickSvnprogress(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMHEADER phdr = reinterpret_cast<LPNMHEADER>(pNMHDR);
	if (m_bThreadRunning)
		return;
	if (m_nSortedColumn == phdr->iItem)
		m_bAscending = !m_bAscending;
	else
		m_bAscending = TRUE;
	m_nSortedColumn = phdr->iItem;
	Sort();

	CString temp;
	SetRedraw(FALSE);
	DeleteAllItems();
	SetItemCountEx (static_cast<int>(m_arData.size()));

	SetRedraw(TRUE);

	*pResult = 0;
}

bool CGitProgressList::NotificationDataIsAux(const NotificationData* pData)
{
	return pData->bAuxItem;
}

void CGitProgressList::AddNotify(NotificationData* data, CColors::Colors color)
{
	if (color != CColors::COLOR_END)
	{
		data->color = g_Colors.GetColor(color);
		data->colorIsDirect = true;
	}
	else
		data->SetColorCode();

	m_arData.push_back(data);
	AddItemToList();

	if ((!data->bAuxItem) && (m_itemCount > 0))
	{
		if (m_pProgControl)
		{
			m_pProgControl->ShowWindow(SW_SHOW);
			m_pProgControl->SetPos(m_itemCount);
			m_pProgControl->SetRange32(0, m_itemCountTotal);
		}
		if (m_pTaskbarList && m_pPostWnd)
		{
			m_pTaskbarList->SetProgressState(m_pPostWnd->GetSafeHwnd(), TBPF_NORMAL);
			m_pTaskbarList->SetProgressValue(m_pPostWnd->GetSafeHwnd(), m_itemCount, m_itemCountTotal);
		}
	}

	// needed as long as RemoteProgressCommand::RemoteCompletionCallback never gets called by libgit2
	if (m_pAnimate)
		m_pAnimate->ShowWindow(SW_HIDE);
}

int CGitProgressList::UpdateProgress(const libgit2::git_indexer_progress* stat)
{
	static ULONGLONG start = 0;
	auto dt = GetTickCount64() - start;
	double speed = 0;

	if (m_bCancelled)
	{
		git_error_set_str(libgit2::git_error_t::GIT_ERROR_NONE, "User cancelled.");
		return libgit2::git_error_code::GIT_EUSER;
	}

	if (dt > 100)
	{
		start = GetTickCount64();
		const size_t ds = stat->received_bytes - m_TotalBytesTransferred;
		speed = ds * 1000.0/dt;
		m_TotalBytesTransferred = stat->received_bytes;
	}
	else
		return 0;

	const int progress = stat->received_objects + stat->indexed_objects;

	if ((stat->total_objects > 1000) && m_pProgControl && (!m_pProgControl->IsWindowVisible()))
		m_pProgControl->ShowWindow(SW_SHOW);

	if (m_pProgressLabelCtrl && m_pProgressLabelCtrl->IsWindowVisible())
		m_pProgressLabelCtrl->ShowWindow(SW_SHOW);

	if (m_pProgControl)
	{
		m_pProgControl->SetPos(progress);
		m_pProgControl->SetRange32(0, 2 * stat->total_objects);
	}
	if (m_pTaskbarList && m_pPostWnd)
	{
		std::ignore = m_pTaskbarList->SetProgressState(m_pPostWnd->GetSafeHwnd(), TBPF_NORMAL);
		std::ignore = m_pTaskbarList->SetProgressValue(m_pPostWnd->GetSafeHwnd(), progress, 2 * stat->total_objects);
	}

	CString progText;
	if (stat->received_bytes < 1024)
		m_sTotalBytesTransferred.Format(IDS_SVN_PROGRESS_TOTALBYTESTRANSFERRED, static_cast<int64_t>(stat->received_bytes));
	else if (stat->received_bytes < 1200000)
		m_sTotalBytesTransferred.Format(IDS_SVN_PROGRESS_TOTALTRANSFERRED, static_cast<int64_t>(stat->received_bytes) / 1024);
	else
		m_sTotalBytesTransferred.Format(IDS_SVN_PROGRESS_TOTALMBTRANSFERRED, static_cast<double>(stat->received_bytes) / 1048576.0);

	CString str;
	if(speed < 1024)
		str.Format(L"%.0f B/s", speed);
	else if(speed < 1024 * 1024)
		str.Format(L"%.2f KiB/s", speed / 1024);
	else
		str.Format(L"%.2f MiB/s", speed / 1048576.0);

	progText.FormatMessage(IDS_SVN_PROGRESS_TOTALANDSPEED, m_sTotalBytesTransferred, str);
	if (m_pProgressLabelCtrl)
		m_pProgressLabelCtrl->SetWindowText(progText);

	SetTimer(TRANSFERTIMER, 2000, nullptr);

	return 0;
}

void CGitProgressList::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TRANSFERTIMER)
	{
		CString progText;
		CString progSpeed = L"0 B/s";
		progText.FormatMessage(IDS_SVN_PROGRESS_TOTALANDSPEED, m_sTotalBytesTransferred, progSpeed);
		if (m_pProgressLabelCtrl)
			m_pProgressLabelCtrl->SetWindowText(progText);

		KillTimer(TRANSFERTIMER);
	}
	if (nIDEvent == VISIBLETIMER)
	{
		if (nEnsureVisibleCount)
			EnsureVisible(GetItemCount()-1, false);
		nEnsureVisibleCount = 0;
	}
}

void CGitProgressList::Sort()
{
	if(m_arData.size() < 2)
		return;

	// We need to sort the blocks which lie between the auxiliary entries
	// This is so that any aux data stays where it was
	NotificationDataVect::iterator actionBlockBegin;
	NotificationDataVect::iterator actionBlockEnd = m_arData.begin();	// We start searching from here

	for(;;)
	{
		// Search to the start of the non-aux entry in the next block
		actionBlockBegin = std::find_if(actionBlockEnd, m_arData.end(), [](const auto& pData) { return !CGitProgressList::NotificationDataIsAux(pData); });
		if(actionBlockBegin == m_arData.end())
		{
			// There are no more actions
			break;
		}
		// Now search to find the end of the block
		actionBlockEnd = std::find_if(actionBlockBegin + 1, m_arData.end(), [](const auto& pData) { return CGitProgressList::NotificationDataIsAux(pData); });
		// Now sort the block
		std::sort(actionBlockBegin, actionBlockEnd, &CGitProgressList::SortCompare);
	}
}

bool CGitProgressList::SortCompare(const NotificationData * pData1, const NotificationData * pData2)
{
	int result = 0;
	switch (m_nSortedColumn)
	{
	case 0:		//action column
		result = pData1->sActionColumnText.Compare(pData2->sActionColumnText);
		break;
	case 1:		//path column
		// Compare happens after switch()
		break;
	default:
		break;
	}

	// Sort by path if everything else is equal
	if (result == 0)
		result = CTGitPath::Compare(pData1->path, pData2->path);

	if (!m_bAscending)
		result = -result;
	return result < 0;
}

void CGitProgressList::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (m_options & ProgOptDryRun)
		return;	// don't do anything in a dry-run.

	if (pWnd != this)
		return;

	const int selIndex = GetSelectionMark();
	if ((point.x == -1) && (point.y == -1))
	{
		// Menu was invoked from the keyboard rather than by right-clicking
		CRect rect;
		GetItemRect(selIndex, &rect, LVIR_LABEL);
		ClientToScreen(&rect);
		point = rect.CenterPoint();
	}

	if ((selIndex < 0) || m_bThreadRunning || GetSelectedCount() == 0)
		return;

	// entry is selected, thread has finished with updating so show the popup menu
	CIconMenu popup;
	if (!popup.CreatePopupMenu())
		return;

	ContextMenuActionList actions;
	NotificationData* data = m_arData[selIndex];
	if (data && GetSelectedCount() == 1)
		data->GetContextMenu(popup, actions);

	if (!actions.empty())
		popup.AppendMenu(MF_SEPARATOR, NULL);
	actions.push_back([&]()
	{
		CString sLines;
		POSITION pos = GetFirstSelectedItemPosition();
		while (pos)
		{
			const int nItem = GetNextSelectedItem(pos);
			NotificationData* data = m_arData[nItem];
			if (data)
			{
				sLines += data->sPathColumnText;
				sLines += L"\r\n";
			}
		}
		sLines.TrimRight();
		if (!sLines.IsEmpty())
			wil::SetClipboardText(tgit::str::StringView{ sLines }, GetSafeHwnd());
	});
	popup.AppendMenuIcon(actions.size(), IDS_LOG_POPUP_COPYTOCLIPBOARD, IDI_COPYCLIP);

	if (actions.empty())
		return;

	const int cmd = popup.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_NONOTIFY, point.x, point.y, this);

	if (cmd <= 0 || static_cast<size_t>(cmd) > actions.size())
		return;

	AfxGetApp()->DoWaitCursor(1);
	actions.at(cmd - 1)();
	AfxGetApp()->DoWaitCursor(-1);
}

void CGitProgressList::OnLvnBegindragSvnprogress(NMHDR* , LRESULT *pResult)
{
	//LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	*pResult = 0;
}

void CGitProgressList::OnSize(UINT nType, int cx, int cy)
{
	CListCtrl::OnSize(nType, cx, cy);
	if ((nType == SIZE_RESTORED)&&(m_bLastVisible))
	{
		if(!m_hWnd)
			return;

		const int count = GetItemCount();
		if (count > 0)
			EnsureVisible(count-1, false);
	}
}

void CGitProgressList::Init()
{
	SetExtendedStyle((CRegDWORD(L"Software\\TortoiseGit\\FullRowSelect", TRUE) ? LVS_EX_FULLROWSELECT : 0) | LVS_EX_DOUBLEBUFFER);

	DeleteAllItems();
	int c = GetHeaderCtrl()->GetItemCount()-1;
	while (c>=0)
		DeleteColumn(c--);

	CString temp;
	temp.LoadString(IDS_PROGRS_ACTION);
	InsertColumn(0, temp);
	temp.LoadString(IDS_PROGRS_PATH);
	InsertColumn(1, temp);

	m_pThread = AfxBeginThread(ProgressThreadEntry, this, THREAD_PRIORITY_NORMAL,0,CREATE_SUSPENDED);
	if (!m_pThread)
		ReportError(CString(MAKEINTRESOURCE(IDS_ERR_THREADSTARTFAILED)));
	else
	{
		m_pThread->m_bAutoDelete = FALSE;
		m_pThread->ResumeThread();
	}

	// Call this early so that the column headings aren't hidden before any
	// text gets added.
	ResizeColumns();

	SetTimer(VISIBLETIMER, 300, nullptr);
}


void CGitProgressList::OnClose()
{
	if (m_bCancelled)
	{
		GitCall::KillRelatedThreads(m_pThread->m_nThreadID, m_pThread->m_hThread);
		InterlockedExchange(&m_bThreadRunning, FALSE);
	}
	else
	{
		m_bCancelled = TRUE;
		return;
	}
	CListCtrl::OnClose();
}

BOOL CGitProgressList::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == 'A')
		{
			if (GetKeyState(VK_CONTROL)&0x8000)
			{
				// Ctrl-A -> select all
				SetSelectionMark(0);
				for (int i=0; i<GetItemCount(); ++i)
					SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
			}
		}
		if ((pMsg->wParam == 'C')||(pMsg->wParam == VK_INSERT))
		{
			int selIndex = GetSelectionMark();
			if (selIndex >= 0)
			{
				if (GetKeyState(VK_CONTROL)&0x8000)
				{
					//Ctrl-C -> copy to clipboard
					CString sClipdata;
					POSITION pos = GetFirstSelectedItemPosition();
					if (pos)
					{
						while (pos)
						{
							int nItem = GetNextSelectedItem(pos);
							CString sAction = GetItemText(nItem, 0);
							CString sPath = GetItemText(nItem, 1);
							CString sMime = GetItemText(nItem, 2);
							sClipdata.AppendFormat(L"%s: %s  %s\r\n", sAction, sPath, sMime);
						}
						wil::SetClipboardText(tgit::str::StringView{ sClipdata });
					}
				}
			}
		}
	} // if (pMsg->message == WM_KEYDOWN)
	return CListCtrl::PreTranslateMessage(pMsg);
}

void CGitProgressList::SetWindowTitle(UINT id, const CString& urlorpath, CString& dialogname)
{
	if (!m_bSetTitle || !m_pPostWnd)
		return;

	dialogname.LoadString(id);
	wil::MFC::SetWindowTitle(m_pPostWnd->GetSafeHwnd(), urlorpath, dialogname);
}

void CGitProgressList::ShowProgressBar()
{
	if (m_pProgControl)
	{
		m_pProgControl->ShowWindow(SW_SHOW);
		m_pProgControl->SetPos(0);
		m_pProgControl->SetRange32(0, 1);
	}
}

void CGitProgressList::SetProgressLabelText(const CString& str)
{
	if (m_pProgressLabelCtrl)
		m_pProgressLabelCtrl->SetWindowText(str);
}

CGitProgressList::WC_File_NotificationData::WC_File_NotificationData(const CTGitPath& path, Git_WC_Notify_Action action)
	: NotificationData()
, action(action)
{
	this->path = path;
	sPathColumnText = path.GetGitPathString().c_str();

	switch (action)
	{
	case Git_WC_Notify_Action::Skip:
		sActionColumnText.LoadString(IDS_SVNACTION_SKIP);
		break;
	case Git_WC_Notify_Action::Add:
		sActionColumnText.LoadString(IDS_SVNACTION_ADD);
		break;
	case Git_WC_Notify_Action::Resolved:
		sActionColumnText.LoadString(IDS_SVNACTION_RESOLVE);
		break;
	case Git_WC_Notify_Action::Revert:
		sActionColumnText.LoadString(IDS_SVNACTION_REVERT);
		break;
	case Git_WC_Notify_Action::Checkout:
		sActionColumnText.LoadString(IDS_PROGRS_CMD_CHECKOUT);
		break;
	case Git_WC_Notify_Action::LFS_Lock:
		sActionColumnText.LoadString(IDS_PROGRS_CMD_LFS_LOCK);
		break;
	case Git_WC_Notify_Action::LFS_Unlock:
		sActionColumnText.LoadString(IDS_PROGRS_CMD_LFS_UNLOCK);
		break;
	default:
		break;
	}
}

void CGitProgressList::WC_File_NotificationData::SetColorCode()
{
	switch (action)
	{
	case Git_WC_Notify_Action::Checkout:
		[[fallthrough]];
	case Git_WC_Notify_Action::Add:
		color = g_Colors.GetColor(CColors::Added);
		colorIsDirect = true;
		break;

	default:
		break;
	}
}

void CGitProgressList::WC_File_NotificationData::GetContextMenu(CIconMenu& popup, ContextMenuActionList& actions)
{
	if ((action == Git_WC_Notify_Action::Add) ||
		(action == Git_WC_Notify_Action::Revert) ||
		(action == Git_WC_Notify_Action::Resolved) ||
		(action == Git_WC_Notify_Action::Checkout) ||
		(action == Git_WC_Notify_Action::LFS_Lock) ||
		(action == Git_WC_Notify_Action::LFS_Unlock))
	{
		actions.push_back([&]()
		{
			auto sPath = g_Git.CombinePath(path);
			auto cmd = std::format(L"/command:log /path:{}", sPath);
			TortoiseProc::RunTortoiseGitProc(cmd);
		});
		popup.AppendMenuIcon(actions.size(), IDS_MENULOG, IDI_LOG);

		popup.AppendMenu(MF_SEPARATOR, NULL);
		if (!fs::exists(g_Git.CombinePath(path))) {
			actions.push_back([&]{ TortoiseProc::ShellOpen(g_Git.CombinePath(path).c_str()); });
			popup.AppendMenuIcon(actions.size(), IDS_LOG_POPUP_OPEN, IDI_OPEN);
			actions.push_back([&]{ TortoiseProc::ShowOpenWithDialog(g_Git.CombinePath(path).c_str()); });
			popup.AppendMenuIcon(actions.size(), IDS_LOG_POPUP_OPENWITH, IDI_OPEN);
		}

		actions.push_back([&]{ TortoiseProc::ExploreTo(nullptr, g_Git.CombinePath(path).c_str()); });
		popup.AppendMenuIcon(actions.size(), IDS_STATUSLIST_CONTEXT_EXPLORE, IDI_EXPLORER);

		if ((action == Git_WC_Notify_Action::LFS_Lock) ||
			(action == Git_WC_Notify_Action::LFS_Unlock))
		{
			popup.AppendMenu(MF_SEPARATOR, NULL);

			CTGitPathList pathList(path);

			actions.push_back([&] {
				CString tempfilename = TempFiles::GetTempFilePath(false).GetWinPathString().c_str();
				VERIFY(pathList.WriteToFile(tempfilename.GetString()));
				CString sCmd;
				sCmd.Format(L"/command:lfslock /pathfile:\"%s\" /deletepathfile", tempfilename);
				TortoiseProc::RunTortoiseGitProc(sCmd);
			});
			popup.AppendMenuIcon(actions.size(), IDS_PROGRS_TITLE_LFS_LOCK, IDI_LFSLOCK);

			actions.push_back([&] {
				CString tempfilename = TempFiles::GetTempFilePath(false).GetWinPathString().c_str();
				VERIFY(pathList.WriteToFile(tempfilename.GetString()));
				CString sCmd;
				sCmd.Format(L"/command:lfsunlock /pathfile:\"%s\" /deletepathfile", tempfilename);
				TortoiseProc::RunTortoiseGitProc(sCmd);
			});
			popup.AppendMenuIcon(actions.size(), IDS_PROGRS_TITLE_LFS_UNLOCK, IDI_LFSUNLOCK);
		}
	}
}

void CGitProgressList::WC_File_NotificationData::HandleDblClick() const
{
	const auto sWinPath = g_Git.CombinePath(path);
	if (fs::exists(sWinPath)) {
		TortoiseProc::ExploreTo(nullptr, sWinPath.c_str());
		return;
	}
	TortoiseProc::ShellOpen(sWinPath.c_str());
}

void CGitProgressList::OnSysColorChange()
{
	__super::OnSysColorChange();
	ThemeAPI::CTheme::Instance().OnSysColorChanged();
}

ULONG CGitProgressList::GetGestureStatus(CPoint /*ptTouch*/)
{
	return 0;
}
