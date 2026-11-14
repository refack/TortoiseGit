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
//

// The TortoiseGitProc.exe application object. It used to live in module
// TortoiseProc (SharedMFC), which every MFC executable links; that put a second
// CWinApp into TortoiseGitBlame and dragged Command/CommandServer, which exist
// only in this exe, into its link. The module keeps the launcher helpers; the
// app, which is the only thing that needs the command factory, lives here.
//
// No header: nothing outside this file needs the CTortoiseProcApp type. Code
// that wants the running app reaches it as a CWinApp through AfxGetApp(), and
// the Explorer window it was started for through TortoiseProc::GetExplorerHWND().

#include "stdafx.h"

// Textual includes first: several of these pull imports of their own.
#include <version.h>
#include "MiscUI/AnimationManager.h"
#include "JumpListHelpers.h"
#include "ProtoModules/VersioncheckParser.h"
#include "Utils/SysImageList.h"
#include "Utils/HistoryCombo.h"
#include "Commands/Command.h"

#include "std.h"

import tgit;
import TortoiseProc;		// launcher helpers, CheckUpgrade, Git2* callbacks, g_sGrouping*
import ConfigureGitExe;
import Registry;
import TGitPath;
import GitHash;
import TempFiles;
import ProjectProperties;
import TGitMessageBox;
import TaskbarUUID;
import Libraries;			// EnsureGitLibrary
import LangDll;
import tgit.Hooks;
import CmdLineParser;


/**
 * \ingroup TortoiseProc
 * Main class of the TortoiseGitProc.exe\n
 * It is the entry point when calling the TortoiseGitProc.exe and
 * handles the command line. Depending on the command line
 * other 'modules' are called, usually dialog boxes which
 * themselves then execute a specific function.\n\n
 * Many commands are executed using the CGitProgressList which
 * just displays the common notify callbacks of the libgit2 commands.
 */
class CTortoiseProcApp : public CWinAppEx {
public:
	CTortoiseProcApp();
	~CTortoiseProcApp() override = default;

	// Overrides
	BOOL InitInstance() override;
	int ExitInstance() override;

	void InitializeJumpList(const CString& appid);
	static void DoInitializeJumpList(const CString& appid);

	ULONG_PTR m_gdiplusToken = 0;

	// Implementation
private:
	DECLARE_MESSAGE_MAP()
	bool retSuccess = false;
	LangDll::CLangDll m_langDll;
};


// The one and only CTortoiseProcApp object
CTortoiseProcApp theApp;


BEGIN_MESSAGE_MAP(CTortoiseProcApp, CWinAppEx)
	ON_COMMAND(ID_HELP, CWinAppEx::OnHelp)
END_MESSAGE_MAP()


CTortoiseProcApp::CTortoiseProcApp() {
	DebugOutput::TracePrintf(_T(__FUNCTION__) L": Constructor\n");
	SetDllDirectory(L"");
	DebugOutput::TracePrintf(_T(__FUNCTION__) L": CommandLine: %s\n", GetCommandLine());
	EnableHtmlHelp();
	// CHooks::Create() is in InitInstance, not here: it fills tgit.Hooks' TheHookMap,
	// a dynamically initialized global in another object, and theApp's own dynamic
	// init may run first now that it lives in the exe rather than in SharedMFC.lib.
	CGit::SetGit2CredentialCallback(TortoiseProc::Git2GetUserPassword);
	CGit::SetGit2CertificateCheckCertificate(TortoiseProc::Git2CertificateCheck);
	m_bLoadUserToolbars = FALSE;
	m_bSaveState = FALSE;
}


// CTortoiseProcApp initialization

