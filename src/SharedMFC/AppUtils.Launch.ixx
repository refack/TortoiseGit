module;
#include "stdafx.h"

#include "Resources/resource.h"


export module AppUtils.Launch;
import tgit;
import TortoiseProc;
import PathUtils;
import CmdLineParser;
import GitRev;
import TGitPath;
import Registry;
import GitHash;
import TGitMessageBox;

/**
 * \ingroup TortoiseProc
 * Starting other processes: TortoiseGitProc itself, the configured diff, merge and
 * patch tools, text editors, and the shell. Everything here ends in CreateProcess or
 * ShellExecute; nothing opens a TortoiseGit dialog of its own.
 */
export namespace CAppUtils {


/**
* Flags for StartExtDiff function.
*/
struct DiffFlags {
	bool bWait{};
	bool bReadOnly{};
	bool bAlternativeTool{}; // If true, invert selection of git's tool vs. the configured external diff tool

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
* Launch the external blame viewer
*/
bool LaunchTortoiseBlame(const CString& sBlameFile, const CString& Rev, const CString& sParams = CString()) {
	auto args = std::vector{
		std::format(LR"("{}")", (til::GetAppDirectory() / L"TortoiseGitBlame.exe").native()),
		std::format(L"/path:{}", str::EscapeCmdValue(sBlameFile)),
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


CString PickDiffTool(const CTGitPath& file1, const CTGitPath& file2) {
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
	if (!ext.empty()) {
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
	if (flags.bAlternativeTool) {
		// Invert external vs. internal diff tool selection.
		if (bCommentedOut)
			viewer.Delete(0); // uncomment
		else
			viewer.Empty();
	} else if (bCommentedOut) {
		viewer.Empty();
	}

	const bool bInternal = viewer.IsEmpty();
	if (bInternal) {
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

	const auto values = StringUtils::expand_vars_t{
		{ L"base", file1 },
		{ L"mine", file2 },
		{ L"bname", sName1.IsEmpty() ? file1 : sName1 },
		{ L"yname", sName2.IsEmpty() ? file2 : sName2 },
		{ L"bpath", originalFile1 },
		{ L"ypath", originalFile2 },
		{ L"brev", hash1.ToString().c_str() },
		{ L"yrev", hash2.ToString().c_str() },
		{ L"wtroot", GitCall::m_CurrentDirStorage.c_str() },
	};
	viewer = StringUtils::ExpandPlaceholdersForCmd(viewer, values, str::escaperoid{ bInternal });

	if (flags.bReadOnly && bInternal)
		viewer += L" /readonly";

	if (jumpToLine > 0)
		viewer.AppendFormat(L" /line:%d", jumpToLine);

	return LaunchApplication(viewer, LaunchApplicationFlags().WaitForStartup(flags.bWait).UseSpecificErrorMessage(IDS_ERR_EXTDIFFSTART));
}


/**
 * Opens the patch in the configured patch viewer, or notepad.exe if none is set.
 */
BOOL StartExtPatch(const CTGitPath& patchfile,
                   const CTGitPath& /*dir*/,
                   const CString& /*sOriginalDescription*/ = CString(),
                   const CString& /*sPatchedDescription*/ = CString(),
                   BOOL /*bReversed*/  = FALSE,
                   const BOOL bWait = FALSE) {
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
BOOL StartUnifiedDiffViewer(const CString& patchfile, const CString& title, const BOOL bWait = FALSE, const bool bAlternativeTool = false) {
	auto viewer = static_cast<CRegString::StringT>(CRegString(L"Software\\TortoiseGit\\DiffViewer"));

	bool bInternal = false;
	// If registry entry for a diff program is commented out, fall back to git's tool.
	const bool bCommentedOut = StringUtils::StartsWith(viewer, L"#");
	if (bAlternativeTool) {
		// Invert external vs. internal diff tool selection.
		if (bCommentedOut)
			viewer.Delete(0); // uncomment
		else
			viewer.Empty();
	} else if (bCommentedOut) {
		viewer.Empty();
	}

	if (viewer.IsEmpty()) {
		// use TortoiseGitUDiff
		viewer = til::GetAppDirectory().c_str();
		viewer += L"TortoiseGitUDiff.exe";
		// enquote the path to TortoiseGitUDiff
		viewer = L'"' + viewer + L'"';
		// add the params
		viewer = viewer + L" /patchfile:%1 /title:%title";
		if (!g_sGroupingUUID.empty()) {
			viewer += L" /groupuuid:\"";
			viewer += g_sGroupingUUID.c_str();
			viewer += L'"';
		}
		bInternal = true;
	}

	if (viewer.Find(L"%1") == -1)
		viewer += L" %1";

	viewer = StringUtils::ExpandPlaceholdersForCmd(
		viewer,
		{
			{ L"1", patchfile },
			{ L"title", title },
		},
		str::escaperoid{ bInternal }
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
) {
	const int diffContext = g_Git.GetConfigValueInt32(L"diff.context", -1);
	const CString tempfile = GitCall::GetTempFile();
	if (tempfile.IsEmpty()) {
		::MessageBox(hWnd, L"Could not create temp file.", L"TortoiseGit", MB_OK | MB_ICONERROR);
		return false;
	}
	if (g_Git.GetUnifiedDiff(url1, rev1, rev2, tempfile, bMerge, bCombine, diffContext, bNoPrefix)) {
		MessageBox(hWnd, g_Git.GetGitLastErr(L"Could not get unified diff.", TGitH::GIT_CMD_DIFF), L"TortoiseGit", MB_OK);
		return false;
	}
	SetFileAttributes(tempfile, FILE_ATTRIBUTE_READONLY);
	StartUnifiedDiffViewer(tempfile, rev1.IsEmpty() ? rev2 : rev1 + L':' + rev2, FALSE, bAlternateDiff);

#if 0
	CString sCmd;
	sCmd.Format(L"%s /command:showcompare /unified",
	            (til::GetAppDirectory() + L"TortoiseGitProc.exe").c_str());
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

	if (hWnd) {
		sCmd += L" /hwnd:";
		wchar_t buf[30];
		swprintf_s(buf, L"%p", static_cast<void*>(hWnd));
		sCmd += buf;
	}

	return LaunchApplication(sCmd, 0, false);
#endif

	return TRUE;
}


CString GetMergeTempFile(const CString& type, const CTGitPath& merge, const bool returnAbsolutePath = true) {
	const auto path = std::format(L"{}.{}{}", merge.GetWinPathString(), type, merge.GetFileExtension());
	if (returnAbsolutePath)
		return g_Git.CombinePath(path).c_str();
	return path.c_str();
}


void RemoveTempMergeFile(const CTGitPath& path, const bool pathIsRelative = true) {
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

	if (!ext.IsEmpty()) {
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

	if (bAlternative && !com.IsEmpty()) {
		if (StringUtils::StartsWith(com, L"#"))
			com.Delete(0);
		else
			com.Empty();
	}

	if (com.IsEmpty() || StringUtils::StartsWith(com, L"#")) {
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

			if (!mergeCmd.IsEmpty()) {
				mergeCmd.Replace(L"$BASE", L"%base");
				mergeCmd.Replace(L"$LOCAL", L"%mine");
				mergeCmd.Replace(L"$REMOTE", L"%theirs");
				mergeCmd.Replace(L"$MERGED", L"%merged");
				com = mergeCmd;
			} else {
				// Nothing configured anywhere. The merged file already carries conflict
				// markers, so editing it by hand is a complete resolution path; notepad.exe
				// is always present and is commonly redirected via Image File Execution
				// Options, so this stays useful rather than being a dead end.
				com = L"notepad.exe %merged";
			}
		}
		if (!g_sGroupingUUID.empty()) {
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
			{ L"wtroot", GitCall::m_CurrentDirStorage.c_str() },
		},
		&str::EscapeAndQuoteParameters<CString>
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
	const bool bLaunched = LaunchApplication(com,
	                                         LaunchApplicationFlags().UseSpecificErrorMessage(IDS_ERR_EXTMERGESTART).WaitForExit(
		                                         bWaitForExit,
		                                         nullptr,
		                                         &exitCode));

	if (bDeleteBaseTheirsMineOnClose) {
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
	if (
		(blocktrust == 2 && exitCode == 0)
		|| (
			blocktrust == 1
			&& 1 == CMessageBox::Show(
				GetExplorerHWND(),
				str,
				IDS_APPNAME,
				2,
				IDI_QUESTION,
				IDS_RESOLVEDBUTTON,
				IDS_MSGBOX_NO
			)
		)
	) {
		std::string out;
		if (g_Git.Run({ L"git.exe", L"add", L"-f", L"--", mergedfile.GetGitPathString() }, &out)) {
			MessageBox(GetExplorerHWND(), CUnicodeUtils::StdGetUnicode(out).c_str(), L"TortoiseGit", MB_OK | MB_ICONERROR);
			return FALSE;
		}
	} else {
		return TRUE;
	}

	if (resolveMsgHwnd && wil::reg::try_get_tgit_bool(L"RefreshFileListAfterResolvingConflict").value_or(true)) {
		static UINT WM_REVERTMSG = RegisterWindowMessage(L"GITSLNM_NEEDSREFRESH");
		::PostMessage(resolveMsgHwnd, WM_REVERTMSG, NULL, NULL);
	}

	return TRUE;
}

}
