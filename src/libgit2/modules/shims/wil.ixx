module;
#include <wil/win32_helpers.h>
#include <wil/resource.h>
#include <wil/filesystem.h>
#include <wil/registry.h>
#include <wil/com.h>			// com_ptr_nothrow: what CComPtr was, with no ATL header
#include <wil/stl.h>			// make_range
#include <winrt/base.h>
#include <ShlObj.h>			// SHGetKnownFolderPath, FOLDERID_*, REFKNOWNFOLDERID
#include <Shlwapi.h>		// PathCompactPathEx, for the fs::path formatter's "{:>N}"
#include <git2.h>			// the libgit2 owners and OpenGitRepository
#pragma comment(lib, "Shlwapi.lib")	// PathCompactPathEx



export module wil;
import std;
import gsl;
import invarients;			// LatchObjectFormat; invarients imports only std, so this stays acyclic

using namespace std::literals;
namespace fs = std::filesystem;
using sloc_t = std::source_location;


/// This is a representation of an API horizon.
/// We should validate all of our invariants here.
///
/// Kept, deliberately, past the wil trim: MAKESTRING (TortoiseShell) reads its
/// strings through LoadStringEx, and moving it to ::LoadStringW changes what it
/// returns for an over-long, NUL-bearing or ill-formed string and which language
/// it picks. That is a behaviour decision with its own commit, not a trim.
export namespace til {

using ::winrt::clock;

// Win32
using RWND = std::remove_pointer_t<HWND>;
using RKEY = std::remove_pointer_t<HKEY>;
using RHANDLE = std::remove_pointer_t<HANDLE>;
using RINSTANCE = std::remove_pointer_t<HINSTANCE>;
using RWSTR = std::remove_pointer_t<LPWSTR>;

constexpr auto UIDToResID(const std::uint_fast32_t uID) { return MAKEINTRESOURCE((uID >> 4U) + 1); }
constexpr auto FirstNibble(const std::uint_fast32_t uID) { return uID & ((1U << 4U) - 1); }

template <typename ErrT>
using expect_nothing = std::expected<void, ErrT>;
using expect_no_hres = std::expected<void, winrt::hresult>;

template <class E>
struct ThrowIfError {
	explicit ThrowIfError(const std::expected<void, E>& r) {
		r.value();
	}
};


[[noreturn]] inline void THROW_LIVE_ERROR(const std::string_view what, const sloc_t loc)
{
	const DWORD err = ::GetLastError();
	throw std::system_error(win32_cast(err), std::system_category(), std::format("[{}:{}] {}", loc.file_name(), loc.line(), what));
}

inline void THROW_LIVE_ERROR_IF(const bool cond, const std::string_view what, const sloc_t loc = {})
{
	if (cond) THROW_LIVE_ERROR(what, loc);
}


static_assert(std::is_convertible_v<const std::string&, std::string_view>);
static_assert(std::is_convertible_v<const std::string&, const char*> == false);
static_assert(std::is_convertible_v<std::string_view, const std::string&> == false);


/// Every high surrogate is followed by a low one, and every low one follows a high one.
[[nodiscard]] constexpr bool IsWellFormedUtf16(const wil::zwstring_view s) noexcept {
	for (size_t i = 0; i < s.size(); ++i) {
		if (IS_LOW_SURROGATE(s.at(i)))
			return false; // low without a high
		if (IS_HIGH_SURROGATE(s.at(i)) && !(++i < s.size() && IS_LOW_SURROGATE(s.at(i))))
			return false; // high without a low
	}
	return true;
}


static_assert(IsWellFormedUtf16(L"a\xD83D\xDE00z")); // valid pair
// ReSharper disable CppStaticAssertFailure
static_assert(!IsWellFormedUtf16(L"a\xD83D")); // trailing high
static_assert(!IsWellFormedUtf16(L"\xDE00z")); // lone low
// ReSharper restore CppStaticAssertFailure

enum class CopyError : std::uint8_t { embedded_null, lone_surrogate, buffer_too_small };



/// Copies `source` into `dest` whole, NUL-terminated, as well-formed UTF-16, or not at all.
/// On failure `dest` holds "" (when it has room), never a partial or stale string.
[[nodiscard]] std::expected<void, CopyError>
CopyTerminated(const std::span<wchar_t> dest, const wil::zwstring_view source) noexcept {
	const auto fail = [&](CopyError e) {
		if (!dest.empty())
			dest.front() = L'\0';
		return std::unexpected(e);
	};

	if (source.contains(L'\0'))
		return fail(CopyError::embedded_null); // length == terminator
	if (!IsWellFormedUtf16(source))
		return fail(CopyError::lone_surrogate); // text, not WCHAR soup
	if (source.size() >= dest.size())
		return fail(CopyError::buffer_too_small); // room for the NUL

	*std::ranges::copy(source, dest.begin()).out = L'\0';
	return {};
}


wil::zwstring_view get_string_resource(const UINT uID, const std::optional<HINSTANCE> oInstance = {}, const WORD wLanguage = LANG_USER_DEFAULT) {
	auto pCurrentStr = [&] -> wil::zwstring_view {
		// A string table is stored in blocks of 16: block (uID / 16) + 1, slot uID % 16.
		const auto* const blockResId = MAKEINTRESOURCE((uID >> 4U) + 1);
		auto* const hInstance = oInstance.value_or(nullptr);
		HRSRC hResInfo = FindResourceEx(hInstance, RT_STRING, blockResId, wLanguage);
		if (!hResInfo) {
			//try the default language before giving up!
			hResInfo = FindResource(hInstance, blockResId, RT_STRING);
			if (!hResInfo)
				return {};
		}

		const HGLOBAL hResData = LoadResource(hInstance, hResInfo);
		if (!hResData)
			return {};
		const auto bytes = ::SizeofResource(hInstance, hResInfo);
		if (!bytes)
			return {};
		const auto* const resVoidp = LockResource(hResData);
		if (!resVoidp)
			return {};
		return wil::zwstring_view{static_cast<const wchar_t*>(resVoidp), bytes / sizeof(wchar_t)};
	}();


	// 5. Index into the block to find the precise string slot (4 first bits, norm 0 to 15)
	const auto stringIndexWithinBlock = uID & ((1U << 4U) - 1);

	// Iterate through the variable-length strings ahead of our target slot
	for (auto i = 0U; i < stringIndexWithinBlock; ++i) {
		// Each entry starts with a WORD length prefix, followed by the characters
		const auto len = pCurrentStr.front();
		pCurrentStr.remove_prefix(sizeof(len) + len);
	}

	// Extract the target length and data pointer
	const WORD targetLen = *pCurrentStr.begin();
	if (!targetLen)
		return {}; // Slot is empty (explicitly null string in the table)
	pCurrentStr = pCurrentStr.substr(1, targetLen);

	// Return a zero-copy view directly into the PE module space
	return pCurrentStr;
}


size_t LoadStringEx(const HINSTANCE& hInstance, const UINT uID, const std::span<wchar_t> lpBuffer, const WORD wLanguage) {
	if (lpBuffer.empty())
		return 0;
	*lpBuffer.begin() = L'\0';

	auto&& resView = get_string_resource(uID, hInstance, wLanguage);
	// ReSharper disable once CppTooWideScopeInitStatement
	auto&& r = CopyTerminated(lpBuffer, resView);
	if (!r)
		return 0;

	return resView.size();
}



struct ConvOpts {
	int code_page = CP_UTF8;
	DWORD flags = 0;
	const char* replacment = nullptr;
};


std::string WideCharToMultiByte(const wil::zwstring_view wide, const ConvOpts opts = {},	const sloc_t loc = sloc_t::current()) {
	if (wide.empty())
		return {};

	// The Macros are number literals (int) but the API calls for UINT :facepalm:
	const auto stupidMicrosoftValue = gsl::narrow<UINT>(opts.code_page);
	const int needed_bytes = ::WideCharToMultiByte(stupidMicrosoftValue, opts.flags, wide.data(), win32_cast(wide.length()), nullptr, 0, opts.replacment, nullptr);
	THROW_LIVE_ERROR_IF(needed_bytes <= 0, "WIN32::WideCharToMultiByte failed", loc);

	std::string out;
	out.resize_and_overwrite(
		needed_bytes,
		[&](auto buf_ptr, auto buf_cap) -> size_t {
			return ::WideCharToMultiByte(opts.code_page, opts.flags, wide.data(), win32_cast(wide.length()), buf_ptr, win32_cast(buf_cap), opts.replacment, nullptr);
		}
	);
	THROW_LIVE_ERROR_IF(out.empty(), "WIN32::WideCharToMultiByte failed", loc);

	return out;
}


std::string WStringToUTF8(const std::wstring& wide) { return WideCharToMultiByte(wide); }


std::wstring MultiByteToWideChar(const std::string_view narrow, const ConvOpts opts = {}, const sloc_t loc = sloc_t::current()) {
	if (narrow.empty())
		return {};

	const int needed_bytes = ::MultiByteToWideChar(opts.code_page, opts.flags, narrow.data(), win32_cast(narrow.length()), nullptr, 0);
	THROW_LIVE_ERROR_IF(needed_bytes <= 0, "WIN32::MultiByteToWideChar failed", loc);

	std::wstring out;
	out.resize_and_overwrite(
		needed_bytes,
		[&](auto buf_ptr, auto buf_cap) -> size_t {
			return ::MultiByteToWideChar(opts.code_page, opts.flags, narrow.data(), win32_cast(narrow.length()), buf_ptr, win32_cast(buf_cap));
		}
	);
	THROW_LIVE_ERROR_IF(out.empty(), "WIN32::MultiByteToWideChar failed", loc);

	return out;
}


std::wstring UTF8ToWString(const std::string_view narrow) { return til::MultiByteToWideChar(narrow); }

void reset_proc_locale() {std::setlocale(LC_ALL, "");}


template <auto InitFn, auto FreeFn>
class scoped_lib {
	bool m_active;
public:
	scoped_lib() : m_active(InitFn() >= 0) {}
	~scoped_lib() { if (m_active) FreeFn(); }
	scoped_lib(scoped_lib&& o) noexcept : m_active(std::exchange(o.m_active, false)) {}
	scoped_lib& operator=(scoped_lib&&) = delete;
	scoped_lib(const scoped_lib&) = delete;
};
using libgit2_token = scoped_lib<git_libgit2_init, git_libgit2_shutdown>;


}


