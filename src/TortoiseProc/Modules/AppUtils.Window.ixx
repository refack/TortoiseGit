module;
#include "stdafx.h"
#include "std.h"

#include <shlobj_core.h>
#include <Utils/SelectFileFilter.h>
#include <Utils/DPIAware.h>
#include <Utils/CreateProcessHelper.h>
#include "Utils/URLFinder.h"



export module AppUtils.Window;
import wil.MFC;
import BrowseFolder;
import ThemeAPI;
import IconBitmapUtils;
import TortoiseProc;

/**
 * \ingroup TortoiseProc
 * What a dialog does to itself: its title, taskbar pinning, log fonts, icons and list
 * backgrounds, the common file dialog, F1 help, and styling the text of a rich edit
 * control. Nothing here runs git.
 */
export namespace CAppUtils {

using wil::MFC::LoadIconEx;


bool SetListCtrlBackgroundImage(const HWND& hListCtrl, const UINT nID, const int width, const int height)
{
	if (static_cast<DWORD>(CRegStdDWORD(L"Software\\TortoiseGit\\ShowListBackgroundImage", TRUE)) == FALSE)
		return false;
	// create a bitmap from the icon
	const CAutoIcon hIcon = LoadIconEx(AfxGetResourceHandle(), MAKEINTRESOURCE(nID), width, height);
	if (!hIcon)
		return false;

	LVBKIMAGE lv;
	lv.ulFlags = LVBKIF_TYPE_WATERMARK | LVBKIF_FLAG_ALPHABLEND;
	lv.hbm = IconBitmapUtils::IconToBitmapPARGB32(hIcon, width, height);
	lv.xOffsetPercent = 100;
	lv.yOffsetPercent = 100;
	ListView_SetBkImage(hListCtrl, &lv);
	return true;
}

bool SetListCtrlBackgroundImage(const HWND& hListCtrl, const UINT nID)
{
	return SetListCtrlBackgroundImage(hListCtrl, nID, CDPIAware::Instance().ScaleX(hListCtrl, 128), CDPIAware::Instance().ScaleY(hListCtrl, 128));
}

bool FileOpenSave(
	CString& path, int* filterindex, const UINT title, const UINT filterId, const bool bOpen, const std::optional<HWND> hwndOwner = {}, const LPCWSTR defaultExt = nullptr, const bool handleAsFile = false
)
{
	// Create a new common save file dialog
	CComPtr<IFileDialog> pfd;

	if (!SUCCEEDED(pfd.CoCreateInstance(bOpen ? CLSID_FileOpenDialog : CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER)))
		return false;

	// Set the dialog options
	DWORD dwOptions;
	if (!SUCCEEDED(pfd->GetOptions(&dwOptions)))
		return false;

	if (bOpen)
	{
		if (!SUCCEEDED(pfd->SetOptions(dwOptions | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST)))
			return false;
	}
	else
	{
		if (!SUCCEEDED(pfd->SetOptions(dwOptions | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST)))
			return false;
	}
	const auto psz_name = fs::path{path.GetString()}.filename().c_str();
	if ((!PathIsDirectory(path) || handleAsFile) && !SUCCEEDED(pfd->SetFileName(psz_name)))
		return false;

	// Set a title
	if (title)
	{
		CString temp;
		temp.LoadString(title);
		StringUtils::RemoveAccelerators(temp);
		std::ignore = pfd->SetTitle(temp);
	}
	CComPtr<IShellItem> psiDefaultFolder;
	if (filterId)
	{
		CSelectFileFilter fileFilter(filterId);
		if (!SUCCEEDED(pfd->SetFileTypes(fileFilter.GetCount(), fileFilter)))
			return false;
		if (filterId == 1602 || filterId == 2501) // IDS_GITEXEFILEFILTER || IDS_PROGRAMSFILEFILTER
		{
			std::ignore = pfd->SetClientGuid({ .Data1 = 0x323ca4b0, .Data2 = 0x62df, .Data3 = 0x4a08, .Data4 = { 0xa5, 0x5, 0x58, 0xde, 0xa2, 0xb9, 0x2d, 0xcd } });
			if (SUCCEEDED(SHCreateItemFromParsingName(PathUtils::GetProgramsDirectory().c_str(), nullptr, IID_PPV_ARGS(&psiDefaultFolder))))
				std::ignore = pfd->SetDefaultFolder(psiDefaultFolder);
		}
	}

	if (defaultExt && !SUCCEEDED(pfd->SetDefaultExtension(defaultExt)))
		return false;

	// set the default folder
	CComPtr<IShellItem> psiFolder;
	if (StringUtils::StartsWith(path, L"\\") || path.Mid(1, 2) == L":\\")
	{
		CString dir = path;
		if (!PathIsDirectory(dir) || handleAsFile)
		{
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
	if (filterindex)
	{
		UINT fi = 0;
		std::ignore = pfd->GetFileTypeIndex(&fi);
		*filterindex = gsl::narrow<int>(fi);
	}
	return true;
}




bool StartHtmlHelp(const DWORD_PTR id, CString page = L"index.html")
{
	ATLASSERT(page == L"index.html" || id == 0);

#if !defined(IDR_HELPMAPPING) || !defined(IDS_APPNAME)
	UNREFERENCED_PARAMETER(id);
	UNREFERENCED_PARAMETER(page);
	return false;
#else
	static std::map<DWORD_PTR, std::wstring> idMap;

	if (idMap.empty())
	{
		DWORD resSize = 0;
		const char* resData = wil::MFC::GetResourceData(L"help", IDR_HELPMAPPING, resSize);
		if (resData)
		{
			const auto resString = std::string_view(resData, resSize);
			const auto lines = str::stringtok(resString, true, "\r\n");
			for (const auto& line : lines)
			{
				if (line.empty())
					continue;
				auto lineParts = str::stringtok(line, true, "=");
				if (lineParts.size() == 2) {
					const auto stoi = std::stoi(std::string{lineParts[0]});
					idMap[stoi] = CUnicodeUtils::StdGetUnicode(lineParts[1]);
				}
			}
		}
	}

	if (idMap.contains(id))
	{
		page = idMap[id].c_str();
		page.Replace(L"@", L"%40");
	}

	CString appName(MAKEINTRESOURCE(IDS_APPNAME));
	// std::format rather than a concatenation chain: this mixes std::wstring,
	// CString and a literal, and getting one of those conversions wrong is the
	// silent kind of wrong (see WideString.h's formatter).
	if (CString appHelp = std::format(L"{}{}_en\\", PathUtils::GetAppDirectory(), appName).c_str(); PathFileExists(appHelp))
	{
		// We have to find the default browser, ourselves, because ShellExecute cannot handle anchors on local HTML files
		DWORD dwszBuffPathLen = MAX_PATH;
		if (CString application; SUCCEEDED(::AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_COMMAND, L"http", L"open", CStrBuf(application, dwszBuffPathLen), &dwszBuffPathLen)))
		{
			if (application.Find(L"%1") < 0)
				application += L" %1";

			if (application.Find(L"\"%1\"") >= 0)
				application.Replace(L"\"%1\"", L"%1");

			appHelp.Replace(L" ", L"%20");
			appHelp.Replace(L'\\', L'/');
			application.Replace(L"%1", L"file:///" + appHelp + page); // if the path is not prefixed with file:///, the anchor separator may not be detected correctly
			return CCreateProcessHelper::CreateProcessDetached(nullptr, application);
		}
	}
	CString url;
	url.Format(L"https://tortoisegit.org/docs/%s/%s", appName.MakeLower(), page);
	return reinterpret_cast<INT_PTR>(ShellExecute(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#endif
}


CString ChooseRepository(const HWND& hWnd, const CString* path)
{
	CBrowseFolder browseFolder;
	auto regLastResopitory = CRegString(L"Software\\TortoiseGit\\TortoiseProc\\LastRepo", L"");

	browseFolder.m_style = BIF_EDITBOX | BIF_NEWDIALOGSTYLE | BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS;
	CString strCloneDirectory;
	if(path)
		strCloneDirectory=*path;
	else
		strCloneDirectory = regLastResopitory;

	CString title;
	title.LoadString(IDS_CHOOSE_REPOSITORY);

	browseFolder.SetInfo(title);

	if (browseFolder.Show(hWnd, strCloneDirectory) == CBrowseFolder::OK)
	{
		regLastResopitory = strCloneDirectory;
		return strCloneDirectory;
	}
	else
		return CString();
}

void RemoveTrailSlash(CString &path)
{
	if(path.IsEmpty())
		return ;

	// For URL, do not trim the slash just after the host name component.
	int index = path.Find(L"://");
	if (index >= 0)
	{
		index += 4;
		index = path.Find(L'/', index);
		if (index == path.GetLength() - 1)
			return;
	}

	while (path[path.GetLength() - 1] == L'\\' || path[path.GetLength() - 1] == L'/')
	{
		path.Truncate(path.GetLength() - 1);
		if(path.IsEmpty())
			return;
	}
}
}
