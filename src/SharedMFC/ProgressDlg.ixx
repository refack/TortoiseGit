module;
#include "stdafx.h"

#include <Mmsystem.h>
#include <Tlhelp32.h>
#include <wil/win32_helpers.h>	// textual std inside: must precede LogFile.h's imports

#include "ProtoModules/LoglistUtils.h"
#include "ProtoModules/LogFile.h"

#include "ProtoModules/GitCliOutputParser.h"


// NOLINTBEGIN(*-pro-type-reinterpret-cast)
export module ProgressDlg;
import tgit;
import TortoiseProc;
import AppUtils.Window;
import TGitMessageBox;
import CmdLineParser;
import IconMenu;
import StandAloneDlg;
import MenuButton;
import SysProgressDlg;
import TGitCache.CacheInterface;

using namespace tgit;
using namespace StandAloneDlg;
using namespace TGitCache;

// module-internal helper: kept outside the exported namespace so it keeps internal linkage
static void EnsurePostMessage(CWnd* pWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	redo:
		if (!pWnd->PostMessage(Msg, wParam, lParam))
		{
			if (GetLastError() == ERROR_NOT_ENOUGH_QUOTA)
			{
				Sleep(20);
				goto redo;
			}
			else
				TracePrintf(_T(__FUNCTION__) L": Message %d-%d could not be sent (error %d; %s)\n", wParam, lParam, GetLastError(), (wil::CFormatMessageWrapper()).c_str());
		}
}

export namespace ProgressDlg {
// constants, not macros: SyncDlg.cpp reuses them and macros do not cross an import
inline constexpr UINT MSG_PROGRESSDLG_UPDATE_UI = WM_USER + 121;

// CProgressDlg dialog
inline constexpr WPARAM MSG_PROGRESSDLG_START = 0;
inline constexpr WPARAM MSG_PROGRESSDLG_RUN = 50;
inline constexpr WPARAM MSG_PROGRESSDLG_END = 110;
inline constexpr WPARAM MSG_PROGRESSDLG_FAILED = 111;

enum class GitProgressAutoClose{
	AUTOCLOSE_NO,
	AUTOCLOSE_IF_NO_OPTIONS,
	AUTOCLOSE_IF_NO_ERRORS,
};

using PostCmdAction = std::function<void()>;

class PostCmd
{
public:
	PostCmd(UINT icon, UINT msgId, PostCmdAction action)
	: icon(icon)
	, action(action)
	{
		label.LoadString(msgId);
	}

	PostCmd(UINT msgId, PostCmdAction action)
	: PostCmd(0, msgId, action)
	{
	}

	PostCmd(UINT icon, const CString& label, PostCmdAction action)
	: icon(icon)
	, action(action)
	, label(label)
	{
	}

	PostCmd(const CString& label, PostCmdAction action)
	: PostCmd(0, label, action)
	{
	}

	UINT			icon;
	CString			label;
	PostCmdAction	action;
};

using PostCmdList = std::vector<PostCmd>;
using PostCmdCallback = std::function<void(DWORD status, PostCmdList&)>;
using PostExecCallback = std::function<void(HWND hWnd, DWORD& exitCode, CString& extraMsg)>;

class CProgressDlg : public CResizableStandAloneDialog
{
	DECLARE_DYNAMIC(CProgressDlg)

public:
	CProgressDlg(CWnd* pParent = nullptr); // standard constructor
	~CProgressDlg() override;

private:

	// Dialog Data
	static constexpr int IDD = IDD_GITPROGRESS;

public:
	std::vector<std::wstring>			m_GitCmd;		// one command, as argv
	PostCmdCallback			m_PostCmdCallback;
	ARGV_VECTOR				m_GitCmdList;	// several commands, run in order
	PostExecCallback		m_PostExecCallback; // After executing command line, this callback can modify exit code / display extra message
	std::vector<std::wstring>			m_GitDirList;
	CString					m_PreText;		// optional text to show in log window before running command
	CString					m_PreFailText;	// optional fail text to show in log window
	bool					m_bShowCommand;	// whether to display the command in the log window (default true)
	CString					m_LogFile;
	GitProgressAutoClose	m_AutoClose;
	CGit *					m_Git;

	DWORD					m_GitStatus;
	CString					m_LogText;

	CString					GetLogText() const { CString text; m_Log.GetWindowText(text); return text; }

protected:
	BOOL OnInitDialog() override;
	void DoDataExchange(CDataExchange* pDX) override; // DDX/DDV support
	void OnCancel() override;
	BOOL PreTranslateMessage(MSG* pMsg) override;
	LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
	PostCmdList				m_PostCmdList;
	void					WriteLog() const;
	CMenuButton				m_ctrlPostCmd;

	CProgressCtrl			m_Progress;

	CGestureEnabledControlTmpl<CRichEditCtrl> m_Log;
	CAnimateCtrl			m_Animate;
	CStatic					m_CurrentWork;
	CWinThread*				m_pThread;

	volatile bool			m_bAbort;
	bool					m_bDone;
	ULONGLONG				m_startTick;

	static UINT				ProgressThreadEntry(LPVOID pVoid);
	UINT					ProgressThread();

	LRESULT					OnProgressUpdateUI(WPARAM wParam,LPARAM lParam);
	afx_msg void			OnTimer(UINT_PTR id);

	afx_msg LRESULT			OnTaskbarBtnCreated(WPARAM wParam, LPARAM lParam);
	CComPtr<ITaskbarList3>	m_pTaskbarList;

