module;
#include <windows.h>
#include <Shlobj_core.h>

export module SysInfo;
import std;
import PathUtils;
import str_utils;


static bool InitializeIsWin11OrLater()
{
	PWSTR pszPath = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_System, KF_FLAG_CREATE, nullptr, &pszPath) != S_OK)
		return false;
	const auto sysPath = std::filesystem::path{ pszPath } / "shell32.dll";
	CoTaskMemFree(pszPath);
	const auto explorerVersion = PathUtils::GetVersionFromFile(sysPath.wstring().c_str());
	// The version is a wide string, which std::from_chars cannot parse: this never
	// compiled while SysInfo.ixx was missing from the FILE_SET. Tokenized the way
	// DarkModeHelper and Theme read the same kind of version string.
	std::vector<int> versionParts;
	StringUtils::stringtok(versionParts, explorerVersion, false, L".");
	return versionParts.size() > 3 && versionParts[2] >= 22000;
}


export namespace SysInfo
{
	bool IsWin11OrLater()
	{
		static const bool value = InitializeIsWin11OrLater();
		return value;
	}
}


