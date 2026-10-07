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
import gsl;

using std::operator""s;
using std::operator""sv;
namespace fs = std::filesystem;

export namespace wil {


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


// Win32
using RWND = std::remove_pointer_t<HWND>;
using RKEY = std::remove_pointer_t<HKEY>;
using RHANDLE = std::remove_pointer_t<HANDLE>;
using RINSTANCE = std::remove_pointer_t<HINSTANCE>;
using RWSTR = std::remove_pointer_t<LPWSTR>;

constexpr auto UIDToResID(const std::uint_fast32_t uID) { return MAKEINTRESOURCE((uID >> 4) + 1); }
constexpr auto FirstNibble(const std::uint_fast32_t uID) { return uID & ((1 << 4) - 1); }

template <typename ErrT>
using expect_nothing = std::expected<void, ErrT>;
using expect_no_hres = std::expected<void, winrt::hresult>;

template <class E>
struct ThrowIfError {
    explicit ThrowIfError(const expect_nothing<E>& r) { r.value(); };
};


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
using wil::make_range;
using wil::open_file;
using wil::GetFileInfo;



namespace winrt
{
    using ::winrt::clock;
    using ::winrt::hresult;
    using ::winrt::impl::error_ok;
    using ::winrt::to_string;
    using ::winrt::to_hstring;
}



// Registry Wrapper
namespace reg
{
	using wil::reg::reg_view_details::reg_view;
	constexpr auto SOFTWARE_TGIT_KEY = L"Software\\TortoiseGit"sv;

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
	value_type get_value_typed(const std::wstring_view key, const std::optional<const value_type> o_def = std::nullopt, const std::optional<RKEY* const> o_root = std::nullopt, const std::optional<std::wstring_view> o_sub = std::nullopt) {
		auto root = o_root.value_or(hkey::CurrentUser);
		auto sub = o_sub.value_or(SOFTWARE_TGIT_KEY);
		auto tmp = wil::reg::try_get_value<value_type>(root, sub.data(), key.data());
		return tmp.value_or(o_def.value_or({}));
	}

	template<>
	bool get_value_typed(const std::wstring_view key, const std::optional<const bool> o_def, const std::optional<RKEY* const> o_root, const std::optional<std::wstring_view> o_sub)
	{
		return get_value_typed<DWORD>(key, o_def, o_root, o_sub) != 0;
	}

	// `using` cannot alias a function template's specialization; thin wrappers can.
	auto get_value_dword(const std::wstring_view key, const std::optional<DWORD> o_def = std::nullopt, const std::optional<RKEY* const> o_root = std::nullopt, const std::optional<std::wstring_view> o_sub = std::nullopt)
	{
		return get_value_typed<DWORD>(key, o_def, o_root, o_sub);
	}
	auto get_value_string(const std::wstring_view key, const std::optional<const wchar_t* const> o_def = std::nullopt, const std::optional<RKEY* const> o_root = std::nullopt, const std::optional<std::wstring_view> o_sub = std::nullopt)
	{
		return std::wstring{get_value_typed<LPCWCHAR>(key, o_def, o_root, o_sub)};
	}