	afx_msg void			OnClose();

	void					SetupLogMessageViewControl();
	afx_msg void			OnEnscrollLog();
	afx_msg void			OnEnLinkLog(NMHDR* pNMHDR, LRESULT* pResult);

	CGitCliOutputParser		m_cliOutputParser;
	static void				UpdateProgressFromLine(const CString& str, CProgressCtrl& progressctrl, HWND hWnd, CComPtr<ITaskbarList3> pTaskbarList, CWnd* currentWorkLabel);

	DECLARE_MESSAGE_MAP()

	//Share with Sync Dailog
	static int ParsePercentage(const CString& log, int pos);

	static void	ClearESC(CString &str);

public:
	static const int s_iSizeLimit;
	static void UpdateCmdOutput(CGitCliOutputParser& cliOutputParser, CRichEditCtrl& log, CProgressCtrl& progressctrl, HWND hWnd, CComPtr<ITaskbarList3> pTaskbarList, CWnd* currentWorkLabel = nullptr);

	/**
	 *@param dirlist if empty, the current directory of param git is used; otherwise each entry in param cmdlist uses the corresponding entry in param dirlist
	 */
	/**
	 * \a cmdlist is a list of *commands*, each an argv vector - not one command's
	 * argv. \a dirlist stays a plain std::vector<std::wstring> because it really is a list of
	 * directories, and it is indexed in lockstep with \a cmdlist.
	 */
	static UINT RunCmdList(CWnd* pWnd, ARGV_VECTOR& cmdlist, std::vector<std::wstring>& dirlist, bool bShowCommand, const CString* pfilename, CGitCliOutputParser& cliOutputParser, volatile bool* bAbort, CGit* git = &g_Git);

	static void KillProcessTree(DWORD dwProcessId, unsigned int depth = 0);

	static void InsertColorText(CRichEditCtrl& edit, const CString& text, COLORREF rgb);

private:
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedButton1();

	struct ACCELLERATOR {
		int id;
		int cnt;
		int wmid;
	};
	std::map<wchar_t, ACCELLERATOR>	m_accellerators;
	HACCEL							m_hAccel;
};


#define REFRESHTIMER 100

// CProgressDlg dialog



IMPLEMENT_DYNAMIC(CProgressDlg, CResizableStandAloneDialog)

const int CProgressDlg::s_iSizeLimit = std::max(16, std::min(static_cast<int>(CRegDWORD(L"Software\\TortoiseGit\\GitOutputLimitinKiB", 2 * 1024)), 100 * 1024)) * 1024;

CProgressDlg::CProgressDlg(CWnd* pParent /*=nullptr*/)
	: CResizableStandAloneDialog(CProgressDlg::IDD, pParent)
	, m_bShowCommand(true)
	, m_bAbort(false)
	, m_bDone(false)
	, m_startTick(GetTickCount64())
	, m_Git(&g_Git)
	, m_hAccel(nullptr)
	, m_pThread(nullptr)
	, m_GitStatus(DWORD(-1))
	, m_cliOutputParser(CProgressDlg::s_iSizeLimit)
{
	int autoClose = CRegDWORD(L"Software\\TortoiseGit\\AutoCloseGitProgress", 0);
	TCmdLineParser::CCmdLineParser parser(AfxGetApp()->m_lpCmdLine);
	if (parser.HasKey(L"closeonend"))
		autoClose = parser.GetLongVal(L"closeonend");
	switch (autoClose)
	{
	case 1:
		m_AutoClose = GitProgressAutoClose::AUTOCLOSE_IF_NO_OPTIONS;
		break;
	case 2:
		m_AutoClose = GitProgressAutoClose::AUTOCLOSE_IF_NO_ERRORS;
		break;
	default:
		m_AutoClose = GitProgressAutoClose::AUTOCLOSE_NO;
		break;
	}
}

CProgressDlg::~CProgressDlg()
{
	if (m_hAccel)
		DestroyAcceleratorTable(m_hAccel);
	delete m_pThread;
}

void CProgressDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CURRENT, this->m_CurrentWork);
	DDX_Control(pDX, IDC_TITLE_ANIMATE, this->m_Animate);
	DDX_Control(pDX, IDC_RUN_PROGRESS, this->m_Progress);
	DDX_Control(pDX, IDC_LOG, this->m_Log);
	DDX_Control(pDX, IDC_PROGRESS_BUTTON1, this->m_ctrlPostCmd);
}

BEGIN_MESSAGE_MAP(CProgressDlg, CResizableStandAloneDialog)
	ON_WM_CLOSE()
	ON_WM_TIMER()
	ON_MESSAGE(MSG_PROGRESSDLG_UPDATE_UI, OnProgressUpdateUI)
	ON_BN_CLICKED(IDOK, &CProgressDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_PROGRESS_BUTTON1, &CProgressDlg::OnBnClickedButton1)
	ON_REGISTERED_MESSAGE(TaskBarButtonCreated, OnTaskbarBtnCreated)
	ON_NOTIFY(EN_LINK, IDC_LOG, OnEnLinkLog)
	ON_EN_VSCROLL(IDC_LOG, OnEnscrollLog)
	ON_EN_HSCROLL(IDC_LOG, OnEnscrollLog)
END_MESSAGE_MAP()

