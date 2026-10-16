module;
#include "stdafx.h"
#include "../std.h"

#include <shlobj_core.h>
#include <Utils/SelectFileFilter.h>
#include <Utils/DPIAware.h>
#include <Utils/LoadIconEx.h>
#include <Utils/CreateProcessHelper.h>
#include "../MiscUI/IconBitmapUtils.h"
#include "Utils/URLFinder.h"
#include "TortoiseProc.h"

#ifndef TGIT_TESTS_ONLY
#include "Utils/BrowseFolder.h"
#include "Utils/Theme.h"
#include "PropKey.h"
#endif

export module AppUtils.Window;

/**
 * \ingroup TortoiseProc
 * What a dialog does to itself: its title, taskbar pinning, log fonts, icons and list
 * backgrounds, the common file dialog, F1 help, and styling the text of a rich edit
 * control. Nothing here runs git.
 */
export namespace CAppUtils {

using ::LoadIconEx;


bool SetListCtrlBackgroundImage(HWND hListCtrl, const UINT nID, const int width, const int height)
{
	if (static_cast<DWORD>(CRegStdDWORD(L"Software\\TortoiseGit\\ShowListBackgroundImage", TRUE)) == FALSE)
		return false;
	// create a bitmap from the icon
	CAutoIcon hIcon = ::LoadIconEx(AfxGetResourceHandle(), MAKEINTRESOURCE(nID), width, height);
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

bool SetListCtrlBackgroundImage(HWND hListCtrl, const UINT nID)
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
	if ((!PathIsDirectory(path) || handleAsFile) && !SUCCEEDED(pfd->SetFileName(PathUtils::GetFileNameFromPath(tgit::str::StringView(path)).c_str())))
		return false;

	// Set a title
	if (title)
	{
		CString temp;
		temp.LoadString(title);
		StringUtils::RemoveAccelerators(temp);
		pfd->SetTitle(temp);
	}
	CComPtr<IShellItem> psiDefaultFolder;
	if (filterId)
	{
		CSelectFileFilter fileFilter(filterId);
		if (!SUCCEEDED(pfd->SetFileTypes(fileFilter.GetCount(), fileFilter)))
			return false;
		if (filterId == 1602 || filterId == 2501) // IDS_GITEXEFILEFILTER || IDS_PROGRAMSFILEFILTER
		{
			pfd->SetClientGuid({ 0x323ca4b0, 0x62df, 0x4a08, { 0xa5, 0x5, 0x58, 0xde, 0xa2, 0xb9, 0x2d, 0xcd } });
			if (SUCCEEDED(SHCreateItemFromParsingName(PathUtils::GetProgramsDirectory().c_str(), nullptr, IID_PPV_ARGS(&psiDefaultFolder))))
				pfd->SetDefaultFolder(psiDefaultFolder);
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
			pfd->SetFolder(psiFolder);
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
		pfd->GetFileTypeIndex(&fi);
		*filterindex = gsl::narrow<int>(fi);
	}
	return true;
}

HICON LoadIconEx(const UINT resourceId, const UINT cx, const UINT cy)
{
	return ::LoadIconEx(AfxGetResourceHandle(), MAKEINTRESOURCE(resourceId), gsl::narrow<int>(cx), gsl::narrow<int>(cy));
}

/**
 * Returns font name which is used for log messages, etc.
 */
CString GetLogFontName()
{
	return CRegString(L"Software\\TortoiseGit\\LogFontName", L"Consolas");
}

/**
 * Returns font size which is used for log messages, etc.
 */
DWORD GetLogFontSize()
{
	return CRegDWORD(L"Software\\TortoiseGit\\LogFontSize", 9);
}

/**
 * Create a font which can is used for log messages, etc
 */
void CreateFontForLogs(HWND hWnd, CFont& fontToCreate)
{
	LOGFONT logFont;
	HDC hScreenDC = ::GetDC(nullptr);
	logFont.lfHeight = -CDPIAware::Instance().PointsToPixelsY(hWnd, GetLogFontSize());
	::ReleaseDC(nullptr, hScreenDC);
	logFont.lfWidth = 0;
	logFont.lfEscapement = 0;
	logFont.lfOrientation = 0;
	logFont.lfWeight = FW_NORMAL;
	logFont.lfItalic = 0;
	logFont.lfUnderline = 0;
	logFont.lfStrikeOut = 0;
	logFont.lfCharSet = DEFAULT_CHARSET;
	logFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
	logFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	logFont.lfQuality = DRAFT_QUALITY;
	logFont.lfPitchAndFamily = FF_DONTCARE | FIXED_PITCH;
	wcsncpy_s(logFont.lfFaceName, GetLogFontName(), _TRUNCATE);
	VERIFY(fontToCreate.CreateFontIndirect(&logFont));
}

const char* GetResourceData(const wchar_t* resName, const int id, DWORD& resLen)
{
	resLen = 0;
	auto hResource = FindResource(nullptr, MAKEINTRESOURCE(id), resName);
	if (!hResource)
		return nullptr;
	auto hResourceLoaded = LoadResource(nullptr, hResource);
	if (!hResourceLoaded)
		return nullptr;
	auto lpResLock = static_cast<const char*>(LockResource(hResourceLoaded));
	resLen = SizeofResource(nullptr, hResource);
	return lpResLock;
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
		const char* resData = GetResourceData(L"help", IDR_HELPMAPPING, resSize);
		if (resData)
		{
			auto resString = std::string(resData, resSize);
			std::vector<std::string> lines;
			StringUtils::stringtok(lines, resString, true, "\r\n");
			for (const auto& line : lines)
			{
				if (line.empty())
					continue;
				std::vector<std::string> lineParts;
				StringUtils::stringtok(lineParts, line, true, "=");
				if (lineParts.size() == 2)
					idMap[std::stoi(lineParts[0], nullptr, 0)] = CUnicodeUtils::GetUnicode<std::wstring>(lineParts[1]);
			}
		}
	}

	if (idMap.find(id) != idMap.end())
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
	url.Format(L"https://tortoisegit.org/docs/%s/%s", static_cast<LPCWSTR>(appName.MakeLower()), static_cast<LPCWSTR>(page));
	return reinterpret_cast<INT_PTR>(ShellExecute(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#endif
}

CString FormatWindowTitle(const CString& urlorpath2, const CString& dialogname, const CString& appname, DWORD format)
{
	CString urlorpath{ urlorpath2 };
	switch (format)
	{
	case 1:
	{
		const CTGitPath filePath{ g_Git.m_CurrentDir.GetString() };
		const CString toStrip = filePath.GetContainingDirectory().GetWinPathString().c_str();
		if (!StringUtils::StartsWith(urlorpath2, g_Git.m_CurrentDir))
			break;
		else if (toStrip.IsEmpty() || StringUtils::EndsWith(toStrip, L'\\'))
			urlorpath = urlorpath2.Right(urlorpath2.GetLength() - toStrip.GetLength());
		else
			urlorpath = urlorpath2.Right(urlorpath2.GetLength() - toStrip.GetLength() - 1);
	}
	break;
	default:
		break;
	}

	ASSERT(dialogname.GetLength() < 80);
	ASSERT(urlorpath.GetLength() < MAX_PATH);
	WCHAR pathbuf[MAX_PATH] = {};

	PathCompactPathEx(pathbuf, urlorpath, 80 - dialogname.GetLength(), 0);

	wcscat_s(pathbuf, L" - ");
	wcscat_s(pathbuf, dialogname);
	wcscat_s(pathbuf, L" - ");
	wcscat_s(pathbuf, appname);

	return pathbuf;
}

/**
 * Apply the @a effects or color (depending on @a mask)
 * for all char ranges given in @a positions to the
 * @a window text.
 */
void SetCharFormat(CWnd* window, const DWORD mask , const DWORD effects, const std::vector<CHARRANGE>& positions)
{
	CHARFORMAT2 format = {};
	format.cbSize = sizeof(CHARFORMAT2);
	format.dwMask = mask;
	format.dwEffects = effects;
	format.crTextColor = effects;

	for (const auto& range : positions)
	{
		window->SendMessage(EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
		window->SendMessage(EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
	}
}

void SetCharFormat(CWnd* window, const DWORD mask, const DWORD effects )
{
	CHARFORMAT2 format = {};
	format.cbSize = sizeof(CHARFORMAT2);
	format.dwMask = mask;
	format.dwEffects = effects;
	window->SendMessage(EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
}

bool FindStyleChars(const CString& sText, wchar_t stylechar, int& start, int& end)
{
	int i=start;
	const int last = sText.GetLength() - 1;
	bool bFoundMarker = false;
	wchar_t c = i == 0 ? L'\0' : sText[i - 1];
	wchar_t nextChar = i >= last ? L'\0' : sText[i + 1];

	// find a starting marker
	while (i < last)
	{
		wchar_t prevChar = c;
		c = nextChar;
		nextChar = sText[i + 1];

		// IsCharAlphaNumeric can be somewhat expensive.
		// Long lines of "*****" or "----" will be pre-emptied efficiently
		// by the (c != nextChar) condition.

		if ((c == stylechar) && (c != nextChar))
		{
			if (IsCharAlphaNumeric(nextChar) && !IsCharAlphaNumeric(prevChar))
			{
				start = ++i;
				bFoundMarker = true;
				break;
			}
		}
		++i;
	}
	if (!bFoundMarker)
		return false;

	// find ending marker
	// c == sText[i - 1]

	bFoundMarker = false;
	while (i <= last)
	{
		wchar_t prevChar = c;
		c = sText[i];
		if (c == stylechar)
		{
			if ((i == last) || (!IsCharAlphaNumeric(sText[i + 1]) && IsCharAlphaNumeric(prevChar)))
			{
				end = i;
				// ReSharper disable once CppAssignedValueIsNeverUsed
				++i;
				bFoundMarker = true;
				break;
			}
		}
		++i;
	}
	return bFoundMarker;
}

bool FindWarningsErrors(const CString& text, std::vector<CHARRANGE>& rangeErrors, std::vector<CHARRANGE>& rangeWarnings)
{
	if (text.IsEmpty())
		return FALSE;
	const auto fnFindMatchAtLineStart = [&](const wchar_t* const needle, int idxStart, std::vector<CHARRANGE>& vRange) -> void {
		const int idxEnd{ idxStart + static_cast<int>(wcslen(needle)) };
		if (idxEnd > text.GetLength())
			return;

		if (StringUtils::StartsWith(static_cast<LPCWSTR>(text) + idxStart, needle))
		{
			const CHARRANGE range{ idxStart, idxEnd };
			vRange.push_back(range);
		}
	};

	int idxStart = 0;
	do
	{
		fnFindMatchAtLineStart(L"fatal: ", idxStart, rangeErrors);
		fnFindMatchAtLineStart(L"error: ", idxStart, rangeErrors);
		fnFindMatchAtLineStart(L"warning: ", idxStart, rangeWarnings);
	} while ((idxStart = text.Find('\n', idxStart) + 1) > 0 && idxStart < text.GetLength());

	return !rangeErrors.empty() || !rangeWarnings.empty();
}

/**
* implements URL searching with the same logic as CSciEdit::StyleURLs
*/
std::vector<CHARRANGE> FindURLMatches(const CString& msg)
{
	std::vector<CHARRANGE> result;

	::FindURLMatches(msg, [](const CString&, int& i) { ++i; }, [&result](int start, int end) {
		CHARRANGE range = { start, end };
		result.push_back(range); });

	return result;
}

#ifndef TGIT_TESTS_ONLY
/**
 * Formats text in a rich edit control (version 2).
 * text in between * chars is formatted bold
 * text in between ^ chars is formatted italic
 * text in between _ chars is underlined
 */
bool FormatTextInRichEditControl(CWnd * pWnd)
{
	CString sText;
	if (!pWnd)
		return false;
	bool bStyled = false;
	pWnd->GetWindowText(sText);
	// the rich edit control doesn't count the CR char!
	// to be exact: CRLF is treated as one char.
	sText.Remove(L'\r');

	// style each line separately
	int offset = 0;
	int nNewlinePos;
	do
	{
		nNewlinePos = sText.Find('\n', offset);
		CString sLine = nNewlinePos >= 0 ? sText.Mid(offset, nNewlinePos - offset) : sText.Mid(offset);

		int start = 0;
		int end = 0;
		while (FindStyleChars(sLine, '*', start, end))
		{
			CHARRANGE range = { static_cast<LONG>(start) + offset, static_cast<LONG>(end) + offset };
			pWnd->SendMessage(EM_EXSETSEL, reinterpret_cast<WPARAM>(nullptr), reinterpret_cast<LPARAM>(&range));
			SetCharFormat(pWnd, CFM_BOLD, CFE_BOLD);
			bStyled = true;
			start = end;
		}
		start = 0;
		end = 0;
		while (FindStyleChars(sLine, '^', start, end))
		{
			CHARRANGE range = { static_cast<LONG>(start) + offset, static_cast<LONG>(end) + offset };
			pWnd->SendMessage(EM_EXSETSEL, reinterpret_cast<WPARAM>(nullptr), reinterpret_cast<LPARAM>(&range));
			SetCharFormat(pWnd, CFM_ITALIC, CFE_ITALIC);
			bStyled = true;
			start = end;
		}
		start = 0;
		end = 0;
		while (FindStyleChars(sLine, '_', start, end))
		{
			CHARRANGE range = { static_cast<LONG>(start) + offset, static_cast<LONG>(end) + offset };
			pWnd->SendMessage(EM_EXSETSEL, reinterpret_cast<WPARAM>(nullptr), reinterpret_cast<LPARAM>(&range));
			SetCharFormat(pWnd, CFM_UNDERLINE, CFE_UNDERLINE);
			bStyled = true;
			start = end;
		}
		offset = nNewlinePos+1;
	} while(nNewlinePos>=0);
	return bStyled;
}

/**
* highlight warnings and errors returned by git command in rich edit control colored in bold
*/
BOOL StyleWarningsErrors(const CString& text, CRichEditCtrl* edit)
{
	ASSERT(edit);

	std::vector<CHARRANGE> rangeErrors;
	std::vector<CHARRANGE> rangeWarnings;
	if (!FindWarningsErrors(text, rangeErrors, rangeWarnings))
		return FALSE;

	const COLORREF colorError{ CTheme::Instance().IsDarkTheme() ? RGB(207, 47, 47) : RGB(255, 0, 0) };
	SetCharFormat(edit, CFM_BOLD, CFE_BOLD, rangeErrors);
	SetCharFormat(edit, CFM_COLOR, colorError, rangeErrors);
	const COLORREF colorWarning{ CTheme::Instance().IsDarkTheme() ? RGB(185, 185, 0) : RGB(160, 160, 0) };
	SetCharFormat(edit, CFM_BOLD, CFE_BOLD, rangeWarnings);
	SetCharFormat(edit, CFM_COLOR, colorWarning, rangeWarnings);

	return TRUE;
}

/**
* implements URL searching with the same logic as CSciEdit::StyleURLs
*/
BOOL StyleURLs(const CString& msg, CWnd* pWnd)
{
	std::vector<CHARRANGE> positions = FindURLMatches(msg);
	SetCharFormat(pWnd, CFM_LINK, CFE_LINK, positions);

	return positions.empty() ? FALSE : TRUE;
}

void SetWindowTitle(HWND hWnd, const CString& urlorpath, const CString& dialogname)
{
	SetWindowText(hWnd, FormatWindowTitle(urlorpath, dialogname, CString(MAKEINTRESOURCE(IDS_APPNAME)), static_cast<DWORD>(CRegStdDWORD(L"Software\\TortoiseGit\\DialogTitles", 0))));
}

/**
* Sets the title of a dialog
*/
void SetWindowTitle(const CWnd& dialog, const CString& urlorpath)
{
	CString sWindowTitle;
	dialog.GetWindowTextW(sWindowTitle);
	ASSERT(sWindowTitle.Find(CString(MAKEINTRESOURCE(IDS_APPNAME)), 0) == -1);
	SetWindowTitle(dialog.GetSafeHwnd(), urlorpath, sWindowTitle);
}

void MarkWindowAsUnpinnable(HWND hWnd)
{
	using SHGPSFW = HRESULT(WINAPI*)(HWND hwnd, REFIID riid, void** ppv);

	CAutoLibrary hShell = AtlLoadSystemLibraryUsingFullPath(L"Shell32.dll");

	if (hShell.IsValid()) {
		auto pfnSHGPSFW = reinterpret_cast<SHGPSFW>(::GetProcAddress(hShell, "SHGetPropertyStoreForWindow"));
		if (pfnSHGPSFW) {
			IPropertyStore *pps;
			HRESULT hr = pfnSHGPSFW(hWnd, IID_PPV_ARGS(&pps));
			if (SUCCEEDED(hr)) {
				PROPVARIANT var;
				var.vt = VT_BOOL;
				var.boolVal = VARIANT_TRUE;
				pps->SetValue(PKEY_AppUserModel_PreventPinning, var);
				pps->Release();
			}
		}
	}
}

CString GetClipboardLink(const CString& skipGitPrefix = L"", int paramsCount = 0)
{
	const auto clipboard = wil::open_clipboard_retrying(nullptr);	// was CClipboardHelper: retries while busy, closes on scope exit
	if (!clipboard)
		return CString();

	CString sClipboardText;
	if (HGLOBAL hglb = GetClipboardData(CF_TEXT); hglb)
	{
		auto lpstr = static_cast<LPCSTR>(GlobalLock(hglb));
		sClipboardText = CString(lpstr);
		GlobalUnlock(hglb);
	}
	if (HGLOBAL hglb = GetClipboardData(CF_UNICODETEXT); hglb)
	{
		auto lpstr = static_cast<LPCWSTR>(GlobalLock(hglb));
		sClipboardText = lpstr;
		GlobalUnlock(hglb);
	}

	if(!sClipboardText.IsEmpty())
	{
		if (sClipboardText[0] == L'"' && sClipboardText[sClipboardText.GetLength() - 1] == L'"')
			sClipboardText=sClipboardText.Mid(1,sClipboardText.GetLength()-2);

		if (int newlinepos = sClipboardText.Find(L"\n"); newlinepos >= 0)
		{
			sClipboardText.Truncate(newlinepos);
			sClipboardText.TrimRight();
		}
		if (sClipboardText.IsEmpty())
			return CString();

		for (const CString& prefix : { L"http://", L"https://", L"git://", L"ssh://", L"git@" })
		{
			if (StringUtils::StartsWith(sClipboardText, prefix) && sClipboardText.GetLength() != prefix.GetLength())
				return sClipboardText;
		}

		if(sClipboardText.GetLength()>=2)
			if (sClipboardText[1] == L':')
				if( (sClipboardText[0] >= 'A' &&  sClipboardText[0] <= 'Z')
					|| (sClipboardText[0] >= 'a' &&  sClipboardText[0] <= 'z') )
					return sClipboardText;

		// trim prefixes like "git clone "
		if (!skipGitPrefix.IsEmpty() && StringUtils::StartsWith(sClipboardText, skipGitPrefix))
		{
			sClipboardText = sClipboardText.Mid(skipGitPrefix.GetLength()).Trim();
			int spacePos = -1;
			while (paramsCount >= 0)
			{
				--paramsCount;
				spacePos = sClipboardText.Find(L' ', spacePos + 1);
				if (spacePos == -1)
					break;
			}
			if (spacePos > 0 && paramsCount < 0)
				sClipboardText.Truncate(spacePos);
			return sClipboardText;
		}
	}

	return CString();
}

CString ChooseRepository(HWND hWnd, const CString* path)
{
	CBrowseFolder browseFolder;
	CRegString regLastResopitory = CRegString(L"Software\\TortoiseGit\\TortoiseProc\\LastRepo", L"");

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

#endif
}
