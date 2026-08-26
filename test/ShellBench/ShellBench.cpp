// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2026 - TortoiseGit

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

// A benchmark for the icon-overlay path, driven through TortoiseGitStub.dll
// exactly as Explorer drives it: DllGetClassObject -> IClassFactory ->
// IShellIconOverlayIdentifier::IsMemberOf, once per overlay per path.
//
// The stub itself has no measurable surface of its own -- it is a LoadLibrary
// and a forwarded call. What this measures is what Explorer actually pays for
// when it paints a folder, which is the whole chain behind that forward.
//
// The executable is deliberately named ShellTest.exe: TortoiseStub.cpp's Debug
// build refuses to load into any process that is not being debugged, with an
// allowlist of ShellTest.exe and verclsid.exe (see the DllMain there). Renaming
// the output would restrict this benchmark to Release builds.
//
// Timing methodology, and why each number is here:
//  - cold      one sweep over a freshly started cache. In ShellCache::exe mode
//              this is mostly "TGitCache has not crawled yet" and is expected
//              to be fast *and wrong*, so it is never reported on its own.
//  - converge  repeated sweeps until two consecutive sweeps return the same
//              state for every path. TGitCache answers asynchronously, so this
//              is the number that says when the overlays a user sees are
//              actually correct. Without it, exe mode looks free.
//  - steady    one further sweep after convergence, reported per call. This is
//              the cost of re-painting a folder the user revisits.

#include <SDKDDKVer.h>
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>

namespace
{
// The overlay CLSIDs, copied from src\TortoiseShell\Guids.h. They are
// duplicated rather than included because this harness deliberately links
// nothing from the product: it must exercise the shipped DLL through its COM
// surface, not share a compilation with it.
struct OverlayId
{
	const wchar_t*	name;
	CLSID			clsid;
};

const OverlayId kOverlays[] = {
	{ L"normal",		{ 0x451C7E59, 0x058F, 0x450a, { 0x8C, 0x42, 0xFE, 0x9A, 0x12, 0xA3, 0x02, 0xFC } } },
	{ L"modified",		{ 0x8DA7CDCB, 0xDC0B, 0x4246, { 0x80, 0xBD, 0x81, 0x2E, 0x94, 0x27, 0x34, 0xAF } } },
	{ L"conflicting",	{ 0x475A024D, 0x6157, 0x4e03, { 0x8C, 0x61, 0xD1, 0xFA, 0x98, 0x06, 0x41, 0x5C } } },
	{ L"unversioned",	{ 0x18BF1135, 0x6EA2, 0x405f, { 0xA7, 0x1E, 0x16, 0xEE, 0xE7, 0xF7, 0x1F, 0x8B } } },
	{ L"added",			{ 0xA38915E4, 0xA460, 0x4143, { 0x8D, 0x6B, 0x0B, 0x45, 0x56, 0x4C, 0x6A, 0x00 } } },
	{ L"deleted",		{ 0xD69716CD, 0x6993, 0x4d0d, { 0x89, 0x8F, 0x5E, 0xBB, 0xC2, 0x5C, 0x5D, 0x4D } } },
	{ L"ignored",		{ 0x1B94B098, 0x57C6, 0x4c39, { 0x9D, 0xC5, 0x8E, 0xB0, 0x0E, 0x42, 0x3D, 0x3E } } },
	{ L"readonly",		{ 0x5F380D0B, 0xEE64, 0x479b, { 0xB2, 0xAD, 0xEF, 0x43, 0x7b, 0xf4, 0xb0, 0xa6 } } },
	{ L"locked",		{ 0x4E453CBA, 0x2AAB, 0x465c, { 0xa0, 0x1E, 0x62, 0x7A, 0x7B, 0xE9, 0xED, 0x73 } } },
};
constexpr size_t kOverlayCount = _countof(kOverlays);

using PFN_DllGetClassObject = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);

double	g_qpcFrequency = 0.0;

double Now()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return static_cast<double>(t.QuadPart) / g_qpcFrequency;
}

// Explorer asks about whatever is visible in a folder, not about what git
// tracks, so this walks the working tree rather than shelling out to
// `git ls-files`. .git is skipped: Explorer would not enumerate it either
// (it is hidden), and including it would benchmark the object store.
// Breadth-first, deliberately. A depth-first walk truncated at --limit samples
// one arbitrary corner of the tree, which made the first version of this
// benchmark report status counts that varied with directory naming rather than
// with the repository. Breadth-first spreads the sample over the whole tree the
// way a user browsing folders does.
void CollectPaths(const std::wstring& root, size_t limit, std::vector<std::wstring>& out)
{
	std::vector<std::wstring> queue{ root };
	for (size_t head = 0; head < queue.size() && out.size() < limit; ++head)
	{
		WIN32_FIND_DATAW data;
		const std::wstring pattern = queue[head] + L"\\*";
		HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
		if (h == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
				continue;
			if (!wcscmp(data.cFileName, L".git"))
				continue;

			std::wstring full = queue[head] + L'\\' + data.cFileName;
			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				queue.push_back(full);
			if (out.size() < limit)
				out.push_back(std::move(full));
		} while (out.size() < limit && ::FindNextFileW(h, &data));
		::FindClose(h);
	}
}

