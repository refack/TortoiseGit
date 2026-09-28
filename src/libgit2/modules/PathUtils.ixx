module;
#include <windows.h>
#include "Utils/ReparseData.h"
#include "Utils/WideString.h"
#include "sanity.h"


export module PathUtils;
import std;
import gsl;
import wil;
import RIAA;
namespace fs = std::filesystem;
using std::operator""sv;

#pragma comment(lib, "Version.lib")

export namespace PathUtils
{

// ====== WINNT decrufting ======

/// The Win32 two-call idiom, with the string as the only buffer: ask with a
/// null destination for the size, then let the API write straight into the
/// std::wstring's storage. resize_and_overwrite is what makes the second
/// half possible - resize() would zero-fill the whole buffer first, and the
/// make_unique<wchar_t[]> these call sites used to allocate meant a second
/// copy on the way out.
///
/// `need` counts the terminating null and is passed through unchanged: the
/// API writes its own terminator at buf[need - 1], which is inside the
/// requested n, and returns the length *without* it - which is exactly the
/// value resize_and_overwrite wants back, so the string ends the right
/// length with no trailing null inside it.
///
/// A fill that returns 0 has failed; the string comes back empty and every
/// caller here reads that as "keep what you had".
template <typename TFill>
std::wstring FillFromWinApi(const std::int32_t need, TFill fill)
{
	if (need <= 0)
		return {};

	std::wstring out;
	out.resize_and_overwrite(gsl::narrow<size_t>(need), [&fill](wchar_t* buf, const size_t n) -> size_t {
		return fill(buf, gsl::narrow<DWORD>(n));
	});
	return out;
}

/// GetModuleFileName has no size query - it truncates, and reports the
/// buffer size back rather than the length it wanted - so the only way to
/// learn the length is to grow until the answer stops filling the buffer.
///
/// The old body used one static 64K array shared by every caller: two
/// threads asking at once overwrote each other's answer, and the pointer
/// outlived the call that produced it. TGitCache and the shell both call
/// this from more than one thread.
std::wstring GetModuleFileNameString(const HMODULE hMod)
{
	// \\?\-prefixed paths stop at 32767 characters, so that is the ceiling.
	constexpr size_t kMaxNtPath = 32768;

	std::wstring path;
	for (size_t cap = MAX_PATH; cap <= kMaxNtPath; cap *= 2)
	{
		DWORD written = 0;
		path.resize_and_overwrite(cap, [&](wchar_t* buf, const size_t n) -> size_t {
			written = GetModuleFileName(hMod, buf, gsl::narrow<DWORD>(n));
			return written;
		});

		if (written == 0)
			return {}; // a real failure, not a truncation
		if (written < cap)
			return path; // the terminator fit, so the whole path did
	}

	throw std::runtime_error("module path longer than the NT path limit");
}

/// SHGetKnownFolderPath allocates the buffer itself, so there is nothing for
/// resize_and_overwrite to do here - the single copy is the std::wstring
/// constructor, and the unique_ptr exists only to get the CoTaskMemFree
/// right on every exit path.
fs::path GetKnownFolder(const wil::KnownFolderCLSID& folderId)
{
	return wil::GetKnownFolderPath(folderId, wil::KnownFolderFlag::KF_FLAG_CREATE);
}

fs::path GetLongPathname(const std::wstring& pathStr)
{
	const auto path = fs::path(pathStr);
	if (path.empty())
		return {};

	return fs::absolute(path);
}

fs::path EnsureTGitSubfolder(const fs::path& folderId)
{
	if (folderId.empty())
		return {};

	const auto tpath = folderId / L"TortoiseGit";
	if (!fs::exists(tpath) && !fs::is_directory(tpath))
		fs::create_directory(tpath);

	return tpath;
}

/// Appends TortoiseGit's own subfolder to one of the per-user roots, creating it.
fs::path TortoiseGitSubfolderOf(const wil::KnownFolderCLSID& folderId)
{
	const auto path = GetKnownFolder(folderId);
	return EnsureTGitSubfolder(path);
}



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
void ConvertToBackslash(std::wstring& path)
{
	tgit::wstr::Replace(path, L"/"sv, L"\\"sv);
}

bool MakeSureDirectoryPathExists(const LPCWSTR path)
{
	auto internalpathbuf = std::wstring{ path };
	ConvertToBackslash(internalpathbuf);
	return std::filesystem::create_directory(internalpathbuf);
}

/**
 * Returns the version string from the VERSION resource of a dll or exe.
 * \param p_strFilename path to the dll or exe
 * \return the version string
 */
std::wstring GetVersionFromFile(const LPCWSTR p_strFilename)
{
	struct TRANSARRAY
	{
		WORD wLanguageID;
		WORD wCharacterSet;
	};

	DWORD dwReserved = 0;
	if (const DWORD dwBufferSize = GetFileVersionInfoSize(p_strFilename, &dwReserved); dwBufferSize > 0)
	{
		const auto pBuffer = std::make_unique<BYTE[]>(dwBufferSize);

		if (pBuffer)
		{
			UINT nInfoSize = 0,
				 nFixedLength = 0;
			LPSTR lpVersion = nullptr;
			VOID* lpFixedPointer;

			GetFileVersionInfo(p_strFilename,
							   0,
							   dwBufferSize,
							   pBuffer.get());

			// Check the current language
			VerQueryValue(pBuffer.get(),
						  L"\\VarFileInfo\\Translation",
						  &lpFixedPointer,
						  &nFixedLength);
			const auto lpTransArray = static_cast<TRANSARRAY*>(lpFixedPointer);

			const auto strLangProductVersion = std::format(
				L"\\StringFileInfo\\{:04x}{:04x}\\ProductVersion", lpTransArray[0].wLanguageID, lpTransArray[0].wCharacterSet
			);

			VerQueryValue(pBuffer.get(),
						  strLangProductVersion.c_str(),
						  reinterpret_cast<LPVOID*>(&lpVersion),
						  &nInfoSize);
			if (nInfoSize && lpVersion)
				return reinterpret_cast<LPCWSTR>(lpVersion);
		}
	}

	return {};
}

/**
 * returns the filename of a full path
 */
std::wstring GetFileNameFromPath(std::wstring_view sPath);

/**
 * returns the file extension from a full path
 */
std::wstring GetFileExtFromPath(std::wstring_view sPath);

/**
 * Copies a file or a folder from \a srcPath to \a destpath, creating
 * intermediate folders if necessary. If \a force is TRUE, then files
 * are overwritten if they already exist.
 * Folders are just created at the new location, no files in them get
 * copied.
 */
bool FileCopy(std::wstring srcPath, std::wstring destPath, bool force = true);

/**
 * parses a string for a path or url. If no path or url is found,
 * an empty string is returned.
 * \remark if more than one path or url is inside the string, only
 * the first one is returned.
 */
std::wstring ParsePathInString(std::wstring_view Str);

/**
 * Returns the path to the installation folder, in our case the TortoiseSVN/bin folder.
 * \remark the path returned has a trailing backslash
 */
std::wstring GetAppDirectory(HMODULE hMod = {});

/**
 * Returns the path to the installation parent folder, in our case the TortoiseSVN folder.
 * \remark the path returned has a trailing backslash
 */
std::wstring GetAppParentDirectory(HMODULE hMod = {});

std::wstring GetDocumentsDirectory();
std::wstring GetProgramsDirectory();

/**
 * Returns the path to the application data folder, in our case the %APPDATA%TortoiseSVN folder.
 * \remark the path returned has a trailing backslash
 */
std::wstring GetAppDataDirectory();
std::wstring GetLocalAppDataDirectory();

/**
 * Removes any of the following namespace prefixes from a path, if found: "\??\", "\\?\", "\\?\UNC\".
 */
void DropPathPrefixes(std::wstring& path);

/**
 * Reads a symlink's target, as UTF-8 bytes with forward slashes - the form git stores
 * a symlink blob in, which is why the out-parameter is a byte string and not a wide one.
 */
int ReadLink(LPCWSTR filename, std::string* pTargetA = {});

/**
 * Ensures that the path ends with a folder separator.
 * If the delimiter already exists, no additional delimiter will be added.
 * \param path to ensure
 */
void EnsureTrailingPathDelimiter(std::wstring& path);

/**
 * Returns a path guaranteeing that a valid path delimiter follows.
 * If the delimiter already exists, no additional delimiter will be added.
 * \param path to ensure
 * \return path including path delimiter
 */
std::wstring BuildPathWithPathDelimiter(std::wstring_view path);

/**
 * Trims a possible included trailing folder separator from the provided path.
 * \param path to trim
 */
void TrimTrailingPathDelimiter(std::wstring& path);

/**
 * ExpandFileName converts the relative file name into a fully qualified path name.
 * ExpandFileName does not verify that the resulting fully qualified path name
 * refers to an existing file, or even that the resulting path exists.
 * \param path to expand
 * \return fully qualified path name
 */
std::wstring ExpandFileName(const std::wstring& path);

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
std::wstring NormalizePath(const std::wstring& path);

/**
 * Compares two paths and returns true if they are logically the same path.
 * The function does not account for symlinks at this point in time.
 * \param path1 to compare
 * \param path2 to compare
 * \return true if they are the same path
 */
bool IsSamePath(const std::wstring& path1, const std::wstring& path2);

/**
 * Checks if two path strings are equal. No conversion of slashes is done!
 * \remark for slash-independent comparison, use IsEquivalentTo()
 *
 * The length-taking overloads are not redundant with the view ones: callers use
 * them to compare a *prefix* of a longer path (see CTGitPath::IsAncestorOf),
 * which a whole-value comparison cannot express.
 */
bool ArePathStringsEqual(std::wstring_view sP1, std::wstring_view sP2);
bool ArePathStringsEqual(gsl::not_null<LPCWSTR> sP1, gsl::not_null<LPCWSTR> sP2, int length);
bool ArePathStringsEqualWithCase(std::wstring_view sP1, std::wstring_view sP2);
bool ArePathStringsEqualWithCase(gsl::not_null<LPCWSTR> sP1, gsl::not_null<LPCWSTR> sP2, int length);

std::wstring GetCopyrightForSelf();

/**
 * Sets the last-write-time of the file to the current time
 */
bool Touch(const std::wstring& path)
{
	const CAutoFile hFile = CreateFile(path.data(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!hFile)
		return false;

	FILETIME ft;
	SYSTEMTIME st;
	GetSystemTime(&st); // Gets the current system time
	SystemTimeToFileTime(&st, &ft); // Converts the current system time to file time format
	return SetFileTime(hFile, // Sets last-write time of the file
					   nullptr, // to the converted current system time
					   nullptr,
					   &ft) != FALSE;
}

std::wstring GetFileNameFromPath(const std::wstring_view sPath)
{
	// This used to normalize every slash to a backslash and then take everything
	// after the last one. Searching for either separator is the same answer
	// without the mutable copy a std::wstring_view cannot make anyway.
	const size_t at = sPath.find_last_of(L"\\/");
	return std::wstring(at == std::wstring_view::npos ? sPath : sPath.substr(at + 1));
}

std::wstring GetFileExtFromPath(const std::wstring_view sPath)
{
	const int dotPos = tgit::wstr::ReverseFind(sPath, L'.');
	int slashPos = tgit::wstr::ReverseFind(sPath, L'\\');
	if (slashPos < 0)
		slashPos = tgit::wstr::ReverseFind(sPath, L'/');
	if (dotPos > slashPos)
		return tgit::wstr::Mid(sPath, dotPos);
	return {};
}

bool FileCopy(std::wstring srcPath, std::wstring destPath, const bool force)
{
	tgit::wstr::Replace(srcPath, L"/"sv, L"\\"sv);
	tgit::wstr::Replace(destPath, L"/"sv, L"\\"sv);
	const std::wstring destFolder = tgit::wstr::Left(destPath, tgit::wstr::ReverseFind(destPath, L'\\'));
	std::ignore = MakeSureDirectoryPathExists(destFolder.c_str());
	return (CopyFile(srcPath.c_str(), destPath.c_str(), !force));
}

std::wstring ParsePathInString(const std::wstring_view Str)
{
	int curPos = 0;
	std::wstring sToken = tgit::wstr::Tokenize(Str, L"'\t\r\n", curPos);
	while (!sToken.empty())
	{
		if (sToken.find_first_of(L"/\\") != std::wstring::npos)
		{
			tgit::wstr::Trim(sToken, L"'\"");
			return { sToken };
		}
		sToken = tgit::wstr::Tokenize(Str, L"'\t\r\n", curPos);
	}
	return {};
}

std::wstring GetAppDirectory(const HMODULE hMod /* = nullptr */)
{
	const std::wstring path = GetModuleFileNameString(hMod);
	return GetLongPathname(tgit::wstr::Left(path, tgit::wstr::ReverseFind(path, L'\\') + 1));
}

std::wstring GetAppParentDirectory(const HMODULE hMod /* = nullptr */)
{
	std::wstring path = GetAppDirectory(hMod);
	path = tgit::wstr::Left(path, tgit::wstr::ReverseFind(path, L'\\'));
	path = tgit::wstr::Left(path, tgit::wstr::ReverseFind(path, L'\\') + 1);
	return path;
}

std::wstring GetAppDataDirectory()
{
	if (const auto env = wil::GetEnvVar(wil::KnownEnvVars::XDGConfig); !env.empty())
		return EnsureTGitSubfolder(env);

	return TortoiseGitSubfolderOf(wil::KnownFolders::RoamingAppData);
}

std::wstring GetLocalAppDataDirectory()
{
	if (const auto env = wil::GetEnvVar(wil::KnownEnvVars::XDGCache); !env.empty())
		return EnsureTGitSubfolder(env);

	return TortoiseGitSubfolderOf(wil::KnownFolders::LocalAppData);
}

std::wstring GetDocumentsDirectory()
{
	if (const auto env = wil::GetEnvVar(wil::KnownEnvVars::XDGData); !env.empty())
		return EnsureTGitSubfolder(env);

	return GetKnownFolder(wil::KnownFolders::Documents);
}

std::wstring GetProgramsDirectory()
{
	if (const auto env = wil::GetEnvVar(wil::KnownEnvVars::XDGBin); !env.empty())
		return EnsureTGitSubfolder(env);

	return GetKnownFolder(wil::KnownFolders::ProgramFiles);
}

void DropPathPrefixes(std::wstring& path)
{
	constexpr wchar_t dosdevices_prefix[] = L"\\\?\?\\";
	constexpr wchar_t nt_prefix[] = L"\\\\?\\";
	constexpr wchar_t unc_prefix[] = L"UNC\\";

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

int ReadLink(const LPCWSTR filename, std::string* pTargetA)
{
	const CAutoFile handle = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!handle)
		return -1;

	DWORD ioctl_ret;
	BYTE buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE] = {};
	const auto reparse_buf = reinterpret_cast<TGIT_REPARSE_DATA_BUFFER*>(&buf);
	if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0, reparse_buf, sizeof(buf), &ioctl_ret, nullptr))
		return -1;

	if (reparse_buf->ReparseTag != IO_REPARSE_TAG_SYMLINK)
		return -1;

	const wchar_t* target = reparse_buf->ReparseBuffer.SymbolicLink.PathBuffer + (reparse_buf->ReparseBuffer.SymbolicLink.SubstituteNameOffset / sizeof(WCHAR));
	const int target_len = reparse_buf->ReparseBuffer.SymbolicLink.SubstituteNameLength / sizeof(WCHAR);
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
		tgit::wstr::Replace(targetW, L"\\"sv, L"/"sv);
		*pTargetA = CUnicodeUtils::StdGetUTF8(targetW);
	}

	return 0;
}

