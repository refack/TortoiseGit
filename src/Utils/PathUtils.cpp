// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2012-2023, 2025-2026 - TortoiseGit
// Copyright (C) 2003-2008, 2013-2015 - TortoiseSVN

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
#include "PathUtils.h"
#include <memory>
#include "WideString.h"
#include "ReparseData.h"
#include "SmartHandle.h"
#include <assert.h>
#include <format>
// These four used to arrive from whichever consumer's stdafx.h happened to
// include them, which worked only because everything that needed them sat
// behind #ifdef CSTRING_AVAILABLE and no CString-less project ever compiled
// that half. With the gate gone the file has to name its own dependencies.
#include <atlbase.h>  // CComHeapPtr
#include <ShlObj.h>   // SHGetKnownFolderPath, FOLDERID_*
#include <Shlwapi.h>  // PathIsURL, PathIsRelative, PathCanonicalize, PathIsDirectory
#include <tchar.h>    // _totlower
#include "UnicodeUtils.h"

#pragma comment(lib, "Shlwapi.lib")

namespace
{
/// GetModuleFileName has no documented upper bound, so grow until it stops filling the buffer.
std::wstring GetModuleFileNameString(HMODULE hMod)
{
	std::wstring path;
	DWORD len = 0;
	DWORD bufferlen = MAX_PATH; // MAX_PATH is not the limit here!
	do
	{
		bufferlen += MAX_PATH; // MAX_PATH is not the limit here!
		path.resize(bufferlen + 1);
		len = GetModuleFileName(hMod, path.data(), bufferlen);
	} while (len == bufferlen);
	path.resize(len);
	return path;
}
} // namespace

BOOL CPathUtils::MakeSureDirectoryPathExists(LPCWSTR path)
{
	const size_t len = wcslen(path) + 10;
	auto buf = std::make_unique<wchar_t[]>(len);
	auto internalpathbuf = std::make_unique<wchar_t[]>(len);
	wchar_t* pPath = internalpathbuf.get();
	SECURITY_ATTRIBUTES attribs = { 0 };
	attribs.nLength = sizeof(SECURITY_ATTRIBUTES);
	attribs.bInheritHandle = FALSE;

	ConvertToBackslash(internalpathbuf.get(), path, len);
	do
	{
		SecureZeroMemory(buf.get(), (len)*sizeof(wchar_t));
		wchar_t* slashpos = wcschr(pPath, L'\\');
		if (slashpos)
			wcsncpy_s(buf.get(), len, internalpathbuf.get(), slashpos - internalpathbuf.get());
		else
			wcsncpy_s(buf.get(), len, internalpathbuf.get(), len);
		CreateDirectory(buf.get(), &attribs);
		pPath = wcschr(pPath, L'\\');
	} while ((pPath++) && (wcschr(pPath, L'\\')));

	return CreateDirectory(internalpathbuf.get(), &attribs);
}

void CPathUtils::ConvertToSlash(LPWSTR path)
{
	assert(path);
	auto pCH = path;
	while ((pCH = wcschr(pCH, L'\\')) != nullptr)
		*pCH = L'/';
}

void CPathUtils::ConvertToBackslash(LPWSTR dest, LPCWSTR src, size_t len)
{
	wcscpy_s(dest, len, src);
	wchar_t* p = dest;
	for (; *p != '\0'; ++p)
		if (*p == '/')
			*p = '\\';
}

void CPathUtils::ConvertToBackslash(std::wstring& path)
{
	tgit::wstr::Replace(path, L'/', L'\\');
}

