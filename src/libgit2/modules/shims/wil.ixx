module;
#include <wil/win32_helpers.h>
#include <wil/resource.h>
#include <wil/filesystem.h>
#include <wil/registry.h>
#include <winrt/base.h>
#include <ShlObj.h>			// SHGetKnownFolderPath, FOLDERID_*, REFKNOWNFOLDERID
#include <git2.h>			// the libgit2 owners and OpenGitRepository

export module wil;
import std;
import gsl;
import invarients;			// LatchObjectFormat; invarients imports only std, so this stays acyclic

using std::operator""s;
using std::operator""sv;
namespace fs = std::filesystem;

// Not exported: the body both text overloads of make_moveable_hglobal share.
namespace wil::tgit_details {
	/// A GMEM_MOVEABLE block holding \a text and a terminating NUL that this writes
	/// itself. The view need not be NUL-terminated - the previous version copied
	/// text.size() + 1 characters, reading one past the end of every view that was
	/// not.
	template <class CharT>
	unique_hglobal make_moveable_text(const std::basic_string_view<CharT> text) {
		const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
		unique_hglobal block{ ::GlobalAlloc(GMEM_MOVEABLE, bytes.size() + sizeof(CharT)) };
		if (!block)
			return {};
		auto* const target = static_cast<std::byte*>(::GlobalLock(block.get()));
		if (!target)
			return {};
		std::ranges::fill_n(std::ranges::copy(bytes, target).out, sizeof(CharT), std::byte{ 0 });
		::GlobalUnlock(block.get());
		return block;
	}
}

export namespace wil {
namespace KnownFolders {
	inline const auto RoamingAppData = FOLDERID_RoamingAppData;
	inline const auto LocalAppData = FOLDERID_LocalAppData;
	inline const auto Documents = FOLDERID_Documents;
	inline const auto ProgramFiles = FOLDERID_ProgramFiles;
}



namespace KnownEnvVars {
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
	explicit ThrowIfError(const expect_nothing<E>& r) {
		[[maybe_unused]]r.value();
	};
};



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
using wil::unique_handle;
using wil::unique_struct;
using wil::scope_exit;
// NOT re-exported: wil::TryGetEnvironmentVariableW, wil::reg::try_get_value_dword,
// wil::reg::try_get_value_string. They are inline in wil's headers and their
// failure-reporting path calls strsafe.h's `static` functions, which cannot cross
// a module boundary: an importer without a textual <strsafe.h> fails with C2129
// ("static function 'StringValidateDestW' declared but not defined"). Use the
// wrappers below instead (TryGetEnvironmentVariable, reg::try_get_dword,
// reg::try_get_string), whose bodies are compiled once, here - the same reason
// CreateFileUnique exists.


// Clipboard - what CClipboardHelper was
using wil::unique_close_clipboard_call;
using wil::open_clipboard;
using wil::unique_hglobal;
using wil::open_file;
using wil::GetFileInfo;
using wil::ResultException;


namespace winrt {
	using ::winrt::clock;
	using ::winrt::hresult;
	using ::winrt::impl::error_ok;
	using ::winrt::to_string;
	using ::winrt::to_hstring;
}



// Registry Wrapper
namespace reg {
	using wil::reg::reg_view_details::reg_view;
	// The stock writers, for call sites that leave the wrappers below. Each takes
	// the root and subkey explicitly, so nothing defaults to HKCU.
	using wil::reg::set_value_string_nothrow;
	using wil::reg::set_value_dword_nothrow;


	/// wil::reg::try_get_value_dword(root, subkey, name), compiled in this module
	/// (see the note at the re-exports). Empty when the value is absent; throws
	/// wil::ResultException on any other failure, exactly as wil's does. The root
	/// and subkey are the caller's - nothing here defaults to HKCU\Software\TortoiseGit.
	std::optional<std::uint32_t> try_get_dword(RKEY* const root, const PCWSTR subkey, const PCWSTR name) {
		return wil::reg::try_get_value_dword(root, subkey, name);
	}


