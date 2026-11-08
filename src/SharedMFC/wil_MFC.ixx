module;
#include <afxwin.h>
#include <propkey.h>
#include <windef.h>
#include <CommCtrl.h>
#include <Windows.h>

#include <wil/win32_helpers.h>
#include <tgitlib/LoadIconEx.h>

#include "Resources/resource.h"

#include "Utils/URLFinder.h"


#pragma comment(lib, "Comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


export module wil.MFC;
import str_utils;
import std;
import gsl;
import wil;
import GitCall;
import ThemeAPI;
import DPIAware;

namespace fs = std::filesystem;
using namespace tgit;

export namespace wil::MFC {


// The HINSTANCE overloads live in tgitlib/LoadIconEx.h (TGitCache and
// TortoiseUDiff use them without MFC); re-exported here for module importers.
using ::LoadIconEx;


inline HICON LoadIconEx(const UINT resourceId, const UINT cx, const UINT cy) {
	return LoadIconEx(AfxGetResourceHandle(), MAKEINTRESOURCE(resourceId), gsl::narrow<int>(cx), gsl::narrow<int>(cy));
}


/**
 * Returns font name which is used for log messages, etc.
 */
std::wstring GetLogFontName() {
	return wil::reg::try_get_tgit_string(L"LogFontName").value_or(L"Consolas");
}


/**
 * Returns font size which is used for log messages, etc.
 */
DWORD GetLogFontSize() {
	return wil::reg::try_get_tgit_dword(L"Software\\TortoiseGit\\LogFontSize").value_or(9);
}


/**
 * Create a font which can is used for log messages, etc
 */
void CreateFontForLogs(const HWND& hWnd, CFont& fontToCreate) {
	LOGFONT logFont;
	auto* const hScreenDC = ::GetDC(nullptr);
	logFont.lfHeight = -CDPIAware::Instance().PointsToPixelsY(hWnd, win32_cast(GetLogFontSize()));
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
	wcsncpy_s(logFont.lfFaceName, GetLogFontName().c_str(), _TRUNCATE);
	VERIFY(fontToCreate.CreateFontIndirect(&logFont));
}


void MarkWindowAsUnpinnable(const HWND& hWnd) {
	const auto hShell = wil::unique_hmodule{ AtlLoadSystemLibraryUsingFullPath(L"Shell32.dll") };
	if (!hShell)
		return;

	auto* pfnSHGPSFW = GetProcAddressByFunctionDeclaration(hShell.get(), SHGetPropertyStoreForWindow); // NOLINT(*-pro-type-reinterpret-cast)
	if (pfnSHGPSFW == nullptr)
		return;

	IPropertyStore* pps{};
	const HRESULT hr = pfnSHGPSFW(hWnd, IID_PPV_ARGS(&pps));
	if (!(SUCCEEDED(hr)))
		return;

	std::ignore = pps->SetValue(PKEY_AppUserModel_PreventPinning, PROPVARIANT{ .vt = VT_BOOL, .boolVal = VARIANT_TRUE });
	std::ignore = pps->Release();
}


/**
* implements URL searching with the same logic as CSciEdit::StyleURLs
*/
std::vector<CHARRANGE> FindURLMatches(const CString& msg)
{
	std::vector<CHARRANGE> result;

	::FindURLMatches(msg, [](const CString&, int& i) { ++i; }, [&result](const int start, const int end) {
		const CHARRANGE range = { .cpMin = start, .cpMax = end };
		result.push_back(range); });

	return result;
}


bool FindStyleChars(const CString& sText, const wchar_t stylechar, int& start, int& end)
{
	int i = start;
	const int last = sText.GetLength() - 1;
	bool bFoundMarker = false;
	wchar_t c = (i <= 0) ? L'\0' : sText[i - 1];
	wchar_t nextChar = i >= last ? L'\0' : sText[i + 1];

	// find a starting marker
	while (i < last)
	{
		const wchar_t prevChar = c;
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
		const wchar_t prevChar = c;
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
	const auto fnFindMatchAtLineStart = [&](const wchar_t* const needle, const int idxStart, std::vector<CHARRANGE>& vRange) -> void {
		const int idxEnd{ idxStart + static_cast<int>(wcslen(needle)) };
		if (idxEnd > text.GetLength())
			return;

		if (StringUtils::StartsWith(static_cast<LPCWSTR>(text) + idxStart, needle))
		{
			const CHARRANGE range{ .cpMin = idxStart, .cpMax = idxEnd };
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

void SetCharFormat(const CWnd* window, const DWORD mask, const DWORD effects )
{
	CHARFORMAT2 format = {};
	format.cbSize = sizeof(CHARFORMAT2);
	format.dwMask = mask;
	format.dwEffects = effects;
	window->SendMessage(EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
}

/**
 * Apply the @a effects or color (depending on @a mask)
 * for all char ranges given in @a positions to the
 * @a window text.
 */
void SetCharFormat(const CWnd* window, const DWORD mask , const DWORD effects, const std::vector<CHARRANGE>& positions)
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


/**
 * Formats text in a rich edit control (version 2).
 * text in between * chars is formatted bold
 * text in between ^ chars is formatted italic
 * text in between _ chars is underlined
 */
bool FormatTextInRichEditControl(const CWnd* pWnd)
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
			CHARRANGE range = { .cpMin = static_cast<LONG>(start) + offset, .cpMax = static_cast<LONG>(end) + offset };
			pWnd->SendMessage(EM_EXSETSEL, reinterpret_cast<WPARAM>(nullptr), reinterpret_cast<LPARAM>(&range));
			SetCharFormat(pWnd, CFM_BOLD, CFE_BOLD);
			bStyled = true;
			start = end;
		}
		start = 0;
		end = 0;
		while (FindStyleChars(sLine, '^', start, end))
		{
			CHARRANGE range = { .cpMin = static_cast<LONG>(start) + offset, .cpMax = static_cast<LONG>(end) + offset };
			pWnd->SendMessage(EM_EXSETSEL, reinterpret_cast<WPARAM>(nullptr), reinterpret_cast<LPARAM>(&range));
			SetCharFormat(pWnd, CFM_ITALIC, CFE_ITALIC);
			bStyled = true;
			start = end;
		}
		start = 0;
		end = 0;
		while (FindStyleChars(sLine, '_', start, end))
		{
			CHARRANGE range = { .cpMin = static_cast<LONG>(start) + offset, .cpMax = static_cast<LONG>(end) + offset };
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
* implements URL searching with the same logic as CSciEdit::StyleURLs
*/
BOOL StyleURLs(const CString& msg, const CWnd* pWnd)
{
	const std::vector<CHARRANGE> positions = FindURLMatches(msg);
	SetCharFormat(pWnd, CFM_LINK, CFE_LINK, positions);

	return positions.empty() ? FALSE : TRUE;
}


std::wstring FormatWindowTitle(const auto& arg_path, const auto& dialogname, const auto& appname, const bool to_rel) {
	constexpr auto MAX_WINDOWS_TITLE_LENGTH = 80;

	auto path = str::to_path(arg_path) / L"";		// makesure it ends with a slash
	if (to_rel)
		if (const auto rel = fs::relative(path, GitCall::m_CurrentDirStorage); !rel.empty())
			path = rel.parent_path().wstring();

	const auto cch_max = MAX_WINDOWS_TITLE_LENGTH - str::as_view(dialogname).size();
	return std::format(L"{:>{}} - {} - {}", path, cch_max, dialogname, appname);
}


void SetWindowTitle(const HWND& hWnd, const CString& urlorpath, const CString& dialogname) {
	const auto format = wil::reg::try_get_tgit_dword(L"DialogTitles").value_or(0);
	const auto lp_string = FormatWindowTitle(urlorpath, dialogname, til::get_string_resource(IDS_APPNAME), format);
	SetWindowText(hWnd, lp_string.c_str());
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


CString GetClipboardLink(const CString& skipGitPrefix = L"", int paramsCount = 0)
{
	const auto clipboard = wil::open_clipboard_retrying(nullptr);	// was CClipboardHelper: retries while busy, closes on scope exit
	if (!clipboard)
		return CString();

	CString sClipboardText;
	if (const HGLOBAL hglb = GetClipboardData(CF_TEXT); hglb)
	{
		const auto lpstr = static_cast<LPCSTR>(GlobalLock(hglb));
		sClipboardText = CString(lpstr);
		GlobalUnlock(hglb);
	}
	if (const HGLOBAL hglb = GetClipboardData(CF_UNICODETEXT); hglb)
	{
		const auto lpstr = static_cast<LPCWSTR>(GlobalLock(hglb));
		sClipboardText = lpstr;
		GlobalUnlock(hglb);
	}

	if(!sClipboardText.IsEmpty())
	{
		if (sClipboardText[0] == L'"' && sClipboardText[sClipboardText.GetLength() - 1] == L'"')
			sClipboardText=sClipboardText.Mid(1,sClipboardText.GetLength()-2);

		if (const int newlinepos = sClipboardText.Find(L"\n"); newlinepos >= 0)
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

	return {};
}


std::string_view GetResourceData(const std::wstring& resName, const int id) {
	auto *const hResource = FindResource(nullptr, MAKEINTRESOURCE(id), resName.c_str());
	if (!hResource)
		return {};

	auto *const hResourceLoaded = LoadResource(nullptr, hResource);
	if (!hResourceLoaded)
		return {};

	const auto *const lpResLock = static_cast<const char*>(LockResource(hResourceLoaded));
	const auto resLen = SizeofResource(nullptr, hResource);
	return {lpResLock, resLen};
}


/**
* highlight warnings and errors returned by git command in rich edit control colored in bold
*/
BOOL StyleWarningsErrors(const CString& text, const CRichEditCtrl* edit) {
	ASSERT(edit);

	std::vector<CHARRANGE> rangeErrors;
	std::vector<CHARRANGE> rangeWarnings;
	if (!wil::MFC::FindWarningsErrors(text, rangeErrors, rangeWarnings))
		return FALSE;

	const COLORREF colorError{ CTheme::Instance().IsDarkTheme() ? RGB(207, 47, 47) : RGB(255, 0, 0) };
	SetCharFormat(edit, CFM_BOLD, CFE_BOLD, rangeErrors);
	SetCharFormat(edit, CFM_COLOR, colorError, rangeErrors);
	const COLORREF colorWarning{ CTheme::Instance().IsDarkTheme() ? RGB(185, 185, 0) : RGB(160, 160, 0) };
	SetCharFormat(edit, CFM_BOLD, CFE_BOLD, rangeWarnings);
	SetCharFormat(edit, CFM_COLOR, colorWarning, rangeWarnings);

	return TRUE;
}
}