bool CPathUtils::Touch(const std::wstring& path)
{
	CAutoFile hFile = CreateFile(path.c_str(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!hFile)
		return false;

	FILETIME ft;
	SYSTEMTIME st;
	GetSystemTime(&st);					// Gets the current system time
	SystemTimeToFileTime(&st, &ft);		// Converts the current system time to file time format
	return SetFileTime(hFile,			// Sets last-write time of the file
		nullptr,						// to the converted current system time
		nullptr,
		&ft) != FALSE;
}

std::wstring CPathUtils::GetFileNameFromPath(const std::wstring_view sPath)
{
	// This used to normalize every slash to a backslash and then take everything
	// after the last one. Searching for either separator is the same answer
	// without the mutable copy a std::wstring_view cannot make anyway.
	const size_t at = sPath.find_last_of(L"\\/");
	return std::wstring(at == std::wstring_view::npos ? sPath : sPath.substr(at + 1));
}

std::wstring CPathUtils::GetFileExtFromPath(const std::wstring_view sPath)
{
	const int dotPos = tgit::wstr::ReverseFind(sPath, L'.');
	int slashPos = tgit::wstr::ReverseFind(sPath, L'\\');
	if (slashPos < 0)
		slashPos = tgit::wstr::ReverseFind(sPath, L'/');
	if (dotPos > slashPos)
		return tgit::wstr::Mid(sPath, dotPos);
	return {};
}

std::wstring CPathUtils::GetLongPathname(const std::wstring& path)
{
	if (path.empty())
		return path;
	wchar_t pathbufcanonicalized[MAX_PATH] = { 0 }; // MAX_PATH ok.
	DWORD ret = 0;
	std::wstring sRet;
	if (!PathIsURL(path.c_str()) && PathIsRelative(path.c_str()))
	{
		ret = GetFullPathName(path.c_str(), 0, nullptr, nullptr);
		if (ret)
		{
			auto pathbuf = std::make_unique<wchar_t[]>(ret + 1);
			if ((ret = GetFullPathName(path.c_str(), ret, pathbuf.get(), nullptr)) != 0)
				sRet.assign(pathbuf.get(), ret);
		}
	}
	else if (PathCanonicalize(pathbufcanonicalized, path.c_str()))
	{
		ret = ::GetLongPathName(pathbufcanonicalized, nullptr, 0);
		if (ret == 0)
			return path;
		auto pathbuf = std::make_unique<wchar_t[]>(ret + 2);
		ret = ::GetLongPathName(pathbufcanonicalized, pathbuf.get(), ret + 1);
		sRet.assign(pathbuf.get(), ret);
	}
	else
	{
		ret = ::GetLongPathName(path.c_str(), nullptr, 0);
		if (ret == 0)
			return path;
		auto pathbuf = std::make_unique<wchar_t[]>(ret + 2);
		ret = ::GetLongPathName(path.c_str(), pathbuf.get(), ret + 1);
		sRet.assign(pathbuf.get(), ret);
	}
	if (ret == 0)
		return path;
	return sRet;
}

BOOL CPathUtils::FileCopy(std::wstring srcPath, std::wstring destPath, BOOL force)
{
	tgit::wstr::Replace(srcPath, L'/', L'\\');
	tgit::wstr::Replace(destPath, L'/', L'\\');
	const std::wstring destFolder = tgit::wstr::Left(destPath, tgit::wstr::ReverseFind(destPath, L'\\'));
	MakeSureDirectoryPathExists(destFolder.c_str());
	return (CopyFile(srcPath.c_str(), destPath.c_str(), !force));
}

std::wstring CPathUtils::ParsePathInString(const std::wstring_view Str)
{
	int curPos = 0;
	std::wstring sToken = tgit::wstr::Tokenize(Str, L"'\t\r\n", curPos);
	while (!sToken.empty())
	{
		if (sToken.find_first_of(L"/\\") != std::wstring::npos)
		{
			tgit::wstr::Trim(sToken, L"'\"");
			return sToken;
		}
		sToken = tgit::wstr::Tokenize(Str, L"'\t\r\n", curPos);
	}
	return {};
}

std::wstring CPathUtils::GetAppDirectory(HMODULE hMod /* = nullptr */)
{
	const std::wstring path = GetModuleFileNameString(hMod);
	return GetLongPathname(tgit::wstr::Left(path, tgit::wstr::ReverseFind(path, L'\\') + 1));
}

std::wstring CPathUtils::GetAppParentDirectory(HMODULE hMod /* = nullptr */)
{
	std::wstring path = GetAppDirectory(hMod);
	path = tgit::wstr::Left(path, tgit::wstr::ReverseFind(path, L'\\'));
	path = tgit::wstr::Left(path, tgit::wstr::ReverseFind(path, L'\\') + 1);
	return path;
}

std::wstring CPathUtils::GetAppDataDirectory()
{
	CComHeapPtr<WCHAR> pszPath;
	if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pszPath) != S_OK)
		return {};

	std::wstring path { static_cast<LPCWSTR>(pszPath) };
	path += L"\\TortoiseGit";
	if (!PathIsDirectory(path.c_str()))
		CreateDirectory(path.c_str(), nullptr);

	path += L'\\';
	return path;
}

