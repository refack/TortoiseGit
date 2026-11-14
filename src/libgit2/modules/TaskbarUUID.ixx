module;
#include <windows.h>
#include <Shobjidl.h>
#include <GdiPlus.h>

export module TaskbarUUID;

import std;
import gsl;
import Registry;
import RIAA;
import CmdLineParser;
import wil;		// com_ptr_nothrow, what CComPtr was

#pragma comment(lib, "gdiplus.lib")
constexpr auto APPID = L"TGIT.TGIT.1";


// export: "export using namespace" of an unexported namespace exports nothing
export namespace TaskbarUUID {


// The process's taskbar grouping: /groupuuid:, or one derived from the working tree,
// and the overlay icon its repository chose. Set by an application's InitInstance,
// read by every StandAloneDlg's OnTaskbarButtonCreated. They live here, below
// StandAloneDlg, because TortoiseProc (their old home) sits above it: TortoiseProc's
// GMF includes dialogs that `import StandAloneDlg`, so the reverse edge is a cycle.
// TortoiseProc re-exports them, so TortoiseProc::g_sGroupingUUID still names these.
std::wstring g_sGroupingUUID{};
std::wstring g_sGroupingIcon{};
bool g_bGroupingRemoveIcon{};


/**
 * Returns the App ID string. See \ref SetTaskIDPerUUID() for details.
 */
std::wstring GetTaskIDPerUUID(const std::wstring_view uuid = {}) {
	auto r = CRegStdDWORD(L"Software\\TortoiseGit\\GroupTaskbarIconsPerRepo", 3);
	std::wstring id = APPID;
	if ((r < 2) || (r == 3)) {
		wchar_t buf[MAX_PATH] = {};
		GetModuleFileName(nullptr, buf, _countof(buf));
		std::wstring n = buf;
		n = n.substr(n.find_last_of('\\'));
		id += n;
	}

	if (r >= 3) {
		if (!uuid.empty()) {
			id += uuid;
		}
		else {
			const CCmdLineParser parser(GetCommandLine());
			if (parser.HasVal(L"groupuuid")) {
				id += parser.GetValStr(L"groupuuid");
			}
		}
	}
	return id;
}


/**
 * Sets the Task ID (Win7) for the process according to settings
 * in the registry:
 * HKCU\\Software\\TortoiseGit\\GroupTaskbarIconsPerRepo
 * determines how this is done. The Task ID is used by the windows
 * taskbar to determine how the app icons on the taskbar are grouped
 * together.
 *
 * GroupTaskbarIconsPerRepo = 0 : Icons are grouped by application.
 * GroupTaskbarIconsPerRepo = 1 : same as 0, one icon per application (regardless of working tree)
 * GroupTaskbarIconsPerRepo = 2 : All TGit icons are grouped to one icon (regardless of working tree)
 * GroupTaskbarIconsPerRepo = 3 : Icons are grouped by working tree path, so
 *                                each TGit dialog gets grouped according
 *                                to the working tree it is used for.
 *                                Each TGit app is grouped separately, i.e.,
 *                                TortoiseGitMerge icons won't get grouped together
 *                                with TortoiseGitProc icons.
 * GroupTaskbarIconsPerRepo = 4 : The same as 1, but all TGit apps are treated
 *                                as one, e.g., a TortoiseGitMerge instance showing
 *                                a diff from repo X is grouped together with
 *                                a log dialog instance for repo X.
 *
 * The repository uuid is used by examining the command line of the process:
 * it must be set with /groupuuid:"uuid".
 */
void SetTaskIDPerUUID() {
	// Called directly: shell32 has exported it since Windows 7, so the
	// AtlLoadSystemLibraryUsingFullPath/GetProcAddress probe guarded nothing.
	const std::wstring id = GetTaskIDPerUUID();
	std::ignore = ::SetCurrentProcessExplicitAppUserModelID(id.c_str());
}



struct GroupEntry { std::wstring_view uuid, icon; };

std::vector<GroupEntry> LoadGroupEntries(const int foundUUIDIndex) {
	// read 0..N until empty, split on ';'
	const auto tsr = std::format(L"{}{}", L"LastUsedUUIDsForGrouping\\", foundUUIDIndex);
	auto value = wil::reg::try_get_tgit_string(tsr).value_or({});
	auto ret = std::views::split(value, L';')
	| std::views::transform([](auto pair_str) {
		auto sv = std::wstring_view{pair_str};
		const auto split_pos = sv.find(L';');
		return GroupEntry{.uuid = sv.substr(0, split_pos), .icon = sv.substr(split_pos + 1)};
	})
	| std::ranges::to<std::vector>();
	return ret;
}
void SaveGroupEntry(size_t index, const GroupEntry& e);        // join with ';' iff icon non-empty

/**
 * Sets a different overlay icon for the taskbar icon on Win7 for each
 * repository uuid. This allows to 'mark' the uuid-grouped icons on the
 * taskbar to make them more distinguishable.
 * Call this function from the OnTaskbarButtonCreated() message handler.
 * To receive this message, you must first register it:
 * \code
 * const UINT TaskBarButtonCreated = RegisterWindowMessage(L"TaskbarButtonCreated");
 * \endcode
 *
 * \param hWnd the "batton"
 * \param uuid the repository uuid; nothing is done when it is empty
 * \param sicon_arg an .ico or image file to use as the overlay, or empty
 * \param bRemoveicon reset the icon remembered for \a uuid
 */
void SetUUIDOverlayIcon(const HWND& hWnd, const std::wstring& uuid, const std::wstring& sicon_arg, const bool bRemoveicon) {
	if (uuid.empty())
		return;

	if (!CRegStdDWORD(L"Software\\TortoiseGit\\GroupTaskbarIconsPerRepo", 3))
		return;

	if (!CRegStdDWORD(L"Software\\TortoiseGit\\GroupTaskbarIconsPerRepoOverlay", TRUE))
		return;

	wil::com_ptr_nothrow<ITaskbarList3> pTaskbarInterface;
	if (FAILED(
		::CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(pTaskbarInterface.put()))))
		return;

