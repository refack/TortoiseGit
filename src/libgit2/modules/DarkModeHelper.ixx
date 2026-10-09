module;
#include <windows.h>
#include <Uxtheme.h>
#include <minwindef.h>
#include "detours/detours.h"
#include "sanity.h"


export module DarkModeHelper;
import std;
import git_h;
import PathUtils;
import StringUtils;
import tgittypes;


// Local linkage
LONG DetourTransaction(const std::function<LONG()>& callback)
{
	LONG res = DetourTransactionBegin();

	if (res != NO_ERROR)
		return res;

	SCOPE_EXIT
	{
		if (res != ERROR_SUCCESS)
			DetourTransactionAbort();
	};

	res = DetourUpdateThread(GetCurrentThread());

	if (res != NO_ERROR)
		return res;

	res = callback();

	if (res != NO_ERROR)
		return res;

	res = DetourTransactionCommit();

	if (res != NO_ERROR)
		return res;

	return res;
}


export namespace TGitDarkMode
{


/// helper class for the Windows 10 dark mode
/// note: we use undocumented APIs here, so be careful!
class DarkModeHelper
{
public:
	static DarkModeHelper& Instance();

	enum class IMMERSIVE_HC_CACHE_MODE
	{
		IHCM_USE_CACHED_VALUE,
		IHCM_REFRESH
	};
	enum class PreferredAppMode
	{
		Default,
		AllowDark,
		ForceDark,
		ForceLight,
		Max
	};
	enum class WINDOWCOMPOSITIONATTRIB
	{
		WCA_UNDEFINED = 0,
		WCA_NCRENDERING_ENABLED = 1,
		WCA_NCRENDERING_POLICY = 2,
		WCA_TRANSITIONS_FORCEDISABLED = 3,
		WCA_ALLOW_NCPAINT = 4,
		WCA_CAPTION_BUTTON_BOUNDS = 5,
		WCA_NONCLIENT_RTL_LAYOUT = 6,
		WCA_FORCE_ICONIC_REPRESENTATION = 7,
		WCA_EXTENDED_FRAME_BOUNDS = 8,
		WCA_HAS_ICONIC_BITMAP = 9,
		WCA_THEME_ATTRIBUTES = 10,
		WCA_NCRENDERING_EXILED = 11,
		WCA_NCADORNMENTINFO = 12,
		WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
		WCA_VIDEO_OVERLAY_ACTIVE = 14,
		WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
		WCA_DISALLOW_PEEK = 16,
		WCA_CLOAK = 17,
		WCA_CLOAKED = 18,
		WCA_ACCENT_POLICY = 19,
		WCA_FREEZE_REPRESENTATION = 20,
		WCA_EVER_UNCLOAKED = 21,
		WCA_VISUAL_OWNER = 22,
		WCA_HOLOGRAPHIC = 23,
		WCA_EXCLUDED_FROM_DDA = 24,
		WCA_PASSIVEUPDATEMODE = 25,
		WCA_USEDARKMODECOLORS = 26,
		WCA_LAST = 27
	};

	struct WINDOWCOMPOSITIONATTRIBDATA
	{
		WINDOWCOMPOSITIONATTRIB Attrib;
		PVOID pvData;
		SIZE_T cbData;
	};

	bool CanHaveDarkMode() const;
	void AllowDarkModeForApp(BOOL allow) const;
	void AllowDarkModeForWindow(HWND hwnd, BOOL allow) const;
	BOOL ShouldAppsUseDarkMode() const;
	BOOL IsDarkModeAllowedForWindow(HWND hwnd) const;
	BOOL IsDarkModeAllowedForApp() const;
	BOOL ShouldSystemUseDarkMode() const;
	void RefreshImmersiveColorPolicyState() const;
	BOOL GetIsImmersiveColorUsingHighContrast(IMMERSIVE_HC_CACHE_MODE mode) const;
	void FlushMenuThemes() const;
	BOOL SetWindowCompositionAttribute(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA* data) const;
	void RefreshTitleBarThemeColor(HWND hWnd, BOOL dark) const;

private:
	DarkModeHelper();
	~DarkModeHelper();

	DarkModeHelper(const DarkModeHelper& t) = delete;
	DarkModeHelper& operator=(const DarkModeHelper& t) = delete;