BOOL CTortoiseProcApp::InitInstance() {
	DebugOutput::TracePrintf(_T(__FUNCTION__) L": InitInstance\n");
	CHooks::Create();	// see the constructor: all statics are initialized by now
	TortoiseProc::CheckUpgrade();
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));
	CMFCButton::EnableWindowsTheming();

	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);

	if (HINSTANCE hInst = m_langDll.Init(L"TortoiseProc"); hInst)
		AfxSetResourceHandle(hInst);
	til::reset_proc_locale();

	DebugOutput::TracePrintf(_T(__FUNCTION__) L": Initializing UI components ...\n");
	// InitCommonControls() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.

	INITCOMMONCONTROLSEX used = {
		.dwSize = sizeof(INITCOMMONCONTROLSEX),
		.dwICC =
		ICC_ANIMATE_CLASS | ICC_BAR_CLASSES | ICC_COOL_CLASSES | ICC_DATE_CLASSES |
		ICC_HOTKEY_CLASS | ICC_INTERNET_CLASSES | ICC_LISTVIEW_CLASSES |
		ICC_NATIVEFNTCTL_CLASS | ICC_PAGESCROLLER_CLASS | ICC_PROGRESS_CLASS |
		ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES | ICC_UPDOWN_CLASS |
		ICC_USEREX_CLASSES | ICC_WIN95_CLASSES,
	};
	InitCommonControlsEx(&used);
	AfxOleInit();
	AfxEnableControlContainer();
	AfxInitRichEdit5();
	CWinAppEx::InitInstance();
	SetRegistryKey(L"TortoiseGit");
	SYS_IMAGE_LIST();
	CHistoryCombo::m_nGitIconIndex = SYS_IMAGE_LIST().AddIcon(wil::MFC::LoadIconEx(IDI_GITCONFIG, 0, 0));
	AfxGetApp()->m_pszProfileName = _wcsdup(L"TortoiseProc");
	// w/o this ResizableLib will store data under TortoiseGitProc which is not compatible with older versions

	CCmdLineParser parser(AfxGetApp()->m_lpCmdLine);

	HWND hWndExplorer = nullptr;
	auto sVal = parser.GetValStr(L"hwnd");
	if (!sVal.empty())
		hWndExplorer = reinterpret_cast<HWND>(_wcstoui64(sVal.data(), nullptr, 16));

	while (hWndExplorer && GetParent(hWndExplorer))
		hWndExplorer = GetParent(hWndExplorer);
	if (hWndExplorer && !IsWindow(hWndExplorer))
		hWndExplorer = nullptr;
	// Published for the dialogs in SharedMFC, which center on it / parent to it.
	TortoiseProc::SetExplorerHWND(hWndExplorer);

	// if HKCU\Software\TortoiseGit\Debug is not 0, show our command line
	// in a message box
	if (CRegDWORD(L"Software\\TortoiseGit\\Debug", FALSE) == TRUE)
		AfxMessageBox(AfxGetApp()->m_lpCmdLine, MB_OK | MB_ICONINFORMATION);

#if PREVIEW
	TracePrintf(_T(__FUNCTION__) L": Preview: %s\n", _T(PREVIEW_INFO));
#else
	if (CString hotfix = (til::GetAppDirectory() / L"hotfix.ini").c_str(); PathFileExists(hotfix)) {
		CString err;
		CVersioncheckParser versionparser;
		if (versionparser.Load(hotfix, err)) {
			auto version = versionparser.GetTortoiseGitVersion();
			if (version.major == TGIT_VERMAJOR && version.minor == TGIT_VERMINOR && (version.micro > TGIT_VERMICRO || version.micro == TGIT_VERMICRO && version.
				build > TGIT_VERBUILD)) {
				DebugOutput::TracePrintf(_T(__FUNCTION__) L": Hotfix: %s\n", versionparser.GetTortoiseGitVersion().version);
			}
		}
	}