// NOLINTBEGIN(cert-dcl58-cpp)
export namespace std {

template <>
struct formatter<sloc_t, char> {
	bool with_func = false;

	constexpr auto parse(std::format_parse_context& ctx) {
		auto it = ctx.begin();
		// "{:f}" adds the function
		if (it != ctx.end() && *it == 'f') { with_func = true; ++it; }   // "{:f}" adds the function
		if (it != ctx.end() && *it != '}') throw std::format_error("bad source_location spec");
		return it;
	}

	auto format(const sloc_t& loc, auto& ctx) const {
		if (with_func)
			return std::format_to(ctx.out(), "{}({}): {}", loc.file_name(), loc.line(), loc.function_name());
		else
			return std::format_to(ctx.out(), "{}({})", loc.file_name(), loc.line());
	}
};
static_assert(std::formattable<sloc_t, char>);


template <>
struct formatter<sloc_t, wchar_t> {
	bool with_func = false;

	constexpr auto parse(std::wformat_parse_context& ctx) {
		auto it = ctx.begin();
		// "{:f}" adds the function
		if (it != ctx.end() && *it == L'f') { with_func = true;	++it; }
		if (it != ctx.end() && *it != L'}')	throw std::format_error("bad source_location spec");
		return it;
	}

	auto format(const sloc_t& loc, auto& ctx) const {
		if (with_func)
			return std::format_to(ctx.out(), L"{}({}): {}", til::UTF8ToWString(loc.file_name()), loc.line(), til::UTF8ToWString(loc.function_name()));
		else
			return std::format_to(ctx.out(), L"{}({})", til::UTF8ToWString(loc.file_name()), loc.line());
	}
};
static_assert(std::formattable<sloc_t, wchar_t>);
static_assert(std::same_as<decltype(std::format(L"{}", sloc_t{})), std::wstring>);
static_assert(std::same_as<decltype(std::format(L"{:f}", sloc_t{})), std::wstring>);


#ifndef __cpp_lib_format_path
template <>
struct formatter<fs::path, wchar_t> {
	bool generic = false;					// "{:g}" prints generic (P2845 spells it the same)
	size_t max_width = 0;					// "{:>N}" compacts via PathCompactPathEx to fit N columns; 0 compacts nothing
	size_t width_arg = std::variant_npos;	// "{:>{}}" takes N from the next format argument
											// NB: P2845's '>' is right-align padding, the opposite job; revisit the call sites when this block dies.

