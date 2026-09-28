module;
#include <windows.h>
#include <SimpleIni.h>
#include <version.h>
#ifdef TORTOISEGITPROC
#include "Utils/DirFileEnum.h"
#endif

export module LandDll;
import std;
import RIAA;
import Registry;
import PathUtils;
import StringUtils;

export namespace LangDll
{
/**
 * \ingroup Utils
 * Helper class to load language dependent resource dlls.
 */
class CLangDll
{
public:
	CLangDll() = default;
	~CLangDll() = default;

	CLangDll(const CLangDll&) = delete;
	CLangDll& operator=(const CLangDll&) = delete;

	HINSTANCE Init(LPCWSTR appname);
	HINSTANCE Init(LPCWSTR appname, HMODULE hModule, DWORD langID);
	DWORD GetLoadedLangId() const { return m_langId; }

	static constexpr DWORD s_defaultLang = 1033;
	static constexpr std::wstring_view s_languagesfolder = L"Languages\\";

#if defined(TORTOISEGITPROC)
	static std::vector<std::pair<CString, DWORD>> GetInstalledLanguages(bool includeNative = false, bool checkVersion = true);
#endif

	static std::wstring GetCompatibleDLLVersion(const std::wstring& appPath);

private:
	CAutoLibrary m_hInstance;
	DWORD m_langId = s_defaultLang;
};

HINSTANCE CLangDll::Init(LPCWSTR appname)
{
	CRegStdDWORD loc = CRegStdDWORD(L"Software\\TortoiseGit\\LanguageID", m_langId);
	return Init(appname, nullptr, loc);
}

HINSTANCE CLangDll::Init(LPCWSTR appname, HMODULE hModule, DWORD langID)
{
	m_hInstance.CloseHandle(); // close any existing handle

	if (langID == s_defaultLang)
		return nullptr;

	wchar_t langpath[MAX_PATH]{};
	GetModuleFileName(hModule, langpath, _countof(langpath));
	wchar_t* pSlash = wcsrchr(langpath, L'\\');
	if (!pSlash)
		return nullptr;

	*pSlash = '\0';

	const std::wstring sVer{ GetCompatibleDLLVersion(langpath) };

	pSlash = wcsrchr(langpath, L'\\');
	if (!pSlash)
		return nullptr;

	*++pSlash = '\0';

	do
	{
		const std::wstring langdllpath = std::format(L"{}{}{}{}.dll", langpath, s_languagesfolder, appname, langID);
		if (PathUtils::GetVersionFromFile(langdllpath.data()) == sVer)
		{
			if (hModule)
				m_hInstance = ::LoadLibraryEx(langdllpath.data(), nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
			else
				m_hInstance = ::LoadLibrary(langdllpath.data());

			if (m_hInstance)
				break;
		}

		DWORD lid = SUBLANGID(langID);
		lid--;
		if (lid > 0)
			langID = MAKELANGID(PRIMARYLANGID(langID), lid);
		else
			langID = 0;
	} while (langID != 0);

	m_langId = langID;
	return m_hInstance;
}
#ifdef TORTOISEGITPROC
std::vector<std::pair<CString, DWORD>> CLangDll::GetInstalledLanguages(bool includeNative /* false */, bool checkVersion /* true */)
{
	std::vector<std::pair<CString, DWORD>> langs;

	wchar_t buf[MAX_PATH]{};
	if (includeNative)
	{
		GetLocaleInfo(CLangDll::s_defaultLang, LOCALE_SNATIVELANGNAME, buf, _countof(buf));
		langs.emplace_back(std::make_pair<>(buf, CLangDll::s_defaultLang));
	}
	const std::wstring sVer{ GetCompatibleDLLVersion(PathUtils::GetAppDirectory().c_str()) };
	CString path = PathUtils::GetAppParentDirectory().c_str();
	path += s_languagesfolder.data();
	CSimpleFileFind finder(path, L"*.dll");
	while (finder.FindNextFileNoDirectories())
	{
		CString filename = finder.GetFileName();
		if (!CStringUtils::StartsWithI(filename, L"TortoiseProc"))
			continue;

		CString file = finder.GetFilePath();
		if (checkVersion && PathUtils::GetVersionFromFile(file) != sVer)
			continue;
		CString sLoc = filename.Mid(static_cast<int>(wcslen(L"TortoiseProc")));
		sLoc = sLoc.Left(sLoc.GetLength() - static_cast<int>(wcslen(L".dll"))); // cut off ".dll"
		if (CStringUtils::StartsWith(sLoc, L"32") && (sLoc.GetLength() > 5))
			continue;
		const DWORD loc = _wtoi(filename.Mid(static_cast<int>(wcslen(L"TortoiseProc"))));
		if (!loc)
			continue;
		GetLocaleInfo(loc, LOCALE_SNATIVELANGNAME, buf, _countof(buf));
		CString sLang = buf;
		GetLocaleInfo(loc, LOCALE_SNATIVECTRYNAME, buf, _countof(buf));
		if (buf[0])
		{
			sLang += L" (";
			sLang += buf;
			sLang += L')';
		}
		langs.emplace_back(std::make_pair<>(sLang, loc));
	}
	return langs;
}
#endif

std::wstring CLangDll::GetCompatibleDLLVersion(const std::wstring& appPath)
{
	CSimpleIni hotfixIniFile;
	if (hotfixIniFile.LoadFile((appPath + L"\\hotfix.ini").data()) == SI_OK)
	{
		if (auto langpackversion = hotfixIniFile.GetValue(L"tortoisegit", L"versionstringlanguagepacks"); langpackversion)
		{
			std::vector<int> versionComponents;
			StringUtils::stringtok(versionComponents, std::wstring(langpackversion), false, L".");
			if (versionComponents.size() == 4 && versionComponents.at(0) == TGIT_VERMAJOR && versionComponents.at(1) == TGIT_VERMINOR && versionComponents.at(2) <= TGIT_VERMICRO)
				return langpackversion;
		}
	}
	return TEXT(STRPRODUCTVER);
}

} // namespace LangDll