#endif

	TortoiseProc::CheckForNewerVersion();

	if (parser.HasKey(L"urlhandler")) {
		auto url = parser.GetUrl(L"urlhandler");
		if (!url)
			return CMessageBox::Show(TortoiseProc::GetExplorerHWND(), IDS_ERR_INVALIDPATH, IDS_APPNAME, MB_ICONERROR);

		auto newCmd = std::format(L"/command:clone /url:{} /hasurlhandler", str::EscapeCmdValue(*url));
		parser = CCmdLineParser(newCmd);
	}

	auto cmd = CommandServer::GetCommand(CString(parser.GetValStr(L"command").c_str()), hWndExplorer, parser);
	if (!cmd) {
		cmd->Execute();
		return FALSE;
	}
	SCOPE_EXIT { Animator::Instance().ShutDown(); };
	if (!cmd->RequiresGitExe() && !cmd->MayTakePath()) {
		retSuccess = cmd->Execute();
		return FALSE;
	}

	if (!g_Git.CheckMsysGitDir()) {
		UINT ret = CMessageBox::Show(hWndExplorer,
		                             IDS_PROC_NOMSYSGIT,
		                             IDS_APPNAME,
		                             3,
		                             IDI_HAND,
		                             IDS_PROC_SETMSYSGITPATH,
		                             IDS_PROC_GOTOMSYSGITWEBSITE,
		                             IDS_ABORTBUTTON);
		if (ret == 2)
			ShellExecute(hWndExplorer, L"open", ConfigureGitExe::GIT_FOR_WINDOWS_URL.data(), nullptr, nullptr, SW_SHOW);
		else if (ret == 1) {
			parser = CCmdLineParser(L"/command:firststart /page:2");
			cmd = CommandServer::GetCommand(CString(parser.GetValStr(L"command").c_str()), hWndExplorer, parser);
			cmd->Execute();
		}
		return FALSE;
	}
	TGitH::ms_LastMsysGitVersion = 0;
	// HACK to re-check Git version in next method call to CheckGitVersion, only needed becasue APPDATA is a Git config dir since 2.46
	if (!ConfigureGitExe::CConfigureGitExe::CheckGitVersion(hWndExplorer))
		return FALSE;

	{
		CString versionString;
		versionString.Format(L"%X", TGitH::ms_LastMsysGitVersion);
		DebugOutput::TracePrintf(_T(__FUNCTION__) L": msysGitVersion: %s\n", versionString);
	}

	if (parser.HasKey(L"path") && parser.HasKey(L"pathfile")) {
		CMessageBox::Show(nullptr, IDS_ERR_INVALIDPATH, IDS_APPNAME, MB_ICONERROR);
		return FALSE;
	}

	CTGitPath cmdLinePath;
	CTGitPathList pathList;
	if (TortoiseProc::g_sGroupingUUID.empty())
		TortoiseProc::g_sGroupingUUID = parser.GetValStr(L"groupuuid");
	if (parser.HasKey(L"pathfile")) {
		CString sPathfileArgument = PathUtils::GetLongPathname(parser.GetValStr(L"pathfile")).c_str();

		cmdLinePath.SetFromUnknown(sPathfileArgument.GetString());
		if (pathList.LoadFromFile(cmdLinePath) == false)
			return FALSE; // no path specified!
		if (parser.HasKey(L"deletepathfile")) {
			// We can delete the temporary path file, now that we've loaded it
			::DeleteFile(cmdLinePath.GetWinPath());
		}
		// This was a path to a temporary file - it's got no meaning now, and
		// anybody who uses it again is in for a problem...
		cmdLinePath.Reset();

	} else {
		CString sPathArgument = parser.GetValStr(L"path").c_str();
		if (sPathArgument.IsEmpty() && parser.HasKey(L"path")) {
			CMessageBox::Show(hWndExplorer, IDS_ERR_INVALIDPATH, IDS_APPNAME, MB_ICONERROR);
			return FALSE;
		}
		pathList.LoadFromAsteriskSeparatedString(sPathArgument.GetString());
		if (!pathList.IsEmpty())
			cmdLinePath = pathList[0];
	}

	if (pathList.IsEmpty()) {
		pathList.AddPath(CTGitPath(GitCall::m_CurrentDirStorage.native()));
	}

	[[maybe_unused]] auto _ = ::CreateMutex(nullptr, FALSE, L"TortoiseGitProc.exe");
	if (!g_Git.SetCurrentDir(cmdLinePath.GetWinPathString().c_str(), parser.HasKey(L"submodule") == TRUE)) {
		for (int i = 0; i < pathList.GetCount(); ++i)
			if (g_Git.SetCurrentDir(pathList[i].GetWinPathString().c_str()))
				break;
	}
	if (parser.HasKey(L"pathfile") && parser.HasKey(L"submodule"))
		g_Git.SetCurrentDir(pathList[0].GetWinPathString().c_str(), true);

	if (!GitCall::m_CurrentDirStorage.empty())
		SetCurrentDirectory(GitCall::m_CurrentDirStorage.c_str());

	if (!cmd->CheckRepo())
		return FALSE;

	if (TortoiseProc::g_sGroupingUUID.empty()) {
		const auto groupSetting = wil::reg::try_get_tgit_dword(L"GroupTaskbarIconsPerRepo").value_or(3);
		switch (groupSetting) {
		case 1:
		case 2:
			// implemented differently to TortoiseSVN atm
			break;
		case 3:
		case 4: {
			if (const fs::path wcrootStd = GitAdminDir::HasAdminDir(GitCall::m_CurrentDirStorage); !wcrootStd.empty()) {
				// CString's MakeLower, not tgit::wstr::MakeLower: the hash names the taskbar
				// group, so the lowercasing must stay byte-for-byte what it was.
				CString wcroot(wcrootStd.c_str());
				const std::string wcRootA = CUnicodeUtils::StdGetUTF8(tgit::str::StringView(wcroot.MakeLower()));
				TortoiseProc::g_sGroupingUUID = TGitHash::GetHashText(wcRootA.data(), wcRootA.size(), TGitHash::HashType::HashSha1).c_str();

				ProjectProperties pp;
				pp.ReadProps();
				CString icon = pp.sIcon;
				icon.Replace('/', '\\');
				if (icon.IsEmpty())
					TortoiseProc::g_bGroupingRemoveIcon = true;
				TortoiseProc::g_sGroupingIcon = icon;
			}
		}
		break;
		default:
			// 0 ("group by application") is a documented setting, and any other
			// value is user-editable registry data: a no-op, as upstream had it
			break;
		}
	}

	CString sAppID = TaskbarUUID::GetTaskIDPerUUID(TortoiseProc::g_sGroupingUUID).c_str();
	InitializeJumpList(sAppID);
	EnsureGitLibrary(false);

	{
		CString err;
		try {
			// requires CWD to be set
			GitCall::m_LogEncode = TGitH::GetLogOutputEncode();

			// make sure all config files are read in order to check that none contains an error
			std::ignore = g_Git.GetConfigValue(L"doesnot.exist");
		} catch (const char* msg) {
			err = CUnicodeUtils::GetUnicode<CString>(msg);
		}

		if (!err.IsEmpty()) {
			const auto choice = CMessageBox::Show(hWndExplorer,
			                                      err,
			                                      IDS_APPNAME,
			                                      1,
			                                      IDI_ERROR,
			                                      IDS_PROC_EDITLOCALGITCONFIG,
			                                      IDS_PROC_EDITGLOBALGITCONFIG,
			                                      IDS_ABORTBUTTON);
			fs::path theConfig;
			if (choice == 1) {
				// open the config file with alternative editor
				theConfig = TGitH::GetGitLocalConfig();
			} else if (choice == 2) {
				// open the global config file with alternative editor
				theConfig = TGitH::GetGitGlobalConfig();
			}
			if (choice == 1 || choice == 2)
				TortoiseProc::LaunchAlternativeEditor(theConfig.c_str());
			return FALSE;
		}
	}

	// execute the requested command
	cmd->SetPaths(pathList, cmdLinePath);
	retSuccess = cmd->Execute();

	// Look for temporary files left around by TortoiseGit and
	// remove them. But only delete 'old' files because some
	// apps might still be needing the recent ones.
	TempFiles::DeleteOldTempFiles();

	// Since the dialog has been closed, return FALSE so that we exit the
	// application, rather than start the application's message pump.
	return FALSE;
}


