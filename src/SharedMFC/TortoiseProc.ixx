module;
#include <version.h>

#include "MiscUI/AnimationManager.h"

#include "Utils/HistoryCombo.h"
#include "Utils/SysImageList.h"
// CertificateValidationHelper.h's SCOPE_EXIT expands to wil::scope_guard. That name
// used to arrive, by accident, through ../TortoiseProc/Commands/Command.h, which the
// module no longer includes (the app moved to src/TortoiseProc/TortoiseProcApp.cpp);
// import it explicitly. MSVC warns C5202 (an import is not a preprocessor directive)
// until the header imports wil itself, as tgitlib/CreateProcessHelper.h does.
#include "Utils/CertificateValidationHelper.h"
#include "Utils/WindowsCredentialsStore.h"

#include "ProtoModules/CheckCertificateDlg.h"
#include "ProtoModules/UserPassword.h"
#include "ProtoModules/VersioncheckParser.h"

#include "Resources/resource.h"

#include <tgitlib/sanity.h>


#define STRUCT_IOVEC_DEFINED

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


export module TortoiseProc;


import tgit;

import DirFileEnum;
import PathUtils;
import ProjectProperties;

import TGitMessageBox;
import TaskbarUUID;
import Libraries;
import LangDll;
import tgit.Hooks;
import CmdLineParser;
import ConfigureGitExe;


// The Explorer window TortoiseGitProc.exe was started for (/hwnd:). Published by
// the app's InitInstance (src\TortoiseProc\TortoiseProcApp.cpp); stays null in
// every other executable that links this module, e.g. TortoiseGitBlame.
namespace TortoiseProc::detail {
HWND g_hWndExplorer{};
}


export namespace TortoiseProc {


// The taskbar grouping globals. Owned by TaskbarUUID, which StandAloneDlg can
// import and this module cannot be imported by (see there); re-exported so
// TortoiseProc::g_sGroupingUUID keeps naming them.
using TaskbarUUID::g_sGroupingUUID;
using TaskbarUUID::g_sGroupingIcon;
using TaskbarUUID::g_bGroupingRemoveIcon;



struct LaunchApplicationFlags {
	bool bWaitForStartup = false;
	bool bWaitForExit = false;
	HANDLE hWaitHandle = nullptr;
	bool bUAC = false;
	LPCWSTR psCWD = nullptr;
	UINT uiIDErrMessageFormat = 0;
	DWORD* pdwExitCode = nullptr;

	LaunchApplicationFlags() {}


	LaunchApplicationFlags& UseSpecificErrorMessage(const UINT idErrMessageFormat) {
		uiIDErrMessageFormat = idErrMessageFormat;
		return *this;
	}


	LaunchApplicationFlags& WaitForStartup(const bool b = true) {
		bWaitForStartup = b;
		return *this;
	}


	LaunchApplicationFlags& WaitForExit(const bool b = true, const HANDLE& h = nullptr, DWORD* pExitCode = nullptr) {
		Implies(h, b);
		bWaitForExit = b;
		hWaitHandle = h;
		pdwExitCode = pExitCode;
		return *this;
	}


	LaunchApplicationFlags& UAC(const bool b = true) {
		bUAC = b;
		return *this;
	}


