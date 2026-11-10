module;
#include "stdafx.h"

#include <ResizableDialog.h>
#include <Utils/CommonDialogFunctions.h>
#include "ProtoModules/EditWordBreak.h"

#pragma comment(lib, "Dwmapi.lib")

// ReSharper disable CppParameterMayBeConst
// ReSharper disable CppDefaultCaseNotHandledInSwitchStatement
// ReSharper disable CppNonExplicitConvertingConstructor
// NOLINTBEGIN(*-branch-clone)

export module StandAloneDlg;
import std;
import gsl;
import Registry;
import ThemeAPI;
import TaskbarUUID;
import DarkModeHelper;
import GitHash;
import SysInfo;		// GetMonitorSetupHash
import Tooltip;
import DPIAware;
// import TortoiseProc;

using namespace TGitDarkMode;
using namespace ThemeAPI;



export namespace StandAloneDlg {
using SysInfo::GetMonitorSetupHash;	// kept here for existing importers; the body lives in SysInfo


constexpr auto DIALOG_BLOCKHORIZONTAL = 1;
constexpr auto DIALOG_BLOCKVERTICAL = 2;

/**
 * \ingroup TortoiseProc
 *
 * A template which can be used as the base-class of dialogs which form the main dialog
 * of a 'dialog-style application'
 * Just provides the boiler-plate code for dealing with application icons
 *
 * \remark Replace all references to CDialog or CResizableDialog in your dialog class with
 * either CResizableStandAloneDialog, CStandAloneDialog or CStateStandAloneDialog, as appropriate
 */
template <typename BaseType>
class CStandAloneDialogTmpl : public BaseType, protected CommonDialogFunctions<BaseType> {
protected:
	CStandAloneDialogTmpl(UINT nIDTemplate, CWnd* pParentWnd = nullptr) :
		BaseType(nIDTemplate, pParentWnd), CommonDialogFunctions<BaseType>(this) {
		m_hIcon = AfxGetApp()->LoadIcon(1); // IDR_MAINFRAME; use the magic value here, because we cannot include resource.h here
	}


	~CStandAloneDialogTmpl() {
		CTheme::Instance().RemoveRegisteredCallback(m_themeCallbackId);
	}


	BOOL OnInitDialog() override {
		m_themeCallbackId = CTheme::Instance().RegisterThemeChangeCallback([this] { SetTheme(CTheme::Instance().IsDarkTheme()); });

		BaseType::OnInitDialog();

		// Set the icon for this dialog.  The framework does this automatically
		//  when the application's main window is not a dialog
		BaseType::SetIcon(m_hIcon, TRUE); // Set big icon
		BaseType::SetIcon(m_hIcon, FALSE); // Set small icon

		BaseType::EnableToolTips();
		m_tooltips.Create(this);
		SetTheme(CTheme::Instance().IsDarkTheme());

		const auto CustomBreak = static_cast<DWORD>(CRegDWORD(L"Software\\TortoiseGit\\UseCustomWordBreak", 2));
		if (CustomBreak)
			SetUrlWordBreakProcToChildWindows(BaseType::GetSafeHwnd(), CustomBreak == 2);

		m_dpi = CDPIAware::Instance().GetDPI(BaseType::GetSafeHwnd());

		return FALSE;
	}


