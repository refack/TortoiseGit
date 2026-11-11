module;
#include "stdafx.h"
#include "../TortoiseProc/Settings/SettingsPropPage.h"

#include "Resources/resource.h"


export module SettingGitCredential;
import tgit;
import TortoiseProc;
import TGitPath;	// GitAdminDir lives in the TGitPath module
import TGitMessageBox;
import AppUtils.Launch;
import Registry;
using namespace libgit2;


namespace SimpleCredentialType
{
	static int Advanced = -1;
	static int None = -1;
	static int LocalWincred = -1;
	static int LocalWinstore = -1;
	static int LocalGCM = -1;
	static int LocalGCMCore = -1;
	static int GlobalWincred = -1;
	static int GlobalWinstore = -1;
	static int GlobalGCM = -1;
	static int GlobalGCMCore = -1;
	static int SystemWincred = -1;
	static int SystemGCM = -1;
	static int SystemGCMCore = -1;
}

namespace ConfigType
{
	static int Local = -1;
	static int Global = -1;
	static int System = -1;
}


// CSettingGitCredential dialog
export class CSettingGitCredential : public ISettingsPropPage
{
	DECLARE_DYNAMIC(CSettingGitCredential)

public:
	enum
	{
		CREDENTIAL_URL			= 0x01,
		CREDENTIAL_HELPER		= 0x02,
		CREDENTIAL_USERNAME		= 0x04,
		CREDENTIAL_USEHTTPPATH	= 0x08,
		CREDENTIAL_ADVANCED_MASK= 0x0F,
		CREDENTIAL_SIMPLE		= 0x10,
	};
	CSettingGitCredential();
	virtual ~CSettingGitCredential();
	UINT GetIconID() override { return IDI_GITCREDENTIAL; }
// Dialog Data
	enum { IDD = IDD_SETTINGSCREDENTIAL };

 	static int GetCredentialDefaultUrlCallback(const git_config_entry *entry, void *payload)
	{
		static_cast<std::vector<std::wstring>*>(payload)->push_back(std::wstring(ConfigLevelToKey(entry->level)));
		return 0;
	}

	static int GetCredentialUrlCallback(const git_config_entry *entry, void *payload)
	{
		const CString name = CUnicodeUtils::GetUnicode<CString>(entry->name);
		const int pos1 = name.Find(L'.');
		const int pos2 = name.ReverseFind(L'.');
		const CString url = name.Mid(pos1 + 1, pos2 - pos1 - 1);
		CString display;
		display.Format(L"%s:%s", static_cast<LPCWSTR>(ConfigLevelToKey(entry->level)), static_cast<LPCWSTR>(url));
		static_cast<std::vector<std::wstring>*>(payload)->push_back(std::wstring(display));
		return 0;
	}

	static int GetCredentialEntryCallback(const git_config_entry *entry, void *payload)
	{
		const CString name = CUnicodeUtils::GetUnicode<CString>(entry->name);
		static_cast<std::vector<std::wstring>*>(payload)->push_back(std::wstring(name));
		return 0;
	}

	static int GetCredentialAnyEntryCallback(const git_config_entry *entry, void *payload)
	{
		const CString name = CUnicodeUtils::GetUnicode<CString>(entry->name);
		CString value;
		if (entry->value)
			value = CUnicodeUtils::GetUnicode<CString>(entry->value);
		CString text;
		text.Format(L"%s\n%s\n%s", static_cast<LPCWSTR>(ConfigLevelToKey(entry->level)), static_cast<LPCWSTR>(name), static_cast<LPCWSTR>(value));
		static_cast<std::vector<std::wstring>*>(payload)->push_back(std::wstring(text));
		return 0;
	}

	static CString GetWinstorePath()
	{
		wchar_t winstorebuf[MAX_PATH] = { 0 };
		ExpandEnvironmentStrings(L"%AppData%\\GitCredStore\\git-credential-winstore.exe", winstorebuf, _countof(winstorebuf));
		CString winstore;
		winstore.Format(L"!'%s'", winstorebuf);
		return winstore;
	}

	static bool WincredExists()
	{
		CString path = GitCall::ms_MsysGitRootDir.c_str();
		path.Append(L"libexec\\git-core\\git-credential-wincred.exe");
		return !!PathFileExists(path);
	}

	static bool WinstoreExists()
	{
		return !!PathFileExists(GetWinstorePath());
	}

