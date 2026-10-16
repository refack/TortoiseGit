// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2023, 2025 - TortoiseGit
// Copyright (C) 2020 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#pragma once
/**
 * Functions to obtain Windows 10 accent colors and colors used to paint window frames.
 */

#include <cstdint>
#include <tuple>
#include <Windows.h>
#include <windows.ui.viewmanagement.h>
#include <wrl.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ntdll.lib")

class Win10Colors
{

	static inline Win10Colors instance;

public:
	Win10Colors() {
		if (!modules_loaded) {
			modules_loaded = true;
			winrt = LoadLibraryW(L"api-ms-win-core-winrt-l1-1-0.dll");
			if (winrt)
				pRoActivateInstance = reinterpret_cast<pfnRoActivateInstance>(GetProcAddress(winrt, "RoActivateInstance"));
			winrt_string = LoadLibraryW(L"api-ms-win-core-winrt-string-l1-1-0.dll");
			if (winrt_string)
				pWindowsCreateStringReference = reinterpret_cast<pfnWindowsCreateStringReference>(GetProcAddress(winrt_string, "WindowsCreateStringReference"));
		}
	}

	~Win10Colors() {
		if (winrt)
			FreeLibrary(winrt);
		if (winrt_string)
			FreeLibrary(winrt_string);
	}


	Win10Colors(const Win10Colors& t) = delete;
	Win10Colors& operator=(const Win10Colors& t) = delete;

	/**
	 * RGBA color. Red is in the LSB, Alpha in the MSB.
	 * You can use GetRValue() et al to access individual components.
	 */
	using RGBA = DWORD;

	/// Accent color shades
	struct AccentColor
	{
		/// foreground accent color
		RGBA foreground;
		/// background accent color
		RGBA background;
		/// Base accent color
		RGBA accent;
		/// Darkest shade.
		RGBA darkest;
		/// Darker shade.
		RGBA darker;
		/// Dark shade.
		RGBA dark;
		/// Light shade.
		RGBA light;
		/// Lighter shade.
		RGBA lighter;
		/// Lightest shade.
		RGBA lightest;
	};

	static HRESULT GetAccentColor(AccentColor& color);

	/// Dynamically loaded WindowsCreateStringReference, if available
	static HRESULT WindowsCreateStringReference(PCWSTR sourceString, UINT32 length, HSTRING_HEADER* hstringHeader, HSTRING* string)
	{
		return instance.WindowsCreateStringReferenceImpl(sourceString, length, hstringHeader, string);
	}

	/// Dynamically loaded RoActivateInstance, if available
	static HRESULT RoActivateInstance(HSTRING activatableClassId, IInspectable** newInstance)
	{
		return instance.RoActivateInstanceImpl(activatableClassId, newInstance);
	}

protected:
	/// Wrap WindowsCreateStringReference
	inline HRESULT WindowsCreateStringReferenceImpl(PCWSTR sourceString, UINT32 length, HSTRING_HEADER* hstringHeader, HSTRING* string)
	{
		if (!pWindowsCreateStringReference)
			return E_NOTIMPL;
		return pWindowsCreateStringReference(sourceString, length, hstringHeader, string);
	}

	/// Wrap RoActivateInstance
	inline HRESULT RoActivateInstanceImpl(HSTRING activatableClassId, IInspectable** inst)
	{
		if (!pRoActivateInstance)
			return E_NOTIMPL;
		return pRoActivateInstance(activatableClassId, inst);
	}

	static inline RGBA MakeRGBA(std::uint8_t R, std::uint8_t G, std::uint8_t B, std::uint8_t A)
	{
		return RGB(R, G, B) | (A << 24);
	}

	static inline RGBA ToRGBA(ABI::Windows::UI::Color color)
	{
		return MakeRGBA(color.R, color.G, color.B, color.A);
	}

private:
	bool               modules_loaded = false;
	HMODULE            winrt          = 0;
	HMODULE            winrt_string   = 0;

	using pfnWindowsCreateStringReference =  HRESULT(STDAPICALLTYPE*)(PCWSTR sourceString, UINT32 length, HSTRING_HEADER* hstringHeader, HSTRING* string);
	pfnWindowsCreateStringReference pWindowsCreateStringReference = nullptr;
	using pfnRoActivateInstance = HRESULT(WINAPI*)(HSTRING activatableClassId, IInspectable** instance);
	pfnRoActivateInstance pRoActivateInstance = nullptr;
};

using namespace Microsoft::WRL;
namespace WindowsUI = ABI::Windows::UI;

/// Call RoActivateInstance and query an interface
template <typename IF>
static HRESULT ActivateInstance(HSTRING classId, ComPtr<IF>& instance) {
	ComPtr<IInspectable> inspectable;
	auto hr = Win10Colors::RoActivateInstance(classId, &inspectable);
	if (FAILED(hr))
		return hr;
	return inspectable.As(&instance);
}

inline HRESULT Win10Colors::GetAccentColor(AccentColor& color) {
	Wrappers::HStringReference classId(L"Windows.UI.ViewManagement.UISettings");
	Microsoft::WRL::ComPtr<WindowsUI::ViewManagement::IUISettings> settings;
	
	if (auto hr = ActivateInstance(classId.Get(), settings) < 0)
		return hr;

	ComPtr<WindowsUI::ViewManagement::IUISettings3> settings3;
	std::ignore = settings.As(&settings3);
	if (!settings3)
		return E_FAIL;

	WindowsUI::Color ui_color;
	auto hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_Foreground, &ui_color);
	if (FAILED(hr))
		return hr;
	color.foreground = ToRGBA(ui_color);
	hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_Background, &ui_color);
	if (FAILED(hr))
		return hr;
	color.background = ToRGBA(ui_color);
	hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_AccentDark3, &ui_color);
	if (FAILED(hr))
		return hr;
	color.darkest = ToRGBA(ui_color);
	hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_AccentDark2, &ui_color);
	if (FAILED(hr))
		return hr;
	color.darker = ToRGBA(ui_color);
	hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_AccentDark1, &ui_color);
	if (FAILED(hr))
		return hr;
	color.dark = ToRGBA(ui_color);
	hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_Accent, &ui_color);
	if (FAILED(hr))
		return hr;
	color.accent = ToRGBA(ui_color);
	hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_AccentLight1, &ui_color);
	if (FAILED(hr))
		return hr;
	color.light = ToRGBA(ui_color);
	hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_AccentLight2, &ui_color);
	if (FAILED(hr))
		return hr;
	color.lighter = ToRGBA(ui_color);
	hr = settings3->GetColorValue(WindowsUI::ViewManagement::UIColorType_AccentLight3, &ui_color);
	if (FAILED(hr))
		return hr;
	color.lightest = ToRGBA(ui_color);

	return S_OK;
}
