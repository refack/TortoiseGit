module;
#include <windef.h>
#include <Uxtheme.h>
#include <minwindef.h>
#include "Git.h"


export module DarkModeHelper;
import PathUtils;
import StringUtils;


export namespace DarkModeHelper {
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

}

export using namespace DarkModeHelper;