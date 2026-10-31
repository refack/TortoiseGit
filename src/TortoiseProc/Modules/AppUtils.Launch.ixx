module;
#include "stdafx.h"
#include "std.h"

#include <shlobj_core.h>
#include "TortoiseProc.h"



export module AppUtils.Launch;
import MessageBox;


template <tgit::str::IsATLCStringDuck TStr>
TStr EscapeAndQuoteParameters(const TStr& input)
{
	// let's go the safe way as there is no standard on Windows (cf. https://www.daviddeley.com/autohotkey/parameters/parameters.htm#WINNOSTANDARD)
	TStr parameter = input;
	parameter.Replace(L"\"", L"%22");
	parameter.TrimRight(L'\\'); // should never occurr
	return L'"' + parameter + L'"';
}

/**
 * \ingroup TortoiseProc
 * Starting other processes: TortoiseGitProc itself, the configured diff, merge and
 * patch tools, text editors, and the shell. Everything here ends in CreateProcess or
 * ShellExecute; nothing opens a TortoiseGit dialog of its own.
 */
export namespace CAppUtils {

/////////////////// some hacky GLOBALS //////////////////
// Defined by each MFC application. Declared in the global module fragment so it
// names the same variable as StandAloneDlg.h's, not one attached to this module.
std::wstring g_sGroupingUUID{};
std::wstring g_sGroupingIcon{};
bool g_bGroupingRemoveIcon{};
/////////////////////////////////////////////////////////

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

/**
* Flags for StartExtDiff function.
*/
struct DiffFlags {
	bool bWait;
	bool bReadOnly;
	bool bAlternativeTool; // If true, invert selection of git's tool vs. the configured external diff tool

	DiffFlags() :
		bWait(false), bReadOnly(false), bAlternativeTool(false) {}


	DiffFlags& Wait(const bool b = true) {
		bWait = b;
		return *this;
	}


	DiffFlags& ReadOnly(const bool b = true) {
		bReadOnly = b;
		return *this;
	}


