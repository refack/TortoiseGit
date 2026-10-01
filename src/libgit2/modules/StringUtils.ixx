module;
#include <afx.h>
#include <atlstr.h>


export module StringUtils;
import std;
import gsl;
import wil;
import wstr;
import RIAA;


// Functions std:: already does. Each names its std:: spelling; they stay only
// until their call sites move to it.
export namespace StringUtils
{
using std::string_view;


// std: std::views::split (+ std::from_chars for numeric containers)
// append = true as the default: a default value should never lose data!
template <typename Container, tgit::wstr::IsRealString StrT, typename CHARTYPE = StrT::value_type>
void stringtok(
	Container& container,
	const StrT& in,
	const bool trim,
	const CHARTYPE* const delimiters,
	const bool append = true
)
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
			if (i == StrT::npos)
				return; // nothing left but white space
		}

		// find the end of the token
		const auto j = in.find_first_of(delimiters, i);

		// push token
		if (j == StrT::npos)
		{
			if constexpr (std::is_same_v<typename Container::value_type, StrT>)
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
			if constexpr (std::is_same_v<typename Container::value_type, StrT>)
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


// std: std::regex_replace(text, std::wregex(L"\r\n|\r|\n"), L"\r\n")
CString EnsureCRLF(const CString& text)
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


/**
 * Find and return the number n of starting characters equal between
 * \ref lhs and \ref rhs. (max n: lhs.Left(n) == rhs.Left(n))
 */
// std: std::ranges::mismatch(lhs, rhs).in1 - lhs.begin()
int GetMatchingLength (const CString& lhs, const CString& rhs)
{
	const int lhsLength = lhs.GetLength();
	const int rhsLength = rhs.GetLength();
	const int maxResult = std::min(lhsLength, rhsLength);

	const LPCWSTR pLhs = lhs;
	const LPCWSTR pRhs = rhs;

	for (int i = 0; i < maxResult; ++i)
		if (pLhs[i] != pRhs[i])
			return i;

	return maxResult;
}


// std: std::ranges::all_of(text, [](wchar_t c) { return c >= 32 && c < 127; })
bool IsPlainReadableASCII(const CString& text)
{
	for (int i = 0; i < text.GetLength(); ++i)
	{
		if (text[i] < 32 || text[i] >= 127)
			return false;
	}
	return true;
}


// std: basic_string_view::starts_with / ends_with
bool StartsWith(const wchar_t* heystack, const CString& needle)
{
	return wcsncmp(heystack, needle, needle.GetLength()) == 0;
}

bool StartsWith(const wchar_t* heystack, const wchar_t* needle)
{
	return wcsncmp(heystack, needle, wcslen(needle)) == 0;
}

bool StartsWith(const char* heystack, const char* needle)
{
	return strncmp(heystack, needle, strlen(needle)) == 0;
}

bool EndsWith(const CString& heystack, const wchar_t* needle)
{
	const auto lenNeedle = wcslen(needle);
	const auto lenHeystack = static_cast<size_t>(heystack.GetLength());
	if (lenNeedle > lenHeystack)
		return false;
	return wcsncmp(static_cast<LPCWSTR>(heystack) + (lenHeystack - lenNeedle), needle, lenNeedle) == 0;
}

bool EndsWith(const CString& heystack, const wchar_t needle)
{
	const auto lenHeystack = heystack.GetLength();
	if (!lenHeystack)
		return false;
	return *(static_cast<LPCWSTR>(heystack) + (lenHeystack - 1)) == needle;
}

// std::wstring / std::wstring_view variants. Constrained rather than taking a
// plain std::wstring_view haystack: that would make EndsWith(L"literal", ...)
// ambiguous against the CString overloads (one user-defined conversion each).
template <tgit::wstr::IsRealString TStr>
	requires std::same_as<typename TStr::value_type, wchar_t>
bool EndsWith(const TStr& heystack, const std::wstring_view needle) noexcept
{
	return std::wstring_view(heystack).ends_with(needle);
}

template <tgit::wstr::IsRealString TStr>
	requires std::same_as<typename TStr::value_type, wchar_t>
bool EndsWith(const TStr& heystack, const wchar_t needle) noexcept
{
	return std::wstring_view(heystack).ends_with(needle);
}


// std: std::replace(buffer, buffer + length, L'|', L'\0')
void PipeToNull(wchar_t* ptr)
{
	if (*ptr == '|')
		*ptr = '\0';
}


/**
 * Replace all pipe (|) character in the string with a nullptr character. Used
 * for passing into Win32 functions that require such representation
 */
void PipesToNulls(wchar_t* buffer, const size_t length)
{
	wchar_t* ptr = buffer + length;
	while (ptr != buffer)
	{
		PipeToNull(ptr);
		ptr--;
	}
}

/// Up to the terminating NUL. This was a default argument `length = 0` on the
/// overload above, which made every one-argument call a no-op: that loop starts
/// at buffer + 0 and stops at buffer. TortoiseSVN had the two as overloads.
void PipesToNulls(wchar_t* buffer)
{
	std::replace(buffer, buffer + wcslen(buffer), L'|', L'\0');
}


// std: view.substr(view.find_first_not_of(...)) / view.substr(0, view.find_last_not_of(...) + 1)
void TrimLeft(std::string_view& view)
{
	while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front())))
		view.remove_prefix(1);
}

