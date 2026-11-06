module;
#include <afxwin.h>
#include <ResizableLayout.h>

#include "Utils/HistoryCombo.h"
#include <Resources/LoglistCommonResource.h>

#include "Resources/resource.h"

#include "Utils/GestureEnabledControl.h"


export module FindDlg;

import std;
import gsl;
import StandAloneDlg;
import Registry;
import ThemeAPI;
using ThemeAPI::CTheme;

// CFindDlg dialog

export namespace FindDlg {

constexpr auto IDT_FILTER = 101;

//! Anchor to the top-left corner
using ::TOP_LEFT;
//! Anchor to the top edge and center horizontally
using ::TOP_CENTER;
//! Anchor to the top-right corner
using ::TOP_RIGHT;
//! Anchor to the left edge and center vertically
using ::MIDDLE_LEFT;
//! Anchor to the center
using ::MIDDLE_CENTER;
//! Anchor to the right edge and center vertically
using ::MIDDLE_RIGHT;
//! Anchor to the bottom-left corner
using ::BOTTOM_LEFT;
//! Anchor to the bottom edge and center horizontally
using ::BOTTOM_CENTER;
//! Anchor to the bottom-right corner
using ::BOTTOM_RIGHT;


class CFindDlg : public CResizableStandAloneDialog
{
	DECLARE_DYNAMIC(CFindDlg)

	explicit CFindDlg(CWnd* pParent =nullptr)
		: CResizableStandAloneDialog(CFindDlg::IDD, pParent)
		, m_pParent(pParent)
		, m_regMatchCase(L"Software\\TortoiseGit\\LogDialog\\FindMatchCase", FALSE)
		, m_regRegex(L"Software\\TortoiseGit\\LogDialog\\FindRegex", FALSE)
	{}

	~CFindDlg() override = default;

	void Create(CWnd* pParent = nullptr) { m_pParent = pParent; CDialog::Create(IDD, pParent); ShowWindow(SW_SHOW); UpdateWindow(); }

	bool IsTerminating() const { return m_bTerminating; }
	bool FindNext() const { return m_bFindNext; }
	bool MatchCase() const { return !!m_bMatchCase; }
	bool Regex() const { return !!m_bRegex; }
	bool IsRef() const { return !!m_bIsRef; }
	CString GetFindString() const { return m_FindString; }
	void SetFindString(const CString& str) { if (!str.IsEmpty()) { m_FindCombo.SetWindowText(str); } }
	void RefreshList();

// Dialog Data
	enum { IDD = IDD_FIND };

protected:
	void DoDataExchange(CDataExchange* pDX) override;    // DDX/DDV support
	void OnCancel() override;
	void PostNcDestroy() override;
	void OnOK() override;
	BOOL OnInitDialog() override;

	void AddToList();

	afx_msg void OnCbnEditchangeFindcombo();
	afx_msg void OnNMClickListRef(NMHDR* pNMHDR, LRESULT* pResult);

	afx_msg void OnEnChangeEditFilter() { SetTimer(IDT_FILTER, 1000, nullptr); }

	afx_msg void OnTimer(UINT_PTR nIDEvent) {
		if( nIDEvent == IDT_FILTER)
		{
			KillTimer(IDT_FILTER);
			this->AddToList();
		}

		CResizableStandAloneDialog::OnTimer(nIDEvent);
	}


	DECLARE_MESSAGE_MAP()

	UINT			m_FindMsg{};
	bool			m_bTerminating{};
	bool			m_bFindNext{};
	BOOL			m_bMatchCase{};
	BOOL			m_bRegex{};
	bool			m_bIsRef{};
	CHistoryCombo	m_FindCombo;
	CString			m_FindString;
	CWnd*			m_pParent{};
	std::vector<std::wstring>	m_RefList;
	CRegDWORD		m_regMatchCase;
	CRegDWORD		m_regRegex;


public:
	CGestureEnabledControlTmpl<CListCtrl> m_ctrlRefList;
	CEdit m_ctrlFilter;
};


IMPLEMENT_DYNAMIC(CFindDlg, CResizableStandAloneDialog)


void CFindDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_MATCHCASE, m_bMatchCase);
	DDX_Check(pDX, IDC_CHECK_REGEX, m_bRegex);
	DDX_Control(pDX, IDC_FINDCOMBO, m_FindCombo);
	DDX_Control(pDX, IDC_LIST_REF, m_ctrlRefList);
	DDX_Control(pDX, IDC_EDIT_FILTER, m_ctrlFilter);
}


BEGIN_MESSAGE_MAP(CFindDlg, CResizableStandAloneDialog)
	ON_CBN_EDITCHANGE(IDC_FINDCOMBO, &CFindDlg::OnCbnEditchangeFindcombo)
	ON_NOTIFY(NM_CLICK, IDC_LIST_REF, &CFindDlg::OnNMClickListRef)
	ON_EN_CHANGE(IDC_EDIT_FILTER, &CFindDlg::OnEnChangeEditFilter)
	ON_WM_TIMER()
