module;
#include <afxcmn.h>



export module Tooltip;

import std;
import ThemeAPI;
import DarkModeHelper;



/**
 * \ingroup Utils
 * Extends the MFC CToolTipCtrl with convenience methods for dialogs and
 * provides mechanism to use tooltips longer than 80 chars whithout having
 * to implement the TTN_NEEDTEXT handler in every dialog.
 */
export class CToolTips : public CToolTipCtrl {
	// Construction
public:
	CToolTips() :
		CToolTipCtrl() {}


	~CToolTips() override {}


	BOOL AddTool(CWnd* pWnd, UINT nIDText, LPCRECT lpRectTool = nullptr, UINT_PTR nIDTool = 0) {
		CString sTemp = LoadTooltip(nIDText);
		toolTextMap[::GetDlgCtrlID(pWnd->GetSafeHwnd())] = sTemp;
		return CToolTipCtrl::AddTool(pWnd, LPSTR_TEXTCALLBACK, lpRectTool, nIDTool);
	}


	BOOL AddTool(CWnd* pWnd, LPCWSTR lpszText = LPSTR_TEXTCALLBACK, LPCRECT lpRectTool = nullptr, UINT_PTR nIDTool = 0) {
		if (lpszText != LPSTR_TEXTCALLBACK)
			toolTextMap[::GetDlgCtrlID(pWnd->GetSafeHwnd())] = CString(lpszText);
		return CToolTipCtrl::AddTool(pWnd, lpszText, lpRectTool, nIDTool);
	}


	void AddTool(int nIdWnd, UINT nIdText, LPCRECT lpRectTool = nullptr, UINT_PTR nIDTool = 0) {
		AddTool(m_pParentWnd->GetDlgItem(nIdWnd), nIdText, lpRectTool, nIDTool);
	}


	void AddTool(int nIdWnd, const CString& sBalloonTipText, LPCRECT lpRectTool = nullptr, UINT_PTR nIDTool = 0) {
		AddTool(m_pParentWnd->GetDlgItem(nIdWnd), sBalloonTipText, lpRectTool, nIDTool);
	}


	void DelTool(CWnd* pWnd, UINT_PTR nIDTool = 0) {
		toolTextMap.erase(::GetDlgCtrlID(pWnd->GetSafeHwnd()));
		return CToolTipCtrl::DelTool(pWnd, nIDTool);
	}


	void DelTool(int nIdWnd, UINT_PTR nIDTool = 0) {
		return DelTool(m_pParentWnd->GetDlgItem(nIdWnd), nIDTool);
	}


	static BOOL ShowBalloon(CWnd* pWnd, UINT nIDText, UINT nIDTitle, UINT icon = 0) {
		CString sTemp = LoadTooltip(nIDText);

		const HWND& hwndTT = CreateWindow(
			TOOLTIPS_CLASS,
			L"",
			TTS_NOPREFIX|TTS_BALLOON|TTS_ALWAYSTIP|TTS_CLOSE,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			pWnd->GetSafeHwnd(),
			nullptr,
			nullptr,
			nullptr
		);
		if (!hwndTT)
			return FALSE;

		DarkModeHelper::Instance().AllowDarkModeForWindow(hwndTT, CTheme::Instance().IsDarkTheme());
		std::ignore = ::SetWindowTheme(hwndTT, L"Explorer", nullptr);

		TOOLINFO ti = {};
		ti.cbSize = sizeof(ti);
		ti.uFlags = TTF_TRACK | TTF_IDISHWND | TTF_PARSELINKS;
		ti.hwnd = pWnd->GetSafeHwnd();
		ti.lpszText = sTemp.GetBuffer();
		::SendMessage(hwndTT, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));
		::SendMessage(hwndTT, TTM_SETTITLE, icon, reinterpret_cast<LPARAM>(static_cast<LPCWSTR>(CString(MAKEINTRESOURCE(nIDTitle)))));
		::SendMessage(hwndTT, TTM_SETMAXTIPWIDTH, 0, 800);

		// Position the tooltip below the control
		RECT rc;
		::GetWindowRect(pWnd->GetSafeHwnd(), &rc);
		::SendMessage(hwndTT, TTM_TRACKPOSITION, 0, MAKELONG(rc.left + 10, rc.bottom));

