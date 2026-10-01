module;
#include <wil/win32_helpers.h>
#include <wil/resource.h>
#include <wil/filesystem.h>
#include <wil/registry.h>
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
using winrt::hresult;


// wil
using wil::ArgvToCommandLine;
using wil::unique_any;
using wil::unique_hfile;
using wil::unique_cotaskmem_string;
using wil::unique_file;
using wil::unique_handle;
using wil::unique_struct;
// Clipboard - what CClipboardHelper was
using wil::unique_close_clipboard_call;
using wil::open_clipboard;
using wil::unique_hglobal;



// Registry Wrapper
namespace reg
{
	using wil::reg::reg_view_details::reg_view;

	namespace hkey
	{
		using ::HKEY__;
		using FAKE_HKEY = HKEY__*;
		union FakeHKeyUnion {
			uintptr_t numeric;
			FAKE_HKEY pointer;
		};

		// ReSharper disable CppRedundantCastExpression
		const auto ClassesRoot = reinterpret_cast<HKEY>(HKEY_CLASSES_ROOT);
		const auto CurrentUser = reinterpret_cast<HKEY>(HKEY_CURRENT_USER);
		const auto LocalMachine = reinterpret_cast<HKEY>(HKEY_LOCAL_MACHINE);
		const auto CurrentUserLocalSettings = reinterpret_cast<HKEY>(HKEY_CURRENT_USER_LOCAL_SETTINGS);
		// ReSharper enable CppRedundantCastExpression
	}

	template<typename value_type>
	value_type get_value_typed(const std::wstring_view key, const std::optional<value_type> def = {}, const HKEY root = hkey::CurrentUser, const std::wstring_view sub = L"Software\\TortoiseGit"sv) {
		auto tmp = wil::reg::try_get_value<value_type>(root, sub.data(), key.data());
		if (tmp)
			return *tmp;
		else
			return *def;
	}

	template<>
	bool get_value_typed(const std::wstring_view key, const std::optional<bool> def, const HKEY root /* = hkey::CurrentUser */, const std::wstring_view sub)
	{
		return get_value_typed<DWORD>(key, def, root, sub) != 0;
	}

	// `using` cannot alias a function template's specialization; thin wrappers can.
	inline DWORD get_value_dword(const std::wstring_view key, const DWORD def = 0, const HKEY root = hkey::CurrentUser, const std::wstring_view sub = L"Software\\TortoiseGit"sv)
	{
		return get_value_typed<DWORD>(key, def, root, sub);
	}

	inline std::wstring get_value_string(const std::wstring_view key, const std::wstring_view def = {}, const HKEY root = hkey::CurrentUser, const std::wstring_view sub = L"Software\\TortoiseGit"sv)
	{
		return get_value_typed<std::wstring>(key, std::wstring(def), root, sub);
	}

	/// The writing twin of get_value_string. Best effort, as CRegString's write()
	/// was: the HRESULT is there for a caller that cares.
	inline hresult set_value_string(const std::wstring_view key, const std::wstring_view value, const HKEY root = hkey::CurrentUser, const std::wstring_view sub = L"Software\\TortoiseGit"sv)
	{
		return wil::reg::set_value_string_nothrow(root, sub.data(), key.data(), value.data());
	}
}



/// open_clipboard, retried: OpenClipboard fails while another process holds the
/// clipboard, which it usually does only briefly. Up to 8 tries, 1 ms doubling,
/// at most 127 ms in all. Test the result with `if (!clipboard)`; keep it in a
/// variable scoped to the whole operation, not in an if-init-statement, or the
/// clipboard closes again before anything is written to it.
unique_close_clipboard_call open_clipboard_retrying(const HWND owner)
{
	for (int attempt = 0; attempt < 8; ++attempt)
	{
		if (attempt > 0)
			::Sleep(1u << (attempt - 1));
		if (auto clipboard = open_clipboard(owner))
			return clipboard;
	}
	// Explicitly false: a default-constructed unique_call *does* call CloseClipboard.
	return unique_close_clipboard_call{ false };
}

/// A GMEM_MOVEABLE block holding a copy of \a bytes - the form SetClipboardData
/// and STGMEDIUM::hGlobal take. Empty on failure. Once the receiver takes it
/// (SetClipboardData succeeded, or it went into an STGMEDIUM), call release();
/// until then this frees it on every early return.
///
/// Takes bytes rather than (pointer, size) so that no caller multiplies a count
/// by sizeof: std::as_bytes(std::span(...)) does that for an object that already
/// exists, which is what replaces the SizeTMult/SizeTAdd overflow checks.
unique_hglobal make_moveable_hglobal(const std::span<const std::byte> bytes)
{
	unique_hglobal block{ ::GlobalAlloc(GMEM_MOVEABLE, bytes.size()) };
	if (!block)
		return {};
	void* const target = ::GlobalLock(block.get());
	if (!target)
		return {};
	std::ranges::copy(bytes, static_cast<std::byte*>(target));
	::GlobalUnlock(block.get());
	return block;
}

/// The text plus its terminating NUL, which is what every text clipboard
/// format expects.
unique_hglobal make_moveable_hglobal(const std::wstring& text)
{
	return make_moveable_hglobal(std::as_bytes(std::span(text.c_str(), text.size() + 1)));
}

unique_hglobal make_moveable_hglobal(const std::string& text)
{
	return make_moveable_hglobal(std::as_bytes(std::span(text.c_str(), text.size() + 1)));
}


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


using CAutoGeneralHandle =  wil::unique_handle;
using CAutoFile =  wil::unique_hfile;

// Concrete parameter types: an abbreviated template's `auto p = nullptr` default
// can never be deduced, so every call that omitted a trailing argument failed.
CAutoFile CreateFileUnique(
   const LPCWSTR lpFileName,
   const DWORD dwDesiredAccess,
   const DWORD dwShareMode,
   const DWORD dwCreationDisposition,
   const DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
   const LPSECURITY_ATTRIBUTES lpSecurityAttributes = nullptr,
   const HANDLE hTemplateFile = nullptr
)
{
	CAutoFile fileHandle{ CreateFileW(
		lpFileName,
		dwDesiredAccess,
		dwShareMode,
		lpSecurityAttributes,
		dwCreationDisposition,
		dwFlagsAndAttributes,
		hTemplateFile
	) };

	// CreateFileW returns INVALID_HANDLE_VALUE on failure, which is exactly what
	// unique_hfile's operator bool tests; get() is never null here.
	THROW_LAST_ERROR_IF(!fileHandle);

	return fileHandle;
}


}
