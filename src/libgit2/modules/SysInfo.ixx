module;
#include <Shlobj_core.h>

export module SysInfo;
import std;
import PathUtils;
import StringUtils;


static bool InitializeIsWin11OrLater()
{
	PWSTR pszPath = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_System, KF_FLAG_CREATE, nullptr, &pszPath) != S_OK)
		return false;
	const auto sysPath = std::filesystem::path{ pszPath } / "shell32.dll";
	CoTaskMemFree(pszPath);
	const auto explorerVersion = CPathUtils::GetVersionFromFile(sysPath.wstring().c_str());
	const auto versionParts = explorerVersion
		| std::views::split('.')
		| std::views::transform([](auto r) {
			int v{};
			std::from_chars(r.data(), r.data() + r.size(), v);
			return v;
		})
		| std::ranges::to<std::vector<int>>();
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