	constexpr auto parse(std::wformat_parse_context& ctx) {
		auto it = ctx.begin();
		if (it != ctx.end() && *it == L'g') { generic = true; ++it; }   // "{:g}" prints generic
		if (it != ctx.end() && *it == L'>') {
			++it;
			if (it != ctx.end() && *it == L'{') {
				++it;
				width_arg = ctx.next_arg_id();	// this MSVC predates P2757's check_dynamic_spec_integral; the type check is at runtime below
				if (it == ctx.end() || *it != L'}') throw std::format_error("bad path spec");
				++it;
			}
			else {
				if (it == ctx.end() || *it < L'0' || L'9' < *it) throw std::format_error("bad path spec");
				for (; it != ctx.end() && L'0' <= *it && *it <= L'9'; ++it)
					max_width = (max_width * 10) + static_cast<size_t>(*it - L'0');
			}
		}
		if (it != ctx.end() && *it != L'}') throw std::format_error("bad path spec");
		return it;
	}

	auto format(const fs::path& path, auto& ctx) const {
		auto pathstr = generic ? path.generic_wstring() : path.wstring();
		auto cch_max = max_width;
		if (width_arg != std::variant_npos)
			cch_max = visit_format_arg([]<class T>(const T& n) -> size_t {
				if constexpr (std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, wchar_t>)
					return n > 0 ? static_cast<size_t>(n) : 0;
				else
					throw std::format_error("path width is not an integer");
			},
			ctx.arg(width_arg)
		);
		if (cch_max && cch_max <= pathstr.size()) {	// cch_max counts the NUL, so exactly cch_max chars don't fit either
			std::wstring compacted;
			compacted.resize_and_overwrite(cch_max, [&pathstr](const std::wstring::pointer buf_data, const size_t buf_size) {
				return PathCompactPathEx(buf_data, pathstr.c_str(), win32_cast(buf_size), 0) ? std::wstring_view{buf_data}.size() : 0;
			});
			pathstr = std::move(compacted);
		}
		return std::format_to(ctx.out(), L"{}", pathstr);
	}
};
static_assert(std::formattable<fs::path, wchar_t>);
static_assert(std::same_as<decltype(std::format(L"{}", fs::path{".."})), std::wstring>);
static_assert(std::same_as<decltype(std::format(L"{}", fs::path{L".."})), std::wstring>);
static_assert(std::same_as<decltype(std::format(L"{:g>40}", fs::path{L".."})), std::wstring>);
static_assert(std::same_as<decltype(std::format(L"{:>{}}", fs::path{L".."}, 40)), std::wstring>);
static_assert(!std::same_as<decltype(std::format("{}", fs::path{L".."})), std::wstring>);
#endif



using ::std::runtime_error;

}
// NOLINTEND(cert-dcl58-cpp)



// Not exported: the body both text overloads of make_moveable_hglobal share.
namespace wil::tgit_details {
/// A GMEM_MOVEABLE block holding \a text and a terminating NUL that this writes
/// itself. The view need not be NUL-terminated - the previous version copied
/// text.size() + 1 characters, reading one past the end of every view that was
/// not.
template <class CharT>
unique_hglobal make_moveable_text(const std::basic_string_view<CharT> text, const sloc_t loc = sloc_t::current()) {
	const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
	auto block = unique_hglobal{ ::GlobalAlloc(GMEM_MOVEABLE, bytes.size() + sizeof(CharT)) };
	til::THROW_LIVE_ERROR_IF(!block, "WIN32::GlobalAlloc(GMEM_MOVEABLE) failed", loc);

	auto* const target = static_cast<std::byte*>(::GlobalLock(block.get()));
	til::THROW_LIVE_ERROR_IF(!target, "WIN32::GlobalLock failed", loc);

	std::ranges::fill_n(std::ranges::copy(bytes, target).out, sizeof(CharT), std::byte{ 0 });
	::GlobalUnlock(block.get());
	return block;
}
}