	BOOL PreTranslateMessage(MSG* pMsg) override {
		m_tooltips.RelayEvent(pMsg, this);
		if (pMsg->message == WM_KEYDOWN) {
			const int nVirtKey = static_cast<int>(pMsg->wParam);

			if (nVirtKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
				wchar_t buffer[129];
				::GetClassName(pMsg->hwnd, buffer, _countof(buffer) - 1);

				if (_wcsnicmp(buffer, L"EDIT", _countof(buffer) - 1) == 0) {
					::PostMessage(pMsg->hwnd,EM_SETSEL, 0, -1);
					return TRUE;
				}
			}
			if (nVirtKey == 'D' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
				CTheme::Instance().SetDarkTheme(!CTheme::Instance().IsDarkTheme());
		}
		return BaseType::PreTranslateMessage(pMsg);
	}


	afx_msg void OnPaint() {
		if (BaseType::IsIconic()) {
			CPaintDC dc(this); // device context for painting

			BaseType::SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

			// Center icon in client rectangle
			const int cxIcon = GetSystemMetrics(SM_CXICON);
			const int cyIcon = GetSystemMetrics(SM_CYICON);
			CRect rect;
			BaseType::GetClientRect(&rect);
			const int x = (rect.Width() - cxIcon + 1) / 2;
			const int y = (rect.Height() - cyIcon + 1) / 2;

			// Draw the icon
			dc.DrawIcon(x, y, m_hIcon);
		} else
			BaseType::OnPaint();
	}


	/**
	 * Wrapper around the CWnd::EnableWindow() method, but
	 * makes sure that a control that has the focus is not disabled
	 * before the focus is passed on to the next control.
	 */
	BOOL DialogEnableWindow(UINT nID, BOOL bEnable) {
		CWnd* pwndDlgItem = BaseType::GetDlgItem(nID);
		if (!pwndDlgItem)
			return FALSE;
		if (bEnable)
			return pwndDlgItem->EnableWindow(bEnable);
		if (BaseType::GetFocus() == pwndDlgItem) {
			BaseType::SendMessage(WM_NEXTDLGCTL, 0, FALSE);
		}
		return pwndDlgItem->EnableWindow(bEnable);
	}


	/**
	 * Refreshes the cursor by forcing a WM_SETCURSOR message.
	 */
	void RefreshCursor() {
		POINT pt;
		GetCursorPos(&pt);
		SetCursorPos(pt.x, pt.y);
	}


	CToolTips m_tooltips;
	int m_themeCallbackId = 0;
	int m_dpi = 0;
	DECLARE_MESSAGE_MAP()

private:
	HCURSOR OnQueryDragIcon() {
		return m_hIcon;
	}

protected:
	afx_msg void OnSysColorChange() {
		BaseType::OnSysColorChange();
		CTheme::Instance().OnSysColorChanged();
		SetTheme(CTheme::Instance().IsDarkTheme());
	}


	LRESULT OnDPIChanged(WPARAM /*wParam*/, LPARAM lParam) {
		CDPIAware::Instance().Invalidate();

		const auto newDPI = CDPIAware::Instance().GetDPI(BaseType::GetSafeHwnd());
		if (m_dpi == 0) {
			m_dpi = newDPI;
			return 0;
		}

		const auto rect = reinterpret_cast<RECT*>(lParam);
		RECT oldRect{};
		GetWindowRect(BaseType::GetSafeHwnd(), &oldRect);

		const double zoom = (static_cast<double>(newDPI) / (static_cast<double>(m_dpi) / 100.0)) / 100.0;
		rect->right = static_cast<LONG>(rect->left + (oldRect.right - oldRect.left) * zoom);
		rect->bottom = static_cast<LONG>(rect->top + (oldRect.bottom - oldRect.top) * zoom);

		const CDPIAware::DPIAdjustData data{ BaseType::GetSafeHwnd(), zoom };
		if constexpr (std::is_same_v<BaseType, CResizableDialog>) {
			auto anchors = BaseType::GetAllAnchors();
			BaseType::RemoveAllAnchors();

			auto minTrackSize = BaseType::GetMinTrackSize();
			minTrackSize.cx = gsl::narrow<long>(minTrackSize.cx * zoom);
			minTrackSize.cy = gsl::narrow<long>(minTrackSize.cy * zoom);
			BaseType::SetMinTrackSize(minTrackSize);

			BaseType::m_noNcCalcSizeAdjustments = true;
			BaseType::SetWindowPos(nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
			BaseType::m_noNcCalcSizeAdjustments = false;
			::EnumChildWindows(BaseType::GetSafeHwnd(), CDPIAware::DPIAdjustChildren, reinterpret_cast<LPARAM>(&data));

			BaseType::AddAllAnchors(anchors);
		} else {
			BaseType::SetWindowPos(nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
			::EnumChildWindows(BaseType::GetSafeHwnd(), CDPIAware::DPIAdjustChildren, reinterpret_cast<LPARAM>(&data));
		}

		m_dpi = newDPI;

		::RedrawWindow(BaseType::GetSafeHwnd(), nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_INTERNALPAINT | RDW_ALLCHILDREN | RDW_UPDATENOW);
		return 1; // let MFC handle this message as well
	}


	virtual void SetTheme(bool bDark) {
		if (bDark) {
			DarkModeHelper::Instance().AllowDarkModeForApp(TRUE);
			DarkModeHelper::Instance().AllowDarkModeForWindow(BaseType::GetSafeHwnd(), TRUE);
			DarkModeHelper::Instance().AllowDarkModeForWindow(m_tooltips.GetSafeHwnd(), TRUE);
			std::ignore = SetWindowTheme(m_tooltips.GetSafeHwnd(), L"Explorer", nullptr);
			SetClassLongPtr(BaseType::GetSafeHwnd(), GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(GetStockObject(BLACK_BRUSH)));
			BOOL darkFlag = TRUE;
			DarkModeHelper::WINDOWCOMPOSITIONATTRIBDATA data = { .Attrib = DarkModeHelper::WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS, .pvData = &darkFlag, .cbData = sizeof(darkFlag) };
			DarkModeHelper::Instance().SetWindowCompositionAttribute(BaseType::GetSafeHwnd(), &data);
			DarkModeHelper::Instance().FlushMenuThemes();
			DarkModeHelper::Instance().RefreshImmersiveColorPolicyState();
			BOOL dark = TRUE;
			DwmSetWindowAttribute(BaseType::GetSafeHwnd(), 19, &dark, sizeof(dark));
		} else {
			DarkModeHelper::Instance().AllowDarkModeForWindow(BaseType::GetSafeHwnd(), FALSE);
			DarkModeHelper::Instance().AllowDarkModeForWindow(m_tooltips.GetSafeHwnd(), FALSE);
			std::ignore = SetWindowTheme(m_tooltips.GetSafeHwnd(), L"Explorer", nullptr);
			BOOL darkFlag = FALSE;
			DarkModeHelper::WINDOWCOMPOSITIONATTRIBDATA data = { .Attrib = DarkModeHelper::WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS, .pvData = &darkFlag, .cbData = sizeof(darkFlag) };
			DarkModeHelper::Instance().SetWindowCompositionAttribute(BaseType::GetSafeHwnd(), &data);
			DarkModeHelper::Instance().FlushMenuThemes();
			DarkModeHelper::Instance().RefreshImmersiveColorPolicyState();
			DarkModeHelper::Instance().AllowDarkModeForApp(FALSE);
			SetClassLongPtr(BaseType::GetSafeHwnd(), GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(GetSysColorBrush(COLOR_3DFACE)));
		}
		CTheme::SetThemeForDialog(BaseType::GetSafeHwnd(), bDark);
		::RedrawWindow(BaseType::GetSafeHwnd(), nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_INTERNALPAINT | RDW_ALLCHILDREN | RDW_UPDATENOW);
	}


	void HtmlHelp(DWORD_PTR dwData, UINT nCmd = 0x000F) override {
		UNREFERENCED_PARAMETER(nCmd);
		std::unreachable();
		// if (!CAppUtils::StartHtmlHelp(dwData))
		// 	AfxMessageBox(AFX_IDP_FAILED_TO_LAUNCH_HELP);
	}


	afx_msg LRESULT OnTaskbarButtonCreated(WPARAM /*wParam*/, LPARAM /*lParam*/) {
		std::unreachable();
		// SetUUIDOverlayIcon(BaseType::m_hWnd, TortoiseProc::g_sGroupingUUID, CAppUtils::g_sGroupingIcon, CAppUtils::g_bGroupingRemoveIcon);
		return 0;
	}


	HICON m_hIcon;
};



class CStateDialog : public CDialog, public CResizableWndState {
public:
	CStateDialog() = default;


	explicit CStateDialog(UINT nIDTemplate, CWnd* pParentWnd = nullptr) :
		CDialog(nIDTemplate, pParentWnd) {}


	explicit CStateDialog(LPCWSTR lpszTemplateName, CWnd* pParentWnd = nullptr) :
		CDialog(lpszTemplateName, pParentWnd) {}


	~CStateDialog() override = default;

private:
	// flags
	bool m_bEnableSaveRestore = false;
	bool m_bRectOnly = false;

	// internal status
	CString m_sSection; // section name (identifies a parent window)

protected:
	// overloaded method, but since this dialog class is for non-resizable dialogs,
	// the bHorzResize and bVertResize params are ignored and passed as false
	// to the base method.
	void EnableSaveRestore(LPCWSTR pszSection, bool bRectOnly = FALSE, BOOL bHorzResize = TRUE, BOOL bVertResize = TRUE) {
		UNREFERENCED_PARAMETER(bHorzResize);
		UNREFERENCED_PARAMETER(bVertResize);
		m_sSection = CString(pszSection) + L'_' + GetMonitorSetupHash().c_str();

		m_bEnableSaveRestore = true;
		m_bRectOnly = bRectOnly;

		// restore immediately
		LoadWindowRect(m_sSection, bRectOnly, false, false);
	}


	CWnd* GetResizableWnd() const override {
		// make the layout know its parent window
		return CWnd::FromHandle(m_hWnd);
	}

	afx_msg void OnDestroy() {
		if (m_bEnableSaveRestore)
			SaveWindowRect(m_sSection, m_bRectOnly);
		CDialog::OnDestroy();
	}

	DECLARE_MESSAGE_MAP()
};



class CResizableStandAloneDialog : public CStandAloneDialogTmpl<CResizableDialog> {
public:
	CResizableStandAloneDialog(UINT nIDTemplate, CWnd* pParentWnd = nullptr);

private:
	DECLARE_DYNAMIC(CResizableStandAloneDialog)

protected:
	BOOL OnInitDialog() override;
	afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnNcMButtonUp(UINT nHitTest, CPoint point);
	afx_msg void OnNcRButtonUp(UINT nHitTest, CPoint point);
	afx_msg LRESULT OnNcHitTest(CPoint point);

	DECLARE_MESSAGE_MAP()

	int m_nResizeBlock = 0;
	long m_width = 0;
	long m_height = 0;


	void BlockResize(int block) {
		m_nResizeBlock = block;
	}


	void EnableSaveRestore(LPCWSTR pszSection, bool bRectOnly = FALSE) {
		// call the base method with the bHorzResize and bVertResize parameters
		// figured out from the resize block flags.
		const std::wstring monitorSetupSection = std::wstring(pszSection) + L'_' + GetMonitorSetupHash();
		__super::EnableSaveRestore(monitorSetupSection.c_str(),
								   bRectOnly,
								   (m_nResizeBlock & DIALOG_BLOCKHORIZONTAL) == 0,
								   (m_nResizeBlock & DIALOG_BLOCKVERTICAL) == 0);
	}

private:
	bool m_bVertical = false;
	bool m_bHorizontal = false;
	CRect m_rcOrgWindowRect;
};



class CStandAloneDialog : public CStandAloneDialogTmpl<CDialog> {
public:
	CStandAloneDialog(UINT nIDTemplate, CWnd* pParentWnd = nullptr);

protected:
	DECLARE_MESSAGE_MAP()

private:
	DECLARE_DYNAMIC(CStandAloneDialog)
};



class CStateStandAloneDialog : public CStandAloneDialogTmpl<CStateDialog> {
public:
	CStateStandAloneDialog(UINT nIDTemplate, CWnd* pParentWnd = nullptr);

protected:
	DECLARE_MESSAGE_MAP()

private:
	DECLARE_DYNAMIC(CStateStandAloneDialog)
};



const UINT TaskBarButtonCreated = RegisterWindowMessage(L"TaskbarButtonCreated");

BEGIN_TEMPLATE_MESSAGE_MAP(CStandAloneDialogTmpl, BaseType, BaseType)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_REGISTERED_MESSAGE(TaskBarButtonCreated, OnTaskbarButtonCreated)
	ON_WM_SYSCOLORCHANGE()
	ON_MESSAGE(WM_DPICHANGED, OnDPIChanged)
END_MESSAGE_MAP()
IMPLEMENT_DYNAMIC(CStandAloneDialog, CStandAloneDialogTmpl)


CStandAloneDialog::CStandAloneDialog(UINT nIDTemplate, CWnd* pParentWnd /*= nullptr*/) :
	CStandAloneDialogTmpl(nIDTemplate, pParentWnd) {}


BEGIN_MESSAGE_MAP(CStandAloneDialog, CStandAloneDialogTmpl)
END_MESSAGE_MAP()

IMPLEMENT_DYNAMIC(CStateStandAloneDialog, CStandAloneDialogTmpl)


CStateStandAloneDialog::CStateStandAloneDialog(UINT nIDTemplate, CWnd* pParentWnd /*= nullptr*/) :
	CStandAloneDialogTmpl(nIDTemplate, pParentWnd) {}


BEGIN_MESSAGE_MAP(CStateStandAloneDialog, CStandAloneDialogTmpl)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

IMPLEMENT_DYNAMIC(CResizableStandAloneDialog, CDialog)


CResizableStandAloneDialog::CResizableStandAloneDialog(UINT nIDTemplate, CWnd* pParentWnd /*= nullptr*/) :
	CStandAloneDialogTmpl(nIDTemplate, pParentWnd) {}


BOOL CResizableStandAloneDialog::OnInitDialog() {
	__super::OnInitDialog();

	RECT rect;
	GetWindowRect(&rect);
	m_height = rect.bottom - rect.top;
	m_width = rect.right - rect.left;

	return FALSE;
}


BEGIN_MESSAGE_MAP(CResizableStandAloneDialog, CStandAloneDialogTmpl)
	ON_WM_SIZING()
	ON_WM_MOVING()
	ON_WM_NCMBUTTONUP()
	ON_WM_NCRBUTTONUP()
	ON_WM_NCHITTEST()
	ON_BN_CLICKED(IDHELP, OnHelp)
END_MESSAGE_MAP()


void CResizableStandAloneDialog::OnSizing(UINT fwSide, LPRECT pRect) {
	m_bVertical = m_bVertical && (fwSide == WMSZ_LEFT || fwSide == WMSZ_RIGHT);
	m_bHorizontal = m_bHorizontal && (fwSide == WMSZ_TOP || fwSide == WMSZ_BOTTOM);
	if (m_nResizeBlock & DIALOG_BLOCKVERTICAL) {
		// don't allow the dialog to be changed in height
		switch (fwSide) {
		case WMSZ_BOTTOM:
		case WMSZ_BOTTOMLEFT:
		case WMSZ_BOTTOMRIGHT:
			pRect->bottom = pRect->top + m_height;
			break;
		case WMSZ_TOP:
		case WMSZ_TOPLEFT:
		case WMSZ_TOPRIGHT:
			pRect->top = pRect->bottom - m_height;
			break;
		}
	}
	if (m_nResizeBlock & DIALOG_BLOCKHORIZONTAL) {
		// don't allow the dialog to be changed in width
		switch (fwSide) {
		case WMSZ_RIGHT:
		case WMSZ_TOPRIGHT:
		case WMSZ_BOTTOMRIGHT:
			pRect->right = pRect->left + m_width;
			break;
		case WMSZ_LEFT:
		case WMSZ_TOPLEFT:
		case WMSZ_BOTTOMLEFT:
			pRect->left = pRect->right - m_width;
			break;
		}
	}
	CStandAloneDialogTmpl::OnSizing(fwSide, pRect);
}


void CResizableStandAloneDialog::OnMoving(UINT fwSide, LPRECT pRect) {
	m_bVertical = m_bHorizontal = false;
	CStandAloneDialogTmpl::OnMoving(fwSide, pRect);
}


void CResizableStandAloneDialog::OnNcMButtonUp(UINT nHitTest, CPoint point) {
	WINDOWPLACEMENT windowPlacement;
	if ((nHitTest == HTMAXBUTTON) && GetWindowPlacement(&windowPlacement) && windowPlacement.showCmd == SW_SHOWNORMAL) {
		CRect rcWorkArea, rcWindowRect;
		GetWindowRect(&rcWindowRect);
		if (m_bVertical) {
			rcWindowRect.top = m_rcOrgWindowRect.top;
			rcWindowRect.bottom = m_rcOrgWindowRect.bottom;
		} else if (SystemParametersInfo(SPI_GETWORKAREA, 0U, &rcWorkArea, 0U)) {
			m_rcOrgWindowRect.top = rcWindowRect.top;
			m_rcOrgWindowRect.bottom = rcWindowRect.bottom;
			rcWindowRect.top = rcWorkArea.top;
			rcWindowRect.bottom = rcWorkArea.bottom;
		}
		m_bVertical = !m_bVertical;
		MoveWindow(&rcWindowRect);
	}
	CStandAloneDialogTmpl::OnNcMButtonUp(nHitTest, point);
}


void CResizableStandAloneDialog::OnNcRButtonUp(UINT nHitTest, CPoint point) {
	WINDOWPLACEMENT windowPlacement;
	if ((nHitTest == HTMAXBUTTON) && GetWindowPlacement(&windowPlacement) && windowPlacement.showCmd == SW_SHOWNORMAL) {
		CRect rcWorkArea, rcWindowRect;
		GetWindowRect(&rcWindowRect);
		if (m_bHorizontal) {
			rcWindowRect.left = m_rcOrgWindowRect.left;
			rcWindowRect.right = m_rcOrgWindowRect.right;
		} else if (SystemParametersInfo(SPI_GETWORKAREA, 0U, &rcWorkArea, 0U)) {
			m_rcOrgWindowRect.left = rcWindowRect.left;
			m_rcOrgWindowRect.right = rcWindowRect.right;
			rcWindowRect.left = rcWorkArea.left;
			rcWindowRect.right = rcWorkArea.right;
		}
		m_bHorizontal = !m_bHorizontal;
		MoveWindow(&rcWindowRect);
		// WORKAROUND
		// for some reasons, when the window is resized horizontally, its menu size is not get adjusted.
		// so, we force it to happen.
		SetMenu(GetMenu());
	}
	CStandAloneDialogTmpl::OnNcRButtonUp(nHitTest, point);
}


LRESULT CResizableStandAloneDialog::OnNcHitTest(CPoint point) {
	if (m_nResizeBlock == 0)
		return __super::OnNcHitTest(point);

	// when resizing is blocked in a direction, don't return
	// a hit code that would allow that resizing.
	// Using the OnNcHitTest handler tells Windows to
	// not show a resizing mouse pointer if it's not possible
	auto ht = __super::OnNcHitTest(point);
	if (m_nResizeBlock & DIALOG_BLOCKVERTICAL) {
		switch (ht) {
		case HTBOTTOMLEFT:
			ht = HTLEFT;
			break;
		case HTBOTTOMRIGHT:
			ht = HTRIGHT;
			break;
		case HTTOPLEFT:
			ht = HTLEFT;
			break;
		case HTTOPRIGHT:
			ht = HTRIGHT;
			break;
		case HTTOP:
			ht = HTBORDER;
			break;
		case HTBOTTOM:
			ht = HTBORDER;
			break;
		}
	}
	if (m_nResizeBlock & DIALOG_BLOCKHORIZONTAL) {
		switch (ht) {
		case HTBOTTOMLEFT:
			ht = HTBOTTOM;
			break;
		case HTBOTTOMRIGHT:
			ht = HTBOTTOM;
			break;
		case HTTOPLEFT:
			ht = HTTOP;
			break;
		case HTTOPRIGHT:
			ht = HTTOP;
			break;
		case HTLEFT:
			ht = HTBORDER;
			break;
		case HTRIGHT:
			ht = HTBORDER;
			break;
		}
	}

	return ht;
}


BEGIN_MESSAGE_MAP(CStateDialog, CDialog)
	ON_BN_CLICKED(IDHELP, OnHelp)
END_MESSAGE_MAP()


}
export using namespace StandAloneDlg;
// NOLINTEND(*-branch-clone)
