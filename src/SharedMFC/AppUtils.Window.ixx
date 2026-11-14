module;
#include "stdafx.h"

#include <shlobj_core.h>
#include <tgitlib/CreateProcessHelper.h>



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
import AppHelp;			// StartHtmlHelp
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
	const auto hIcon = wil::LoadIconAuto(AfxGetResourceHandle(), MAKEINTRESOURCE(nID), width, height);
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




// Lives in AppHelp, below StandAloneDlg (see there); re-exported for its callers here.
using AppHelp::StartHtmlHelp;


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
