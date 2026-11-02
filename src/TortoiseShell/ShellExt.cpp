// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2011-2023, 2025-2026 - TortoiseGit
// Copyright (C) 2003-2014 - TortoiseSVN

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
#include "stdafx.h"

// Initialize GUIDs (should be done only and at-least once per DLL/EXE)
#include <initguid.h>
#include "Guids.h"

#include "ShellExt.h"

#include <shellapi.h>

#include "ShellState.h"
import TGitPath;	// GitAdminDir lives in the TGitPath module
#undef swprintf

// *********************** CShellExt *************************
CShellExt::CShellExt(FileState state)
	: m_State(state)
	,regDiffLater(L"Software\\TortoiseGit\\DiffLater", L"")
{
	InterlockedIncrement(&g_cRefThisDll);

	{
		AutoLocker lock(g_csGlobalCOMGuard);
		GetShellState().objects.Insert(this);
	}

	INITCOMMONCONTROLSEX used = {
		sizeof(INITCOMMONCONTROLSEX),
			ICC_LISTVIEW_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_USEREX_CLASSES
	};
	InitCommonControlsEx(&used);
	LoadLangDll();
}

CShellExt::~CShellExt()
{
	AutoLocker lock(g_csGlobalCOMGuard);
	InterlockedDecrement(&g_cRefThisDll);
	GetShellState().objects.Erase(this);
}

STDMETHODIMP CShellExt::QueryInterface(REFIID riid, LPVOID FAR *ppv)
{
	if (!ppv)
		return E_POINTER;

	*ppv = nullptr;

	if (IsEqualIID(riid, IID_IShellExtInit) || IsEqualIID(riid, IID_IUnknown))
		*ppv = static_cast<LPSHELLEXTINIT>(this);
	else if (IsEqualIID(riid, IID_IContextMenu))
		*ppv = static_cast<LPCONTEXTMENU>(this);
	else if (IsEqualIID(riid, IID_IContextMenu2))
		*ppv = static_cast<LPCONTEXTMENU2>(this);
	else if (IsEqualIID(riid, IID_IContextMenu3))
		*ppv = static_cast<LPCONTEXTMENU3>(this);
	else if (IsEqualIID(riid, IID_IShellIconOverlayIdentifier))
		*ppv = static_cast<IShellIconOverlayIdentifier*>(this);
	else if (IsEqualIID(riid, IID_IShellPropSheetExt))
		*ppv = static_cast<LPSHELLPROPSHEETEXT>(this);
	else if (IsEqualIID(riid, IID_IShellCopyHook))
		*ppv = static_cast<ICopyHook*>(this);
	else if (IsEqualIID(riid, IID_IExplorerCommand))
		*ppv = static_cast<IExplorerCommand*>(this);
	else if (IsEqualIID(riid, IID_IObjectWithSite))
		*ppv = static_cast<IObjectWithSite*>(this);
	else
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) CShellExt::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CShellExt::Release()
{
	if (InterlockedDecrement(&m_cRef))
		return m_cRef;

	delete this;

	return 0L;
}

// IPersistFile members
STDMETHODIMP CShellExt::GetClassID(CLSID *pclsid)
{
	if (!pclsid)
		return E_POINTER;
	*pclsid = CLSID_Tortoisegit_UNCONTROLLED;
	return S_OK;
}

STDMETHODIMP CShellExt::Load(LPCOLESTR /*pszFileName*/, DWORD /*dwMode*/)
{
	return S_OK;
}

// ICopyHook member
UINT __stdcall CShellExt::CopyCallback(HWND /*hWnd*/, UINT wFunc, UINT /*wFlags*/, LPCWSTR pszSrcFile, DWORD dwSrcAttribs, LPCWSTR /*pszDestFile*/, DWORD /*dwDestAttribs*/)
{
	switch (wFunc)
	{
	case FO_MOVE:
	case FO_DELETE:
	case FO_RENAME:
		if (pszSrcFile && (dwSrcAttribs & FILE_ATTRIBUTE_DIRECTORY) && g_ShellCache.IsPathAllowed(pszSrcFile))
		{
			const auto cacheType = g_ShellCache.GetCacheType();
			switch (cacheType)
			{
			case ShellCache::exe:
			{
				thread_local static std::wstring shortcut = L"::*-"; // initialize with an invalid path
				if (wcsncmp(shortcut.c_str(), pszSrcFile, shortcut.size()) == 0)
					break;
				shortcut = pszSrcFile;
				if (!shortcut.empty() && shortcut.back() != L'\\')
					shortcut += L'\\';

				m_remoteCacheLink.ReleaseLockForPath(CTGitPath(pszSrcFile, true));
				break;
			}
			case ShellCache::dll:
			case ShellCache::dllFull:
			{
				if (const auto topDir = GitAdminDir::HasAdminDir(pszSrcFile); !topDir.empty())
					m_CachedStatus.m_GitStatus.ReleasePath(CString(topDir.c_str()));
				break;
			}
			default:
				break;
			}
		}
		break;
	default:
		break;
	}

	// we could now wait a little bit to give the cache time to release the handles.
	// but the explorer/shell already retries any action for about two seconds
	// if it first fails. So if the cache hasn't released the handle yet, the explorer
	// will retry anyway, so we just leave here immediately.
	return IDYES;
}

// IObjectWithSite
HRESULT __stdcall CShellExt::SetSite(IUnknown* pUnkSite)
{
	m_site = pUnkSite;
	return S_OK;
}

HRESULT __stdcall CShellExt::GetSite(REFIID riid, void** ppvSite)
{
	return m_site.CopyTo(riid, ppvSite);
}