std::wstring CPathUtils::GetLocalAppDataDirectory()
{
	CComHeapPtr<WCHAR> pszPath;
	if (SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &pszPath) != S_OK)
		return {};
	std::wstring path { static_cast<LPCWSTR>(pszPath) };
	path += L"\\TortoiseGit";
	if (!PathIsDirectory(path.c_str()))
		CreateDirectory(path.c_str(), nullptr);

	path += L'\\';
	return path;
}

std::wstring CPathUtils::GetDocumentsDirectory()
{
	CComHeapPtr<WCHAR> pszPath;
	if (SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &pszPath) != S_OK)
		return {};

	return std::wstring(static_cast<LPCWSTR>(pszPath));
}

std::wstring CPathUtils::GetProgramsDirectory()
{
	CComHeapPtr<WCHAR> pszPath;
	if (SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_CREATE, nullptr, &pszPath) != S_OK)
		return {};

	return std::wstring(static_cast<LPCWSTR>(pszPath));
}

int CPathUtils::ReadLink(LPCWSTR filename, std::string* pTargetA)
{
	CAutoFile handle  = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!handle)
		return -1;

	DWORD ioctl_ret;
	BYTE buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE] = { 0 };
	auto reparse_buf = reinterpret_cast<TGIT_REPARSE_DATA_BUFFER*>(&buf);
	if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0, reparse_buf, sizeof(buf), &ioctl_ret, nullptr))
		return -1;

	if (reparse_buf->ReparseTag != IO_REPARSE_TAG_SYMLINK)
		return -1;

	wchar_t* target = reparse_buf->ReparseBuffer.SymbolicLink.PathBuffer + (reparse_buf->ReparseBuffer.SymbolicLink.SubstituteNameOffset / sizeof(WCHAR));
	int target_len = reparse_buf->ReparseBuffer.SymbolicLink.SubstituteNameLength / sizeof(WCHAR);
	if (!target_len)
		return -1;

	// not a symlink
	if (wcsncmp(target, L"\\??\\Volume{", 11) == 0)
		return -1;

	if (pTargetA)
	{
		std::wstring targetW(target, target_len);
		// The path may need to have a prefix removed
		DropPathPrefixes(targetW);
		tgit::wstr::Replace(targetW, L'\\', L'/');
		*pTargetA = CUnicodeUtils::StdGetUTF8(targetW);
	}

	return 0;
}

void CPathUtils::DropPathPrefixes(std::wstring& path)
{
	static constexpr wchar_t dosdevices_prefix[] = L"\\\?\?\\";
	static constexpr wchar_t nt_prefix[] = L"\\\\?\\";
	static constexpr wchar_t unc_prefix[] = L"UNC\\";

	size_t skip = 0;
	if (tgit::wstr::StartsWith(path, dosdevices_prefix))
		skip += wcslen(dosdevices_prefix);
	else if (tgit::wstr::StartsWith(path, nt_prefix))
		skip += wcslen(nt_prefix);

	if (skip)
	{
		if (path.size() - skip > wcslen(unc_prefix) && tgit::wstr::StartsWith(std::wstring_view(path).substr(skip), unc_prefix))
			skip += wcslen(unc_prefix);

		path.erase(0, skip);
	}
}