	/// The writing twin of get_value_string. Best effort, as CRegString's write()
	/// was: the HRESULT is there for a caller that cares.
	inline expect_no_hres set_value_string(
	    const std::wstring_view key,
	    const std::wstring_view value,
	    RKEY* const root = hkey::CurrentUser,
	    const std::wstring_view sub = SOFTWARE_TGIT_KEY
	)
	{
		const auto ret = wil::reg::set_value_string_nothrow(root, sub.data(), key.data(), value.data());
	    if (ret != winrt::error_ok) {
	        return std::unexpected(ret);
	    } else {
	        return {};
	    }
	}
}



/// open_clipboard, retried: OpenClipboard fails while another process holds the
/// clipboard, which it usually does only briefly. Up to 8 tries, 1 ms doubling,
/// at most 127 ms in all. Test the result with `if (!clipboard)`; keep it in a
/// variable scoped to the whole operation, not in an if-init-statement, or the
/// clipboard closes again before anything is written to it.
unique_close_clipboard_call open_clipboard_retrying(RWND* const owner)
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
unique_hglobal make_moveable_hglobal(const std::wstring_view text)
{
	return make_moveable_hglobal(std::as_bytes(std::span(text.data(), text.size() + 1)));
}

unique_hglobal make_moveable_hglobal(const std::string_view text)
{
	return make_moveable_hglobal(std::as_bytes(std::span(text.data(), text.size() + 1)));
}


// Win32
using KnownFolderCLSID = KNOWNFOLDERID;
using RefKnownFolderCLSID = REFKNOWNFOLDERID;
using KnownFolderFlag = KNOWN_FOLDER_FLAG;
constexpr auto IsURL = PathIsURL;


auto GetKnownFolderPath(
	RefKnownFolderCLSID folderId,
	const KnownFolderFlag flags = KF_FLAG_DEFAULT,
	RHANDLE* const token = {}
)
{
	wil::unique_cotaskmem_string result{};
	if (::SHGetKnownFolderPath(folderId, flags, token, result.put()) != S_OK) {
        const auto hstring = winrt::to_hstring(folderId);
        throw std::runtime_error{ "SHGetKnownFolderPath failed for " + winrt::to_string(hstring) };
    }
    return fs::path{ result.get() };
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
   SECURITY_ATTRIBUTES* const lpSecurityAttributes = {},
   RHANDLE* const hTemplateFile = {}
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


/// This is a representation of an API horizon.
/// We should validate all of our invariants here.
namespace til {


/// Every high surrogate is followed by a low one, and every low one follows a high one.
[[nodiscard]] constexpr bool IsWellFormedUtf16(const std::wstring_view s) noexcept
{
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (IS_LOW_SURROGATE(s[i]))
			return false;                                             // low without a high
		if (IS_HIGH_SURROGATE(s[i]) && !(++i < s.size() && IS_LOW_SURROGATE(s[i])))
			return false;                                             // high without a low
	}
	return true;
}
static_assert( IsWellFormedUtf16(L"a\xD83D\xDE00z"));   // valid pair
// ReSharper disable CppStaticAssertFailure
static_assert(!IsWellFormedUtf16(L"a\xD83D"));          // trailing high
static_assert(!IsWellFormedUtf16(L"\xDE00z"));          // lone low
// ReSharper restore CppStaticAssertFailure

enum class CopyError { embedded_null, lone_surrogate, buffer_too_small };

/// Copies `source` into `dest` whole, NUL-terminated, as well-formed UTF-16, or not at all.
/// On failure `dest` holds "" (when it has room), never a partial or stale string.
[[nodiscard]] expect_nothing<CopyError>
CopyTerminated(const std::span<wchar_t> dest, const std::wstring_view source) noexcept
{
	const auto fail = [&](CopyError e) {
		if (!dest.empty()) dest.front() = L'\0';
		return std::unexpected(e);
	};

	if (source.contains(L'\0'))        return fail(CopyError::embedded_null);    // length == terminator
	if (!IsWellFormedUtf16(source))    return fail(CopyError::lone_surrogate);   // text, not WCHAR soup
	if (source.size() >= dest.size())  return fail(CopyError::buffer_too_small); // room for the NUL

	*std::ranges::copy(source, dest.begin()).out = L'\0';
	return {};
}

std::wstring_view get_string_resource(const std::optional<wil::RINSTANCE*> oInstance, const UINT uID, const WORD wLanguage = LANG_USER_DEFAULT)
{
    const wchar_t* pCurrentStr;
    {
        const auto hInstance = oInstance.value_or(nullptr);
        HRSRC hResInfo = FindResourceEx(hInstance, RT_STRING, wil::UIDToResID(uID), wLanguage);
        if (!hResInfo)
        {
            //try the default language before giving up!
            hResInfo = FindResource(hInstance, wil::UIDToResID(uID), RT_STRING);
            if (!hResInfo)
                return {};
        }

        const HGLOBAL hResData = LoadResource(hInstance, hResInfo);
        if (!hResData)
            return {};

        pCurrentStr = static_cast<const wchar_t*>(LockResource(hResData));
        if (!pCurrentStr)
            return {};
    }

	// 5. Index into the block to find the precise string slot (4 first bits, norm 0 to 15)
	const auto stringIndexWithinBlock = FirstNibble(uID);

	// Iterate through the variable-length strings ahead of our target slot
	for (auto i = 0u; i < stringIndexWithinBlock; ++i)
	{
		// Each entry starts with a WORD length prefix, followed by the characters
		const auto len = *reinterpret_cast<const WORD*>(pCurrentStr);
		pCurrentStr += 1 + len;
	}

	// Extract the target length and data pointer
	WORD targetLen = *reinterpret_cast<const WORD*>(pCurrentStr);
	if (targetLen == 0)
		return {}; // Slot is empty (explicitly null string in the table)

	// Return a zero-copy view directly into the PE module space
	return { pCurrentStr + 1, targetLen };
}

size_t LoadStringEx(wil::RINSTANCE* const hInstance, const UINT uID, wchar_t* const lpBuffer_, const int nBufferMax, const WORD wLanguage)
{
	if (lpBuffer_ == nullptr || nBufferMax <= 0)
		return 0;
	lpBuffer_[0] = L'\0';

	auto&& lpBuffer = std::span{ lpBuffer_, gsl::narrow<size_t>(nBufferMax) };
	auto&& resView = get_string_resource(hInstance, uID, wLanguage);
    auto&& r = CopyTerminated(lpBuffer, resView);
    if (!r)
        return 0;           // ThrowIfError{r};

    return resView.size();
}

}


} // end wil
