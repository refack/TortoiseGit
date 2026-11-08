module;
#include <windows.h>
#include <Shlobj_core.h>	// SHGetKnownFolderPath

export module SysInfo;
import std;
import wil;
import gsl;

import GitHash;		// TGitHash::GetHashText, for GetMonitorSetupHash
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
	const auto versionParts = tgit::str::version_tok(til::WStringToUTF8(explorerVersion));
	return versionParts.size() > 3 && versionParts.at(2) >= Windows11_Cutoff;
}


static BOOL CALLBACK MonitorEnum(HMONITOR hMon, HDC /*hdc*/, LPRECT lprcMonitor, LPARAM pData)
{
	MONITORINFOEX miex = {};
	miex.cbSize = sizeof(MONITORINFOEX);
	GetMonitorInfo(hMon, &miex);

	if (miex.dwFlags == DISPLAY_DEVICE_MIRRORING_DRIVER)
		return TRUE;

	auto* pMonRects = reinterpret_cast<std::vector<RECT>*>(pData);
	pMonRects->push_back(*lprcMonitor);
	return TRUE;
}


export namespace SysInfo
{
	bool IsWin11OrLater()
	{
		static const bool value = InitializeIsWin11OrLater();
		return value;
	}

	/// A stable fingerprint of the monitor layout, so per-setup window
	/// placements (docked vs. on-the-road) land in distinct registry keys.
	std::wstring GetMonitorSetupHash()
	{
		std::vector<RECT> monRects;
		EnumDisplayMonitors(nullptr, nullptr, MonitorEnum, reinterpret_cast<LPARAM>(&monRects));
		std::ranges::sort(monRects,
						  [](const RECT& a, const RECT& b) -> bool {
							  if (a.left == b.left)
								  return a.top < b.top;
							  return a.left < b.left;
						  });
		return TGitHash::GetHashText(monRects.data(), monRects.size() * sizeof(RECT), TGitHash::HashType::HashMd5);
	}
}