	static bool GCMExists()
	{
		CString path = GitCall::ms_MsysGitRootDir.c_str();
		path.Append(L"libexec\\git-core\\git-credential-manager.exe");
		if (!PathFileExists(path))
			return false;
		// CCM requires .NET 4.5.1 or later
		// try to detect it (only works for >=4.5): https://msdn.microsoft.com/en-us/library/hh925568
		return CRegDWORD(L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full\\Release", 0, false, HKEY_LOCAL_MACHINE) >= 378675;
	}

	static bool GCMCoreExists()
	{
		CString path = GitCall::ms_MsysGitRootDir.c_str();
		path.Append(L"libexec\\git-core\\git-credential-manager-core.exe");
		return !!PathFileExists(path);
	}

protected:
	static CString ConfigLevelToKey(git_config_level_t level)
	{
		switch (level)
		{
		case git_config_level_t::GIT_CONFIG_LEVEL_PROGRAMDATA:
			return L"P";
		case git_config_level_t::GIT_CONFIG_LEVEL_SYSTEM:
			return L"S";
		case git_config_level_t::GIT_CONFIG_LEVEL_XDG:
			return L"X";
		case git_config_level_t::GIT_CONFIG_LEVEL_GLOBAL:
			return L"G";
		default:
			return L"L";
		}
	}

	void DoDataExchange(CDataExchange* pDX) override;    // DDX/DDV support

	DECLARE_MESSAGE_MAP()

	afx_msg void OnCbnSelchangeComboSimplecredential();
	afx_msg void OnBnClickedButtonAdd();
	afx_msg void OnLbnSelchangeListUrl();
	afx_msg void OnCbnSelchangeComboConfigType();
	afx_msg void OnEnChangeEditUrl();
	afx_msg void OnEnChangeEditHelper();
	afx_msg void OnEnChangeEditUsername();
	afx_msg void OnBnClickedCheckUsehttppath();
	afx_msg void OnBnClickedButtonRemove();
	afx_msg void OnBnClickedOpensettingselevated();
	afx_msg void OnBnClickedWindowscredmgr();

	BOOL OnInitDialog() override;
	BOOL OnApply() override;

	void EnableAdvancedOptions();
	BOOL IsUrlExist(const CString& text);

	void AddConfigType(int& index, const CString& text, bool add = true);
	void AddSimpleCredential(int& index, const CString& text, bool add = true);
	void FillSimpleList(bool addNone, bool systemWincred, bool systemGCM, bool systemGCMCore);
	void LoadList();
	CString Load(const CString& key);
	void Save(const CString& key, const CString& value);
	int DeleteOtherKeys(int type);
	bool SaveSimpleCredential(int type);
	bool SaveSettings();

	int			m_ChangedMask = 0;
	int			m_iSimpleStoredValue; // the SimpleCredential value initially read from config

	CComboBox	m_ctrlSimpleCredential;
	CListBox	m_ctrlUrlList;
	CComboBox	m_ctrlConfigType;
	CString		m_strUrl;
	CString		m_strHelper;
	CString		m_strUsername;
	BOOL		m_bUseHttpPath;
};



// CSettingGitCredential dialog

IMPLEMENT_DYNAMIC(CSettingGitCredential, ISettingsPropPage)

CSettingGitCredential::CSettingGitCredential()
	: ISettingsPropPage(CSettingGitCredential::IDD)
	, m_bUseHttpPath(FALSE)
	, m_iSimpleStoredValue(-2)
{
}

CSettingGitCredential::~CSettingGitCredential()
{
}

void CSettingGitCredential::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COMBO_SIMPLECREDENTIAL, m_ctrlSimpleCredential);
	DDX_Control(pDX, IDC_LIST_REMOTE, m_ctrlUrlList);
	DDX_Text(pDX, IDC_EDIT_URL, m_strUrl);
	DDX_Text(pDX, IDC_COMBO_HELPER, m_strHelper);
	DDX_Text(pDX, IDC_EDIT_USERNAME, m_strUsername);
	DDX_Check(pDX, IDC_CHECK_USEHTTPPATH, m_bUseHttpPath);
	DDX_Control(pDX, IDC_COMBO_CONFIGTYPE, m_ctrlConfigType);
}


BEGIN_MESSAGE_MAP(CSettingGitCredential, CPropertyPage)
	ON_CBN_SELCHANGE(IDC_COMBO_SIMPLECREDENTIAL, &CSettingGitCredential::OnCbnSelchangeComboSimplecredential)
	ON_BN_CLICKED(IDC_BUTTON_ADD, &CSettingGitCredential::OnBnClickedButtonAdd)
	ON_LBN_SELCHANGE(IDC_LIST_REMOTE, &CSettingGitCredential::OnLbnSelchangeListUrl)
	ON_CBN_SELCHANGE(IDC_COMBO_CONFIGTYPE, &CSettingGitCredential::OnCbnSelchangeComboConfigType)
	ON_EN_CHANGE(IDC_EDIT_URL, &CSettingGitCredential::OnEnChangeEditUrl)
	ON_CBN_EDITCHANGE(IDC_COMBO_HELPER, &CSettingGitCredential::OnEnChangeEditHelper)
	ON_CBN_SELCHANGE(IDC_COMBO_HELPER, &CSettingGitCredential::OnEnChangeEditHelper)
	ON_EN_CHANGE(IDC_EDIT_USERNAME, &CSettingGitCredential::OnEnChangeEditUsername)
	ON_BN_CLICKED(IDC_CHECK_USEHTTPPATH, &CSettingGitCredential::OnBnClickedCheckUsehttppath)
	ON_BN_CLICKED(IDC_BUTTON_REMOVE, &CSettingGitCredential::OnBnClickedButtonRemove)
	ON_BN_CLICKED(IDC_OPENSETTINGSELEVATED, &CSettingGitCredential::OnBnClickedOpensettingselevated)
	ON_BN_CLICKED(IDC_WINDOWSCREDMGR, &CSettingGitCredential::OnBnClickedWindowscredmgr)
END_MESSAGE_MAP()

static CStringA RegexEscape(CStringA str)
{
	CStringA result;
	for (int i = 0; i < str.GetLength(); ++i)
	{
		const char c = str[i];
		switch (c)
		{
			case '\\': case '*': case '+': case '?': case '|':
			case '{': case '[': case '(': case ')': case '^':
			case '$': case '.': case '#': case ' ':
				result.AppendChar('\\');
				result.AppendChar(c);
				break;
			case '\t': result.Append("\\t"); break;
			case '\n': result.Append("\\n"); break;
			case '\r': result.Append("\\r"); break;
			case '\f': result.Append("\\f"); break;
			default: result.AppendChar(c); break;
		}
	}

	return result;
}

