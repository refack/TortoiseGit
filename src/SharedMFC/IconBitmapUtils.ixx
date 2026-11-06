module;
#include "stdafx.h"
#include <tgitlib/sanity.h>



export module IconBitmapUtils;
import std;
import wil;
import wil.MFC;
import RIAA;



export class IconBitmapUtils
{
public:
	IconBitmapUtils() = delete;

	static HBITMAP IconToBitmap(const HINSTANCE& hInst, UINT uIcon);
	static HBITMAP IconToBitmapPARGB32(const HICON& hIcon, int width, int height);
	static HBITMAP IconToBitmapPARGB32(const HINSTANCE& hInst, UINT uIcon);
	static HRESULT Create32BitHBITMAP(const HDC& hdc, const SIZE* psize, __deref_opt_out void** ppvBits, __out HBITMAP* phBmp);
	static HRESULT ConvertBufferToPARGB32(const HPAINTBUFFER& hPaintBuffer, HDC hdc, HICON hIcon, const SIZE& sizIcon);
	static bool HasAlpha(__in const Gdiplus::ARGB* pargb, const SIZE& sizImage, int cxRow);
	static HRESULT ConvertToPARGB32(const HDC& hdc, __inout Gdiplus::ARGB* pargb, HBITMAP hbmp, const SIZE& sizImage, int cxRow);
};


#pragma comment(lib, "UxTheme.lib")


HBITMAP IconBitmapUtils::IconToBitmap(const HINSTANCE& hInst, const UINT uIcon)
{
	const CAutoIcon hIcon = wil::MFC::LoadIconEx(hInst, MAKEINTRESOURCE(uIcon), 12, 12);
	if (!hIcon)
		return nullptr;

	RECT rect{};

	rect.right = ::GetSystemMetrics(SM_CXMENUCHECK);
	rect.bottom = ::GetSystemMetrics(SM_CYMENUCHECK);

	const HWND desktop = ::GetDesktopWindow();
	if (!desktop)
		return nullptr;

	const HDC screenDev = ::GetDC(desktop);
	if (!screenDev)
		return nullptr;
	SCOPE_EXIT { ::ReleaseDC(desktop, screenDev); };

	// Create a compatible DC
	const HDC dstHdc = ::CreateCompatibleDC(screenDev);
	if (!dstHdc)
		return nullptr;
	SCOPE_EXIT { ::DeleteDC(dstHdc); };

	// Create a new bitmap of icon size
	const HBITMAP bmp = ::CreateCompatibleBitmap(screenDev, rect.right, rect.bottom);
	if (!bmp)
		return nullptr;

	// Select it into the compatible DC
	auto* oldDstBmp = static_cast<HBITMAP>(::SelectObject(dstHdc, bmp));
	if (!oldDstBmp)
		return nullptr;

	// Fill the background of the compatible DC with the white color
	// that is taken by menu routines as transparent
	::SetBkColor(dstHdc, RGB(255, 255, 255));
	::ExtTextOut(dstHdc, 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);

	// Draw the icon into the compatible DC
	::DrawIconEx(dstHdc, 0, 0, hIcon, rect.right, rect.bottom, 0, nullptr, DI_NORMAL);

	// Restore settings
	::SelectObject(dstHdc, oldDstBmp);

	return bmp;
}

HBITMAP IconBitmapUtils::IconToBitmapPARGB32(const HINSTANCE& hInst, const UINT uIcon)
{
	const int iconWidth = GetSystemMetrics(SM_CXSMICON);
	const int iconHeight = GetSystemMetrics(SM_CYSMICON);
	const CAutoIcon hIcon = wil::MFC::LoadIconEx(hInst, MAKEINTRESOURCE(uIcon), iconWidth, iconHeight);

	return IconToBitmapPARGB32(hIcon, iconWidth, iconHeight);
}

HBITMAP IconBitmapUtils::IconToBitmapPARGB32(const HICON& hIcon, const int width, const int height)
{
	if (!hIcon)
		return nullptr;

	SIZE sizIcon;
	sizIcon.cx = width;
	sizIcon.cy = height;

	RECT rcIcon;
	SetRect(&rcIcon, 0, 0, sizIcon.cx, sizIcon.cy);

	const HDC hdcDest = CreateCompatibleDC(nullptr);
	if (!hdcDest)
		return nullptr;
	SCOPE_EXIT { DeleteDC(hdcDest); };

	HBITMAP hBmp = nullptr;
	if (FAILED(Create32BitHBITMAP(hdcDest, &sizIcon, nullptr, &hBmp)))
		return nullptr;

	auto* hBmpOld = static_cast<HBITMAP>(SelectObject(hdcDest, hBmp));
	if (!hBmpOld)
	{
		::DeleteObject(hBmp);
		return nullptr;
	}

	constexpr BLENDFUNCTION bfAlpha = { .BlendOp=AC_SRC_OVER, .BlendFlags=0, .SourceConstantAlpha=255, .AlphaFormat=AC_SRC_ALPHA };
	BP_PAINTPARAMS paintParams = {};
	paintParams.cbSize = sizeof(paintParams);
	paintParams.dwFlags = BPPF_ERASE;
	paintParams.pBlendFunction = &bfAlpha;

	HDC hdcBuffer = nullptr;
	const HPAINTBUFFER hPaintBuffer = BeginBufferedPaint(hdcDest, &rcIcon, BPBF_DIB, &paintParams, &hdcBuffer);
	if (hPaintBuffer)
	{
		if (DrawIconEx(hdcBuffer, 0, 0, hIcon, sizIcon.cx, sizIcon.cy, 0, nullptr, DI_NORMAL))
		{
			// If icon did not have an alpha channel we need to convert buffer to PARGB
			std::ignore = ConvertBufferToPARGB32(hPaintBuffer, hdcDest, hIcon, sizIcon);
		}

		// This will write the buffer contents to the destination bitmap
		std::ignore = EndBufferedPaint(hPaintBuffer, TRUE);
	}

	SelectObject(hdcDest, hBmpOld);

	return hBmp;
}

