module;
#include <windows.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <crtdbg.h>
#include <limits.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>
#include <git2.h>
// Both are leaves of #defines: the action names' string ids, and the shell's
// ITEMIS_* flags that GetAdminDirMask() answers in.
#include <Resources/LoglistCommonResource.h>
#include <TortoiseShell/Globals.h>
#ifdef TGIT_LFS
#include <nlohmann/json.hpp>
#endif

export module TGitPath;
import std;
import gsl;
import wstr;
import tgittypes;
import invarients;
import PathUtils;
import RIAA;
import Registry;
import DebugOutput;

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

using std::operator""sv;
using tgit::types::BYTE_VECTOR;
using tgit::types::STRING_VECTOR;

// This module is layered, and each layer uses only the ones above it:
//
//   1. detail        - string and filesystem primitives; no git, no classes.
//   2. Host          - what the process's CGit supplies. CGit (Git.h) imports
//                      this module, so this module can never import CGit back;
//                      everything it used to reach for through g_Git comes in
//                      through here instead.
//   3. GitAdminDir   - locating .git, worktree links, bare repositories. Plain
//                      functions over std::wstring; no CTGitPath.
//   4. CTGitPath     - the path value type and its caches.
//   5. CTGitPathList - the container, and git-output parsers that fill it.
//
// The two edges that used to close cycles are gone rather than hidden:
// GitAdminDir asked a CTGitPath "is this a directory?" (now detail::QueryAttributes,
// which CTGitPath uses too), and CTGitPath::Delete called
// CTGitPathList::DeleteViaShell (now a free function in layer 1).
//
// CString appears nowhere, including inside function bodies: it is a different
// type in an MFC TU than in an ATL one, and a module is compiled once for both.
// See CLAUDE.md, "The CString ABI wall".

namespace TGitPath::detail
{
// CString::Replace's contract - left to right, non-overlapping, replaced text is
// not rescanned. Not tgit::wstr::Replace, which advances by the replacement's
// length and so never terminates when the replacement is shorter than the match.
void ReplaceAll(std::wstring& s, const std::wstring_view from, const std::wstring_view to)
{
	if (from.empty())
		return;
	for (size_t pos = s.find(from); pos != std::wstring::npos; pos = s.find(from, pos + to.size()))
		s.replace(pos, from.size(), to);
}

/// Adds the trailing separator a bare drive needs: "C:" is the current directory
/// *on* C:, "C:\" is its root.
void SanitizeRootPath(std::wstring& path, const wchar_t separator)
{
	if (path.size() == 2 && path[1] == L':')
		path += separator;
}

/// The forward-slash (git) form of a path, exactly as CTGitPath stores it.
std::wstring ToFwdslash(const std::wstring_view path)
{
	std::wstring result(path);
	std::ranges::replace(result, L'\\', L'/');
	// We don't leave a trailing /
	tgit::wstr::TrimRight(result, L"/");
	ReplaceAll(result, L"//?/", L"");
	SanitizeRootPath(result, L'/');
	ReplaceAll(result, L"file:////", L"file://");
	return result;
}

/// The backslash (Windows) form of a path, exactly as CTGitPath stores it.
std::wstring ToBackslash(const std::wstring_view path)
{
	std::wstring result(path);
	std::ranges::replace(result, L'/', L'\\');
	tgit::wstr::TrimRight(result, L"\\");
	SanitizeRootPath(result, L'\\');
	return result;
}

bool FileExists(const std::wstring& path)
{
	return !!::PathFileExists(path.c_str());
}

bool IsDirectoryPath(const std::wstring& path)
{
	return !!::PathIsDirectory(path.c_str());
}

/// CStringUtils::StartsWithI.
bool StartsWithNoCase(const std::wstring_view s, const std::wstring_view prefix)
{
	return s.size() >= prefix.size() && _wcsnicmp(s.data(), prefix.data(), prefix.size()) == 0;
}

/// CStringUtils::FastCompareNoCase: ASCII-only case folding while every
/// differing character is ASCII, and the CRT's _wcsicmp over the *whole* string
/// the moment one is not. The sort order of CTGitPathList depends on this, so it
/// is reproduced rather than approximated with towlower.
int FastCompareNoCase(const std::wstring& lhs, const std::wstring& rhs)
{
	// Walks one past the shorter string on purpose: both are NUL-terminated, so
	// "ab" against "abc" ends on '\0' against 'c'.
	const size_t count = (std::min)(lhs.size(), rhs.size() + 1) + 1;
	const wchar_t* left = lhs.c_str();
	const wchar_t* right = rhs.c_str();
	for (size_t i = 0; i < count; ++i)
	{
		int leftChar = left[i];
		int rightChar = right[i];
		if (leftChar == rightChar)
		{
			if (leftChar == 0)
				return 0;
			continue;
		}
		if ((leftChar | rightChar) >= 0x80)
			return _wcsicmp(left, right);
		if (leftChar >= 'A' && leftChar <= 'Z')
			leftChar += 'a' - 'A';
		if (rightChar >= 'A' && rightChar <= 'Z')
			rightChar += 'a' - 'A';
		if (const int diff = leftChar - rightChar; diff != 0)
			return diff;
	}
	return 0;
}

/// CStringUtils::GetMatchingLength: the length of the common prefix, case-sensitive.
int GetMatchingLength(const std::wstring_view lhs, const std::wstring_view rhs)
{
	return static_cast<int>(std::ranges::mismatch(lhs, rhs).in1 - lhs.begin());
}

// Plain unique_ptrs rather than SmartLibgit2's CAutoConfig/CAutoIndex: those
// re-expose their base's conversion operator with a using-declaration that
// MSVC does not honor across the module boundary (C2248), and two owners are
// all this module needs.
using ConfigPtr = std::unique_ptr<git_config, decltype(&git_config_free)>;
using IndexPtr = std::unique_ptr<git_index, decltype(&git_index_free)>;

ConfigPtr NewConfig()
{
	git_config* config = nullptr;
	git_config_new(&config);
	return { config, &git_config_free };
}

bool DeleteViaShell(const LPCWSTR doubleNulTerminatedPaths, const bool useTrashbin, const bool showErrorUI)
{
	SHFILEOPSTRUCT shop = {};
	shop.wFunc = FO_DELETE;
	shop.pFrom = doubleNulTerminatedPaths;
	shop.fFlags = FOF_NOCONFIRMATION | FOF_NO_CONNECTED_ELEMENTS;
	if (!showErrorUI)
		shop.fFlags |= FOF_NOERRORUI | FOF_SILENT;
	if (useTrashbin)
		shop.fFlags |= FOF_ALLOWUNDO;
	return SHFileOperation(&shop) == 0;
}
} // namespace TGitPath::detail

export namespace TGitPath
{
/// What this module needs from the process's CGit, handed in rather than imported.
///
/// CGit installs this once at startup (see InstallHost). An empty member means
/// "no answer", and each has a fallback that matches what g_Git answered when it
/// had no working tree: relative paths stay relative, no config file is read,
/// and string ids resolve against the executable's own resources. The
/// safe.directory check therefore fails *closed* without a host, which is the
/// direction that check exists for.
struct Host
{
	/// g_Git.CombinePath: a path relative to the current working tree, made absolute.
	std::function<std::wstring(std::wstring_view relativePath)> combinePath;
	/// g_Git.GetHomeDirectory(), which "~/" in safe.directory expands to.
	std::function<std::wstring()> homeDirectory;
	/// The config files safe.directory is read from: g_Git.GetGitGlobalConfig(),
	/// g_Git.GetGitGlobalXDGConfig(), and the SystemConfig registry value.
	std::function<std::wstring()> globalConfigPath;
	std::function<std::wstring()> xdgConfigPath;
	std::function<std::wstring()> systemConfigPath;
	/// Loads a string resource - from the language DLL, in TortoiseGitProc.
	///
	/// This is a hook rather than a CString::LoadString here because *which*
	/// resource chain gets searched depends on the CString flavor doing the
	/// loading: MFC's walks AfxGetResourceHandle(), which is where the
	/// translation lives, and ATL's does not. This module is compiled once and
	/// cannot be both, so the flavor-aware caller does the loading.
	std::function<std::wstring(unsigned int id)> loadString;
};

void InstallHost(Host host);

/// Runs git with an argv vector and captures its output; g_Git.Run's argv
/// overload. The operations on CTGitPathList that shell out take one of these
/// rather than reaching for g_Git.
using GitRunner = std::function<int(const STRING_VECTOR& argv, BYTE_VECTOR* out, BYTE_VECTOR* err)>;

/// Was CGit::GetGitPathString: a Windows path in the forward-slash form git uses.
std::wstring ToGitPath(const std::wstring_view path)
{
	return detail::ToFwdslash(path);
}

/// Was CGit::GetGitPathStringA: the same, as the UTF-8 libgit2 takes.
std::string ToGitPathA(const std::wstring_view path)
{
	return CUnicodeUtils::StdGetUTF8(ToGitPath(path));
}

/**
 * Deletes a double-NUL-terminated list of paths through the shell.
 * \param useTrashbin if true, the items go to the Windows trash bin.
 * \param showErrorUI if true, the shell may show its own error dialog.
 */
bool DeleteViaShell(const LPCWSTR doubleNulTerminatedPaths, const bool useTrashbin, const bool showErrorUI)
{
	return detail::DeleteViaShell(doubleNulTerminatedPaths, useTrashbin, showErrorUI);
}
} // export namespace TGitPath