export namespace wil {

using ::wil::GetEnvironmentVariableW;
// The R* pointee aliases (RWND, RKEY, RHANDLE, RINSTANCE, RWSTR), file_ticks,
// expect_nothing/expect_no_hres/ThrowIfError, the wil::winrt re-exports and the
// CAuto* handle aliases are gone: they renamed Win32, std or winrt types. Spell
// HWND, HKEY, HANDLE, HINSTANCE, ::winrt::clock and unique_handle/unique_file.

// wil
using wil::ArgvToCommandLine;
// The flags and the operators DEFINE_ENUM_FLAG_OPERATORS put beside them in
// namespace wil travel together: without the operators, `a | b` on two
// re-exported enumerators finds no viable overload in an importer.
using wil::ArgvToCommandLineFlags;
using wil::operator|;
using wil::operator|=;
using wil::operator&;
using wil::operator&=;
using wil::operator^;
using wil::operator^=;
using wil::operator~;
using wil::unique_any;
using wil::unique_hfile;
using wil::unique_cotaskmem_string;
using wil::unique_hlocal_string;
using wil::unique_file;
using wil::unique_hicon;
using wil::unique_handle;
using wil::unique_hmodule;
using wil::unique_struct;
using wil::unique_hdc;
using wil::unique_hbitmap;
using wil::scope_exit;
// COM owners. The nothrow flavor only: CComPtr never threw, and the throwing
// policies reach wil's failure reporting, whose strsafe statics cannot cross an
// import (C2129, see the note below). Spell .get() / .put(); create instances
// with ::CoCreateInstance(..., IID_PPV_ARGS(p.put())).
using wil::com_ptr_nothrow;
// NOT re-exported: wil::TryGetEnvironmentVariableW, wil::reg::try_get_value_dword,
// wil::reg::try_get_value_string. They are inline in wil's headers and their
// failure-reporting path calls strsafe.h's `static` functions, which cannot cross
// a module boundary: an importer without a textual <strsafe.h> fails with C2129
// ("static function 'StringValidateDestW' declared but not defined"). Use the
// wrappers below instead (TryGetEnvironmentVariable, reg::try_get_dword,
// reg::try_get_string), whose bodies are compiled once, here.


// Clipboard - what CClipboardHelper was
using wil::unique_close_clipboard_call;
using wil::open_clipboard;
using wil::unique_hglobal;
using wil::open_file;
using wil::GetFileInfo;
using wil::ResultException;
using wil::make_range;
using wil::GetDC;



// Owners for libgit2 handles. Every one is wil's, so there is no class to get
// wrong - the hand-rolled ones they replaced re-exposed a conversion operator
// through a using-declaration that MSVC refuses across a module boundary (C2248).
// No implicit conversion to the raw pointer: pass .get(), and .put() (or &x,
// which frees first) for an out-parameter.
template <typename T, void Free(T*)>
using unique_git = wil::unique_any<T*, decltype(Free), Free>;

using unique_git_object = unique_git<git_object, git_object_free>;
using unique_git_commit = unique_git<git_commit, git_commit_free>;
using unique_git_tree = unique_git<git_tree, git_tree_free>;
using unique_git_blob = unique_git<git_blob, git_blob_free>;
using unique_git_tag = unique_git<git_tag, git_tag_free>;
using unique_git_tree_entry = unique_git<git_tree_entry, git_tree_entry_free>;
using unique_git_reference = unique_git<git_reference, git_reference_free>;
using unique_git_repository = unique_git<git_repository, git_repository_free>;
using unique_git_config = unique_git<git_config, git_config_free>;
using unique_git_config_entry = unique_git<git_config_entry, git_config_entry_free>;
using unique_git_index = unique_git<git_index, git_index_free>;
using unique_git_submodule = unique_git<git_submodule, git_submodule_free>;
using unique_git_diff = unique_git<git_diff, git_diff_free>;
using unique_git_patch = unique_git<git_patch, git_patch_free>;
using unique_git_diff_stats = unique_git<git_diff_stats, git_diff_stats_free>;
using unique_git_remote = unique_git<git_remote, git_remote_free>;
using unique_git_reflog = unique_git<git_reflog, git_reflog_free>;
using unique_git_revwalk = unique_git<git_revwalk, git_revwalk_free>;
using unique_git_branch_iterator = unique_git<git_branch_iterator, git_branch_iterator_free>;
using unique_git_reference_iterator = unique_git<git_reference_iterator, git_reference_iterator_free>;
using unique_git_describe_result = unique_git<git_describe_result, git_describe_result_free>;
using unique_git_status_list = unique_git<git_status_list, git_status_list_free>;
using unique_git_note = unique_git<git_note, git_note_free>;
using unique_git_signature = unique_git<git_signature, git_signature_free>;
using unique_git_mailmap = unique_git<git_mailmap, git_mailmap_free>;
using unique_git_worktree = unique_git<git_worktree, git_worktree_free>;

// Structs libgit2 fills in place and disposes of by address; unique_struct
// derives from the struct, so &buf is a git_buf* and buf.ptr just works.
using unique_git_buf = wil::unique_struct<git_buf, decltype(&git_buf_dispose), git_buf_dispose>;
using unique_git_strarray = wil::unique_struct<git_strarray, decltype(&git_strarray_dispose), git_strarray_dispose>;

// WIN32 Whole Environment Block;
using unique_environment_block = wil::unique_any<wchar_t*, decltype(FreeEnvironmentStringsW), FreeEnvironmentStringsW>;




// Registry. There is no TortoiseGit default here any more: the get_value_* /
// set_value_string wrappers that silently read HKCU\Software\TortoiseGit are gone,
// and every caller names its root and subkey (Registry::TortoiseGitKey owns the
// TortoiseGit one). A missing value is an empty optional; the caller's value_or
// is its default, so no default can be a null pointer (CLAUDE.md failure mode 5).
namespace reg {
	/// HKCU\<this> (and HKLM\<this>) is where TortoiseGit keeps its settings.
	/// NUL-terminated by construction, so it can go straight to a Win32 API.
	constexpr auto TortoiseGitKey = L"Software\\TortoiseGit"sv;


	// The stock writers. Each takes the root and subkey explicitly.
	using wil::reg::set_value_string_nothrow;
	using wil::reg::set_value_dword_nothrow;
	using wil::reg::open_unique_key;
	using wil::reg::key_iterator;
	using wil::reg::value_iterator;
	using reg_view_tgit = wil::reg::reg_view_details::reg_view_t<err_returncode_policy>;

	static_assert(std::is_convertible_v<PCWSTR, std::wstring_view>);
	static_assert(std::is_convertible_v<std::wstring, std::wstring_view>);
	static_assert(std::is_convertible_v<std::wstring_view, std::wstring> == false);

	til::expect_no_hres set_value_dword_expected(const HKEY& key, const wil::zwstring_view subkey, const wil::zwstring_view value_name, const uint32_t data) noexcept {
		auto&& ret = ::wil::reg::set_value_nothrow(key, subkey.data(), value_name.data(), data);
		if (ret)
			return std::unexpected{ret};
		return {};
	}
	til::expect_no_hres set_value_string_expected(const HKEY& key, const wil::zwstring_view subkey, const wil::zwstring_view value_name, const std::wstring& data) noexcept {
		auto&& ret = ::wil::reg::set_value_nothrow(key, subkey.data(), value_name.data(), data);
		if (ret)
			return std::unexpected{ret};

		return {};
	}



	/// wil::reg::try_get_value_dword(root, subkey, name), compiled in this module
	/// (see the note at the re-exports). Empty when the value is absent; throws
	/// wil::ResultException on any other failure, exactly as wil's does.
	std::optional<std::uint32_t> try_get_dword(const HKEY& root, const wil::zwstring_view subkey, const wil::zwstring_view name) {
		return try_get_value_dword(root, subkey.data(), name.data());
	}


	bool get_bool(const HKEY& root, const wil::zwstring_view subkey, const wil::zwstring_view name, const bool def) {
		auto&& temp = try_get_dword(root, subkey.data(), name.data());
		return temp ? *temp : def;
	}


	/// wil::reg::try_get_value_string(root, subkey, name), compiled in this module.
	/// Same contract as try_get_dword.
	std::optional<std::wstring> try_get_string(const HKEY& root, const wil::zwstring_view subkey, const wil::zwstring_view name) {
		return try_get_value_string(root, subkey.data(), name.data());
	}



#pragma region TGit shortcuts

	std::optional<std::wstring> try_get_both_string(const wil::zwstring_view subkey, const wil::zwstring_view name) {
		auto value = try_get_string(HKEY_CURRENT_USER, subkey, name);
		if (!value)
			value = try_get_string(HKEY_LOCAL_MACHINE, subkey, name);
		return value;
	}

	std::optional<DWORD> try_get_both_dword(const wil::zwstring_view subkey, const wil::zwstring_view name) {
		auto value = wil::reg::try_get_dword(HKEY_CURRENT_USER, subkey, name);
		if (!value)
			value = wil::reg::try_get_dword(HKEY_LOCAL_MACHINE, subkey, name);

		return value;
	}

	std::optional<std::wstring> try_get_tgit_string(const wil::zwstring_view name) {
		return try_get_both_string(TortoiseGitKey, name);
	}

