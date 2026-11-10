module;
#include "stdafx.h"

#include <shlobj_core.h>
#include <tgitlib/CreateProcessHelper.h>

#include "Resources/resource.h"

#include "Utils/URLFinder.h"



export module AppUtils.Window;
import std;
import wil;
import wil.MFC;
import str_utils;
import BrowseFolder;
import ThemeAPI;
import IconBitmapUtils;
import PathUtils;
import TortoiseProc;
import DPIAware;
import ConfigureGitExe;
using namespace tgit;

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
	if (wil::reg::try_get_tgit_bool(L"ShowListBackgroundImage").value_or(true))
		return false;
	// create a bitmap from the icon
	const auto hIcon = wil::unique_hicon{LoadIconEx(AfxGetResourceHandle(), MAKEINTRESOURCE(nID), width, height)};
	if (!hIcon)
		return false;

	LVBKIMAGE lv;
	lv.ulFlags = LVBKIF_TYPE_WATERMARK | LVBKIF_FLAG_ALPHABLEND;
	lv.hbm = IconBitmapUtils::IconToBitmapPARGB32(hIcon.get(), width, height);
	lv.xOffsetPercent = 100;
	lv.yOffsetPercent = 100;
	ListView_SetBkImage(hListCtrl, &lv);
	return true;
}

bool SetListCtrlBackgroundImage(const HWND& hListCtrl, const UINT nID)
{
	return SetListCtrlBackgroundImage(hListCtrl, nID, CDPIAware::Instance().ScaleX(hListCtrl, 128), CDPIAware::Instance().ScaleY(hListCtrl, 128));
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
		const auto resString = wil::MFC::GetResourceData(L"help", IDR_HELPMAPPING);
		if (!resString.data()) {
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
	if (CString appHelp = std::format(L"{}{}_en\\", til::GetAppDirectory(), appName).c_str(); PathFileExists(appHelp))
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
	auto regLastResopitory = wil::reg::try_get_tgit_string(L"TortoiseProc\\LastRepo").value_or(L"");

	browseFolder.m_style = BIF_EDITBOX | BIF_NEWDIALOGSTYLE | BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS;
	CString strCloneDirectory = (path) ? path->GetString() : regLastResopitory.c_str();
	if(path)
		strCloneDirectory = *path;
	else
		strCloneDirectory = regLastResopitory.c_str();

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