	/// wil::reg::try_get_value_string(root, subkey, name), compiled in this module.
	/// Same contract as try_get_dword.
	std::optional<std::wstring> try_get_string(RKEY* const root, const PCWSTR subkey, const PCWSTR name) {
		return wil::reg::try_get_value_string(root, subkey, name);
	}


	constexpr auto SOFTWARE_TGIT_KEY = L"Software\\TortoiseGit"sv;



	namespace hkey {

		// ReSharper disable CppRedundantCastExpression
		const auto ClassesRoot = reinterpret_cast<HKEY>(HKEY_CLASSES_ROOT);
		const auto CurrentUser = reinterpret_cast<HKEY>(HKEY_CURRENT_USER);
		const auto LocalMachine = reinterpret_cast<HKEY>(HKEY_LOCAL_MACHINE);
		const auto CurrentUserLocalSettings = reinterpret_cast<HKEY>(HKEY_CURRENT_USER_LOCAL_SETTINGS);
		// ReSharper enable CppRedundantCastExpression
	}



	template <typename value_type>
	value_type get_value_typed(
		const std::wstring_view key,
		const std::optional<const value_type> o_def = std::nullopt,
		const std::optional<RKEY* const> o_root = std::nullopt,
		const std::optional<std::wstring_view> o_sub = std::nullopt
	) {
		auto root = o_root.value_or(hkey::CurrentUser);
		auto sub = o_sub.value_or(SOFTWARE_TGIT_KEY);
		auto tmp = wil::reg::try_get_value<value_type>(root, sub.data(), key.data());
		return tmp.value_or(o_def.value_or({}));
	}


	template <>
	bool get_value_typed(
		const std::wstring_view key,
		const std::optional<const bool> o_def,
		const std::optional<RKEY* const> o_root,
		const std::optional<std::wstring_view> o_sub
	) {
		return get_value_typed<DWORD>(key, o_def, o_root, o_sub) != 0;
	}


	static_assert(std::is_convertible_v<std::wstring, const std::wstring_view&>);

	// `using` cannot alias a function template's specialization; thin wrappers can.
	auto get_value_dword(
		const std::wstring_view key,
		const std::optional<DWORD> o_def = std::nullopt,
		const std::optional<RKEY* const> o_root = std::nullopt,
		const std::optional<std::wstring_view> o_sub = std::nullopt
	) {
		return get_value_typed<DWORD>(key, o_def, o_root, o_sub);
	}


	/// CRegString's contract: the value, else the default, else empty - never UB.
	/// It used to be std::wstring{ get_value_typed<LPCWCHAR>(...) }: a missing value
	/// with no default is a nullptr there, and std::wstring(nullptr) is undefined
	/// (CLAUDE.md failure mode 5) - it crashed g_Git's constructor, via
	/// GetConfiguredSshClient(), on any machine without an "SSH" value. Read owned,
	/// so no pointer into a buffer that has gone out of scope is ever involved.
	std::wstring get_value_string(
		const std::wstring_view key,
		const std::optional<const wchar_t* const> o_def = std::nullopt,
		const std::optional<RKEY* const> o_root = std::nullopt,
		// ReSharper disable once CppPassValueParameterByConstReference
		const std::optional<std::wstring_view> o_sub = std::nullopt
	) {
		const auto root = o_root.value_or(hkey::CurrentUser);
		// views need not be NUL-terminated; the API takes C strings
		const std::wstring sub(o_sub.value_or(SOFTWARE_TGIT_KEY));
		const std::wstring name(key);
		if (auto value = wil::reg::try_get_value_string(root, sub.c_str(), name.c_str()))
			return std::move(*value);
		const wchar_t* const def = o_def.value_or(nullptr);
		return def ? std::wstring(def) : std::wstring();
	}