	std::optional<DWORD> try_get_both_bool(const wil::zwstring_view subkey, const wil::zwstring_view name) {
		return wil::reg::try_get_both_dword(subkey, name);
	}

	std::optional<DWORD> try_get_tgit_dword(const wil::zwstring_view name) {
		return wil::reg::try_get_both_dword(TortoiseGitKey, name);
	}

	std::optional<bool> try_get_tgit_bool(const wil::zwstring_view name) {
		return try_get_tgit_dword(name);
	}

	til::expect_no_hres set_tgit_string(const HKEY& root, const wil::zwstring_view name, const wil::zwstring_view data) {
		auto ret = wil::reg::set_value_nothrow(root, TortoiseGitKey.data(), name.data(), std::wstring{data});
		if (ret)
			return std::unexpected{ret};
		return {};
	}
	auto set_tgit_user_string(const wil::zwstring_view name, const wil::zwstring_view data) {
		return set_tgit_string(HKEY_CURRENT_USER, name, data);
	}
	auto set_tgit_machine_string(const wil::zwstring_view name, const wil::zwstring_view data) {
		return set_tgit_string(HKEY_LOCAL_MACHINE, name, data);
	}

	til::expect_no_hres set_tgit_dword(const HKEY& root, const std::wstring& name, const DWORD data) {
		HRESULT ret = wil::reg::set_value_nothrow(root, TortoiseGitKey.data(), name.data(), data);
		return til::expect_no_hres(std::unexpect, ret);
	}
	auto set_tgit_user_dword(const wil::zwstring_view name, const DWORD data) {
		return set_tgit_dword(HKEY_CURRENT_USER, name, data);
	}
	auto set_tgit_machine_dword(const wil::zwstring_view name, const DWORD data) {
		return set_tgit_dword(HKEY_LOCAL_MACHINE, name, data);
	}

	til::expect_no_hres delete_tgit_value(const wil::zwstring_view name) {
		{
			const auto regview = reg_view_tgit{HKEY_CURRENT_USER};
			const auto maybe_user = regview.try_get_value<PCWSTR>(TortoiseGitKey.data(), name.data());
			if (maybe_user.has_value()) {
				auto ret = regview.delete_value(TortoiseGitKey.data(), name.data());
				if (ret != 0)
					return std::unexpected{ret};
			}
		}

		{
			const auto regview = reg_view_tgit{HKEY_LOCAL_MACHINE};
			const auto maybe_user = regview.try_get_value<PCWSTR>(TortoiseGitKey.data(), name.data());
			if (maybe_user.has_value()) {
				auto ret = regview.delete_value(TortoiseGitKey.data(), name.data());
				if (ret != 0)
					return std::unexpected{ret};
			}
		}

		return {};
	}


	// std::vector<std::wstring> getValues(const HKEY& m_base, std::wstring sub) {
	// 	auto ret = std::vector<std::wstring>{};
	// 	auto k = wil::reg::open_unique_key(m_base, sub.c_str());
	// 	for (const auto& val : wil::make_range(wil::reg::value_iterator{k}, wil::reg::value_iterator{}))
	// 	{
	// 		// val is wil::reg::value_iterator_data<std::wstring>
	// 		const std::wstring& name = val.name; // Value name (sized automatically)
	// 		const DWORD type = val.type;         // e.g., REG_SZ, REG_DWORD
	//
	// 		if (type == REG_SZ)
	// 		{
	// 			auto data = wil::reg::try_get_value_string(hKey, name.c_str());
	// 		}
	// 	}
	// 	{
	//
	// 	}
	// }
	//
	// bool CRegistryKey::getValues(std::vector<reg_string_t>& values)
	// {
	// 	values.clear();
	//
	// 	auto k = wil::reg::open_unique_key(m_base, m_path.c_str());
	// 	for (const auto& [value, type] : wil::make_range(wil::reg::value_iterator{k.get()}, {})) {
	// 		values.emplace_back(value);
	// 	}
	//
	// 	return !values.empty();
	// }
#pragma endregion


}


/// A most-recently-used list of strings (commit messages, log ranges), kept as
/// one REG_MULTI_SZ value under HKEY_CURRENT_USER. It replaces Utils\RegHistory.h,
/// which stored one REG_SZ value per entry (<name>0, <name>1, ...) and wrote them
/// one at a time. Load still reads that layout when the REG_MULTI_SZ value is
/// absent, and Save removes it, so an upgrade keeps the user's history.
///
/// The bodies are compiled here, not in the importer, for the same reason as the
/// reg wrappers above. A registry failure is not an error to the caller: an
/// unreadable history loads empty, and a failed write makes Save return false.
class CRegHistory {
	static constexpr auto DEFAULT_HISTORY_DEPTH = 25;
	std::wstring m_subkey;
	std::wstring m_name;
	std::vector<std::wstring> m_entries;
	size_t m_maxEntries = DEFAULT_HISTORY_DEPTH;


	void Truncate() {
		if (m_entries.size() > m_maxEntries)
			m_entries.resize(m_maxEntries);
	}


	[[nodiscard]] std::wstring LegacyValueName(const size_t n) const {
		return std::format(L"{}{}", m_name, n);
	}

public:
	/// Reads the history stored as value \a name under HKCU\ \a subkey, e.g.
	/// ("Software\\TortoiseGit\\History\\merge", "logmsgs"). With an empty
	/// argument this does nothing, and the history stays unbound (Save fails).
	void Load() {
		m_entries.clear();

		const HRESULT hr = ResultFromException([&] {
			// ReSharper disable once CppLocalVariableMayBeConst
			if (auto entries =
				wil::reg::try_get_value_multistring(HKEY_CURRENT_USER, m_subkey.c_str(), m_name.c_str())) {
				m_entries = std::move(*entries);
				return;
			}
			for (size_t n = 0; n < m_maxEntries; ++n) {
				auto entry = wil::reg::try_get_value_string(
					HKEY_CURRENT_USER,
					m_subkey.c_str(),
					LegacyValueName(n).c_str()
				);
				if (!entry || entry->empty())
					break;
				m_entries.push_back(std::move(*entry));
			}
		});
		if (FAILED(hr))
			m_entries.clear();
		// wil reads a REG_MULTI_SZ holding no strings back as one empty string.
		std::erase_if(m_entries, [](const std::wstring& entry) { return entry.empty(); });
		Truncate();
	}
	void Load(std::wstring&& subkey, std::wstring&& name) {
		if (subkey.empty() || name.empty())
			return;
		m_subkey = std::move(subkey);
		m_name = std::move(name);
		this->Load();
	}