BOOL CProgressDlg::OnInitDialog()
{
	CResizableStandAloneDialog::OnInitDialog();

	// Let the TaskbarButtonCreated message through the UIPI filter. If we don't
	// do this, Explorer would be unable to send that message to our window if we
	// were running elevated. It's OK to make the call all the time, since if we're
	// not elevated, this is a no-op.
	using ChangeWindowMessageFilterExDFN = BOOL(STDAPICALLTYPE)(HWND hWnd, UINT message, DWORD action, PCHANGEFILTERSTRUCT pChangeFilterStruct);

	CHANGEFILTERSTRUCT cfs = { .cbSize = sizeof(CHANGEFILTERSTRUCT), .ExtStatus = 0 };
	auto hUser = wil::unique_hmodule{AtlLoadSystemLibraryUsingFullPath(L"user32.dll")};
	if (hUser) {
		auto* pfnChangeWindowMessageFilterEx = GetProcAddressByFunctionDeclaration(hUser.get(), ChangeWindowMessageFilterEx);
		if (pfnChangeWindowMessageFilterEx)
			pfnChangeWindowMessageFilterEx(m_hWnd, TaskBarButtonCreated, MSGFLT_ALLOW, &cfs);
	}
	m_pTaskbarList.Release();
	if (FAILED(m_pTaskbarList.CoCreateInstance(CLSID_TaskbarList)))
		m_pTaskbarList = nullptr;

	AddAnchor(IDC_TITLE_ANIMATE, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_RUN_PROGRESS, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_LOG, TOP_LEFT, BOTTOM_RIGHT);

	AddAnchor(IDOK, BOTTOM_RIGHT);
	AddAnchor(IDCANCEL, BOTTOM_RIGHT);
	AddAnchor(IDC_PROGRESS_BUTTON1, BOTTOM_LEFT);
	AddAnchor(IDC_CURRENT, TOP_LEFT);

	this->GetDlgItem(IDC_PROGRESS_BUTTON1)->ShowWindow(SW_HIDE);
	DialogEnableWindow(IDC_PROGRESS_BUTTON1, FALSE);
	m_Animate.Open(IDR_DOWNLOAD);

	SetupLogMessageViewControl();

	CString InitialText;
	if (!m_PreText.IsEmpty())
		InitialText = m_PreText + L"\r\n";
#if 0
	if (m_bShowCommand && !m_GitCmd.empty())
		InitialText += CGit::SerializeArgvStdWString(m_GitCmd) + L"\r\n\r\n";
#endif
	m_Log.SetWindowTextW(InitialText);
	m_CurrentWork.SetWindowText(L"");

	if (!m_PreFailText.IsEmpty())
		InsertColorText(this->m_Log, m_PreFailText, CTheme::Instance().IsDarkTheme() ? RGB(207, 47, 47) : RGB(255, 0, 0));

	EnableSaveRestore(L"ProgressDlg");
	SetTheme(CTheme::Instance().IsDarkTheme());

	m_pThread = AfxBeginThread(ProgressThreadEntry, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pThread)
	{
		CMessageBox::Show(this->m_hWnd, IDS_ERR_THREADSTARTFAILED, IDS_APPNAME, MB_OK | MB_ICONERROR);
		DialogEnableWindow(IDCANCEL, TRUE);
	}
	else
	{
		m_pThread->m_bAutoDelete = FALSE;
		m_pThread->ResumeThread();
	}

	wil::MFC::SetWindowTitle(*this, m_Git->m_CurrentDir.c_str());

	// Make sure this dialog is shown in foreground (see issue #1536)
	SetForegroundWindow();

	return TRUE;
}

UINT CProgressDlg::ProgressThreadEntry(LPVOID pVoid)
{
	return static_cast<CProgressDlg*>(pVoid)->ProgressThread();
}