	/// The writing twin of get_value_string. Best effort, as CRegString's write()
	/// was: the HRESULT is there for a caller that cares.
	inline expect_no_hres set_value_string(
		const std::wstring_view key,
		const std::wstring_view value,
		RKEY* const root = hkey::CurrentUser,
		const std::wstring_view sub = SOFTWARE_TGIT_KEY
	) {
		const auto ret = wil::reg::set_value_string_nothrow(root, sub.data(), key.data(), value.data());
		if (ret != winrt::error_ok) {
			return std::unexpected(ret);
		}
		else {
			return {};
		}
	}
}



/// open_clipboard, retried: OpenClipboard fails while another process holds the
/// clipboard, which it usually does only briefly. Up to 8 tries, 1 ms doubling,
/// at most 127 ms in all. Test the result with `if (!clipboard)`; keep it in a
/// variable scoped to the whole operation, not in an if-init-statement, or the
/// clipboard closes again before anything is written to it.
unique_close_clipboard_call open_clipboard_retrying(RWND* const owner) {
	for (int attempt = 0; attempt < 8; ++attempt) {
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
unique_hglobal make_moveable_hglobal(const std::wstring_view text) {
	return tgit_details::make_moveable_text(text);
}


unique_hglobal make_moveable_hglobal(const std::string_view text) {
	return tgit_details::make_moveable_text(text);
}


/// Puts \a text on the clipboard as CF_UNICODETEXT; the OS synthesizes CF_TEXT
/// from it. What StringUtils::WriteAsciiStringToClipboard(const CStringW&, RWND*)
/// did, without the CString. A CString caller bridges visibly with a view.
bool SetClipboardText(const std::wstring_view text, RWND* const owner = nullptr) {
	// Not an if-init-statement: the clipboard has to stay open until we return.
	const auto clipboard = open_clipboard_retrying(owner);
	if (!clipboard)
		return false;

	::EmptyClipboard();
	auto block = make_moveable_hglobal(text);
	if (!block || !::SetClipboardData(CF_UNICODETEXT, block.get()))
		return false;
	block.release(); // the clipboard owns it now
	return true;
}


// Win32
using KnownFolderCLSID = KNOWNFOLDERID;
using RefKnownFolderCLSID = REFKNOWNFOLDERID;
using KnownFolderFlag = KNOWN_FOLDER_FLAG;


auto GetKnownFolderPath(
	RefKnownFolderCLSID folderId,
	const KnownFolderFlag flags = KF_FLAG_DEFAULT,
	RHANDLE* const token = {}
) {
	wil::unique_cotaskmem_string result{};
	if (::SHGetKnownFolderPath(folderId, flags, token, result.put()) != S_OK) {
		const auto hstring = winrt::to_hstring(folderId);
		throw std::runtime_error{ "SHGetKnownFolderPath failed for " + winrt::to_string(hstring) };
	}
	return fs::path{ result.get() };
}



auto GetEnvVar(const KnownEnvVar which) {
	return fs::path{ wil::GetEnvironmentVariableW(which.data()).get() };
}


/// wil::TryGetEnvironmentVariableW, compiled in this module (see the note at the
/// re-exports). Empty when the variable is not set - unlike GetEnvVar above, which
/// throws then; throws wil::ResultException on any other failure.
std::optional<std::wstring> TryGetEnvironmentVariable(const PCWSTR name) {
	const auto value = wil::TryGetEnvironmentVariableW(name);
	if (!value)
		return std::nullopt;
	return std::wstring{ value.get() };
}


using CAutoGeneralHandle = wil::unique_handle;
using CAutoFile = wil::unique_file;
using CAutoFILE = wil::unique_file;

// Concrete parameter types: an abbreviated template's `auto p = nullptr` default
// can never be deduced, so every call that omitted a trailing argument failed.
CAutoGeneralHandle CreateFileUnique(
	const LPCWSTR lpFileName,
	const DWORD dwDesiredAccess,
	const DWORD dwShareMode,
	const DWORD dwCreationDisposition,
	const DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
	SECURITY_ATTRIBUTES* const lpSecurityAttributes = {},
	RHANDLE* const hTemplateFile = {}
) {
	CAutoGeneralHandle fileHandle{ CreateFileW(
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
	[[nodiscard]] constexpr bool IsWellFormedUtf16(const std::wstring_view s) noexcept {
		for (size_t i = 0; i < s.size(); ++i) {
			if (IS_LOW_SURROGATE(s[i]))
				return false; // low without a high
			if (IS_HIGH_SURROGATE(s[i]) && !(++i < s.size() && IS_LOW_SURROGATE(s[i])))
				return false; // high without a low
		}
		return true;
	}


	static_assert(IsWellFormedUtf16(L"a\xD83D\xDE00z")); // valid pair
	// ReSharper disable CppStaticAssertFailure
	static_assert(!IsWellFormedUtf16(L"a\xD83D")); // trailing high
	static_assert(!IsWellFormedUtf16(L"\xDE00z")); // lone low
	// ReSharper restore CppStaticAssertFailure

	enum class CopyError { embedded_null, lone_surrogate, buffer_too_small };



	/// Copies `source` into `dest` whole, NUL-terminated, as well-formed UTF-16, or not at all.
	/// On failure `dest` holds "" (when it has room), never a partial or stale string.
	[[nodiscard]] expect_nothing<CopyError>
	CopyTerminated(const std::span<wchar_t> dest, const std::wstring_view source) noexcept {
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


	std::wstring_view get_string_resource(const std::optional<wil::RINSTANCE*> oInstance, const UINT uID,
	                                      const WORD wLanguage = LANG_USER_DEFAULT) {
		const wchar_t* pCurrentStr;
		{
			const auto hInstance = oInstance.value_or(nullptr);
			HRSRC hResInfo = FindResourceEx(hInstance, RT_STRING, wil::UIDToResID(uID), wLanguage);
			if (!hResInfo) {
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
		for (auto i = 0u; i < stringIndexWithinBlock; ++i) {
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


	size_t LoadStringEx(wil::RINSTANCE* const hInstance, const UINT uID, wchar_t* const lpBuffer_, const int nBufferMax,
	                    const WORD wLanguage) {
		if (lpBuffer_ == nullptr || nBufferMax <= 0)
			return 0;
		lpBuffer_[0] = L'\0';

		auto&& lpBuffer = std::span{ lpBuffer_, gsl::narrow<size_t>(nBufferMax) };
		auto&& resView = get_string_resource(hInstance, uID, wLanguage);
		auto&& r = CopyTerminated(lpBuffer, resView);
		if (!r)
			return 0; // ThrowIfError{r};

		return resView.size();
	}
}



class CFormatMessageWrapper {
	DWORD err_id = {};
	unique_hlocal_string buffer; // FORMAT_MESSAGE_ALLOCATE_BUFFER hands back LocalAlloc'ed memory
	DWORD length = {};

public:
	explicit CFormatMessageWrapper(const std::optional<DWORD> maybe_err_id = std::nullopt) {
		err_id = maybe_err_id ? *maybe_err_id : ::GetLastError();

		// The Dark Art incantation that has lived for over 30 years.
		length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			err_id,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			reinterpret_cast<LPWSTR>(buffer.put()),
			0,
			nullptr
		);
	}


	CFormatMessageWrapper(const CFormatMessageWrapper&) = delete;
	CFormatMessageWrapper& operator=(const CFormatMessageWrapper&) = delete;

	explicit operator bool() const { return length != 0; }
	[[nodiscard]] bool empty() const { return length == 0; }
	[[nodiscard]] LPCWSTR c_str() const { return buffer ? buffer.get() : L""; }
	[[nodiscard]] DWORD code() const { return err_id; }
};



// Owners for libgit2 handles, moved here from wil_git2 (SmartLibgit2Ref.ixx).
// Every one is wil's, so there is no class to get wrong - the hand-rolled
// CSmartLibgit2Ref re-exposed its conversion operator through a
// using-declaration that MSVC refuses across a module boundary (C2248).
template <typename T, void Free(T*)>
using unique_git = unique_any<T*, decltype(Free), Free>;

using unique_git_object           = unique_git<git_object,             git_object_free>;
using unique_git_commit           = unique_git<git_commit,             git_commit_free>;
using unique_git_tree             = unique_git<git_tree,               git_tree_free>;
using unique_git_blob             = unique_git<git_blob,               git_blob_free>;
using unique_git_tag              = unique_git<git_tag,                git_tag_free>;
using unique_git_tree_entry       = unique_git<git_tree_entry,         git_tree_entry_free>;
using unique_git_reference        = unique_git<git_reference,          git_reference_free>;
using unique_git_repository       = unique_git<git_repository,         git_repository_free>;
using unique_git_config           = unique_git<git_config,             git_config_free>;
using unique_git_index            = unique_git<git_index,              git_index_free>;
using unique_git_submodule        = unique_git<git_submodule,          git_submodule_free>;
using unique_git_diff             = unique_git<git_diff,               git_diff_free>;
using unique_git_patch            = unique_git<git_patch,              git_patch_free>;
using unique_git_diff_stats       = unique_git<git_diff_stats,         git_diff_stats_free>;
using unique_git_remote           = unique_git<git_remote,             git_remote_free>;
using unique_git_reflog           = unique_git<git_reflog,             git_reflog_free>;
using unique_git_revwalk          = unique_git<git_revwalk,            git_revwalk_free>;
using unique_git_branch_iterator  = unique_git<git_branch_iterator,    git_branch_iterator_free>;
using unique_git_reference_iterator = unique_git<git_reference_iterator, git_reference_iterator_free>;
using unique_git_describe_result  = unique_git<git_describe_result,    git_describe_result_free>;
using unique_git_status_list      = unique_git<git_status_list,        git_status_list_free>;
using unique_git_note             = unique_git<git_note,               git_note_free>;
using unique_git_signature        = unique_git<git_signature,          git_signature_free>;
using unique_git_mailmap          = unique_git<git_mailmap,            git_mailmap_free>;
using unique_git_worktree         = unique_git<git_worktree,           git_worktree_free>;

// Structs libgit2 fills in place and disposes of by address; unique_struct
// derives from the struct, so &buf is a git_buf* and buf.ptr just works.
using unique_git_buf      = unique_struct<git_buf,      decltype(&git_buf_dispose),      git_buf_dispose>;
using unique_git_strarray = unique_struct<git_strarray, decltype(&git_strarray_dispose), git_strarray_dispose>;


/// The one thing CAutoRepository did that is not ownership: TGitCache and the
/// shell extension never initialize gitdll, so opening a repository here is the
/// only place their object format is ever established. Check the result with
/// is_valid(); on failure it is empty and git_error_last() says why.
///
/// UTF-8 only, deliberately: wil cannot see CUnicodeUtils. A caller holding a
/// wide path goes through the one wide overload that encodes with
/// CUnicodeUtils::StdGetUTF8 (CAutoRepository(std::wstring_view) today,
/// TGitH::OpenGitRepository(std::wstring_view) once the bridge retires) - never
/// its own conversion (CStringA(path) would be the ACP, not UTF-8).
unique_git_repository OpenGitRepository(const char* const gitDirUtf8) {
	unique_git_repository repo;
	if (!git_repository_open(repo.put(), gitDirUtf8))
		tgit::invarients::LatchObjectFormat(git_repository_oid_type(repo.get()));
	return repo;
}
} // end wil



/// What sanity.h's SCOPE_EXIT expands against: `scope_exit_helper() << [&] { ... };`.
/// Global, because the macro names it unqualified. The guard is wil's: its move
/// disarms the source, which tgittypes' hand-rolled scope_exit_t did not (a moved-
/// from guard still ran its lambda). tgittypes re-exports this one rather than
/// defining its own, so no TU can reach two of them.
export struct scope_exit_helper {
	template <class F>
	[[nodiscard]] auto operator<<(F&& f) const {
		return wil::scope_exit(std::forward<F>(f));
	}
};