	using AllowDarkModeForAppFPN = void(WINAPI*)(BOOL allow);
	using SetPreferredAppModeFPN = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
	using AllowDarkModeForWindowFPN   = void(WINAPI*)(HWND hwnd, BOOL allow);
	using ShouldAppsUseDarkModeFPN = BOOL(WINAPI*)();
	using IsDarkModeAllowedForWindowFPN = BOOL(WINAPI*)(HWND hwnd);
	using IsDarkModeAllowedForAppFPN = BOOL(WINAPI*)();
	using ShouldSystemUseDarkModeFPN = BOOL(WINAPI*)();
	using RefreshImmersiveColorPolicyStateFN = void(WINAPI*)();
	using GetIsImmersiveColorUsingHighContrastFN = BOOL(WINAPI*)(IMMERSIVE_HC_CACHE_MODE mode);
	using FlushMenuThemesFN = void(WINAPI*)();
	using OpenNcThemeDataFPN = HTHEME(WINAPI*)(HWND hWnd, LPCWSTR pszClassList);
	using SetWindowCompositionAttributeFPN = BOOL(WINAPI*)(HWND hwnd, WINDOWCOMPOSITIONATTRIBDATA* data);
	using OpenNcThemeDataType = HTHEME(WINAPI*)(HWND hwnd, LPCWSTR classList);

	static LONG DetourOpenNcThemeData();
	static LONG RestoreOpenNcThemeData();
	static HTHEME WINAPI DetouredOpenNcThemeData(HWND hwnd, LPCWSTR classList);