	LaunchApplicationFlags& UseCWD(const LPCWSTR pCwd) {
		psCWD = pCwd;
		return *this;
	}
};



int ExploreTo(const HWND& hwnd, CString path) {
	if (PathFileExists(path)) {
		HRESULT ret = E_FAIL;
		ITEMIDLIST __unaligned* pidl = ILCreateFromPath(path);
		if (pidl) {
			ret = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
			ILFree(pidl);
		}
		return SUCCEEDED(ret) ? 0 : -1;
	}
	// if filepath does not exist any more, navigate to closest matching folder
	do {
		const int pos = path.ReverseFind(L'\\');
		if (pos <= 3)
			break;
		path.Truncate(pos);
	}
	while (!PathFileExists(path));
	return reinterpret_cast<INT_PTR>(ShellExecute(hwnd, L"explore", path, nullptr, nullptr, SW_SHOW)) > 32 ? 0 : -1;
}


bool ShowOpenWithDialog(const CString& file, const HWND& hwnd = nullptr) {
	OPENASINFO oi = {};
	oi.pcszFile = file;
	oi.oaifInFlags = OAIF_EXEC;
	return SUCCEEDED(SHOpenWithDialog(hwnd, &oi));
}


bool ShellOpen(const CString& file, const HWND& hwnd = nullptr) {
	if (reinterpret_cast<INT_PTR>(ShellExecute(hwnd, nullptr, file, nullptr, nullptr, SW_SHOW)) > HINSTANCE_ERROR)
		return true;

	return ShowOpenWithDialog(file, hwnd);
}


bool IsAlternativeEditorConfigured(CString* editTool = nullptr) {
	const CString alternativeEditor = CRegString(L"Software\\TortoiseGit\\AlternativeEditor");
	if (editTool)
		*editTool = alternativeEditor;
	return !alternativeEditor.IsEmpty() && !StringUtils::StartsWith(alternativeEditor, L"#");
}


/**
* Launch an external application (usually the diff viewer)
*/
bool LaunchApplication(const CString& sCommandLine, const LaunchApplicationFlags& flags) {
	LPCWSTR theCWD = nullptr;
	if (flags.psCWD)
		theCWD = flags.psCWD;

	if (flags.bUAC) {
		CString file, param;
		SHELLEXECUTEINFO shellinfo{};
		shellinfo.cbSize = sizeof(shellinfo);
		shellinfo.lpVerb = L"runas";
		shellinfo.nShow = SW_SHOWNORMAL;
		shellinfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shellinfo.lpDirectory = theCWD;

		int pos = sCommandLine.Find('"');
		if (pos == 0) {
			pos = sCommandLine.Find('"', 2);
			if (pos > 1) {
				file = sCommandLine.Mid(1, pos - 1);
				param = sCommandLine.Mid(pos + 1);
			} else {
				if (flags.uiIDErrMessageFormat != 0) {
					CString temp;
					temp.Format(flags.uiIDErrMessageFormat, (wil::CFormatMessageWrapper()).c_str());
					MessageBox(nullptr, temp, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
				}
				return false;
			}
		} else {
			pos = sCommandLine.Find(' ', 1);
			if (pos > 0) {
				file = sCommandLine.Left(pos);
				param = sCommandLine.Mid(pos + 1);
			} else
				file = sCommandLine;
		}

		shellinfo.lpFile = file;
		shellinfo.lpParameters = param;

		if (!ShellExecuteEx(&shellinfo)) {
			if (flags.uiIDErrMessageFormat != 0) {
				CString temp;
				temp.Format(flags.uiIDErrMessageFormat, (wil::CFormatMessageWrapper()).c_str());
				MessageBox(nullptr, temp, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
			}
			return false;
		}

		const auto piProcess = wil::unique_handle{std::move(shellinfo.hProcess)};
		if (piProcess) {
			if (flags.bWaitForStartup)
				WaitForInputIdle(piProcess.get(), 10000);

			if (flags.bWaitForExit) {
				DWORD count = 1;
				auto handles = std::array<HANDLE,2>{piProcess.get()};
				if (flags.hWaitHandle) {
					count = 2;
					handles[1] = flags.hWaitHandle;
				}
				WaitForMultipleObjects(count, handles.data(), FALSE, INFINITE);
				if (flags.hWaitHandle)
					CloseHandle(flags.hWaitHandle);

				if (flags.pdwExitCode) {
					if (!GetExitCodeProcess(piProcess.get(), flags.pdwExitCode))
						return false;
				}
			}
		}

		return true;
	}

	STARTUPINFO startup = {};
	PROCESS_INFORMATION process = {};
	startup.cb = sizeof(startup);

	CString cleanCommandLine(sCommandLine);
	if (CreateProcess(nullptr, cleanCommandLine.GetBuffer(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, theCWD, &startup, &process) == 0) {
		if (flags.uiIDErrMessageFormat) {
			CString temp;
			temp.Format(flags.uiIDErrMessageFormat, (wil::CFormatMessageWrapper()).c_str());
			MessageBox(nullptr, temp, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
		}
		return false;
	}

	[[maybe_unused]] const auto RAII_pi_thread = wil::unique_handle(std::move(process.hThread));
	const auto piProcess = wil::unique_handle(std::move(process.hProcess));

	AllowSetForegroundWindow(process.dwProcessId);

	if (flags.bWaitForStartup)
		WaitForInputIdle(piProcess.get(), 10000);

	if (flags.bWaitForExit) {
		DWORD count = 1;
		HANDLE handles[2];
		handles[0] = piProcess.get();
		if (flags.hWaitHandle) {
			count = 2;
			handles[1] = flags.hWaitHandle;
		}
		WaitForMultipleObjects(count, handles, FALSE, INFINITE);
		if (flags.hWaitHandle)
			CloseHandle(flags.hWaitHandle);

		if (flags.pdwExitCode) {
			if (!GetExitCodeProcess(piProcess.get(), flags.pdwExitCode))
				return false;
		}
	}

	return true;
}


bool RunTortoiseGitProc(const std::wstring_view sCommandLine, const bool uac = false, const bool includeGroupingUUID = false) {
	auto sCmd = std::format(L"\"{}\" {}", til::GetAppDirectory() / L"TortoiseGitProc.exe", sCommandLine);
	if (AfxGetMainWnd()->GetSafeHwnd() && !sCommandLine.contains(L"/hwnd:"))
		std::format_to(std::back_inserter(sCmd), L" /hwnd:{}", static_cast<const void*>(AfxGetMainWnd()->GetSafeHwnd()));
	if (!g_sGroupingUUID.empty() && includeGroupingUUID)
		std::format_to(std::back_inserter(sCmd), L" /groupuuid:\"{}\"", g_sGroupingUUID);

	return LaunchApplication(sCmd.c_str(), LaunchApplicationFlags().UAC(uac));
}
// Constrained so a literal or `const wchar_t*` reaches only the std::wstring_view
// overload above; otherwise both are one user-defined conversion away (C2668).
template <typename T>
	requires std::same_as<std::remove_cvref_t<T>, CString>
bool RunTortoiseGitProc(const T& sCommandLine, const bool uac = false, const bool includeGroupingUUID = false) {
	return RunTortoiseGitProc(str::as_view(sCommandLine), uac, includeGroupingUUID);
}


/**
 * Sets up all the default diff and merge scripts.
 * \param force if true, overwrite all existing entries
 * \param type "Diff", "Merge" or an empty string
 */
bool SetupDiffScripts(const bool force, const CString& type) {
	CString scriptsdir = PathUtils::GetAppParentDirectory().c_str();
	scriptsdir += L"Diff-Scripts";
	DirFileEnum::CSimpleFileFind files(scriptsdir);
	while (files.FindNextFileNoDirectories()) {
		CString file = files.GetFilePath();
		CString filename = files.GetFileName();
		CString ext = file.Mid(file.ReverseFind('-') + 1);
		ext = L"." + ext.Left(ext.ReverseFind(L'.'));
		std::set<CString> extensions;
		extensions.insert(ext);
		CString kind;
		if (StringUtils::EndsWithI(file, L"vbs"))
			kind = L" //E:vbscript";
		if (StringUtils::EndsWithI(file, L"js"))
			kind = L" //E:javascript";
		// open the file, read the first line and find possible extensions
		// this script can handle
		try {
			CStdioFile f(file, CFile::modeRead | CFile::shareDenyNone);
			CString extline;
			if (f.ReadString(extline)) {
				if ((extline.GetLength() > 15) &&
					(StringUtils::StartsWith(extline, L"// extensions: ") ||
						StringUtils::StartsWith(extline, L"' extensions: "))) {
					if (extline[0] == '/')
						extline = extline.Mid(static_cast<int>(wcslen(L"// extensions: ")));
					else
						extline = extline.Mid(static_cast<int>(wcslen(L"' extensions: ")));
					CString sToken;
					int curPos = 0;
					sToken = extline.Tokenize(L";", curPos);
					while (!sToken.IsEmpty()) {
						if (!sToken.IsEmpty()) {
							if (sToken[0] != '.')
								sToken = L"." + sToken;
							extensions.insert(sToken);
						}
						sToken = extline.Tokenize(L";", curPos);
					}
				}
			}
			f.Close();
		} catch (CFileException& e) {
			tgit::DebugOutput::Trace(L"[SetupDiffScripts] file issues {}", e.m_cause);
			e.Delete();
		}

		for (const auto& extension : extensions) {
			if (type.IsEmpty() || (type.Compare(L"Diff") == 0)) {
				if (StringUtils::StartsWithI(filename, L"diff-")) {
					auto diffreg = CRegString(L"Software\\TortoiseGit\\DiffTools\\" + extension);
					auto diffregstring = static_cast<CRegString::StringT>(diffreg);
					if (force || (diffregstring.IsEmpty()) || (diffregstring.Find(filename) >= 0))
						diffreg = L"wscript.exe \"" + file + L"\" %base %mine" + kind;
				}
			}
			if (type.IsEmpty() || (type.Compare(L"Merge") == 0)) {
				if (StringUtils::StartsWithI(filename, L"merge-")) {
					auto diffreg = CRegString(L"Software\\TortoiseGit\\MergeTools\\" + extension);
					auto diffregstring = static_cast<CRegString::StringT>(diffreg);
					if (force || (diffregstring.IsEmpty()) || (diffregstring.Find(filename) >= 0))
						diffreg = L"wscript.exe \"" + file + L"\" %merged %theirs %mine %base" + kind;
				}
			}
		}
	}

	return true;
}


bool IsAdminLogin() {
	SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
	PSID administratorsGroup;
	// Initialize SID.
	if (!AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administratorsGroup))
		return false;
	SCOPE_EXIT { FreeSid(administratorsGroup); };

	// Check whether the token is present in admin group.
	BOOL isInAdminGroup = FALSE;
	if (!CheckTokenMembership(nullptr, administratorsGroup, &isInAdminGroup))
		return false;

	return !!isInAdminGroup;
}


bool LaunchNotepad(const CString& filename, const bool uac = false) {
	const auto pszPath = wil::GetKnownFolderPath(FOLDERID_System);
	if (pszPath.empty())
		return false;

	const auto sCmd = std::format(LR"("{}" "{}")", pszPath / L"notepad.exe", filename);

	return LaunchApplication(sCmd.c_str(), LaunchApplicationFlags().UAC(uac).UseSpecificErrorMessage(IDS_ERR_TEXTVIEWSTART));
}


/**
 * Launches the standard text viewer/editor application which is associated
 * with txt files.
 * \return TRUE if the program could be started.
 */
BOOL StartTextViewer(const std::wstring_view file) {
	auto viewer = wil::reg::try_get_string(HKEY_CLASSES_ROOT, L"\\Shell\\Open\\Command\\", {}).value_or({});

	DWORD len = ExpandEnvironmentStrings(viewer.c_str(), nullptr, 0);
	const auto buf = std::make_unique<wchar_t[]>(len + 1);
	ExpandEnvironmentStrings(viewer.c_str(), buf.get(), len);
	viewer = buf.get();
	len = ExpandEnvironmentStrings(file.data(), nullptr, 0);
	const auto buf2 = std::make_unique<wchar_t[]>(len + 1);
	ExpandEnvironmentStrings(file.data(), buf2.get(), len);

	const auto file_expanded = buf2.get();
	if (viewer.empty())
		return ShowOpenWithDialog(file_expanded) ? TRUE : FALSE;

	constexpr auto placeholder = L"%1"sv;
	if (const auto pos = viewer.find(placeholder); pos != viewer.npos) {
		viewer.replace(pos, pos + placeholder.size(), file_expanded);
	} else {
		viewer += L' ';
		viewer += file_expanded;
	}

	if (!LaunchApplication(viewer.c_str(), LaunchApplicationFlags().UseSpecificErrorMessage(IDS_ERR_TEXTVIEWSTART)))
		return FALSE;
	return TRUE;
}


bool LaunchAlternativeEditor(const CString& filename) {
	CString editTool;
	if (!IsAlternativeEditorConfigured(&editTool))
		return LaunchNotepad(filename);

	const auto sCmd = std::format(LR"("{}" "{}")", editTool, filename);

	return LaunchApplication(sCmd.c_str(), LaunchApplicationFlags().UseSpecificErrorMessage(IDS_ERR_TEXTVIEWSTART));
}



/// The application object (CTortoiseProcApp) lives in TortoiseGitProc.exe, in
/// src\TortoiseProc\TortoiseProcApp.cpp. Code here and in its importers that
/// needs the running app uses AfxGetApp(), which is right in every executable.

/// Called once by TortoiseGitProc's InitInstance with the /hwnd: it was given.
void SetExplorerHWND(const HWND hWnd) {
	detail::g_hWndExplorer = hWnd;
}

/// The Explorer window this process was started for, or nullptr if there is
/// none (any more): re-checked on every call, as the window may have closed.
HWND GetExplorerHWND() {
	return ::IsWindow(detail::g_hWndExplorer) ? detail::g_hWndExplorer : nullptr;
}


/// The full key path of a TortoiseGit setting, for CRegString. REG_MSYSGIT_PATH &
/// co. are value names under Software\TortoiseGit now (Registry::TortoiseGitKey, which git_h reads them under);
/// in the header era they were full paths, and passing one of the new ones
/// straight to CRegString would compile and silently read HKCU\MSysGit.
template <std::size_t LEN2>
constexpr auto TGitRegPath(const wchar_t (&valName)[LEN2]) {
		constexpr auto prefix = L"Software\\TortoiseGit\\"sv;
	constexpr std::size_t total_len = prefix.size() + LEN2;

	std::array<wchar_t, total_len> result{};
	std::copy(prefix.begin(), prefix.end(), result.begin());
	std::copy_n(valName, LEN2, result.begin() + prefix.size());
	result.back() = L'\0';

	return CString{ result.data(), static_cast<int>(result.size()) };
}


//////////////////////////////////////////////////////////////////////////

using cert_t = std::vector<char>;
cert_t last_accepted_cert{};

constexpr boolean cert_t_cmp(const git_cert_x509* cert) {
	const std::span received{ static_cast<const char*>(cert->data), cert->len };
	return std::ranges::equal(last_accepted_cert, received);
}

constexpr void cert_t_set(const git_cert_x509* cert) {
	last_accepted_cert.assign(static_cast<const char*>(cert->data), static_cast<const char*>(cert->data) + cert->len);
}

int Git2GetUserPassword(git_credential **out, const char *url, const char *username_from_url, unsigned int /*allowed_types*/, void * /*payload*/)
{
	CUserPassword dlg;
	dlg.m_URL = CUnicodeUtils::GetUnicode<CString>(url);
	if (username_from_url)
		dlg.m_UserName = CUnicodeUtils::GetUnicode<CString>(username_from_url);

	if (dlg.DoModal() == IDOK)
		return git_credential_userpass_plaintext_new(out, CUnicodeUtils::GetUTF8<CStringA>(dlg.m_UserName), dlg.m_passwordA);

	git_error_set_str(git_error_t::GIT_ERROR_NONE, "User cancelled.");
	return git_error_code::GIT_EUSER;
}

int Git2CertificateCheck(git_cert* base_cert, int /*valid*/, const char* host, void* /*payload*/)
{
	if (base_cert->cert_type == git_cert_t::GIT_CERT_X509)
	{
		auto cert = reinterpret_cast<git_cert_x509*>(base_cert);

		if (cert_t_cmp(cert))
			return 0;

		PCCERT_CONTEXT pServerCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, static_cast<BYTE*>(cert->data), static_cast<DWORD>(cert->len));
		SCOPE_EXIT { CertFreeCertificateContext(pServerCert); };

		DWORD verificationError = VerifyServerCertificate(pServerCert, CUnicodeUtils::GetUnicode<CString>(host).GetBuffer(), 0);
		if (!verificationError)
		{
			cert_t_set(cert);
			return 0;
		}

		CString servernameInCert;
		CertGetNameString(pServerCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, CStrBuf(servernameInCert, 128), 128);

		CString issuer;
		CertGetNameString(pServerCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, nullptr, CStrBuf(issuer, 128), 128);

		CCheckCertificateDlg dlg;
		dlg.cert = cert;
		dlg.m_sCertificateCN = servernameInCert;
		dlg.m_sCertificateIssuer = issuer;
		dlg.m_sHostname = CUnicodeUtils::GetUnicode<CString>(host);
		dlg.m_sError = wil::CFormatMessageWrapper(verificationError).c_str();
		if (dlg.DoModal() == IDOK)
		{
			cert_t_set(cert);
			return 0;
		}
	}
	return git_error_code::GIT_ECERTIFICATE;
}


//////////////////////////////////////////////////////////////////////////

CWnd* GetParentCWnd(const HWND& hWnd) {
	if (GetExplorerHWND() != hWnd && IsWindow(hWnd))
		return CWnd::FromHandle(hWnd);

	return nullptr;
}


void CheckForNewerVersion() {
	// check for newer versions
	if (CRegDWORD(L"Software\\TortoiseGit\\VersionCheck", TRUE) != FALSE) {
		time_t now;
		tm ptm;

		time(&now);
		if ((now != 0) && (localtime_s(&ptm, &now) == 0)) {
			// TODO: decide of policy
#if PREVIEW
			// Check daily for new preview releases
			CRegDWORD oldday = CRegDWORD(L"Software\\TortoiseGit\\CheckNewerDay", DWORD(-1));
			if (oldday == -1)
				oldday = ptm.tm_yday;
			else {
				if (static_cast<DWORD>(oldday) != static_cast<DWORD>(ptm.tm_yday)) {
					oldday = ptm.tm_yday;
#else
			// we don't calculate the real 'week of the year' here
			// because just to decide if we should check for an update
			// that's not needed.
			int week = (ptm.tm_yday + CRegDWORD(L"Software\\TortoiseGit\\CheckNewerWeekDay", 0)) / 7;

			CRegDWORD oldweek = CRegDWORD(L"Software\\TortoiseGit\\CheckNewerWeek", -1);
			if (oldweek == -1)
				oldweek = week; // first start of TortoiseProc, no update check needed
			else {
				if (static_cast<DWORD>(week) != oldweek) {
					oldweek = week;
#endif
					RunTortoiseGitProc(L"/command:updatecheck"sv, false, false);
				}
			}
		}
	}
}


void CheckUpgrade() {
	CRegString regVersion = CRegString(L"Software\\TortoiseGit\\CurrentVersion");
	CString sVersion = regVersion;
	DebugOutput::TracePrintf(_T(__FUNCTION__) L": Current TGit Version %s vs. last used version %s\n", _T(STRPRODUCTVER), sVersion);
	if (sVersion.Compare(_T(STRPRODUCTVER)) == 0)
		return;
	// we're starting the first time with a new version!

	unsigned lVersion = 0;
	int pos = sVersion.Find('.');
	if (pos > 0) {
		lVersion = (_wtol(sVersion.Left(pos)) << 24);
		lVersion |= (_wtol(sVersion.Mid(pos + 1)) << 16);
		pos = sVersion.Find('.', pos + 1);
		lVersion |= (_wtol(sVersion.Mid(pos + 1)) << 8);
	} else {
		pos = sVersion.Find(',');
		if (pos > 0) {
			lVersion = (_wtol(sVersion.Left(pos)) << 24);
			lVersion |= (_wtol(sVersion.Mid(pos + 1)) << 16);
			pos = sVersion.Find(',', pos + 1);
			lVersion |= (_wtol(sVersion.Mid(pos + 1)) << 8);
		}
	}

	// generic cleanup
	if (CRegStdDWORD(L"Software\\TortoiseGit\\UseLibgit2", TRUE) != TRUE) {
		if (CMessageBox::Show(nullptr,
		                      L"You have disabled the usage of libgit2 in TortoiseGit.\n\nThis might be the case in order to resolve an issue in an older TortoiseGit version.\n\nDo you want to restore the default value (i.e., enable it)?",
		                      L"TortoiseGit",
		                      MB_ICONQUESTION | MB_YESNO) == IDYES)
			CRegStdDWORD(L"Software\\TortoiseGit\\UseLibgit2").removeValue();
	}

	if (CRegStdDWORD(L"Software\\TortoiseGit\\UseLibgit2_mask").exists()) {
		if (CMessageBox::Show(nullptr,
		                      L"You have a non-default setting of UseLibgit2_mask in your registry.\n\nThis might be the case in order to resolve an issue in an older TortoiseGit version.\n\nDo you want to restore the default value (i.e., remove custom setting from registry)?",
		                      L"TortoiseGit",
		                      MB_ICONQUESTION | MB_YESNO) == IDYES)
			CRegStdDWORD(L"Software\\TortoiseGit\\UseLibgit2_mask").removeValue();
	}

	CMessageBox::RemoveRegistryKey(L"OldMsysgitVersionWarning");

	CRegDWORD checkNewerWeekDay = CRegDWORD(L"Software\\TortoiseGit\\CheckNewerWeekDay", 0);
	if (!checkNewerWeekDay.exists() || lVersion <= ConvertVersionToInt(1, 8, 16)) {
		auto mt = std::mt19937{ std::random_device{}() };
		std::uniform_int_distribution dist(0, 6);
		checkNewerWeekDay = dist(mt);
	}

	// version specific updates
	if (lVersion <= ConvertVersionToInt(2, 19, 0, 0)) {
		// upgrade to 2.19.0: force recreation of all diff scripts.
		SetupDiffScripts(true, CString());
	}

	if (lVersion <= ConvertVersionToInt(2, 9, 2)) {
		if (auto inlineAdded = CRegDWORD(L"Software\\TortoiseGitMerge\\InlineAdded"); inlineAdded.exists()) {
			CRegDWORD(L"Software\\TortoiseGitMerge\\Colors\\InlineAdded") = static_cast<DWORD>(inlineAdded);
			inlineAdded.removeValue();
		}
		if (auto inlineRemoved = CRegDWORD(L"Software\\TortoiseGitMerge\\InlineRemoved"); inlineRemoved.exists()) {
			CRegDWORD(L"Software\\TortoiseGitMerge\\Colors\\InlineRemoved") = static_cast<DWORD>(inlineRemoved);
			inlineRemoved.removeValue();
		}
	}

	if (lVersion <= ConvertVersionToInt(2, 9, 1))
		CRegStdDWORD(L"Software\\TortoiseGit\\TortoiseProc\\PatchDlgWidth").removeValue();

	if (lVersion <= ConvertVersionToInt(2, 4, 1)) {
		CRegStdDWORD(L"Software\\TortoiseGit\\CommitAskBeforeCancel").removeValue();
	}

	if (lVersion <= ConvertVersionToInt(2, 4, 0)) {
		CRegStdDWORD commmitAskBeforeCancel(L"Software\\TortoiseGit\\CommitAskBeforeCancel");
		if (commmitAskBeforeCancel.exists() && commmitAskBeforeCancel != IDYES)
			commmitAskBeforeCancel = IDYES;
	}

	if (lVersion <= ConvertVersionToInt(2, 2, 1)) {
		// This used to migrate the SMTP password out of the registry and into the
		// Windows credential store. The mail feature is gone, so there is nothing
		// left to migrate it *for* - but leaving a plaintext password behind in
		// the registry because its consumer was deleted would be worse than the
		// bug the migration was written to fix. Delete both, unconditionally.
		CRegString(L"Software\\TortoiseGit\\TortoiseProc\\SendMail\\Username").removeValue();
		CRegString(L"Software\\TortoiseGit\\TortoiseProc\\SendMail\\Password").removeValue();
		for (const CString& setting : { L"SyncIn", L"SyncOut" }) {
			CRegDWORD reg(L"Software\\TortoiseGit\\StatusColumns\\" + setting + L"loglistVersion", 0xff);
			if (reg == 6)
				reg.removeValue();
		}
	}

	if (lVersion <= ConvertVersionToInt(2, 1, 5)) {
		// We updated GITSLC_COL_VERSION, but only significant changes were made for GitStatusList
		// so, smoothly migrate GitLoglistBase settings
		for (const CString& setting : { L"log", L"Blame", L"Rebase", L"reflog", L"SyncIn", L"SyncOut" }) {
			CRegDWORD reg(L"Software\\TortoiseGit\\StatusColumns\\" + setting + L"loglistVersion", 0xff);
			if (reg == 5)
				reg = 6;
		}
	}

	if (lVersion <= ConvertVersionToInt(1, 9, 0)) {
		if (CRegDWORD(L"Software\\TortoiseGit\\TGitCacheCheckContent", TRUE) == FALSE) {
			CRegDWORD(L"Software\\TortoiseGit\\TGitCacheCheckContentMaxSize") = 0;
			CRegDWORD(L"Software\\TortoiseGit\\TGitCacheCheckContent").removeValue();
		}
	}

	if (lVersion <= ConvertVersionToInt(1, 8, 8, 1)) {
		CRegStdDWORD(L"Software\\TortoiseGit\\StatusColumns\\BrowseRefs").removeValue();
		CRegStdString(L"Software\\TortoiseGit\\StatusColumns\\BrowseRefs_Order").removeValue();
		CRegStdString(L"Software\\TortoiseGit\\StatusColumns\\BrowseRefs_Width").removeValue();
	}

	if (lVersion <= ConvertVersionToInt(1, 8, 4, 1)) {
		// Was: migrate UseMAPI to DeliveryType. Both settings belonged to the
		// removed mail feature, so both just go.
		CRegStdDWORD(L"Software\\TortoiseGit\\TortoiseProc\\SendMail\\UseMAPI").removeValue();
		CRegStdDWORD(L"Software\\TortoiseGit\\TortoiseProc\\SendMail\\DeliveryType").removeValue();
	}

	if (lVersion <= ConvertVersionToInt(1, 8, 2, 2)) {
		// upgrade to 1.8.3: force recreation of all diff scripts.
		SetupDiffScripts(true, CString());
	}

	if (lVersion <= ConvertVersionToInt(1, 8, 1)) {
		if (CRegStdDWORD(L"Software\\TortoiseGit\\LogTopoOrder", TRUE) == FALSE)
			CRegStdDWORD(L"Software\\TortoiseGit\\LogOrderBy") = 0;

		// smoothly migrate broken msysgit path settings
		CString oldmsysGitSetting = CRegString(CString(std::format(L"Software\\TortoiseGit\\{}", REG_MSYSGIT_PATH).c_str()));
		oldmsysGitSetting.TrimRight(L'\\');
		if (oldmsysGitSetting.GetLength() > 4 && StringUtils::EndsWith(oldmsysGitSetting, L"\\cmd")) {
			CString newPath = oldmsysGitSetting.Left(oldmsysGitSetting.GetLength() - 3) + L"bin";
			if (PathFileExists(newPath + L"\\git.exe")) {
				CRegString(CString(std::format(L"Software\\TortoiseGit\\{}", REG_MSYSGIT_PATH).c_str())) = newPath;
				g_Git.m_bInitialized = FALSE;
				g_Git.CheckMsysGitDir();
			}
		}
	}

	if (lVersion <= ConvertVersionToInt(1, 4, 0)) {
		CRegStdDWORD(L"Software\\TortoiseGit\\OwnerdrawnMenus").removeValue();
	}

	if (lVersion <= ConvertVersionToInt(1, 7, 6)) {
		std::ignore = CoInitialize(nullptr);
		EnsureGitLibrary();
		CoUninitialize();
		CRegStdDWORD(L"Software\\TortoiseGit\\ConvertBase").removeValue();
		CRegStdDWORD(L"Software\\TortoiseGit\\DiffProps").removeValue();
		if (CRegStdDWORD(L"Software\\TortoiseGit\\CheckNewer", TRUE) == FALSE)
			CRegStdDWORD(L"Software\\TortoiseGit\\VersionCheck") = FALSE;
		CRegStdDWORD(L"Software\\TortoiseGit\\CheckNewer").removeValue();
	}

	DebugOutput::TracePrintf(_T(__FUNCTION__) L": Setting up diff scripts ...\n");
	SetupDiffScripts(false, CString());

	// set the current version so we don't come here again until the next update!
	regVersion = _T(STRPRODUCTVER);
}


}
export using namespace TortoiseProc;