	/// Writes the history in one call, then removes the legacy per-entry values.
	[[nodiscard]] bool Save() const {
		if (m_subkey.empty())
			return false;
		const HRESULT hr = ResultFromException([&] {
			wil::reg::set_value_multistring(HKEY_CURRENT_USER, m_subkey.c_str(), m_name.c_str(), m_entries);
		});
		if (FAILED(hr))
			return false;
		// The legacy values run contiguously from <name>0. Once they are gone this is one failing call per Save.
		constexpr auto ARBITRARY_ROOF = 100;
		for (size_t n = 0; n < ARBITRARY_ROOF; ++n) {
			auto&& ret = ::RegDeleteKeyValueW(HKEY_CURRENT_USER, m_subkey.c_str(), LegacyValueName(n).c_str());
			if (ret != ERROR_SUCCESS)
				break;
		}
		return true;
	}


	/// Moves \a text to the front of the list, adding it if it is not there yet.
	/// Reloads first, so an entry another dialog saved in the meantime is kept.
	/// Returns true when \a text was new.
	bool AddEntry(const wil::zwstring_view text) {
		if (text.empty())
			return false;
		if (!m_subkey.empty())
			Load();

		const auto existing = std::ranges::find(m_entries, text);
		const bool added = existing == m_entries.end();
		if (!added)
			m_entries.erase(existing);
		m_entries.emplace(m_entries.begin(), text);
		Truncate();
		return added;
	}


	void RemoveEntry(const size_t pos) {
		if (pos < m_entries.size())
			m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(pos));
	}


	/// The most entries kept. The default is 25.
	void SetMaxHistoryItems(const size_t maxEntries) {
		m_maxEntries = maxEntries;
		Truncate();
	}


	[[nodiscard]] size_t GetCount() const { return m_entries.size(); }
	[[nodiscard]] bool IsEmpty() const { return m_entries.empty(); }
	[[nodiscard]] const std::wstring& GetEntry(const size_t pos) const { return m_entries[pos]; }
};



