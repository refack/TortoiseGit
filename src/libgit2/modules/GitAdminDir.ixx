module;
#include <atlstr.h>
#include <git2/config.h>

export module GitAdminDir;
import std;
import gsl;
import wil;
import wstr;

import RIAA;
import PathUtils;
import DebugOutput;
import TGitPath;
import StringUtils;
import Registry;
import SmartLibgit2;

using std::operator""sv;
using std::operator""s;
using std::operator+;
using std::wstring_view;
using std::wstring;
namespace fs = std::filesystem;
using tgit::wstr::StringView;


export namespace GitAdminDir {

class GitAdminDir
{
public:
	GitAdminDir() = delete;

	/// Returns true if the path points to or below an admin directory
	static bool IsAdminDirPath(std::wstring_view path, const WCHAR** found = nullptr);

	static bool IsWorkingTreeOrBareRepo(const CString& path);

	/// Returns true if the path points is a bare repository
	static bool IsBareRepo(std::wstring_view path, bool* invalidFormat = nullptr);

	/// Returns true if the path (file or folder) has an admin directory
	/// associated, i.e. if the path is in a working copy.
	static bool HasAdminDir(const CString& path);
	static bool HasAdminDir(const CString& path, CString* ProjectTopDir);
	// IsAdminDirPath is only touched/set to true if and only if the path is an AdminDirPath
	static bool HasAdminDir(const CString& path, bool bDir, CString* ProjectTopDir = nullptr, bool* IsAdminDirPath = nullptr);
	static CString GetSuperProjectRoot(const CString& path_);