BOOL CSettingGitCredential::OnInitDialog()
{
	ISettingsPropPage::OnInitDialog();

	AdjustControlSize(IDC_CHECK_USEHTTPPATH);

	const bool hasLocal = !GitAdminDir::HasAdminDir(GitCall::m_CurrentDirStorage).empty();

	m_ctrlUrlList.ResetContent();

	AddConfigType(ConfigType::Local, CString(MAKEINTRESOURCE(IDS_SETTINGS_LOCAL)), hasLocal);
	AddConfigType(ConfigType::Global, CString(MAKEINTRESOURCE(IDS_SETTINGS_GLOBAL)));
	AddConfigType(ConfigType::System, CString(MAKEINTRESOURCE(IDS_SETTINGS_SYSTEM)));
	m_ctrlConfigType.SetCurSel(0);

	if (WincredExists())
		static_cast<CComboBox*>(GetDlgItem(IDC_COMBO_HELPER))->AddString(L"wincred");
	if (WinstoreExists())
		static_cast<CComboBox*>(GetDlgItem(IDC_COMBO_HELPER))->AddString(GetWinstorePath());
	if (GCMExists())
		static_cast<CComboBox*>(GetDlgItem(IDC_COMBO_HELPER))->AddString(L"manager");
	if (GCMCoreExists())
		static_cast<CComboBox*>(GetDlgItem(IDC_COMBO_HELPER))->AddString(L"manager-core");

	LoadList();
	m_iSimpleStoredValue = m_ctrlSimpleCredential.GetCurSel();

	if (!TortoiseProc::IsAdminLogin())
	{
		reinterpret_cast<CButton*>(GetDlgItem(IDC_OPENSETTINGSELEVATED))->SetShield(TRUE);
		GetDlgItem(IDC_OPENSETTINGSELEVATED)->EnableWindow(TRUE);
		GetDlgItem(IDC_OPENSETTINGSELEVATED)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_STATICELEVATIONNEEDED)->ShowWindow(SW_SHOW);
	}

	EnableAdvancedOptions();

	UpdateData(FALSE);
	return TRUE;
}
// CSettingGitCredential message handlers

void CSettingGitCredential::OnCbnSelchangeComboSimplecredential()
{
	EnableAdvancedOptions();
	SetModified();
	m_ChangedMask |= CREDENTIAL_SIMPLE;
}