namespace TGitPath::detail
{
Host& TheHost()
{
	static Host host;
	return host;
}

std::wstring CombinePath(const std::wstring_view relativePath)
{
	if (const auto& combine = TheHost().combinePath)
		return combine(relativePath);
	return std::wstring(relativePath);
}

std::wstring LoadResourceString(const unsigned int id)
{
	if (const auto& load = TheHost().loadString)
		return load(id);
	// With a zero buffer size LoadString hands back a pointer into the
	// read-only resource section instead of copying.
	const wchar_t* text = nullptr;
	const int length = ::LoadStringW(::GetModuleHandleW(nullptr), id, reinterpret_cast<LPWSTR>(&text), 0);
	return length > 0 ? std::wstring(text, static_cast<size_t>(length)) : std::wstring{};
}

std::wstring AskHost(const std::function<std::wstring()>& question)
{
	return question ? question() : std::wstring{};
}

struct FileAttributes
{
	/// False when the probe failed for a reason other than "not there" - a
	/// sharing violation, say. Nothing about the item is known then, but it is
	/// reported as existing, which is the conservative answer.
	bool known = false;
	bool exists = false;
	bool isDirectory = false;
	bool isReadOnly = false;
	__int64 lastWriteTime = 0;
	__int64 fileSize = 0;
};

/// One GetFileAttributesEx, with the long-path handling CTGitPath always had:
/// 248 characters is where CreateDirectory stops accepting a path without the
/// \\?\ prefix, and a prefixed path must be absolute.
FileAttributes QueryAttributes(const std::wstring& backslashPath)
{
	std::wstring probe;
	if (backslashPath.empty())
		probe = L".";
	else if (backslashPath.size() >= 248)
		probe = L"\\\\?\\" + (::PathIsRelative(backslashPath.c_str()) ? CombinePath(backslashPath) : backslashPath);
	else
		probe = backslashPath;

	FileAttributes result;
	if (WIN32_FILE_ATTRIBUTE_DATA attribs{}; ::GetFileAttributesEx(probe.c_str(), GetFileExInfoStandard, &attribs))
	{
		result.known = true;
		result.exists = true;
		result.isDirectory = !!(attribs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
		result.isReadOnly = !!(attribs.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
		// don't cast directly to an __int64:
		// http://msdn.microsoft.com/en-us/library/windows/desktop/ms724284%28v=vs.85%29.aspx
		// "Do not cast a pointer to a FILETIME structure to either a ULARGE_INTEGER* or __int64* value
		// because it can cause alignment faults on 64-bit Windows."
		result.lastWriteTime = static_cast<__int64>(attribs.ftLastWriteTime.dwHighDateTime) << 32 | attribs.ftLastWriteTime.dwLowDateTime;
		if (!result.isDirectory)
			result.fileSize = static_cast<__int64>(attribs.nFileSizeHigh) << 32 | attribs.nFileSizeLow;
		return result;
	}

	const DWORD err = ::GetLastError();
	result.known = err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_INVALID_NAME;
	result.exists = !result.known;
	return result;
}

/// What CTGitPath(path).IsDirectory() answers, without a CTGitPath: the same
/// normalization, the same probe, and false for anything the probe cannot see.
bool IsDirectoryOnDisk(const std::wstring_view path)
{
	return QueryAttributes(ToBackslash(ToFwdslash(path))).isDirectory;
}

bool IsValidWindowsPathCharacter(const wchar_t ch)
{
	// cf. https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
	// Windows disallows ASCII control characters 0x00–0x1F
	if (ch < 32)
		return false;

	// Windows reserved path characters
	switch (ch)
	{
	case L'<':
	case L'>':
	case L':':
	case L'"':
	case L'/':
	case L'\\':
	case L'|':
	case L'?':
	case L'*':
		return false;
	default:
		return true;
	}
}

/// Whether topDir is listed in safe.directory in the global, XDG or system config.
///
/// A simplified version without any owner check, solely to prevent NTLM leaks
/// through a rooted gitdir: link; further checks are delegated to libgit and
/// libgit2. Every entry is visited: an empty value resets, so a later entry can
/// revoke an earlier one, and the last word wins.
bool IsListedInSafeDirectory(const std::wstring_view topDir)
{
	const Host& host = TheHost();
	const ConfigPtr temp = NewConfig();
	const auto add = [&temp](const std::wstring& path, const git_config_level_t level) {
		if (!path.empty())
			git_config_add_file_ondisk(temp.get(), ToGitPathA(path).c_str(), level, nullptr, FALSE);
	};
	add(AskHost(host.globalConfigPath), GIT_CONFIG_LEVEL_GLOBAL);
	add(AskHost(host.xdgConfigPath), GIT_CONFIG_LEVEL_XDG);
	add(AskHost(host.systemConfigPath), GIT_CONFIG_LEVEL_SYSTEM);
	git_config* snapshot = nullptr;
	git_config_snapshot(&snapshot, temp.get());
	const ConfigPtr config{ snapshot, &git_config_free };

	struct Payload
	{
		std::wstring_view topDir;
		const Host& host;
		bool isSafe = false;
	} payload{ topDir, host };

	const auto visit = [](const git_config_entry* entry, void* opaque) -> int {
		auto& data = *static_cast<Payload*>(opaque);
		if (!entry->value || !entry->value[0]) // reset
		{
			data.isSafe = false;
			return 0;
		}
		const std::string_view value{ entry->value };
		if (value == "*")
		{
			data.isSafe = true;
			return 0;
		}

		std::wstring valueW = CUnicodeUtils::StdGetUnicode(value);
		if (valueW.starts_with(L"~/"))
			valueW = AskHost(data.host.homeDirectory) + valueW.substr(1);
		std::ranges::replace(valueW, L'/', L'\\');
		if (valueW.ends_with(L"\\*"))
		{
			valueW.pop_back();
			data.isSafe = data.topDir.starts_with(valueW);
		}
		else
			data.isSafe = valueW == data.topDir;
		return 0;
	};
	return git_config_get_multivar_foreach(config.get(), "safe.directory", nullptr, visit, &payload) >= 0 && payload.isSafe;
}
} // namespace TGitPath::detail

namespace TGitPath
{
void InstallHost(Host host)
{
	detail::TheHost() = std::move(host);
}
}

export namespace GitAdminDir
{
/// PathFileExists, except that it refuses a UNC path naming only a server:
/// probing \\server\.git makes the SMB client write an error to the event log.
bool GitPathFileExists(const std::wstring_view path)
{
	if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
	{
		const size_t server = path.find(L'\\', 2);
		if (server == std::wstring_view::npos || path.find(L'\\', server + 1) == std::wstring_view::npos)
			return false;
	}
	return TGitPath::detail::FileExists(std::wstring(path));
}

/// Returns true if the path points to or below an admin directory.
///
/// Only the *first* "\.git" is looked at, as the wcsstr this replaces did: so
/// "C:\.github\x\.git" is not reported. \a found, if given, points at that
/// "\.git" inside \a path.
bool IsAdminDirPath(const std::wstring_view path, const wchar_t** found = nullptr)
{
	constexpr auto dotGit = L"\\.git"sv;
	const size_t at = path.find(dotGit);
	if (at == std::wstring_view::npos)
		return false;
	if (const size_t after = at + dotGit.size(); after < path.size() && path[after] != L'\\')
		return false;
	if (found)
		*found = path.data() + at;
	return true;
}

/// Returns true if the path is a bare repository. \a invalidFormat is set when
/// it looks like one but uses the reftable format, which is not supported.
bool IsBareRepo(const std::wstring_view path, bool* invalidFormat = nullptr)
{
	using TGitPath::detail::FileExists;

	if (path.empty())
		return false;

	if (IsAdminDirPath(path))
		return false;

	// don't check for \\COMPUTERNAME\HEAD
	if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\' && path.find(L'\\', 2) == std::wstring_view::npos)
		return false;

	const std::wstring base(path);
	if (!FileExists(base + L"\\HEAD") || !FileExists(base + L"\\config"))
		return false;

	if (!FileExists(base + L"\\objects\\") || !FileExists(base + L"\\refs\\"))
		return false;

	if (!TGitPath::detail::IsDirectoryPath(base + L"\\refs\\heads")) // ".git/refs/heads" is a file when reftable-format is used
	{
		if (invalidFormat)
			*invalidFormat = true;
		return false;
	}

	return true;
}

/**
 * Resolves a .git *file* (a worktree or submodule link, "gitdir: <path>") to the
 * directory it names. Returns an empty string when it names nothing usable.
 */
std::wstring ReadGitLink(const std::wstring_view topDir, const std::wstring_view dotGitPath)
{
	constexpr std::string_view GITDIR_PREFIX = "gitdir: ";

	const RIAA::CAutoFILE pFile = _wfsopen(std::wstring(dotGitPath).c_str(), L"r", SH_DENYWR);
	if (!pFile)
		return {};

	constexpr int size = 65536;
	auto buffer = std::make_unique<char[]>(size);
	const int length = static_cast<int>(fread(buffer.get(), sizeof(char), size, pFile));
	std::string_view gitPathA(buffer.get(), static_cast<size_t>(length));
	if (!gitPathA.starts_with(GITDIR_PREFIX))
		return {};
	gitPathA.remove_prefix(GITDIR_PREFIX.size());
	gitPathA = gitPathA.substr(0, gitPathA.find_last_not_of("\r\n") + 1);
	std::wstring gitPath = CUnicodeUtils::StdGetUnicode(gitPathA);
	if (gitPath.empty())
		return gitPath;

	std::ranges::replace(gitPath, L'/', L'\\');
	// cf. <https://projectzero.google/2016/02/the-definitive-guide-on-win32-to-nt.html> for an overview of special prefixes that need to be handled
	if (gitPath.size() >= 2 && gitPath[1] == L':')
	{
		if (gitPath.size() > 2 && gitPath[2] != L'\\') // drive relative paths are unsupported (also unsupported in Git for Windows)
			return {};
		PathUtils::TrimTrailingPathDelimiter(gitPath);
		return gitPath;
	}
	// gate all paths starting with a backslash that are rooted paths
	if (gitPath[0] == L'\\' && gitPath.size() >= 2 && !TGitPath::detail::IsValidWindowsPathCharacter(gitPath[1]))
	{
		const std::wstring top(topDir);
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTIONW__ L": Found path starting with backslash for worktree \"%s\" in .git-file: %s\n", top.c_str(), gitPath.c_str());

		PathUtils::TrimTrailingPathDelimiter(gitPath);
		if (gitPath.empty()) // gitPath just contained (back)slashes
			return {};

		if (!TGitPath::detail::IsListedInSafeDirectory(topDir))
			return {};
		return gitPath;
	}
	if (gitPath[0] == L'\\') // rooted path, but not UNC or otherwise special path such as `\\UNC`` or `\\??` etc.
	{
		if (topDir.size() < 2 || topDir[1] != L':') // rooted paths are only supported on drives
			return {};
		PathUtils::TrimTrailingPathDelimiter(gitPath);
		return std::wstring(topDir.substr(0, 2)) + gitPath;
	}

	gitPath = PathUtils::BuildPathWithPathDelimiter(topDir) + gitPath;
	wchar_t canonical[MAX_PATH] = {};
	::PathCanonicalize(canonical, gitPath.c_str());
	return canonical;
}

/**
 * The per-worktree admin directory: \a projectTopDir itself for a bare
 * repository, its .git directory, or what its .git file links to. Always ends
 * with "\".
 */
std::optional<std::wstring> GetWorktreeAdminDirPath(const std::wstring_view projectTopDir)
{
	if (IsBareRepo(projectTopDir))
		return PathUtils::BuildPathWithPathDelimiter(projectTopDir);

	const std::wstring dotGitPath = PathUtils::BuildPathWithPathDelimiter(projectTopDir) + L".git";
	if (TGitPath::detail::IsDirectoryOnDisk(dotGitPath))
		return PathUtils::BuildPathWithPathDelimiter(dotGitPath);

	const std::wstring linked = ReadGitLink(projectTopDir, dotGitPath);
	if (linked.empty())
		return std::nullopt;
	return PathUtils::BuildPathWithPathDelimiter(linked);
}

struct AdminDir
{
	/// Always ends with "\".
	std::wstring path;
	/// True when \a path came from the worktree's commondir, i.e. the admin
	/// directory is shared and the per-worktree one is somewhere else.
	bool isWorktree = false;
};

/**
 * The shared admin directory: for a linked worktree the one its commondir names,
 * otherwise the same as GetWorktreeAdminDirPath.
 */
std::optional<AdminDir> GetAdminDirPath(const std::wstring_view projectTopDir)
{
	const auto wtAdminDir = GetWorktreeAdminDirPath(projectTopDir);
	if (!wtAdminDir)
		return std::nullopt;

	const std::wstring pathToCommonDir = *wtAdminDir + L"commondir";
	if (!TGitPath::detail::FileExists(pathToCommonDir))
		return AdminDir{ *wtAdminDir, false };

	const RIAA::CAutoFILE pFile = _wfsopen(pathToCommonDir.c_str(), L"rb", SH_DENYWR);
	if (!pFile)
		return std::nullopt;

	constexpr int size = 65536;
	auto buffer = std::make_unique<char[]>(size);
	const int length = static_cast<int>(fread(buffer.get(), sizeof(char), size, pFile));
	std::wstring commonDir = CUnicodeUtils::StdGetUnicode(std::string_view(buffer.get(), static_cast<size_t>(length)));
	tgit::wstr::TrimRight(commonDir, L"\r\n");
	std::ranges::replace(commonDir, L'/', L'\\');
	if (::PathIsRelative(commonDir.c_str()))
		return AdminDir{ PathUtils::BuildPathWithPathDelimiter(*wtAdminDir + commonDir), true };
	return AdminDir{ PathUtils::BuildPathWithPathDelimiter(commonDir), true };
}

/**
 * Whether \a path is inside a working tree, walking up from it (from its parent,
 * when it is a file). \a projectTopDir receives the working tree's root, and
 * \a isAdminDirPath is set - and only ever set, never cleared - when \a path is
 * itself on or below a .git directory.
 *
 * isDirectory accepts a bool and nothing else. A plain `bool` parameter would
 * also take a pointer, by a standard conversion that outranks every
 * user-defined one, so HasAdminDir(path, &topDir) with a mistyped topDir would
 * compile and read the out-parameter as "this is a directory" - the exact shape
 * of CLAUDE.md's failure mode (3).
 */
bool HasAdminDir(const std::wstring_view path, const std::same_as<bool> auto isDirectory, std::wstring* projectTopDir = nullptr, bool* isAdminDirPath = nullptr)
{
	if (path.empty())
		return false;
	std::wstring dirName(path);
	if (!isDirectory)
	{
		// e.g "C:\"
		if (path.size() <= 3)
			return false;
		dirName.resize(static_cast<size_t>((std::max)(0, tgit::wstr::ReverseFind(dirName, L'\\'))));
	}

	// a .git dir or anything inside it should be left out, only interested in working copy files -- Myagi
	if (IsAdminDirPath(dirName))
	{
		if (isAdminDirPath)
			*isAdminDirPath = true;
		return false;
	}

	for (;;)
	{
		if (GitPathFileExists(dirName + L"\\.git"))
		{
			// Make sure to add the trailing slash to root paths such as 'C:'
			TGitPath::detail::SanitizeRootPath(dirName, L'\\');

			if (projectTopDir)
				*projectTopDir = dirName;

/* This code is not necessary in TGitCache as there are further checks for valid repos, however,
 * TODO: Optimize for usage in TGitCache
 */
#ifndef TGITCACHE
			const auto adminDir = GetAdminDirPath(dirName);
			if (!adminDir)
				return false;

			using TGitPath::detail::FileExists;
			if (!FileExists(adminDir->path + L"\\HEAD") || !FileExists(adminDir->path + L"\\config"))
				return false;

			if (!FileExists(adminDir->path + L"\\objects\\") || !FileExists(adminDir->path + L"\\refs\\") || !TGitPath::detail::IsDirectoryPath(adminDir->path + L"\\refs\\heads")) // ".git/refs/heads" is a file when reftable-format is used
				return false;
#endif

			return true;
		}
#ifndef TGITCACHE
		// If there is a bare repo inside a regular repo, this needs to return false so that:
		// - GetAdminDirMask() returns 0 causing the context menu of the outer repo to not be shown
		// - CShellExt::IsMemberOf shows no overlays inside the bare repo
		else if (IsBareRepo(dirName))
			return false;
#endif

		const int x = tgit::wstr::ReverseFind(dirName, L'\\');
		if (x < 2)
			break;

		dirName.resize(static_cast<size_t>(x));
		// don't check for \\COMPUTERNAME\.git
		if (dirName[0] == L'\\' && dirName[1] == L'\\' && tgit::wstr::Find(dirName, L'\\', 2) < 0)
			break;
	}

	return false;
}

/// As above, asking the filesystem whether \a path is a directory.
bool HasAdminDir(const std::wstring_view path, std::wstring* projectTopDir = nullptr)
{
	return HasAdminDir(path, TGitPath::detail::IsDirectoryPath(std::wstring(path)), projectTopDir);
}

bool IsWorkingTreeOrBareRepo(const std::wstring_view path)
{
	return HasAdminDir(path) || IsBareRepo(path);
}

/// The nearest enclosing working tree, if - and only if - it has submodules.
/// Stops at the first .git it finds either way.
std::wstring GetSuperProjectRoot(const std::wstring_view path)
{
	std::wstring projectRoot(path);
	do
	{
		if (GitPathFileExists(projectRoot + L"\\.git"))
		{
			if (GitPathFileExists(projectRoot + L"\\.gitmodules"))
				return projectRoot;
			return {};
		}

		projectRoot.resize(static_cast<size_t>((std::max)(0, tgit::wstr::ReverseFind(projectRoot, L'\\'))));

		// don't check for \\COMPUTERNAME\.git
		if (projectRoot.size() >= 2 && projectRoot[0] == L'\\' && projectRoot[1] == L'\\' && tgit::wstr::Find(projectRoot, L'\\', 2) < 0)
			return {};
	} while (tgit::wstr::ReverseFind(projectRoot, L'\\') > 0);

	return {};
}

/// Whether the admin directory \a adminDir (ending with "\") has a stash, as a
/// loose ref or in packed-refs.
bool HasStash(const std::wstring_view adminDir)
{
	const std::wstring base(adminDir);
	if (TGitPath::detail::FileExists(base + L"refs\\stash"))
		return true;

	const RIAA::CAutoFile hfile = ::CreateFile((base + L"packed-refs").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!hfile)
		return false;

	LARGE_INTEGER fileSize;
	if (!::GetFileSizeEx(hfile, &fileSize) || fileSize.QuadPart == 0 || fileSize.QuadPart >= INT_MAX)
		return false;

	auto buff = std::unique_ptr<char[]>(new (std::nothrow) char[fileSize.LowPart + 1]); // prevent default initialization and throwing on allocation error
	if (!buff)
		return false;
	DWORD size = 0;
	if (!::ReadFile(hfile, buff.get(), fileSize.LowPart, &size, nullptr))
		return false;
	buff[fileSize.LowPart] = '\0';

	if (size != fileSize.LowPart)
		return false;

	constexpr auto stashRef = "refs/stash"sv;
	for (DWORD i = 0; i < fileSize.LowPart;)
	{
		if (buff[i] == '#' || buff[i] == '^')
		{
			while (buff[i] != '\n')
			{
				++i;
				if (i == fileSize.LowPart)
					break;
			}
			++i;
		}

		if (i >= fileSize.LowPart)
			break;

		while (buff[i] != ' ')
		{
			++i;
			if (i == fileSize.LowPart)
				break;
		}

		++i;
		if (i >= fileSize.LowPart)
			break;

		if (i <= fileSize.LowPart - stashRef.size() && (buff[i + stashRef.size()] == '\n' || buff[i + stashRef.size()] == '\0') && std::string_view(buff.get() + i, stashRef.size()) == stashRef)
			return true;
		while (buff[i] != '\n')
		{
			++i;
			if (i == fileSize.LowPart)
				break;
		}

		while (buff[i] == '\n')
		{
			++i;
			if (i == fileSize.LowPart)
				break;
		}
	}
	return false;
}
} // export namespace GitAdminDir

export namespace TGitPath
{
constexpr int PARENT_MASK = 0xFFFFFF;
constexpr int MERGE_MASK  = 0x1000000;

// The string type here is std::wstring rather than CString on purpose, and it
// is not a style choice: CString is CStringT<wchar_t, StrTraitMFC_DLL<...>> in
// an MFC project and CStringT<wchar_t, StrTraitATL<...>> in an ATL one. Those
// mangle differently, so a type carrying CString members or signatures cannot
// be compiled once and linked by both TortoiseGitProc and TortoiseGit.dll.
// CTGitPath is passed through every layer of both, which makes it the widest
// piece of that boundary. See CLAUDE.md, "Making src\ cohesive".
//
// Parameters are std::wstring_view uniformly. A CString call site converts by
// spelling .GetString(), which is one implicit conversion; passing the CString
// itself would need two (operator PCWSTR, then the view's constructor) and
// will not compile - except through the explicit IsStupidString constructor.
//
// Every "is it a directory" parameter is std::same_as<bool> auto rather than
// bool, for the reason GitAdminDir::HasAdminDir gives: a bool parameter also
// accepts any pointer. That constraint is what used to be the deleted
// SetFromGit(const wchar_t*, T*) poison overload, generalized - SetFromGit(path,
// &oldPath) cannot reach a directory flag through any overload now.
class CTGitPath
{
public:
	CTGitPath() = default;
#ifdef TGIT_UNIT_TESTS
	// The test build mocks UpdateAttributes. This used to key off gmock's include
	// guard, which a module can never see - it is compiled once, not per
	// includer - so the test build has to say so when it compiles this module.
	virtual ~CTGitPath() = default;
#endif
	explicit CTGitPath(const std::wstring_view sUnknownPath) { SetFromUnknown(sUnknownPath); }
	template <tgit::wstr::IsStupidString StrT>
	explicit CTGitPath(const StrT& sUnknownPath) : CTGitPath(std::wstring_view{ sUnknownPath.GetString(), gsl::narrow<size_t>(sUnknownPath.GetLength()) }) {}
	CTGitPath(const std::wstring_view sUnknownPath, const std::same_as<bool> auto bIsDirectory) : CTGitPath(sUnknownPath)
	{
		m_bDirectoryKnown = true;
		m_bIsDirectory = bIsDirectory;
	}

