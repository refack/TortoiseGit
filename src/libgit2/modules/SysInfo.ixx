module;
#include <windows.h>
#include <Shlobj_core.h>	// SHGetKnownFolderPath

export module SysInfo;
import std;
import wil;
import gsl;

import PathUtils;
import str_utils;


constexpr auto Windows11_Cutoff = 22000;

static bool InitializeIsWin11OrLater()
{
	auto pszPath = wil::unique_cotaskmem_string{};
	auto&& ret = SHGetKnownFolderPath(FOLDERID_System, KF_FLAG_CREATE, nullptr, pszPath.put());
	if (ret != S_OK)
		return false;
	const auto sysPath = std::filesystem::path{ {pszPath.get(), size_t_cast(ret)} } / "shell32.dll";

	const auto explorerVersion = PathUtils::GetVersionFromFile(sysPath.c_str());
	const auto versionParts = tgit::zstr::version_tok(til::WStringToUTF8(explorerVersion));
	return versionParts.size() > 3 && versionParts.at(2) >= Windows11_Cutoff;
}


export namespace SysInfo
{
	bool IsWin11OrLater()
	{
		static const bool value = InitializeIsWin11OrLater();
		return value;
	}
}