void CSettingGitCredential::OnBnClickedButtonAdd()
{
	UpdateData();

	if (m_strHelper.Trim().IsEmpty())
	{
		CMessageBox::Show(GetSafeHwnd(), IDS_GITCREDENTIAL_HELPEREMPTY, IDS_APPNAME, MB_OK | MB_ICONERROR);
		return;
	}

	m_ChangedMask = CREDENTIAL_URL | CREDENTIAL_HELPER | CREDENTIAL_USERNAME | CREDENTIAL_USEHTTPPATH;
	const int sel = m_ctrlConfigType.GetCurSel();
	const CString prefix = sel == ConfigType::System ? L"S" : sel == ConfigType::Global ? L"G" : L"L";
	CString text;
	if (!m_strUrl.IsEmpty())
		text.Format(L"%s:%s", static_cast<LPCWSTR>(prefix), static_cast<LPCWSTR>(m_strUrl));
	else
		text = prefix;
	if (IsUrlExist(text))
	{
		CString msg;
		msg.Format(IDS_GITCREDENTIAL_OVERWRITEHELPER, static_cast<LPCWSTR>(m_strUrl));
		if (CMessageBox::Show(GetSafeHwnd(), msg, L"TortoiseGit", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
			m_ChangedMask &= ~CREDENTIAL_URL;
		else
			return;
	}
	else
	{
		if (m_strUsername.IsEmpty())
			m_ChangedMask &= ~CREDENTIAL_USERNAME;
		if (!m_bUseHttpPath)
			m_ChangedMask &= ~CREDENTIAL_USEHTTPPATH;
	}

	SaveSettings();
}

void CSettingGitCredential::EnableAdvancedOptions()
{
	const BOOL enable = m_ctrlSimpleCredential.GetCurSel() == SimpleCredentialType::Advanced ? TRUE : FALSE;
	GetDlgItem(IDC_LIST_REMOTE)->EnableWindow(enable);
	GetDlgItem(IDC_EDIT_URL)->EnableWindow(enable);
	GetDlgItem(IDC_COMBO_HELPER)->EnableWindow(enable);
	GetDlgItem(IDC_EDIT_USERNAME)->EnableWindow(enable);
	GetDlgItem(IDC_COMBO_CONFIGTYPE)->EnableWindow(enable);
	GetDlgItem(IDC_CHECK_USEHTTPPATH)->EnableWindow(enable);

	const bool canModifySystem = TortoiseProc::IsAdminLogin() || (m_ctrlConfigType.GetCurSel() != ConfigType::System);
	GetDlgItem(IDC_BUTTON_ADD)->EnableWindow(enable && canModifySystem);
	GetDlgItem(IDC_BUTTON_REMOVE)->EnableWindow(enable && canModifySystem);
}

BOOL CSettingGitCredential::IsUrlExist(const CString& text)
{
	CString str;
	for(int i = 0; i < m_ctrlUrlList.GetCount();i++)
	{
		m_ctrlUrlList.GetText(i, str);
		if (str == text)
			return true;
	}
	return false;
}

void CSettingGitCredential::OnLbnSelchangeListUrl()
{
	CWaitCursor wait;

	if (m_ChangedMask & CREDENTIAL_ADVANCED_MASK)
	{
		if (CMessageBox::Show(GetSafeHwnd(), IDS_GITCREDENTIAL_SAVEHELPER, IDS_APPNAME, 1, IDI_QUESTION, IDS_SAVEBUTTON, IDS_DISCARDBUTTON) == 1)
			SaveSettings();
	}
	m_ChangedMask &= ~CREDENTIAL_ADVANCED_MASK;
	if (!m_ChangedMask)
		SetModified(FALSE);

	CString cmd, output;
	const int index = m_ctrlUrlList.GetCurSel();
	if (index < 0)
	{
		m_strHelper.Empty();
		m_strUrl.Empty();
		m_strHelper.Empty();
		m_bUseHttpPath = FALSE;
		UpdateData(FALSE);
		return;
	}

	CString text;
	m_ctrlUrlList.GetText(index, text);
	const int pos = text.Find(L':');
	const CString prefix = pos >= 0 ? text.Left(pos) : text.Left(1);
	m_ctrlConfigType.SetCurSel((prefix == L"S" || prefix == L"P") ? ConfigType::System : prefix == L"X" ? ConfigType::Global : prefix == L"G" ? ConfigType::Global : ConfigType::Local);
	m_strUrl = pos >= 0 ? text.Mid(pos + 1) : CString();

	m_strHelper = Load(L"helper");
	m_strUsername = Load(L"username");
	m_bUseHttpPath = Load(L"useHttpPath") == L"true" ? TRUE : FALSE;

	const bool canModifySystem = TortoiseProc::IsAdminLogin() || (m_ctrlConfigType.GetCurSel() != ConfigType::System);
	GetDlgItem(IDC_BUTTON_ADD)->EnableWindow(canModifySystem ? TRUE : FALSE);
	GetDlgItem(IDC_BUTTON_REMOVE)->EnableWindow(canModifySystem ? TRUE : FALSE);
	this->UpdateData(FALSE);
}

void CSettingGitCredential::OnCbnSelchangeComboConfigType()
{
	if (!TortoiseProc::IsAdminLogin())
	{
		const int sel = m_ctrlConfigType.GetCurSel();
		GetDlgItem(IDC_BUTTON_ADD)->EnableWindow(sel != ConfigType::System);
		GetDlgItem(IDC_BUTTON_REMOVE)->EnableWindow(sel != ConfigType::System);
		if (sel == ConfigType::System)
			return;
	}
	SetModified();
}

void CSettingGitCredential::OnEnChangeEditUrl()
{
	m_ChangedMask |= CREDENTIAL_URL;
	SetModified();
}

void CSettingGitCredential::OnEnChangeEditHelper()
{
	m_ChangedMask |= CREDENTIAL_HELPER;

	UpdateData();
	if (!m_strHelper.IsEmpty())
		SetModified();
	else if (!m_ChangedMask)
		SetModified(0);
}

void CSettingGitCredential::OnEnChangeEditUsername()
{
	m_ChangedMask |= CREDENTIAL_USERNAME;
	SetModified();
}

void CSettingGitCredential::OnBnClickedCheckUsehttppath()
{
	m_ChangedMask |= CREDENTIAL_USEHTTPPATH;
	SetModified();
}

void CSettingGitCredential::AddConfigType(int& index, const CString& text, bool add)
{
	if (add)
		index = m_ctrlConfigType.AddString(text);
}

void CSettingGitCredential::AddSimpleCredential(int& index, const CString& text, bool add)
{
	if (add)
		index = m_ctrlSimpleCredential.AddString(text);
}

void CSettingGitCredential::FillSimpleList(bool anySystem, bool systemWincred, bool systemGCM, bool systemGCMCore)
{
	ATLASSERT(SimpleCredentialType::Advanced == -1);

	const bool hasLocal = !GitAdminDir::HasAdminDir(GitCall::m_CurrentDirStorage).empty();
	const bool isAdmin = TortoiseProc::IsAdminLogin();

	AddSimpleCredential(SimpleCredentialType::Advanced, CString(MAKEINTRESOURCE(IDS_ADVANCED)));
	if (isAdmin || !anySystem)
		AddSimpleCredential(SimpleCredentialType::None, CString(MAKEINTRESOURCE(IDS_NONE)));
	if (isAdmin || !(systemWincred || systemGCM || systemGCMCore || anySystem))
	{
		AddSimpleCredential(SimpleCredentialType::LocalWincred, CString(MAKEINTRESOURCE(IDS_LOCAL_WINCRED)), hasLocal && WincredExists());
		AddSimpleCredential(SimpleCredentialType::LocalWinstore, CString(MAKEINTRESOURCE(IDS_LOCAL_WINSTORE)), hasLocal && WinstoreExists());
		AddSimpleCredential(SimpleCredentialType::LocalGCM, CString(MAKEINTRESOURCE(IDS_LOCAL_GCM)), hasLocal && GCMExists());
		AddSimpleCredential(SimpleCredentialType::LocalGCMCore, CString(MAKEINTRESOURCE(IDS_LOCAL_GCMCORE)), hasLocal && GCMCoreExists());
		AddSimpleCredential(SimpleCredentialType::GlobalWincred, CString(MAKEINTRESOURCE(IDS_GLOBAL_WINCRED)), WincredExists());
		AddSimpleCredential(SimpleCredentialType::GlobalWinstore, CString(MAKEINTRESOURCE(IDS_GLOBAL_WINSTORE)), WinstoreExists());
		AddSimpleCredential(SimpleCredentialType::GlobalGCM, CString(MAKEINTRESOURCE(IDS_GLOBAL_GCM)), GCMExists());
		AddSimpleCredential(SimpleCredentialType::GlobalGCMCore, CString(MAKEINTRESOURCE(IDS_GLOBAL_GCMCORE)), GCMCoreExists());
	}
	if (isAdmin || systemWincred)
		AddSimpleCredential(SimpleCredentialType::SystemWincred, CString(MAKEINTRESOURCE(IDS_SYSTEM_WINCRED)), WincredExists());
	if (isAdmin || systemGCM)
		AddSimpleCredential(SimpleCredentialType::SystemGCM, CString(MAKEINTRESOURCE(IDS_SYSTEM_GCM)), GCMExists());
	if (isAdmin || systemGCMCore)
		AddSimpleCredential(SimpleCredentialType::SystemGCMCore, CString(MAKEINTRESOURCE(IDS_SYSTEM_GCMCORE)), GCMCoreExists());
}

void CSettingGitCredential::LoadList()
{
	wil::unique_git_config config;
	git_config_new(config.put());
	const wil::unique_git_repository repo(g_Git.GetGitRepository());
	git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitLocalConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_LOCAL, repo.get(), FALSE);
	git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_GLOBAL, repo.get(), FALSE);
	git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalXDGConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_XDG, repo.get(), FALSE);
	git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitSystemConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_SYSTEM, repo.get(), FALSE);

	std::vector<std::wstring> defaultList, urlList;
	git_config_foreach_match(config.get(), "credential\\.helper", GetCredentialDefaultUrlCallback, &defaultList);
	git_config_foreach_match(config.get(), "credential\\..*\\.helper", GetCredentialUrlCallback, &urlList);
	std::vector<std::wstring> anyList;
	git_config_foreach_match(config.get(), "credential\\..*", GetCredentialAnyEntryCallback, &anyList);

	for (size_t i = 0; i < defaultList.size(); ++i)
		m_ctrlUrlList.AddString(defaultList[i].c_str());
	for (size_t i = 0; i < urlList.size(); ++i)
		m_ctrlUrlList.AddString(urlList[i].c_str());

	if (anyList.empty())
	{
		FillSimpleList(false, false, false, false);
		m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::None);
		return;
	}
	if (anyList.size() > 1)
	{
		bool hasSystemType = false;
		for (size_t i = 0; i < anyList.size(); ++i)
		{
			int pos = 0;
			CString prefix = tgit::wstr::Tokenize(anyList[i], L"\n", pos).c_str();
			if (prefix == L"S" || prefix == L"P")
			{
				hasSystemType = true;
				break;
			}
		}
		FillSimpleList(hasSystemType, false, false, false);
		m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::Advanced);
		return;
	}

	int pos = 0;
	const CString prefix = tgit::wstr::Tokenize(anyList[0], L"\n", pos).c_str();
	const CString key = tgit::wstr::Tokenize(anyList[0], L"\n", pos).c_str();
	const CString value = tgit::wstr::Tokenize(anyList[0], L"\n", pos).c_str();
	if (key != L"credential.helper")
	{
		FillSimpleList(true, false, false, false);
		m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::Advanced);
		return;
	}

	const CString winstore = GetWinstorePath();
	if (prefix == L"L")
	{
		if (value == L"wincred")
		{
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::LocalWincred);
			return;
		}
		else if (value == winstore)
		{
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::LocalWinstore);
			return;
		}
		else if (value == L"manager")
		{
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::LocalGCM);
			return;
		}
		else if (value == L"manager-core")
		{
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::LocalGCMCore);
			return;
		}
	}

	if (prefix == L"G" || prefix == L"X")
	{
		if (value == L"wincred")
		{
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::GlobalWincred);
			return;
		}
		else if (value == winstore)
		{
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::GlobalWinstore);
			return;
		}
		else if (value == L"manager")
		{
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::GlobalGCM);
			return;
		}
		else if (value == L"manager-core")
		{
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::GlobalGCMCore);
			return;
		}
	}

	if (prefix == L"S" || prefix == L"P")
	{
		if (value == L"wincred")
		{
			FillSimpleList(true, true, false, false);
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::SystemWincred);
			return;
		}
		else if (value == L"manager")
		{
			FillSimpleList(true, false, true, false);
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::SystemGCM);
			return;
		}
		else if (value == L"manager-core")
		{
			FillSimpleList(true, false, false, true);
			m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::SystemGCMCore);
			return;
		}
	}

	FillSimpleList((prefix == L"S" || prefix == L"P"), false, false, false);
	m_ctrlSimpleCredential.SetCurSel(SimpleCredentialType::Advanced);
}