	int m_ParentNo = 0;

	enum class StagingStatus
	{
		DontCare,
		TotallyStaged,
		PartiallyStaged,
		TotallyUnstaged
	};

	enum Actions : unsigned int
	{
		LOGACTIONS_ADDED	= 0x00000001,
		LOGACTIONS_MODIFIED	= 0x00000002,
		LOGACTIONS_REPLACED	= 0x00000004,
		LOGACTIONS_DELETED	= 0x00000008,
		LOGACTIONS_UNMERGED = 0x00000010,
		LOGACTIONS_COPY		= 0x00000040,
		LOGACTIONS_MERGED   = 0x00000080,
		LOGACTIONS_ASSUMEVALID = 0x00000200,
		LOGACTIONS_SKIPWORKTREE = 0x00000400,
		LOGACTIONS_MISSING  = 0x00001000,
		LOGACTIONS_UNVER	= 0x80000000,
		LOGACTIONS_IGNORE	= 0x40000000,

		// For log filter only
		LOGACTIONS_HIDE		= 0x20000000,
		LOGACTIONS_GRAY		= 0x10000000,
	};

	std::wstring m_StatAdd;
	std::wstring m_StatDel;
	StagingStatus m_stagingStatus = StagingStatus::DontCare;
#ifdef TGIT_LFS
	std::wstring m_LFSLockOwner;
#endif
	unsigned int m_Action = 0;
	bool m_Checked = false;

	static unsigned int ParseStatus(char status);
	void ParseAndUpdateStatus(const char status) { m_Action |= ParseStatus(status); }
	unsigned int ParseAndUpdateStatus(git_delta_t status);
	std::wstring GetActionName() const;
	static std::wstring GetActionName(unsigned int action);
	// Split out so the action-to-resource mapping, including its precedence
	// order, can be asserted without a resource module: loading the string
	// needs an MFC app instance that the test binary does not have.
	static unsigned int GetActionNameResourceId(unsigned int action);

	/**
	 * Set the path as it comes from git: forward slashes, relative to the
	 * working tree. \a oldPath is the path before a rename or copy.
	 */
	void SetFromGit(const std::wstring_view sPath)
	{
		Reset();
		m_sFwdslashPath = sPath;
		detail::SanitizeRootPath(m_sFwdslashPath, L'/');
	}
	void SetFromGit(const std::wstring_view sPath, const std::same_as<bool> auto bIsDirectory)
	{
		SetFromGit(sPath);
		m_bDirectoryKnown = true;
		m_bIsDirectory = bIsDirectory;
	}
	void SetFromGit(const std::wstring_view sPath, const std::wstring_view oldPath)
	{
		SetFromGit(sPath);
		m_sOldFwdslashPath = oldPath;
	}
	void SetFromGit(const std::wstring_view sPath, const std::wstring_view oldPath, const std::same_as<bool> auto bIsDirectory)
	{
		SetFromGit(sPath, bIsDirectory);
		m_sOldFwdslashPath = oldPath;
	}
	/// As SetFromGit, from the UTF-8 git and libgit2 hand out.
	void SetFromGit(const char* pPath)
	{
		Reset();
		_ASSERTE(pPath);
		if (!pPath)
			return;
		m_sFwdslashPath = CUnicodeUtils::StdGetUnicode(pPath);
		detail::SanitizeRootPath(m_sFwdslashPath, L'/');
	}
	void SetFromGit(const char* pPath, const std::same_as<bool> auto bIsDirectory)
	{
		SetFromGit(pPath);
		m_bDirectoryKnown = true;
		m_bIsDirectory = bIsDirectory;
	}

	/**
	 * Set the path as UNICODE with backslashes
	 */
	void SetFromWin(LPCWSTR pPath);
	void SetFromWin(std::wstring_view sPath);
	void SetFromWin(const LPCWSTR pPath, const std::same_as<bool> auto bIsDirectory) { SetFromWin(std::wstring_view(pPath), bIsDirectory); }
	void SetFromWin(const std::wstring_view sPath, const std::same_as<bool> auto bIsDirectory)
	{
		Reset();
		m_sBackslashPath = sPath;
		m_bIsDirectory = bIsDirectory;
		m_bDirectoryKnown = true;
		detail::SanitizeRootPath(m_sBackslashPath, L'\\');
	}
	/**
	 * Set the path from an unknown source.
	 */
	void SetFromUnknown(std::wstring_view sPath);
	/**
	 * Returns the path in Windows format, i.e. with backslashes
	 */
	LPCWSTR GetWinPath() const;
	/**
	 * Returns the path in Windows format, i.e. with backslashes
	 */
	const std::wstring& GetWinPathString() const;
	/**
	 * Returns the path with forward slashes.
	 */
	const std::wstring& GetGitPathString() const;

	const std::wstring& GetGitOldPathString() const { return m_sOldFwdslashPath; }

	/**
	 * Returns the path for showing in an UI.
	 *
	 * URL's are returned with forward slashes, unescaped if necessary
	 * Paths are returned with backward slashes
	 */
	const std::wstring& GetUIPathString() const;
	/**
	 * Returns true if the path points to a directory
	 */
	bool IsDirectory() const;

	bool IsDirectoryKnown() const { return m_bDirectoryKnown; }

	CTGitPath GetSubPath(const CTGitPath& root) const;

	/**
	 * Returns the directory. If the path points to a directory, then the path
	 * is returned unchanged. If the path points to a file, the path to the
	 * parent directory is returned.
	 */
	CTGitPath GetDirectory() const;
	/**
	 * Same as above, except in the case that the path points to nothing (to a deleted file/folder).
	 * In that case GetDirectory returns the path unchanged and GetDirectoryOrParentIfDeleted returns the path to the parent.
	 */
	CTGitPath GetDirectoryOrParentIfDeleted() const;
	/**
	* Returns the directory which contains the item the path refers to.
	* If the path is a directory, then this returns the directory above it.
	* If the path is to a file, then this returns the directory which contains the path
	* parent directory is returned.
	*/
	CTGitPath GetContainingDirectory() const;
	/**
	 * Get the 'root path' (e.g. "c:\") - Used to pass to GetDriveType
	 */
	std::wstring GetRootPathString() const;
	/**
	 * Returns the filename part of the full path.
	 * \remark don't call this for directories.
	 */
	std::wstring GetFilename() const { return GetFileOrDirectoryName(); }
	std::wstring GetBaseFilename() const;
	/**
	 * Returns the item's name without the full path.
	 */
	std::wstring GetFileOrDirectoryName() const;
	/**
	 * Returns the item's name without the full path, unescaped if necessary.
	 */
	std::wstring GetUIFileOrDirectoryName() const;
	/**
	 * Returns the file extension, including the dot.
	 * \remark Returns an empty string for directories
	 */
	std::wstring GetFileExtension() const;

	void UpdateCase();

	bool IsEmpty() const;
	void Reset();
	/**
	 * Checks if two paths are equal. The slashes are taken care of.
	 */
	bool IsEquivalentTo(const CTGitPath& rhs) const;
	bool IsEquivalentToWithoutCase(const CTGitPath& rhs) const;
	bool operator==(const CTGitPath& x) const { return IsEquivalentTo(x); }

	/**
	 * Checks if \c possibleDescendant is a child of this path.
	 */
	bool IsAncestorOf(const CTGitPath& possibleDescendant) const;
	/**
	 * Get a string representing the file path, optionally with a base
	 * section stripped off the front
	 * Returns a string with fwdslash paths
	 */
	std::wstring GetDisplayString(const CTGitPath* pOptionalBasePath = nullptr) const;
	/**
	 * Compares two paths. Slash format is irrelevant.
	 */
	static int Compare(const CTGitPath& left, const CTGitPath& right);

	/** As operator<, but for checking if paths are equivalent
	 */
	static bool PredLeftEquivalentToRight(const CTGitPath& left, const CTGitPath& right) { return left.IsEquivalentTo(right); }

	/** Checks if the left path is pointing to the same working copy path as the right.
	 * The same wc path means the paths are equivalent once all the admin dir path parts
	 * are removed. This is used in the TGitCache crawler to filter out all the 'duplicate'
	 * paths to crawl.
	 */
	static bool PredLeftSameWCPathAsRight(const CTGitPath& left, const CTGitPath& right);

	static bool CheckChild(const CTGitPath& parent, const CTGitPath& child) { return parent.IsAncestorOf(child); }

	/**
	 * appends a string to this path.
	 *\remark - missing slashes are not added - this is just a string concatenation, but with
	 * preservation of the proper caching behavior.
	 * If you want to join a file- or directory-name onto the path, you should use AppendPathString
	 */
	void AppendRawString(std::wstring_view sAppend);

