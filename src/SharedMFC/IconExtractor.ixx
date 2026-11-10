module;
#include <Windows.h>

#include "Resources/resource.h"


// NOLINTBEGIN(*-avoid-c-arrays,*-pro-bounds-avoid-unchecked-container-access,*-pro-type-reinterpret-cast)
export module IconExtractor;
import std;
import tgit;


constexpr auto BytesPerLine(const LPBITMAPINFOHEADER& lpBMIH) {
	const auto bits = lpBMIH->biWidth * lpBMIH->biPlanes * lpBMIH->biBitCount;
	return (((bits + 31U) >> 5U) << 2U);
}


export namespace IconExtractor {
// The following structures are taken from iconpro sdk example
#pragma pack(push)
#pragma pack(2)
struct MEMICONDIRENTRY{
	BYTE bWidth; // Width of the image
	BYTE bHeight; // Height of the image (times 2)
	BYTE bColorCount; // Number of colors in image (0 if >=8bpp)
	BYTE bReserved; // Reserved
	WORD wPlanes; // Color Planes
	WORD wBitCount; // Bits per pixel
	DWORD dwBytesInRes; // how many bytes in this resource?
	WORD nID; // the ID
};



struct MEMICONDIR {
	WORD idReserved; // Reserved
	WORD idType; // resource type (1 for icons)
	WORD idCount; // how many images?
	MEMICONDIRENTRY* idEntries; // the entries for each image
};
#pragma pack(pop)


struct ICONIMAGE{
	UINT Width, Height, Colors; // Width, Height and bpp
	std::unique_ptr<BYTE[]> lpBits; // ptr to DIB bits
	DWORD dwNumBytes; // how many bytes?
	LPBITMAPINFO lpbi; // ptr to header
	LPBYTE lpXOR; // ptr to XOR image bits
	LPBYTE lpAND; // ptr to AND image bits
};



struct ICONRESOURCE {
	bool bHasChanged{}; // Has image changed?
	UINT nNumImages{}; // How many images?
	std::unique_ptr<ICONIMAGE[]> IconImages; // Image entries
};



// These next two structs represent how the icon information is stored
// in an ICO file.
struct ICONDIRENTRY {
	BYTE bWidth; // Width of the image
	BYTE bHeight; // Height of the image (times 2)
	BYTE bColorCount; // Number of colors in image (0 if >=8bpp)
	BYTE bReserved; // Reserved
	WORD wPlanes; // Color Planes
	WORD wBitCount; // Bits per pixel
	DWORD dwBytesInRes; // how many bytes in this resource?
	DWORD dwImageOffset; // where in the file is this image
};



struct ICONDIR {
	WORD idReserved; // Reserved
	WORD idType; // resource type (1 for icons)
	WORD idCount; // how many images?
	ICONDIRENTRY* idEntries; // the entries for each image
};


DWORD WriteICOHeader(const HANDLE& hFile, const UINT nNumEntries) {
	WORD Output = 0;
	DWORD dwBytesWritten = 0;

	// Write 'reserved' WORD
	if (!WriteFile(hFile, &Output, sizeof(WORD), &dwBytesWritten, nullptr))
		return GetLastError();
	// Did we write a WORD?
	if (dwBytesWritten != sizeof(WORD))
		return GetLastError();
	// Write 'type' WORD (1)
	Output = 1;
	if (!WriteFile(hFile, &Output, sizeof(WORD), &dwBytesWritten, nullptr))
		return GetLastError();
	// Did we write a WORD?
	if (dwBytesWritten != sizeof(WORD))
		return GetLastError();
	// Write Number of Entries
	Output = static_cast<WORD>(nNumEntries);
	if (!WriteFile(hFile, &Output, sizeof(WORD), &dwBytesWritten, nullptr))
		return GetLastError();
	// Did we write a WORD?
	if (dwBytesWritten != sizeof(WORD))
		return GetLastError();

	return NO_ERROR;
}


DWORD CalculateImageOffset(const ICONRESOURCE* lpIR, const UINT nIndex) {
	// Calculate the ICO header size
	DWORD dwSize = 3 * sizeof(WORD);
	// Add the ICONDIRENTRY's
	dwSize += lpIR->nNumImages * sizeof(ICONDIRENTRY);
	// Add the sizes of the previous images
	for (UINT i = 0; i < nIndex; ++i)
		dwSize += lpIR->IconImages[i].dwNumBytes;
	return dwSize;
}


DWORD WriteIconToICOFile(const ICONRESOURCE* lpIR, const LPCWSTR& szFileName) {
	const auto hFile = wil::unique_handle{ ::CreateFile(szFileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
	// open the file
	if (!hFile)
		return GetLastError();

	// Write the header
	if (WriteICOHeader(hFile.get(), lpIR->nNumImages))
		return GetLastError();

	// Write the ICONDIRENTRY's
	for (const auto& iconImage : std::span(lpIR->IconImages.get(), lpIR->nNumImages)) {
		ICONDIRENTRY ide = {};

		// Convert internal format to ICONDIRENTRY
		ide.bWidth = static_cast<BYTE>(iconImage.Width);
		ide.bHeight = static_cast<BYTE>(iconImage.Height);
		if (ide.bHeight == 0) // 256x256 icon, both width and height must be 0
			ide.bWidth = 0;
		ide.bReserved = 0;
		ide.wPlanes = iconImage.lpbi->bmiHeader.biPlanes;
		ide.wBitCount = iconImage.lpbi->bmiHeader.biBitCount;
		const auto narrow_cast = gsl::narrow_cast<WORD>(ide.wPlanes * ide.wBitCount);

		ide.bColorCount = (narrow_cast >= 8) ? 0 : (1U << narrow_cast);
		ide.dwBytesInRes = iconImage.dwNumBytes;
		ide.dwImageOffset = CalculateImageOffset(lpIR, win32_cast(&iconImage - lpIR->IconImages.get()));

		// Write the ICONDIRENTRY to disk
		DWORD dwBytesWritten = 0;
		if (!WriteFile(hFile.get(), &ide, sizeof(ICONDIRENTRY), &dwBytesWritten, nullptr))
			return GetLastError();

		if (dwBytesWritten != sizeof(ICONDIRENTRY))
			return GetLastError();
	}

	// Write the image bits for each image
	for (const ICONIMAGE& iconImage : std::span{ lpIR->IconImages.get(), lpIR->nNumImages }) {
		const DWORD dwTemp = iconImage.lpbi->bmiHeader.biSizeImage;
		bool bError = false; // fix size even on error

		// Set the sizeimage member to zero, but not if the icon is PNG
		if (iconImage.lpbi->bmiHeader.biCompression != 65536)
			iconImage.lpbi->bmiHeader.biSizeImage = 0;
		DWORD dwBytesWritten = 0;
		if (!WriteFile(hFile.get(), iconImage.lpBits.get(), iconImage.dwNumBytes, &dwBytesWritten, nullptr))
			bError = true;

		if (dwBytesWritten != iconImage.dwNumBytes)
			bError = true;

		// set it back
		iconImage.lpbi->bmiHeader.biSizeImage = dwTemp;
		if (bError)
			return GetLastError();
	}
	return NO_ERROR;
}


WORD DIBNumColors(const LPSTR& lpbi) {
	const DWORD dwClrUsed = reinterpret_cast<LPBITMAPINFOHEADER>(lpbi)->biClrUsed;

	if (dwClrUsed)
		return static_cast<WORD>(dwClrUsed);

	const WORD wBitCount = reinterpret_cast<LPBITMAPINFOHEADER>(lpbi)->biBitCount;

	switch (wBitCount) {
	case 1:
		return 2;
	case 4:
		return 16;
	case 8:
		return 256;
	default:
		return 0;
	}
	//return 0;
}


WORD PaletteSize(const LPSTR& lpbi) {
	return (DIBNumColors(lpbi) * sizeof(RGBQUAD));
}


LPSTR FindDIBBits(const LPSTR& lpbi) {
	const auto lpbi_offset = *reinterpret_cast<LPDWORD>(lpbi);
	return (lpbi + lpbi_offset + PaletteSize(lpbi));
}



BOOL AdjustIconImagePointers(ICONIMAGE* lpImage) {
	if (!lpImage)
		return FALSE;

	// BITMAPINFO is at beginning of bits
	lpImage->lpbi = reinterpret_cast<LPBITMAPINFO>(lpImage->lpBits.get());
	// Width - simple enough
	lpImage->Width = lpImage->lpbi->bmiHeader.biWidth;
	// Icons are stored in funky format where height is doubled - account for it
	lpImage->Height = (lpImage->lpbi->bmiHeader.biHeight) / 2;
	// How many colors?
	lpImage->Colors = lpImage->lpbi->bmiHeader.biPlanes * lpImage->lpbi->bmiHeader.biBitCount;
	// XOR bits follow the header and color table
	lpImage->lpXOR = reinterpret_cast<PBYTE>(FindDIBBits(reinterpret_cast<LPSTR>(lpImage->lpbi)));
	// AND bits follow the XOR bits
	lpImage->lpAND = lpImage->lpXOR + (lpImage->Height * BytesPerLine(reinterpret_cast<LPBITMAPINFOHEADER>(lpImage->lpbi)));

	return TRUE;
}


DWORD ExtractIcon(const HINSTANCE hResource, const LPCWSTR id, const LPCWSTR TargetICON) {
	// Find the group icon resource
	const HRSRC hRsrc = FindResource(hResource, id, RT_GROUP_ICON);
	til::THROW_LIVE_ERROR_IF(!hRsrc, "FindResource failed");

	const auto hGlobal = wil::unique_hglobal{ ::LoadResource(hResource, hRsrc) };
	til::THROW_LIVE_ERROR_IF(!hGlobal, "LoadResource failed");

	const auto* lpIcon = static_cast<MEMICONDIR*>(LockResource(hGlobal.get()));
	til::THROW_LIVE_ERROR_IF(!lpIcon, "LockResource failed");

	const auto lpIR = std::make_unique<ICONRESOURCE>();
	til::THROW_LIVE_ERROR_IF(!lpIR, "make_unique failed");
	lpIR->IconImages = std::make_unique<ICONIMAGE[]>(lpIcon->idCount - 1);
	lpIR->nNumImages = lpIcon->idCount;

	// Go through all the icons
	for (UINT i = 0; i < lpIR->nNumImages; ++i) {
		// Get the individual icon
		const auto _rsrc = FindResource(hResource, MAKEINTRESOURCE(lpIcon->idEntries[i].nID), RT_ICON);
		if (hRsrc == nullptr)
			return GetLastError();
		const auto _global = LoadResource(hResource, _rsrc);
		if (_global == nullptr)
			return GetLastError();

		// Store a copy of the resource locally
		lpIR->IconImages[i].dwNumBytes = SizeofResource(hResource, hRsrc);
		lpIR->IconImages[i].lpBits = std::make_unique<BYTE[]>(lpIR->IconImages[i].dwNumBytes);
		if (!lpIR->IconImages[i].lpBits)
			return GetLastError();

		memcpy(lpIR->IconImages[i].lpBits.get(), LockResource(_global), lpIR->IconImages[i].dwNumBytes);

		// Adjust internal pointers
		if (!AdjustIconImagePointers(&(lpIR->IconImages[i])))
			return GetLastError();
	}

	return WriteIconToICOFile(lpIR.get(), TargetICON);
}


void SetupBareRepoIcon(const CString& path) {
	constexpr auto sIni = L"[.ShellClassInfo]\r\nConfirmFileOp=0\r\nIconFile=git.ico\r\nIconIndex=0\r\nInfoTip=Git Repository\r\n"sv;

	if (PathFileExists(path + L"\\Desktop.ini"))
		return;

	// create a desktop.ini file which sets our own icon for the repo folder
	// we extract the icon to use from the resources and write it to disk
	// so even those who don't have TGit installed can benefit from it.
	if (IconExtractor::ExtractIcon(nullptr, MAKEINTRESOURCE(IDI_GITFOLDER), path + L"\\git.ico") == 0)
	{
		const auto hFile = wil::unique_handle(::CreateFile(
			path + L"\\Desktop.ini", GENERIC_WRITE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN,
			nullptr
		));
		if (hFile) 	{
			DWORD dwWritten = 0;
			WriteFile(hFile.get(), sIni.data(), std::ssize(sIni) * sizeof(wchar_t), &dwWritten, nullptr);
		}
		PathMakeSystemFolder(path);
	}
}


}
export using namespace IconExtractor;
// NOLINTEND(*-avoid-c-arrays)