//static function, Share with SyncDialog
UINT CProgressDlg::RunCmdList(CWnd* pWnd, ARGV_VECTOR& cmdlist, std::vector<std::wstring>& dirlist, bool bShowCommand, const CString* pfilename, CGitCliOutputParser& cliparser, volatile bool* bAbort, CGit* git)
{
	UINT ret = 0;

	std::vector<std::unique_ptr<CBlockCacheForPath>> cacheBlockList;
	std::vector<std::unique_ptr<CGit>> gitList;
	if (dirlist.empty())
		cacheBlockList.push_back(std::make_unique<CBlockCacheForPath>(git->m_CurrentDir.c_str()));
	else
	{
		for (const auto& dir : dirlist)
		{
			auto pGit = std::make_unique<CGit>();
			pGit->SetCurrentDirExact(dir.c_str());
			gitList.push_back(std::move(pGit));
			cacheBlockList.push_back(std::make_unique<CBlockCacheForPath>(dir.c_str()));
		}
	}

	EnsurePostMessage(pWnd, MSG_PROGRESSDLG_UPDATE_UI, MSG_PROGRESSDLG_START, 0);

	cliparser.Reset();

	for (size_t i = 0; i < cmdlist.size() && !*bAbort; ++i)
	{
		if (cmdlist[i].empty())
			continue;

		// The flat line is only wanted for the log and the traces now, so it is
		// built once here rather than being the thing that gets run. The Trim
		// this replaces existed because the command was assembled by pasting
		// optional pieces together and could pick up stray spaces; argv elements
		// have no such seams.
		const CString command = cmdlist[i].to_cmd().c_str();

		if (bShowCommand)
		{
			const std::string str = CUnicodeUtils::StdGetUTF8(
				gitList.empty() || (gitList.size() == 1 && gitList[0]->m_CurrentDir == git->m_CurrentDir)
					? std::format(L"{}{}\n", i > 0 ? L"\n" : L"", command)
					: std::format(L"{}{}\n{}\n", i > 0 ? L"\n" : L"", gitList[i]->m_CurrentDir, command));
			cliparser.AppendChunk(str);
			pWnd->PostMessage(MSG_PROGRESSDLG_UPDATE_UI, MSG_PROGRESSDLG_RUN, 0);
		}

		PROCESS_INFORMATION pi;
		wil::unique_handle hRead;
		if (const int runAsyncRet = GitCall::RunAsync(cmdlist[i], pi, hRead.put(), nullptr, pfilename); runAsyncRet) {
			EnsurePostMessage(pWnd, MSG_PROGRESSDLG_UPDATE_UI, MSG_PROGRESSDLG_FAILED, -1 * runAsyncRet);
			return runAsyncRet;
		}
		// just for the RAII
		[[maybe_unused]] auto piProcess = wil::unique_handle(std::move(pi.hProcess));
		[[maybe_unused]] auto piThread = wil::unique_handle(std::move(pi.hThread));

		DWORD readnumber;
		char buffer[1024];
		while (ReadFile(hRead.get(), buffer, sizeof(buffer), &readnumber, nullptr)) {
			cliparser.AppendChunk({ buffer, readnumber });
		}
		TracePrintf(_T(__FUNCTION__) L": waiting for process to finish (%s), aborted: %d\n", command, *bAbort);

		WaitForSingleObject(pi.hProcess, INFINITE);

		DWORD status = 0;
		if (!GetExitCodeProcess(pi.hProcess, &status) || *bAbort)
		{
			TracePrintf(_T(__FUNCTION__) L": process %s finished, status code could not be fetched, (error %d; %s), aborted: %d\n", command, GetLastError(), (wil::CFormatMessageWrapper()).c_str(), *bAbort);

			EnsurePostMessage(pWnd, MSG_PROGRESSDLG_UPDATE_UI, MSG_PROGRESSDLG_FAILED, status);
			return GitCall::TGIT_GIT_ERROR_GET_EXIT_CODE;
		}
		TracePrintf(_T(__FUNCTION__) L": process %s finished with code %d\n", command, status);
		ret |= status;
	}

	EnsurePostMessage(pWnd, MSG_PROGRESSDLG_UPDATE_UI, MSG_PROGRESSDLG_END, ret);

	return ret;
}

UINT CProgressDlg::ProgressThread()
{
	if (!m_GitCmd.empty())
		m_GitCmdList.push_back(m_GitCmd);

	CString* pfilename;

	if (m_LogFile.IsEmpty())
		pfilename = nullptr;
	else
		pfilename = &m_LogFile;

	m_startTick = GetTickCount64();
	m_GitStatus = RunCmdList(this, m_GitCmdList, m_GitDirList, m_bShowCommand, pfilename, m_cliOutputParser, &m_bAbort, m_Git);
	return 0;
}

