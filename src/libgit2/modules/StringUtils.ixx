module;
#include "Utils/ClipboardHelper.h"
#include <afx.h>
#include <atlstr.h>

export module StringUtils;

import std;
import gsl;
import wstr;
import RIAA;

export namespace StringUtils {

using std::string_view;


// append = true as the default: a default value should never lose data!
template <typename Container, typename CHARTYPE, typename TRAIT>
void stringtok(Container& container, const std::basic_string<CHARTYPE, TRAIT>& in, bool trim, const CHARTYPE* const delimiters, bool append = true)
{
	const auto len = in.length();
	decltype(in.length()) i = 0;
	if (!append)
		container.clear();

	while (i < len)
	{
		if (trim)
		{
			// eat leading whitespace
			i = in.find_first_not_of(delimiters, i);
			if (i == std::basic_string<CHARTYPE, TRAIT>::npos)
				return; // nothing left but white space
		}

		// find the end of the token
		const auto j = in.find_first_of(delimiters, i);

		// push token
		if (j == std::basic_string<CHARTYPE, TRAIT>::npos)
		{
			if constexpr (std::is_same_v<typename Container::value_type, std::basic_string<CHARTYPE, TRAIT>>)
				container.push_back(in.substr(i));
			else if constexpr (std::is_same_v<CHARTYPE, wchar_t>)
				container.push_back(static_cast<Container::value_type>(_wtoi64(in.substr(i).c_str())));
			else if constexpr (std::is_same_v<CHARTYPE, char>)
				container.push_back(static_cast<Container::value_type>(_atoi64(in.substr(i).c_str())));
			else
				static_assert(false);
			return;
		}
		else
		{
			if constexpr (std::is_same_v<typename Container::value_type, std::basic_string<CHARTYPE, TRAIT>>)
				container.push_back(in.substr(i, j - i));
			else if constexpr (std::is_same_v<CHARTYPE, wchar_t>)
				container.push_back(static_cast<Container::value_type>(_wtoi64(in.substr(i, j - i).c_str())));
			else if constexpr (std::is_same_v<CHARTYPE, char>)
				container.push_back(static_cast<Container::value_type>(_atoi64(in.substr(i, j - i).c_str())));
			else
				static_assert(false);
		}

		// set up for next loop
		i = j + 1;
	}
}

/**
 * \ingroup Utils
 * string helper functions
 */
class CStringUtils
{
public:
	CStringUtils() = delete;

	/**
	 * Removes all '&' chars from a string.
	 */
	static void RemoveAccelerators(CString& text);

	/**
	 * Returns the accellerator used in the string or \0
	 */
	static wchar_t GetAccellerator(const CString& text);

	/**
	 * Escapes all '&' chars from a string with another '&'.
	 */
	static CString EscapeAccellerators(CString& text);

	static CString EnsureCRLF(const CString& text);
	/**
	 * Writes an ASCII CString to the clipboard in CF_TEXT format
	 */
	static bool WriteAsciiStringToClipboard(const CStringA& sClipdata, LCID lcid, HWND hOwningWnd = nullptr);
	/**
	 * Writes a String to the clipboard in both CF_UNICODETEXT and CF_TEXT format
	 */
	static bool WriteAsciiStringToClipboard(const CStringW& sClipdata, HWND hOwningWnd = nullptr);

	/**
	* Writes an ASCII CString to the clipboard in TGIT_UNIFIEDDIFF format, which is basically the patch file
	* as a ASCII string.
	*/
	static bool WriteDiffToClipboard(const CStringA& sClipdata, HWND hOwningWnd = nullptr);

	/**
	 * Reads the string \text from the file \path in utf8 encoding.
	 */
	static bool ReadStringFromTextFile(const CString& path, CString& text);

	static BOOL WildCardMatch(const CString& wildcard, const CString& string);
	static CString LinesWrap(const CString& longstring, int limit = 80, bool bCompactPaths = false);
	static CString WordWrap(const CString& longstring, int limit, bool bCompactPaths, bool bForceWrap, int tabSize);
	static std::vector<CString> WordWrap(const CString& longstring, int limit, int tabSize);
	/**
	 * Unescapes Git quoted filenames
	 * This is not a full implementation of the unescaper as we skip some conversions that will result in invalid filenames.
	 */
	static CString UnescapeGitQuotePath(std::wstring_view s);
	static CString UnescapeGitQuotePathA(std::string_view s);
	/**
	 * Find and return the number n of starting characters equal between
	 * \ref lhs and \ref rhs. (max n: lhs.Left(n) == rhs.Left(n))
	 */
	static int GetMatchingLength (const CString& lhs, const CString& rhs);

