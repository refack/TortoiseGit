module;
#include <wil/win32_helpers.h>
#include <wil/resource.h>
#include <wil/filesystem.h>
#include <winrt/base.h>
#include <ShlObj.h>			// SHGetKnownFolderPath, FOLDERID_*, REFKNOWNFOLDERID
#include <Shlwapi.h>		// PathIsURL

#pragma comment(lib, "Shlwapi.lib")

export module wil;
import std;

using std::operator""s;
using std::operator""sv;
namespace fs = std::filesystem;


export namespace wil
{

namespace KnownFolders
{
	inline const auto RoamingAppData = FOLDERID_RoamingAppData;
	inline const auto LocalAppData = FOLDERID_LocalAppData;
	inline const auto Documents = FOLDERID_Documents;
	inline const auto ProgramFiles = FOLDERID_ProgramFiles;
}

namespace KnownEnvVars
{
	constexpr auto XDGBin = L"XDG_BIN_HOME"sv;
	constexpr auto XDGData = L"XDG_DATA_HOME"sv;
	constexpr auto XDGConfig = L"XDG_CONFIG_HOME"sv;
	constexpr auto XDGCache = L"XDG_CACHE_HOME"sv;
	constexpr auto KDGState = L"XDG_STATE_HOME"sv;
}
using KnownEnvVar = std::wstring_view;


// STL
using file_ticks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;


// WinRT
using winrt::clock;
using winrt::to_hstring;


// wil
using wil::unique_any;
using wil::unique_hfile;
using wil::ArgvToCommandLine;
using wil::unique_any;
using wil::unique_hfile;
using wil::unique_cotaskmem_string;
using wil::unique_file;


// Win32
using KnownFolderCLSID = KNOWNFOLDERID;
using RefKnownFolderCLSID = REFKNOWNFOLDERID;
using KnownFolderFlag = KNOWN_FOLDER_FLAG;
constexpr auto IsURL = PathIsURL;


auto GetKnownFolderPath(
	RefKnownFolderCLSID folderId,
	const KnownFolderFlag flags = KF_FLAG_DEFAULT,
	const HANDLE token = nullptr
)
{
	wil::unique_cotaskmem_string result{};
	if (::SHGetKnownFolderPath(folderId, flags, token, result.put()) != S_OK)
		throw std::runtime_error{ "SHGetKnownFolderPath failed for " + to_string(to_hstring(folderId)) };
	return std::filesystem::path{ result.get() };
}


auto GetEnvVar(const KnownEnvVar which)
{
	return fs::path{wil::GetEnvironmentVariableW(which.data()).get()};
}


}