	/**
	* appends a part of a path to this path.
	*\remark - missing slashes are dealt with properly. Don't use this to append a file extension, for example
	*
	*/
	void AppendPathString(std::wstring_view sAppend);

	/**
	 * Get the file modification time - returns zero for files which don't exist
	 */
	__int64 GetLastWriteTime(bool force = false) const;

	/**
	 * Get the file size. Returns zero for directories or files that don't exist.
	 */
	__int64 GetFileSize() const;

	bool IsReadOnly() const;

	/**
	 * Checks if the path really exists.
	 */
	bool Exists() const;

	/**
	 * Deletes the file/folder
	 * \param bTrash if true, uses the Windows trash bin when deleting.
	 * \param bShowErrorUI if true, shows error.
	 */
	bool Delete(bool bTrash, bool bShowErrorUI) const;

	/**
	 * Checks if a git admin directory is present. For files, the check
	 * is done in the same directory. For folders, it checks if the folder itself
	 * contains an admin directory.
	 */
	bool HasAdminDir(std::wstring* projectTopDir = nullptr, bool force = false) const;
	void SetHasAdminDir(bool hasAdminDir, std::wstring_view projectTopDir) const;
	bool HasSubmodules() const;
	bool HasGitSVNDir() const;
	bool IsBisectActive() const;
	bool IsRebaseActive() const;
	bool IsCherryPickActive() const;
	bool IsMergeActive() const;
	bool HasStashDir() const;
	bool HasRebaseApply() const;
	bool HasLFS() const;

	bool IsWCRoot() const;

	int GetAdminDirMask() const;

	bool IsRegisteredSubmoduleOfParentProject(std::wstring* parentProjectRoot = nullptr) const;

	/**
	 * Checks if the path point to or below a git admin directory (.Git).
	 */
	bool IsAdminDir() const;

	/**
	 * Checks if the path or URL is valid on Windows.
	 * A path is valid if conforms to the specs in the windows API.
	 * An URL is valid if the path checked out from it is valid
	 * on windows. That means an URL which is valid according to the WWW specs
	 * isn't necessarily valid as a windows path (e.g. http://myserver.com/repos/file:name
	 * is a valid URL, but the path is illegal on windows ("file:name" is illegal), so
	 * this function would return \c false for that URL).
	 */
	bool IsValidOnWindows() const;

	std::wstring GetAbbreviatedRename() const;

	/**
	 * Marks a path as a file by unsetting the cached IsDirectory status
	 * Used while diffing commits where a submodule changed to a file
	 */
	void UnsetDirectoryStatus() const { m_bIsDirectory = false; }
	/**
	 * Marks a path as a directory by setting the cached IsDirectory status
	 * Used while diffing commits where a file changed to a submodule
	 */
	void SetDirectoryStatus() const { m_bIsDirectory = true; }

#ifdef TGIT_UNIT_TESTS
protected:
	virtual void UpdateAttributes() const;
#endif

private:
#ifndef TGIT_UNIT_TESTS
	void UpdateAttributes() const;
#endif

	// All these functions are const, and all the data
	// is mutable, in order that the hidden caching operations
	// can be carried out on a const CTGitPath object, which is what's
	// likely to be passed between functions
	// The public 'SetFromxxx' functions are not const, and so the proper
	// const-correctness semantics are preserved
	void SetFwdslashPath(const std::wstring_view sPath) const { m_sFwdslashPath = detail::ToFwdslash(sPath); }
	void SetBackslashPath(const std::wstring_view sPath) const { m_sBackslashPath = detail::ToBackslash(sPath); }
	void EnsureBackslashPathSet() const;
	void EnsureFwdslashPathSet() const;

	/// The shared admin directory of this path's working tree, or empty - which
	/// the callers then probe relative to the current directory, as they always did.
	GitAdminDir::AdminDir AdminDirOrEmpty() const;
	/// The per-worktree admin directory, or empty.
	std::wstring WorktreeAdminDirOrEmpty() const;

	mutable std::wstring m_sBackslashPath;
	mutable std::wstring m_sFwdslashPath;
	mutable std::wstring m_sUIPath;
	mutable std::wstring m_sProjectRoot;

	//used for rename case
	mutable std::wstring m_sOldFwdslashPath;

	// Have we yet determined if this is a directory or not?
	mutable bool m_bDirectoryKnown = false;
	mutable bool m_bIsDirectory = false;
	mutable bool m_bLastWriteTimeKnown = false;
	mutable __int64 m_lastWriteTime = 0;
	mutable __int64 m_fileSize = 0;
	mutable bool m_bIsReadOnly = false;
	mutable bool m_bHasAdminDirKnown = false;
	mutable bool m_bHasAdminDir = false;
	mutable bool m_bIsValidOnWindowsKnown = false;
	mutable bool m_bIsValidOnWindows = false;
	mutable bool m_bIsAdminDirKnown = false;
	mutable bool m_bIsAdminDir = false;
	mutable bool m_bIsWCRootKnown = false;
	mutable bool m_bIsWCRoot = false;
	mutable bool m_bExists = false;
	mutable bool m_bExistsKnown = false;
};

/**
 * Compares two paths and return true if left is earlier in sort order than right
 * (Uses CTGitPath::Compare logic, but is suitable for std::sort and similar)
 */
bool operator<(const CTGitPath& left, const CTGitPath& right)
{
	return CTGitPath::Compare(left, right) < 0;
}

//////////////////////////////////////////////////////////////////////////

/**
 * \ingroup Utils
 * This class represents a list of paths
 */
class CTGitPathList
{
public:
	CTGitPathList() = default;
	// A constructor which allows a path list to be easily built with one initial entry in
	explicit CTGitPathList(const CTGitPath& firstEntry) { AddPath(firstEntry); }
	unsigned int m_Action = 0;

	void AddPath(const CTGitPath& newPath);
	bool LoadFromFile(const CTGitPath& filename);
	[[nodiscard]] bool WriteToFile(std::wstring_view sFilename, bool bUTF8 = false) const;
	[[nodiscard]] bool WriteToPathSpecFile(std::wstring_view sFilename) const;
	[[nodiscard]] const CTGitPath* LookForGitPath(std::wstring_view path) const;
	int ParserFromLog(const BYTE_VECTOR& log);
	int ParserFromLsFileSimple(const BYTE_VECTOR& out, unsigned int action, bool clear = true);
	int ParserFromLsFile(const BYTE_VECTOR& out);
	void UpdateStagingStatusFromPath(std::wstring_view path, CTGitPath::StagingStatus status);

	/// The untracked (or, with LOGACTIONS_IGNORE, ignored) files, via
	/// `git ls-files --others`, once per entry of \a filterlist or once overall.
	int FillUnRev(const GitRunner& git, unsigned int action, const CTGitPathList* filterlist = nullptr, std::wstring* err = nullptr);
#ifdef TGIT_LFS
	int FillLFSLocks(const GitRunner& git, unsigned int action, std::wstring* err = nullptr);
#ifndef TGIT_UNIT_TESTS
private:
#endif
	int ParserFromLFSLocks(unsigned int action, std::wstring_view output, std::wstring* err = nullptr);
public:
#endif
	/// The index entries of \a repository carrying any of the given flags.
	int FillBasedOnIndexFlags(git_repository* repository, unsigned short flag, unsigned short flagextended, const CTGitPathList* filterlist = nullptr);
	unsigned int GetAction() const { return m_Action; }
	/**
	 * Load from the path argument string, when the 'path' parameter is used
	 * This is a list of paths, with '*' between them
	 */
	void LoadFromAsteriskSeparatedString(std::wstring_view sPathString);
	[[nodiscard]] std::wstring CreateAsteriskSeparatedString() const;

	[[nodiscard]] int GetCount() const { return static_cast<int>(m_paths.size()); }
	[[nodiscard]] bool IsEmpty() const { return m_paths.empty(); }
	void Clear();
	const CTGitPath& operator[](std::int64_t index) const;
	[[nodiscard]] bool AreAllPathsFiles() const;
	[[nodiscard]] bool AreAllPathsDirectories() const;
	[[nodiscard]] bool AreAllPathsFilesInOneDirectory() const;
	[[nodiscard]] bool IsAnyAncestorOf(const CTGitPath& possibleDescendant) const;

	/**
	 * returns the directory which all items have in common.
	 * if not all paths are in the same directory, then
	 * an empty path is returned
	 */
	[[nodiscard]] CTGitPath GetCommonDirectory() const;
	/**
	 * returns the root path of all paths in the list.
	 * only returns an empty path if not all paths are on
	 * the same drive/root.
	 */
	[[nodiscard]] CTGitPath GetCommonRoot() const;
	void SortByPathname(bool bReverse = false);
	/**
	 * Delete all the files in the list, then clear the list.
	 * \param bTrash if true, the items are deleted using the Windows trash bin
	 * \param bFilesOnly
	 * \param bShowErrorUI if true, show error dialog box when error occurs.
	 */
	void DeleteAllFiles(bool bTrash, bool bFilesOnly = true, bool bShowErrorUI = false);
	/** Remove duplicate entries from the list (sorts the list as a side-effect */
	void RemoveDuplicates();
	/** Removes all paths which are on or in a git admin directory */
	void RemoveAdminPaths();
	void RemovePath(const CTGitPath& path);
	void RemoveItem(const CTGitPath& path);
	/**
	 * Removes all child items and leaves only the top folders. Useful if you
	 * create the list to remove them (i.e. if you remove a parent folder, the
	 * child files and folders don't have to be deleted anymore)
	 */
	void RemoveChildren();

	/** Checks if two CTGitPathLists are the same */
	bool IsEqual(const CTGitPathList& list) const;

	using PathVector = std::vector<CTGitPath>;
	PathVector m_paths;
	// If the list contains just files in one directory, then
	// this contains the directory name
	mutable CTGitPath m_commonBaseDirectory;