	/**
	 * Optimizing wrapper around CompareNoCase.
	 */
	static int FastCompareNoCase (const CStringW& lhs, const CStringW& rhs);

	static void ParseEmailAddress(CString mailaddress, CString& parsedAddress, CString* parsedName = nullptr);

	static bool IsPlainReadableASCII(const CString& text);

	static CString EscapeWindowsCliArguments(const CString& argument);

	static bool StartsWith(const wchar_t* heystack, const CString& needle);
	static bool StartsWithI(const wchar_t* heystack, const CString& needle);
	static bool WriteStringToTextFile(LPCWSTR path, LPCWSTR text, bool bUTF8 = true);
	static bool EndsWith(const CString& heystack, const wchar_t* needle);
	static bool EndsWith(const CString& heystack, wchar_t needle);
	static bool EndsWithI(const CString& heystack, const wchar_t* needle);
	static bool StartsWith(const wchar_t* heystack, const wchar_t* needle);
	static bool StartsWith(const char* heystack, const char* needle);

	/**
	 * Quotes one argv element the way CommandLineToArgvW / the MSVC runtime read
	 * it back. Always adds the surrounding quotes; see \ref NeedsWindowsCliQuoting
	 * for the "only when it matters" test the argv serializer applies first.
	 *
	 * Flavor-neutral on purpose: this is the building block CGit::SerializeArgv
	 * needs, and CGit is on the list of sources the ATL side compiles too.
	 */
	[[nodiscard]] static std::wstring EscapeWindowsCliArgument(std::wstring_view argument);

	/**
	 * True when \a argument would not survive the round trip unquoted. An empty
	 * element must be quoted (otherwise it vanishes); whitespace and quotes are
	 * the only other characters CommandLineToArgvW treats as structure.
	 *
	 * A lone backslash needs nothing: backslashes are only special immediately
	 * before a quote, and there is no quote here to be before.
	 */
	[[nodiscard]] static bool NeedsWindowsCliQuoting(std::wstring_view argument);

	/**
	 * Quotes one argv element for a POSIX shell, which is what the msys2/cygwin
	 * path needs: CGit writes the whole command line into a temp file and hands
	 * it to bash.exe, so there the "command line" is shell *source*.
	 */
	[[nodiscard]] static std::wstring EscapePosixShellArgument(std::wstring_view argument);

	/**
	 * True unless \a argument is made entirely of characters no shell touches.
	 * Deliberately a whitelist: a blacklist of bash metacharacters is a list you
	 * are always one shell feature behind on.
	 */
	[[nodiscard]] static bool NeedsPosixShellQuoting(std::wstring_view argument);

	/**
	 * Writes the string \text to the file \path, either in utf16 or utf8 encoding,
	 * depending on the \c bUTF8 param.
	 */
	static bool WriteStringToTextFile(const std::wstring& path, std::wstring_view text, bool bUTF8 = true);

	/**
	 * Replace all pipe (|) character in the string with a nullptr character. Used
	 * for passing into Win32 functions that require such representation
	 */
	static void PipesToNulls(wchar_t* buffer, size_t length);
	static void PipesToNulls(wchar_t* buffer);

	static void TrimRight(std::string_view& view);
	static void TrimRight(std::wstring_view& view);
	static void TrimLeft(std::string_view& view);

	static std::string_view TrimRight(const std::string_view sv, const std::string_view chars);