	static bool GetAdminDirPath(const CString& projectTopDir, CString& adminDir, bool* isWorktree = nullptr);
	static bool GetWorktreeAdminDirPath(const CString& projectTopDir, CString& adminDir);
	static CString ReadGitLink(const CString& topDir, const CString& dotGitPath);

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
	static CAutoConfig config;
#endif
};

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
CAutoConfig GitAdminDir::config;
#endif

CString GitAdminDir::GetSuperProjectRoot(const CString& path_)
{
	for (auto path = fs::path(path_.GetString()); path.has_relative_path(); path = path.parent_path())
		if (CGit::GitPathFileExists(path / ".git") && CGit::GitPathFileExists(path / ".gitmodules"))
			return CUnicodeUtils::GetUnicode<CString>(path.string());

	return {};

}

bool GitAdminDir::IsWorkingTreeOrBareRepo(const CString& path)
{
	return HasAdminDir(path) || IsBareRepo({path, gsl::narrow<size_t>(path.GetLength())});
}

bool GitAdminDir::HasAdminDir(const CString& path)
{
	return HasAdminDir(path, !!PathIsDirectory(path));
}

bool GitAdminDir::HasAdminDir(const CString& path,CString* ProjectTopDir)
{
	return HasAdminDir(path, !!PathIsDirectory(path),ProjectTopDir);
}

bool GitAdminDir::HasAdminDir(const CString& path, const bool bDir, CString* ProjectTopDir, bool* IsAdminDirPath)
{
	if (path.IsEmpty())
		return false;
	CString sDirName = path;
	if (!bDir)
	{
		// e.g "C:\"
		if (path.GetLength() <= 3)
			return false;
		sDirName.Truncate(std::max(0, sDirName.ReverseFind(L'\\')));
	}

	// a .git dir or anything inside it should be left out, only interested in working copy files -- Myagi
	if (GitAdminDir::IsAdminDirPath(StringView{sDirName}))
	{
		if (IsAdminDirPath)
			*IsAdminDirPath = true;
		return false;
	}

	for (;;)
	{
		if (CGit::GitPathFileExists(StringView{sDirName + L"\\.git"}))
		{
			// Make sure to add the trailing slash to root paths such as 'C:'
			if (sDirName.GetLength() == 2 && sDirName[1] == L':')
				sDirName += L'\\';

			if (ProjectTopDir)
				*ProjectTopDir = sDirName;

/* This code is not necessary in TGitCache as there are further checks for valid repos, however,
 * TODO: Optimize for usage in TGitCache
 */
#ifndef TGITCACHE
			CString adminDir;
			if (!GetAdminDirPath(sDirName, adminDir))
				return false;

			if (!PathFileExists(adminDir + L"\\HEAD") || !PathFileExists(adminDir + L"\\config"))
				return false;

			if (!PathFileExists(adminDir + L"\\objects\\") || !PathFileExists(adminDir + L"\\refs\\") || !PathIsDirectory(adminDir + L"\\refs\\heads")) // ".git/refs/heads" is a file when reftable-format is used
				return false;
#endif

			return true;
		}
#ifndef TGITCACHE
		// If there is a bare repo inside a regular repo, this needs to return false so that:
		// - GetAdminDirMask() returns 0 causing the context menu of the outer repo to not be shown
		// - CShellExt::IsMemberOf shows no overlays inside the bare repo
		else if (IsBareRepo(StringView{sDirName}))
			return false;
#endif

		const int x = sDirName.ReverseFind(L'\\');
		if (x < 2)
			break;

		sDirName.Truncate(x);
		// don't check for \\COMPUTERNAME\.git
		if (sDirName[0] == L'\\' && sDirName[1] == L'\\' && sDirName.Find(L'\\', 2) < 0)
			break;
	}

	return false;
}
/**
 * Returns the .git-path (if .git is a file, read the repository path and return it)
 * adminDir always ends with "\"
 */
bool GitAdminDir::GetAdminDirPath(const CString& projectTopDir, CString& adminDir, bool* isWorktree)
{
	CString wtAdminDir;
	if (!GetWorktreeAdminDirPath(projectTopDir, wtAdminDir))
		return false;

	CString pathToCommonDir = wtAdminDir + L"commondir";
	if (!PathFileExists(pathToCommonDir))
	{
		adminDir = wtAdminDir;
		if (isWorktree)
			*isWorktree = false;
		return true;
	}

	CAutoFILE pFile = _wfsopen(pathToCommonDir, L"rb", SH_DENYWR);
	if (!pFile)
		return false;

	constexpr int size = 65536;
	CStringA commonDirA;
	const int length = static_cast<int>(fread(commonDirA.GetBufferSetLength(size), sizeof(char), size, pFile));
	commonDirA.ReleaseBuffer(length);
	CString commonDir = CUnicodeUtils::GetUnicode<CString>(commonDirA);
	commonDir.TrimRight(L"\r\n");
	commonDir.Replace(L'/', L'\\');
	if (PathIsRelative(commonDir))
		adminDir = CPathUtils::BuildPathWithPathDelimiter(tgit::wstr::StringView(wtAdminDir + commonDir)).c_str();
	else
		adminDir = CPathUtils::BuildPathWithPathDelimiter(tgit::wstr::StringView(commonDir)).c_str();
	if (isWorktree)
		*isWorktree = true;
	return true;
}

bool GitAdminDir::GetWorktreeAdminDirPath(const CString& projectTopDir, CString& adminDir)
{
	if (IsBareRepo(StringView{projectTopDir}))
	{
		adminDir = CPathUtils::BuildPathWithPathDelimiter(tgit::wstr::StringView(projectTopDir)).c_str();
		return true;
	}

	const std::wstring sDotGitPath = CPathUtils::BuildPathWithPathDelimiter(tgit::wstr::StringView(projectTopDir)) + L".git";
	if (CTGitPath(sDotGitPath).IsDirectory())
	{
		adminDir = CPathUtils::BuildPathWithPathDelimiter(sDotGitPath).c_str();
		return true;
	}
	else
	{
		const CString result = ReadGitLink(projectTopDir, sDotGitPath.c_str());
		if (result.IsEmpty())
			return false;
		adminDir = CPathUtils::BuildPathWithPathDelimiter(tgit::wstr::StringView(result)).c_str();
		return true;
	}
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
		[[fallthrough]];
	case L'>':
		[[fallthrough]];
	case L':':
		[[fallthrough]];
	case L'"':
		[[fallthrough]];
	case L'/':
		[[fallthrough]];
	case L'\\':
		[[fallthrough]];
	case L'|':
		[[fallthrough]];
	case L'?':
		[[fallthrough]];
	case L'*':
		return false;

	default:
		return true;
	}
}

constexpr std::string_view GITDIR_PREFIX = "gitdir: ";


bool GitAdminDir::IsAdminDirPath(const std::wstring_view path, const WCHAR** found)
{
	constexpr auto git_sv = L"\\.git"sv;
	// Search for "\\.git\\" in the middle or "\\.git" at the end
	const auto iFound = path.find(git_sv);
	if (iFound != std::wstring_view::npos)
		return false;
	const auto pFound = path.data() + iFound;
	if (pFound[git_sv.length()] == L'\\' || pFound[git_sv.length()] == L'\0')
	{
		*found = pFound;
		return true;
	}
	return false;
}


bool GitAdminDir::IsBareRepo(const std::wstring_view path, bool* invalidFormat)
{
	using std::filesystem::exists;

	if (path.empty())
		return false;

	if (IsAdminDirPath(path))
		return false;

	// don't check for \\COMPUTERNAME\HEAD
	if (path[0] == L'\\' && path[1] == L'\\')
	{
		if (path.find(L'\\', 2) == wstring::npos)
			return false;
	}

	const auto pathBuf = std::wstring{path};
	if (!exists(pathBuf + L"\\HEAD") || !exists(pathBuf + L"\\config"))
		return false;

	if (!exists(pathBuf + L"\\objects\\") || !exists(pathBuf + L"\\refs\\"))
		return false;

	if (std::filesystem::is_directory(pathBuf + L"\\refs\\heads")) // ".git/refs/heads" is a file when reftable-format is used
	{
		if (invalidFormat)
			*invalidFormat = true;
		return false;
	}

	return true;
}