	auto begin() noexcept { return m_paths.begin(); }
	auto begin() const noexcept { return m_paths.cbegin(); }
	auto cbegin() const noexcept { return m_paths.cbegin(); }
	auto end() noexcept { return m_paths.end(); }
	auto end() const noexcept { return m_paths.cend(); }
	auto cend() const noexcept { return m_paths.cend(); }
};
} // export namespace TGitPath

namespace TGitPath
{
//////////////////////////////////////////////////////////////////////////
// CTGitPath: status and action names

unsigned int CTGitPath::ParseStatus(const char status)
{
	switch (status)
	{
	case 'M':
		return LOGACTIONS_MODIFIED;
	case 'R':
		return LOGACTIONS_REPLACED;
	case 'A':
		return LOGACTIONS_ADDED;
	case 'D':
		return LOGACTIONS_DELETED;
	case 'U':
		return LOGACTIONS_UNMERGED;
	case 'K':
		return LOGACTIONS_DELETED;
	case 'C':
		return LOGACTIONS_COPY;
	case 'T':
		return LOGACTIONS_MODIFIED;
	default:
		return 0;
	}
}

unsigned int CTGitPath::ParseAndUpdateStatus(const git_delta_t status)
{
	if (status == GIT_DELTA_MODIFIED)
		m_Action |= LOGACTIONS_MODIFIED;
	if (status == GIT_DELTA_RENAMED)
		m_Action |= LOGACTIONS_REPLACED;
	if (status == GIT_DELTA_ADDED)
		m_Action |= LOGACTIONS_ADDED;
	if (status == GIT_DELTA_DELETED)
		m_Action |= LOGACTIONS_DELETED;
	if (status == GIT_DELTA_UNMODIFIED)
		m_Action |= LOGACTIONS_UNMERGED;
	if (status == GIT_DELTA_COPIED)
		m_Action |= LOGACTIONS_COPY;
	if (status == GIT_DELTA_TYPECHANGE)
		m_Action |= LOGACTIONS_MODIFIED;

	return m_Action;
}

std::wstring CTGitPath::GetActionName(const unsigned int action)
{
	// This used to `return MAKEINTRESOURCE(IDS_...)` directly, which worked
	// only because the return type was CString: CStringT's PCXSTR constructor
	// checks IS_INTRESOURCE and calls LoadString for you. std::wstring has no
	// such constructor - it takes the pseudo-pointer at face value and hands it
	// to wcslen, which crashed TortoiseGitProc the moment the log list painted
	// an Action cell. The id is chosen first and loaded explicitly, by the host
	// - see Host::loadString for why this module does not load it itself.
	const unsigned int id = GetActionNameResourceId(action);
	if (std::wstring name = detail::LoadResourceString(id); !name.empty())
		return name;
	::OutputDebugStringW(std::format(L"CTGitPath::GetActionName({}) = {}", action, id).c_str());
	return {};
}

unsigned int CTGitPath::GetActionNameResourceId(const unsigned int action)
{
	unsigned int id = IDS_PATHACTIONS_UNKNOWN;
	if (action & CTGitPath::LOGACTIONS_UNMERGED)
		id = IDS_PATHACTIONS_CONFLICT;
	else if (action & CTGitPath::LOGACTIONS_ADDED)
		id = IDS_PATHACTIONS_ADD;
	else if (action & CTGitPath::LOGACTIONS_MISSING)
		id = IDS_PATHACTIONS_MISSING;
	else if (action & CTGitPath::LOGACTIONS_DELETED)
		id = IDS_PATHACTIONS_DELETE;
	else if (action & CTGitPath::LOGACTIONS_MERGED)
		id = IDS_PATHACTIONS_MERGED;
	else if (action & CTGitPath::LOGACTIONS_MODIFIED)
		id = IDS_PATHACTIONS_MODIFIED;
	else if (action & CTGitPath::LOGACTIONS_REPLACED)
		id = IDS_PATHACTIONS_RENAME;
	else if (action & CTGitPath::LOGACTIONS_COPY)
		id = IDS_PATHACTIONS_COPY;
	else if (action & CTGitPath::LOGACTIONS_ASSUMEVALID)
		id = IDS_PATHACTIONS_ASSUMEUNCHANGED;
	else if (action & CTGitPath::LOGACTIONS_SKIPWORKTREE)
		id = IDS_PATHACTIONS_SKIPWORKTREE;
	else if (action & CTGitPath::LOGACTIONS_IGNORE)
		id = IDS_PATHACTIONS_IGNORED;

	return id;
}

std::wstring CTGitPath::GetActionName() const
{
	return GetActionName(m_Action);
}

//////////////////////////////////////////////////////////////////////////
// CTGitPath: the two string forms

void CTGitPath::SetFromWin(const LPCWSTR pPath)
{
	SetFromWin(std::wstring_view(pPath));
	_ASSERTE(tgit::wstr::Find(m_sBackslashPath, L'/') < 0);
}

void CTGitPath::SetFromWin(const std::wstring_view sPath)
{
	Reset();
	m_sBackslashPath = sPath;
	detail::ReplaceAll(m_sBackslashPath, L"\\\\?\\", L"");
	detail::SanitizeRootPath(m_sBackslashPath, L'\\');
}

void CTGitPath::SetFromUnknown(const std::wstring_view sPath)
{
	Reset();
	// Just set whichever path we think is most likely to be used
	SetFwdslashPath(sPath);
}

LPCWSTR CTGitPath::GetWinPath() const
{
	if (IsEmpty())
		return L"";
	return GetWinPathString().c_str();
}

// This is a temporary function, to be used during the migration to
// the path class.  Ultimately, functions consuming paths should take a CTGitPath&, not a string
const std::wstring& CTGitPath::GetWinPathString() const
{
	if (m_sBackslashPath.empty())
		SetBackslashPath(m_sFwdslashPath);
	return m_sBackslashPath;
}

const std::wstring& CTGitPath::GetGitPathString() const
{
	if (m_sFwdslashPath.empty())
		SetFwdslashPath(m_sBackslashPath);
	return m_sFwdslashPath;
}

const std::wstring& CTGitPath::GetUIPathString() const
{
	if (m_sUIPath.empty())
		m_sUIPath = GetWinPathString();
	return m_sUIPath;
}

void CTGitPath::EnsureBackslashPathSet() const
{
	if (m_sBackslashPath.empty())
	{
		SetBackslashPath(m_sFwdslashPath);
		_ASSERTE(IsEmpty() || !m_sBackslashPath.empty());
	}
}

void CTGitPath::EnsureFwdslashPathSet() const
{
	if (m_sFwdslashPath.empty())
	{
		SetFwdslashPath(m_sBackslashPath);
		_ASSERTE(IsEmpty() || !m_sFwdslashPath.empty());
	}
}

bool CTGitPath::IsEmpty() const
{
	// Check the backward slash path first, since the chance that this
	// one is set is higher. In case of a 'false' return value it's a little
	// bit faster.
	return m_sBackslashPath.empty() && m_sFwdslashPath.empty();
}

// Reset all the caches
void CTGitPath::Reset()
{
	m_bDirectoryKnown = false;
	m_bLastWriteTimeKnown = false;
	m_bHasAdminDirKnown = false;
	m_bIsValidOnWindowsKnown = false;
	m_bIsAdminDirKnown = false;
	m_bExistsKnown = false;

	m_sBackslashPath.clear();
	m_sFwdslashPath.clear();
	m_sUIPath.clear();
	m_sProjectRoot.clear();
	m_sOldFwdslashPath.clear();

	m_Action = 0;
	m_StatAdd.clear();
	m_StatDel.clear();
	m_ParentNo = 0;
	m_stagingStatus = StagingStatus::DontCare;
	_ASSERTE(IsEmpty());
}

void CTGitPath::UpdateCase()
{
	std::wstring longPath = PathUtils::GetLongPathname(GetWinPathString()).wstring();
	PathUtils::TrimTrailingPathDelimiter(longPath);
	m_sBackslashPath = std::move(longPath);
	detail::SanitizeRootPath(m_sBackslashPath, L'\\');
	SetFwdslashPath(m_sBackslashPath);
}

void CTGitPath::AppendRawString(const std::wstring_view sAppend)
{
	EnsureFwdslashPathSet();
	std::wstring strCopy = (m_sFwdslashPath += sAppend);
	SetFromUnknown(strCopy);
}

void CTGitPath::AppendPathString(const std::wstring_view sAppend)
{
	EnsureBackslashPathSet();
	std::wstring cleanAppend(sAppend);
	std::ranges::replace(cleanAppend, L'/', L'\\');
	tgit::wstr::TrimLeft(cleanAppend, L"\\");
	tgit::wstr::TrimRight(m_sBackslashPath, L"\\");
	std::wstring strCopy = m_sBackslashPath;
	strCopy += L'\\';
	strCopy += cleanAppend;
	SetFromWin(strCopy);
}

//////////////////////////////////////////////////////////////////////////
// CTGitPath: pure path arithmetic

CTGitPath CTGitPath::GetSubPath(const CTGitPath& root) const
{
	CTGitPath path;

	if (tgit::wstr::StartsWith(GetWinPathString(), root.GetWinPathString()))
	{
		const std::wstring& str = GetWinPathString();
		path.SetFromWin(tgit::wstr::Right(str, static_cast<int>(str.size()) - static_cast<int>(root.GetWinPathString().size()) - 1));
	}
	return path;
}

CTGitPath CTGitPath::GetDirectory() const
{
	if (IsDirectory() || !Exists())
		return *this;
	return GetContainingDirectory();
}

CTGitPath CTGitPath::GetDirectoryOrParentIfDeleted() const
{
	if (IsDirectory() && Exists())
		return *this;
	return GetContainingDirectory();
}

CTGitPath CTGitPath::GetContainingDirectory() const
{
	EnsureBackslashPathSet();

	std::wstring sDirName = tgit::wstr::Left(m_sBackslashPath, tgit::wstr::ReverseFind(m_sBackslashPath, L'\\'));
	if (sDirName.size() == 2 && sDirName[1] == L':')
	{
		// This is a root directory, which needs a trailing slash
		sDirName += L'\\';
		if (sDirName == m_sBackslashPath)
		{
			// We were clearly provided with a root path to start with - we should return nothing now
			sDirName.clear();
		}
	}
	if (sDirName.size() == 1 && sDirName[0] == L'\\')
	{
		// We have an UNC path and we already are the root
		sDirName.clear();
	}
	CTGitPath retVal;
	retVal.SetFromWin(sDirName);
	return retVal;
}

std::wstring CTGitPath::GetRootPathString() const
{
	EnsureBackslashPathSet();
	// PathStripToRoot writes in place, so the buffer has to be MAX_PATH-sized
	// and then trimmed back to the NUL it leaves - which is what CStrBuf did.
	std::wstring workingPath = m_sBackslashPath;
	workingPath.resize((std::max)(workingPath.size(), static_cast<size_t>(MAX_PATH))); // MAX_PATH ok here.
	[[maybe_unused]] const BOOL stripped = ::PathStripToRoot(workingPath.data());
	_ASSERTE(stripped);
	tgit::wstr::ReleaseBuffer(workingPath);
	return workingPath;
}

std::wstring CTGitPath::GetBaseFilename() const
{
	std::wstring filename = GetFilename();
	if (const int dot = tgit::wstr::ReverseFind(filename, L'.'); dot > 0)
		filename.resize(static_cast<size_t>(dot));
	return filename;
}

std::wstring CTGitPath::GetFileOrDirectoryName() const
{
	EnsureBackslashPathSet();
	return tgit::wstr::Mid(m_sBackslashPath, tgit::wstr::ReverseFind(m_sBackslashPath, L'\\') + 1);
}

std::wstring CTGitPath::GetUIFileOrDirectoryName() const
{
	const std::wstring& uiPath = GetUIPathString();
	return tgit::wstr::Mid(uiPath, tgit::wstr::ReverseFind(uiPath, L'\\') + 1);
}

std::wstring CTGitPath::GetFileExtension() const
{
	if (!IsDirectory())
	{
		EnsureBackslashPathSet();
		const int dotPos = tgit::wstr::ReverseFind(m_sBackslashPath, L'.');
		const int slashPos = tgit::wstr::ReverseFind(m_sBackslashPath, L'\\');
		if (dotPos > slashPos)
			return tgit::wstr::Mid(m_sBackslashPath, dotPos);
	}
	return {};
}

// Test if both paths refer to the same item
// Ignores case and slash direction
bool CTGitPath::IsEquivalentTo(const CTGitPath& rhs) const
{
	// Try and find a slash direction which avoids having to convert
	// both filenames
	if (!m_sBackslashPath.empty())
	{
		// *We've* got a \ path - make sure that the RHS also has a \ path
		rhs.EnsureBackslashPathSet();
		return PathUtils::ArePathStringsEqualWithCase(m_sBackslashPath, rhs.m_sBackslashPath);
	}
	// Assume we've got a fwdslash path and make sure that the RHS has one
	rhs.EnsureFwdslashPathSet();
	return PathUtils::ArePathStringsEqualWithCase(m_sFwdslashPath, rhs.m_sFwdslashPath);
}

bool CTGitPath::IsEquivalentToWithoutCase(const CTGitPath& rhs) const
{
	// Try and find a slash direction which avoids having to convert
	// both filenames
	if (!m_sBackslashPath.empty())
	{
		// *We've* got a \ path - make sure that the RHS also has a \ path
		rhs.EnsureBackslashPathSet();
		return PathUtils::ArePathStringsEqual(m_sBackslashPath, rhs.m_sBackslashPath);
	}
	// Assume we've got a fwdslash path and make sure that the RHS has one
	rhs.EnsureFwdslashPathSet();
	return PathUtils::ArePathStringsEqual(m_sFwdslashPath, rhs.m_sFwdslashPath);
}

bool CTGitPath::IsAncestorOf(const CTGitPath& possibleDescendant) const
{
	possibleDescendant.EnsureBackslashPathSet();
	EnsureBackslashPathSet();

	if (m_sBackslashPath.empty() && ::PathIsRelative(possibleDescendant.m_sBackslashPath.c_str()))
		return true;

	if (m_sBackslashPath.size() > possibleDescendant.m_sBackslashPath.size())
		return false;

	if (!PathUtils::ArePathStringsEqual(m_sBackslashPath.c_str(), possibleDescendant.m_sBackslashPath.c_str(), static_cast<int>(m_sBackslashPath.size())))
		return false;

	if (m_sBackslashPath.size() == possibleDescendant.m_sBackslashPath.size())
		return true;

	return possibleDescendant.m_sBackslashPath[m_sBackslashPath.size()] == L'\\' ||
	       (m_sBackslashPath.size() == 3 && m_sBackslashPath[1] == L':');
}

// Get a string representing the file path, optionally with a base
// section stripped off the front.
std::wstring CTGitPath::GetDisplayString(const CTGitPath* pOptionalBasePath /* = nullptr*/) const
{
	EnsureFwdslashPathSet();
	if (pOptionalBasePath)
	{
		// Find the length of the base-path without having to do an 'ensure' on it
		const int baseLength = static_cast<int>((std::max)(pOptionalBasePath->m_sBackslashPath.size(), pOptionalBasePath->m_sFwdslashPath.size()));

		// Now, chop that baseLength of the front of the path
		std::wstring relative = tgit::wstr::Mid(m_sFwdslashPath, baseLength);
		tgit::wstr::TrimLeft(relative, L"/");
		return relative;
	}
	return m_sFwdslashPath;
}

int CTGitPath::Compare(const CTGitPath& left, const CTGitPath& right)
{
	left.EnsureBackslashPathSet();
	right.EnsureBackslashPathSet();
	return detail::FastCompareNoCase(left.m_sBackslashPath, right.m_sBackslashPath);
}

bool CTGitPath::PredLeftSameWCPathAsRight(const CTGitPath& left, const CTGitPath& right)
{
	if (left.IsAdminDir() && right.IsAdminDir())
	{
		CTGitPath l = left;
		CTGitPath r = right;
		do
		{
			l = l.GetContainingDirectory();
		} while (l.HasAdminDir());
		do
		{
			r = r.GetContainingDirectory();
		} while (r.HasAdminDir());
		return l.GetContainingDirectory().IsEquivalentTo(r.GetContainingDirectory());
	}
	return left.GetDirectory().IsEquivalentTo(right.GetDirectory());
}

bool CTGitPath::IsValidOnWindows() const
{
	if (m_bIsValidOnWindowsKnown)
		return m_bIsValidOnWindows;

	m_bIsValidOnWindows = false;
	EnsureBackslashPathSet();
	std::wstring sMatch = m_sBackslashPath + L"\r\n";
	std::wstring sPattern;
	// the 'file://' URL is just a normal windows path:
	if (detail::StartsWithNoCase(sMatch, L"file:\\\\"))
	{
		sMatch = tgit::wstr::Mid(sMatch, static_cast<int>(wcslen(L"file:\\\\")));
		tgit::wstr::TrimLeft(sMatch, L"\\");
		sPattern = L"^(\\\\\\\\\\?\\\\)?(([a-zA-Z]:|\\\\)\\\\)?(((\\.)|(\\.\\.)|([^\\\\/:\\*\\?\"\\|<> ](([^\\\\/:\\*\\?\"\\|<>\\. ])|([^\\\\/:\\*\\?\"\\|<>]*[^\\\\/:\\*\\?\"\\|<>\\. ]))?))\\\\)*[^\\\\/:\\*\\?\"\\|<> ](([^\\\\/:\\*\\?\"\\|<>\\. ])|([^\\\\/:\\*\\?\"\\|<>]*[^\\\\/:\\*\\?\"\\|<>\\. ]))?$";
	}
	else
		sPattern = L"^(\\\\\\\\\\?\\\\)?(([a-zA-Z]:|\\\\)\\\\)?(((\\.)|(\\.\\.)|([^\\\\/:\\*\\?\"\\|<> ](([^\\\\/:\\*\\?\"\\|<>\\. ])|([^\\\\/:\\*\\?\"\\|<>]*[^\\\\/:\\*\\?\"\\|<>\\. ]))?))\\\\)*[^\\\\/:\\*\\?\"\\|<> ](([^\\\\/:\\*\\?\"\\|<>\\. ])|([^\\\\/:\\*\\?\"\\|<>]*[^\\\\/:\\*\\?\"\\|<>\\. ]))?$";

	try
	{
		std::wregex rx(sPattern, std::regex_constants::icase | std::regex_constants::ECMAScript);
		std::wsmatch match;

		std::wstring rmatch = sMatch;
		if (std::regex_match(rmatch, match, rx))
		{
			if (std::wstring(match[0]).compare(sMatch) == 0)
				m_bIsValidOnWindows = true;
		}
		if (m_bIsValidOnWindows)
		{
			// now check for illegal filenames
			std::wregex rx2(L"\\\\(lpt\\d|com\\d|aux|nul|prn|con)(\\\\|$)", std::regex_constants::icase | std::regex_constants::ECMAScript);
			rmatch = m_sBackslashPath;
			if (std::regex_search(rmatch, rx2, std::regex_constants::match_default))
				m_bIsValidOnWindows = false;
		}
	}
	catch (std::exception&) {}

	m_bIsValidOnWindowsKnown = true;
	return m_bIsValidOnWindows;
}

std::wstring CTGitPath::GetAbbreviatedRename() const
{
	if (GetGitOldPathString().empty())
		return GetFileOrDirectoryName();

	// Find common prefix which ends with a slash
	auto prefix_length = 0ull;
	for (size_t i = 0, maxLength = (std::min)(m_sOldFwdslashPath.size(), m_sFwdslashPath.size()); i < maxLength; ++i)
	{
		if (m_sOldFwdslashPath[i] != m_sFwdslashPath[i])
			break;
		if (m_sOldFwdslashPath[i] == L'/')
			prefix_length = i + 1;
	}

	LPCWSTR oldName = m_sOldFwdslashPath.c_str() + m_sOldFwdslashPath.size();
	LPCWSTR newName = m_sFwdslashPath.c_str() + m_sFwdslashPath.size();

	auto suffix_length = 0ull;
	auto prefix_adjust_for_slash = (prefix_length ? 1ul : 0ul);
	while (m_sOldFwdslashPath.c_str() + prefix_length - prefix_adjust_for_slash <= oldName &&
		   m_sFwdslashPath.c_str() + prefix_length - prefix_adjust_for_slash <= newName &&
		   *oldName == *newName)
	{
		if (oldName[0] == L'/')
			suffix_length = (std::max)(m_sOldFwdslashPath.size() - (oldName - m_sOldFwdslashPath.c_str()), 0ull);
		--oldName;
		--newName;
	}

	/*
	* pfx{old_midlen => new_midlen}sfx
	* {pfx-old => pfx-new}sfx
	* pfx{sfx-old => sfx-new}
	* name-old => name-new
	*/
	// The prefix and the suffix can overlap: renaming D/E to D/F/E leaves the old
	// path with nothing of its own between them, and its mid length wants to be
	// negative. With CString's int lengths that was an "if (< 0) = 0" guard; on
	// size_t the subtraction wraps first, so max(.., 0ull) reads like the same
	// guard and is a no-op. Test the sum instead of clamping the difference.
	const auto midlen = [prefix_length, suffix_length](const size_t total) {
		return total > prefix_length + suffix_length ? total - prefix_length - suffix_length : 0ull;
	};
	const auto old_midlen = midlen(m_sOldFwdslashPath.size());
	const auto new_midlen = midlen(m_sFwdslashPath.size());

	std::wstring ret;
	if (prefix_length + suffix_length)
	{
		ret = tgit::wstr::Left(m_sOldFwdslashPath, static_cast<int>(prefix_length));
		ret += L'{';
	}
	ret += tgit::wstr::Mid(m_sOldFwdslashPath, prefix_length, old_midlen);
	ret += L" => ";
	ret += tgit::wstr::Mid(m_sFwdslashPath, prefix_length, new_midlen);
	if (prefix_length + suffix_length)
	{
		ret += L'}';
		ret += tgit::wstr::Mid(m_sFwdslashPath, m_sFwdslashPath.size() - suffix_length, suffix_length);
	}
	return ret;
}

//////////////////////////////////////////////////////////////////////////
// CTGitPath: the filesystem

void CTGitPath::UpdateAttributes() const
{
	EnsureBackslashPathSet();
	const detail::FileAttributes attributes = detail::QueryAttributes(m_sBackslashPath);
	m_bIsDirectory = attributes.isDirectory;
	m_lastWriteTime = attributes.lastWriteTime;
	m_fileSize = attributes.fileSize;
	m_bExists = attributes.exists;
	// Only a successful probe says anything about the read-only bit; a failed
	// one leaves the last answer standing, as it always did.
	if (attributes.known && attributes.exists)
		m_bIsReadOnly = attributes.isReadOnly;
	if (!attributes.known)
		return;
	m_bDirectoryKnown = true;
	m_bLastWriteTimeKnown = true;
	m_bExistsKnown = true;
}

bool CTGitPath::IsDirectory() const
{
	if (!m_bDirectoryKnown)
		UpdateAttributes();
	return m_bIsDirectory;
}

__int64 CTGitPath::GetLastWriteTime(const bool force /* = false */) const
{
	if (!m_bLastWriteTimeKnown || force)
		UpdateAttributes();
	return m_lastWriteTime;
}

__int64 CTGitPath::GetFileSize() const
{
	if (!m_bDirectoryKnown)
		UpdateAttributes();
	return m_fileSize;
}

bool CTGitPath::IsReadOnly() const
{
	if (!m_bLastWriteTimeKnown)
		UpdateAttributes();
	return m_bIsReadOnly;
}

bool CTGitPath::Exists() const
{
	if (!m_bExistsKnown)
		UpdateAttributes();
	return m_bExists;
}

bool CTGitPath::Delete(const bool bTrash, const bool bShowErrorUI) const
{
	EnsureBackslashPathSet();
	::SetFileAttributes(m_sBackslashPath.c_str(), FILE_ATTRIBUTE_NORMAL);
	bool bRet = false;
	if (Exists())
	{
		if (bTrash || IsDirectory())
		{
			// SHFileOperation takes a list, terminated by an empty entry.
			std::wstring list = m_sBackslashPath;
			list += L'\0';
			bRet = detail::DeleteViaShell(list.c_str(), bTrash, bShowErrorUI);
		}
		else
			bRet = !!::DeleteFile(m_sBackslashPath.c_str());
	}
	m_bExists = false;
	m_bExistsKnown = true;
	return bRet;
}

//////////////////////////////////////////////////////////////////////////
// CTGitPath: the working tree it belongs to

bool CTGitPath::HasAdminDir(std::wstring* projectTopDir /* = nullptr */, const bool force /* = false */) const
{
	if (m_bHasAdminDirKnown && !force)
	{
		if (projectTopDir)
			*projectTopDir = m_sProjectRoot;
		return m_bHasAdminDir;
	}

	EnsureBackslashPathSet();
	bool isAdminDir = false;
	m_sProjectRoot.clear();
	m_bHasAdminDir = GitAdminDir::HasAdminDir(m_sBackslashPath, IsDirectory(), &m_sProjectRoot, &isAdminDir);
	m_bHasAdminDirKnown = true;
	if ((m_bHasAdminDir || isAdminDir) && !m_bIsAdminDirKnown)
	{
		m_bIsAdminDir = isAdminDir;
		m_bIsAdminDirKnown = true;
	}
	if (projectTopDir)
		*projectTopDir = m_sProjectRoot;
	return m_bHasAdminDir;
}

void CTGitPath::SetHasAdminDir(const bool hasAdminDir, const std::wstring_view projectTopDir) const
{
	m_bHasAdminDir = hasAdminDir;
	if (hasAdminDir)
		m_sProjectRoot = projectTopDir;
	else
		m_sProjectRoot.clear();
	m_bHasAdminDirKnown = true;
}

GitAdminDir::AdminDir CTGitPath::AdminDirOrEmpty() const
{
	return GitAdminDir::GetAdminDirPath(m_sProjectRoot).value_or(GitAdminDir::AdminDir{});
}

std::wstring CTGitPath::WorktreeAdminDirOrEmpty() const
{
	return GitAdminDir::GetWorktreeAdminDirPath(m_sProjectRoot).value_or(std::wstring{});
}

bool CTGitPath::HasSubmodules() const
{
	return HasAdminDir() && detail::FileExists(m_sProjectRoot + L"\\.gitmodules");
}

bool CTGitPath::HasGitSVNDir() const
{
	return HasAdminDir() && detail::FileExists(AdminDirOrEmpty().path + L"svn\\.metadata");
}

bool CTGitPath::IsBisectActive() const
{
	return HasAdminDir() && detail::FileExists(WorktreeAdminDirOrEmpty() + L"BISECT_START");
}

bool CTGitPath::IsRebaseActive() const
{
	if (!HasAdminDir())
		return false;
	const std::wstring dotGitPath = WorktreeAdminDirOrEmpty();
	return detail::IsDirectoryPath(dotGitPath + L"rebase-apply") || detail::IsDirectoryPath(dotGitPath + L"tgitrebase.active");
}

bool CTGitPath::IsCherryPickActive() const
{
	return HasAdminDir() && detail::FileExists(WorktreeAdminDirOrEmpty() + L"CHERRY_PICK_HEAD");
}

bool CTGitPath::IsMergeActive() const
{
	return HasAdminDir() && detail::FileExists(WorktreeAdminDirOrEmpty() + L"MERGE_HEAD");
}

bool CTGitPath::HasStashDir() const
{
	return HasAdminDir() && GitAdminDir::HasStash(AdminDirOrEmpty().path);
}

bool CTGitPath::HasRebaseApply() const
{
	return HasAdminDir() && detail::FileExists(WorktreeAdminDirOrEmpty() + L"rebase-apply");
}

bool CTGitPath::HasLFS() const
{
	return HasAdminDir() && detail::FileExists(AdminDirOrEmpty().path + L"lfs");
}

bool CTGitPath::IsWCRoot() const
{
	if (m_bIsWCRootKnown)
		return m_bIsWCRoot;

	m_bIsWCRootKnown = true;
	m_bIsWCRoot = false;

	std::wstring topDirectory;
	if (!IsDirectory() || !HasAdminDir(&topDirectory))
		return m_bIsWCRoot;

	if (IsEquivalentToWithoutCase(CTGitPath(topDirectory)))
		m_bIsWCRoot = true;

	return m_bIsWCRoot;
}

int CTGitPath::GetAdminDirMask() const
{
	int status = 0;
	if (!HasAdminDir())
		return status;

	// ITEMIS_INGIT will be revoked if necessary in TortoiseShell/ContextMenu.cpp
	status |= ITEMIS_INGIT | ITEMIS_INVERSIONEDFOLDER;

	if (IsDirectory())
	{
		status |= ITEMIS_FOLDERINGIT;
		if (IsWCRoot())
		{
			status |= ITEMIS_WCROOT;

			if (IsRegisteredSubmoduleOfParentProject())
				status |= ITEMIS_SUBMODULE;
		}
	}

	const GitAdminDir::AdminDir adminDir = AdminDirOrEmpty();
	if (GitAdminDir::HasStash(adminDir.path))
		status |= ITEMIS_STASH;

	if (detail::FileExists(adminDir.path + L"svn\\.metadata"))
		status |= ITEMIS_GITSVN;

	const std::wstring dotGitPath = adminDir.isWorktree ? WorktreeAdminDirOrEmpty() : adminDir.path;

	if (detail::FileExists(dotGitPath + L"BISECT_START"))
		status |= ITEMIS_BISECT;

	if (detail::FileExists(dotGitPath + L"MERGE_HEAD"))
		status |= ITEMIS_MERGEACTIVE;

	if (detail::FileExists(m_sProjectRoot + L"\\.gitmodules"))
		status |= ITEMIS_SUBMODULECONTAINER;

	return status;
}

bool CTGitPath::IsRegisteredSubmoduleOfParentProject(std::wstring* parentProjectRoot /* nullptr */) const
{
	std::wstring topProjectDir;
	if (!GitAdminDir::HasAdminDir(GetWinPathString(), false, &topProjectDir))
		return false;

	if (parentProjectRoot)
		*parentProjectRoot = topProjectDir;

	const std::wstring gitmodules = topProjectDir + L"\\.gitmodules";
	if (!detail::FileExists(gitmodules))
		return false;

	const detail::ConfigPtr config = detail::NewConfig();
	git_config_add_file_ondisk(config.get(), ToGitPathA(gitmodules).c_str(), GIT_CONFIG_LEVEL_APP, nullptr, FALSE);
	std::wstring relativePath = tgit::wstr::Mid(GetWinPathString(), static_cast<int>(topProjectDir.size()));
	std::ranges::replace(relativePath, L'\\', L'/');
	tgit::wstr::Trim(relativePath, L"/");
	std::string submodulePath = CUnicodeUtils::StdGetUTF8(relativePath);
	const auto matches = [](const git_config_entry* entry, void* data) {
		return *static_cast<const std::string*>(data) == entry->value ? GIT_EUSER : 0;
	};
	return git_config_foreach_match(config.get(), "submodule\\..*\\.path", matches, &submodulePath) == GIT_EUSER;
}

bool CTGitPath::IsAdminDir() const
{
	if (m_bIsAdminDirKnown)
		return m_bIsAdminDir;

	EnsureBackslashPathSet();
	m_bIsAdminDir = GitAdminDir::IsAdminDirPath(m_sBackslashPath);
	m_bIsAdminDirKnown = true;
	if (m_bIsAdminDir && !m_bHasAdminDirKnown)
	{
		m_bHasAdminDir = false;
		m_bHasAdminDirKnown = true;
	}
	return m_bIsAdminDir;
}

//////////////////////////////////////////////////////////////////////////
// CTGitPathList: the container

void CTGitPathList::AddPath(const CTGitPath& newPath)
{
	m_paths.push_back(newPath);
	m_commonBaseDirectory.Reset();
}

void CTGitPathList::Clear()
{
	m_Action = 0;
	m_paths.clear();
	m_commonBaseDirectory.Reset();
}

const CTGitPath& CTGitPathList::operator[](const std::int64_t index) const
{
	_ASSERTE(index >= 0 && static_cast<size_t>(index) < m_paths.size());
	return m_paths[static_cast<size_t>(index)];
}

bool CTGitPathList::AreAllPathsFiles() const
{
	// Look through the vector for any directories - if we find them, return false
	return std::ranges::none_of(m_paths, &CTGitPath::IsDirectory);
}

bool CTGitPathList::AreAllPathsDirectories() const
{
	// Look through the vector for directories - if we find none, return false
	return std::ranges::all_of(m_paths, &CTGitPath::IsDirectory);
}

bool CTGitPathList::IsAnyAncestorOf(const CTGitPath& possibleDescendant) const
{
	return std::ranges::any_of(m_paths, [&possibleDescendant](const CTGitPath& path) { return path.IsAncestorOf(possibleDescendant); });
}

bool CTGitPathList::AreAllPathsFilesInOneDirectory() const
{
	// Check if all the paths are files and in the same directory
	m_commonBaseDirectory.Reset();
	for (const auto& path : m_paths)
	{
		if (path.IsDirectory())
			return false;
		const CTGitPath baseDirectory = path.GetDirectory();
		if (m_commonBaseDirectory.IsEmpty())
			m_commonBaseDirectory = baseDirectory;
		else if (!m_commonBaseDirectory.IsEquivalentTo(baseDirectory))
		{
			// Different path
			m_commonBaseDirectory.Reset();
			return false;
		}
	}
	return true;
}

CTGitPath CTGitPathList::GetCommonDirectory() const
{
	if (m_commonBaseDirectory.IsEmpty())
	{
		for (const auto& path : m_paths)
		{
			const CTGitPath baseDirectory = path.GetDirectory();
			if (m_commonBaseDirectory.IsEmpty())
				m_commonBaseDirectory = baseDirectory;
			else if (!m_commonBaseDirectory.IsEquivalentTo(baseDirectory))
			{
				// Different path
				m_commonBaseDirectory.Reset();
				break;
			}
		}
	}
	// since we only checked strings, not paths,
	// we have to make sure now that we really return a *path* here
	if (std::ranges::any_of(m_paths, [this](const CTGitPath& path) { return !m_commonBaseDirectory.IsAncestorOf(path); }))
		m_commonBaseDirectory = m_commonBaseDirectory.GetContainingDirectory();
	return m_commonBaseDirectory;
}

CTGitPath CTGitPathList::GetCommonRoot() const
{
	if (IsEmpty())
		return CTGitPath();

	if (GetCount() == 1)
		return m_paths[0];

	// first entry is common root for itself
	// (add trailing '\\' to detect partial matches of the last path element)
	std::wstring root = m_paths[0].GetWinPathString() + L'\\';
	int rootLength = static_cast<int>(root.size());

	// determine common path string prefix
	for (auto it = m_paths.cbegin() + 1; it != m_paths.cend(); ++it)
	{
		const std::wstring path = it->GetWinPathString() + L'\\';

		const int newLength = detail::GetMatchingLength(root, path);
		if (newLength != rootLength)
		{
			root.resize((std::min)(static_cast<size_t>(newLength), root.size()));
			rootLength = newLength;
		}
	}

	// remove the last (partial) path element
	if (rootLength > 0)
		root.resize(static_cast<size_t>((std::max)(0, tgit::wstr::ReverseFind(root, L'\\'))));

	// done
	return CTGitPath(root);
}

void CTGitPathList::SortByPathname(const bool bReverse /*= false*/)
{
	std::sort(m_paths.begin(), m_paths.end());
	if (bReverse)
		std::reverse(m_paths.begin(), m_paths.end());
}

void CTGitPathList::DeleteAllFiles(const bool bTrash, const bool bFilesOnly, const bool bShowErrorUI)
{
	if (m_paths.empty())
		return;
	SortByPathname(true); // nested ones first

	// SHFileOperation's list form: NUL-separated, then an empty entry.
	std::wstring sPaths;
	for (const auto& path : m_paths)
	{
		if (path.Exists() && (path.IsDirectory() != bFilesOnly || !bFilesOnly))
		{
			if (!path.IsDirectory())
				::SetFileAttributes(path.GetWinPath(), FILE_ATTRIBUTE_NORMAL);

			sPaths += path.GetWinPathString();
			sPaths += L'\0';
		}
	}
	if (sPaths.empty())
		return;
	sPaths += L'\0';
	sPaths += L'\0';
	detail::DeleteViaShell(sPaths.c_str(), bTrash, bShowErrorUI);
	Clear();
}

void CTGitPathList::RemoveDuplicates()
{
	SortByPathname();
	// Remove the duplicates
	// (Unique moves them to the end of the vector, then erase chops them off)
	m_paths.erase(std::unique(m_paths.begin(), m_paths.end(), &CTGitPath::PredLeftEquivalentToRight), m_paths.end());
}

void CTGitPathList::RemoveAdminPaths()
{
	std::erase_if(m_paths, [](const CTGitPath& path) { return path.IsAdminDir(); });
}

void CTGitPathList::RemovePath(const CTGitPath& path)
{
	if (const auto it = std::ranges::find_if(m_paths, [&path](const CTGitPath& p) { return p.IsEquivalentTo(path); }); it != m_paths.end())
		m_paths.erase(it);
}

void CTGitPathList::RemoveItem(const CTGitPath& path)
{
	if (const auto it = std::ranges::find_if(m_paths, [&path](const CTGitPath& p) { return PathUtils::ArePathStringsEqualWithCase(p.GetGitPathString(), path.GetGitPathString()); }); it != m_paths.end())
		m_paths.erase(it);
}

void CTGitPathList::RemoveChildren()
{
	// Sort paths using a custom comparator that sorts directories before files and parent directories before their children
	std::ranges::sort(m_paths, [](const CTGitPath& left, const CTGitPath& right) {
		std::wstring leftPath = left.GetWinPathString();
		std::wstring rightPath = right.GetWinPathString();
		std::ranges::replace(leftPath, L'\\', L'\1');
		std::ranges::replace(rightPath, L'\\', L'\1');
		return tgit::wstr::CompareNoCase(leftPath, rightPath) < 0;
	});
	m_paths.erase(std::unique(m_paths.begin(), m_paths.end(), &CTGitPath::CheckChild), m_paths.end());
}

bool CTGitPathList::IsEqual(const CTGitPathList& list) const
{
	return std::ranges::equal(list.m_paths, m_paths, [](const CTGitPath& l, const CTGitPath& r) { return l.IsEquivalentTo(r); });
}

const CTGitPath* CTGitPathList::LookForGitPath(const std::wstring_view path) const
{
	const auto it = std::ranges::find_if(m_paths, [path](const CTGitPath& p) { return PathUtils::ArePathStringsEqualWithCase(p.GetGitPathString(), path); });
	return it == m_paths.end() ? nullptr : &*it;
}

void CTGitPathList::UpdateStagingStatusFromPath(const std::wstring_view path, const CTGitPath::StagingStatus status)
{
	if (const auto it = std::ranges::find_if(m_paths, [path](const CTGitPath& p) { return PathUtils::ArePathStringsEqualWithCase(p.GetGitPathString(), path); }); it != m_paths.end())
		it->m_stagingStatus = status;
}

//////////////////////////////////////////////////////////////////////////
// CTGitPathList: files and strings

void CTGitPathList::LoadFromAsteriskSeparatedString(const std::wstring_view sPathString)
{
	int pos = 0;
	for (;;)
	{
		const std::wstring temp = tgit::wstr::Tokenize(sPathString, L"*", pos);
		if (temp.empty())
			break;
		AddPath(CTGitPath(PathUtils::GetLongPathname(temp).wstring()));
	}
}

std::wstring CTGitPathList::CreateAsteriskSeparatedString() const
{
	std::wstring sRet;
	for (const auto& path : m_paths)
	{
		if (!sRet.empty())
			sRet += L'*';
		sRet += path.GetWinPathString();
	}
	return sRet;
}

// The byte formats below are exactly what CStdioFile produced and consumed,
// since the other end of these files is the shell extension and git:
//   LoadFromFile     - typeBinary on a Unicode build: raw UTF-16LE, a line ends
//                      at L'\n', which is dropped; a preceding L'\r' is kept.
//   WriteToFile      - typeBinary: raw UTF-16LE, L'\n' after each path.
//   WriteToFile(bUTF8) - typeText and a CStringA: despite the name, the thread's
//                      ANSI code page, with "\r\n" after each path.
bool CTGitPathList::LoadFromFile(const CTGitPath& filename)
{
	Clear();
	const RIAA::CAutoFile file = ::CreateFile(filename.GetWinPath(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	LARGE_INTEGER size{};
	if (!file || !::GetFileSizeEx(file, &size) || size.QuadPart > std::numeric_limits<DWORD>::max())
	{
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ ": could not open target file list\n");
		return false;
	}

	std::wstring content(static_cast<size_t>(size.QuadPart) / sizeof(wchar_t), L'\0');
	DWORD read = 0;
	if (!content.empty() && !::ReadFile(file, content.data(), static_cast<DWORD>(content.size() * sizeof(wchar_t)), &read, nullptr))
	{
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ ": could not read target file list\n");
		return false;
	}
	content.resize(read / sizeof(wchar_t));

	for (const auto line : std::views::split(content, L'\n'))
	{
		// for every selected file/folder
		if (!line.empty())
			AddPath(CTGitPath(std::wstring_view(line.begin(), line.end())));
	}
	return true;
}

bool CTGitPathList::WriteToFile(const std::wstring_view sFilename, const bool bUTF8 /* = false */) const
{
	const RIAA::CAutoFile file = ::CreateFile(std::wstring(sFilename).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!file)
	{
		tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ ": could not create temp file\n");
		return false;
	}

