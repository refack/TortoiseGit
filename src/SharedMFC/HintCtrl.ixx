module;
#include "stdafx.h"

#include "Utils/MyMemDC.h"



export module HintCtrl;
import ThemeAPI;

/**
 * \ingroup Utils
 * Allows to show a hint text on a control, basically hiding the control
 * content. Can be used for example during lengthy operations (showing "please wait")
 * or to indicate why the control is empty (showing "no data available").
 */
export template <typename BaseType> class CHintCtrl : public BaseType
{
public:
	CHintCtrl() : BaseType()
	{
		NONCLIENTMETRICS metrics = {};
		metrics.cbSize = sizeof(NONCLIENTMETRICS);
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &metrics, FALSE);
		m_uiFont.CreateFontIndirect(&metrics.lfMessageFont);
	}
	virtual ~CHintCtrl()
	{
	}

	void ShowText(const CString& sText, const bool forceupdate = false)
	{
		m_sText = sText;
		BaseType::Invalidate();
		if (forceupdate)
			BaseType::UpdateWindow();
	}

	void ClearText()
	{
		m_sText.Empty();
		BaseType::Invalidate();
	}

	bool HasText() const {return !m_sText.IsEmpty();}


protected:
	DECLARE_MESSAGE_MAP()

	afx_msg void OnPaint()
	{
		const LRESULT defres = BaseType::Default();
		if (!m_sText.IsEmpty())
		{
			const COLORREF clrText = CTheme::Instance().IsDarkTheme() ? CTheme::darkTextColor : ::GetSysColor(COLOR_WINDOWTEXT);
			COLORREF clrTextBk;
			if (BaseType::IsWindowEnabled())
				clrTextBk = CTheme::Instance().IsDarkTheme() ? CTheme::darkBkColor : ::GetSysColor(COLOR_WINDOW);
			else
				clrTextBk = CTheme::Instance().GetThemeColor(::GetSysColor(COLOR_3DFACE));

			CRect rc;
			BaseType::GetClientRect(&rc);
			bool bIsEmpty = false;
			const CListCtrl * pListCtrl = dynamic_cast<CListCtrl*>(this);
			if (pListCtrl)
			{
				const CHeaderCtrl* pHC = pListCtrl->GetHeaderCtrl();
				if (pHC)
				{
					CRect rcH;
					rcH.SetRectEmpty();
					pHC->GetItemRect(0, &rcH);
					rc.top += rcH.bottom;
				}
				bIsEmpty = pListCtrl->GetItemCount() == 0;
			}
			CDC* pDC = BaseType::GetDC();
			{
				pDC->SetBkMode(TRANSPARENT);
				pDC->SetTextColor(clrText);
				pDC->SetBkColor(clrTextBk);
				CMyMemDC memDC(pDC, &rc);

				memDC.SetTextColor(clrText);
				memDC.SetBkColor(clrTextBk);
				memDC.SetBkMode(TRANSPARENT);
				if (bIsEmpty)
					memDC.BitBlt(rc.left, rc.top, rc.Width(), rc.Height(), pDC, rc.left, rc.top, SRCCOPY);
				else
					memDC.FillSolidRect(rc, clrTextBk);
				rc.top += 10;
				CGdiObject* oldfont = memDC.SelectObject(CGdiObject::FromHandle(m_uiFont));
				memDC.DrawText(m_sText, rc, DT_CENTER | DT_VCENTER |
					DT_WORDBREAK | DT_NOPREFIX | DT_NOCLIP);
				memDC.SelectObject(oldfont);
			}
			BaseType::ReleaseDC(pDC);
		}
		if (defres)
		{
			// the Default() call did not process the WM_PAINT message!
			// Validate the update region ourselves to avoid
			// an endless loop repainting
			CRect rc;
			BaseType::GetUpdateRect(&rc, FALSE);
			if (!rc.IsRectEmpty())
				BaseType::ValidateRect(rc);
		}
	}

private:
	CString			m_sText;
	CFont			m_uiFont;
};

BEGIN_TEMPLATE_MESSAGE_MAP(CHintCtrl, BaseType, BaseType)
	ON_WM_PAINT()
END_MESSAGE_MAP()