std::wstring GetCopyrightForSelf()
{
	const std::wstring path = GetModuleFileNameString(nullptr);

	std::wstring strReturn;
	DWORD dwReserved = 0;
	if (const DWORD dwBufferSize = GetFileVersionInfoSize(path.c_str(), &dwReserved); dwBufferSize > 0)
	{
		const auto pBuffer = std::make_unique<BYTE[]>(dwBufferSize);

		if (pBuffer)
		{
			GetFileVersionInfo(path.c_str(),
							   0,
							   dwBufferSize,
							   pBuffer.get());

			UINT nFixedLength = 0;
			VOID* lpFixedPointer;
			struct TRANSARRAY
			{
				WORD wLanguageID;
				WORD wCharacterSet;
			};
			// Check the current language
			VerQueryValue(pBuffer.get(), L"\\VarFileInfo\\Translation", &lpFixedPointer, &nFixedLength);
			const auto lpTransArray = static_cast<TRANSARRAY*>(lpFixedPointer);

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

std::wstring BuildPathWithPathDelimiter(const std::wstring_view path)
{
	std::wstring result(path);
	EnsureTrailingPathDelimiter(result);
	return result;
}

void EnsureTrailingPathDelimiter(std::wstring& path)
{
	if (!path.empty() && path.back() != L'\\')
		path += L'\\';
}

void TrimTrailingPathDelimiter(std::wstring& path)
{
	tgit::wstr::TrimRight(path, L"\\");
}

std::wstring ExpandFileName(const std::wstring& path)
{
	if (path.empty())
		return path;

	const std::wstring expanded = FillFromWinApi(GetFullPathName(path.c_str(), 0, nullptr, nullptr),
												 [&path](wchar_t* buf, const DWORD cap) { return GetFullPathName(path.c_str(), cap, buf, nullptr); });
	const auto ret = expanded.empty() ? path : expanded;
	return ret;
}

std::wstring NormalizePath(const std::wstring& path)
{
	// Account for ..\ and .\ that may occur in each path
	std::wstring nPath = ExpandFileName(path);

	tgit::wstr::MakeLower(nPath);

	TrimTrailingPathDelimiter(nPath);

	return nPath;
}

bool ArePathStringsEqual(const gsl::not_null<LPCWSTR> sP1, const gsl::not_null<LPCWSTR> sP2, int length)
{
	// We work from the end of the strings, because path differences
	// are more likely to occur at the far end of a string
	LPCWSTR pP1 = sP1 + (length - 1);
	LPCWSTR pP2 = sP2 + (length - 1);
	while (length-- > 0)
	{
		if (std::tolower(*pP1--) != std::tolower(*pP2--))
			return false;
	}
	return true;
}

bool ArePathStringsEqual(const std::wstring_view sP1, const std::wstring_view sP2)
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
	return ArePathStringsEqual(sP1.data(), sP2.data(), static_cast<int>(sP1.size()));
}

bool ArePathStringsEqualWithCase(const gsl::not_null<LPCWSTR> sP1, const gsl::not_null<LPCWSTR> sP2, int length)
{
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

bool ArePathStringsEqualWithCase(const std::wstring_view sP1, const std::wstring_view sP2)
{
	if (sP1.size() != sP2.size())
	{
		// Different lengths
		return false;
	}
	if (sP1.empty())
		return true;
	return ArePathStringsEqualWithCase(sP1.data(), sP2.data(), static_cast<int>(sP1.size()));
}

std::wstring GetCWD()
{
	const std::wstring cwd = fs::current_path();
	if (cwd.empty())
		return {};

	return GetLongPathname(cwd);
}

bool IsSamePath(const std::wstring& path1, const std::wstring& path2)
{
	return ArePathStringsEqualWithCase(NormalizePath(GetLongPathname(path1)), NormalizePath(GetLongPathname(path2)));
}


} // namespace PathUtils

export using namespace PathUtils;