LRESULT CProgressDlg::OnProgressUpdateUI(WPARAM wParam, LPARAM lParam)
{
	if (wParam == MSG_PROGRESSDLG_START)
	{
		if (CRegDWORD(L"Software\\TortoiseGit\\DownloadAnimation", TRUE) == TRUE)
			m_Animate.Play(0, INT_MAX, INT_MAX);
		DialogEnableWindow(IDCANCEL, TRUE);
		if (m_pTaskbarList)
		{
			m_pTaskbarList->SetProgressState(m_hWnd, TBPF_NORMAL);
			m_pTaskbarList->SetProgressValue(m_hWnd, 0, 100);
		}
		SetTimer(REFRESHTIMER, 125, nullptr);
		return 0;
	}
	if (wParam == MSG_PROGRESSDLG_END || wParam == MSG_PROGRESSDLG_FAILED)
	{
		KillTimer(REFRESHTIMER);
		UpdateCmdOutput(m_cliOutputParser, m_Log, m_Progress, GetSafeHwnd(), m_pTaskbarList, &m_CurrentWork);
		TracePrintf(_T(__FUNCTION__) L": got message: %d\n", wParam);
		ULONGLONG tickSpent = GetTickCount64() - m_startTick;
		CString strEndTime = CLoglistUtils::FormatDateAndTime(CTime::GetCurrentTime(), DATE_SHORTDATE, true, false);

		m_cliOutputParser.Reset();

		m_bDone = true;
		m_Progress.SetPos(100);
		this->DialogEnableWindow(IDOK, TRUE);

		m_GitStatus = static_cast<DWORD>(lParam);
		if (m_GitCmd.empty() && m_GitCmdList.empty())
			m_GitStatus = DWORD(-1);

		if (m_PostExecCallback)
		{
			CString extraMsg;
			m_PostExecCallback(GetSafeHwnd(), m_GitStatus, extraMsg);
			if (!extraMsg.IsEmpty())
			{
				const int start = m_Log.GetTextLength();
				m_Log.SetSel(start, start);
				m_Log.ReplaceSel(extraMsg);
			}
		}
		{
			CString text;
			m_Log.GetWindowText(text);
			text.Remove('\r');
			if (static_cast<DWORD>(CRegStdDWORD(L"Software\\TortoiseGit\\StyleGitOutput", TRUE)) == TRUE)
				wil::MFC::StyleWarningsErrors(text, &m_Log);
			wil::MFC::StyleURLs(text, &m_Log);
		}

		if (m_Animate.IsPlaying() && !m_GitStatus)
			m_Animate.Play(28, 29, 1); // IDR_DOWNLOAD Frame No. Range 0 ~ 29 (Memo: -1 does not work for last frame, but 32767(WORD_MAX/2) does.)
		else
			m_Animate.Stop();

		if (this->m_GitStatus)
		{
			m_Progress.SetState(PBST_ERROR);
			if (m_pTaskbarList)
			{
				std::ignore = m_pTaskbarList->SetProgressState(m_hWnd, TBPF_ERROR);
				std::ignore = m_pTaskbarList->SetProgressValue(m_hWnd, 100, 100);
			}
			CString log;
			log.Format(IDS_PROC_PROGRESS_GITUNCLEANEXIT, m_GitStatus);
			m_CurrentWork.SetWindowText(log);
			CString err;
			if (CRegDWORD(L"Software\\TortoiseGit\\ShowGitexeTimings", TRUE))
				err.Format(L"\r\n\r\n%s (%I64u ms @ %s)\r\n", log, tickSpent, strEndTime);
			else
				err.Format(L"\r\n\r\n%s\r\n", log);
			if (!m_GitCmd.empty() || !m_GitCmdList.empty())
				InsertColorText(this->m_Log, err, CTheme::Instance().IsDarkTheme() ? RGB(207, 47, 47) : RGB(255, 0, 0));
			if (CRegDWORD(L"Software\\TortoiseGit\\NoSounds", FALSE) == FALSE)
				PlaySound(reinterpret_cast<LPCWSTR>(SND_ALIAS_SYSTEMEXCLAMATION), nullptr, SND_ALIAS_ID | SND_ASYNC);
		}
		else {
			if (m_pTaskbarList)
				std::ignore = m_pTaskbarList->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
			CString temp;
			temp.LoadString(IDS_SUCCESS);
			m_CurrentWork.SetWindowText(temp);
			CString log;
			if (CRegDWORD(L"Software\\TortoiseGit\\ShowGitexeTimings", TRUE))
				log.Format(L"\r\n%s (%I64u ms @ %s)\r\n", temp, tickSpent, strEndTime);
			else
				log.Format(L"\r\n%s\r\n", temp);
			if (CTheme::Instance().IsHighContrastMode())
				InsertColorText(this->m_Log, log, ::GetSysColor(COLOR_WINDOWTEXT));
			else
				InsertColorText(this->m_Log, log, CTheme::Instance().IsDarkTheme() ? RGB(0, 178, 255) : RGB(0, 0, 255));
			this->DialogEnableWindow(IDCANCEL, FALSE);
		}

		m_Log.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);

		if (wParam == MSG_PROGRESSDLG_END)
		{
			if (m_PostCmdCallback) // new handling method using callback
			{
				m_PostCmdCallback(m_GitStatus, m_PostCmdList);

				if (!m_PostCmdList.empty())
				{
					int i = 0;
					for (const auto& entry : m_PostCmdList)
					{
						++i;
						m_ctrlPostCmd.AddEntry(entry.label, entry.icon);
						wchar_t accellerator = StringUtils::GetAccellerator(entry.label);
						if (accellerator == L'\0')
							continue;
						++m_accellerators[accellerator].cnt;
						if (m_accellerators[accellerator].cnt > 1)
							m_accellerators[accellerator].id = -1;
						else
							m_accellerators[accellerator].id = i - 1;
					}

					if (m_accellerators.size())
					{
						auto lpaccelNew = static_cast<LPACCEL>(LocalAlloc(LPTR, m_accellerators.size() * sizeof(ACCEL)));
						SCOPE_EXIT { LocalFree(lpaccelNew); };
						i = 0;
						for (auto& entry : m_accellerators)
						{
							lpaccelNew[i].cmd = static_cast<WORD>(WM_USER + 1 + entry.second.id);
							lpaccelNew[i].fVirt = FVIRTKEY | FALT;
							lpaccelNew[i].key = entry.first;
							entry.second.wmid = lpaccelNew[i].cmd;
							++i;
						}
						m_hAccel = CreateAcceleratorTable(lpaccelNew, static_cast<int>(m_accellerators.size()));
					}
					DialogEnableWindow(IDC_PROGRESS_BUTTON1, TRUE);
					GetDlgItem(IDC_PROGRESS_BUTTON1)->ShowWindow(SW_SHOW);
				}
			}
		}

		if (wParam == MSG_PROGRESSDLG_END && m_GitStatus == 0)
		{
			if (m_AutoClose == GitProgressAutoClose::AUTOCLOSE_IF_NO_OPTIONS && m_PostCmdList.empty() || m_AutoClose == GitProgressAutoClose::AUTOCLOSE_IF_NO_ERRORS)
				PostMessage(WM_COMMAND, 1, reinterpret_cast<LPARAM>(GetDlgItem(IDOK)->m_hWnd));
		}
		return 0;
	}

	return 0;
}

void CProgressDlg::OnTimer(UINT_PTR id)
{
	if (id == REFRESHTIMER)
		UpdateCmdOutput(m_cliOutputParser, m_Log, m_Progress, GetSafeHwnd(), m_pTaskbarList, &m_CurrentWork);

	__super::OnTimer(id);
}

//static function, Share with SyncDialog
int CProgressDlg::ParsePercentage(const CString& log, int s1)
{
	int s2 = s1 - 1;
	for (int i = s1 - 1; i >= 0; i--)
	{
		if (log[i] >= L'0' && log[i] <= L'9')
			s2 = i;
		else
			break;
	}
	return _wtol(log.Mid(s2, s1 - s2));
}