// One IsMemberOf sweep. state[i] records which overlay claimed path i, or
// kNoOverlay -- that vector is what convergence is measured on.
constexpr uint8_t kNoOverlay = 0xFF;

double Sweep(IShellIconOverlayIdentifier* const* handlers, const std::vector<std::wstring>& paths, std::vector<uint8_t>& state)
{
	state.assign(paths.size(), kNoOverlay);
	const double start = Now();
	for (size_t i = 0; i < paths.size(); ++i)
	{
		const DWORD attrs = ::GetFileAttributesW(paths[i].c_str());
		for (size_t o = 0; o < kOverlayCount; ++o)
		{
			if (!handlers[o])
				continue;
			if (handlers[o]->IsMemberOf(paths[i].c_str(), attrs) == S_OK)
			{
				state[i] = static_cast<uint8_t>(o);
				break;
			}
		}
	}
	return Now() - start;
}

void SetCacheType(DWORD value)
{
	HKEY key = nullptr;
	if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TortoiseGit", 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) != ERROR_SUCCESS)
	{
		wprintf(L"warning: could not open HKCU\\Software\\TortoiseGit for writing\n");
		return;
	}
	::RegSetValueExW(key, L"CacheType", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
	::RegCloseKey(key);
}

const wchar_t* CacheTypeName(DWORD t)
{
	switch (t)
	{
		case 0: return L"none";
		case 1: return L"exe (TGitCache)";
		case 2: return L"dll";
		case 3: return L"dllFull";
		default: return L"?";
	}
}

void KillCacheProcess()
{
	// TGitCache holds crawled state in memory, so leaving a warm one running
	// would make every "cold" number a warm one. It restarts on demand.
	STARTUPINFOW si{ sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi{};
	wchar_t cmd[] = L"taskkill.exe /F /IM TGitCache.exe";
	if (::CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		::WaitForSingleObject(pi.hProcess, 10000);
		::CloseHandle(pi.hProcess);
		::CloseHandle(pi.hThread);
	}
}
} // namespace

int wmain(int argc, wchar_t* argv[])
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	g_qpcFrequency = static_cast<double>(freq.QuadPart);

	std::vector<std::wstring> repos;
	std::wstring stubPath;
	size_t limit = 5000;
	int maxRounds = 40;
	DWORD cacheType = 1;
	bool killCache = true;

	for (int i = 1; i < argc; ++i)
	{
		const std::wstring a = argv[i];
		if (a == L"--repo" && i + 1 < argc)
			repos.emplace_back(argv[++i]);
		else if (a == L"--stub" && i + 1 < argc)
			stubPath = argv[++i];
		else if (a == L"--limit" && i + 1 < argc)
			limit = static_cast<size_t>(_wtoi(argv[++i]));
		else if (a == L"--rounds" && i + 1 < argc)
			maxRounds = _wtoi(argv[++i]);
		else if (a == L"--cachetype" && i + 1 < argc)
			cacheType = static_cast<DWORD>(_wtoi(argv[++i]));
		else if (a == L"--keep-cache")
			killCache = false;
		else
		{
			wprintf(L"usage: ShellTest.exe --repo <dir> [--repo <dir>...] [--stub <TortoiseGitStub.dll>]\n"
					L"                     [--cachetype 0|1|2|3] [--limit N] [--rounds N] [--keep-cache]\n"
					L"\n"
					L"  --cachetype  0=none 1=exe(TGitCache) 2=dll 3=dllFull; written to\n"
					L"               HKCU\\Software\\TortoiseGit\\CacheType before the DLL is loaded.\n"
					L"               Run one process per mode: ShellCache reads it once.\n");
			return 2;
		}
	}
	if (repos.empty())
	{
		wprintf(L"error: at least one --repo is required\n");
		return 2;
	}

	// Order matters: the value has to be in place before TortoiseGit.dll is
	// loaded, because ShellCache reads it when the first overlay handler is
	// constructed and caches it behind a ticker.
	SetCacheType(cacheType);
	if (killCache)
		KillCacheProcess();

	if (stubPath.empty())
	{
		wchar_t self[MAX_PATH]{};
		::GetModuleFileNameW(nullptr, self, _countof(self));
		::PathRemoveFileSpecW(self);
		stubPath = std::wstring(self) + L"\\TortoiseGitStub.dll";
	}

	::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	const HMODULE stub = ::LoadLibraryExW(stubPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!stub)
	{
		wprintf(L"error: LoadLibrary(%s) failed with %lu\n", stubPath.c_str(), ::GetLastError());
		wprintf(L"       a Debug TortoiseGitStub.dll only loads into ShellTest.exe, verclsid.exe\n"
				L"       or a debugged process - see DllMain in src\\TortoiseShell\\TortoiseStub.cpp\n");
		return 1;
	}

	const auto getClassObject = reinterpret_cast<PFN_DllGetClassObject>(::GetProcAddress(stub, "DllGetClassObject"));
	if (!getClassObject)
	{
		wprintf(L"error: TortoiseGitStub.dll exports no DllGetClassObject\n");
		return 1;
	}

	IShellIconOverlayIdentifier* handlers[kOverlayCount] = {};
	const double loadStart = Now();
	size_t live = 0;
	for (size_t o = 0; o < kOverlayCount; ++o)
	{
		IClassFactory* factory = nullptr;
		HRESULT hr = getClassObject(kOverlays[o].clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
		if (FAILED(hr) || !factory)
		{
			wprintf(L"warning: no class factory for %s (hr=0x%08lX)\n", kOverlays[o].name, static_cast<unsigned long>(hr));
			continue;
		}
		hr = factory->CreateInstance(nullptr, IID_IShellIconOverlayIdentifier, reinterpret_cast<void**>(&handlers[o]));
		factory->Release();
		if (FAILED(hr))
		{
			wprintf(L"warning: no overlay identifier for %s (hr=0x%08lX)\n", kOverlays[o].name, static_cast<unsigned long>(hr));
			handlers[o] = nullptr;
			continue;
		}
		++live;
	}
	const double loadMs = (Now() - loadStart) * 1000.0;

	wprintf(L"stub          %s\n", stubPath.c_str());
	wprintf(L"cache mode    %lu (%s)\n", cacheType, CacheTypeName(cacheType));
	wprintf(L"handlers      %zu of %zu, instantiated in %.1f ms (includes the real DLL's load)\n\n", live, kOverlayCount, loadMs);

	wprintf(L"%-26s %8s %10s %10s %8s %12s\n", L"repo", L"paths", L"cold ms", L"conv ms", L"rounds", L"steady us/p");
	wprintf(L"%-26s %8s %10s %10s %8s %12s\n", L"--------------------------", L"--------", L"----------", L"----------", L"--------", L"------------");

	for (const auto& repo : repos)
	{
		std::vector<std::wstring> paths;
		CollectPaths(repo, limit, paths);
		if (paths.empty())
		{
			wprintf(L"%-26s   (no files found)\n", repo.c_str());
			continue;
		}

		std::vector<uint8_t> prev, cur;
		const double coldMs = Sweep(handlers, paths, prev) * 1000.0;

		// Convergence: sweep until two consecutive sweeps agree.
		//
		// Agreement alone is NOT enough, and getting that wrong is the trap
		// this benchmark exists to avoid. IsMemberOf returning S_FALSE from
		// every handler is ambiguous: it means "this path carries no overlay",
		// which covers both "not versioned" and "TGitCache has not crawled
		// this yet". Two consecutive all-S_FALSE sweeps agree perfectly while
		// carrying no information at all -- which is exactly what an
		// uncrawled TGitCache produces, and it makes exe mode look instant.
		// So convergence additionally requires that something was claimed.
		const auto claimedIn = [](const std::vector<uint8_t>& v) {
			return static_cast<size_t>(std::count_if(v.begin(), v.end(), [](uint8_t s) { return s != kNoOverlay; }));
		};

		double convMs = coldMs;
		int rounds = 1;
		for (; rounds < maxRounds; ++rounds)
		{
			convMs += Sweep(handlers, paths, cur) * 1000.0;
			if (cur == prev && claimedIn(cur) > 0)
				break;
			prev.swap(cur);
		}
		const size_t claimed = claimedIn(cur);

		const double steadyMs = Sweep(handlers, paths, cur) * 1000.0;
		const double perPathUs = (steadyMs * 1000.0) / static_cast<double>(paths.size());

		const wchar_t* leaf = ::PathFindFileNameW(repo.c_str());
		wprintf(L"%-26s %8zu %10.1f %10.1f %8d %12.1f%s\n", leaf, paths.size(), coldMs, convMs, rounds, perPathUs,
				rounds >= maxRounds ? (claimed ? L"  (never settled)" : L"  (NO OVERLAY EVER CLAIMED - timings meaningless)") : L"");

		// Without this the timings are uninterpretable: a sweep in which every
		// path is rejected as "not in a working copy" is fast and measures
		// nothing. Printing which overlays were actually claimed is what
		// distinguishes a real status computation from a fast bail-out.
		size_t histogram[kOverlayCount + 1] = {};
		for (const uint8_t s : cur)
			++histogram[s == kNoOverlay ? kOverlayCount : s];
		wprintf(L"%-26s   states:", L"");
		for (size_t o = 0; o < kOverlayCount; ++o)
		{
			if (histogram[o])
				wprintf(L" %s=%zu", kOverlays[o].name, histogram[o]);
		}
		if (histogram[kOverlayCount])
			wprintf(L" (none)=%zu", histogram[kOverlayCount]);
		wprintf(L"\n");
	}

	for (auto* h : handlers)
	{
		if (h)
			h->Release();
	}
	::CoUninitialize();
	// The stub is deliberately not FreeLibrary'd: DllCanUnloadNow would unload
	// the real DLL underneath us and there is nothing left to measure.
	return 0;
}