	/**
	 * Expands placeholders to corresponding values provided in the map replacements.
	 * This method makes sure that values cannot contain other placeholders that may be replaced later on.
	 */
	[[nodiscard]] static CString ExpandPlaceholdersForCmd(const CString& input, const std::map<CString, CString>& replacements, CString (*escaper)(CString));
};


int strwildcmp(const char *wild, const char *string)
{
	const char* cp = nullptr;
	const char* mp = nullptr;
	while ((*string) && (*wild != '*'))
	{
		if ((*wild != *string) && (*wild != '?'))
			return 0;
		++wild;
		++string;
	}
	while (*string)
	{
		if (*wild == '*')
		{
			if (!*++wild)
				return 1;
			mp = wild;
			cp = string+1;
		}
		else if ((*wild == *string) || (*wild == '?'))
		{
			++wild;
			++string;
		}
		else
		{
			wild = mp;
			string = cp++;
		}
	}

	while (*wild == '*')
		++wild;
	return !*wild;
}

int wcswildcmp(const wchar_t *wild, const wchar_t *string)
{
	const wchar_t* cp = nullptr;
	const wchar_t* mp = nullptr;
	while ((*string) && (*wild != '*'))
	{
		if ((*wild != *string) && (*wild != '?'))
			return 0;
		++wild;
		++string;
	}
	while (*string)
	{
		if (*wild == '*')
		{
			if (!*++wild)
				return 1;
			mp = wild;
			cp = string+1;
		}
		else if ((*wild == *string) || (*wild == '?'))
		{
			++wild;
			++string;
		}
		else
		{
			wild = mp;
			string = cp++;
		}
	}

	while (*wild == '*')
		++wild;
	return !*wild;
}

void CStringUtils::RemoveAccelerators(CString& text)
{
	int pos = 0;
	while ((pos=text.Find('&',pos))>=0)
	{
		if (text.GetLength() > (pos-1))
		{
			if (text.GetAt(pos+1)!=' ')
				text.Delete(pos);
		}
		++pos;
	}
}

CString CStringUtils::EscapeAccellerators(CString& text)
{
	text.Replace(L"&", L"&&");
	return text;
}

wchar_t CStringUtils::GetAccellerator(const CString& text)
{
	int pos = 0;
	while ((pos = text.Find(L'&', pos)) >= 0 && pos + 1 < text.GetLength())
	{
		if (text.GetAt(pos + 1) == '&')
			++pos;
		else if (text.GetAt(pos + 1) != ' ')
			return towupper(text.GetAt(pos + 1));
		++pos;
	}
	return L'\0';
}

CString CStringUtils::EnsureCRLF(const CString& text)
{
	CString result;
	const int length = text.GetLength();
	for (int i = 0; i < length; ++i)
	{
		if (text[i] == L'\r' && (i == length - 1 || text[i + 1] != L'\n'))
			result += L"\r\n";
		else if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
			result += L"\r\n";
		else
			result += text[i];
	}
	return result;
}
bool CStringUtils::WriteAsciiStringToClipboard(const CStringA& sClipdata, const LCID lcid, const HWND hOwningWnd)
{
	CClipboardHelper clipboardHelper;
	if (!clipboardHelper.Open(hOwningWnd))
		return false;

	EmptyClipboard();
	HGLOBAL hClipboardData = CClipboardHelper::GlobalAlloc(sClipdata.GetLength() + 1);
	if (!hClipboardData)
		return false;

	auto pchData = static_cast<char*>(GlobalLock(hClipboardData));
	if (!pchData)
		return false;

	strcpy_s(pchData, sClipdata.GetLength() + 1, static_cast<LPCSTR>(sClipdata));
	GlobalUnlock(hClipboardData);
	if (!SetClipboardData(CF_TEXT, hClipboardData))
		return false;

	HANDLE hlocmem = CClipboardHelper::GlobalAlloc(sizeof(LCID));
	if (!hlocmem)
		return false;

	if (const auto plcid = static_cast<PLCID>(GlobalLock(hlocmem)))
	{
		*plcid = lcid;
		SetClipboardData(CF_LOCALE, plcid);
	}
	GlobalUnlock(hlocmem);

	return true;
}

bool CStringUtils::WriteAsciiStringToClipboard(const CStringW& sClipdata, const HWND hOwningWnd)
{
	CClipboardHelper clipboardHelper;
	if (!clipboardHelper.Open(hOwningWnd))
		return false;

	EmptyClipboard();
	HGLOBAL hClipboardData = CClipboardHelper::GlobalAlloc((sClipdata.GetLength() + 1) * sizeof(WCHAR));
	if (!hClipboardData)
		return false;

	auto pchData = static_cast<WCHAR*>(GlobalLock(hClipboardData));
	if (!pchData)
		return false;

	wcscpy_s(pchData, sClipdata.GetLength() + 1, static_cast<LPCWSTR>(sClipdata));
	GlobalUnlock(hClipboardData);
	if (!SetClipboardData(CF_UNICODETEXT, hClipboardData))
		return false;

	// no need to also set CF_TEXT : the OS does this
	// automatically.
	return true;
}

bool CStringUtils::WriteDiffToClipboard(const CStringA& sClipdata, const HWND hOwningWnd)
{
	UINT cFormat = RegisterClipboardFormat(L"TGIT_UNIFIEDDIFF");
	if (cFormat == 0)
		return false;
	CClipboardHelper clipboardHelper;
	if (!clipboardHelper.Open(hOwningWnd))
		return false;

	EmptyClipboard();
	HGLOBAL hClipboardData = CClipboardHelper::GlobalAlloc(sClipdata.GetLength() + 1);
	if (!hClipboardData)
		return false;

	auto pchData = static_cast<char*>(GlobalLock(hClipboardData));
	if (!pchData)
		return false;

	strcpy_s(pchData, sClipdata.GetLength() + 1, static_cast<LPCSTR>(sClipdata));
	GlobalUnlock(hClipboardData);
	if (!SetClipboardData(cFormat,hClipboardData))
		return false;
	if (!SetClipboardData(CF_TEXT, hClipboardData))
		return false;

	CString sClipdataW = CUnicodeUtils::GetUnicode<CString>(sClipdata);
	auto hClipboardDataW = CClipboardHelper::GlobalAlloc((sClipdataW.GetLength() + 1) * sizeof(wchar_t));
	if (!hClipboardDataW)
		return false;

	auto pchDataW = static_cast<wchar_t*>(GlobalLock(hClipboardDataW));
	if (!pchDataW)
		return false;

	wcscpy_s(pchDataW, sClipdataW.GetLength() + 1, static_cast<LPCWSTR>(sClipdataW));
	GlobalUnlock(hClipboardDataW);
	if (!SetClipboardData(CF_UNICODETEXT, hClipboardDataW))
		return false;

	return true;
}
bool CStringUtils::ReadStringFromTextFile(const CString& path, CString& text)
{
	if (!PathFileExists(path))
		return false;
	try
	{
		CStdioFile file;
		// w/o typeBinary for some files \r gets dropped
		if (!file.Open(path, CFile::typeBinary | CFile::modeRead | CFile::shareDenyWrite) || file.GetLength() >= INT_MAX)
			return false;

		CStringA filecontent;
		const UINT filelength = static_cast<UINT>(file.GetLength());
		const int bytesread = static_cast<int>(file.Read(filecontent.GetBuffer(filelength), filelength));
		filecontent.ReleaseBuffer(bytesread);
		text = CUnicodeUtils::GetUnicode<CString>(filecontent);
		file.Close();
	}
	catch (CFileException* pE)
	{
		text.Empty();
		pE->Delete();
		return false;
	}
	return true;
}
BOOL CStringUtils::WildCardMatch(const CString& wildcard, const CString& string)
{
	return wcswildcmp(wildcard, string);
}

CString CStringUtils::LinesWrap(const CString& longstring, const int limit /* = 80 */, const bool bCompactPaths /* = true */)
{
	CString retString;
	if ((longstring.GetLength() < limit) || (limit == 0))
		return longstring;  // no wrapping needed.
	// now start breaking the string into lines

	int linepos = 0;
	int lineposold = 0;
	CString temp;
	while ((linepos = longstring.Find('\n', linepos)) >= 0)
	{
		temp = longstring.Mid(lineposold, linepos-lineposold);
		if ((linepos+1)<longstring.GetLength())
			++linepos;
		else
			break;
		lineposold = linepos;
		if (!retString.IsEmpty())
			retString += L'\n';
		retString += WordWrap(temp, limit, bCompactPaths, false, 4);
	}
	temp = longstring.Mid(lineposold);
	if (!temp.IsEmpty())
		retString += L'\n';
	retString += WordWrap(temp, limit, bCompactPaths, false, 4);
	retString.Trim();
	return retString;
}

CString CStringUtils::WordWrap(const CString& longstring, int limit, const bool bCompactPaths, const bool bForceWrap, const int tabSize)
{
	int nLength = longstring.GetLength();
	CString retString;

	if (limit < 0)
		limit = 0;

	int nLineStart = 0;
	int nLineEnd = 0;
	int tabOffset = 0;
	for (int i = 0; i < nLength; ++i)
	{
		if (i-nLineStart+tabOffset >= limit)
		{
			if (nLineEnd == nLineStart)
			{
				if (bForceWrap)
					nLineEnd = i;
				else
				{
					while ((i < nLength) && (longstring[i] != ' ') && (longstring[i] != '\t'))
						++i;
					nLineEnd = i;
				}
			}
			if (bCompactPaths)
			{
				CString longline = longstring.Mid(nLineStart, nLineEnd-nLineStart).Left(MAX_PATH-1);
				if ((bCompactPaths)&&(longline.GetLength() < MAX_PATH))
				{
					if (((!PathIsFileSpec(longline))&&longline.Find(':')<3)||(PathIsURL(longline)))
					{
						auto buf = std::array<wchar_t, MAX_PATH>{};
						PathCompactPathEx(buf.data(), longline, limit+1, 0);
						longline = buf.data();
					}
				}
				retString += longline;
			}
			else
				retString += longstring.Mid(nLineStart, nLineEnd-nLineStart);
			retString += L'\n';
			tabOffset = 0;
			nLineStart = nLineEnd;
		}
		if (longstring[i] == ' ')
			nLineEnd = i;
		if (longstring[i] == '\t')
		{
			tabOffset += (tabSize - i % tabSize);
			nLineEnd = i;
		}
	}
	if (bCompactPaths)
	{
		CString longline = longstring.Mid(nLineStart).Left(MAX_PATH-1);
		if ((bCompactPaths)&&(longline.GetLength() < MAX_PATH))
		{
			if (((!PathIsFileSpec(longline))&&longline.Find(':')<3)||(PathIsURL(longline)))
			{
				auto buf = std::array<wchar_t, MAX_PATH>{};
				PathCompactPathEx(buf.data(), longline, limit+1, 0);
				longline = buf.data();
			}
		}
		retString += longline;
	}
	else
		retString += longstring.Mid(nLineStart);

	return retString;
}

std::vector<CString> CStringUtils::WordWrap(const CString& longstring, int limit, const int tabSize)
{
	int nLength = longstring.GetLength();
	std::vector<CString> retVec;

	if (limit < 0)
		limit = 0;

	int nLineStart = 0;
	int nLineEnd = 0;
	int tabOffset = 0;
	for (int i = 0; i < nLength; ++i)
	{
		if (i - nLineStart + tabOffset >= limit)
		{
			if (nLineEnd == nLineStart)
				nLineEnd = i;

			auto sMid = longstring.Mid(nLineStart, nLineEnd - nLineStart);
			retVec.push_back(sMid);

			tabOffset = 0;
			nLineStart = nLineEnd;
		}
		if (longstring[i] == ' ')
			nLineEnd = i;
		if (longstring[i] == '\t')
		{
			tabOffset += (tabSize - i % tabSize);
			nLineEnd = i;
		}
	}

	auto sMid = longstring.Mid(nLineStart);
	retVec.push_back(sMid);

	return retVec;
}

int CStringUtils::GetMatchingLength (const CString& lhs, const CString& rhs)
{
	const int lhsLength = lhs.GetLength();
	const int rhsLength = rhs.GetLength();
	const int maxResult = std::min(lhsLength, rhsLength);

	LPCWSTR pLhs = lhs;
	LPCWSTR pRhs = rhs;

	for (int i = 0; i < maxResult; ++i)
		if (pLhs[i] != pRhs[i])
			return i;

	return maxResult;
}

int CStringUtils::FastCompareNoCase (const CStringW& lhs, const CStringW& rhs)
{
	// attempt latin-only comparison

	INT_PTR count = std::min(lhs.GetLength(), rhs.GetLength()+1);
	const wchar_t* left = lhs;
	const wchar_t* right = rhs;
	for (const wchar_t* last = left + count+1; left < last; ++left, ++right)
	{
		int leftChar = *left;
		int rightChar = *right;

		int diff = leftChar - rightChar;
		if (diff != 0)
		{
			// case-sensitive comparison found a difference

			if ((leftChar | rightChar) >= 0x80)
			{
				// non-latin char -> fall back to CRT code
				// (full comparison required as we might have
				// skipped special chars / UTF plane selectors)

				return _wcsicmp (lhs, rhs);
			}

			// normalize to lower case

			if ((leftChar >= 'A') && (leftChar <= 'Z'))
				leftChar += 'a' - 'A';
			if ((rightChar >= 'A') && (rightChar <= 'Z'))
				rightChar += 'a' - 'A';

			// compare again

			diff = leftChar - rightChar;
			if (diff != 0)
				return diff;
		}
	}

	// must be equal (both ended with a 0)

	return 0;
}

bool CStringUtils::IsPlainReadableASCII(const CString& text)
{
	for (int i = 0; i < text.GetLength(); ++i)
	{
		if (text[i] < 32 || text[i] >= 127)
			return false;
	}
	return true;
}

auto cleanup_space(const std::wstring_view string) {
	return std::regex_replace(std::wstring{ string }, std::wregex(L"\\s+"), L" ");
}

void cleanup_space(CString& string)
{
	string = cleanup_space(std::wstring_view{ string }).c_str();
}

CString CStringUtils::UnescapeGitQuotePath(const std::wstring_view s)
{
	return UnescapeGitQuotePathA(CUnicodeUtils::StdGetUTF8(s));
}

CString CStringUtils::UnescapeGitQuotePathA(const std::string_view s)
{
	const int i_size = gsl::narrow<int>(s.size());
	CStringA t;
	t.Preallocate(i_size);
	bool isEscaped = false;
	for (int i = 0; i < i_size; ++i)
	{
		char c = s[i];
		if (isEscaped)
		{
			if (c >= '0' && c <= '3')
			{
				if (i + 2 < i_size)
				{
					c = (((c - '0') & 03) << 6) | (((s[i + 1] - '0') & 07) << 3) | ((s[i + 2] - '0') & 07);
					i += 2;
					t += c;
				}
			}
			else
			{
				// we're on purpose not supporting all possible abbreviations such as \n, \r, \t in this as these filenames are invalid on Windows anaway
				t += c;
			}
			isEscaped = false;
		}
		else
		{
			if (c == '\\')
				isEscaped = true;
			else if (c == '"')
				break;
			else
				t += c;
		}
	}
	return CUnicodeUtils::GetUnicode<CString>(t);
}

void get_sane_name(CString* out, const CString* name, const CString& email)
{
	const CString* src = name;
	if (name->GetLength() < 3 || 60 < name->GetLength() || wcschr(*name, L'@') || wcschr(*name, L'<') || wcschr(*name, L'>'))
		src = &email;
	else if (name == out)
		return;
	*out = *src;
}

void parse_bogus_from(const CString& mailaddress, CString& parsedAddress, CString* parsedName)
{
	/* John Doe <johndoe> */

	const int bra = mailaddress.Find(L'<');
	if (bra < 0)
		return;
	const int ket = mailaddress.Find(L'>');
	if (ket < 0)
		return;

	parsedAddress = mailaddress.Mid(bra + 1, ket - bra - 1);

	if (parsedName)
	{
		*parsedName = mailaddress.Left(bra).Trim();
		get_sane_name(parsedName, parsedName, parsedAddress);
	}
}

void CStringUtils::ParseEmailAddress(CString mailaddress, CString& parsedAddress, CString* parsedName)
{
	if (parsedName)
		parsedName->Empty();

	mailaddress = mailaddress.Trim();
	if (mailaddress.IsEmpty())
	{
		parsedAddress.Empty();
		return;
	}

	if (mailaddress.Left(1) == L'"')
	{
		mailaddress = mailaddress.TrimLeft();
		bool escaped = false;
		bool opened = true;
		for (int i = 1; i < mailaddress.GetLength(); ++i)
		{
			if (mailaddress[i] == L'"')
			{
				if (!escaped)
				{
					opened = !opened;
					if (!opened)
					{
						if (parsedName)
							*parsedName = mailaddress.Mid(1, i - 1);
						mailaddress = mailaddress.Mid(i);
						break;
					}
				}
				else
				{
					escaped = false;
					mailaddress.Delete(i - 1);
					--i;
				}
			}
			else if (mailaddress[i] == L'\\')
				escaped = !escaped;
		}
	}

	const auto buf = mailaddress.GetBuffer();
	auto at = wcschr(buf, L'@');
	if (!at)
	{
		parse_bogus_from(mailaddress, parsedAddress, parsedName);
		return;
	}

	/* Pick up the string around '@', possibly delimited with <>
	 * pair; that is the email part.
	 */
	while (at > buf)
	{
		auto c = at[-1];
		if (_istspace(c))
			break;
		if (c == L'<')
		{
			at[-1] = L' ';
			break;
		}
		at--;
	}

	mailaddress.ReleaseBuffer();
	const size_t el = wcscspn(at, L" \n\t\r\v\f>");
	parsedAddress = mailaddress.Mid(static_cast<int>(at - buf), static_cast<int>(el));
	mailaddress.Delete(static_cast<int>(at - buf), static_cast<int>(el + (at[el] ? 1 : 0)));

	/* The remainder is name.  It could be
	 *
	 * - "John Doe <john.doe@xz>"			(a), or
	 * - "john.doe@xz (John Doe)"			(b), or
	 * - "John (zzz) Doe <john.doe@xz> (Comment)"	(c)
	 *
	 * but we have removed the email part, so
	 *
	 * - remove extra spaces which could stay after email (case 'c'), and
	 * - trim from both ends, possibly removing the () pair at the end
	 *   (cases 'b' and 'c').
	 */
	cleanup_space(mailaddress);
	mailaddress.Trim();
	if (!mailaddress.IsEmpty() && ((mailaddress[0] == L'(' && mailaddress[mailaddress.GetLength() - 1] == L')') || (mailaddress[0] == L'"' && mailaddress[mailaddress.GetLength() - 1] == L'"')))
		mailaddress = mailaddress.Mid(1, mailaddress.GetLength() - 2);

	if (parsedName && parsedName->IsEmpty())
		get_sane_name(parsedName, &mailaddress, parsedAddress);
}

bool CStringUtils::StartsWith(const wchar_t* heystack, const CString& needle)
{
	return wcsncmp(heystack, needle, needle.GetLength()) == 0;
}

bool CStringUtils::EndsWith(const CString& heystack, const wchar_t* needle)
{
	const auto lenNeedle = wcslen(needle);
	const auto lenHeystack = static_cast<size_t>(heystack.GetLength());
	if (lenNeedle > lenHeystack)
		return false;
	return wcsncmp(static_cast<LPCWSTR>(heystack) + (lenHeystack - lenNeedle), needle, lenNeedle) == 0;
}

bool CStringUtils::EndsWith(const CString& heystack, const wchar_t needle)
{
	const auto lenHeystack = heystack.GetLength();
	if (!lenHeystack)
		return false;
	return *(static_cast<LPCWSTR>(heystack) + (lenHeystack - 1)) == needle;
}

bool CStringUtils::EndsWithI(const CString& heystack, const wchar_t* needle)
{
	const auto lenNeedle = wcslen(needle);
	const auto lenHeystack = static_cast<size_t>(heystack.GetLength());
	if (lenNeedle > lenHeystack)
		return false;
	return _wcsnicmp(static_cast<LPCWSTR>(heystack) + (lenHeystack - lenNeedle), needle, lenNeedle) == 0;
}

bool CStringUtils::StartsWithI(const wchar_t* heystack, const CString& needle)
{
	return _wcsnicmp(heystack, needle, needle.GetLength()) == 0;
}

bool CStringUtils::WriteStringToTextFile(const LPCWSTR path, const LPCWSTR text, const bool bUTF8 /* = true */)
{
	return WriteStringToTextFile(static_cast<const std::wstring&>(path), std::wstring_view(text), bUTF8);
}


bool CStringUtils::StartsWith(const wchar_t* heystack, const wchar_t* needle)
{
	return wcsncmp(heystack, needle, wcslen(needle)) == 0;
}

bool CStringUtils::StartsWith(const char* heystack, const char* needle)
{
	return strncmp(heystack, needle, strlen(needle)) == 0;
}

bool CStringUtils::WriteStringToTextFile(const std::wstring& path, const std::wstring_view text, const bool bUTF8 /* = true */)
{
	DWORD dwWritten = 0;
	RIAA::CAutoFile hFile = CreateFile(path.c_str(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!hFile)
		return false;

	if (bUTF8)
	{
		std::string buf = CUnicodeUtils::StdGetUTF8(text);
		if (!WriteFile(hFile, buf.c_str(), static_cast<DWORD>(buf.length()), &dwWritten, nullptr))
		{
			return false;
		}
	}
	else
	{
		if (!WriteFile(hFile, text.data(), static_cast<DWORD>(text.length() * sizeof(wchar_t)), &dwWritten, nullptr))
		{
			return false;
		}
	}
	return true;
}

void PipeToNull(wchar_t* ptr)
{
	if (*ptr == '|')
		*ptr = '\0';
}

void CStringUtils::PipesToNulls(wchar_t* buffer, const size_t length)
{
	wchar_t* ptr = buffer + length;
	while (ptr != buffer)
	{
		PipeToNull(ptr);
		ptr--;
	}
}

void CStringUtils::PipesToNulls(wchar_t* buffer)
{
	wchar_t* ptr = buffer;
	while (*ptr)
	{
		PipeToNull(ptr);
		++ptr;
	}
}

void CStringUtils::TrimLeft(std::string_view& view)
{
	while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front())))
		view.remove_prefix(1);
}