CString CSettingGitCredential::Load(const CString& key)
{
	CString cmd;

	if (m_strUrl.IsEmpty())
		cmd.Format(L"credential.%s", static_cast<LPCWSTR>(key));
	else
		cmd.Format(L"credential.%s.%s", static_cast<LPCWSTR>(m_strUrl), static_cast<LPCWSTR>(key));

	wil::unique_git_config config;
	git_config_new(config.put());
	const wil::unique_git_repository repo(g_Git.GetGitRepository());
	const int sel = m_ctrlConfigType.GetCurSel();
	if (sel == ConfigType::Local)
		git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitLocalConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_LOCAL, repo.get(), FALSE);
	else if (sel == ConfigType::Global)
	{
		git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_GLOBAL, repo.get(), FALSE);
		git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalXDGConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_XDG, repo.get(), FALSE);
	}
	else if (sel == ConfigType::System)
		git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitSystemConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_SYSTEM, repo.get(), FALSE);

	const CStringA cmdA = CUnicodeUtils::GetUTF8<CStringA>(cmd);
	CStringA valueA;
	git_config_entry* entry;
	if (!git_config_get_entry(&entry, config.get(), cmdA))
		valueA = CStringA(entry->value);

	return CUnicodeUtils::GetUnicode<CString>(valueA);
}

