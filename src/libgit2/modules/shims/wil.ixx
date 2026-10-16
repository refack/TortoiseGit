module;
#include <wil/win32_helpers.h>
#include <wil/resource.h>
#include <wil/filesystem.h>
#include <wil/registry.h>
#include <wil/com.h>			// com_ptr_nothrow: what CComPtr was, with no ATL header
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
using wil::unique_handle;
using wil::unique_struct;
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


// Registry. There is no TortoiseGit default here any more: the get_value_* /
// set_value_string wrappers that silently read HKCU\Software\TortoiseGit are gone,
// and every caller names its root and subkey (Registry::TortoiseGitKey owns the
// TortoiseGit one). A missing value is an empty optional; the caller's value_or
// is its default, so no default can be a null pointer (CLAUDE.md failure mode 5).
namespace reg {
	// The stock writers. Each takes the root and subkey explicitly.
	using wil::reg::set_value_string_nothrow;
	using wil::reg::set_value_dword_nothrow;


	/// wil::reg::try_get_value_dword(root, subkey, name), compiled in this module
	/// (see the note at the re-exports). Empty when the value is absent; throws
	/// wil::ResultException on any other failure, exactly as wil's does.
	std::optional<std::uint32_t> try_get_dword(const HKEY& root, const PCWSTR subkey, const PCWSTR name) {
		return wil::reg::try_get_value_dword(root, subkey, name);
	}