#pragma comment(lib, "Version.lib")
std::wstring CPathUtils::GetVersionFromFile(LPCWSTR p_strFilename)
{
	struct TRANSARRAY
	{
		WORD wLanguageID;
		WORD wCharacterSet;
	};

	DWORD dwReserved = 0;
	DWORD dwBufferSize = GetFileVersionInfoSize(p_strFilename, &dwReserved);

	if (dwBufferSize > 0)
	{
		auto pBuffer = std::make_unique<BYTE[]>(dwBufferSize);

		if (pBuffer)
		{
			UINT        nInfoSize = 0,
						nFixedLength = 0;
			LPSTR       lpVersion = nullptr;
			VOID*       lpFixedPointer;
			TRANSARRAY* lpTransArray;

			dwReserved = 0;
			GetFileVersionInfo(p_strFilename,
				dwReserved,
				dwBufferSize,
				pBuffer.get());

			// Check the current language
			VerQueryValue(pBuffer.get(),
				L"\\VarFileInfo\\Translation",
				&lpFixedPointer,
				&nFixedLength);
			lpTransArray = static_cast<TRANSARRAY*>(lpFixedPointer);

			wchar_t strLangProductVersion[MAX_PATH] = { 0 };
			swprintf_s(strLangProductVersion, L"\\StringFileInfo\\%04x%04x\\ProductVersion", lpTransArray[0].wLanguageID, lpTransArray[0].wCharacterSet);

			VerQueryValue(pBuffer.get(),
				static_cast<LPCWSTR>(strLangProductVersion),
				reinterpret_cast<LPVOID*>(&lpVersion),
				&nInfoSize);
			if (nInfoSize && lpVersion)
				return reinterpret_cast<LPCWSTR>(lpVersion);
		}
	}

	return {};
}

std::wstring CPathUtils::GetCopyrightForSelf()
{
	const std::wstring path = GetModuleFileNameString(nullptr);

	std::wstring strReturn;
	DWORD dwReserved = 0;
	DWORD dwBufferSize = GetFileVersionInfoSize(path.c_str(), &dwReserved);

	if (dwBufferSize > 0)
	{
		auto pBuffer = std::make_unique<BYTE[]>(dwBufferSize);

		if (pBuffer)
		{
			dwReserved = 0;
			GetFileVersionInfo(path.c_str(),
				dwReserved,
				dwBufferSize,
				pBuffer.get());

			UINT nFixedLength = 0;
			VOID* lpFixedPointer;
			struct TRANSARRAY
			{
				WORD wLanguageID;
				WORD wCharacterSet;
			};
			TRANSARRAY* lpTransArray;
			// Check the current language
			VerQueryValue(pBuffer.get(), L"\\VarFileInfo\\Translation", &lpFixedPointer, &nFixedLength);
			lpTransArray = static_cast<TRANSARRAY*>(lpFixedPointer);

			const std::wstring strLangLegalCopyright = std::format(L"\\StringFileInfo\\{:04x}{:04x}\\LegalCopyright", lpTransArray[0].wLanguageID, lpTransArray[0].wCharacterSet);

			UINT nInfoSize = 0;
			LPWSTR lpVersion = nullptr;
			VerQueryValue(pBuffer.get(), strLangLegalCopyright.c_str(), reinterpret_cast<LPVOID*>(&lpVersion), &nInfoSize);
			if (nInfoSize && lpVersion)
				strReturn = lpVersion;
		}
	}

	return strReturn;
}