void CStringUtils::TrimRight(std::string_view& view)
{
	while (!view.empty() && std::isspace(static_cast<unsigned char>(view.back())))
		view.remove_suffix(1);
}

void CStringUtils::TrimRight(std::wstring_view& view)
{
	while (!view.empty() && std::iswspace(static_cast<wchar_t>(view.back())))
		view.remove_suffix(1);
}

std::string_view CStringUtils::TrimRight(const std::string_view sv, const std::string_view chars)
{
	const auto pos = sv.find_last_not_of(chars);

	if (pos == std::string_view::npos)
		return {};

	return sv.substr(0, pos + 1);
}

CString CStringUtils::ExpandPlaceholdersForCmd(const CString& input, const std::map<CString, CString>& values, CString (*escaper)(CString))
{
	CString result;
	const int len = input.GetLength();
	for (int i = 0; i < len; ++i)
	{
		if (input[i] != L'%')
		{
			result += input[i];
			continue;
		}

		const int start = i + 1;
		int j = start;
		while (j < len && std::iswalnum(input[j]))
			++j;

		if (j == start)
		{
			result += L'%';
			continue;
		}

		CString key = input.Mid(start, j - start);
		if (auto it = values.find(key); it != values.end())
		{
			if (i > 0 && input.GetAt(i - 1) == L'"' && j < len && input.GetAt(j) == L'"')
			{
				result.Truncate(result.GetLength() - 1);
				++i;
			}
			result += escaper(it->second);
			i += j - start;
			continue;
		}

		result += L'%';
		result += key;

		i = j - 1;
	}

	return result;
}

