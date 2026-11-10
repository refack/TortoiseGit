module;
#include "stdafx.h"


export module MenuButton;
import ThemeAPI;
import ThemeControls;
import IconMenu;


/**
 * \ingroup Utils
 * A button control with a menu to choose from different
 * actions. Clicking on the left "button" part is the same
 * as with a normal button, clicking on the right "arrow"
 * part will bring up a menu where the user can choose what
 * action the button should do.
 */
export class CMenuButton : public CThemeMFCMenuButton
{
public:
	DECLARE_DYNCREATE(CMenuButton);

	CMenuButton();
	~CMenuButton() override;

	/**
	 * Inserts a text to be shown in the button menu.
	 * The text is inserted at the end of the menu string list.
	 * \return the index of the inserted item. This index is
	 * returned in GetCurrentEntry().
	 */
	INT_PTR AddEntry(const CString& sEntry, UINT uIcon = 0U);

	/**
	 * Inserts an array of strings to be shown in the
	 * button menu. The strings are inserted at the end
	 * of the menu string list.
	 * \return the index of the first inserted item. This index
	 * is returned in GetCurrentEntry().
	 */
	INT_PTR	AddEntries(const CStringArray& sEntries);

	/**
	 * Returns the currently shown entry index of the button.
	 */
	INT_PTR	GetCurrentEntry() const { return (m_nMenuResult == 0) ? m_nDefault - 1 : m_nMenuResult - 1; }

	/**
	 * Sets which of the menu strings should be shown in the
	 * button and be active.
	 * \return true if successful
	 */
	bool	SetCurrentEntry(INT_PTR entry);

	/**
	 * Determines if the button control is drawn with the XP
	 * themes or without. The default is \a true, which means
	 * the control is drawn with theming support if the underlying
	 * OS has it enabled and supports it.
	 */
	void	RemoveAll();

	bool	m_bMarkDefault = true;

	/** Don't show text on Button */
	bool	m_bShowCurrentItem = true;

	bool	m_bAlwaysShowArrow = false;

protected:
	BOOL PreTranslateMessage(MSG* pMsg) override;

	BOOL IsPressed() const override;

	afx_msg void OnDraw(CDC* pDC, const CRect& rect, UINT uiState) override;
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnSysColorChange();
	afx_msg LRESULT OnThemeChanged();

	afx_msg BOOL OnClicked();
	afx_msg void OnDestroy();

	CIconMenu	m_btnMenu;
	INT_PTR	m_nDefault = 0;

	DECLARE_MESSAGE_MAP()

	CStringArray m_sEntries;

	CFont m_Font;
	void FixFont();
};


IMPLEMENT_DYNCREATE(CMenuButton, CMFCMenuButton)

CMenuButton::CMenuButton() : CThemeMFCMenuButton()
{
	m_bOSMenu = TRUE;
	m_bDefaultClick = TRUE;
	m_bTransparent = TRUE;
	m_bStayPressed = TRUE;

	m_btnMenu.CreatePopupMenu();
	m_hMenu = m_btnMenu.GetSafeHmenu();
}

CMenuButton::~CMenuButton()
{
}

bool CMenuButton::SetCurrentEntry(INT_PTR entry)
{
	if (entry < 0 || entry >= m_sEntries.GetCount() || !m_bShowCurrentItem)
		return false;

	SetWindowText(m_sEntries[entry]);
	if (m_bMarkDefault)
	{
		m_nDefault = entry + 1;
		m_btnMenu.SetDefaultItem(static_cast<UINT>(m_nDefault), FALSE);
	}

	return true;
}

void CMenuButton::RemoveAll()
{
	for (int index = 0; index < m_sEntries.GetCount(); index++)
		m_btnMenu.RemoveMenu(0, MF_BYPOSITION);
	m_sEntries.RemoveAll();
	m_nDefault = m_nMenuResult = 0;
}

INT_PTR CMenuButton::AddEntry(const CString& sEntry, UINT uIcon /*= 0U*/)
{
	INT_PTR ret = m_sEntries.Add(sEntry);
	m_btnMenu.AppendMenuIcon(m_sEntries.GetCount(), sEntry, uIcon);

	if (ret == 0)
		SetCurrentEntry(ret);
	return ret;
}

INT_PTR CMenuButton::AddEntries(const CStringArray& sEntries)
{
	INT_PTR ret = -1;
	for (int index = 0; index < sEntries.GetCount(); index++)
	{
		if (index == 0)
			ret = AddEntry(sEntries[index]);
		else
			AddEntry(sEntries[index]);
	}
	return ret;
}

void CMenuButton::FixFont()
{
	CWnd* pWnd = GetParent();

	if (pWnd == nullptr)
		return;

	CFont* pFont = pWnd->GetFont();
	if (pFont == nullptr)
		return;

	LOGFONT logfont;
	if (pFont->GetLogFont(&logfont) == 0)
		return;

	if (m_Font.CreateFontIndirect(&logfont) == FALSE)
		return;

	SetFont(&m_Font);
	Invalidate();
}

BEGIN_MESSAGE_MAP(CMenuButton, CMFCMenuButton)
	ON_WM_DESTROY()
	ON_CONTROL_REFLECT_EX(BN_CLICKED, &CMenuButton::OnClicked)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_THEMECHANGED()
	ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()


BOOL CMenuButton::OnClicked()
{
	SetCurrentEntry(GetCurrentEntry());

	// let the parent handle the message the usual way
	return FALSE;
}

BOOL CMenuButton::PreTranslateMessage(MSG* pMsg)
{
	if(pMsg->message == WM_KEYDOWN)
	{
		switch(pMsg->wParam)
		{
		case VK_RETURN:
		case VK_SPACE:
			if (!m_bMenuIsActive)
			{
				GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), BN_CLICKED), reinterpret_cast<LPARAM>(m_hWnd));
				return TRUE;
			}
		case VK_F4:
			if (m_bAlwaysShowArrow)
				GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), BN_CLICKED), reinterpret_cast<LPARAM>(m_hWnd));
			else
				OnShowMenu();
			return TRUE;
		}
	}

	return __super::PreTranslateMessage(pMsg);
}

void CMenuButton::OnDestroy()
{
	m_sEntries.RemoveAll();

	__super::OnDestroy();
}

BOOL CMenuButton::IsPressed() const
{
	return __super::IsPressed() || m_bChecked;
}

void CMenuButton::OnDraw(CDC* pDC, const CRect& rect, UINT uiState)
{
	m_bNoArrow = !m_bAlwaysShowArrow && m_sEntries.GetCount() < 2;
	if (!m_bAlwaysShowArrow && m_bNoArrow && !CTheme::Instance().IsDarkTheme())
		CMFCButton::OnDraw(pDC, rect, uiState);
	else
		__super::OnDraw(pDC, rect, uiState);

	if (m_Font.m_hObject == nullptr)
		FixFont();
}

void CMenuButton::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_bAlwaysShowArrow || m_sEntries.GetCount() < 2)
		CMFCButton::OnLButtonDown(nFlags, point);
	else
		__super::OnLButtonDown(nFlags, point);
}

LRESULT CMenuButton::OnThemeChanged()
{
	CMFCVisualManager::GetInstance()->DestroyInstance();
	m_Font.DeleteObject();
	return 0L;
}

void CMenuButton::OnSysColorChange()
{
	__super::OnSysColorChange();
	GetGlobalData()->UpdateSysColors();
}
