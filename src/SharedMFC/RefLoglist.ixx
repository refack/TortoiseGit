module;
#include "stdafx.h"
#include "ProtoModules/LoglistUtils.h"


export module RefLogList;
import tgit;
import AppUtils.Launch;
import TortoiseProc;	// RunTortoiseGitProc
import DPIAware;
import GitLogListBase;
import ColumnManager;



export class CRefLogList : public CGitLogList
{
	DECLARE_DYNAMIC(CRefLogList)
public:
	CRefLogList();
	void InsertRefLogColumn();
	enum
	{
		REFLOG_HASH,
		REFLOG_REF,
		REFLOG_ACTION,
		REFLOG_MESSAGE,
		REFLOG_DATE,
	};

	std::vector<GitRevLoglist> m_RevCache;
	using CGitLogListBase::m_CurrentBranch;

protected:
	void OnNMDblclkLoglist(NMHDR* /*pNMHDR*/, LRESULT* pResult) override;
	void OnLvnGetdispinfoLoglist(NMHDR* pNMHDR, LRESULT* pResult) override;
	void OnNMCustomdrawLoglist(NMHDR* pNMHDR, LRESULT* pResult) override;
	BOOL OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* pResult) override;

	void CopySelectionToClipBoard(int toCopy = ID_COPYCLIPBOARDFULL) override;
};

IMPLEMENT_DYNAMIC(CRefLogList, CGitLogList)

CRefLogList::CRefLogList()
{
	m_ColumnRegKey = L"reflog";
	this->m_ContextMenuMask |= this->GetContextMenuBit(ID_LOG);
	this->m_ContextMenuMask &= ~GetContextMenuBit(ID_COMPARETWOCOMMITCHANGES);
}

void CRefLogList::InsertRefLogColumn()
{
	CString temp;

	Init();
	SetStyle();

	const auto columnWidth = CDPIAware::Instance().ScaleX(GetSafeHwnd(), ICONITEMBORDER + 16 * 4);
	const ColumnDefinition columns[] = {
		{ REFLOG_HASH, IDS_HASH, true, true, columnWidth },
		{ REFLOG_REF, IDS_REF, true, true, columnWidth },
		{ REFLOG_ACTION, IDS_ACTION, true, true, columnWidth },
		{ REFLOG_MESSAGE, IDS_MESSAGE, true, true, CDPIAware::Instance().ScaleX(GetSafeHwnd(), LOGLIST_MESSAGE_MIN_WIDTH) },
		{ REFLOG_DATE, IDS_STATUSLIST_COLDATE, true, true, columnWidth },
	};

	SetRedraw(false);

	constexpr int columnVersion = 6; // adjust when changing number/names/etc. of columns
	m_ColumnManager.ReadSettings(columns, m_ColumnRegKey + L"loglist", columnVersion);

	SetRedraw(true);
}

void CRefLogList::OnLvnGetdispinfoLoglist(NMHDR *pNMHDR, LRESULT *pResult)
{
	NMLVDISPINFO *pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);

	// Create a pointer to the item
	LV_ITEM* pItem = &(pDispInfo)->item;

	// Do the list need text information?
	if (!(pItem->mask & LVIF_TEXT))
		return;

	// By default, clear text buffer.
	lstrcpyn(pItem->pszText, L"", pItem->cchTextMax);

	bool bOutOfRange = pItem->iItem >= static_cast<int>(m_arShownList.size());

	*pResult = 0;
	if (m_bNoDispUpdates || bOutOfRange)
		return;

	// Which item number?
	GitRevLoglist* pLogEntry = m_arShownList.SafeGetAt(pItem->iItem);

	CString temp;

	// Which column?
	switch (pItem->iSubItem)
	{
	case REFLOG_HASH:
		if (pLogEntry)
		{
			lstrcpyn(pItem->pszText,(pLogEntry->m_CommitHash.ToString()).c_str(), pItem->cchTextMax - 1);
		}
		break;
	case REFLOG_REF:
		if(pLogEntry)
			lstrcpyn(pItem->pszText, pLogEntry->m_Ref, pItem->cchTextMax - 1);
		break;
	case REFLOG_ACTION:
		if (pLogEntry)
			lstrcpyn(pItem->pszText, pLogEntry->m_RefAction, pItem->cchTextMax - 1);
		break;
	case REFLOG_MESSAGE:
		if (pLogEntry)
			lstrcpyn(pItem->pszText, pLogEntry->GetSubject().Trim(), pItem->cchTextMax - 1);
		break;
	case REFLOG_DATE:
		if (pLogEntry)
			lstrcpyn(pItem->pszText, CLoglistUtils::FormatDateAndTime(pLogEntry->GetCommitterDate(), m_DateFormat, true, m_bRelativeTimes), pItem->cchTextMax - 1);
		break;

	default:
		ASSERT(false);
	}
}