// algorithm based on https://learn.microsoft.com/en-gb/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
// also cf. https://learn.microsoft.com/en-us/cpp/cpp/main-function-command-line-args
std::wstring CStringUtils::EscapeWindowsCliArgument(const std::wstring_view argument)
{
	std::wstring result;
	result.reserve(argument.size() + 2);
	result += L'"';

	const size_t length = argument.size();

	for (size_t i = 0;; ++i)
	{
		size_t numberBackslashes = 0;

		// Count consecutive backslashes
		while (i < length && argument[i] == L'\\')
		{
			++i;
			++numberBackslashes;
		}

		// End of string
		if (i >= length)
		{
			// Escape all backslashes, but let the terminating double quotation mark we add below be interpreted as a metacharacter.
			result.append(numberBackslashes * 2, L'\\');
			break;
		}
		else if (argument[i] == L'"') // Escape all backslashes and the following double quotation mark.
			result.append(numberBackslashes * 2 + 1, L'\\');
		else // Backslashes aren't special here.
			result.append(numberBackslashes, L'\\');
		result += argument[i];
	}

	result += L'"';

	return result;
}

bool CStringUtils::NeedsWindowsCliQuoting(const std::wstring_view argument)
{
	// An empty element has to be quoted, or it disappears from the round trip.
	return argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
}

std::wstring CStringUtils::EscapePosixShellArgument(const std::wstring_view argument)
{
	// Single quotes suppress every expansion bash performs, and the only thing
	// that cannot appear between them is a single quote - so close, escape it
	// outside, and reopen.
	std::wstring result;
	result.reserve(argument.size() + 2);
	result += L'\'';
	for (const wchar_t c : argument)
	{
		if (c == L'\'')
			result += L"'\\''";
		else
			result += c;
	}
	result += L'\'';

	return result;
}

bool CStringUtils::NeedsPosixShellQuoting(const std::wstring_view argument)
{
	if (argument.empty())
		return true;
	for (const wchar_t c : argument)
	{
		if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.' || c == L'/' || c == L'=' || c == L':' || c == L',' || c == L'+'))
			return true;
	}
	return false;
}


CString CStringUtils::EscapeWindowsCliArguments(const CString& argument)
{
	const std::wstring escaped = EscapeWindowsCliArgument(std::wstring_view(argument, argument.GetLength()));
	return CString(escaped.data(), static_cast<int>(escaped.size()));
}

}

export using namespace StringUtils;