	AllowDarkModeForAppFPN m_pAllowDarkModeForApp = nullptr;
	SetPreferredAppModeFPN m_pSetPreferredAppMode = nullptr;
	AllowDarkModeForWindowFPN m_pAllowDarkModeForWindow = nullptr;
	ShouldAppsUseDarkModeFPN m_pShouldAppsUseDarkMode = nullptr;
	IsDarkModeAllowedForWindowFPN m_pIsDarkModeAllowedForWindow = nullptr;
	IsDarkModeAllowedForAppFPN m_pIsDarkModeAllowedForApp = nullptr;
	ShouldSystemUseDarkModeFPN m_pShouldSystemUseDarkMode = nullptr;
	RefreshImmersiveColorPolicyStateFN m_pRefreshImmersiveColorPolicyState = nullptr;
	GetIsImmersiveColorUsingHighContrastFN m_pGetIsImmersiveColorUsingHighContrast = nullptr;
	FlushMenuThemesFN m_pFlushMenuThemes = nullptr;
	SetWindowCompositionAttributeFPN m_pSetWindowCompositionAttribute = nullptr;
	HMODULE m_hUxthemeLib = nullptr;
	bool m_bCanHaveDarkMode = false;
	static OpenNcThemeDataType m_openNcThemeData;
	/// Detours refuses to detach a hook it never attached, so the pair has to be balanced
	/// rather than called speculatively. AllowDarkModeForApp(FALSE) runs at startup in light
	/// mode, before anything is hooked.
	static bool m_bOpenNcThemeDataDetoured;
};


DarkModeHelper::OpenNcThemeDataType DarkModeHelper::m_openNcThemeData = nullptr;
bool DarkModeHelper::m_bOpenNcThemeDataDetoured = false;

DarkModeHelper& DarkModeHelper::Instance()
{
	static DarkModeHelper helper;
	return helper;
}

bool DarkModeHelper::CanHaveDarkMode() const
{
	return m_bCanHaveDarkMode;
}

void DarkModeHelper::AllowDarkModeForApp(BOOL allow) const
{
	if (m_pAllowDarkModeForApp)
		m_pAllowDarkModeForApp(allow ? 1 : 0);
	if (m_pSetPreferredAppMode)
		m_pSetPreferredAppMode(allow ? PreferredAppMode::ForceDark : PreferredAppMode::Default);
	if (allow)
		DetourOpenNcThemeData();
	else
		RestoreOpenNcThemeData();
}

void DarkModeHelper::AllowDarkModeForWindow(HWND hwnd, BOOL allow) const
{
	if (m_pAllowDarkModeForWindow)
		m_pAllowDarkModeForWindow(hwnd, allow);
}

BOOL DarkModeHelper::ShouldAppsUseDarkMode() const
{
	if (m_pShouldAppsUseDarkMode && m_pAllowDarkModeForApp)
		return m_pShouldAppsUseDarkMode() & 0x01;
	return ShouldSystemUseDarkMode();
}

BOOL DarkModeHelper::IsDarkModeAllowedForWindow(HWND hwnd) const
{
	if (m_pIsDarkModeAllowedForWindow)
		return m_pIsDarkModeAllowedForWindow(hwnd);
	return FALSE;
}

BOOL DarkModeHelper::IsDarkModeAllowedForApp() const
{
	if (m_pIsDarkModeAllowedForApp)
		return m_pIsDarkModeAllowedForApp();
	return FALSE;
}

BOOL DarkModeHelper::ShouldSystemUseDarkMode() const
{
	if (m_pShouldSystemUseDarkMode)
		return m_pShouldSystemUseDarkMode();
	return FALSE;
}

void DarkModeHelper::RefreshImmersiveColorPolicyState() const
{
	if (m_pRefreshImmersiveColorPolicyState)
		m_pRefreshImmersiveColorPolicyState();
}

BOOL DarkModeHelper::GetIsImmersiveColorUsingHighContrast(IMMERSIVE_HC_CACHE_MODE mode) const
{
	if (m_pGetIsImmersiveColorUsingHighContrast)
		return m_pGetIsImmersiveColorUsingHighContrast(mode);
	return FALSE;
}

void DarkModeHelper::FlushMenuThemes() const
{
	if (m_pFlushMenuThemes)
		m_pFlushMenuThemes();
}

BOOL DarkModeHelper::SetWindowCompositionAttribute(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA* data) const
{
	if (m_pSetWindowCompositionAttribute)
		return m_pSetWindowCompositionAttribute(hWnd, data);
	return FALSE;
}

void DarkModeHelper::RefreshTitleBarThemeColor(HWND hWnd, BOOL dark) const
{
	WINDOWCOMPOSITIONATTRIBDATA data = { WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
	SetWindowCompositionAttribute(hWnd, &data);
}

DarkModeHelper::DarkModeHelper()
{
	INITCOMMONCONTROLSEX used = {
		sizeof(INITCOMMONCONTROLSEX),
		ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES
	};
	InitCommonControlsEx(&used);

	m_bCanHaveDarkMode = false;
	long micro = 0;
	{
		const auto version = PathUtils::GetVersionFromFile(L"uxtheme.dll");
		std::vector<long> tokens;
		StringUtils::stringtok(tokens, version, false, L".");
		if (tokens.size() == 4)
		{
			auto major = tokens[0];
			auto minor = tokens[1];
			micro = tokens[2];

			// the windows 10 update 1809 has the version
			// number as 10.0.17763.1
			if (major > 10)
				m_bCanHaveDarkMode = true;
			else if (major == 10)
			{
				if (minor > 0)
					m_bCanHaveDarkMode = true;
				else if (micro > 17762)
					m_bCanHaveDarkMode = true;
			}
		}
	}

	m_hUxthemeLib = LoadLibrary(L"uxtheme.dll");
	if (m_hUxthemeLib && m_bCanHaveDarkMode)
	{
		// Note: these functions are undocumented! Which meas I shouldn't even use them.
		// So, since MS decided to keep this new feature to themselves, I have to use
		// undocumented functions to adjust.
		// Let's just hope they change their minds and document these functions one day...

		// first try with the names, just in case MS decides to properly export these functions
		if (micro < 18362)
			m_pAllowDarkModeForApp = reinterpret_cast<AllowDarkModeForAppFPN>(GetProcAddress(m_hUxthemeLib, "AllowDarkModeForApp"));
		else
			m_pSetPreferredAppMode = reinterpret_cast<SetPreferredAppModeFPN>(GetProcAddress(m_hUxthemeLib, "SetPreferredAppMode"));
		m_pAllowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFPN>(GetProcAddress(m_hUxthemeLib, "AllowDarkModeForWindow"));
		m_pShouldAppsUseDarkMode = reinterpret_cast<ShouldAppsUseDarkModeFPN>(GetProcAddress(m_hUxthemeLib, "ShouldAppsUseDarkMode"));
		m_pIsDarkModeAllowedForWindow = reinterpret_cast<IsDarkModeAllowedForWindowFPN>(GetProcAddress(m_hUxthemeLib, "IsDarkModeAllowedForWindow"));
		m_pIsDarkModeAllowedForApp = reinterpret_cast<IsDarkModeAllowedForAppFPN>(GetProcAddress(m_hUxthemeLib, "IsDarkModeAllowedForApp"));
		m_pShouldSystemUseDarkMode = reinterpret_cast<ShouldSystemUseDarkModeFPN>(GetProcAddress(m_hUxthemeLib, "ShouldSystemUseDarkMode"));
		m_pRefreshImmersiveColorPolicyState = reinterpret_cast<RefreshImmersiveColorPolicyStateFN>(GetProcAddress(m_hUxthemeLib, "RefreshImmersiveColorPolicyState"));
		m_pGetIsImmersiveColorUsingHighContrast = reinterpret_cast<GetIsImmersiveColorUsingHighContrastFN>(GetProcAddress(m_hUxthemeLib, "GetIsImmersiveColorUsingHighContrast"));
		m_pFlushMenuThemes = reinterpret_cast<FlushMenuThemesFN>(GetProcAddress(m_hUxthemeLib, "FlushMenuThemes"));
		m_pSetWindowCompositionAttribute = reinterpret_cast<SetWindowCompositionAttributeFPN>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
		if (!m_pAllowDarkModeForApp && micro < 18362)
			m_pAllowDarkModeForApp = reinterpret_cast<AllowDarkModeForAppFPN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(135)));
		if (!m_pSetPreferredAppMode && micro >= 18362)
			m_pSetPreferredAppMode = reinterpret_cast<SetPreferredAppModeFPN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(135)));
		if (!m_pAllowDarkModeForWindow)
			m_pAllowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFPN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(133)));
		if (!m_pShouldAppsUseDarkMode)
			m_pShouldAppsUseDarkMode = reinterpret_cast<ShouldAppsUseDarkModeFPN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(132)));
		if (!m_pIsDarkModeAllowedForWindow)
			m_pIsDarkModeAllowedForWindow = reinterpret_cast<IsDarkModeAllowedForWindowFPN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(137)));
		if (!m_pIsDarkModeAllowedForApp)
			m_pIsDarkModeAllowedForApp = reinterpret_cast<IsDarkModeAllowedForAppFPN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(139)));
		if (!m_pShouldSystemUseDarkMode)
			m_pShouldSystemUseDarkMode = reinterpret_cast<ShouldSystemUseDarkModeFPN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(138)));
		if (!m_pRefreshImmersiveColorPolicyState)
			m_pRefreshImmersiveColorPolicyState = reinterpret_cast<RefreshImmersiveColorPolicyStateFN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(104)));
		if (!m_pGetIsImmersiveColorUsingHighContrast)
			m_pGetIsImmersiveColorUsingHighContrast = reinterpret_cast<GetIsImmersiveColorUsingHighContrastFN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(106)));
		if (!m_pFlushMenuThemes)
			m_pFlushMenuThemes = reinterpret_cast<FlushMenuThemesFN>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(136)));
		if (!m_openNcThemeData)
			m_openNcThemeData = reinterpret_cast<OpenNcThemeDataType>(GetProcAddress(m_hUxthemeLib, MAKEINTRESOURCEA(49)));
	}
}