	DiffFlags& AlternativeTool(const bool b = true) {
		bAlternativeTool = b;
		return *this;
	}
};

/**
* Launch an external application (usually the diff viewer)
*/
bool LaunchApplication(const CString& sCommandLine, const LaunchApplicationFlags& flags)
{
	LPCWSTR theCWD = nullptr;
	if (flags.psCWD)
		theCWD = flags.psCWD;

	if (flags.bUAC)
	{
		CString file, param;
		SHELLEXECUTEINFO shellinfo{};
		shellinfo.cbSize = sizeof(shellinfo);
		shellinfo.lpVerb = L"runas";
		shellinfo.nShow = SW_SHOWNORMAL;
		shellinfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shellinfo.lpDirectory = theCWD;

		int pos = sCommandLine.Find('"');
		if (pos == 0)
		{
			pos = sCommandLine.Find('"', 2);
			if (pos > 1)
			{
				file = sCommandLine.Mid(1, pos - 1);
				param = sCommandLine.Mid(pos + 1);
			}
			else
			{
				if (flags.uiIDErrMessageFormat != 0)
				{
					CString temp;
					temp.Format(flags.uiIDErrMessageFormat, (CFormatMessageWrapper()).c_str());
					MessageBox(nullptr, temp, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
				}
				return false;
			}
		}
		else
		{
			pos = sCommandLine.Find(' ', 1);
			if (pos > 0)
			{
				file = sCommandLine.Left(pos);
				param = sCommandLine.Mid(pos + 1);
			}
			else
				file = sCommandLine;
		}

		shellinfo.lpFile = file;
		shellinfo.lpParameters = param;

		if (!ShellExecuteEx(&shellinfo))
		{
			if (flags.uiIDErrMessageFormat != 0)
			{
				CString temp;
				temp.Format(flags.uiIDErrMessageFormat, (CFormatMessageWrapper()).c_str());
				MessageBox(nullptr, temp, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
			}
			return false;
		}

		const CAutoGeneralHandle piProcess(std::move(shellinfo.hProcess));
		if (piProcess)
		{
			if (flags.bWaitForStartup)
				WaitForInputIdle(piProcess, 10000);

			if (flags.bWaitForExit)
			{
				DWORD count = 1;
				HANDLE handles[2];
				handles[0] = piProcess;
				if (flags.hWaitHandle)
				{
					count = 2;
					handles[1] = flags.hWaitHandle;
				}
				WaitForMultipleObjects(count, handles, FALSE, INFINITE);
				if (flags.hWaitHandle)
					CloseHandle(flags.hWaitHandle);

				if (flags.pdwExitCode)
				{
					if (!GetExitCodeProcess(piProcess, flags.pdwExitCode))
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
	if (CreateProcess(nullptr, cleanCommandLine.GetBuffer(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, theCWD, &startup, &process) == 0)
	{
		if (flags.uiIDErrMessageFormat)
		{
			CString temp;
			temp.Format(flags.uiIDErrMessageFormat, (CFormatMessageWrapper()).c_str());
			MessageBox(nullptr, temp, L"TortoiseGit", MB_OK | MB_ICONINFORMATION);
		}
		return false;
	}

	[[maybe_unused]] const auto RAII_pi_thread = wil::unique_handle(std::move(process.hThread));
	const auto piProcess = wil::unique_handle(std::move(process.hProcess));

	AllowSetForegroundWindow(process.dwProcessId);

	if (flags.bWaitForStartup)
		WaitForInputIdle(piProcess.get(), 10000);

	if (flags.bWaitForExit)
	{
		DWORD count = 1;
		HANDLE handles[2];
		handles[0] = piProcess.get();
		if (flags.hWaitHandle)
		{
			count = 2;
			handles[1] = flags.hWaitHandle;
		}
		WaitForMultipleObjects(count, handles, FALSE, INFINITE);
		if (flags.hWaitHandle)
			CloseHandle(flags.hWaitHandle);

		if (flags.pdwExitCode)
		{
			if (!GetExitCodeProcess(piProcess.get(), flags.pdwExitCode))
				return false;
		}
	}

	return true;
}

bool RunTortoiseGitProc(const CString& sCommandLine, const bool uac = false, const bool includeGroupingUUID = false)
{
	const std::wstring pathToExecutable = PathUtils::GetAppDirectory() / L"TortoiseGitProc.exe";
	CString sCmd = std::format(L"\"{}\" {}", pathToExecutable, sCommandLine).c_str();
	if (AfxGetMainWnd()->GetSafeHwnd() && (sCommandLine.Find(L"/hwnd:") < 0))
		sCmd.AppendFormat(L" /hwnd:%p", AfxGetMainWnd()->GetSafeHwnd());
	if (!g_sGroupingUUID.empty() && includeGroupingUUID)
	{
		sCmd += L" /groupuuid:\"";
		sCmd += g_sGroupingUUID.c_str();
		sCmd += L'"';
	}

	return LaunchApplication(sCmd, LaunchApplicationFlags().UAC(uac));
}

bool IsAdminLogin()
{
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

int ExploreTo(const HWND& hwnd, CString path)
{
	if (PathFileExists(path))
	{
		HRESULT ret = E_FAIL;
		ITEMIDLIST __unaligned* pidl = ILCreateFromPath(path);
		if (pidl)
		{
			ret = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
			ILFree(pidl);
		}
		return SUCCEEDED(ret) ? 0 : -1;
	}
	// if filepath does not exist any more, navigate to closest matching folder
	do
	{
		const int pos = path.ReverseFind(L'\\');
		if (pos <= 3)
			break;
		path.Truncate(pos);
	} while (!PathFileExists(path));
	return reinterpret_cast<INT_PTR>(ShellExecute(hwnd, L"explore", path, nullptr, nullptr, SW_SHOW)) > 32 ? 0 : -1;
}

bool ShowOpenWithDialog(const CString& file, const HWND& hwnd = nullptr)
{
	OPENASINFO oi = {};
	oi.pcszFile = file;
	oi.oaifInFlags = OAIF_EXEC;
	return SUCCEEDED(SHOpenWithDialog(hwnd, &oi));
}

bool ShellOpen(const CString& file, const HWND& hwnd = nullptr)
{
	if (reinterpret_cast<INT_PTR>(ShellExecute(hwnd, nullptr, file, nullptr, nullptr, SW_SHOW)) > HINSTANCE_ERROR)
		return true;

	return ShowOpenWithDialog(file, hwnd);
}

/**
 * Launches the standard text viewer/editor application which is associated
 * with txt files.
 * \return TRUE if the program could be started.
 */
BOOL StartTextViewer(const std::wstring_view file)
{
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

bool LaunchNotepad(const CString& filename, const bool uac = false)
{
	const auto pszPath = wil::GetKnownFolderPath(FOLDERID_System);
	if (pszPath.empty())
		return false;

	const auto sCmd = std::format(LR"("{}" "{}")", pszPath / L"notepad.exe", filename);

	return LaunchApplication(sCmd.c_str(), LaunchApplicationFlags().UAC(uac).UseSpecificErrorMessage(IDS_ERR_TEXTVIEWSTART));
}

bool IsAlternativeEditorConfigured(CString* editTool = nullptr)
{
	const CString alternativeEditor = CRegString(L"Software\\TortoiseGit\\AlternativeEditor");
	if (editTool)
		*editTool = alternativeEditor;
	return !alternativeEditor.IsEmpty() && !StringUtils::StartsWith(alternativeEditor, L"#");
}

bool LaunchAlternativeEditor(const CString& filename)
{
	CString editTool;
	if (!IsAlternativeEditorConfigured(&editTool))
		return LaunchNotepad(filename);

	const auto sCmd = std::format(LR"("{}" "{}")", editTool, filename);

	return LaunchApplication(sCmd.c_str(), LaunchApplicationFlags().UseSpecificErrorMessage(IDS_ERR_TEXTVIEWSTART));
}

/**
* Launch the external blame viewer
*/
bool LaunchTortoiseBlame(const CString& sBlameFile, const CString& Rev, const CString& sParams = CString())
{
	auto args = std::vector{
		std::format(LR"("{}")", (PathUtils::GetAppDirectory() / L"TortoiseGitBlame.exe").native()),
		std::format(L"/path:{}", TCmdLineParser::EscapeValue(sBlameFile)),
	};
	if (!g_sGroupingUUID.empty())
		args.push_back(std::format(LR"(/groupuuid:"{}")", g_sGroupingUUID));
	if (!Rev.IsEmpty() && Rev != GitRev::GetWorkingCopyRef())
		args.push_back(std::format(LR"(/rev:"{}")", Rev.GetString()));
	if (!sParams.IsEmpty())
		args.push_back(sParams.GetString());

	const auto cmd = args | std::views::join_with(L' ') | std::ranges::to<std::wstring>();
	return LaunchApplication(cmd.c_str(), LaunchApplicationFlags().UseSpecificErrorMessage(IDS_ERR_TGITBLAME));
}


CString PickDiffTool(const CTGitPath& file1, const CTGitPath& file2)
{
	std::wstring filename2 = file2.GetFilename();
	tgit::wstr::MakeLower(filename2);
	CString difftool = CRegString(std::format(L"Software\\TortoiseGit\\DiffTools\\{}", filename2).c_str());
	if (!difftool.IsEmpty())
		return difftool;
	std::wstring filename1 = file1.GetFilename();
	tgit::wstr::MakeLower(filename1);
	difftool = CRegString(std::format(L"Software\\TortoiseGit\\DiffTools\\{}", filename1).c_str());
	if (!difftool.IsEmpty())
		return difftool;

	// Is there an extension specific diff tool?
	std::wstring ext = file2.GetFileExtension();
	tgit::wstr::MakeLower(ext);
	if (!ext.empty())
	{
		difftool = CRegString(std::format(L"Software\\TortoiseGit\\DiffTools\\{}", ext).c_str());
		if (!difftool.IsEmpty())
			return difftool;
	}

	// Finally, pick a generic external diff tool
	difftool = CRegString(L"Software\\TortoiseGit\\Diff");
	return difftool;
}

bool StartExtDiff(
	const CString& file1,
	const CString& file2,
	const CString& sName1,
	const CString& sName2,
	const CString& originalFile1,
	const CString& originalFile2,
	const CGitHash& hash1,
	const CGitHash& hash2,
	const DiffFlags& flags,
	const int jumpToLine = 0
) {
	CString viewer = PickDiffTool(CTGitPath(file1.GetString()), CTGitPath(file2.GetString()));
	// If registry entry for a diff program is commented out, fall back to git's tool.
	const bool bCommentedOut = StringUtils::StartsWith(viewer, L"#");
	if (flags.bAlternativeTool)
	{
		// Invert external vs. internal diff tool selection.
		if (bCommentedOut)
			viewer.Delete(0); // uncomment
		else
			viewer.Empty();
	}
	else if (bCommentedOut)
		viewer.Empty();

	const bool bInternal = viewer.IsEmpty();
	if (bInternal)
	{
		/*
		 * Nothing configured here, so defer to git's own diff.tool. --no-index is what
		 * makes that possible: the two sides have already been extracted to files by the
		 * time we get here, and --no-index is how git diffs two paths that are not in the
		 * index. --no-prompt stops it asking per file.
		 *
		 * The titles are lost this way, which is precisely what the configured command
		 * above buys: git hands the tool two paths and nothing else, while the TortoiseGit
		 * template can pass %bname/%yname so the tool shows "file @ HEAD" rather than a
		 * temp filename. Delegation is the floor; the template is the better experience.
		 */
		viewer = L"git.exe difftool --no-prompt --no-index -- %base %mine";
	}
	// check if the params are set. If not, just add the files to the command line
	if ((viewer.Find(L"%base") < 0) && (viewer.Find(L"%mine") < 0))
		viewer += L" %base %mine";

	const auto escaper = bInternal ? &EscapeAndQuoteParameters<wstr::CString> : &TCmdLineParser::EscapeValue<wstr::CString, wstr::CString>;

	const auto values = StringUtils::expand_vars_t{
		{ L"base", file1 },
		{ L"mine", file2 },
		{ L"bname", sName1.IsEmpty() ? file1 : sName1 },
		{ L"yname", sName2.IsEmpty() ? file2 : sName2 },
		{ L"bpath", originalFile1 },
		{ L"ypath", originalFile2 },
		{ L"brev", hash1.ToString().c_str() },
		{ L"yrev", hash2.ToString().c_str() },
		{ L"wtroot", g_Git.m_CurrentDir.c_str() },
	};
	viewer = StringUtils::ExpandPlaceholdersForCmd(viewer, values, escaper);

	if (flags.bReadOnly && bInternal)
		viewer += L" /readonly";

	if (jumpToLine > 0)
		viewer.AppendFormat(L" /line:%d", jumpToLine);

	return LaunchApplication(viewer, LaunchApplicationFlags().WaitForStartup(flags.bWait).UseSpecificErrorMessage(IDS_ERR_EXTDIFFSTART));
}

/**
 * Opens the patch in the configured patch viewer, or notepad.exe if none is set.
 */
BOOL StartExtPatch(const CTGitPath& patchfile, const CTGitPath& /*dir*/, const CString& /*sOriginalDescription*/ = CString(), const CString& /*sPatchedDescription*/ = CString(), BOOL /*bReversed*/ = FALSE, const BOOL bWait = FALSE)
{
	/*
	 * A patch file is just a file, so hand it to whatever the user configured and let that
	 * decide how to render it - Beyond Compare, for instance, has a "Text Patch" file
	 * viewer. Unlike diff and merge there is nothing in git to delegate to: git applies
	 * patches, it does not display them.
	 *
	 * notepad.exe is the floor rather than an arbitrary choice: it is always present on
	 * Windows, and users who want something better routinely redirect it through Image File
	 * Execution Options, so the fallback stays customisable without TortoiseGit knowing.
	 */
	CString viewer = CRegString(L"Software\\TortoiseGit\\PatchViewer");
	if (viewer.Trim().IsEmpty())
		viewer = L"notepad.exe %patchfile";

	const CString quotedPatchFile = std::format(L"\"{}\"", patchfile.GetWinPathString()).c_str();
	if (viewer.Find(L"%patchfile") < 0)
		viewer += L' ' + quotedPatchFile;
	else
		viewer.Replace(L"%patchfile", quotedPatchFile);

	return LaunchApplication(viewer, LaunchApplicationFlags().WaitForStartup(!!bWait).UseSpecificErrorMessage(IDS_ERR_DIFFVIEWSTART));
}

/**
 * Starts the external unified diff viewer (the app associated with *.diff or *.patch files).
 * If no app is associated with those file types, the default text editor is used.
 */
BOOL StartUnifiedDiffViewer(const CString& patchfile, const CString& title, const BOOL bWait = FALSE, const bool bAlternativeTool = false)
{
	auto viewer = static_cast<CRegString::StringT>(CRegString(L"Software\\TortoiseGit\\DiffViewer"));

	bool bInternal = false;
	// If registry entry for a diff program is commented out, fall back to git's tool.
	const bool bCommentedOut = StringUtils::StartsWith(viewer, L"#");
	if (bAlternativeTool)
	{
		// Invert external vs. internal diff tool selection.
		if (bCommentedOut)
			viewer.Delete(0); // uncomment
		else
			viewer.Empty();
	}
	else if (bCommentedOut)
		viewer.Empty();

	if (viewer.IsEmpty())
	{
		// use TortoiseGitUDiff
		viewer = PathUtils::GetAppDirectory().c_str();
		viewer += L"TortoiseGitUDiff.exe";
		// enquote the path to TortoiseGitUDiff
		viewer = L'"' + viewer + L'"';
		// add the params
		viewer = viewer + L" /patchfile:%1 /title:%title";
		if (!g_sGroupingUUID.empty())
		{
			viewer += L" /groupuuid:\"";
			viewer += g_sGroupingUUID.c_str();
			viewer += L'"';
		}
		bInternal = true;
	}

	if (viewer.Find(L"%1") == -1)
		viewer += L" %1";

	viewer = StringUtils::ExpandPlaceholdersForCmd(
		viewer, {
			{ L"1", patchfile },
			{ L"title", title },
		},
		bInternal ? TCmdLineParser::EscapeValue<CString, CString> : EscapeAndQuoteParameters<CString>
	);

	if (!LaunchApplication(viewer, LaunchApplicationFlags().WaitForStartup(!!bWait).UseSpecificErrorMessage(IDS_ERR_DIFFVIEWSTART)))
		return FALSE;
	return TRUE;
}

/**
 * Replacement for GitDiff::ShowUnifiedDiff(), but started as a separate process.
 */
bool StartShowUnifiedDiff(
	const HWND& hWnd,
	const CTGitPath& url1,
	const CString& rev1,
	const CTGitPath& url2,
	const CString& rev2,
	const bool bAlternateDiff = false,
	bool bIgnoreAncestry = false,
	bool _ = false,
	const bool bMerge = false,
	const bool bCombine = false,
	const bool bNoPrefix = false
)
{
	const int diffContext = g_Git.GetConfigValueInt32(L"diff.context", -1);
	const CString tempfile = GitCall::GetTempFile();
	if (tempfile.IsEmpty())
	{
		::MessageBox(hWnd, L"Could not create temp file.", L"TortoiseGit", MB_OK | MB_ICONERROR);
		return false;
	}
	if (g_Git.GetUnifiedDiff(url1, rev1, rev2, tempfile, bMerge, bCombine, diffContext, bNoPrefix))
	{
		MessageBox(hWnd, g_Git.GetGitLastErr(L"Could not get unified diff.", TGitH::GIT_CMD_DIFF), L"TortoiseGit", MB_OK);
		return false;
	}
	SetFileAttributes(tempfile, FILE_ATTRIBUTE_READONLY);
	StartUnifiedDiffViewer(tempfile, rev1.IsEmpty() ? rev2 : rev1 + L':' + rev2, FALSE, bAlternateDiff);

#if 0
	CString sCmd;
	sCmd.Format(L"%s /command:showcompare /unified",
		(PathUtils::GetAppDirectory() + L"TortoiseGitProc.exe").c_str());
	sCmd += L" /url1:\"" + url1.GetGitPathString().c_str() + L'"';
	if (rev1.IsValid())
		sCmd += L" /revision1:" + rev1.ToString();
	sCmd += L" /url2:\"" + url2.GetGitPathString().c_str() + L'"';
	if (rev2.IsValid())
		sCmd += L" /revision2:" + rev2.ToString();
	if (peg.IsValid())
		sCmd += L" /pegrevision:" + peg.ToString();
	if (headpeg.IsValid())
		sCmd += L" /headpegrevision:" + headpeg.ToString();

	if (bAlternateDiff)
		sCmd += L" /alternatediff";

	if (bIgnoreAncestry)
		sCmd += L" /ignoreancestry";

	if (hWnd)
	{
		sCmd += L" /hwnd:";
		wchar_t buf[30];
		swprintf_s(buf, L"%p", static_cast<void*>(hWnd));
		sCmd += buf;
	}

	return LaunchApplication(sCmd, 0, false);
#endif
	return TRUE;
}

/**
 * Sets up all the default diff and merge scripts.
 * \param force if true, overwrite all existing entries
 * \param type "Diff", "Merge" or an empty string
 */
bool SetupDiffScripts(const bool force, const CString& type)
{
	CString scriptsdir = PathUtils::GetAppParentDirectory().c_str();
	scriptsdir += L"Diff-Scripts";
	CSimpleFileFind files(scriptsdir);
	while (files.FindNextFileNoDirectories())
	{
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
		try
		{
			CStdioFile f(file, CFile::modeRead | CFile::shareDenyNone);
			CString extline;
			if (f.ReadString(extline))
			{
				if ((extline.GetLength() > 15 ) &&
					(StringUtils::StartsWith(extline, L"// extensions: ") ||
					StringUtils::StartsWith(extline, L"' extensions: ")))
				{
					if (extline[0] == '/')
						extline = extline.Mid(static_cast<int>(wcslen(L"// extensions: ")));
					else
						extline = extline.Mid(static_cast<int>(wcslen(L"' extensions: ")));
					CString sToken;
					int curPos = 0;
					sToken = extline.Tokenize(L";", curPos);
					while (!sToken.IsEmpty())
					{
						if (!sToken.IsEmpty())
						{
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

		for (const auto& extension : extensions)
		{
			if (type.IsEmpty() || (type.Compare(L"Diff") == 0))
			{
				if (StringUtils::StartsWithI(filename, L"diff-"))
				{
					auto diffreg = CRegString(L"Software\\TortoiseGit\\DiffTools\\" + extension);
					auto diffregstring = static_cast<CRegString::StringT>(diffreg);
					if (force || (diffregstring.IsEmpty()) || (diffregstring.Find(filename) >= 0))
						diffreg = L"wscript.exe \"" + file + L"\" %base %mine" + kind;
				}
			}
			if (type.IsEmpty() || (type.Compare(L"Merge") == 0))
			{
				if (StringUtils::StartsWithI(filename, L"merge-"))
				{
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

CString GetMergeTempFile(const CString& type, const CTGitPath& merge, const bool returnAbsolutePath = true)
{
	const auto path = std::format(L"{}.{}{}", merge.GetWinPathString(), type, merge.GetFileExtension());
	if (returnAbsolutePath)
		return g_Git.CombinePath(path).c_str();
	return path.c_str();
}

void RemoveTempMergeFile(const CTGitPath& path, const bool pathIsRelative = true)
{
	::DeleteFile(GetMergeTempFile(L"LOCAL", path, pathIsRelative));
	::DeleteFile(GetMergeTempFile(L"REMOTE", path, pathIsRelative));
	::DeleteFile(GetMergeTempFile(L"BASE", path, pathIsRelative));
}

/**
 * Launches the external merge program if there is one.
 * \return TRUE if the program could be started
 */
BOOL StartExtMerge(
	const bool bAlternative,
	const CTGitPath& basefile,
	const CTGitPath& theirfile,
	const CTGitPath& yourfile,
	const CTGitPath& mergedfile,
	const CString& basename = {},
	const CString& theirname = {},
	const CString& yourname = {},
	const CString& mergedname = {},
	const HWND& resolveMsgHwnd = {},
	const bool bDeleteBaseTheirsMineOnClose = {}
) {
	CString ext = mergedfile.GetFileExtension().c_str();
	CString com = CRegString(L"Software\\TortoiseGit\\Merge");

	if (!ext.IsEmpty())
	{
		// is there an extension specific merge tool?
		const CString mergetool = CRegString(L"Software\\TortoiseGit\\MergeTools\\" + ext.MakeLower());
		if (!mergetool.IsEmpty())
			com = mergetool;
	}
	// is there a filename specific merge tool?
	std::wstring mergedFilename = mergedfile.GetFilename();
	tgit::wstr::MakeLower(mergedFilename);
	CRegString mergetool(std::format(L"Software\\TortoiseGit\\MergeTools\\.{}", mergedFilename).c_str());
	if (!static_cast<CRegString::StringT>(mergetool).IsEmpty())
		com = mergetool;

	if (bAlternative && !com.IsEmpty())
	{
		if (StringUtils::StartsWith(com, L"#"))
			com.Delete(0);
		else
			com.Empty();
	}

	if (com.IsEmpty() || StringUtils::StartsWith(com, L"#"))
	{
		{
			/*
			 * Fall back to git's merge.tool. This is translated rather than delegated:
			 * git mergetool only operates on paths that are conflicted in the index, while
			 * this function is also used to merge arbitrary files, so the tool has to be
			 * resolved and launched here.
			 *
			 * mergetool.<tool>.cmd is a shell template over $BASE/$LOCAL/$REMOTE/$MERGED,
			 * mapped onto TortoiseGit's placeholders below. That covers the usual
			 * "program plus arguments" template but not one relying on real shell syntax.
			 */
			CString mergeCmd;
			if (const CString mergeTool = g_Git.GetConfigValue(L"merge.tool"); !mergeTool.IsEmpty())
				mergeCmd = g_Git.GetConfigValue(L"mergetool." + mergeTool + L".cmd");

			if (!mergeCmd.IsEmpty())
			{
				mergeCmd.Replace(L"$BASE", L"%base");
				mergeCmd.Replace(L"$LOCAL", L"%mine");
				mergeCmd.Replace(L"$REMOTE", L"%theirs");
				mergeCmd.Replace(L"$MERGED", L"%merged");
				com = mergeCmd;
			}
			else
			{
				// Nothing configured anywhere. The merged file already carries conflict
				// markers, so editing it by hand is a complete resolution path; notepad.exe
				// is always present and is commonly redirected via Image File Execution
				// Options, so this stays useful rather than being a dead end.
				com = L"notepad.exe %merged";
			}
		}
		if (!g_sGroupingUUID.empty())
		{
			com += L" /groupuuid:\"";
			com += g_sGroupingUUID.c_str();
			com += L'"';
		}
	}
	// check if the params are set. If not, just add the files to the command line
	if ((com.Find(L"%merged") < 0) && (com.Find(L"%base") < 0) && (com.Find(L"%theirs") < 0) && (com.Find(L"%mine") < 0))
		com += L" %base %theirs %mine %merged";

	com = StringUtils::ExpandPlaceholdersForCmd(
		com,
		{
			{ L"base", basefile.GetWinPathString().c_str() },
			{ L"theirs", theirfile.GetWinPathString().c_str() },
			{ L"mine", yourfile.GetWinPathString().c_str() },
			{ L"merged", mergedfile.GetWinPathString().c_str() },
			{ L"bname", basename.IsEmpty() ? CString(basefile.GetUIFileOrDirectoryName().c_str()) : basename },
			{ L"tname", theirname.IsEmpty() ? CString(theirfile.GetUIFileOrDirectoryName().c_str()) : theirname },
			{ L"yname", yourname.IsEmpty() ? CString(yourfile.GetUIFileOrDirectoryName().c_str()) : yourname },
			{ L"mname", mergedname.IsEmpty() ? CString(mergedfile.GetUIFileOrDirectoryName().c_str()) : mergedname },
			{ L"wtroot", g_Git.m_CurrentDir.c_str() },
		},
		&EscapeAndQuoteParameters<CString>
	);

	/*
	 * Every merge tool is external now, so there is no in-process viewer to hand a /readonly
	 * switch to - and none of them honours /deletebasetheirsmineonclose either. The conflict
	 * path hands us three temp files it expects to be cleaned up once the tool is finished
	 * with them, so when it asks for that we have to wait and delete them ourselves; the files
	 * are still open while the tool runs.
	 */
	const auto blocktrust = wil::reg::try_get_tgit_dword(L"Software\\TortoiseGit\\MergeBlockTrustBehavior").value_or(0);
	const bool bWaitForExit = bDeleteBaseTheirsMineOnClose || blocktrust >= 1;
	auto exitCode = std::numeric_limits<DWORD>::max();
	const bool bLaunched = LaunchApplication(com, LaunchApplicationFlags().UseSpecificErrorMessage(IDS_ERR_EXTMERGESTART).WaitForExit(bWaitForExit, nullptr, &exitCode));

	if (bDeleteBaseTheirsMineOnClose)
	{
		::DeleteFile(basefile.GetWinPathString().c_str());
		::DeleteFile(theirfile.GetWinPathString().c_str());
		::DeleteFile(yourfile.GetWinPathString().c_str());
	}

	if (!bLaunched)
		return FALSE;

	if (!bWaitForExit)
		return TRUE;

	RemoveTempMergeFile(mergedfile, false);

	CString str;
	str.Format(IDS_MERGESUCCESSFUL, mergedfile.GetGitPathString().c_str());
	if ((blocktrust == 2 && exitCode == 0) || (blocktrust == 1 && CMessageBox::Show(GetExplorerHWND(), str, IDS_APPNAME, 2, IDI_QUESTION, IDS_RESOLVEDBUTTON, IDS_MSGBOX_NO) == 1))
	{
		std::string out;
		if (g_Git.Run({ L"git.exe", L"add", L"-f", L"--", mergedfile.GetGitPathString() }, &out))
		{
			MessageBox(GetExplorerHWND(), CUnicodeUtils::StdGetUnicode(out).c_str(), L"TortoiseGit", MB_OK | MB_ICONERROR);
			return FALSE;
		}
	}
	else
		return TRUE;

	if (resolveMsgHwnd && CRegDWORD(L"Software\\TortoiseGit\\RefreshFileListAfterResolvingConflict", TRUE) == TRUE)
	{
		static UINT WM_REVERTMSG = RegisterWindowMessage(L"GITSLNM_NEEDSREFRESH");
		::PostMessage(resolveMsgHwnd, WM_REVERTMSG, NULL, NULL);
	}

	return TRUE;
}

}