void CSettingGitCredential::Save(const CString& key, const CString& value)
{
	CString cmd, out;

	if (m_strUrl.IsEmpty())
		cmd.Format(L"credential.%s", static_cast<LPCWSTR>(key));
	else
		cmd.Format(L"credential.%s.%s", static_cast<LPCWSTR>(m_strUrl), static_cast<LPCWSTR>(key));

	const int sel = m_ctrlConfigType.GetCurSel();
	const CONFIG_TYPE configLevel = sel == ConfigType::System ? CONFIG_TYPE::CONFIG_SYSTEM : sel == ConfigType::Global ? CONFIG_TYPE::CONFIG_GLOBAL : CONFIG_TYPE::CONFIG_LOCAL;

	const bool old = g_Git.m_IsUseGitDLL;
	// workaround gitdll bug
	// TODO: switch to libgit2
	g_Git.m_IsUseGitDLL = false;
	if (g_Git.SetConfigValue(cmd, value, configLevel))
	{
		CString msg;
		msg.FormatMessage(IDS_PROC_SAVECONFIGFAILED, static_cast<LPCWSTR>(cmd), static_cast<LPCWSTR>(value));
		CMessageBox::Show(GetSafeHwnd(), msg, L"TortoiseGit", MB_OK | MB_ICONERROR);
	}
	if (value.IsEmpty())
	{
		if (g_Git.UnsetConfigValue(cmd, configLevel))
		{
			CString msg;
			msg.FormatMessage(IDS_PROC_SAVECONFIGFAILED, static_cast<LPCWSTR>(cmd), static_cast<LPCWSTR>(value));
			CMessageBox::Show(GetSafeHwnd(), msg, L"TortoiseGit", MB_OK | MB_ICONERROR);
		}
	}
	g_Git.m_IsUseGitDLL = old;
}

int CSettingGitCredential::DeleteOtherKeys(int type)
{
	CString match;
	if (type == SimpleCredentialType::LocalWincred)
		match = L"L\ncredential.helper\nwincred";
	else if (type == SimpleCredentialType::LocalWinstore)
		match = L"L\ncredential.helper\n" + GetWinstorePath();
	else if (type == SimpleCredentialType::LocalGCM)
		match = L"L\ncredential.helper\nmanager";
	else if (type == SimpleCredentialType::LocalGCMCore)
		match = L"L\ncredential.helper\nmanager-core";
	else if (type == SimpleCredentialType::GlobalWincred)
		match = L"G\ncredential.helper\nwincred";
	else if (type == SimpleCredentialType::GlobalWinstore)
		match = L"G\ncredential.helper\n" + GetWinstorePath();
	else if (type == SimpleCredentialType::GlobalGCM)
		match = L"G\ncredential.helper\nmanager";
	else if (type == SimpleCredentialType::GlobalGCMCore)
		match = L"G\ncredential.helper\nmanager-core";
	else if (type == SimpleCredentialType::SystemWincred)
		match = L"S\ncredential.helper\nwincred";
	else if (type == SimpleCredentialType::SystemGCM)
		match = L"S\ncredential.helper\nmanager";
	else if (type == SimpleCredentialType::SystemGCMCore)
		match = L"S\ncredential.helper\nmanager-core";

	wil::unique_git_config config;
	git_config_new(config.put());
	const wil::unique_git_repository repo = g_Git.GetGitRepository();
	git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitLocalConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_LOCAL, repo.get(), FALSE);
	git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_GLOBAL, repo.get(), FALSE);
	git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalXDGConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_XDG, repo.get(), FALSE);
	git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitSystemConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_SYSTEM, repo.get(), FALSE);

	std::vector<std::wstring> list;
	git_config_foreach_match(config.get(), "credential\\..*", GetCredentialAnyEntryCallback, &list);
	for (size_t i = 0; i < list.size(); ++i)
	{
		int pos = 0;
		CString prefix = tgit::wstr::Tokenize(list[i], L"\n", pos).c_str();
		if ((prefix == L"S" || prefix == L"P") && list[i] != tgit::str::StringView(match) && !TortoiseProc::IsAdminLogin())
		{
			if (MessageBox(L"Cannot modify a system config without proper rights.\nDiscard changes?", L"TortoiseGit", MB_ICONERROR | MB_YESNO) == IDYES)
				return 0;
			return -1;
		}
	}

	// workaround gitdll bug
	// TODO: switch to libgit2
	const bool old = g_Git.m_IsUseGitDLL;
	g_Git.m_IsUseGitDLL = false;
	SCOPE_EXIT { g_Git.m_IsUseGitDLL = old; };
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (list[i] == tgit::str::StringView(match))
			continue;

		int pos = 0;
		CString prefix = tgit::wstr::Tokenize(list[i], L"\n", pos).c_str();
		CString key = tgit::wstr::Tokenize(list[i], L"\n", pos).c_str();
		const CONFIG_TYPE configLevel = (prefix == L"S" || prefix == L"P") ? CONFIG_TYPE::CONFIG_SYSTEM : prefix == L"G" || prefix == L"X" ? CONFIG_TYPE::CONFIG_GLOBAL : CONFIG_TYPE::CONFIG_LOCAL;
		if (g_Git.UnsetConfigValue(key, configLevel))
		{
			CString msg;
			msg.FormatMessage(IDS_PROC_SAVECONFIGFAILED, static_cast<LPCWSTR>(key), L"");
			CMessageBox::Show(GetSafeHwnd(), msg, L"TortoiseGit", MB_OK | MB_ICONERROR);
			return 1;
		}
	}

	return 0;
}

