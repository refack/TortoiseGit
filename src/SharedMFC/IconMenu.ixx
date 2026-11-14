module;
#include "stdafx.h"


export module IconMenu;
import tgit;
import IconBitmapUtils;


/**
 * \ingroup utils
 * Extends the CMenu class with a convenience method to insert a menu
 * entry with an icon.
 *
 * The icons are loaded from the resources and converted to a bitmap.
 * The bitmaps are destroyed together with the CIconMenu object.
 */
export class CIconMenu : public CMenu
{
public:
	CIconMenu();
	~CIconMenu();

	BOOL CreateMenu();
	BOOL CreatePopupMenu();
	BOOL AppendMenuIcon(UINT_PTR nIDNewItem, LPCWSTR lpszNewItem, UINT uIcon = 0, HMENU hsubmenu = nullptr);
	BOOL AppendMenuIcon(UINT_PTR nIDNewItem, UINT_PTR nNewItem, UINT uIcon = 0, HMENU hsubmenu = nullptr);
	void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct) override;
	void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct) override;

	BOOL SetMenuItemData(UINT_PTR nIDNewItem, LONG_PTR data);
	LONG_PTR GetMenuItemData(UINT_PTR nIDNewItem);

private:
	BOOL SetMenuStyle();

private:
	std::map<UINT_PTR, UINT>	icons;
	std::map<UINT_PTR, HICON>	iconhandles;
	bool						bShowIcons;
};



CIconMenu::CIconMenu() : CMenu() {
	bShowIcons = wil::reg::try_get_tgit_bool(L"ShowAppContextMenuIcons").value_or(true);
}

CIconMenu::~CIconMenu()
{
	for (const auto& iconhandle : iconhandles)
		DestroyIcon(iconhandle.second);
}

BOOL CIconMenu::CreateMenu()
{
	if (__super::CreateMenu() == FALSE)
		return FALSE;

	SetMenuStyle();

	return TRUE;
}

BOOL CIconMenu::CreatePopupMenu()
{
	if (__super::CreatePopupMenu() == FALSE)
		return FALSE;

	SetMenuStyle();

	return TRUE;
}

BOOL CIconMenu::SetMenuStyle()
{
	MENUINFO MenuInfo = { 0 };

	MenuInfo.cbSize  = sizeof(MenuInfo);
	MenuInfo.fMask   = MIM_STYLE | MIM_APPLYTOSUBMENUS;
	MenuInfo.dwStyle = MNS_CHECKORBMP;

	SetMenuInfo(&MenuInfo);

	return TRUE;
}

BOOL CIconMenu::AppendMenuIcon(UINT_PTR nIDNewItem, LPCWSTR lpszNewItem, UINT uIcon /* = 0 */, HMENU hsubmenu /* = nullptr */)
{
	wchar_t menutextbuffer[255] = { 0 };
	wcscpy_s(menutextbuffer, lpszNewItem);

	MENUITEMINFO info = {0};
	info.cbSize = sizeof(info);
	info.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_ID;
	info.fType = MFT_STRING;

	if (hsubmenu)
	{
		info.fMask |= MIIM_SUBMENU;
		info.hSubMenu = hsubmenu;
	}

	info.wID = static_cast<UINT>(nIDNewItem);
	info.dwTypeData = menutextbuffer;
	if ((uIcon != 0) && bShowIcons)
	{
		info.fMask |= MIIM_BITMAP;
		info.hbmpItem = IconBitmapUtils::IconToBitmapPARGB32(AfxGetResourceHandle(), uIcon);

		icons[nIDNewItem] = uIcon;
	}

	return InsertMenuItem(static_cast<UINT>(nIDNewItem), &info);
}

BOOL CIconMenu::AppendMenuIcon(UINT_PTR nIDNewItem, UINT_PTR nNewItem, UINT uIcon /* = 0 */, HMENU hsubmenu /* = nullptr */)
{
	CString temp;
	temp.LoadString(static_cast<UINT>(nNewItem));

	return AppendMenuIcon(nIDNewItem, temp, uIcon, hsubmenu);
}

void CIconMenu::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct || lpDrawItemStruct->CtlType != ODT_MENU)
		return;		//not for a menu
	HICON hIcon = nullptr;
	bool bDestroyIcon = true;
	int iconWidth = GetSystemMetrics(SM_CXSMICON);
	int iconHeight = GetSystemMetrics(SM_CYSMICON);
	if (iconhandles.find(lpDrawItemStruct->itemID) != iconhandles.end())
	{
		hIcon = iconhandles[lpDrawItemStruct->itemID];
		bDestroyIcon = false;
	}
	else
		hIcon = wil::LoadIconEx(AfxGetResourceHandle(), MAKEINTRESOURCE(icons[lpDrawItemStruct->itemID]), iconWidth, iconHeight);
	if (!hIcon)
		return;
	DrawIconEx(lpDrawItemStruct->hDC,
		lpDrawItemStruct->rcItem.left - iconWidth,
		lpDrawItemStruct->rcItem.top + (lpDrawItemStruct->rcItem.bottom - lpDrawItemStruct->rcItem.top - iconHeight) / 2,
		hIcon, iconWidth, iconHeight,
		0, nullptr, DI_NORMAL);
	if (bDestroyIcon)
		DestroyIcon(hIcon);
}

void CIconMenu::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	if (!lpMeasureItemStruct)
		return;
	lpMeasureItemStruct->itemWidth += 2;
	if (lpMeasureItemStruct->itemHeight < 16)
		lpMeasureItemStruct->itemHeight = 16;
}

BOOL CIconMenu::SetMenuItemData(UINT_PTR nIDNewItem, LONG_PTR data)
{
	MENUITEMINFO menuinfo ={0};
	menuinfo.cbSize = sizeof(menuinfo);
	GetMenuItemInfo(static_cast<UINT>(nIDNewItem), &menuinfo);
	menuinfo.dwItemData =data;
	menuinfo.fMask |= MIIM_DATA;
	return SetMenuItemInfo(static_cast<UINT>(nIDNewItem), &menuinfo);
}

LONG_PTR CIconMenu::GetMenuItemData(UINT_PTR nIDNewItem)
{
	MENUITEMINFO menuinfo ={0};
	menuinfo.fMask |= MIIM_DATA;
	menuinfo.cbSize = sizeof(menuinfo);
	GetMenuItemInfo(static_cast<UINT>(nIDNewItem), &menuinfo);

	return menuinfo.dwItemData;
}
