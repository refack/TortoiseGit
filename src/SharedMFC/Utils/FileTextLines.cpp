// TortoiseGitMerge - a Diff/Patch program

// Copyright (C) 2016, 2019, 2021, 2023, 2025 - TortoiseGit
// Copyright (C) 2007-2016, 2019 - TortoiseSVN

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

/// FIRST
#include <afx.h>
/// FIRST

// the four IDS_ERR_FILE_* strings are shared: this file is compiled into TortoiseProc,
// TortoiseGitBlame and the unit tests, which each have their own resource.h
#include <../../libgit2/Resources/LoglistCommonResource.h>
#include "Utils/FileTextLines.h"
import RIAA;

#include <gsl/narrow>
#include <Shlwapi.h>

import std;
import wil;
import Registry;
import str_utils;

CFileTextLines::UnicodeType CFileTextLines::CheckUnicodeType(LPCVOID pBuffer, int cb)
{
	if (cb < 2)
		return CFileTextLines::UnicodeType::ASCII;
	auto pVal32 = static_cast<const UINT32*>(pBuffer);
	auto pVal16 = static_cast<const UINT16*>(pBuffer);
	auto pVal8 = static_cast<const UINT8*>(pBuffer);
	// scan the whole buffer for a 0x00000000 sequence
	// if found, we assume a binary file
	int nDwords = cb/4;
	for (int j=0; j<nDwords; ++j)
	{
		if (0x00000000 == pVal32[j])
			return CFileTextLines::UnicodeType::BINARY;
	}
	if (cb >=4 )
	{
		if (*pVal32 == 0x0000FEFF)
		{
			return CFileTextLines::UnicodeType::UTF32_LE;
		}
		if (*pVal32 == 0xFFFE0000)
		{
			return CFileTextLines::UnicodeType::UTF32_BE;
		}
	}
	if (*pVal16 == 0xFEFF)
	{
		return CFileTextLines::UnicodeType::UTF16_LEBOM;
	}
	if (*pVal16 == 0xFFFE)
	{
		return CFileTextLines::UnicodeType::UTF16_BEBOM;
	}
	if (cb < 3)
		return CFileTextLines::UnicodeType::ASCII;
	if (*pVal16 == 0xBBEF)
	{
		if (pVal8[2] == 0xBF)
			return CFileTextLines::UnicodeType::UTF8BOM;
	}
	// check for illegal UTF8 sequences
	bool bNonANSI = false;
	int nNeedData = 0;
	int i=0;
	int nullcount = 0;
	for (; i < cb; ++i)
	{
		if (pVal8[i] == 0)
		{
			++nullcount;
			// count the null chars, we do not want to treat an ASCII/UTF8 file
			// as UTF16 just because of some null chars that might be accidentally
			// in the file.
			// Use an arbitrary value of one fiftieth of the file length as
			// the limit after which a file is considered UTF16.
			if (nullcount >(cb / 50))
			{
				// null-chars are not allowed for ASCII or UTF8, that means
				// this file is most likely UTF16 encoded
				if (i % 2)
					return CFileTextLines::UnicodeType::UTF16_LE;
				else
					return CFileTextLines::UnicodeType::UTF16_BE;
			}
		}
		if ((pVal8[i] & 0x80) != 0) // non ASCII
		{
			bNonANSI = true;
			break;
		}
	}
	// check remaining text for UTF-8 validity
	for (; i<cb; ++i)
	{
		UINT8 zChar = pVal8[i];
		if ((zChar & 0x80)==0) // Ascii
		{
			if (zChar == 0)
			{
				++nullcount;
				// count the null chars, we do not want to treat an ASCII/UTF8 file
				// as UTF16 just because of some null chars that might be accidentally
				// in the file.
				// Use an arbitrary value of one fiftieth of the file length as
				// the limit after which a file is considered UTF16.
				if (nullcount > (cb / 50))
				{
					// null-chars are not allowed for ASCII or UTF8, that means
					// this file is most likely UTF16 encoded
					if (i%2)
						return CFileTextLines::UnicodeType::UTF16_LE;
					else
						return CFileTextLines::UnicodeType::UTF16_BE;
				}
				nNeedData = 0;
			}
			else if (nNeedData)
			{
				return CFileTextLines::UnicodeType::ASCII;
			}
			continue;
		}
		if ((zChar & 0x40)==0) // top bit
		{
			if (!nNeedData)
				return CFileTextLines::UnicodeType::ASCII;
			--nNeedData;
		}
		else if (nNeedData)
		{
			return CFileTextLines::UnicodeType::ASCII;
		}
		else if ((zChar & 0x20)==0) // top two bits
		{
			if (zChar<=0xC1)
				return CFileTextLines::UnicodeType::ASCII;
			nNeedData = 1;
		}
		else if ((zChar & 0x10)==0) // top three bits
		{
			nNeedData = 2;
		}
		else if ((zChar & 0x08)==0) // top four bits
		{
			if (zChar>=0xf5)
				return CFileTextLines::UnicodeType::ASCII;
			nNeedData = 3;
		}
		else
			return CFileTextLines::UnicodeType::ASCII;
	}
	if (bNonANSI && nNeedData==0)
		// if get here thru nonAscii and no missing data left then its valid UTF8
		return CFileTextLines::UnicodeType::UTF8;
	if (!bNonANSI && (DWORD(CRegDWORD(L"Software\\TortoiseGitMerge\\UseUTF8", FALSE))))
		return CFileTextLines::UnicodeType::UTF8;
	return CFileTextLines::UnicodeType::ASCII;
}


