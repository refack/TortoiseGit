// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2016-2022, 2025-2026 - TortoiseGit
// Copyright (C) 2003-2008, 2013-2014 - TortoiseSVN

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
#pragma once

#include <string>
#include <string_view>
// Not needed by the declarations below, but by the call sites: a caller still
// holding a CString reaches these signatures through tgit::wstr::View, and
// there is no way to use this header from such a caller without it. Same
// reasoning as Git.h's include of it.
#include "WideString.h"

/**
 * \ingroup Utils
 * helper class to handle path strings.
 *
 * This class used to have two APIs: a small flavor-neutral one, and a much
 * larger CString one behind #ifdef CSTRING_AVAILABLE - a macro this header
 * defined for MFC builds and which src\TGitCache, src\TortoiseShell and
 * test\Cache define in their stdafx.h to opt in. That gate was not decoration:
 * CString is CStringT<wchar_t, StrTraitMFC_DLL<...>> in an MFC project and
 * CStringT<wchar_t, StrTraitATL<...>> in an ATL one, so GetAppDirectory()
 * genuinely returned a different type depending on who compiled it, and
 * PathUtils.obj had to be rebuilt per consumer. There is no gate any more:
 * every signature here is std::wstring, so this is one object file that both
 * TortoiseGitProc and TortoiseGit.dll can link.
 *
 * Parameter conventions, decided once so call sites do not have to re-derive
 * them: std::wstring_view where the implementation is pure string manipulation,
 * const std::wstring& where the value flows into a Win32 API that needs null
 * termination, and LPCWSTR kept as-is where the entry point was already
 * flavor-neutral (converting those would be churn with nothing bought).
 */
class CPathUtils
{
public:
	CPathUtils() = delete;
	static BOOL			MakeSureDirectoryPathExists(LPCWSTR path);
	static void			ConvertToBackslash(LPWSTR dest, LPCWSTR src, size_t len);
	static void			ConvertToSlash(LPWSTR path);

	/**
	 * Returns the version string from the VERSION resource of a dll or exe.
	 * \param p_strFilename path to the dll or exe
	 * \return the version string
	 */
	static std::wstring GetVersionFromFile(LPCWSTR p_strFilename);

	static void ConvertToBackslash(std::wstring& path);

	/**
	 * returns the filename of a full path
	 */
	static std::wstring GetFileNameFromPath(std::wstring_view sPath);

	/**
	 * returns the file extension from a full path
	 */
	static std::wstring GetFileExtFromPath(std::wstring_view sPath);

	/**
	 * Returns the long pathname of a path which may be in 8.3 format.
	 */
	static std::wstring GetLongPathname(const std::wstring& path);

	/**
	 * Copies a file or a folder from \a srcPath to \a destpath, creating
	 * intermediate folders if necessary. If \a force is TRUE, then files
	 * are overwritten if they already exist.
	 * Folders are just created at the new location, no files in them get
	 * copied.
	 */
	static BOOL FileCopy(std::wstring srcPath, std::wstring destPath, BOOL force = TRUE);

	/**
	 * parses a string for a path or url. If no path or url is found,
	 * an empty string is returned.
	 * \remark if more than one path or url is inside the string, only
	 * the first one is returned.
	 */
	static std::wstring ParsePathInString(std::wstring_view Str);

	/**
	 * Returns the path to the installation folder, in our case the TortoiseSVN/bin folder.
	 * \remark the path returned has a trailing backslash
	 */
	static std::wstring GetAppDirectory(HMODULE hMod = nullptr);

	/**
	 * Returns the path to the installation parent folder, in our case the TortoiseSVN folder.
	 * \remark the path returned has a trailing backslash
	 */
	static std::wstring GetAppParentDirectory(HMODULE hMod = nullptr);

	static std::wstring GetDocumentsDirectory();
	static std::wstring GetProgramsDirectory();

	/**
	 * Returns the path to the application data folder, in our case the %APPDATA%TortoiseSVN folder.
	 * \remark the path returned has a trailing backslash
	 */
	static std::wstring GetAppDataDirectory();
	static std::wstring GetLocalAppDataDirectory();

	/**
	 * Removes any of the following namespace prefixes from a path, if found: "\??\", "\\?\", "\\?\UNC\".
	 */
	static void DropPathPrefixes(std::wstring& path);

	/**
	 * Reads a symlink's target, as UTF-8 bytes with forward slashes - the form git stores
	 * a symlink blob in, which is why the out-parameter is a byte string and not a wide one.
	 */
	static int ReadLink(LPCWSTR filename, std::string* target = nullptr);

	/**
	 * Ensures that the path ends with a folder separator.
	 * If the delimiter already exists, no additional delimiter will be added.
	 * \param path to ensure
	 */
	static void EnsureTrailingPathDelimiter(std::wstring& path);

	/**
	 * Returns a path guaranteeing that a valid path delimiter follows.
	 * If the delimiter already exists, no additional delimiter will be added.
	 * \param path to ensure
	 * \return path including path delimiter
	 */
	static std::wstring BuildPathWithPathDelimiter(std::wstring_view path);

	/**
	 * Trims a possible included trailing folder separator from the provided path.
	 * \param path to trim
	 */
	static void TrimTrailingPathDelimiter(std::wstring& path);

	/**
	 * ExpandFileName converts the relative file name into a fully qualified path name.
	 * ExpandFileName does not verify that the resulting fully qualified path name
	 * refers to an existing file, or even that the resulting path exists.
	 * \param path to expand
	 * \return fully qualified path name
	 */
	static std::wstring ExpandFileName(const std::wstring& path);

	/**
	 * This method will make a path comparable to another path.
	 * It will do the following:
	 * 1.) Modify all characters in the path to be lower case
	 * 2.) Account for ..\'s and .\'s that may occur in the middle of the path and remove them
	 * 3.) Remove the trailing path delimiter at the end
	 * The function does not account for symlinks or DOS 8.3 file/folder names at this point in time.
	 * \param path to normalize
	 * \return normalized path
	 */
	static std::wstring NormalizePath(const std::wstring& path);

	/**
	 * Compares two paths and returns true if they are logically the same path.
	 * The function does not account for symlinks at this point in time.
	 * \param path1 to compare
	 * \param path2 to compare
	 * \return true if they are the same path
	 */
	static bool IsSamePath(const std::wstring& path1, const std::wstring& path2);

	/**
	 * Checks if two path strings are equal. No conversion of slashes is done!
	 * \remark for slash-independent comparison, use IsEquivalentTo()
	 *
	 * The length-taking overloads are not redundant with the view ones: callers use
	 * them to compare a *prefix* of a longer path (see CTGitPath::IsAncestorOf),
	 * which a whole-value comparison cannot express.
	 */
	static bool ArePathStringsEqual(std::wstring_view sP1, std::wstring_view sP2);
	static bool ArePathStringsEqual(LPCWSTR sP1, LPCWSTR sP2, int length);
	static bool ArePathStringsEqualWithCase(std::wstring_view sP1, std::wstring_view sP2);
	static bool ArePathStringsEqualWithCase(LPCWSTR sP1, LPCWSTR sP2, int length);

	static std::wstring GetCopyrightForSelf();

	/**
	 * Sets the last-write-time of the file to the current time
	 */
	static bool Touch(const std::wstring& path);

	static std::wstring GetCWD();
};