void CProgressDlg::ClearESC(CString& str)
{
	// see http://ascii-table.com/ansi-escape-sequences.php and http://tldp.org/HOWTO/Bash-Prompt-HOWTO/c327.html
	str.Replace(L"\033[K", L""); // erase until end of line; no need to care for this, because we always clear the whole line

	// drop colors
	while (true)
	{
		const int escapePosition = str.Find(L'\033');
		if (escapePosition >= 0 && str.GetLength() >= escapePosition + 3)
		{
			if (str.Mid(escapePosition, 2) == L"\033[")
			{
				const int colorEnd = str.Find(L'm', escapePosition + 2);
				if (colorEnd > 0)
				{
					bool found = true;
					for (int i = escapePosition + 2; i < colorEnd; ++i)
					{
						if (str[i] != L';' && (str[i] < L'0' && str[i] > L'9'))
						{
							found = false;
							break;
						}
					}
					if (found)
					{
						if (escapePosition > 0)
							str = str.Left(escapePosition) + str.Mid(colorEnd + 1);
						else
							str = str.Mid(colorEnd);
						continue;
					}
				}
			}
		}
		break;
	}
}

void CProgressDlg::UpdateCmdOutput(CGitCliOutputParser& cliOutputParser, CRichEditCtrl& log, CProgressCtrl& progressctrl, HWND hWnd, CComPtr<ITaskbarList3> pTaskbarList, CWnd* currentWorkLabel)
{
	EmittedLines out = cliOutputParser.ProcessPending();
	if (out.text.empty() && !out.erasePreviousLineWithLength)
		return;

	const int currentLength = log.GetWindowTextLength();
	if (currentLength > 0 && out.erasePreviousLineWithLength)
	{
		const int lineCount = log.GetLineCount();
		if (lineCount > 0)
		{
			const int start = log.LineIndex(lineCount - 1);
			log.SetSel(start, currentLength);
		}
		else
			log.SetSel(0, 0);
	}
	else
		log.SetSel(currentLength, currentLength);

	CString str = CUnicodeUtils::GetUnicode<CString>(out.text);
	if (currentLength == 0 && GitCall::m_LogEncode != CP_UTF8 &&!out.text.empty() && out.text[0] == '[')
	{
		size_t start = 0;
		// git.exe commit output begins with "[BRANCH] SUBJECT" whereas SUBJECT is encoded using m_LogEncode
		for (size_t i = 0; i < out.text.length(); ++i)
		{
			if (out.text[i] == ']')
				start = i;
			else if (start > 0 && out.text[i] == '\n')
			{
				start = i;
				break;
			}
		}

		std::string_view buff = out.text;
		// an explicit code page, so StdGetUnicode: GetUnicode is the UTF-8-only path
		str = CUnicodeUtils::StdGetUnicode<CString>(buff.substr(0, start), GitCall::m_LogEncode);
		buff.remove_prefix(start);
		str += CUnicodeUtils::GetUnicode<CStringW>(buff);
	}
	ClearESC(str);
	log.ReplaceSel(str);
	log.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	UpdateProgressFromLine(str, progressctrl, hWnd, pTaskbarList, currentWorkLabel);

	if (out.limited || currentLength + str.GetLength() >= CProgressDlg::s_iSizeLimit)
	{
		cliOutputParser.ActivateDropMode();

		const int newLength = log.GetWindowTextLength();
		str.Format(L"[Output truncated at about %d KiB]", newLength / 1024);
		log.SetSel(newLength, newLength);
		log.ReplaceSel(L"\n\n...\n" + str);

		if (currentWorkLabel)
			currentWorkLabel->SetWindowTextW(str);

		log.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);

		if (pTaskbarList)
			pTaskbarList->SetProgressState(hWnd, TBPF_INDETERMINATE);
	}
}

void CProgressDlg::UpdateProgressFromLine(const CString& str, CProgressCtrl& progressctrl, HWND hWnd, CComPtr<ITaskbarList3> pTaskbarList, CWnd* currentWorkLabel)
{
	const int s1 = str.ReverseFind(L':');
	const int s2 = str.Find(L'%');
	if (s1 <= 0 || s2 <= 0)
		return;

	if (currentWorkLabel)
		currentWorkLabel->SetWindowTextW(str.Left(s1));

	const int pos = ParsePercentage(str, s2);
	if (pos > 0)
	{
		progressctrl.SetPos(pos);
		if (pTaskbarList)
		{
			pTaskbarList->SetProgressState(hWnd, TBPF_NORMAL);
			pTaskbarList->SetProgressValue(hWnd, pos, 100);
		}
	}
}
// CProgressDlg message handlers

void CProgressDlg::WriteLog() const
{
	CLogFile logfile(GitCall::m_CurrentDirStorage.c_str());
	if (logfile.Open())
	{
		logfile.AddTimeLine();
		CString text = GetLogText();
		LPCWSTR psz_string = text;
		while (*psz_string)
		{
			const size_t i_len = wcscspn(psz_string, L"\r\n");
			logfile.AddLine(CString(psz_string, static_cast<int>(i_len)));
			psz_string += i_len;
			if (*psz_string == '\r')
			{
				++psz_string;
				if (*psz_string == '\n')
					++psz_string;
			}
			else if (*psz_string == '\n')
				++psz_string;
		}
		if (m_bAbort)
		{
			CString canceled;
			canceled.LoadString(IDS_USERCANCELLED);
			logfile.AddLine(canceled);
		}
		logfile.Close();
	}
}

