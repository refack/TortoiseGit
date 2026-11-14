module;
#include "stdafx.h"		// Resources/resource.h: IDR_HELPMAPPING, IDS_APPNAME - without them StartHtmlHelp is `return false`

#include <tgitlib/CreateProcessHelper.h>

export module AppHelp;
import std;
import wil;
import wil.MFC;
import str_utils;
using namespace tgit;

/**
 * \ingroup TortoiseProc
 * F1 help: the local help files when they are installed, the online docs otherwise.
 *
 * Its own module, below StandAloneDlg, rather than in AppUtils.Window: every
 * CStandAloneDialogTmpl's HtmlHelp override calls it, and AppUtils.* import
 * TortoiseProc, whose GMF includes dialogs that `import StandAloneDlg` - so
 * StandAloneDlg importing AppUtils.Window would be a module cycle.
 * CAppUtils::StartHtmlHelp re-exports it for everything above.
 */
export namespace AppHelp {

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

}
