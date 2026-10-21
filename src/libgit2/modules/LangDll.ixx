module;
#include <windows.h>
#include <SimpleIni.h>
#include <version.h>

export module LangDll;
import std;
import wil;
import gsl;

import DirFileEnum;	// GetInstalledLanguages; it was TORTOISEGITPROC-only, a per-consumer macro a module never sees
import RIAA;
import Registry;
import PathUtils;
import str_utils;
using namespace std::literals;
namespace fs = std::filesystem;
using tgit::wstr::CString;


export namespace LangDll {
constexpr auto s_defaultLang = DWORD{ 1033 };
constexpr auto s_languagesfolder = L"Languages\\"sv;


std::wstring GetCompatibleDLLVersion(const std::wstring& appPath) {
	CSimpleIni hotfixIniFile;
	if (hotfixIniFile.LoadFile((appPath + L"\\hotfix.ini").data()) == SI_OK) {
		if (const auto* langpackversion = hotfixIniFile.GetValue(L"tortoisegit", L"versionstringlanguagepacks"); langpackversion) {
			using namespace std::ranges;
			auto in_narrow = til::WStringToUTF8(langpackversion);
			auto versionComponents = tgit::zstr::version_tok(in_narrow);
			if (
				versionComponents.size() == 4 &&
				versionComponents.at(0).value_or(0) == TGIT_VERMAJOR &&
				versionComponents.at(1).value_or(0) == TGIT_VERMINOR &&
				versionComponents.at(2).value_or(0) <= TGIT_VERMICRO
			)
				return langpackversion;
		}
	}
	return TEXT(STRPRODUCTVER);
}


using lang_buf_pair = std::pair<CString, DWORD>;


std::vector<lang_buf_pair> GetInstalledLanguages(bool includeNative = false, bool checkVersion = true) {
	std::vector<lang_buf_pair> langs;

	auto buf = std::array<wchar_t, MAX_PATH>{};
	if (includeNative) {
		GetLocaleInfo(s_defaultLang, LOCALE_SNATIVELANGNAME, buf.data(), win32_cast(buf.size()));
		langs.emplace_back(lang_buf_pair{ buf.data(), s_defaultLang });
	}
	const std::wstring sVer{ GetCompatibleDLLVersion(PathUtils::GetAppDirectory().c_str()) };
	CString path = PathUtils::GetAppParentDirectory().c_str();
	path += s_languagesfolder.data();
	CSimpleFileFind finder(path, L"*.dll");
	while (finder.FindNextFileNoDirectories()) {
		CString filename = finder.GetFileName();
		if (!StringUtils::StartsWithI(filename, L"TortoiseProc"))
			continue;

		CString file = finder.GetFilePath();
		if (checkVersion && PathUtils::GetVersionFromFile(file) != sVer)
			continue;
		CString sLoc = filename.Mid(static_cast<int>(wcslen(L"TortoiseProc")));
		sLoc = sLoc.Left(sLoc.GetLength() - static_cast<int>(wcslen(L".dll"))); // cut off ".dll"
		if (StringUtils::StartsWith(sLoc, L"32") && (sLoc.GetLength() > 5))
			continue;
		const DWORD loc = _wtoi(filename.Mid(static_cast<int>(wcslen(L"TortoiseProc"))));
		if (!loc)
			continue;
		const auto len1 = GetLocaleInfo(loc, LOCALE_SNATIVELANGNAME, buf.data(), win32_cast(buf.size()));
		auto sLang = (len1 == SI_OK) ? std::wstring{ buf.data(), gsl::narrow<size_t>(len1) } : std::wstring{};
		const auto len2 = GetLocaleInfo(loc, LOCALE_SNATIVECTRYNAME, buf.data(), win32_cast(buf.size()));
		if (len2 == SI_OK)
			sLang += std::format(L" ({})", std::wstring_view{ buf.data(), gsl::narrow<size_t>(len2) });

		langs.emplace_back(lang_buf_pair{ sLang.data(), loc });
	}
	return langs;
}



/**
 * \ingroup Utils
 * Helper class to load language dependent resource dlls.
 */
class CLangDll {
	CAutoLibrary m_hInstance;
	DWORD m_langId = s_defaultLang;

public:
	CLangDll() = default;
	~CLangDll() = default;

	CLangDll(const CLangDll&) = delete;
	CLangDll(const CLangDll&&) = delete;
	CLangDll& operator=(const CLangDll&) = delete;
	CLangDll&& operator=(const CLangDll&&) = delete;


	[[nodiscard]] DWORD GetLoadedLangId() const { return m_langId; }

	HINSTANCE Init(LPCWSTR appname) {
		CRegStdDWORD loc = CRegStdDWORD(L"Software\\TortoiseGit\\LanguageID", m_langId);
		return Init(appname, nullptr, loc);
	}

	HINSTANCE Init(LPCWSTR appname, HMODULE hModule, DWORD langID);
};



HINSTANCE CLangDll::Init(LPCWSTR appname, HMODULE hModule, DWORD langID) {
	m_hInstance.CloseHandle(); // close any existing handle

	if (langID == s_defaultLang)
		return nullptr;

	auto langpath_buf = std::array<wchar_t, MAX_PATH>{};
	auto&& ret = GetModuleFileName(hModule, langpath_buf.data(), win32_cast(langpath_buf.size()));
	if (ret != SI_OK)
		return {};

	auto langpath = fs::path{ { langpath_buf.data(), gsl::narrow<size_t>(ret) } };
	const auto sVer = GetCompatibleDLLVersion(langpath);

	if (langpath.has_parent_path())
		return nullptr;

	langpath = langpath.parent_path();

	do {
		const auto langdllpath = langpath / std::format(L"{}{}{}.dll", s_languagesfolder, appname, langID);
		if (PathUtils::GetVersionFromFile(langdllpath.c_str()) == sVer) {
			if (hModule)
				m_hInstance = ::LoadLibraryEx(langdllpath.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
			else
				m_hInstance = ::LoadLibrary(langdllpath.c_str());

			if (m_hInstance)
				break;
		}

		DWORD lid = SUBLANGID(langID);
		lid--;
		if (lid > 0)
			langID = MAKELANGID(PRIMARYLANGID(langID), lid);
		else
			langID = 0;
	}
	while (langID != 0);

	m_langId = langID;
	return m_hInstance;
}
}

// as the sibling modules do: TortoiseProc names CLangDll unqualified
export using namespace LangDll;