void CProgressDlg::OnBnClickedOk()
{
	if (m_pThread) // added here because Close-button is "called" from thread by PostMessage
		::WaitForSingleObject(m_pThread->m_hThread, 5000);
	m_Log.GetWindowText(this->m_LogText);
	WriteLog();
	OnOK();
}

void CProgressDlg::OnBnClickedButton1()
{
	WriteLog();
	ShowWindow(SW_HIDE);
	m_PostCmdList.at(m_ctrlPostCmd.GetCurrentEntry()).action();
	EndDialog(IDOK);
}

void CProgressDlg::OnClose()
{
	DialogEnableWindow(IDCANCEL, TRUE);
	__super::OnClose();
}

void CProgressDlg::OnCancel()
{
	TracePrintf(_T(__FUNCTION__) L": User canceled\n");
	m_bAbort = true;
	if (m_bDone)
	{
		WriteLog();
		CResizableStandAloneDialog::OnCancel();
		return;
	}

	if (GitCall::m_CurrentGitPi.hProcess)
	{
		DWORD dwConfirmKillProcess = CRegDWORD(L"Software\\TortoiseGit\\ConfirmKillProcess");
		if (dwConfirmKillProcess && CMessageBox::Show(m_hWnd, IDS_PROC_CONFIRMKILLPROCESS, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
		if (::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0))
		{
			::WaitForSingleObject(GitCall::m_CurrentGitPi.hProcess, 10000);
		}

		KillProcessTree(GitCall::m_CurrentGitPi.dwProcessId);
	}

	::WaitForSingleObject(GitCall::m_CurrentGitPi.hProcess, 10000);
	if (m_pThread)
	{
		if (::WaitForSingleObject(m_pThread->m_hThread, 5000) == WAIT_TIMEOUT)
			GitCall::KillRelatedThreads(m_pThread->m_nThreadID, m_pThread->m_hThread);
	}

	WriteLog();
	CResizableStandAloneDialog::OnCancel();
}

void CProgressDlg::KillProcessTree(DWORD dwProcessId, unsigned int depth)
{
	// recursively kills a process tree
	// This is not optimized, but works and isn't called very often ;)

	if (!dwProcessId || depth > 20)
		return;

	PROCESSENTRY32 pe = { .dwSize = sizeof(PROCESSENTRY32) };

	const auto hSnap = wil::unique_handle{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};

	if (::Process32First(hSnap.get(), &pe)) {
		do {
			if (pe.th32ParentProcessID == dwProcessId)
				KillProcessTree(pe.th32ProcessID, depth + 1);
		} while (::Process32Next(hSnap.get(), &pe));

		if (const auto hProc = wil::unique_handle{::OpenProcess(PROCESS_TERMINATE, FALSE, dwProcessId)}; hProc)
			::TerminateProcess(hProc.get(), 1);
	}
}

void CProgressDlg::InsertColorText(CRichEditCtrl& edit, const CString& text, COLORREF rgb)
{
	CHARFORMAT old, cf;
	edit.GetDefaultCharFormat(cf);
	old = cf;
	cf.dwMask |= CFM_COLOR;
	cf.crTextColor = rgb;
	cf.dwEffects |= CFE_BOLD;
	cf.dwEffects &= ~CFE_AUTOCOLOR;
	edit.SetSel(edit.GetTextLength() - 1, edit.GetTextLength());
	edit.ReplaceSel(text);
	edit.SetSel(edit.LineIndex(edit.GetLineCount() - 2), edit.GetTextLength());
	edit.SetSelectionCharFormat(cf);
	edit.SetSel(edit.GetTextLength(), edit.GetTextLength());
	edit.SetDefaultCharFormat(old);
}

LRESULT CProgressDlg::OnTaskbarBtnCreated(WPARAM wParam, LPARAM lParam)
{
	m_pTaskbarList.Release();
	m_pTaskbarList.CoCreateInstance(CLSID_TaskbarList);
	return __super::OnTaskbarButtonCreated(wParam, lParam);
}

BOOL CProgressDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_hAccel && TranslateAccelerator(m_hWnd, m_hAccel, pMsg))
		return TRUE;
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_ESCAPE)
		{
			// pressing the ESC key should close the dialog. But since we disabled the escape
			// key (so the user doesn't get the idea that he could simply undo an e.g. update)
			// this won't work.
			// So if the user presses the ESC key, change it to VK_RETURN so the dialog gets
			// the impression that the OK button was pressed.
			if ((!GetDlgItem(IDCANCEL)->IsWindowEnabled())
				&&(GetDlgItem(IDOK)->IsWindowEnabled())&&(GetDlgItem(IDOK)->IsWindowVisible()))
			{
				// since we convert ESC to RETURN, make sure the OK button has the focus.
				GetDlgItem(IDOK)->SetFocus();
				// make sure the RETURN is not handled by the RichEdit
				pMsg->hwnd = GetSafeHwnd();
				pMsg->wParam = VK_RETURN;
			}
		}
	}
	else if (pMsg->message == WM_CONTEXTMENU || pMsg->message == WM_RBUTTONDOWN)
	{
		auto pWnd = GetDlgItem(IDC_LOG);
		if (pWnd == GetFocus())
		{
			CIconMenu popup;
			if (popup.CreatePopupMenu())
			{
				long start = -1, end = -1;
				auto pEdit = static_cast<CRichEditCtrl*>(GetDlgItem(IDC_LOG));
				pEdit->GetSel(start, end);
				popup.AppendMenuIcon(WM_COPY, IDS_SCIEDIT_COPY, IDI_COPYCLIP);
				if (start >= end)
					popup.EnableMenuItem(WM_COPY, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
				popup.AppendMenu(MF_SEPARATOR);
				popup.AppendMenuIcon(EM_SETSEL, IDS_STATUSLIST_CONTEXT_COPYEXT, IDI_COPYCLIP);
				const int cmd = popup.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_NONOTIFY, pMsg->pt.x, pMsg->pt.y, this);
				switch (cmd)
				{
				case 0: // no command selected
					break;
				case EM_SETSEL:
				{
					pEdit->SetRedraw(FALSE);
					const int oldLine = pEdit->GetFirstVisibleLine();
					pEdit->SetSel(0, -1);
					pEdit->Copy();
					pEdit->SetSel(start, end);
					const int newLine = pEdit->GetFirstVisibleLine();
					pEdit->LineScroll(oldLine - newLine);
					pEdit->SetRedraw(TRUE);
					pEdit->RedrawWindow();
				}
				break;
				case WM_COPY:
					::SendMessage(GetDlgItem(IDC_LOG)->GetSafeHwnd(), cmd, 0, -1);
					break;
				}
				return TRUE;
			}
		}
	}
	return __super::PreTranslateMessage(pMsg);
}