/// open_clipboard, retried: OpenClipboard fails while another process holds the
/// clipboard, which it usually does only briefly. Up to 8 tries, 1 ms doubling,
/// at most 127 ms in all. Test the result with `if (!clipboard)`; keep it in a
/// variable scoped to the whole operation, like an if-init-statement testing
/// for validity, or the it closes before anything is written to it.
unique_close_clipboard_call open_clipboard_retrying(const HWND& owner) {
	constexpr auto NUM_RETRY_CALLS = 8;
	for (auto attempt = 0U; attempt < NUM_RETRY_CALLS; ++attempt) {
		if (attempt > 0)
			::Sleep(1U << (attempt - 1));
		if (auto clipboard = open_clipboard(owner); clipboard)
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
unique_hglobal make_moveable_hglobal(const std::span<const std::byte> bytes) {
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


/// The text plus a terminating NUL, which is what every text clipboard
/// format expects. The NUL is written here, so any view will do.
unique_hglobal make_moveable_hglobal(const wil::zwstring_view text) {
	return tgit_details::make_moveable_text(text);
}


unique_hglobal make_moveable_hglobal(const std::string_view text) {
	return tgit_details::make_moveable_text(text);
}


/// Puts \a text on the clipboard as CF_UNICODETEXT; the OS synthesizes CF_TEXT
/// from it. What StringUtils::WriteAsciiStringToClipboard(const CStringW&, HWND)
/// did, without the CString. A CString caller bridges visibly with a view.
///
/// The clipboard is one lock for the whole session: while it is open, every
/// other process's OpenClipboard fails. So everything that can fail or throw
/// (the allocation and copy) happens before it is opened, and EmptyClipboard
/// sits right beside SetClipboardData - a failure leaves the user's clipboard
/// as it was, and never leaves it locked.
bool SetClipboardText(const wil::zwstring_view text, const HWND& owner = nullptr) {
	auto block = make_moveable_hglobal(text);
	if (!block)
		return false;

	const auto clipboard = open_clipboard_retrying(owner);
	if (!clipboard)
		return false;
	if (!::EmptyClipboard() || !::SetClipboardData(CF_UNICODETEXT, block.get()))
		return false;
	block.release(); // the clipboard owns it now
	return true;
}


/// The known folder's path. Throws std::runtime_error (not wil::ResultException)
/// when SHGetKnownFolderPath fails: no caller catches either, and changing the
/// type is a decision of its own, not part of trimming this module.
fs::path GetKnownFolderPath(
	REFKNOWNFOLDERID folderId,
	const KNOWN_FOLDER_FLAG flags = KF_FLAG_DEFAULT,
	const std::optional<HANDLE> token = std::nullopt,
	const sloc_t loc = sloc_t::current()
) {
	wil::unique_cotaskmem_string result{};
	auto&& ret = ::SHGetKnownFolderPath(folderId, flags, token.value_or(nullptr), result.put());
	til::THROW_LIVE_ERROR_IF(ret != S_OK, "SHGetKnownFolderPath failed", loc);
	return fs::path{ result.get() };
}



/// wil::TryGetEnvironmentVariableW, compiled in this module (see the note at the
/// re-exports). Empty when the variable is not set; throws wil::ResultException on
/// any other failure.
std::optional<std::wstring> TryGetEnvironmentVariable(const PCWSTR name) {
	const auto value = wil::TryGetEnvironmentVariableW(name);
	if (!value)
		return std::nullopt;
	return std::wstring{ value.get() };
}


class CFormatMessageWrapper {
	DWORD err_id = {};
	size_t length = {};
	std::wstring message;

public:
	/// The Dark Art incantation that has lived for over 30 years. There is no
	/// two-call sizing here: with FORMAT_MESSAGE_ALLOCATE_BUFFER, lpBuffer is an
	/// LPWSTR* that FormatMessage fills with a LocalAlloc'd block. Treated as a
	/// two-call API, the sizing call failed (0) and the second wrote a pointer
	/// into a zero-length buffer, then returned more than resize_and_overwrite gave.
	explicit CFormatMessageWrapper(const std::optional<DWORD> maybe_err_id = std::nullopt)
	: err_id(maybe_err_id ? *maybe_err_id : ::GetLastError())
	{
		wil::unique_hlocal_string buffer;
		length = size_t_cast(::FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			err_id,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			reinterpret_cast<LPWSTR>(buffer.put()),
			0,
			nullptr
		));
		if (length)
			message.assign(buffer.get(), length);
	}

	explicit operator bool() const { return length != 0; }
	[[nodiscard]] auto code() const { return err_id; }
	[[nodiscard]] const auto& get() const { return message; }
	[[nodiscard]] auto empty() const { return length == 0; }
	[[nodiscard]] auto c_str() const { return message.c_str(); }
};


/// The one thing CAutoRepository did that is not ownership: TGitCache and the
/// shell extension never initialize gitdll, so opening a repository here is the
/// only place their object format is ever established. Check the result with
/// is_valid(); on failure it is empty and git_error_last() says why.
///
/// UTF-8 only, deliberately: wil cannot see CUnicodeUtils. A caller holding a
/// wide path goes through the one wide overload that encodes with
/// CUnicodeUtils::StdGetUTF8, TGitH::OpenGitRepository(std::wstring_view) in
/// git_h - never its own conversion (CStringA(path) would be the ACP, not UTF-8).
unique_git_repository OpenGitRepository(const char* const gitDirUtf8) {
	unique_git_repository repo;
	if (!git_repository_open(repo.put(), gitDirUtf8))
		tgit::invarients::LatchObjectFormat(git_repository_oid_type(repo.get()));
	return repo;
}


// ---- POSIX quoting: NOT exported ------------------------------------------------
// Moved verbatim from str_utils, where they were parked unexported. They are git
// domain - the msys2/cygwin path writes the command line into a temp file for
// bash.exe - and sit here beside SerializeArgv, whose msys2/cygwin branch calls them.

/**
 * Quotes one argv element for a POSIX shell, which is what the msys2/cygwin
 * path needs: CGit writes the whole command line into a temp file and hands
 * it to bash.exe, so there the "command line" is shell *source*.
 */
std::wstring EscapePosixShellArgument(const wil::zwstring_view argument) {
	// Single quotes suppress every expansion bash performs, and the only thing
	// that cannot appear between them is a single quote - so close, escape it
	// outside, and reopen.
	std::wstring result;
	result.reserve(argument.size() + 2);
	result += L'\'';
	for (const wchar_t c : argument) {
		if (c == L'\'')
			result += L"'\\''";
		else
			result += c;
	}
	result += L'\'';

	return result;
}

bool isextra_posix_char(const wchar_t c) {
	return (c == L'_' || c == L'-' || c == L'.' || c == L'/' || c == L'=' || c == L':' || c == L',' || c == L'+');
}

/**
 * True unless \a argument is made entirely of characters no shell touches.
 * Deliberately a whitelist: a blacklist of bash metacharacters is a list you
 * are always one shell feature behind on.
 */
bool NeedsPosixShellQuoting(const wil::zwstring_view str) {
	if (str.empty())
		return true;

	// A character is plain when it is an ASCII letter or digit OR one of the
	// extras. ASCII spelled out, not std::isalnum: that takes an int in the
	// unsigned char range, and a wchar_t above 255 is undefined behaviour.
	const auto is_plain = [](const wchar_t c) {
		return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || isextra_posix_char(c);
	};
	return !std::ranges::all_of(str, is_plain);
}



/// Short-lived: owns the UTF-8 copies plus the null-terminated pointer array.
class Utf8Argv {
	std::vector<std::string> _s;
	std::vector<const char*> _p;

public:
	explicit Utf8Argv(std::span<const std::wstring> a)
	: _s{ std::from_range, std::views::transform(a, til::WStringToUTF8) } {
		// reserve, not _p(n): vector(n) is n nullptrs, and push_back would land after them
		_p.reserve(_s.size() + 1);
		for (auto& s : _s)
			_p.push_back(s.c_str());
		_p.push_back(nullptr);
	}

	[[nodiscard]] int argc() const { return gsl::narrow<int>(_s.size()); }
	[[nodiscard]] const char** argv() noexcept { return _p.data(); }
};



/// A C-style (argc, argv) over strings it owns, for gitdll's git_open_log /
/// git_open_diff, which take `int argc, const char** argv` as git's own main does.
/// gitdll copies what it keeps (dup_argv in gitdll.c), so an ArgvData only has to
/// outlive the open call - it is never handed over and never freed by gitdll.
///
/// argv points into the std::strings in _args. Moving is safe - moving a
/// std::vector hands over its buffer, so the std::string objects (and any SSO
/// characters inside them) stay where they are - but copying is not, since a
/// copy's pointers would still point into the original. Hence move-only.
class ArgvData {
	std::vector<std::wstring> _args{};

public:
	ArgvData() = default;

	ArgvData(const std::initializer_list<std::wstring> args) : _args{ args } {}

	/// For an argv assembled conditionally before the call. Implicit, like the list
	/// constructors, so a vector reaches every `const ArgvData&` parameter; a braced
	/// list still picks the initializer_list constructor, which list-init prefers.
	// ReSharper disable once CppNonExplicitConvertingConstructor cppcoreguidelines-rvalue-reference-param-not-moved
	ArgvData(const std::vector<std::wstring>& args) : _args{ args } {} // NOLINT(*-explicit-constructor)
	ArgvData(std::vector<std::wstring>&& args) : _args(::std::move(args)) {} // NOLINT(*-explicit-constructor)
	ArgvData(std::vector<std::wstring>&& args, bool diff) : _args(args) {} // NOLINT(*-explicit-constructor)

	ArgvData(const std::initializer_list<std::string> args) :
		_args{ std::from_range, std::views::transform(args, til::UTF8ToWString) } {}


	// keep a std::string_view / const char* ctor that converts with StdGetUTF16 / MultiByteToWideChar if callers need it

	ArgvData& operator<<(const wil::zwstring_view arg) {
		_args.emplace_back(arg);
		return *this;
	}

	ArgvData& push_range(const std::initializer_list<std::wstring> args) {
		_args.append_range(args);
		return *this;
	}

	ArgvData& operator<<(const std::vector<std::wstring>& args) {
		_args.append_range(args);
		return *this;
	}

	[[deprecated("I don't like this name and shape, use <<")]]
	ArgvData& emplace_back(const wil::zwstring_view arg) {
		_args.emplace_back(arg);
		return *this;
	}

	[[nodiscard]] bool operator!() const noexcept { return _args.empty(); }
	[[nodiscard]] bool empty() const noexcept { return _args.empty(); }
	[[nodiscard]] const auto& args() const noexcept { return _args; }
	[[nodiscard]] std::wstring to_cmd() const { return wil::ArgvToCommandLine(_args); }
	[[nodiscard]] Utf8Argv utf8() const { return Utf8Argv{ _args }; } // for gitdll

	/**
	 * Two serializations, because RunAsync has two backends. Native git.exe gets
	 * MSDN's backslash/quote rules, read back by CommandLineToArgvW. For
	 * msys2/cygwin the line is written to a temp file and handed to bash.exe, so
	 * there it is shell *source* and gets POSIX single-quoting instead.
	 *
	 * Elements are quoted only when quoting changes the reading. That is not an
	 * optimization: RunAsync decides whether to prepend ms_LastMsysGitDir by
	 * testing whether the line starts with "git", so a serializer that quoted
	 * argv[0] unconditionally would silently resolve git through PATH instead.
	 */
	[[nodiscard]] auto SerializeArgv(const bool is_weird) const {
		if (!is_weird)
			return to_cmd();

		std::wstring cmd;
		for (const auto& arg : _args) {
			if (!cmd.empty())
				cmd += L' ';
			cmd += NeedsPosixShellQuoting(arg) ? EscapePosixShellArgument(arg) : arg;
		}
		return cmd;
	}
};



/// What sanity.h's SCOPE_EXIT expands against: `scope_exit_helper() << [&] { ... };`.
/// Global, because the macro names it unqualified. The guard is wil's: its move
/// disarms the source, which tgittypes' hand-rolled scope_exit_t did not (a moved-
/// from guard still ran its lambda). This is the only definition: a TU that
/// expands SCOPE_EXIT imports wil.
struct scope_guard {
	template <class F>
	[[nodiscard]] auto operator<<(F&& f) const {
		return wil::scope_exit(std::forward<F>(f));
	}
};

fs::path GetModulePath(const HMODULE module = {} /* current process module */) {
	const auto ret = ::wil::GetModuleFileNameW(module);
	return ret.get();
}


/// GetResourceView<WORD>(...) -> std::span<const WORD>; empty when the resource is missing.
/// Nothing to release: the span views the mapped image and stays valid while `module`
/// is loaded (UnlockResource is ((h), 0); FreeResource is a 16-bit leftover).
template <typename DataType>
	requires std::is_trivially_copyable_v<DataType>
auto GetResourceView(const HMODULE module, const LPCWSTR type, const LPCWSTR name, const std::optional<LANGID> language = {}) {
	using Elem = std::remove_const_t<DataType>;

	const HRSRC info = language
		? ::FindResourceExW(module, type, name, *language)
		: ::FindResourceW(module, name, type);
	const HGLOBAL data = info ? ::LoadResource(module, info) : nullptr;
	const auto* ptr = static_cast<const Elem*>(data ? ::LockResource(data) : nullptr);
	if (!ptr)
		return std::span<const Elem>{};
	return std::span<const Elem>{ ptr, ::SizeofResource(module, info) / sizeof(Elem) };
}


auto LoadIconAuto(const HINSTANCE& hInstance, const LPCWSTR& lpIconName, int iconWidth, int iconHeight) {
	// the docs for LoadIconWithScaleDown don't mention that a size of 0 will
	// use the default system icon size like for e.g. LoadImage or LoadIcon.
	// So we don't assume that this works but do it ourselves.
	if (iconWidth == 0)
		iconWidth = GetSystemMetrics(SM_CXSMICON);
	if (iconHeight == 0)
		iconHeight = GetSystemMetrics(SM_CYSMICON);
	HICON hIcon = nullptr;
	if (FAILED(LoadIconWithScaleDown(hInstance, lpIconName, iconWidth, iconHeight, &hIcon))) {
		// fallback, just in case
		hIcon = static_cast<HICON>(LoadImage(hInstance, lpIconName, IMAGE_ICON, iconWidth, iconHeight, LR_DEFAULTCOLOR));
	}
	return wil::unique_hicon{hIcon};
}


HICON LoadIconEx(const HINSTANCE& hInstance, const LPCWSTR& lpIconName) {
	HICON hIcon = nullptr;
	if (FAILED(LoadIconWithScaleDown(hInstance, lpIconName, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), &hIcon))) {
		// fallback, just in case
		hIcon = LoadIcon(hInstance, lpIconName);
	}
	return hIcon;
}


HICON LoadIconEx(const HINSTANCE& hInstance, const LPCWSTR& lpIconName, int iconWidth, int iconHeight) {
	return LoadIconAuto(hInstance, lpIconName, iconWidth, iconHeight).release();
}


union ResourcePtr {
	const wchar_t*		wstring = nullptr;
	const WORD*			words;
	const std::byte*	bytes;
};


void EatRibbon(const std::byte* res) {

}


static_assert(!std::is_assignable_v<const std::byte*, ResourcePtr>);
static_assert(!std::invocable<decltype(EatRibbon), ResourcePtr>);
static_assert(std::invocable<decltype(EatRibbon), decltype(ResourcePtr::bytes)>);
static_assert(!std::invocable<decltype(EatRibbon), decltype(ResourcePtr::words)>);
static_assert(!std::invocable<decltype(EatRibbon), decltype(ResourcePtr::wstring)>);

}

