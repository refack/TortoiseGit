module;
#include <tgitlib/sanity.h>

#include "Resources/resource.h"



export module ConfigureGitExe;
import std;
import gsl;
import wil;
import str_utils;
import git_h;
import MessageBox;
import TGitCache.CacheInterface;
import PathUtils;
import Registry;


export namespace ConfigureGitExe {


class CSelectFileFilter {
public:
	CSelectFileFilter(UINT stringId) { Load(stringId); }
	CSelectFileFilter() {}
	~CSelectFileFilter() {}

	operator const wchar_t*() { return buffer.get(); }


	void Load(UINT stringId) {
		ResetAll();
		CString sFilter;
		sFilter.LoadString(stringId);
		// the filter with its '|' turned into NULs, plus the double-NUL terminator
		// the common dialogs want (make_unique<T[]> value-initializes, so the tail is zero)
		const size_t length = static_cast<size_t>(sFilter.GetLength());
		buffer = std::make_unique<wchar_t[]>(length + 4);
		std::replace_copy(sFilter.GetString(), sFilter.GetString() + length, buffer.get(), L'|', L'\0');

		int pos = 0;
		CString temp;
		for (;;) {
			temp = sFilter.Tokenize(L"|", pos);
			if (temp.IsEmpty()) {
				break;
			}
			filternames.push_back(temp);
			temp = sFilter.Tokenize(L"|", pos);
			filtermasks.push_back(temp);
		}
		filterspec.reset(new COMDLG_FILTERSPEC[filternames.size()]);
		for (size_t i = 0; i < filternames.size(); ++i) {
			filterspec[i].pszName = filternames[i];
			filterspec[i].pszSpec = filtermasks[i];
		}
	}


	UINT GetCount() const { return static_cast<UINT>(filternames.size()); }
	operator const COMDLG_FILTERSPEC*() { return filterspec.get(); }

private:
	std::unique_ptr<wchar_t[]> buffer;
	std::unique_ptr<COMDLG_FILTERSPEC[]> filterspec;
	std::vector<CString> filternames;
	std::vector<CString> filtermasks;