void CTortoiseProcApp::InitializeJumpList(const CString& appid) {
	// for Win7 : use a custom jump list
	std::ignore = CoInitialize(nullptr);
	SetAppID(appid);
	DeleteJumpList(appid);
	DoInitializeJumpList(appid);
	CoUninitialize();
}


void CTortoiseProcApp::DoInitializeJumpList(const CString& appid) {
	ATL::CComPtr<ICustomDestinationList> pcdl;
	HRESULT hr = pcdl.CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER);
	if (FAILED(hr))
		return;

	hr = pcdl->SetAppID(appid);
	if (FAILED(hr))
		return;

	UINT uMaxSlots;
	ATL::CComPtr<IObjectArray> poaRemoved;
	hr = pcdl->BeginList(&uMaxSlots, IID_PPV_ARGS(&poaRemoved));
	if (FAILED(hr))
		return;

	ATL::CComPtr<IObjectCollection> poc;
	hr = poc.CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER);
	if (FAILED(hr))
		return;

	CString sTemp = CString(MAKEINTRESOURCE(IDS_MENUSETTINGS));
	StringUtils::RemoveAccelerators(sTemp);

	ATL::CComPtr<IShellLink> psl;
	hr = CreateShellLink(L"/command:settings", sTemp, 18, &psl);
	if (SUCCEEDED(hr)) {
		std::ignore = poc->AddObject(psl);
	}
	sTemp = CString(MAKEINTRESOURCE(IDS_MENUHELP));
	StringUtils::RemoveAccelerators(sTemp);
	psl.Release(); // Need to release the object before calling operator&()
	hr = CreateShellLink(L"/command:help", sTemp, 17, &psl);
	if (SUCCEEDED(hr)) {
		std::ignore = poc->AddObject(psl);
	}

	ATL::CComPtr<IObjectArray> poa;
	hr = poc.QueryInterface(&poa);
	if (SUCCEEDED(hr)) {
		std::ignore = pcdl->AppendCategory(CString(MAKEINTRESOURCE(IDS_PROC_TASKS)), poa);
		std::ignore = pcdl->CommitList();
	}
}


int CTortoiseProcApp::ExitInstance() {
	SYS_IMAGE_LIST().Cleanup();
	Gdiplus::GdiplusShutdown(m_gdiplusToken);

	CWinAppEx::ExitInstance();
	if (retSuccess)
		return 0;
	return -1;
}