export namespace til {

constexpr auto FailCode = static_cast<DWORD>(-1);

std::wstring GetOSVanityString() {
	constexpr auto subkey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"sv;

	auto product = wil::reg::try_get_string(HKEY_LOCAL_MACHINE, subkey, L"ProductName").value_or(L"Windows"s); // e.g., "Windows 10 Pro"
	auto display = wil::reg::try_get_string(HKEY_LOCAL_MACHINE, subkey, L"DisplayVersion").value_or(L"11"s); // e.g., "24H2"
	auto build = wil::reg::try_get_string(HKEY_LOCAL_MACHINE, subkey, L"CurrentBuild").value_or(L"Legacy"s); // e.g., "26100"
	DWORD ubr = wil::reg::try_get_dword(HKEY_LOCAL_MACHINE, subkey, L"UBR").value_or(0);

	// Optional: Fix the Win 10 lie without touching NTDLL
	if (!build.empty() && std::wcstol(build.c_str(), nullptr, 10) >= 22000) {
		if (auto pos = product.find(L"Windows 10"); pos != std::wstring::npos) {
			product.replace(pos, 10, L"Windows 11");
		}
	}

	return std::format(L"{} {} (Build {}.{})", product, display, build, ubr);
}

/**
 * Returns the path to the installation folder, in our case the TortoiseSVN/bin folder.
 * \remark the path returned has a trailing backslash
 */
fs::path GetAppDirectory(const HMODULE hMod = {})
{
	const auto path = wil::GetModulePath(hMod);
	return path.parent_path();
}


}
