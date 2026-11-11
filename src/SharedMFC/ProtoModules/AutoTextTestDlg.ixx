module;
#include "stdafx.h"
#include "AutoTextTestDlg.h"
#include "Utils/HighResClock.h"

import tgit;
import TortoiseProc;
import AppUtils.Window;

// CAutoTextTestDlg dialog

IMPLEMENT_DYNAMIC(CAutoTextTestDlg, CStandAloneDialog)


static constexpr std::wstring Std_DDX_Text(CDataExchange* pDX, const int rid) {
	CString tmp;
	DDX_Text(pDX, rid, tmp);
	return std::wstring{tmp};
}


class CAutoTextTestDlg : public CStandAloneDialog
{
	DECLARE_DYNAMIC(CAutoTextTestDlg)

public:
	CAutoTextTestDlg(CWnd* pParent = nullptr) : CStandAloneDialog(CAutoTextTestDlg::IDD, pParent) {}

	// Dialog Data
	enum { IDD = IDD_AUTOTEXTTESTDLG };

protected:
	void DoDataExchange(CDataExchange* pDX) override;	// DDX/DDV support
	BOOL OnInitDialog() override;
	afx_msg void OnBnClickedAutotextscan();

	DECLARE_MESSAGE_MAP()

private:
	std::wstring m_sContent;
	std::wstring m_sRegex;
	std::wstring m_sResult;
	std::wstring m_sTimingLabel;
	CRichEditCtrl m_cContent;
};


void CAutoTextTestDlg::DoDataExchange(CDataExchange* pDX) {
	CStandAloneDialog::DoDataExchange(pDX);
	m_sRegex = Std_DDX_Text(pDX, IDC_AUTOTEXTREGEX);
	m_sResult = Std_DDX_Text(pDX, IDC_TESTRESULT);
	m_sTimingLabel = Std_DDX_Text(pDX, IDC_TIMINGLABEL);
	DDX_Control(pDX, IDC_AUTOTEXTCONTENT, m_cContent);
}

BEGIN_MESSAGE_MAP(CAutoTextTestDlg, CStandAloneDialog)
	ON_BN_CLICKED(IDC_AUTOTEXTSCAN, &CAutoTextTestDlg::OnBnClickedAutotextscan)
END_MESSAGE_MAP()


BOOL CAutoTextTestDlg::OnInitDialog()
{
	CStandAloneDialog::OnInitDialog();
	wil::MFC::MarkWindowAsUnpinnable(m_hWnd);

	m_cContent.LimitText(200*1024);

	SetTheme(CTheme::Instance().IsDarkTheme());

	return TRUE;
}

void CAutoTextTestDlg::OnBnClickedAutotextscan()
{
	UpdateData();

	m_sResult.clear();
	m_sTimingLabel.clear();
	CString tmp_sContent;
	m_cContent.GetTextRange(0, m_cContent.GetTextLength(), tmp_sContent);
	m_sContent = tmp_sContent;
	if ((!m_sContent.empty())&&(!m_sRegex.empty()))
	{
		try {
			std::set<std::wstring> autolist;
			CHighResClock timer;

			auto regCheck = str::regex_match<wchar_t>(m_sRegex, std::regex_constants::icase | std::regex_constants::ECMAScript);
			for (const auto& match : regCheck(m_sContent)) {
				for (size_t i = 1; i < match.size(); ++i) {
					if (match[i].second - match[i].first) {
						ATLTRACE(L"matched keyword : %s\n", std::wstring(match[i]).c_str());
						auto result = std::wstring(match[i]);
						if (!result.empty()) {
							autolist.insert(result);
						}
					}
				}
			}

			timer.Stop();
			for (const auto& append : autolist)
			{
				m_sResult += append.c_str();
				m_sResult += L"\r\n";
			}
			m_sTimingLabel = std::format(L"Parse time: {} uSecs", timer.GetMusecsTaken());
		}
		catch (std::exception &ex)
		{
			m_sResult = L"Regex is invalid!\r\n" + CString(ex.what());
		}
	}
	UpdateData(FALSE);
}