DarkModeHelper::~DarkModeHelper()
{
	FreeLibrary(m_hUxthemeLib);
}

LONG DarkModeHelper::DetourOpenNcThemeData()
{
	if (m_bOpenNcThemeDataDetoured || !m_openNcThemeData)
		return NO_ERROR;

	const LONG res = DetourTransaction([] { return DetourAttach(&reinterpret_cast<PVOID&>(m_openNcThemeData), DetouredOpenNcThemeData); });
	if (res == NO_ERROR)
		m_bOpenNcThemeDataDetoured = true;
	return res;
}

LONG DarkModeHelper::RestoreOpenNcThemeData()
{
	if (!m_bOpenNcThemeDataDetoured)
		return NO_ERROR;

	const LONG res = DetourTransaction([] { return DetourDetach(&reinterpret_cast<PVOID&>(m_openNcThemeData), DetouredOpenNcThemeData); });
	if (res == NO_ERROR)
		m_bOpenNcThemeDataDetoured = false;
	return res;
}

HTHEME WINAPI DarkModeHelper::DetouredOpenNcThemeData(HWND hwnd, LPCWSTR classList)
{
	// The "ItemsView" theme used to style listview controls in dark mode doesn't change the
	// scrollbar colors. By changing the class here, the scrollbars in a listview control will
	// appear dark in dark mode.
	if (lstrcmp(classList, L"ScrollBar") == 0)
	{
		hwnd = nullptr;
		classList = L"Explorer::ScrollBar";
	}

	return m_openNcThemeData(hwnd, classList);
}

}

// The namespace is not called DarkModeHelper: with this using-directive, a
// namespace and a class of the same name made every DarkModeHelper::Instance()
// ambiguous at global scope (C2872) in every importer.
export using namespace TGitDarkMode;