bool CSettingGitCredential::SaveSimpleCredential(int type)
{
	CONFIG_TYPE configLevel;
	CString value;
	if (type == SimpleCredentialType::LocalWincred)
	{
		configLevel = CONFIG_TYPE::CONFIG_LOCAL;
		value = L"wincred";
	}
	else if (type == SimpleCredentialType::LocalWinstore)
	{
		configLevel = CONFIG_TYPE::CONFIG_LOCAL;
		value = GetWinstorePath();
	}
	else if (type == SimpleCredentialType::LocalGCM)
	{
		configLevel = CONFIG_TYPE::CONFIG_LOCAL;
		value = L"manager";
	}
	else if (type == SimpleCredentialType::LocalGCMCore)
	{
		configLevel = CONFIG_TYPE::CONFIG_LOCAL;
		value = L"manager-core";
	}
	else if (type == SimpleCredentialType::GlobalWincred)
	{
		configLevel = CONFIG_TYPE::CONFIG_GLOBAL;
		value = L"wincred";
	}
	else if (type == SimpleCredentialType::GlobalWinstore)
	{
		configLevel = CONFIG_TYPE::CONFIG_GLOBAL;
		value = GetWinstorePath();
	}
	else if (type == SimpleCredentialType::GlobalGCM)
	{
		configLevel = CONFIG_TYPE::CONFIG_GLOBAL;
		value = L"manager";
	}
	else if (type == SimpleCredentialType::GlobalGCMCore)
	{
		configLevel = CONFIG_TYPE::CONFIG_GLOBAL;
		value = L"manager-core";
	}
	else if (type == SimpleCredentialType::SystemWincred)
	{
		configLevel = CONFIG_TYPE::CONFIG_SYSTEM;
		value = L"wincred";
	}
	else if (type == SimpleCredentialType::SystemGCM)
	{
		configLevel = CONFIG_TYPE::CONFIG_SYSTEM;
		value = L"manager";
	}
	else if (type == SimpleCredentialType::SystemGCMCore)
	{
		configLevel = CONFIG_TYPE::CONFIG_SYSTEM;
		value = L"manager-core";
	}
	else
		return true;

	// workaround gitdll bug
	// TODO: switch to libgit2
	const bool old = g_Git.m_IsUseGitDLL;
	g_Git.m_IsUseGitDLL = false;
	if (g_Git.SetConfigValue(L"credential.helper", value, configLevel))
	{
		CString msg;
		msg.FormatMessage(IDS_PROC_SAVECONFIGFAILED, L"credential.helper", static_cast<LPCWSTR>(value));
		CMessageBox::Show(GetSafeHwnd(), msg, L"TortoiseGit", MB_OK | MB_ICONERROR);
		return false;
	}
	g_Git.m_IsUseGitDLL = old;
	return true;
}

BOOL CSettingGitCredential::OnApply()
{
	if (!m_ChangedMask)
		return ISettingsPropPage::OnApply();

	if (SaveSettings())
		return ISettingsPropPage::OnApply();
	else
		return FALSE;
}