CString GitAdminDir::ReadGitLink(const CString& topDir, const CString& dotGitPath)
{
	const wil::unique_file pFile{_wfsopen(dotGitPath, L"r", SH_DENYWR)};

	if (!pFile)
		return L"";

	constexpr int size = 65536;
	auto buffer = std::make_unique<char[]>(size);
	const int length = static_cast<int>(fread(buffer.get(), sizeof(char), size, pFile.get()));
	std::string_view gitPathA(buffer.get(), length);
	if (!gitPathA.starts_with(GITDIR_PREFIX))
		return L"";
	std::wstring gitPath = CUnicodeUtils::StdGetUnicode(StringUtils::CStringUtils::TrimRight(gitPathA.substr(GITDIR_PREFIX.size()), "\r\n"));
	if (gitPath.empty())
		return gitPath.c_str();

	tgit::wstr::Replace(gitPath, L'/', L'\\');
	// https://projectzero.google/2016/02/the-definitive-guide-on-win32-to-nt.html for an overview of special prefixes that need to be handled
	if (gitPath.size() >= 2 && gitPath[1] == L':')
	{
		if (gitPath.size() > 2 && gitPath[2] != L'\\') // drive relative paths are unsupported (also unsupported in Git for Windows)
			return {};
		PathUtils::CPathUtils::TrimTrailingPathDelimiter(gitPath);
		return gitPath.c_str();
	}
	// gate all paths starting with a backslash that are rooted paths
	// if (gitPath[0] == L'\\' && gitPath.size() >= 2 && !IsValidWindowsPathCharacter(gitPath[1]))
	// {
	// 	tgit::DebugOutput::CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": Found path starting with backslash for worktree \"%s\" in .git-file: %s\n", topDir, gitPath.c_str());
	//
	// 	PathUtils::CPathUtils::TrimTrailingPathDelimiter(gitPath);
	// 	if (gitPath.empty()) // gitPath just contained (back)slashes
	// 		return {};
	//
	// 	// check whether the topDir (worktree) is listed in safe.directories
	// 	// NOTE: This is a simplified version without any owner check solely to prevent NTLM leaks; further checks are delegated to libgit and libgit2
	// 	CAutoConfig config;
	// 	CString globalConfig = g_Git.GetGitGlobalConfig();
	// 	CString globalXDGConfig = g_Git.GetGitGlobalXDGConfig();
	// 	auto systemConfig = Registry::CRegStdString(REG_SYSTEM_GITCONFIGPATH.data(), L"", FALSE);
	//
	// 	CAutoConfig temp{ true };
	// 	auto c_str = GetGitPathStringA(globalConfig);
	//
	// 	git_config_add_file_ondisk(temp, c_str.data(), GIT_CONFIG_LEVEL_GLOBAL, nullptr, FALSE);
	//
	// 	git_config_add_file_ondisk(temp, GetGitPathStringA(globalXDGConfig).c_str(), GIT_CONFIG_LEVEL_XDG, nullptr, FALSE);
	// 	if (systemConfig.exists()) {
	// 		const auto path = GetGitPathStringA(systemConfig.defaultValue()).c_str();
	// 		git_config_add_file_ondisk(temp, path, GIT_CONFIG_LEVEL_SYSTEM, nullptr, FALSE);
	// 	}
	// 	git_config_snapshot(config.GetPointer(), temp);
	//
	// 	struct validate_ownership_data
	// 	{
	// 		const std::wstring_view topDir;
	// 		boolean is_safe = false;
	// 	} ownership_data = { .topDir = tgit::wstr::StringView{topDir}, .is_safe = false };
	//
	// 	auto callback = [](const git_config_entry *entry, void *payload) -> int {
	// 		const auto data = static_cast<struct validate_ownership_data *>(payload);
	// 		if (!entry->value || !entry->value[0]) // reset
	// 		{
	// 			data->is_safe = false;
	// 			return 0;
	// 		}
	// 		const std::string_view value{ entry->value };
	// 		if (value == "*") {
	// 			data->is_safe = true;
	// 			return 0;
	// 		}
	//
	// 		auto valueW = CUnicodeUtils::StdGetUnicode(value);
	// 		if (valueW.starts_with(L"~/"))
	// 			valueW = std::wstring{ g_Git.GetHomeDirectory() } + tgit::wstr::Mid(valueW, L"~"sv.length());
	// 		tgit::wstr::Replace(valueW, L'/', L'\\');
	// 		if (valueW.ends_with(L"\\*")) {
	// 			valueW.resize(valueW.length() - 1);
	// 			data->is_safe = data->topDir.starts_with(valueW);
	// 		} else
	// 			data->is_safe = (valueW == data->topDir);
	// 		return 0;
	// 	};
	// 	if (git_config_get_multivar_foreach(config, "safe.directory", nullptr, callback, &ownership_data) < 0 || !ownership_data.is_safe)
	// 		return {};
	//
	// 	return gitPath.c_str();
	// }
	// if (gitPath[0] == L'\\' && gitPath.size() >= 2 && !IsValidWindowsPathCharacter(gitPath[1]))
	// {
	// 	tgit::DebugOutput::CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) L": Found path starting with backslash for worktree \"%s\" in .git-file: %s\n", topDir, gitPath.c_str());
	//
	// 	PathUtils::CPathUtils::TrimTrailingPathDelimiter(gitPath);
	// 	if (gitPath.empty()) // gitPath just contained (back)slashes
	// 		return {};
	//
	// 	// check whether the topDir (worktree) is listed in safe.directories
	// 	// NOTE: This is a simplified version without any owner check solely to prevent NTLM leaks; further checks are delegated to libgit and libgit2
	// 	CAutoConfig config;
	// 	CString globalConfig = g_Git.GetGitGlobalConfig();
	// 	CString globalXDGConfig = g_Git.GetGitGlobalXDGConfig();
	// 	auto systemConfig = Registry::CRegStdString(REG_SYSTEM_GITCONFIGPATH.data(), L"", FALSE);
	//
	// 	CAutoConfig temp{ true };
	// 	auto c_str = GetGitPathStringA(globalConfig);
	//
	// 	git_config_add_file_ondisk(temp, c_str.data(), GIT_CONFIG_LEVEL_GLOBAL, nullptr, FALSE);
	//
	// 	git_config_add_file_ondisk(temp, GetGitPathStringA(globalXDGConfig).c_str(), GIT_CONFIG_LEVEL_XDG, nullptr, FALSE);
	// 	if (systemConfig.exists()) {
	// 		const auto path = GetGitPathStringA(systemConfig.defaultValue()).c_str();
	// 		git_config_add_file_ondisk(temp, path, GIT_CONFIG_LEVEL_SYSTEM, nullptr, FALSE);
	// 	}
	// 	git_config_snapshot(config.GetPointer(), temp);
	//
	// 	struct validate_ownership_data
	// 	{
	// 		const std::wstring_view topDir;
	// 		boolean is_safe = false;
	// 	} ownership_data = { .topDir = tgit::wstr::StringView{topDir}, .is_safe = false };
	//
	// 	auto callback = [](const git_config_entry *entry, void *payload) -> int {
	// 		const auto data = static_cast<struct validate_ownership_data *>(payload);
	// 		if (!entry->value || !entry->value[0]) // reset
	// 		{
	// 			data->is_safe = false;
	// 			return 0;
	// 		}
	// 		const std::string_view value{ entry->value };
	// 		if (value == "*") {
	// 			data->is_safe = true;
	// 			return 0;
	// 		}
	//
	// 		auto valueW = CUnicodeUtils::StdGetUnicode(value);
	// 		if (valueW.starts_with(L"~/"))
	// 			valueW = std::wstring{ g_Git.GetHomeDirectory() } + tgit::wstr::Mid(valueW, L"~"sv.length());
	// 		tgit::wstr::Replace(valueW, L'/', L'\\');
	// 		if (valueW.ends_with(L"\\*")) {
	// 			valueW.resize(valueW.length() - 1);
	// 			data->is_safe = data->topDir.starts_with(valueW);
	// 		} else
	// 			data->is_safe = (valueW == data->topDir);
	// 		return 0;
	// 	};
	// 	if (git_config_get_multivar_foreach(config, "safe.directory", nullptr, callback, &ownership_data) < 0 || !ownership_data.is_safe)
	// 		return {};
	//
	// 	return gitPath.c_str();
	// }
	else if (gitPath[0] == L'\\') // rooted path, but not UNC or otherwise special path such as `\\UNC`` or `\\??` etc.
	{
		if (topDir.GetLength() < 2 || topDir[1] != L':') // rooted paths are only supported on drives
			return {};
		PathUtils::CPathUtils::TrimTrailingPathDelimiter(gitPath);
		return topDir.Mid(0, 2) + gitPath.c_str();
	}

	gitPath = PathUtils::CPathUtils::BuildPathWithPathDelimiter(tgit::wstr::StringView(topDir)) + gitPath;
	CString adminDir;
	PathCanonicalize(CStrBuf(adminDir, MAX_PATH), gitPath.c_str());
	return adminDir;
}
}

export using namespace GitAdminDir;