HRESULT IconBitmapUtils::Create32BitHBITMAP(const HDC& hdc, const SIZE* psize, __deref_opt_out void** ppvBits, __out HBITMAP* phBmp)
{
	if (!psize)
		return E_INVALIDARG;

	if (!phBmp)
		return E_POINTER;

	*phBmp = nullptr;

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biCompression = BI_RGB;

	bmi.bmiHeader.biWidth = psize->cx;
	bmi.bmiHeader.biHeight = psize->cy;
	bmi.bmiHeader.biBitCount = 32;

	const HDC hdcUsed = hdc ? hdc : GetDC(nullptr);
	if (hdcUsed)
	{
		*phBmp = CreateDIBSection(hdcUsed, &bmi, DIB_RGB_COLORS, ppvBits, nullptr, 0);
		if (hdc != hdcUsed)
			ReleaseDC(nullptr, hdcUsed);
	}
	return (!*phBmp) ? E_OUTOFMEMORY : S_OK;
}

HRESULT IconBitmapUtils::ConvertBufferToPARGB32(const HPAINTBUFFER& hPaintBuffer, const HDC hdc, const HICON hIcon, const SIZE& sizIcon)
{
	RGBQUAD* prgbQuad = nullptr;
	int cxRow = 0;
	if (const HRESULT hr = GetBufferedPaintBits(hPaintBuffer, &prgbQuad, &cxRow); FAILED(hr))
		return hr;

	auto* pArgb = reinterpret_cast<Gdiplus::ARGB*>(prgbQuad);
	if (HasAlpha(pArgb, sizIcon, cxRow))
		return S_OK;

	ICONINFO info{};
	SCOPE_EXIT
	{
		if (info.hbmColor)
			DeleteObject(info.hbmColor);
		if (info.hbmMask)
			DeleteObject(info.hbmMask);
	};
	if (!GetIconInfo(hIcon, &info))
		return S_OK;
	if (info.hbmMask)
		return ConvertToPARGB32(hdc, pArgb, info.hbmMask, sizIcon, cxRow);

	return S_OK;
}

bool IconBitmapUtils::HasAlpha(__in const Gdiplus::ARGB* pargb, const SIZE& sizImage, const int cxRow)
{
	const ULONG cxDelta = cxRow - sizImage.cx;
	for (ULONG y = sizImage.cy; y; --y)
	{
		for (ULONG x = sizImage.cx; x; --x)
		{
			if (*pargb++ & 0xFF000000)
				return true;
		}

		pargb += cxDelta;
	}

	return false;
}

HRESULT IconBitmapUtils::ConvertToPARGB32(const HDC& hdc, __inout Gdiplus::ARGB* pargb, const HBITMAP hbmp, const SIZE& sizImage, const int cxRow)
{
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biCompression = BI_RGB;

	bmi.bmiHeader.biWidth = sizImage.cx;
	bmi.bmiHeader.biHeight = sizImage.cy;
	bmi.bmiHeader.biBitCount = 32;

	const HANDLE hHeap = GetProcessHeap();
	auto* pvBits = HeapAlloc(hHeap, 0, bmi.bmiHeader.biWidth * 4 * bmi.bmiHeader.biHeight);
	if (!pvBits)
		return E_OUTOFMEMORY;
	SCOPE_EXIT { HeapFree(hHeap, 0, pvBits); };

	if (GetDIBits(hdc, hbmp, 0, bmi.bmiHeader.biHeight, pvBits, &bmi, DIB_RGB_COLORS) != bmi.bmiHeader.biHeight)
		return E_UNEXPECTED;

	const ULONG cxDelta = cxRow - bmi.bmiHeader.biWidth;
	const auto* pargbMask = static_cast<Gdiplus::ARGB *>(pvBits);

	for (ULONG y = bmi.bmiHeader.biHeight; y; --y)
	{
		for (ULONG x = bmi.bmiHeader.biWidth; x; --x)
		{
			if (*pargbMask++)
			{
				// transparent pixel
				*pargb++ = 0;
			}
			else
			{
				// opaque pixel
				*pargb++ |= 0xFF000000;
			}
		}
		pargb += cxDelta;
	}

	return S_OK;
}