LRESULT CProgressDlg::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (m_hAccel && message == WM_COMMAND && LOWORD(wParam) >= WM_USER && LOWORD(wParam) <= WM_USER + m_accellerators.size())
	{
		for (const auto& entry : m_accellerators)
		{
			if (entry.second.wmid != LOWORD(wParam))
				continue;
			if (entry.second.id == -1)
				m_ctrlPostCmd.PostMessage(WM_KEYDOWN, VK_F4, NULL);
			else
			{
				m_ctrlPostCmd.SetCurrentEntry(entry.second.id);
				OnBnClickedButton1();
			}
			return 0;
		}
	}

	return __super::DefWindowProc(message, wParam, lParam);
}

void CProgressDlg::OnEnLinkLog(NMHDR* pNMHDR, LRESULT* pResult)
{
	// similar code in SyncDlg.cpp and LogDlg.cpp
	ENLINK* pEnLink = reinterpret_cast<ENLINK*>(pNMHDR);
	if ((pEnLink->msg == WM_LBUTTONUP) || (pEnLink->msg == WM_SETCURSOR))
	{
		CString msg;
		GetDlgItemText(IDC_LOG, msg);
		msg.Replace(L"\r\n", L"\n");
		CString url = msg.Mid(pEnLink->chrg.cpMin, pEnLink->chrg.cpMax - pEnLink->chrg.cpMin);
		// check if it's an email address
		const auto atpos = url.Find(L'@');
		if ((atpos > 0) && (url.ReverseFind(L'.') > atpos) && !::PathIsURL(url))
			url = L"mailto:" + url;
		if (::PathIsURL(url))
		{
			if (pEnLink->msg == WM_LBUTTONUP)
				ShellExecute(GetSafeHwnd(), L"open", url, nullptr, nullptr, SW_SHOWDEFAULT);
			else
			{
				static RECT prevRect = { 0 };
				CWnd* pMsgView = GetDlgItem(IDC_LOG);
				if (pMsgView)
				{
					RECT rc;
					POINTL pt;
					pMsgView->SendMessage(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt), pEnLink->chrg.cpMin);
					rc.left = pt.x;
					rc.top = pt.y;
					pMsgView->SendMessage(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt), pEnLink->chrg.cpMax);
					rc.right = pt.x;
					rc.bottom = pt.y + 12;
					if ((prevRect.left != rc.left) || (prevRect.top != rc.top))
					{
						m_tooltips.DelTool(pMsgView, 1);
						m_tooltips.AddTool(pMsgView, url, &rc, 1);
						prevRect = rc;
					}
				}
			}
		}
	}
	*pResult = 0;
}

void CProgressDlg::OnEnscrollLog()
{
	m_tooltips.DelTool(GetDlgItem(IDC_LOG), 1);
}

void CProgressDlg::SetupLogMessageViewControl()
{
	CFont logFont;
	wil::MFC::CreateFontForLogs(GetSafeHwnd(), logFont);
	m_Log.SetFont(&logFont);
	// make the log message rich edit control send a message when the mouse pointer is over a link
	std::ignore = m_Log.SendMessage(EM_SETEVENTMASK, NULL, ENM_LINK | ENM_SCROLL);

	CHARFORMAT2 format = { 0 };
	format.cbSize = sizeof(CHARFORMAT2);
	format.dwMask = CFM_COLOR | CFM_BACKCOLOR;
	if (CTheme::Instance().IsDarkTheme())
	{
		format.crTextColor = CTheme::darkTextColor;
		format.crBackColor = CTheme::darkBkColor;
	}
	else
	{
		format.crTextColor = ::GetSysColor(COLOR_WINDOWTEXT);
		format.crBackColor = ::GetSysColor(COLOR_WINDOW);
	}
	std::ignore = m_Log.SendMessage(EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
	std::ignore = m_Log.SendMessage(EM_SETBKGNDCOLOR, 0, format.crBackColor);
}

} // namespace ProgressDlg
export using namespace ProgressDlg;
// NOLINTEND(*-pro-type-reinterpret-cast)