	/// wil::reg::try_get_value_string(root, subkey, name), compiled in this module.
	/// Same contract as try_get_dword.
	std::optional<std::wstring> try_get_string(const HKEY& root, const PCWSTR subkey, const PCWSTR name) {
		return wil::reg::try_get_value_string(root, subkey, name);
	}
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
	std::wstring m_subkey{};
	std::wstring m_name{};
	std::vector<std::wstring> m_entries{};
	size_t m_maxEntries{ 25 };

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
	void Load(std::wstring subkey, std::wstring name) {
		if (subkey.empty() || name.empty())
			return;
		m_subkey = std::move(subkey);
		m_name = std::move(name);
		m_entries.clear();

		const HRESULT hr = ResultFromException([&] {
			// ReSharper disable once CppLocalVariableMayBeConst
			if (auto entries = wil::reg::try_get_value_multistring(HKEY_CURRENT_USER, m_subkey.c_str(), m_name.c_str())) {
				m_entries = std::move(*entries);
				return;
			}
			for (size_t n = 0; n < m_maxEntries; ++n) {
				auto entry = wil::reg::try_get_value_string(HKEY_CURRENT_USER, m_subkey.c_str(), LegacyValueName(n).c_str());
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


	/// Writes the history in one call, then removes the legacy per-entry values.
	bool Save() const {
		if (m_subkey.empty())
			return false;
		const HRESULT hr = ResultFromException([&] {
			wil::reg::set_value_multistring(HKEY_CURRENT_USER, m_subkey.c_str(), m_name.c_str(), m_entries);
		});
		if (FAILED(hr))
			return false;
		// The legacy values run contiguously from <name>0. Once they are gone this is
		// one failing call per Save.
		for (size_t n = 0; ::RegDeleteKeyValueW(HKEY_CURRENT_USER, m_subkey.c_str(), LegacyValueName(n).c_str()) == ERROR_SUCCESS; ++n) {
		}
		return true;
	}


	/// Moves \a text to the front of the list, adding it if it is not there yet.
	/// Reloads first, so an entry another dialog saved in the meantime is kept.
	/// Returns true when \a text was new.
	bool AddEntry(const std::wstring_view text) {
		if (text.empty())
			return false;
		if (!m_subkey.empty())
			Load(m_subkey, m_name);

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
/// from it. What StringUtils::WriteAsciiStringToClipboard(const CStringW&, HWND)
/// did, without the CString. A CString caller bridges visibly with a view.
bool SetClipboardText(const std::wstring_view text, const HWND& owner = nullptr) {
	// An if-init-statement for truth: `clipboard` is a RAII so we spell out a nested scope for it.
	if (const auto clipboard = open_clipboard_retrying(owner); clipboard) {
		::EmptyClipboard();
		auto block = make_moveable_hglobal(text);
		if (!block || !::SetClipboardData(CF_UNICODETEXT, block.get()))
			return false;
		block.release(); // the clipboard owns it now
		return true;
	}
	return false;
}


/// The known folder's path. Throws std::runtime_error (not wil::ResultException)
/// when SHGetKnownFolderPath fails: no caller catches either, and changing the
/// type is a decision of its own, not part of trimming this module.
fs::path GetKnownFolderPath(
	REFKNOWNFOLDERID folderId,
	const KNOWN_FOLDER_FLAG flags = KF_FLAG_DEFAULT,
	const std::optional<HANDLE> token = std::nullopt
) {
	wil::unique_cotaskmem_string result{};
	if (::SHGetKnownFolderPath(folderId, flags, token.value_or(nullptr), result.put()) != S_OK) {
		const auto hstring = ::winrt::to_hstring(folderId);
		throw std::runtime_error{ "SHGetKnownFolderPath failed for " + ::winrt::to_string(hstring) };
	}
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


/// This is a representation of an API horizon.
/// We should validate all of our invariants here.
///
/// Kept, deliberately, past the wil trim: MAKESTRING (TortoiseShell) reads its
/// strings through LoadStringEx, and moving it to ::LoadStringW changes what it
/// returns for an over-long, NUL-bearing or ill-formed string and which language
/// it picks. That is a behaviour decision with its own commit, not a trim.
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
	[[nodiscard]] std::expected<void, CopyError>
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


	std::wstring_view get_string_resource(const std::optional<HINSTANCE> oInstance, const UINT uID,
	                                      const WORD wLanguage = LANG_USER_DEFAULT) {
		// A string table is stored in blocks of 16: block (uID / 16) + 1, slot uID % 16.
		const auto blockResId = MAKEINTRESOURCE((uID >> 4) + 1);
		const wchar_t* pCurrentStr;
		{
			const auto hInstance = oInstance.value_or(nullptr);
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

			pCurrentStr = static_cast<const wchar_t*>(LockResource(hResData));
			if (!pCurrentStr)
				return {};
		}

		// 5. Index into the block to find the precise string slot (4 first bits, norm 0 to 15)
		const auto stringIndexWithinBlock = uID & ((1u << 4) - 1);

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


	size_t LoadStringEx(
		const HINSTANCE& hInstance,
		const UINT uID,
		wchar_t* const lpBuffer_,
		const int nBufferMax,
		const WORD wLanguage
	) {
		if (lpBuffer_ == nullptr || nBufferMax <= 0)
			return 0;
		lpBuffer_[0] = L'\0';

		auto&& lpBuffer = std::span{ lpBuffer_, gsl::narrow<size_t>(nBufferMax) };
		auto&& resView = get_string_resource(hInstance, uID, wLanguage);
		// ReSharper disable once CppTooWideScopeInitStatement
		auto&& r = CopyTerminated(lpBuffer, resView);
		if (!r)
			return 0;

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



// Owners for libgit2 handles. Every one is wil's, so there is no class to get
// wrong - the hand-rolled ones they replaced re-exposed a conversion operator
// through a using-declaration that MSVC refuses across a module boundary (C2248).
// No implicit conversion to the raw pointer: pass .get(), and .put() (or &x,
// which frees first) for an out-parameter.
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
/// CUnicodeUtils::StdGetUTF8, TGitH::OpenGitRepository(std::wstring_view) in
/// git_h - never its own conversion (CStringA(path) would be the ACP, not UTF-8).
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
/// from guard still ran its lambda). This is the only definition: a TU that
/// expands SCOPE_EXIT imports wil.
export struct scope_exit_helper {
	template <class F>
	[[nodiscard]] auto operator<<(F&& f) const {
		return wil::scope_exit(std::forward<F>(f));
	}
};