	const auto write = [&file](const void* data, const size_t bytes) {
		DWORD written = 0;
		return ::WriteFile(file, data, gsl::narrow<DWORD>(bytes), &written, nullptr) && written == bytes;
	};
	for (const auto& path : m_paths)
	{
		const bool ok = bUTF8
			? [&] { const std::string line = CUnicodeUtils::StdGetMulti(path.GetGitPathString(), CP_THREAD_ACP) + "\r\n"; return write(line.data(), line.size()); }()
			: [&] { const std::wstring line = path.GetGitPathString() + L'\n'; return write(line.data(), line.size() * sizeof(wchar_t)); }();
		if (!ok)
		{
			tgit::DebugOutput::CTraceToOutputDebugString::Instance()(__FUNCTION__ ": could not write temp file\n");
			return false;
		}
	}
	return true;
}

bool CTGitPathList::WriteToPathSpecFile(const std::wstring_view sFilename) const
{
	const RIAA::CAutoFile hFile = ::CreateFile(std::wstring(sFilename).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
	if (!hFile)
		return false;

	// NUL-terminated UTF-8, for --pathspec-from-file with --pathspec-file-nul.
	for (const auto& path : m_paths)
	{
		std::string line = CUnicodeUtils::StdGetUTF8(path.GetGitPathString());
		line += '\0';
		DWORD dwWritten = 0;
		if (!::WriteFile(hFile, line.data(), static_cast<DWORD>(line.size()), &dwWritten, nullptr))
			return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
// CTGitPathList: git output parsers

int CTGitPathList::ParserFromLsFileSimple(const BYTE_VECTOR& out, const unsigned int action, const bool clear /*= true*/)
{
	size_t pos = 0;
	const size_t end = out.size();
	CTGitPath path;
	if (clear)
		Clear();
	while (pos < end)
	{
		const size_t endOfLine = out.find('\0', pos);
		if (endOfLine == BYTE_VECTOR::npos || endOfLine == pos || endOfLine - pos >= INT_MAX)
			return -1;

		std::wstring pathstring = CUnicodeUtils::StdGetUnicode(std::string_view(&out[pos], endOfLine - pos));
		// SetFromGit resets the path
		if (pathstring.ends_with(L'/'))
		{
			pathstring.pop_back();
			path.SetFromGit(pathstring, true);
		}
		else
			path.SetFromGit(pathstring);

		path.m_Action = action;
		AddPath(path);

		pos = out.findNextString(endOfLine);
	}
	return 0;
}

// similar code in CGit::ParseConflictHashesFromLsFile
int CTGitPathList::ParserFromLsFile(const BYTE_VECTOR& out)
{
	size_t pos = 0;
	const size_t end = out.size();
	CTGitPath path;
	Clear();
	while (pos < end)
	{
		const size_t lineStart = pos;

		// m_Action is never used and propably never worked (needs to be set after path.SetFromGit)
		// also dropped LOGACTIONS_CACHE for 'H'
		// path.m_Action=path.ParserAction(out[pos]);
		pos = out.find(' ', pos); // advance to mode
		if (pos == BYTE_VECTOR::npos)
			return -1;

		const size_t modestart = pos + 1;

		pos = out.find(' ', pos + 1); // advance to hash
		if (pos == BYTE_VECTOR::npos)
			return -1;

		pos = out.find(' ', pos + 1); // advance to Stage
		if (pos == BYTE_VECTOR::npos)
			return -1;

		const size_t stagestart = pos + 1;

		pos = out.find('\t', pos + 1); // advance to filename
		if (pos == BYTE_VECTOR::npos)
			return -1;

		++pos;
		const size_t fileNameEnd = out.find(0, pos);
		// <tag> <mode> <object> <stage>\t<file>, the object as wide as the repository's hashes
		if (fileNameEnd == BYTE_VECTOR::npos || fileNameEnd == pos || pos - lineStart != "H 100644 "sv.size() + 2 * tgit::invarients::HashSize() + " 0\t"sv.size())
			return -1;
		const std::wstring pathstring = CUnicodeUtils::StdGetUnicode(std::string_view(&out[pos], fileNameEnd - pos));
		// SetFromGit resets the path
		path.SetFromGit(pathstring, (strtol(&out[modestart], nullptr, 8) & S_IFDIR) == S_IFDIR);
		if (strtol(&out[stagestart], nullptr, 10) != 0)
		{
			if (!IsEmpty() && path == m_paths.back())
			{
				pos = out.findNextString(pos);
				continue;
			}
			path.m_Action = CTGitPath::LOGACTIONS_UNMERGED;
		}

		AddPath(path);

		pos = out.findNextString(pos);
	}
	return 0;
}

int CTGitPathList::ParserFromLog(const BYTE_VECTOR& log)
{
	static const bool mergeReplacedStatus = Registry::CRegDWORD(L"Software\\TortoiseGit\\MergeReplacedStatusKS", TRUE, false, HKEY_LOCAL_MACHINE) == TRUE; // TODO: remove kill-switch
	Clear();
	std::map<std::wstring, size_t> duplicateMap;
	size_t pos = 0;
	CTGitPath path;
	m_Action = 0;

	const size_t logend = log.size();
	while (pos < logend)
	{
		if (log[pos] == ':')
		{
			bool merged = false;
			if (pos + 1 >= logend)
				return -1;
			if (log[pos + 1] == ':')
			{
				merged = true;
				++pos;
			}

			const size_t statusEnd = log.find('\0', pos);
			/*
			 * There are at least two modes (each 6 characters) and two hashes (variable length [4, 40], cf. https://github.com/git/git/blob/master/environment.c#L18)
			 * and the status (a char + optional score), each separated by space
			 */
			if (statusEnd == BYTE_VECTOR::npos || statusEnd - pos < ((6 + 1) + (6 + 1) + (4 + 1) + (4 + 1) + 1))
				return -1;

			const int modeOld = strtol(&log[pos + 1], nullptr, 8);
			const int modeNew = strtol(&log[pos + 7], nullptr, 8);
			// find start of status character
			size_t statusStart = log.find(' ', statusEnd - 6); // status: "A", "D", "U" or "C100" etc., 6 is chosen to find its start without interferring with the dst hash, see comment above
			if (statusStart == BYTE_VECTOR::npos)
				return -1;

			++statusStart;
			pos = log.find('\0', statusStart); // advance to filename
			if (pos == BYTE_VECTOR::npos || statusStart == pos)
				return -1;
			++pos;

			std::wstring oldPathname;
			if (log[statusStart] == 'C' || log[statusStart] == 'R')
			{
				const size_t filenameEnd = log.find('\0', pos);
				if (filenameEnd == BYTE_VECTOR::npos || pos == filenameEnd || filenameEnd - pos >= INT_MAX)
					return -1;
				// old filename before rename
				oldPathname = CUnicodeUtils::StdGetUnicode(std::string_view(&log[pos], filenameEnd - pos));
				pos = filenameEnd + 1;
			}
			const size_t filenameEnd = log.find('\0', pos);
			if (filenameEnd == BYTE_VECTOR::npos || pos == filenameEnd || filenameEnd - pos >= INT_MAX)
				return -1;
			const std::wstring pathname = CUnicodeUtils::StdGetUnicode(std::string_view(&log[pos], filenameEnd - pos));
			pos = filenameEnd + 1;

			if (const auto existing = duplicateMap.find(pathname); existing != duplicateMap.end())
			{
				CTGitPath& p = m_paths[existing->second];
				if (!(mergeReplacedStatus && p.m_Action == CTGitPath::LOGACTIONS_REPLACED && (log[statusStart] == 'A' || log[statusStart] == 'D')))
					p.ParseAndUpdateStatus(log[statusStart]);

				// reset submodule/folder status if a staged entry is not a folder
				if (p.IsDirectory() && ((modeOld && !(modeOld & S_IFDIR)) || (modeNew && !(modeNew & S_IFDIR))))
					p.UnsetDirectoryStatus();
				else if (!p.IsDirectory() && (modeNew && (modeNew & S_IFDIR)))
					p.SetDirectoryStatus();

				if (merged)
					p.m_Action |= CTGitPath::LOGACTIONS_MERGED;
				m_Action |= p.m_Action;
			}
			else
			{
				unsigned int ac = CTGitPath::ParseStatus(log[statusStart]);
				ac |= merged ? CTGitPath::LOGACTIONS_MERGED : 0;

				const int relevantMode = (ac & (CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_UNMERGED)) ? modeOld : modeNew;
				const bool isSubmodule = (relevantMode & S_IFDIR) == S_IFDIR;

				// SetFromGit resets the path, hence action must be set afterwards
				path.SetFromGit(pathname, oldPathname, isSubmodule);
				path.m_Action = ac;
				m_Action |= ac;

				AddPath(path);
				duplicateMap.emplace(path.GetGitPathString(), m_paths.size() - 1);
				if (mergeReplacedStatus && !oldPathname.empty())
					duplicateMap.emplace(path.GetGitOldPathString(), m_paths.size() - 1);
			}
		}
		else // numstat output
		{
			size_t tabstart = log.find('\t', pos); // find end of first number (added lines)
			if (tabstart == BYTE_VECTOR::npos || tabstart - pos >= INT_MAX)
				return -1;

			const std::wstring statAdd = CUnicodeUtils::StdGetUnicode(std::string_view(&log[pos], tabstart - pos));
			pos = tabstart + 1;

			tabstart = log.find('\t', pos); // find end of second number (removed lines)
			if (tabstart == BYTE_VECTOR::npos || tabstart - pos >= INT_MAX)
				return -1;

			const std::wstring statDel = CUnicodeUtils::StdGetUnicode(std::string_view(&log[pos], tabstart - pos));
			pos = tabstart + 1;

			if (pos >= logend)
				return -1;

			std::wstring oldPathname;
			if (log[pos] == '\0') // rename which holds an "old" pathname
			{
				++pos;
				const size_t endPathname = log.find('\0', pos);
				if (endPathname == BYTE_VECTOR::npos || pos == endPathname || endPathname - pos >= INT_MAX)
					return -1;
				oldPathname = CUnicodeUtils::StdGetUnicode(std::string_view(&log[pos], endPathname - pos));
				pos = endPathname + 1;
			}
			const size_t endPathname = log.find('\0', pos);
			if (endPathname == BYTE_VECTOR::npos || pos == endPathname || endPathname - pos >= INT_MAX)
				return -1;
			const std::wstring pathname = CUnicodeUtils::StdGetUnicode(std::string_view(&log[pos], endPathname - pos));
			pos = endPathname + 1;

			// SetFromGit resets the path. numstat does not say, so "not a directory".
			path.SetFromGit(pathname, oldPathname, false);

			if (const auto existing = duplicateMap.find(path.GetGitPathString()); existing != duplicateMap.end())
			{
				CTGitPath& p = m_paths[existing->second];
				p.m_StatAdd = statAdd;
				p.m_StatDel = statDel;
			}
			else
			{
				path.m_StatAdd = statAdd;
				path.m_StatDel = statDel;
				AddPath(path);
				duplicateMap.emplace(path.GetGitPathString(), m_paths.size() - 1);
			}
		}
	}
	return 0;
}

#ifdef TGIT_LFS
int CTGitPathList::ParserFromLFSLocks(const unsigned int action, const std::wstring_view output, std::wstring* err)
{
	using json = nlohmann::json;
	Clear();

	if (output.empty())
		return 0;

	try
	{
		auto result = json::parse(CUnicodeUtils::StdGetUTF8(output));
		for (auto& r : result)
		{
			if (r["id"].get<std::string>().empty())
				continue;
			CTGitPath gitPath;
			gitPath.SetFromGit(CUnicodeUtils::StdGetUnicode(r["path"].get<std::string>()));
			gitPath.m_Action = action;
			gitPath.m_LFSLockOwner = CUnicodeUtils::StdGetUnicode(r["owner"]["name"].get<std::string>());
			AddPath(gitPath);
		}
	}
	catch (json::parse_error& ex)
	{
		if (err)
			err->append(CUnicodeUtils::StdGetUnicode(ex.what()));
		return -1;
	}
	return 0;
}
#endif

//////////////////////////////////////////////////////////////////////////
// CTGitPathList: filled by running git

int CTGitPathList::FillUnRev(const GitRunner& git, const unsigned int action, const CTGitPathList* filterlist, std::wstring* err)
{
	Clear();

	const int count = filterlist ? filterlist->GetCount() : 1;
	for (int i = 0; i < count; ++i)
	{
		STRING_VECTOR cmd{ L"git.exe", L"ls-files", L"--exclude-standard", L"--full-name", L"--others", L"-z" };
		if (action & CTGitPath::LOGACTIONS_IGNORE)
			cmd.emplace_back(L"-i");

		if (filterlist)
		{
			_ASSERTE(!(*filterlist)[i].GetWinPathString().empty());
			cmd.insert(cmd.cend(), { L"--", (*filterlist)[i].GetGitPathString() });
		}

		BYTE_VECTOR out, errb;
		if (git(cmd, &out, &errb))
		{
			if (err)
				*err = errb.Decode();
			return -1;
		}

		if (ParserFromLsFileSimple(out, action, false) < 0)
			return -1;
	}
	return 0;
}

#ifdef TGIT_LFS
int CTGitPathList::FillLFSLocks(const GitRunner& git, const unsigned int action, std::wstring* err)
{
	Clear();

	BYTE_VECTOR out, errb;
	if (git({ L"git.exe", L"lfs", L"locks", L"--json" }, &out, &errb) != 0)
	{
		if (err)
			err->append(CUnicodeUtils::StdGetUnicode(std::string_view(errb.data(), errb.size())));
		return -1;
	}

	return ParserFromLFSLocks(action, CUnicodeUtils::StdGetUnicode(std::string_view(out.data(), out.size())), err);
}
#endif

int CTGitPathList::FillBasedOnIndexFlags(git_repository* const repository, const unsigned short flag, const unsigned short flagextended, const CTGitPathList* filterlist /*nullptr*/)
{
	Clear();
	CTGitPath path;

	if (!repository)
		return -1;

	git_index* rawIndex = nullptr;
	if (git_repository_index(&rawIndex, repository))
		return -1;
	const detail::IndexPtr index{ rawIndex, &git_index_free };

	const int count = filterlist ? filterlist->GetCount() : 1;
	for (int j = 0; j < count; ++j)
	{
		for (size_t i = 0, ecount = git_index_entrycount(index.get()); i < ecount; ++i)
		{
			const git_index_entry* e = git_index_get_byindex(index.get(), i);

			if (!e || !((e->flags & flag) || (e->flags_extended & flagextended)) || !e->path)
				continue;

			const std::wstring one = CUnicodeUtils::StdGetUnicode(e->path);

			if (filterlist)
			{
				const CTGitPath& filter = (*filterlist)[j];
				const bool matches = filter.GetWinPathString().empty()
					|| one == filter.GetGitPathString()
					|| (detail::IsDirectoryPath(detail::CombinePath(filter.GetWinPathString())) && one.starts_with(filter.GetGitPathString() + L'/'));
				if (!matches)
					continue;
			}

			//SetFromGit will clear all status
			path.SetFromGit(one, (e->mode & S_IFDIR) == S_IFDIR);
			if (e->flags_extended & GIT_INDEX_ENTRY_SKIP_WORKTREE)
				path.m_Action = CTGitPath::LOGACTIONS_SKIPWORKTREE;
			else if (e->flags & GIT_INDEX_ENTRY_VALID)
				path.m_Action = CTGitPath::LOGACTIONS_ASSUMEVALID;
			AddPath(path);
		}
	}
	RemoveDuplicates();
	return 0;
}
} // namespace TGitPath

export using namespace TGitPath;
export using namespace GitAdminDir;