void CRefLogList::OnNMDblclkLoglist(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	// a double click on an entry in the revision list has happened
	*pResult = 0;

	POSITION pos = GetFirstSelectedItemPosition();
	const int indexNext = GetNextSelectedItem(pos);
	if (indexNext < 0)
		return;

	auto pSelLogEntry = m_arShownList.SafeGetAt(indexNext);
	if (!pSelLogEntry)
		return;

	CString cmdline;
	cmdline.Format(L"/command:log /path:%s /endrev:%s /rev:%s", str::EscapeCmdValue(g_Git.CombinePath(m_Path).wstring()).c_str(), (pSelLogEntry->m_CommitHash.ToString()).c_str(), (pSelLogEntry->m_CommitHash.ToString()).c_str());
	TortoiseProc::RunTortoiseGitProc(cmdline);
}

void CRefLogList::OnNMCustomdrawLoglist(NMHDR * /*pNMHDR*/, LRESULT *pResult)
{
	// Take the default processing unless we set this to something else below.
	*pResult = CDRF_DODEFAULT;
}

BOOL CRefLogList::OnToolTipText(UINT /*id*/, NMHDR* /*pNMHDR*/, LRESULT* /*pResult*/)
{
	return FALSE;
}

void CRefLogList::CopySelectionToClipBoard(int toCopy)
{
	CString sClipdata;
	POSITION pos = GetFirstSelectedItemPosition();
	if (pos)
	{
		CString sRev;
		sRev.LoadString(IDS_LOG_REVISION);
		CString sDate;
		sDate.LoadString(IDS_LOG_DATE);
		CString sMessage;
		sMessage.LoadString(IDS_LOG_MESSAGE);
		bool first = true;
		while (pos)
		{
			auto pLogEntry = m_arShownList.SafeGetAt(GetNextSelectedItem(pos));

			if (toCopy == ID_COPYCLIPBOARDFULL)
			{
				sClipdata.AppendFormat(L"%s: %s\r\n%s: %s\r\n%s: %s%s%s\r\n",
									sRev, (pLogEntry->m_CommitHash.ToString()).c_str(),
									sDate, CLoglistUtils::FormatDateAndTime(pLogEntry->GetCommitterDate(), m_DateFormat, true, m_bRelativeTimes),
									sMessage, pLogEntry->m_RefAction, pLogEntry->m_RefAction.IsEmpty() ? L"" : L": ", pLogEntry->GetSubjectBody(true));
			}
			else if (toCopy == ID_COPYCLIPBOARDMESSAGES)
			{
				sClipdata += L"* ";
				if (!pLogEntry->m_RefAction.IsEmpty())
				{
					sClipdata += pLogEntry->m_RefAction;
					sClipdata += L": ";
				}
				sClipdata += pLogEntry->GetSubjectBody(true);
				sClipdata += L"\r\n\r\n";
			}
			else
			{
				if (!first)
					sClipdata += L"\r\n";
				sClipdata += pLogEntry->m_CommitHash.ToString().c_str();
			}

			first = false;
		}
		wil::SetClipboardText(tgit::str::StringView{ sClipdata }, GetSafeHwnd());
	}
}