void TrimRight(std::string_view& view)
{
	while (!view.empty() && std::isspace(static_cast<unsigned char>(view.back())))
		view.remove_suffix(1);
}

void TrimRight(std::wstring_view& view)
{
	while (!view.empty() && std::iswspace(view.back()))
		view.remove_suffix(1);
}

std::string_view TrimRight(const std::string_view sv, const std::string_view chars)
{
	const auto pos = sv.find_last_not_of(chars);

	if (pos == std::string_view::npos)
		return {};

	return sv.substr(0, pos + 1);
}


/**
 * Reads the string \text from the file \path in utf8 encoding.
 */
// std: std::ifstream(path, std::ios::binary) + CUnicodeUtils
bool ReadStringFromTextFile(const CString& path, CString& text)
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


/**
 * Writes the string \text to the file \path, either in utf16 or utf8 encoding,
 * depending on the \c bUTF8 param.
 */
// std: std::ofstream(path, std::ios::binary) + CUnicodeUtils, modulo FILE_SHARE_DELETE
bool WriteStringToTextFile(const std::wstring_view path, const std::wstring_view text, const bool bUTF8 = true)
{
	DWORD dwWritten = 0;
	const RIAA::CAutoFile hFile = CreateFile(path.data(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!hFile)
		return false;

	if (bUTF8)
	{
		const std::string buf = CUnicodeUtils::StdGetUTF8(text);
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
bool WriteStringToTextFile(const LPCWSTR path, const LPCWSTR text, const bool bUTF8 = true)
{
	return WriteStringToTextFile(std::wstring_view{path}, std::wstring_view{text}, bUTF8);
}

}

// Functionality Splitter

// The extras: what std:: has no spelling for - Win32 UI and clipboard,
// case-insensitive compare, git's quoting and From: conventions, and
// command-line quoting.
export namespace StringUtils
{


// int strwildcmp(const char *wild, const char *string)
// {
// 	const char* cp = nullptr;
// 	const char* mp = nullptr;
// 	while ((*string) && (*wild != '*'))
// 	{
// 		if ((*wild != *string) && (*wild != '?'))
// 			return 0;
// 		++wild;
// 		++string;
// 	}
// 	while (*string)
// 	{
// 		if (*wild == '*')
// 		{
// 			if (!*++wild)
// 				return 1;
// 			mp = wild;
// 			cp = string+1;
// 		}
// 		else if ((*wild == *string) || (*wild == '?'))
// 		{
// 			++wild;
// 			++string;
// 		}
// 		else
// 		{
// 			wild = mp;
// 			string = cp++;
// 		}
// 	}
//
// 	while (*wild == '*')
// 		++wild;
// 	return !*wild;
// }
//

/**
 * Removes all '&' chars from a string.
 */
void RemoveAccelerators(CString& text)
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

/**
 * Escapes all '&' chars from a string with another '&'.
 */
CString EscapeAccellerators(CString& text)
{
	text.Replace(L"&", L"&&");
	return text;
}

/**
 * Returns the accellerator used in the string or \0
 */
wchar_t GetAccellerator(const CString& text)
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


/**
 * Writes an ASCII CString to the clipboard in CF_TEXT format
 */
bool WriteAsciiStringToClipboard(const CStringA& sClipdata, const LCID lcid, const HWND hOwningWnd)
{
	// Not an if-init-statement: the clipboard has to stay open until we return.
	const auto clipboard = wil::open_clipboard_retrying(hOwningWnd);
	if (!clipboard)
		return false;

	EmptyClipboard();
	auto text = wil::make_moveable_hglobal(std::string{ tgit::wstr::StringView{ sClipdata } });
	if (!text || !SetClipboardData(CF_TEXT, text.get()))
		return false;
	text.release(); // the clipboard owns it now

	// SetClipboardData takes the handle; this used to pass the GlobalLock'ed
	// pointer, which is a different value for a GMEM_MOVEABLE block.
	if (auto locale = wil::make_moveable_hglobal(std::as_bytes(std::span(&lcid, 1))); locale && SetClipboardData(CF_LOCALE, locale.get()))
		locale.release();

	return true;
}


/**
 * Writes a String to the clipboard in both CF_UNICODETEXT and CF_TEXT format
 */
bool WriteAsciiStringToClipboard(const CStringW& sClipdata, const HWND hOwningWnd)
{
	const auto clipboard = wil::open_clipboard_retrying(hOwningWnd);
	if (!clipboard)
		return false;

	EmptyClipboard();
	auto text = wil::make_moveable_hglobal(std::wstring{ tgit::wstr::StringView{ sClipdata } });
	if (!text || !SetClipboardData(CF_UNICODETEXT, text.get()))
		return false;
	text.release(); // the clipboard owns it now

	// no need to also set CF_TEXT : the OS does this
	// automatically.
	return true;
}


/**
* Writes an ASCII CString to the clipboard in TGIT_UNIFIEDDIFF format, which is basically the patch file
* as a ASCII string.
*/
bool WriteDiffToClipboard(const CStringA& sClipdata, const HWND hOwningWnd)
{
	const UINT cFormat = RegisterClipboardFormat(L"TGIT_UNIFIEDDIFF");
	if (cFormat == 0)
		return false;
	const auto clipboard = wil::open_clipboard_retrying(hOwningWnd);
	if (!clipboard)
		return false;

	EmptyClipboard();
	auto diff = wil::make_moveable_hglobal(std::string{ tgit::wstr::StringView{ sClipdata } });
	if (!diff || !SetClipboardData(cFormat, diff.get()))
		return false;
	// The same block is also offered as CF_TEXT, so the clipboard owns it from
	// the first successful SetClipboardData on.
	const HGLOBAL diffOwnedByClipboard = diff.release();
	if (!SetClipboardData(CF_TEXT, diffOwnedByClipboard))
		return false;

	const std::wstring sClipdataW = CUnicodeUtils::StdGetUnicode(std::string_view(sClipdata.GetString(), sClipdata.GetLength()));
	auto textW = wil::make_moveable_hglobal(sClipdataW);
	if (!textW || !SetClipboardData(CF_UNICODETEXT, textW.get()))
		return false;
	textW.release();

	return true;
}


CString WordWrap(const CString& longstring, int limit, const bool bCompactPaths, const bool bForceWrap, const int tabSize)
{
	const int nLength = longstring.GetLength();
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
				if (longline.GetLength() < MAX_PATH)
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
		if (longline.GetLength() < MAX_PATH)
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

std::vector<CString> WordWrap(const CString& longstring, int limit, const int tabSize)
{
	const int nLength = longstring.GetLength();
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

	const auto sMid = longstring.Mid(nLineStart);
	retVec.push_back(sMid);

	return retVec;
}


CString LinesWrap(const CString& longstring, const int limit /* = 80 */, const bool bCompactPaths /* = true */)
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


/**
 * Optimizing wrapper around CompareNoCase.
 */
int FastCompareNoCase (const CStringW& lhs, const CStringW& rhs)
{
	// attempt latin-only comparison

	const INT_PTR count = std::min(lhs.GetLength(), rhs.GetLength()+1);
	const wchar_t* left = lhs;
	const wchar_t* right = rhs;
	for (const wchar_t* last = left + count+1; left < last; ++left, ++right)
	{
		int leftChar = *left;
		int rightChar = *right;

		if (int diff = leftChar - rightChar; diff != 0)
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


// Case-insensitive prefix/suffix tests: std:: has no locale-free spelling.
bool StartsWithI(const wchar_t* heystack, const CString& needle)
{
	return _wcsnicmp(heystack, needle, needle.GetLength()) == 0;
}

bool EndsWithI(const CString& heystack, const wchar_t* needle)
{
	const auto lenNeedle = wcslen(needle);
	const auto lenHeystack = static_cast<size_t>(heystack.GetLength());
	if (lenNeedle > lenHeystack)
		return false;
	return _wcsnicmp(static_cast<LPCWSTR>(heystack) + (lenHeystack - lenNeedle), needle, lenNeedle) == 0;
}

// Constrained for the same reason as the EndsWith templates above.
template <tgit::wstr::IsRealString TStr>
	requires std::same_as<typename TStr::value_type, wchar_t>
bool EndsWithI(const TStr& heystack, const std::wstring_view needle) noexcept
{
	const std::wstring_view h(heystack);
	return h.size() >= needle.size() && _wcsnicmp(h.data() + (h.size() - needle.size()), needle.data(), needle.size()) == 0;
}


/**
 * Unescapes Git quoted filenames
 * This is not a full implementation of the unescaper as we skip some conversions that will result in invalid filenames.
 */
CString UnescapeGitQuotePathA(const std::string_view s)
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
CString UnescapeGitQuotePath(const std::wstring_view s)
{
	return UnescapeGitQuotePathA(CUnicodeUtils::StdGetUTF8(s));
}


struct EmailAddress
{
	std::wstring address;
	std::wstring name;
};

/**
 * Splits a From:-style string into address and display name, best effort.
 *
 * Handles what git's mailinfo.c handle_from() does, which this used to be a
 * line-by-line port of:
 *   John Doe <john.doe@xz>                   "John Doe" <john.doe@xz>
 *   john.doe@xz (John Doe)                   <john.doe@xz> "John Doe"
 *   John (zzz) Doe <john.doe@xz> (Comment)   John Doe <johndoe>   (no '@')
 * A leading quoted name is taken verbatim, with \" unescaped. Any other name
 * falls back to the address when it does not look like a name - under 3 or
 * over 60 characters, or containing @, < or > - which is git's get_sane_name().
 * Not an RFC 5322 parser: no groups, no encoded words, no nested comments.
 */
EmailAddress ParseEmailAddress(const std::wstring_view input)
{
	static const std::wregex quotedName(LR"re(^\s*"((?:[^"\\]|\\.)*)"([\s\S]*)$)re");
	static const std::wregex escapedQuote(LR"(\\")");
	static const std::wregex atToken(LR"(<?([^\s<]*@[^\s>]*)>?)");
	static const std::wregex bracketed(LR"(^([\s\S]*?)<([^>]*)>)");
	static const std::wregex whitespaceRun(LR"(\s+)");

	const auto trim = [](std::wstring s) {
		tgit::wstr::Trim(s);
		return s;
	};
	const auto saneName = [](std::wstring name, const std::wstring& address) -> std::wstring {
		if (name.size() >= 3 && name.size() <= 60 && name.find_first_of(L"@<>") == std::wstring::npos)
			return name;
		return address;
	};

	EmailAddress result;
	std::wstring rest = trim(std::wstring(input));
	if (rest.empty())
		return result;

	std::wstring quoted;
	if (std::wsmatch m; std::regex_match(rest, m, quotedName))
	{
		quoted = std::regex_replace(m[1].str(), escapedQuote, L"\"");
		rest = m[2].str();
	}

	if (std::wsmatch m; std::regex_search(rest, m, atToken))
	{
		result.address = m[1].str();
		rest = m.prefix().str() + L' ' + m.suffix().str();
	}
	else if (std::regex_search(rest, m, bracketed))
	{
		// "John Doe <johndoe>": an address without an '@'
		result.address = m[2].str();
		rest = m[1].str();
	}
	else
		return { .address = {}, .name = quoted };

	if (!quoted.empty())
	{
		result.name = std::move(quoted);
		return result;
	}

	// what is left is the name, possibly wrapped in () or "" (cases "john.doe@xz (John Doe)" and "<john.doe@xz> \"John Doe\"")
	rest = trim(std::regex_replace(rest, whitespaceRun, L" "));
	if (rest.size() >= 2 && ((rest.front() == L'(' && rest.back() == L')') || (rest.front() == L'"' && rest.back() == L'"')))
		rest = rest.substr(1, rest.size() - 2);
	result.name = saneName(std::move(rest), result.address);
	return result;
}


/**
 * Expands placeholders to corresponding values provided in the map replacements.
 * This method makes sure that values cannot contain other placeholders that may be replaced later on.
 */
CString ExpandPlaceholdersForCmd(const CString& input, const std::map<CString, CString>& values, CString (*escaper)(CString))
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


/**
 * Quotes one argv element the way CommandLineToArgvW / the MSVC runtime read
 * it back. Always adds the surrounding quotes; see \ref NeedsWindowsCliQuoting
 * for the "only when it matters" test the argv serializer applies first.
 *
 * Flavor-neutral on purpose: this is the building block CGit::SerializeArgv
 * needs, and CGit is on the list of sources the ATL side compiles too.
 */
// algorithm based on https://learn.microsoft.com/en-gb/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
// also cf. https://learn.microsoft.com/en-us/cpp/cpp/main-function-command-line-args
std::wstring EscapeWindowsCliArgument(const std::wstring_view argument)
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


/**
 * True when \a argument would not survive the round trip unquoted. An empty
 * element must be quoted (otherwise it vanishes); whitespace and quotes are
 * the only other characters CommandLineToArgvW treats as structure.
 *
 * A lone backslash needs nothing: backslashes are only special immediately
 * before a quote, and there is no quote here to be before.
 */
bool NeedsWindowsCliQuoting(const std::wstring_view argument)
{
	// An empty element has to be quoted, or it disappears from the round trip.
	return argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
}


/**
 * Quotes one argv element for a POSIX shell, which is what the msys2/cygwin
 * path needs: CGit writes the whole command line into a temp file and hands
 * it to bash.exe, so there the "command line" is shell *source*.
 */
std::wstring EscapePosixShellArgument(const std::wstring_view argument)
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


/**
 * True unless \a argument is made entirely of characters no shell touches.
 * Deliberately a whitelist: a blacklist of bash metacharacters is a list you
 * are always one shell feature behind on.
 */
bool NeedsPosixShellQuoting(const std::wstring_view argument)
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


CString EscapeWindowsCliArguments(const CString& argument)
{
	const std::wstring escaped = EscapeWindowsCliArgument(std::wstring_view(argument, argument.GetLength()));
	return CString(escaped.data(), static_cast<int>(escaped.size()));
}

}

export using namespace StringUtils;