	auto sicon = std::wstring{sicon_arg};
	int foundUUIDIndex = 0;
	do {
		// auto entries = LoadGroupEntries();
		// auto it = std::ranges::find(entries, uuid, &GroupEntry::uuid);
		// auto thing = (it == entries.end()) ? entries.emplace_back(uuid, sicon_arg) : *it;
		//
		// auto sicon = sicon_arg;
		// if (bRemoveicon)
		// 	thing.icon.clear();
		// else if (!sicon_arg.empty())
		// 	thing.icon = sicon_arg;
		// else
		// 	sicon = thing.icon;                       // read back
		//
		// SaveGroupEntry(it - entries.begin(), *it);

		const auto tsr = std::format(L"{}{}", L"Software\\TortoiseGit\\LastUsedUUIDsForGrouping\\", foundUUIDIndex);
		auto r = CRegStdString(tsr);
		std::wstring sr = r;
		if (sr.empty()) {
			sr = uuid;
			sr += sicon.empty() ? L"" : (L";" + sicon);
			r = sr;
		}
		size_t sep = sr.find(L';');
		const std::wstring olduuid = sep != std::wstring::npos ? sr.substr(0, sep) : sr;
		if (olduuid == uuid) {
			if (bRemoveicon)
				r = uuid; // reset icon path in registry
			else if (!sicon.empty())
				r = uuid + (sicon.empty() ? L"" : (L";" + sicon));
			else
				sicon = sep != std::wstring::npos ? sr.substr(sep + 1) : L"";
			break;
		}
		foundUUIDIndex++;
	}
	while (foundUUIDIndex < 20);
	if (foundUUIDIndex >= 20) {
		auto r = CRegStdString(L"Software\\TortoiseGit\\LastUsedUUIDsForGrouping\\1");
		r.removeKey();
	}

	int iconWidth = GetSystemMetrics(SM_CXSMICON);
	int iconHeight = GetSystemMetrics(SM_CYSMICON);

	CAutoIcon icon;
	if (!sicon.empty()) {
		if (sicon.size() >= 4 && !_wcsicmp(sicon.substr(sicon.size() - 4).c_str(), L".ico"))
			icon = wil::LoadIconEx(nullptr, sicon.c_str(), iconWidth, iconHeight);
		else {
			ULONG_PTR gdiplusToken = 0;
			Gdiplus::GdiplusStartupInput gdiplusStartupInput;
			GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
			if (gdiplusToken) {
				{
					const auto pBitmap = std::make_unique<Gdiplus::Bitmap>(sicon.c_str(), FALSE);
					if (pBitmap->GetLastStatus() == Gdiplus::Status::Ok)
						pBitmap->GetHICON(icon.GetPointer());
				}
				Gdiplus::GdiplusShutdown(gdiplusToken);
			}
		}
	}

	if (!icon) {
		DWORD colors[6] = { 0x80FF0000, 0x80FFFF00, 0x8000FF00, 0x800000FF, 0x80000000, 0x8000FFFF };

		const std::vector<BYTE>::size_type size = iconWidth * iconWidth;
		// AND mask - monochrome - determines which pixels get drawn
		std::vector<BYTE> ADD{};
		ADD.assign(size, 0xFF);
		// XOR mask - 32bpp ARGB - determines the pixel values
		std::vector<BYTE> XOR{};
		auto add_val = static_cast<BYTE>(colors[foundUUIDIndex % 6]);
		ADD.assign(size, add_val);
		icon = CreateIcon(nullptr, iconWidth, iconHeight, 1, 32, ADD.data(), XOR.data());
	}
	std::ignore = pTaskbarInterface->SetOverlayIcon(hWnd, icon, uuid.c_str());
}


/**
 * \ref SetUUIDOverlayIcon for an application without grouping globals: the uuid
 * is /groupuuid:"uuid" from the process's command line, with no icon. What the
 * non-MFC build of the old one-argument SetUUIDOverlayIcon did.
 */
void SetUUIDOverlayIconFromCommandLine(const HWND& hWnd) {
	const auto parser = CCmdLineParser{ GetCommandLine() };
	// GetValStr, not GetVal - std::wstring(nullptr) is UB (CLAUDE.md failure mode 5)
	SetUUIDOverlayIcon(hWnd, parser.GetValPtr(L"groupuuid"), std::wstring{}, false);
}
}

export using namespace TaskbarUUID;