	void ResetAll() {
		buffer.reset();
		// First release the struct that references the vectors, then clear the vectors
		filterspec.reset();
		filternames.clear();
		filtermasks.clear();
	}
};



constexpr auto GIT_FOR_WINDOWS_URL = L"https://gitforwindows.org/"sv;


bool FileOpenSave(
	CString& path,
	int* filterindex,
	const UINT title,
	const UINT filterId,
	const bool bOpen,
	const std::optional<HWND> hwndOwner = {},
	const LPCWSTR defaultExt = nullptr,
	const bool handleAsFile = false
) {
	// Create a new common save file dialog
	CComPtr<IFileDialog> pfd;

	if (!SUCCEEDED(pfd.CoCreateInstance(bOpen ? CLSID_FileOpenDialog : CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER)))
		return false;

	// Set the dialog options
	DWORD dwOptions;
	if (!SUCCEEDED(pfd->GetOptions(&dwOptions)))
		return false;

	if (bOpen) {
		if (!SUCCEEDED(pfd->SetOptions(dwOptions | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST)))
			return false;
	} else {
		if (!SUCCEEDED(pfd->SetOptions(dwOptions | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST)))
			return false;
	}
	const auto psz_name = fs::path{ path.GetString() }.filename().c_str();
	if ((!PathIsDirectory(path) || handleAsFile) && !SUCCEEDED(pfd->SetFileName(psz_name)))
		return false;

	// Set a title
	if (title) {
		CString temp;
		temp.LoadString(title);
		StringUtils::RemoveAccelerators(temp);
		std::ignore = pfd->SetTitle(temp);
	}
	CComPtr<IShellItem> psiDefaultFolder;
	if (filterId) {
		CSelectFileFilter fileFilter(filterId);
		if (!SUCCEEDED(pfd->SetFileTypes(fileFilter.GetCount(), fileFilter)))
			return false;
		if (filterId == 1602 || filterId == 2501) // IDS_GITEXEFILEFILTER || IDS_PROGRAMSFILEFILTER
		{
			std::ignore = pfd->SetClientGuid({ .Data1 = 0x323ca4b0, .Data2 = 0x62df, .Data3 = 0x4a08,
			                                   .Data4 = { 0xa5, 0x5, 0x58, 0xde, 0xa2, 0xb9, 0x2d, 0xcd } });
			if (SUCCEEDED(SHCreateItemFromParsingName(PathUtils::GetProgramsDirectory().c_str(), nullptr, IID_PPV_ARGS(&psiDefaultFolder))))
				std::ignore = pfd->SetDefaultFolder(psiDefaultFolder);
		}
	}

	if (defaultExt && !SUCCEEDED(pfd->SetDefaultExtension(defaultExt)))
		return false;

	// set the default folder
	CComPtr<IShellItem> psiFolder;
	if (StringUtils::StartsWith(path, L"\\") || path.Mid(1, 2) == L":\\") {
		CString dir = path;
		if (!PathIsDirectory(dir) || handleAsFile) {
			if (PathRemoveFileSpec(dir.GetBuffer()))
				dir.ReleaseBuffer();
			else
				dir.Empty();
		}
		if (!dir.IsEmpty() && SUCCEEDED(SHCreateItemFromParsingName(dir, nullptr, IID_PPV_ARGS(&psiFolder))))
			std::ignore = pfd->SetFolder(psiFolder);
	}

	// Show the save/open file dialog
	if (!SUCCEEDED(pfd->Show(hwndOwner.value_or(nullptr))))
		return false;

	// Get the selection from the user
	CComPtr<IShellItem> psiResult;
	if (!SUCCEEDED(pfd->GetResult(&psiResult)))
		return false;

	CComHeapPtr<WCHAR> pszPath;
	if (!SUCCEEDED(psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
		return false;

	path = CString(pszPath);
	if (filterindex) {
		UINT fi = 0;
		std::ignore = pfd->GetFileTypeIndex(&fi);
		*filterindex = gsl::narrow<int>(fi);
	}
	return true;
}



class CConfigureGitExe {
public:
	CConfigureGitExe() :
		m_regCygwinHack(L"Software\\TortoiseGit\\CygwinHack", FALSE)
		, m_regMsys2Hack(L"Software\\TortoiseGit\\Msys2Hack", FALSE) {};


	static bool CheckGitVersion(HWND hwnd) {
		if (CAppUtils::IsGitVersionNewerOrEqual(hwnd, 2, 47))
			return true;

		CString tmp;
		tmp.Format(IDS_PROC_OLDMSYSGIT, L"2.47+");
		const auto ret = CMessageBox::ShowCheck(hwnd,
		                                        tmp,
		                                        IDS_APPNAME,
		                                        1,
		                                        IDI_EXCLAMATION,
		                                        IDS_PROC_GOTOMSYSGITWEBSITE,
		                                        IDS_ABORTBUTTON,
		                                        IDS_IGNOREBUTTON,
		                                        L"OldMsysgitVersionWarning",
		                                        IDS_PROC_NOTSHOWAGAINIGNORE);
		if (ret == 3)
			return true;

		CMessageBox::RemoveRegistryKey(L"OldMsysgitVersionWarning"); // only store answer if it is "Ignore"
		if (ret == 1)
			ShellExecute(hwnd, L"open", GIT_FOR_WINDOWS_URL.data(), nullptr, nullptr, SW_SHOW);

		return false;
	}

protected:
	CRegDWORD m_regCygwinHack;
	CRegDWORD m_regMsys2Hack;
	CRegString m_regMsysGitPath;
	CRegString m_regMsysGitExtranPath;


	static void PerformCommonGitPathCleanup(CString& path) {
		path.Trim(L"\"'");

		if (path.Find(L'%') >= 0) {
			int neededSize = ExpandEnvironmentStrings(path, nullptr, 0);
			CString origPath(path);
			ExpandEnvironmentStrings(origPath, CStrBuf(path, neededSize, CStrBuf::SET_LENGTH), neededSize);
		}

		path.Replace(L'/', L'\\');
		path.Replace(L"\\\\", L"\\");

		if (StringUtils::EndsWith(path, L"git.exe"))
			path.Truncate(path.GetLength() - 7);

		path.TrimRight(L'\\');

		// prefer git.exe in cmd-directory for Git for Windows based on msys2
		if (path.GetLength() > 12 && (StringUtils::EndsWith(path, L"\\mingw32\\bin") || StringUtils::EndsWith(path, L"\\mingw64\\bin")) && PathFileExists(
			path.Left(path.GetLength() - 12) + L"\\cmd\\git.exe"))
			path = path.Left(path.GetLength() - 12) + L"\\cmd";

		// prefer git.exe in bin-directory, see https://github.com/msysgit/msysgit/issues/103
		if (path.GetLength() > 5 && StringUtils::EndsWith(path, L"\\cmd") && PathFileExists(path.Left(path.GetLength() - 4) + L"\\bin\\git.exe"))
			path = path.Left(path.GetLength() - 4) + L"\\bin";
	}


	static bool GuessExtraPath(CString gitpath, CString& pathaddition) {
		if (gitpath.IsEmpty())
			return false;

		PerformCommonGitPathCleanup(gitpath);

		if (!StringUtils::EndsWith(gitpath, L"bin"))
			return false;

		gitpath.Truncate(gitpath.GetLength() - 3);
		gitpath += L"mingw\\bin";
		if (!::PathFileExists(gitpath))
			return false;

		if (pathaddition.Find(gitpath + L';') >= 0 || StringUtils::EndsWith(pathaddition, gitpath))
			return false;

		pathaddition = gitpath + pathaddition;
		return true;
	}


	static bool SelectFolder(HWND hwnd, CString& gitpath, CString& pathaddition) {
		CString dir;
		dir = gitpath;
		if (!dir.IsEmpty())
			dir += L"\\git.exe";
		if (!CAppUtils::FileOpenSave(dir, nullptr, 0, IDS_GITEXEFILEFILTER, true, hwnd))
			return false;

		gitpath = dir;
		PerformCommonGitPathCleanup(gitpath);
		if (!PathFileExists(gitpath + L"\\git.exe"))
			CMessageBox::Show(hwnd, IDS_PROC_NOMSYSGIT, IDS_APPNAME, MB_ICONEXCLAMATION);
		GuessExtraPath(gitpath, pathaddition);
		return true;
	}


	bool CheckGitExe(HWND hwnd,
	                 CString& gitpath,
	                 CString& pathaddition,
	                 int versionLabelId,
	                 std::function<void(UINT)> callHelp,
	                 bool* needWorkarounds = nullptr) {
		SetWindowText(GetDlgItem(hwnd, versionLabelId), L"");

		PerformCommonGitPathCleanup(gitpath);

		GuessExtraPath(gitpath, pathaddition);

		if (gitpath.IsEmpty() || !PathFileExists(gitpath + L"\\git.exe")) {
			CMessageBox::Show(hwnd, IDS_PROC_NOMSYSGIT, IDS_APPNAME, MB_ICONERROR);
			return false;
		}

		CString oldpath = m_regMsysGitPath;
		CString oldextranpath = m_regMsysGitExtranPath;

		StoreSetting(hwnd, gitpath, m_regMsysGitPath);
		StoreSetting(hwnd, pathaddition, m_regMsysGitExtranPath);
		bool undoOnError = true;
		SCOPE_EXIT {
			if (!undoOnError)
				return;
			StoreSetting(hwnd, oldpath, m_regMsysGitPath);
			StoreSetting(hwnd, oldextranpath, m_regMsysGitExtranPath);
		};

		CString checkhelpHint;
		checkhelpHint.LoadString(IDS_SEEMANUALGITEXEPATH);

		g_Git.m_bInitialized = false;

		if (g_Git.CheckMsysGitDir(FALSE)) {
			CString out;
			int ret = GitCall::Run({ L"git.exe", L"--version" }, &out, CP_UTF8); // stderr merged into out, as upstream's Run(cmd, &out, code)
			SetWindowText(GetDlgItem(hwnd, versionLabelId), out);
			if (out.IsEmpty()) {
				if (ret == 0xC0000135) {
					if (CMessageBox::Show(hwnd, IDS_ERR_GITDLLMISSING, IDS_APPNAME, 1, IDI_ERROR, IDS_MSGBOX_OK, IDS_MSGBOX_HELP) == 2)
						callHelp(IDD_SETTINGSMAIN);
					return false;
				}
				CString tmp;
				tmp.Format(IDS_ERR_GITCALLFAILED, ret);
				tmp.AppendChar(L'\n');
				tmp.AppendChar(L'\n');
				tmp.Append(checkhelpHint);
				if (CMessageBox::Show(hwnd, tmp, IDS_APPNAME, 1, IDI_ERROR, IDS_MSGBOX_OK, IDS_MSGBOX_HELP) == 2)
					callHelp(IDD_SETTINGSMAIN);
				return false;
			} else if (!StringUtils::StartsWith(out.Trim(), L"git version ")) {
				CString tmp;
				tmp.Format(IDS_ERR_GITNOVALIDOUTPUT, static_cast<LPCWSTR>(out.Trim()));
				tmp.AppendChar(L'\n');
				tmp.AppendChar(L'\n');
				tmp.Append(checkhelpHint);
				if (CMessageBox::Show(hwnd, tmp, IDS_APPNAME, 1, IDI_ERROR, IDS_MSGBOX_OK, IDS_MSGBOX_HELP) == 2)
					callHelp(IDD_SETTINGSMAIN);
				return false;
			} else if (!(GitCall::ms_bCygwinGit || GitCall::ms_bMsys2Git) && out.Find(L"msysgit") == -1 && out.Find(L"windows") == -1) {
				bool wasAlreadyWarned = !needWorkarounds || *needWorkarounds;
				if (CMessageBox::Show(hwnd, IDS_ERR_GITNEEDHACKS, IDS_APPNAME, 1, IDI_INFORMATION, IDS_MSGBOX_OK, IDS_MSGBOX_HELP) == 2)
					callHelp(IDD_SETTINGSMAIN);
				if (needWorkarounds)
					*needWorkarounds = true;
				if (!wasAlreadyWarned)
					return false;
			} else if ((GitCall::ms_bCygwinGit || GitCall::ms_bMsys2Git) && out.Find(L"msysgit") > 0 && out.Find(L"windows") > 0) {
				if (CMessageBox::Show(hwnd, IDS_ERR_GITUNNEEDEDHACKS, IDS_APPNAME, 1, IDI_INFORMATION, IDS_MSGBOX_OK, IDS_MSGBOX_HELP) == 2)
					callHelp(IDD_SETTINGSMAIN);
				if (needWorkarounds)
					*needWorkarounds = true;
				return false;
			}
		} else {
			CString tmp;
			tmp.LoadString(IDS_PROC_NOMSYSGIT);
			tmp.AppendChar(L'\n');
			tmp.AppendChar(L'\n');
			tmp.Append(checkhelpHint);
			if (CMessageBox::Show(hwnd, tmp, IDS_APPNAME, 1, IDI_ERROR, IDS_MSGBOX_OK, IDS_MSGBOX_HELP) == 2)
				callHelp(IDD_SETTINGSMAIN);
			return false;
		}

		// force recalculation of Git version
		TGitH::ms_LastMsysGitVersion = 0;
		CRegDWORD(L"Software\\TortoiseGit\\git_file_time").removeValue();
		if (!CheckGitVersion(hwnd))
			return false;

		// tell the cache to refresh everything and restart
		SendCacheCommand(TGITCACHECOMMAND_REFRESHALL);
		SendCacheCommand(TGITCACHECOMMAND_END);

		undoOnError = false;
		return true;
	}


	template <class T, class Reg>
	void StoreSetting(HWND hwnd, const T& value, Reg& registryKey) {
		registryKey = value;
		if (registryKey.GetLastError() != ERROR_SUCCESS)
			CMessageBox::Show(hwnd, registryKey.getErrorString(), L"TortoiseGit", MB_ICONERROR);
	}
};


}