		// Show the tooltip
		::SendMessage(hwndTT, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));

		return TRUE;
	}


	void ShowBalloon(int nIdWnd, UINT nIdText, UINT nIDTitle, UINT icon = 0) {
		ShowBalloon(m_pParentWnd->GetDlgItem(nIdWnd), nIdText, nIDTitle, icon);
	}


	void RelayEvent(LPMSG lpMsg, CWnd* dlgWnd = nullptr) {
		if (lpMsg->message == WM_NULL)
			return;

		if (dlgWnd && ((lpMsg->message == WM_MOUSEMOVE) || (lpMsg->message == WM_NCMOUSEMOVE)) && (lpMsg->hwnd == dlgWnd->m_hWnd)) {
			// allow tooltips for disabled controls
			CRect rect;
			POINT pt;

			pt.x = LOWORD(lpMsg->lParam);
			pt.y = HIWORD(lpMsg->lParam);

			for (auto it = toolTextMap.cbegin(); it != toolTextMap.cend(); ++it) {
				CWnd* pWndCtrl = dlgWnd->GetDlgItem(it->first);
				if (pWndCtrl == nullptr)
					continue;
				pWndCtrl->GetWindowRect(&rect);
				if (lpMsg->message == WM_MOUSEMOVE)
					dlgWnd->ScreenToClient(&rect);

				if (rect.PtInRect(pt)) {
					// save the original parameters
					HWND origHwnd = lpMsg->hwnd;
					LPARAM origLParam = lpMsg->lParam;

					// translate and relay the mouse move message to
					// the tooltip control as if they were sent
					// by the disabled control
					lpMsg->hwnd = pWndCtrl->m_hWnd;

					if (lpMsg->message == WM_MOUSEMOVE)
						dlgWnd->ClientToScreen(&pt);
					pWndCtrl->ScreenToClient(&pt);
					lpMsg->lParam = MAKELPARAM(pt.x, pt.y);

					__super::RelayEvent(lpMsg);

					// restore the original parameters
					lpMsg->hwnd = origHwnd;
					lpMsg->lParam = origLParam;
					return;
				}
			}
		}

		// Let the ToolTip process this message.
		__super::RelayEvent(lpMsg);
	}


	BOOL Create(CWnd* pParentWnd, DWORD dwStyle = 0) override {
		m_pParentWnd = pParentWnd;
		m_pParentWnd->EnableToolTips();
		BOOL bRet = CToolTipCtrl::Create(pParentWnd, dwStyle);
		SetMaxTipWidth(600);
		SetDelayTime(TTDT_AUTOPOP, 30000);
		m_themeCallbackId = CTheme::Instance().RegisterThemeChangeCallback([this] { SetTheme(CTheme::Instance().IsDarkTheme()); });
		SetTheme(CTheme::Instance().IsDarkTheme());
		return bRet;
	}

protected:
	DECLARE_MESSAGE_MAP()
	afx_msg BOOL OnTtnNeedText(NMHDR* pNMHDR, LRESULT* pResult) {
		if (pNMHDR->code == TTN_NEEDTEXTW) {
			auto lpnmtdi = reinterpret_cast<LPNMTTDISPINFO>(pNMHDR);
			UINT_PTR nID = pNMHDR->idFrom;

			if (lpnmtdi->uFlags & TTF_IDISHWND) {
				// idFrom is actually the HWND of the tool
				nID = ::GetDlgCtrlID(reinterpret_cast<HWND>(nID));
			}

			auto iterFind = toolTextMap.find(static_cast<unsigned int>(nID));
			if (iterFind != toolTextMap.end()) {
				lpnmtdi->lpszText = const_cast<LPWSTR>(static_cast<LPCWSTR>(iterFind->second));
				lpnmtdi->hinst = AfxGetResourceHandle();
				*pResult = 0;
				return TRUE;
			}
		}
		return FALSE;
	}

private:
	void SetTheme(bool bDark) {
		DarkModeHelper::Instance().AllowDarkModeForWindow(GetSafeHwnd(), bDark);
		std::ignore = ::SetWindowTheme(GetSafeHwnd(), L"Explorer", nullptr);
	}


	CWnd* m_pParentWnd = nullptr;
	std::map<UINT, CString> toolTextMap;
	int m_themeCallbackId = 0;


	static CString LoadTooltip(UINT nIDText) {
		CString sTemp;
		sTemp.LoadString(nIDText);
		// tooltips can't handle \t and single \n, only spaces and \r\n
		sTemp.Replace('\t', ' ');
		sTemp.Replace(L"\r\n", L"\n");
		sTemp.Replace(L"\n", L"\r\n");
		return sTemp;
	}
};



BEGIN_MESSAGE_MAP(CToolTips, CToolTipCtrl)
	ON_NOTIFY_REFLECT_EX(TTN_NEEDTEXT, &CToolTips::OnTtnNeedText)
END_MESSAGE_MAP()