END_MESSAGE_MAP()


// CFindDlg message handlers

void CFindDlg::OnCancel()
{
	m_bTerminating = true;

	CWnd *parent = m_pParent;
	if (!parent)
		parent = GetParent();

	if (parent)
		std::ignore = parent->SendMessage(m_FindMsg);

	DestroyWindow();
}

void CFindDlg::PostNcDestroy()
{
	delete this;
}

void CFindDlg::OnOK()
{
	UpdateData();
	m_FindCombo.SaveHistory();

	m_regMatchCase = m_bMatchCase;
	m_regRegex = m_bRegex;

	if (m_FindCombo.GetString().IsEmpty())
		return;
	m_bFindNext = true;
	m_FindString = m_FindCombo.GetString();

	CWnd *parent = m_pParent;
	if (!parent)
		parent = GetParent();

	if (parent)
		std::ignore = parent->SendMessage(m_FindMsg);
	m_bFindNext = false;
}

BOOL CFindDlg::OnInitDialog()
{
	__super::OnInitDialog();
	m_FindMsg = RegisterWindowMessage(FINDMSGSTRING);

	m_bMatchCase = m_regMatchCase;
	m_bRegex = m_regRegex;
	UpdateData(FALSE);

	m_FindCombo.SetCaseSensitive(TRUE);
	m_FindCombo.LoadHistory(L"Software\\TortoiseGit\\History\\Find", L"Search");
	m_FindCombo.SetCurSel(0);
	m_FindCombo.SetFocus();

	this->AddAnchor(IDC_STATIC_FIND, TOP_LEFT, TOP_RIGHT);
	this->AddAnchor(IDC_FINDCOMBO, TOP_LEFT, TOP_RIGHT);
	this->AddAnchor(IDOK, TOP_RIGHT);
	this->AddAnchor(IDCANCEL, TOP_RIGHT);
	this->AddAnchor(IDC_STATIC_GROUP_REF, TOP_LEFT, BOTTOM_RIGHT);
	this->AddAnchor(IDC_STATIC_FILTER, BOTTOM_LEFT);
	this->AddAnchor(IDC_EDIT_FILTER, BOTTOM_LEFT, BOTTOM_RIGHT);
	this->AddAnchor(IDC_LIST_REF, TOP_LEFT, BOTTOM_RIGHT);
	this->AddOthersToAnchor();

	EnableSaveRestore(L"FindDlg");
	SetTheme(CTheme::Instance().IsDarkTheme());

	auto imagelist = gsl::owner(new CImageList());
	imagelist->Create(IDB_BITMAP_REFTYPE,16,3,RGB(255,255,255));
	this->m_ctrlRefList.SetImageList(imagelist,LVSIL_SMALL);

	CRect rect;
	m_ctrlRefList.GetClientRect(&rect);

	this->m_ctrlRefList.InsertColumn(0, L"Ref", 0, rect.Width() - 50);
	if (CRegDWORD(L"Software\\TortoiseGit\\FullRowSelect", TRUE))
		m_ctrlRefList.SetExtendedStyle(m_ctrlRefList.GetExtendedStyle() | LVS_EX_FULLROWSELECT);
	RefreshList();
	return FALSE;
}

void CFindDlg::RefreshList()
{
	m_RefList.clear();
	if (g_Git.GetRefList(m_RefList))
		MessageBox(g_Git.GetGitLastErr(L"Could not get all refs."), L"TortoiseGit", MB_ICONERROR);
	AddToList();
}

void CFindDlg::OnCbnEditchangeFindcombo()
{
	UpdateData();
	GetDlgItem(IDOK)->EnableWindow(!m_FindCombo.GetString().IsEmpty());
}

void CFindDlg::AddToList()
{
	this->m_ctrlRefList.DeleteAllItems();
	CString filter;
	this->m_ctrlFilter.GetWindowText(filter);

	int item =0;
	for (const auto & ref : m_RefList) {
		int nImage = -1;
		if (ref.starts_with(L"refs/tags/"))
			nImage = 0;
		else if (ref.starts_with(L"refs/remotes/"))
			nImage = 2;
		else if (ref.starts_with(L"refs/heads/"))
			nImage = 1;

		if(ref.contains(filter))
			m_ctrlRefList.InsertItem(item++, ref.data(), nImage);
	}
}

void CFindDlg::OnNMClickListRef(NMHDR *pNMHDR, LRESULT *pResult)
{
	auto *pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);

	this->m_FindString = this->m_ctrlRefList.GetItemText(pNMItemActivate->iItem,0);
	this->m_bIsRef =true;

	CWnd *parent = m_pParent;
	if (!parent)
		parent = GetParent();

	if (parent)
		std::ignore = parent->SendMessage(m_FindMsg);

	this->m_bIsRef =false;

	*pResult = 0;
}

}
export using namespace FindDlg;