bool CSettingGitCredential::SaveSettings()
{
	CWaitCursor wait;
	UpdateData();

	const int type = m_ctrlSimpleCredential.GetCurSel();
	if ((type == SimpleCredentialType::SystemWincred || type == SimpleCredentialType::SystemGCM || type == SimpleCredentialType::SystemGCMCore) && !TortoiseProc::IsAdminLogin())
	{
		if (m_iSimpleStoredValue == type)
		{
			if (DeleteOtherKeys(type) > 0)
				return false;
			m_ChangedMask = 0;
			return true;
		}

		if (MessageBox(L"Cannot modify a system config without proper rights.\nDiscard changes?", L"TortoiseGit", MB_ICONERROR | MB_YESNO) == IDYES)
			return true;
		return false;
	}
	if (type != SimpleCredentialType::Advanced)
	{
		if (!SaveSimpleCredential(type))
			return false;

		if (DeleteOtherKeys(type) > 0)
			return false;
		m_ChangedMask = 0;
		return true;
	}

	int sel = m_ctrlConfigType.GetCurSel();
	if (sel == ConfigType::System && !TortoiseProc::IsAdminLogin())
	{
		if (MessageBox(L"Cannot modify a system config without proper rights.\nDiscard changes?", L"TortoiseGit", MB_ICONERROR | MB_YESNO) == IDYES)
			return true;
		return false;
	}

	if (m_ChangedMask & CREDENTIAL_URL)
	{
		//Add Helper
		if (m_strHelper.IsEmpty())
		{
			CMessageBox::Show(GetSafeHwnd(), IDS_GITCREDENTIAL_HELPEREMPTY, IDS_APPNAME, MB_OK | MB_ICONERROR);
			return false;
		}
		m_strUrl.Replace(L'\\', L'/');
		m_strHelper.Replace(L'\\', L'/');

		Save(L"helper", m_strHelper);
		m_ChangedMask &= ~CREDENTIAL_HELPER;

		sel = m_ctrlConfigType.GetCurSel();
		const CString prefix = sel == ConfigType::System ? L"S" : sel == ConfigType::Global ? L"G" : L"L";
		CString text;
		if (!m_strUrl.IsEmpty())
			text.Format(L"%s:%s", static_cast<LPCWSTR>(prefix), static_cast<LPCWSTR>(m_strUrl));
		else
			text = prefix;
		const int urlIndex = m_ctrlUrlList.AddString(text);
		m_ctrlUrlList.SetCurSel(urlIndex);
		GetDlgItem(IDC_BUTTON_ADD)->EnableWindow(TRUE);
	}

	if (m_ChangedMask & CREDENTIAL_HELPER)
	{
		m_strHelper.Replace(L'\\', L'/');
		Save(L"helper", m_strHelper);
	}

	if (m_ChangedMask & CREDENTIAL_USERNAME)
		Save(L"username", m_strUsername);

	if (m_ChangedMask & CREDENTIAL_USEHTTPPATH)
		Save(L"useHttpPath", m_bUseHttpPath ? L"true" : L"");

	m_ChangedMask &= ~CREDENTIAL_ADVANCED_MASK;
	if (!m_ChangedMask)
		SetModified(FALSE);

	return true;
}

void CSettingGitCredential::OnBnClickedButtonRemove()
{
	const int index = m_ctrlUrlList.GetCurSel();
	if (index >= 0)
	{
		CString str;
		m_ctrlUrlList.GetText(index, str);
		CString msg;
		msg.Format(IDS_WARN_REMOVE, static_cast<LPCWSTR>(str));
		if (CMessageBox::Show(GetSafeHwnd(), msg, L"TortoiseGit", MB_YESNO | MB_ICONQUESTION) == IDYES)
		{
			wil::unique_git_config config;
			git_config_new(config.put());
			const wil::unique_git_repository repo(g_Git.GetGitRepository());
			const int pos = str.Find(L':');
			const CString prefix = pos >= 0 ? str.Left(pos) : str;
			const CString url = pos >= 0 ? str.Mid(pos + 1) : CString();
			CONFIG_TYPE configLevel = CONFIG_TYPE::CONFIG_LOCAL;
			switch (prefix[0])
			{
			case L'L':
				{
				configLevel = CONFIG_TYPE::CONFIG_LOCAL;
				git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitLocalConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_LOCAL, repo.get(), FALSE);
				break;
				}
			case L'G':
			case L'X':
				{
				configLevel = CONFIG_TYPE::CONFIG_GLOBAL;
				git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_GLOBAL, repo.get(), FALSE);
				git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalXDGConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_XDG, repo.get(), FALSE);
				break;
				}
			case L'P':
			case L'S':
				{
				if (!TortoiseProc::IsAdminLogin())
				{
					MessageBox(L"Cannot modify a system config without proper rights.", L"TortoiseGit", MB_ICONERROR);
					return;
				}
				configLevel = CONFIG_TYPE::CONFIG_SYSTEM;
				git_config_add_file_ondisk(config.get(), TGitPath::ToGitPathA(TGitH::GetGitSystemConfig().native()).c_str(), git_config_level_t::GIT_CONFIG_LEVEL_SYSTEM, repo.get(), FALSE);
				break;
				}
			}

			std::vector<std::wstring> list;
			const CStringA urlA = CUnicodeUtils::GetUTF8<CStringA>(url);
			const CStringA pattern = urlA.IsEmpty() ? CStringA("^credential\\.[^.]+$") : ("credential\\." + RegexEscape(urlA) + "\\..*");
			git_config_foreach_match(config.get(), pattern, GetCredentialEntryCallback, &list);
			for (size_t i = 0; i < list.size(); ++i)
				g_Git.UnsetConfigValue(list[i].c_str(), configLevel);
			m_ctrlUrlList.DeleteString(index);
			OnLbnSelchangeListUrl();
		}
	}
}

void CSettingGitCredential::OnBnClickedOpensettingselevated()
{
	CString sCmd;
	sCmd.Format(L"/command:settings /page:gitcredential /path:\"%s\"", GitCall::m_CurrentDirStorage.c_str());
	TortoiseProc::RunTortoiseGitProc(sCmd, true);
}

void CSettingGitCredential::OnBnClickedWindowscredmgr()
{
	// https://stackoverflow.com/a/31782500/3906760
	if (CComHeapPtr<WCHAR> wcharPtr; SUCCEEDED(SHGetKnownFolderPath(FOLDERID_System, 0, nullptr, &wcharPtr)))
	{
		const CString folder{ static_cast<LPCWSTR>(wcharPtr) };
		TortoiseProc::LaunchApplication(folder + L"\\rundll32.exe keymgr.dll,KRShowKeyMgr", TortoiseProc::LaunchApplicationFlags());
	}
}