BOOL CFileTextLines::Load(const CString& sFilePath, int /*lengthHint*/ /* = 0*/)
{
	m_SaveParams.m_LineEndings = EOL::AutoLine;
	if (!m_bKeepEncoding)
		m_SaveParams.m_UnicodeType = CFileTextLines::UnicodeType::AUTOTYPE;
	RemoveAll();

	if (PathIsDirectory(sFilePath))
	{
		m_sErrorString.Format(IDS_ERR_FILE_NOTAFILE, static_cast<LPCWSTR>(sFilePath));
		return FALSE;
	}

	if (!PathFileExists(sFilePath))
	{
		//file does not exist, so just return SUCCESS
		return TRUE;
	}

	CAutoFile hFile = CreateFile(sFilePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (!hFile)
	{
		SetErrorString();
		return FALSE;
	}

	LARGE_INTEGER fsize;
	if (!GetFileSizeEx(hFile, &fsize))
	{
		SetErrorString();
		return FALSE;
	}
	if (fsize.QuadPart >= std::numeric_limits<int>::max())
	{
		// file is way too big for us
		m_sErrorString.LoadString(IDS_ERR_FILE_TOOBIG);
		return FALSE;
	}

	// Read the file straight into its buffer: resize_and_overwrite allocates without
	// zero-filling, hands the storage to ReadFile, and keeps exactly what was read.
	std::string fileBytes;
	DWORD readError = ERROR_SUCCESS;
	try
	{
		fileBytes.resize_and_overwrite(fsize.LowPart, [&](char* const buffer, const size_t capacity) {
			DWORD dwReadBytes = 0;
			if (!ReadFile(hFile, buffer, static_cast<DWORD>(capacity), &dwReadBytes, nullptr))
			{
				readError = GetLastError();
				return size_t{ 0 };
			}
			return static_cast<size_t>(dwReadBytes);
		});
	}
	catch (CMemoryException* e) // MFC's new handler throws this rather than std::bad_alloc
	{
		e->GetErrorMessage(CStrBuf(m_sErrorString, 1000), 1000);
		e->Delete();
		return FALSE;
	}
	catch (const std::bad_alloc&)
	{
		m_sErrorString.LoadString(IDS_ERR_FILE_TOOBIG);
		return FALSE;
	}
	if (readError != ERROR_SUCCESS)
	{
		SetLastError(readError); // SetErrorString reports GetLastError()
		SetErrorString();
		return FALSE;
	}
	hFile.CloseHandle();

	// detect type
	if (m_SaveParams.m_UnicodeType == CFileTextLines::UnicodeType::AUTOTYPE)
	{
		m_SaveParams.m_UnicodeType = this->CheckUnicodeType(fileBytes.data(), gsl::narrow<int>(fileBytes.size()));
	}
	// enforce conversion for all but ASCII and UTF8 type
	m_bNeedsConversion = (m_SaveParams.m_UnicodeType != CFileTextLines::UnicodeType::UTF8) && (m_SaveParams.m_UnicodeType != CFileTextLines::UnicodeType::ASCII);

	// no need to decode empty file
	if (fileBytes.empty())
		return TRUE;

	// we may have to convert the file content - CString is UTF16LE
	std::unique_ptr<CDecodeFilter> pFilter;
	std::optional<std::wstring> decoded;
	try
	{
		switch (m_SaveParams.m_UnicodeType)
		{
		case UnicodeType::BINARY:
			m_sErrorString.Format(IDS_ERR_FILE_BINARY, static_cast<LPCWSTR>(sFilePath));
			return FALSE;
		case UnicodeType::UTF8:
		case UnicodeType::UTF8BOM:
			pFilter = std::make_unique<CUtf8Filter>(nullptr);
			break;
		default:
		case UnicodeType::ASCII:
			pFilter = std::make_unique<CAsciiFilter>(nullptr);
			break;
		case UnicodeType::UTF16_BE:
		case UnicodeType::UTF16_BEBOM:
			pFilter = std::make_unique<CUtf16beFilter>(nullptr);
			break;
		case UnicodeType::UTF16_LE:
		case UnicodeType::UTF16_LEBOM:
			pFilter = std::make_unique<CUtf16leFilter>(nullptr);
			break;
		case UnicodeType::UTF32_BE:
			pFilter = std::make_unique<CUtf32beFilter>(nullptr);
			break;
		case UnicodeType::UTF32_LE:
			pFilter = std::make_unique<CUtf32leFilter>(nullptr);
			break;
		}
		decoded = pFilter->Decode(fileBytes);
		if (!decoded)
		{
			SetErrorString();
			return FALSE;
		}
	}
	catch (CMemoryException* e)
	{
		e->GetErrorMessage(CStrBuf(m_sErrorString, 1000), 1000);
		e->Delete();
		return FALSE;
	}
	catch (const std::bad_alloc&)
	{
		m_sErrorString.LoadString(IDS_ERR_FILE_TOOBIG);
		return FALSE;
	}
	std::string{}.swap(fileBytes); // decoded now; free the raw bytes (clear() would keep the capacity) before the lines are built

	const std::wstring_view converted = *decoded;
	int nReadChars = static_cast<int>(converted.size()); // see above, we have a INT_MAX limitation
	auto pTextBuf = converted.data();
	const wchar_t* pLineStart = pTextBuf;
	if (!converted.empty() && ((m_SaveParams.m_UnicodeType == UnicodeType::UTF8BOM)
		|| (m_SaveParams.m_UnicodeType == UnicodeType::UTF16_LEBOM)
		|| (m_SaveParams.m_UnicodeType == UnicodeType::UTF16_BEBOM)
		|| (m_SaveParams.m_UnicodeType == UnicodeType::UTF32_LE)
		|| (m_SaveParams.m_UnicodeType == UnicodeType::UTF32_BE)))
	{
		// ignore the BOM
		++pTextBuf;
		++pLineStart;
		--nReadChars;
	}

	// fill in the lines into the array
	size_t countEOLs[static_cast<int>(EOL::_COUNT)] = { 0 };
	CFileTextLine oTextLine;
	for (int i = nReadChars; i; --i)
	{
		EOL eEol;
		switch (*pTextBuf++)
		{
		case '\r':
			// crlf line ending or cr line ending
			eEol = ((i > 1) && *(pTextBuf) == '\n') ? EOL::CRLF : EOL::CR;
			break;
		case '\n':
			// lfcr line ending or lf line ending
			eEol = ((i > 1) && *(pTextBuf) == '\r') ? EOL::LFCR : EOL::LF;
			if (eEol == EOL::LFCR)
			{
				// LFCR is very rare on Windows, so we have to double check
				// that this is not just a LF followed by CRLF
				if (((countEOLs[static_cast<int>(EOL::CRLF)] > 1) || (countEOLs[static_cast<int>(EOL::LF)] > 1) || (GetCount() < 2)) &&
					((i > 2) && (*(pTextBuf+1) == '\n')))
				{
					// change the EOL back to a simple LF
					eEol = EOL::LF;
				}
			}
			break;
		case 0x000b:
			eEol = EOL::VT;
			break;
		case 0x000c:
			eEol = EOL::FF;
			break;
		case 0x0085:
			eEol = EOL::NEL;
			break;
		case 0x2028:
			eEol = EOL::LS;
			break;
		case 0x2029:
			eEol = EOL::PS;
			break;
		default:
			continue;
		}
		oTextLine.sLine = CString(pLineStart, static_cast<int>(pTextBuf-pLineStart) - 1);
		oTextLine.eEnding = eEol;
		CStdFileLineArray::Add(oTextLine);
		++countEOLs[static_cast<int>(eEol)];
		if (eEol == EOL::CRLF || eEol == EOL::LFCR)
		{
			++pTextBuf;
			--i;
		}
		pLineStart = pTextBuf;
	}
	CString line(pLineStart, static_cast<int>(pTextBuf - pLineStart));
	Add(line, EOL::NoEnding);

	// some EOLs are not supported by the svn diff lib.
	m_bNeedsConversion |= (countEOLs[static_cast<int>(EOL::CRLF)] != 0);
	m_bNeedsConversion |= (countEOLs[static_cast<int>(EOL::FF)] != 0);
	m_bNeedsConversion |= (countEOLs[static_cast<int>(EOL::VT)] != 0);
	m_bNeedsConversion |= (countEOLs[static_cast<int>(EOL::NEL)] != 0);
	m_bNeedsConversion |= (countEOLs[static_cast<int>(EOL::LS)] != 0);
	m_bNeedsConversion |= (countEOLs[static_cast<int>(EOL::PS)] != 0);

	size_t eolmax = 0;
	for (int nEol = 0; nEol < static_cast<int>(EOL::_COUNT); nEol++)
	{
		if (eolmax < countEOLs[nEol])
		{
			eolmax = countEOLs[nEol];
			m_SaveParams.m_LineEndings = static_cast<EOL>(nEol);
		}
	}

	return TRUE;
}

void CFileTextLines::StripWhiteSpace(CString& sLine, DWORD dwIgnoreWhitespaces, bool blame)
{
	if (blame)
	{
		if (sLine.GetLength() > 66)
			sLine = sLine.Mid(66);
	}
	switch (dwIgnoreWhitespaces)
	{
	case 0:
		// Compare whitespaces
		// do nothing
		break;
	case 1:
		// Ignore all whitespaces
		sLine.TrimLeft(L" \t");
		sLine.TrimRight(L" \t");
		break;
	case 2:
		// Ignore leading whitespace
		sLine.TrimLeft(L" \t");
		break;
	case 3:
		// Ignore ending whitespace
		sLine.TrimRight(L" \t");
		break;
	}
}

/**
	Encoding pattern:
		- encode & save BOM
		- Get Line
		- modify line - whitespaces, lowercase
		- encode & save line
		- get cached encoded eol
		- save eol
*/
BOOL CFileTextLines::Save( const CString& sFilePath
						, bool bSaveAsUTF8 /*= false */
						, bool bUseSVNCompatibleEOLs /*= false */
						, DWORD dwIgnoreWhitespaces /*= 0 */
						, BOOL bIgnoreCase /*= FALSE */
						, bool bBlame /*= false*/
						, bool bIgnoreComments /*= false*/
						, const CString& linestart /*= CString()*/
						, const CString& blockstart /*= CString()*/
						, const CString& blockend /*= CString()*/
						, const std::wregex& rx /*= std::wregex()*/
						, const std::wstring& replacement /*=L""*/)
{
	m_sCommentLine = linestart;
	m_sCommentBlockStart = blockstart;
	m_sCommentBlockEnd = blockend;

	try
	{
		CString destPath = sFilePath;
		// now make sure that the destination directory exists
		int ind = 0;
		while (destPath.Find('\\', ind)>=2)
		{
			if (!PathIsDirectory(destPath.Left(destPath.Find('\\', ind))))
			{
				if (!CreateDirectory(destPath.Left(destPath.Find('\\', ind)), nullptr))
					return FALSE;
			}
			ind = destPath.Find('\\', ind)+1;
		}

		CStdioFile file;			// Hugely faster than CFile for big file writes - because it uses buffering
		if (!file.Open(sFilePath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary | CFile::shareDenyNone))
		{
			m_sErrorString.Format(IDS_ERR_FILE_OPEN, static_cast<LPCWSTR>(sFilePath));
			return FALSE;
		}

		std::unique_ptr<CEncodeFilter> pFilter;
		bool bSaveBom = true;
		CFileTextLines::UnicodeType eUnicodeType = bSaveAsUTF8 ? CFileTextLines::UnicodeType::UTF8 : m_SaveParams.m_UnicodeType;
		switch (eUnicodeType)
		{
		default:
		case CFileTextLines::UnicodeType::ASCII:
			bSaveBom = false;
			pFilter = std::make_unique<CAsciiFilter>(&file);
			break;
		case CFileTextLines::UnicodeType::UTF8:
			bSaveBom = false;
			[[fallthrough]];
		case CFileTextLines::UnicodeType::UTF8BOM:
			pFilter = std::make_unique<CUtf8Filter>(&file);
			break;
		case CFileTextLines::UnicodeType::UTF16_BE:
			bSaveBom = false;
			pFilter = std::make_unique<CUtf16beFilter>(&file);
			break;
		case CFileTextLines::UnicodeType::UTF16_BEBOM:
			pFilter = std::make_unique<CUtf16beFilter>(&file);
			break;
		case CFileTextLines::UnicodeType::UTF16_LE:
			bSaveBom = false;
			pFilter = std::make_unique<CUtf16leFilter>(&file);
			break;
		case CFileTextLines::UnicodeType::UTF16_LEBOM:
			pFilter = std::make_unique<CUtf16leFilter>(&file);
			break;
		case CFileTextLines::UnicodeType::UTF32_BE:
			pFilter = std::make_unique<CUtf32beFilter>(&file);
			break;
		case CFileTextLines::UnicodeType::UTF32_LE:
			pFilter = std::make_unique<CUtf32leFilter>(&file);
			break;
		}

		if (bSaveBom)
		{
			//first write the BOM
			pFilter->Write(L"\xfeff");
		}
		// cache EOLs
		std::string oEncodedEol[static_cast<int>(EOL::_COUNT)];
		oEncodedEol[static_cast<int>(EOL::LF)] = pFilter->Encode(L"\n"); // x0a
		oEncodedEol[static_cast<int>(EOL::CR)] = pFilter->Encode(L"\r"); // x0d
		oEncodedEol[static_cast<int>(EOL::CRLF)] = pFilter->Encode(L"\r\n"); // x0d x0a
		if (bUseSVNCompatibleEOLs)
		{
			// when using EOLs that are supported by the svn lib,
			// we have to use the same EOLs as the file has in case
			// they're already supported, but a different supported one
			// in case the original one isn't supported.
			// Only this way the option "ignore EOLs (recommended)" unchecked
			// actually shows the lines as different.
			// However, the diff won't find and differences in EOLs
			// for these special EOLs if they differ between those special ones
			// listed below.
			// But it will work properly for the most common EOLs LF/CR/CRLF.
			oEncodedEol[static_cast<int>(EOL::LFCR)] = oEncodedEol[static_cast<int>(EOL::CR)];
			for (int nEol = 0; nEol < static_cast<int>(EOL::NoEnding); nEol++)
			{
				if (oEncodedEol[nEol].empty())
					oEncodedEol[nEol] = oEncodedEol[static_cast<int>(EOL::LF)];
			}
		}
		else
		{
			oEncodedEol[static_cast<int>(EOL::LFCR)] = pFilter->Encode(L"\n\r");
			oEncodedEol[static_cast<int>(EOL::VT)] = pFilter->Encode(L"\v"); // x0b
			oEncodedEol[static_cast<int>(EOL::FF)] = pFilter->Encode(L"\f"); // x0c
			oEncodedEol[static_cast<int>(EOL::NEL)] = pFilter->Encode(L"\x85");
			oEncodedEol[static_cast<int>(EOL::LS)] = pFilter->Encode(L"\x2028");
			oEncodedEol[static_cast<int>(EOL::PS)] = pFilter->Encode(L"\x2029");
		}
		oEncodedEol[static_cast<int>(EOL::AutoLine)] = oEncodedEol[static_cast<int>(m_SaveParams.m_LineEndings == EOL::AutoLine ? EOL::CRLF : m_SaveParams.m_LineEndings)];

		bool bInBlockComment = false;
		for (int i=0; i<GetCount(); i++)
		{
			CString sLineT = GetAt(i);
			if (bIgnoreComments)
				bInBlockComment = StripComments(sLineT, bInBlockComment);
			if (!rx._Empty())
				LineRegex(sLineT, rx, replacement);
			StripWhiteSpace(sLineT, dwIgnoreWhitespaces, bBlame);
			if (bIgnoreCase)
				sLineT = sLineT.MakeLower();
			pFilter->Write(tgit::str::StringView{ sLineT });
			EOL eEol = GetLineEnding(i);
			pFilter->WriteBytes(oEncodedEol[static_cast<int>(eEol)]);
		}
		file.Close();
	}
	catch (CException * e)
	{
		e->GetErrorMessage(CStrBuf(m_sErrorString, 4096), 4096);
		e->Delete();
		return FALSE;
	}
	return TRUE;
}

void CFileTextLines::SetErrorString()
{
	m_sErrorString = wil::CFormatMessageWrapper().c_str();
}

void CFileTextLines::CopySettings(CFileTextLines * pFileToCopySettingsTo) const
{
	if (pFileToCopySettingsTo)
	{
		pFileToCopySettingsTo->m_SaveParams = m_SaveParams;
	}
}

const wchar_t * CFileTextLines::GetEncodingName(UnicodeType eEncoding)
{
	switch (eEncoding)
	{
	case UnicodeType::ASCII:
		return L"ASCII";
	case UnicodeType::BINARY:
		return L"BINARY";
	case UnicodeType::UTF16_LE:
		return L"UTF-16LE";
	case UnicodeType::UTF16_LEBOM:
		return L"UTF-16LE BOM";
	case UnicodeType::UTF16_BE:
		return L"UTF-16BE";
	case UnicodeType::UTF16_BEBOM:
		return L"UTF-16BE BOM";
	case UnicodeType::UTF32_LE:
		return L"UTF-32LE";
	case UnicodeType::UTF32_BE:
		return L"UTF-32BE";
	case UnicodeType::UTF8:
		return L"UTF-8";
	case UnicodeType::UTF8BOM:
		return L"UTF-8 BOM";
	}
	return L"";
}

bool CFileTextLines::IsInsideString(const CString& sLine, int pos)
{
	int scount = 0;
	int ccount = 0;
	auto spos = sLine.Find('"');
	while (spos >= 0 && spos < pos)
	{
		++scount;
		spos = sLine.Find('"', spos + 1);
	}
	auto cpos = sLine.Find('\'');
	while (cpos >= 0 && cpos < pos)
	{
		++ccount;
		cpos = sLine.Find('"', cpos + 1);
	}
	return (scount % 2 != 0 || ccount % 2 != 0);
}

bool CFileTextLines::StripComments( CString& sLine, bool bInBlockComment )
{
	int startpos = 0;
	int oldStartPos = -1;
	do
	{
		if (bInBlockComment)
		{
			int endpos = sLine.Find(m_sCommentBlockEnd);
			if (IsInsideString(sLine, endpos))
				endpos = -1;
			if (endpos >= 0 && (endpos > startpos || endpos == 0))
			{
				sLine = sLine.Left(startpos) + sLine.Mid(endpos + m_sCommentBlockEnd.GetLength());
				bInBlockComment = false;
				startpos = endpos;
			}
			else
			{
				sLine = sLine.Left(startpos);
				startpos = -1;
			}
		}
		if (!bInBlockComment)
		{
			startpos = m_sCommentBlockStart.IsEmpty() ? -1 : sLine.Find(m_sCommentBlockStart, startpos);
			int startpos2 = m_sCommentLine.IsEmpty() ? -1 : sLine.Find(m_sCommentLine);
			if ((startpos2 < startpos && startpos2 >= 0) || (startpos2 >= 0 && startpos < 0))
			{
				// line comment
				// look if there's a string marker (" or ') before that
				// note: this check is not fully correct. For example, it
				// does not account for escaped chars or even multiline strings.
				// but it has to be fast, so this has to do...
				if (!IsInsideString(sLine, startpos2))
				{
					// line comment, erase the rest of the line
					sLine = sLine.Left(startpos2);
					startpos = -1;
				}
				if (startpos == oldStartPos)
					return false;
				oldStartPos = startpos;
			}
			else if (startpos >= 0)
			{
				// starting block comment
				if (!IsInsideString(sLine, startpos))
					bInBlockComment = true;
				else
					++startpos;
			}
		}
	} while (startpos >= 0);

	return bInBlockComment;
}

void CFileTextLines::LineRegex( CString& sLine, const std::wregex& rx, const std::wstring& replacement ) const
{
	std::wstring str = static_cast<LPCWSTR>(sLine);
	std::wstring str2 = std::regex_replace(str, rx, replacement);
	sLine = str2.c_str();
}


namespace
{
/// UTF-16 code units, copied out of \a bytes. A trailing odd byte is dropped, as it always was.
std::wstring WideFromBytes(const std::string_view bytes)
{
	std::wstring text;
	text.resize_and_overwrite(bytes.size() / sizeof(wchar_t), [&bytes](wchar_t* const out, const size_t count) {
		std::memcpy(out, bytes.data(), count * sizeof(wchar_t));
		return count;
	});
	return text;
}

/// UTF-32 to UTF-16. Code points outside the BMP become surrogate pairs; out-of-range ones U+FFFD.
std::wstring DecodeUtf32(const std::string_view bytes, const bool bigEndian)
{
	const size_t count = bytes.size() / sizeof(std::uint32_t);
	std::wstring text;
	text.reserve(count); // one unit per code point, plus one more for each outside the BMP
	for (size_t i = 0; i < count; ++i)
	{
		std::uint32_t zChar;
		std::memcpy(&zChar, bytes.data() + i * sizeof(zChar), sizeof(zChar)); // the bytes need not be aligned
		if (bigEndian)
			zChar = std::byteswap(zChar);
		if (zChar >= 0x110000)
			text += L'\xfffd'; // ? mark
		else if (zChar >= 0x10000)
		{
			zChar -= 0x10000;
			text += static_cast<wchar_t>(0xd800 | ((zChar >> 10) & 0x3ff)); // lead surrogate
			text += static_cast<wchar_t>(0xdc00 | (zChar & 0x3ff)); // trail surrogate
		}
		else
			text += static_cast<wchar_t>(zChar);
	}
	return text;
}

/// UTF-16 to UTF-32, into \a out. Unpaired surrogates become U+FFFD.
void EncodeUtf32(std::string& out, const std::wstring_view text, const bool bigEndian)
{
	out.clear();
	for (size_t i = 0; i < text.size(); ++i)
	{
		std::uint32_t zChar = text[i];
		if ((zChar & 0xfc00) == 0xd800) // lead surrogate
		{
			if (i + 1 < text.size() && (text[i + 1] & 0xfc00) == 0xdc00) // trail surrogate follows
				zChar = 0x10000 + ((zChar & 0x3ff) << 10) + (text[++i] & 0x3ff);
			else
				zChar = 0xfffd; // ? mark
		}
		else if ((zChar & 0xfc00) == 0xdc00) // trail surrogate without lead
			zChar = 0xfffd; // ? mark
		if (bigEndian)
			zChar = std::byteswap(zChar);
		const auto unit = std::bit_cast<std::array<char, sizeof(zChar)>>(zChar);
		out.append(unit.data(), unit.size());
	}
}
} // namespace


std::optional<std::wstring> CAsciiFilter::Decode(const std::string_view bytes) const
{
	const int nFlags = (m_nCodePage == CP_ACP) ? MB_PRECOMPOSED : 0;
	const int len = gsl::narrow<int>(bytes.size()); // Load refuses files of INT_MAX bytes and up
	// dry decode is around 8 times faster then real one, alternatively we can set buffer to max length
	const int nReadChars = MultiByteToWideChar(m_nCodePage, nFlags, bytes.data(), len, nullptr, 0);
	if (nReadChars <= 0)
		return std::nullopt;
	std::wstring text;
	text.resize_and_overwrite(static_cast<size_t>(nReadChars), [&](wchar_t* const out, const size_t count) {
		return static_cast<size_t>(MultiByteToWideChar(m_nCodePage, nFlags, bytes.data(), len, out, static_cast<int>(count)));
	});
	if (text.size() != static_cast<size_t>(nReadChars))
		return std::nullopt;
	return text;
}

const std::string& CAsciiFilter::Encode(const std::wstring_view text)
{
	// ask for the size rather than guessing 3 bytes per character
	const int len = gsl::narrow<int>(text.size());
	const int need = len ? WideCharToMultiByte(m_nCodePage, 0, text.data(), len, nullptr, 0, nullptr, nullptr) : 0;
	m_encoded.resize_and_overwrite(static_cast<size_t>((std::max)(need, 0)), [&](char* const out, const size_t capacity) {
		return capacity ? static_cast<size_t>(WideCharToMultiByte(m_nCodePage, 0, text.data(), len, out, static_cast<int>(capacity), nullptr, nullptr)) : size_t{ 0 };
	});
	return m_encoded;
}


std::optional<std::wstring> CUtf16leFilter::Decode(const std::string_view bytes) const
{
	// we believe data is ok for use
	return WideFromBytes(bytes);
}

const std::string& CUtf16leFilter::Encode(const std::wstring_view text)
{
	const auto bytes = std::as_bytes(std::span(text));
	m_encoded.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	return m_encoded;
}


std::optional<std::wstring> CUtf16beFilter::Decode(const std::string_view bytes) const
{
	std::wstring text = WideFromBytes(bytes);
	for (wchar_t& unit : text)
		unit = std::byteswap(unit);
	return text;
}

const std::string& CUtf16beFilter::Encode(const std::wstring_view text)
{
	CUtf16leFilter::Encode(text);
	// swap the two bytes of every UTF-16 unit
	for (size_t i = 0; i + 1 < m_encoded.size(); i += 2)
		std::swap(m_encoded[i], m_encoded[i + 1]);
	return m_encoded;
}


// Both UTF-32 directions used to hide real bugs in their hand-sized buffers,
// both present before the module port (FileTextLinesTest pins them):
//  - Decode reported the UTF-32 count as the length, dropping one UTF-16 unit
//    per non-BMP character off the end of the text.
//  - Encode sized its buffer as 2 bytes per character and wrote 4, so a line
//    longer than CBuffer's ~1K rounding overran the heap.
// (Decode also masked the trail surrogate with 0x7ff rather than 0x3ff. That one
// was harmless: bit 10 is already set in 0xdc00, so OR-ing it in changed nothing.)
std::optional<std::wstring> CUtf32leFilter::Decode(const std::string_view bytes) const
{
	return DecodeUtf32(bytes, false);
}

const std::string& CUtf32leFilter::Encode(const std::wstring_view text)
{
	EncodeUtf32(m_encoded, text, false);
	return m_encoded;
}


std::optional<std::wstring> CUtf32beFilter::Decode(const std::string_view bytes) const
{
	return DecodeUtf32(bytes, true);
}

const std::string& CUtf32beFilter::Encode(const std::wstring_view text)
{
	EncodeUtf32(m_encoded, text, true);
	return m_encoded;
}