std::wstring CPathUtils::BuildPathWithPathDelimiter(const std::wstring_view path)
{
	std::wstring result(path);
	EnsureTrailingPathDelimiter(result);
	return result;
}

void CPathUtils::EnsureTrailingPathDelimiter(std::wstring& path)
{
	if (!path.empty() && path.back() != L'\\')
		path += L'\\';
}

void CPathUtils::TrimTrailingPathDelimiter(std::wstring& path)
{
	tgit::wstr::TrimRight(path, L"\\");
}

std::wstring CPathUtils::ExpandFileName(const std::wstring& path)
{
	if (path.empty())
		return path;

	const DWORD ret = GetFullPathName(path.c_str(), 0, nullptr, nullptr);
	if (!ret)
		return path;

	// ret counts the terminating null, so ret characters of storage is exactly
	// what GetFullPathName was asked for; resize back to what it actually wrote.
	std::wstring sRet;
	sRet.resize(ret);
	if (const DWORD written = GetFullPathName(path.c_str(), ret, sRet.data(), nullptr))
	{
		sRet.resize(written);
		return sRet;
	}
	return path;
}

std::wstring CPathUtils::NormalizePath(const std::wstring& path)
{
	// Account for ..\ and .\ that may occur in each path
	std::wstring nPath = ExpandFileName(path);

	tgit::wstr::MakeLower(nPath);

	TrimTrailingPathDelimiter(nPath);

	return nPath;
}

bool CPathUtils::IsSamePath(const std::wstring& path1, const std::wstring& path2)
{
	return ArePathStringsEqualWithCase(NormalizePath(GetLongPathname(path1)), NormalizePath(GetLongPathname(path2)));
}

bool CPathUtils::ArePathStringsEqual(const std::wstring_view sP1, const std::wstring_view sP2)
{
	if (sP1.size() != sP2.size())
	{
		// Different lengths
		return false;
	}
	// A default-constructed view has a null data(), which the length-taking
	// overload asserts against; two empty paths are equal either way.
	if (sP1.empty())
		return true;
	return CPathUtils::ArePathStringsEqual(sP1.data(), sP2.data(), static_cast<int>(sP1.size()));
}

bool CPathUtils::ArePathStringsEqual(LPCWSTR sP1, LPCWSTR sP2, int length)
{
	assert(sP1 && sP2);

	// We work from the end of the strings, because path differences
	// are more likely to occur at the far end of a string
	LPCWSTR pP1 = sP1 + (length - 1);
	LPCWSTR pP2 = sP2 + (length - 1);
	while (length-- > 0)
	{
		if (_totlower(*pP1--) != _totlower(*pP2--))
			return false;
	}
	return true;
}

bool CPathUtils::ArePathStringsEqualWithCase(const std::wstring_view sP1, const std::wstring_view sP2)
{
	if (sP1.size() != sP2.size())
	{
		// Different lengths
		return false;
	}
	if (sP1.empty())
		return true;
	return CPathUtils::ArePathStringsEqualWithCase(sP1.data(), sP2.data(), static_cast<int>(sP1.size()));
}

bool CPathUtils::ArePathStringsEqualWithCase(LPCWSTR sP1, LPCWSTR sP2, int length)
{
	assert(sP1 && sP2);

	// We work from the end of the strings, because path differences
	// are more likely to occur at the far end of a string
	LPCWSTR pP1 = sP1 + (length - 1);
	LPCWSTR pP2 = sP2 + (length - 1);
	while (length-- > 0)
	{
		if ((*pP1--) != (*pP2--))
			return false;
	}
	return true;
}

std::wstring CPathUtils::GetCWD()
{
	DWORD len = GetCurrentDirectory(0, nullptr);
	if (!len)
		return {};

	if (auto originalCurrentDirectory = std::make_unique<wchar_t[]>(len); GetCurrentDirectory(len, originalCurrentDirectory.get()))
		return CPathUtils::GetLongPathname(originalCurrentDirectory.get());

	return {};
}
