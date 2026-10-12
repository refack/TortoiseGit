module;
// The string shim: the ONLY module whose global module fragment sees afx.h or
// atlstr.h, and the only one that tests TGIT_MFC (CLAUDE.md, "Strings"). Every
// other module gets CString from here, as tgit::wstr::CString / tgit::zstr::CString,
// and TGIT_MFC is set per target, so the choice below is frozen for each importer.
//
// The fork comes first: MFC requires afx.h ahead of anything that includes
// windows.h.
#ifdef TGIT_MFC
#include <afx.h>
#endif
#include <windows.h>
#include <atlstr.h>
#include <winrt/base.h>		// ::winrt::to_hstring, in CUnicodeUtils::GetUnicode


export module str_utils;
import std;
import gsl;
import wil;			// wil::unique_hfile, for the text-file helpers


// ---- The shim fork ------------------------------------------------------------
// CString is not one type: MFC-Dynamic gets CStringT<wchar_t, StrTraitMFC_DLL<...>>,
// ATL-only gets CStringT<wchar_t, StrTraitATL<...>>, and the two mangle differently
// (CLAUDE.md, "The CString ABI wall"). Each flavor of this module names its own,
// explicitly: under _AFX, atlstr.h declares no ATL::CStringW and afxstr.h declares
// the MFC one globally; without it, ATL::CAtlStringW is what ATL::CStringW names.
#ifdef TGIT_MFC
export namespace tgit::wstr { using CString = ::CStringW; }	// CStringT<wchar_t, StrTraitMFC_DLL<wchar_t>>
export namespace tgit::zstr { using CString = ::CStringA; }	// CStringT<char, StrTraitMFC_DLL<char>>
#else
export namespace tgit::wstr { using CString = ATL::CAtlStringW; }	// CStringT<wchar_t, StrTraitATL<wchar_t, ChTraitsCRT<wchar_t>>>
export namespace tgit::zstr { using CString = ATL::CAtlStringA; }	// CStringT<char, StrTraitATL<char, ChTraitsCRT<char>>>
#endif


// ---- tgit::str: the kind-generic layer ------------------------------------------
// Traits and concepts over every string shape a call site hands the converters,
// std or CStringT. Neither wide nor narrow, which is why this is not tgit::zstr
// (the narrow side) any more.
export namespace tgit::str
{

/// std::basic_string and std::basic_string_view, by shape.
template <typename TStr>
concept IsRealString = requires(const TStr& s) {
	typename TStr::value_type;
	typename TStr::const_pointer;
//	typename TStr::traits_type;
	{ s.data() } -> std::convertible_to<typename TStr::const_pointer>;
	{ s.size() } -> std::convertible_to<size_t>;
};

// The shape of CStringT<XCHAR, StrTraits> and nothing ATL-specific: this module
template <typename TStr>
concept IsStupidString = requires(const TStr& s) {
	typename TStr::XCHAR;
	typename TStr::PCXSTR;
//	typename TStr::StrTraits;
	{ s.GetString() } -> std::convertible_to<typename TStr::PCXSTR>;
	{ s.GetLength() } -> std::convertible_to<int>;
};


template <typename TStr>
concept IsTCompString = IsRealString<TStr> || IsStupidString<TStr>;

/// Per-kind access, selected by shape. The primary is left undefined, so a type
/// that is neither kind fails as "incomplete type", and leaving it unconstrained
/// avoids leaning on subsumption.
template <typename TStr>
struct StringViewTraits;

// size_type is the length type the kind's (data, length) constructor takes -
// int for CStringT, size_type for the std ones - and size<TOut>(s) is s's
// length in TOut's size_type, so TOut(data(s), size<TOut>(s)) is exact for
// either output kind. Narrowing, so a std string too long for a CString
// throws rather than wrapping.
template <IsRealString TStr>
struct StringViewTraits<TStr> {
	using char_type     = TStr::value_type;
	using const_pointer = TStr::const_pointer;
	using size_type     = TStr::size_type;
	static constexpr const_pointer data(const TStr& s) noexcept { return s.data(); }
	static constexpr size_t        size(const TStr& s) noexcept { return s.size(); }

	template <IsTCompString TOut>
	static constexpr auto          size(const TStr& s) { return gsl::narrow<typename StringViewTraits<TOut>::size_type>(size(s)); }
};

template <IsStupidString TStr>
struct StringViewTraits<TStr> {
	using char_type     = TStr::XCHAR;
	using const_pointer = TStr::PCXSTR;
	using size_type     = int;
	static constexpr const_pointer data(const TStr& s) noexcept { return s.GetString(); }
	static constexpr size_t        size(const TStr& s) noexcept { return gsl::narrow_cast<size_t>(s.GetLength()); }

	template <IsTCompString TOut>
	static constexpr auto          size(const TStr& s) { return gsl::narrow<typename StringViewTraits<TOut>::size_type>(size(s)); }
};



template <typename TStr>
struct StringTraits;

// overwrite(s, cap, fill) is resize_and_overwrite for either kind: fill(buf,
// cap) writes at most cap characters straight into s's own storage and returns
// how many it wrote, which becomes the length. GetBuffer / ReleaseBuffer is
// CStringT's spelling of the same thing - neither zero-fills, neither copies.
// fill must not throw: resize_and_overwrite makes that a precondition, so a
// failure is reported by writing nothing and checked once the call returns.

template <IsRealString TStr> requires (!std::ranges::enable_borrowed_range<TStr>)
// template <IsRealString TStr>
struct StringTraits<TStr> : StringViewTraits<TStr> {
	static constexpr void        resize(TStr& s, size_t l) { s.resize(l); }

	template <typename TFill>
	static constexpr void        overwrite(TStr& s, const size_t cap, TFill fill)
	{
		s.resize_and_overwrite(cap, [&fill](typename TStr::value_type* buf, const size_t n) { return fill(buf, n); });
	}
};

template <IsStupidString TStr>
struct StringTraits<TStr> : StringViewTraits<TStr> {
	static constexpr void        resize(TStr& s, size_t l) { s.Truncate(gsl::narrow_cast<int>(l)); }

	template <typename TFill>
	static void                  overwrite(TStr& s, const size_t cap, TFill fill)
	{
		s.ReleaseBuffer(gsl::narrow<int>(fill(s.GetBuffer(gsl::narrow<int>(cap)), cap)));
	}
};


/// A NUL-terminated char / wchar_t pointer.
template <typename TStr>
concept IsRawString = std::is_pointer_v<TStr>
	&& (std::same_as<std::remove_cv_t<std::remove_pointer_t<TStr>>, char>
	 || std::same_as<std::remove_cv_t<std::remove_pointer_t<TStr>>, wchar_t>);

// A raw pointer has StringTraits but no StringViewTraits. Every view accessor
// is an O(1) read of a length the string already knows; a pointer knows none,
// so size() walks it - O(n), which is why it lives here with the owners and
// not with the views. No resize / overwrite: nothing can grow a pointer.
//
// A null pointer is the empty string, as CString(nullptr) is. std::wstring(nullptr)
// is UB, and treating "absent" as "crash" is the trap CLAUDE.md lists as failure
// mode 5 (CCmdLineParser::GetVal handing nullptr to a std::wstring).
template <IsRawString TStr>
struct StringTraits<TStr> {
	using char_type     = std::remove_cv_t<std::remove_pointer_t<TStr>>;
	using const_pointer = const char_type*;
	static constexpr const_pointer data(const TStr s) noexcept { return s; }
	static constexpr size_t        size(const TStr s) noexcept { return s ? std::char_traits<char_type>::length(s) : 0; }
};


// "Has a view of TChar for free" versus "can be read as TChar at all". A
// shape with no specialization leaves the traits incomplete, which the
// requires-expression reports as unsatisfied rather than as a hard error.
template <typename T, typename TChar>
concept HasViewTraitsOf = requires(const T& s) {
	{ StringViewTraits<T>::data(s) } -> std::convertible_to<const TChar*>;
	{ StringViewTraits<T>::size(s) } -> std::same_as<size_t>;
} && std::same_as<typename StringViewTraits<T>::char_type, TChar>;

template <typename T, typename TChar>
concept HasStringTraitsOf = requires(const T& s) {
	{ StringTraits<T>::data(s) } -> std::convertible_to<const TChar*>;
	{ StringTraits<T>::size(s) } -> std::same_as<size_t>;
} && std::same_as<typename StringTraits<T>::char_type, TChar>;

/// The traits key for an owned input as a template deduces it from `const TIn&`:
/// a literal deduces as char[N], and the const has to go in before the decay
/// or it would come out as char* rather than const char*.
template <typename TIn>
using TOwnedKey = std::decay_t<const TIn>;


template <typename TStr>
concept TStrOfChar = IsTCompString<TStr> && std::same_as<typename StringViewTraits<TStr>::char_type, char>;

template <typename TStr>
concept TStrOfWide = IsTCompString<TStr> && std::same_as<typename StringViewTraits<TStr>::char_type, wchar_t>;


/// Non-owning view of either kind of string - a CString, for calling the
/// std::wstring core from a call site that has not migrated yet, or a std one.
///
/// O(1) for a CString: it knows its length, so this does not walk the string
/// the way std::wstring_view(cstring) would.
template <IsTCompString TStr>
struct StringView : std::basic_string_view<typename StringViewTraits<TStr>::char_type>
{
	using cnv_view_t = StringViewTraits<TStr>;
	using std_view_t = std::basic_string_view<typename cnv_view_t::char_type>;

	constexpr auto    as_view()    noexcept { return std_view_t{this->data(), this->size()}; }

	constexpr static std_view_t    as_view(const TStr& s)    noexcept { return {cnv_view_t::data(s), cnv_view_t::size(s)}; }
	constexpr explicit             StringView(const TStr& s) noexcept : std_view_t{as_view(s)} {}
	constexpr explicit             StringView(const cnv_view_t::const_pointer s) noexcept : std_view_t{s} {}
};


/// Default predicate: dispatches on character type.
struct is_space_t {
	bool operator()(char c)    const noexcept { return std::isspace(static_cast<unsigned char>(c)) != 0; } // negative char is UB
	bool operator()(wchar_t c) const noexcept { return std::iswspace(c) != 0; }
};
inline constexpr is_space_t is_space{};


}

// Inside this module only - targeted, not a using-directive, so nothing here
// pulls a whole namespace into another's lookup.
using tgit::str::IsRealString;
using tgit::str::IsStupidString;
using tgit::str::IsTCompString;
using tgit::str::StringViewTraits;
using tgit::str::StringTraits;
using tgit::str::HasViewTraitsOf;
using tgit::str::HasStringTraitsOf;
using tgit::str::TOwnedKey;
using tgit::str::TStrOfChar;
using tgit::str::TStrOfWide;
using tgit::str::StringView;
using tgit::str::is_space;


// The traits classification of every string shape a call site hands the
// converters, checked here because this module sees CStringT as well as the std
// types.
//
// View traits (GetUnicode / GetUTF8, O(1) on our side): everything that
// knows its own length. Raw pointers are deliberately left out - a pointer
// knows no length, so reaching one means walking the string, and that is a
// conversion, not a cast.
static_assert(HasViewTraitsOf<std::string, char>,         "std::string");
static_assert(HasViewTraitsOf<std::wstring, wchar_t>,     "std::wstring");
static_assert(HasViewTraitsOf<std::string_view, char>,    "std::string_view");
static_assert(HasViewTraitsOf<std::wstring_view, wchar_t>, "std::wstring_view");
static_assert(!HasViewTraitsOf<const char*, char>,        "const char* has no O(1) length");
static_assert(!HasViewTraitsOf<const wchar_t*, wchar_t>,  "const wchar_t* has no O(1) length");
static_assert(HasViewTraitsOf<tgit::zstr::CString, char>,    "tgit::zstr::CString");
static_assert(HasViewTraitsOf<tgit::wstr::CString, wchar_t>, "tgit::wstr::CString");

// String traits (Convert2Unicode / Convert2Multibyte): the owners, raw
// pointers included. The views are the ones left out - they own nothing.
static_assert(HasStringTraitsOf<std::string, char>,          "std::string");
static_assert(HasStringTraitsOf<std::wstring, wchar_t>,      "std::wstring");
static_assert(!HasStringTraitsOf<std::string_view, char>,    "std::string_view owns nothing");
static_assert(!HasStringTraitsOf<std::wstring_view, wchar_t>, "std::wstring_view owns nothing");
static_assert(HasStringTraitsOf<const char*, char>,          "const char*");
static_assert(HasStringTraitsOf<const wchar_t*, wchar_t>,    "const wchar_t*");
static_assert(HasStringTraitsOf<tgit::zstr::CString, char>,    "tgit::zstr::CString");
static_assert(HasStringTraitsOf<tgit::wstr::CString, wchar_t>, "tgit::wstr::CString");


// Helpers for carrying src\Git and src\Utils from CString to std::wstring.
//
// Why this exists at all: CString is not one type. In an MFC-Dynamic project it
// is CStringT<wchar_t, StrTraitMFC_DLL<...>>; in an ATL-only project it is
// CStringT<wchar_t, StrTraitATL<...>>. Those mangle differently, so a single
// compiled library cannot expose CString in its headers and serve both
// TortoiseGitProc and TortoiseGit.dll. std::wstring is one type in both, and on
// Windows it is already what LPCWSTR points at, so the boundary stays free.
//
// Three of the CString idioms this code uses do NOT translate by renaming, and
// each one fails silently rather than at compile time. They are the reason this
// header exists rather than a search-and-replace:
//
//   CString::Find returns -1 when absent; std::wstring::find returns npos,
//   which is SIZE_MAX. So `if (s.Find(L"x") >= 0)` becomes always-true when
//   renamed to find(). Every one of those is a live bug that compiles.
//
//   CString::GetLength returns int; size() returns size_t. `s.GetLength() - 1`
//   on an empty string is -1, while `s.size() - 1` is SIZE_MAX, and a signed
//   loop counter compared against size() changes meaning under promotion.
//
//   CString::Mid and ::Left clamp their arguments; substr throws
//   std::out_of_range past the end. Code that relies on the clamp turns a
//   correct result into an exception.
//
// So the functions below deliberately keep CString's contract - int offsets,
// -1 for absent, clamping - so that converting a call site is a rename and
// nothing more. That is the whole argument for choosing std::wstring over
// std::u16string: the migration stays mechanical and the compiler checks it.
// NEW code should prefer std::wstring's own members and npos; these are for
// carrying existing call sites across without re-deciding each one.
export namespace tgit::wstr
{
// CString itself is declared at the shim fork, at the top of this module.

/// Whitespace CString::Trim() removes, so a converted call site keeps behaving.
inline constexpr std::wstring_view kDefaultTrimChars = L" \t\r\n";



/// CString::Find contract: byte offset, or -1 when absent.
inline int Find(const std::wstring_view s, const std::wstring_view needle, const int from = 0) noexcept
{
	if (from < 0 || static_cast<size_t>(from) > s.size())
		return -1;
	const size_t at = s.find(needle, static_cast<size_t>(from));
	return at == std::wstring_view::npos ? -1 : static_cast<int>(at);
}

/// CString::Find contract: byte offset, or -1 when absent.
inline int Find(const std::wstring_view s, const wchar_t needle, const int from = 0) noexcept
{
	if (from < 0 || static_cast<size_t>(from) > s.size())
		return -1;
	const size_t at = s.find(needle, static_cast<size_t>(from));
	return at == std::wstring_view::npos ? -1 : static_cast<int>(at);
}

/// CString::ReverseFind contract: byte offset, or -1 when absent.
inline int ReverseFind(const std::wstring_view s, const wchar_t needle) noexcept
{
	const size_t at = s.rfind(needle);
	return at == std::wstring_view::npos ? -1 : static_cast<int>(at);
}

/// CString::FindOneOf contract: byte offset, or -1 when absent.
inline int FindOneOf(const std::wstring_view s, const std::wstring_view chars) noexcept
{
	const size_t at = s.find_first_of(chars);
	return at == std::wstring_view::npos ? -1 : static_cast<int>(at);
}

inline std::wstring Left(const std::wstring_view s, const size_t count) {
	return std::wstring{ s.substr(0, count) };
}

/// CString::Left, including its clamping: substr() would give the full str.
inline std::wstring Left(const std::wstring_view s, const int _count)
{
	if (_count < 0)
		return {};
	const auto count = static_cast<size_t>(_count);
	return Left(s, count);
}

/// CString::Right, including its clamping.
inline std::wstring Right(const std::wstring_view s, const long _count)
{
	if (_count < 0)
		return {};
	const auto count = static_cast<std::wstring_view::size_type>(_count);
	const size_t take = std::min(count, s.size());
	return std::wstring(s.substr(s.size() - take));
}

/// substr's contract rather than CString's: unsigned, and npos means "to the
/// end". This is the overload for a caller that already holds real sizes; the
/// int ones below are the CString bridge, and they are not the same function.
inline std::wstring Mid(const std::wstring_view s, const std::wstring_view::size_type from, const std::wstring_view::size_type count)
{
	if (from > s.size())
		return {};
	return std::wstring{ s.substr(from, count) };
}

/// CString::Mid(iFirst), including its clamping. CString spells this as
/// Mid(iFirst, GetLength() - iFirst), so a negative iFirst asks for *more* than
/// the whole string and clamps back to all of it.
inline std::wstring Mid(const std::wstring_view s, const int from)
{
	if (from < 0)
		return std::wstring{ s };
	return Mid(s, static_cast<std::wstring_view::size_type>(from), std::wstring_view::npos);
}

/// CString::Mid(iFirst, nCount), including its clamping. **A negative nCount is
/// clamped to zero before anything else**, so Mid(n, -1) is the empty string and
/// emphatically not "the rest of it".
///
/// That is why there is no default argument here: one signature cannot say both
/// "no count was given, so take the tail" and "a count of -1 was given, so take
/// nothing". Defaulting it to npos made Mid(n, -1) return the tail; defaulting
/// it to 0 makes Mid(n) return nothing. Two overloads, no default, and each one
/// matches its CString counterpart exactly.
inline std::wstring Mid(const std::wstring_view s, const int from, const int count)
{
	if (count <= 0)
		return {};
	return Mid(s, static_cast<std::wstring_view::size_type>(std::max(from, 0)), static_cast<std::wstring_view::size_type>(count));
}

/// CString::Replace contract: every non-overlapping occurrence, left to right;
/// returns how many were replaced, and an empty \a from replaces nothing.
/// (The generic version this replaces overwrote to.size() characters rather
/// than from.size(), and never terminated for an empty \a to.)
inline int Replace(std::wstring& s, const std::wstring_view from, const std::wstring_view to)
{
	if (from.empty())
		return 0;
	int count = 0;
	for (size_t pos = 0; (pos = s.find(from, pos)) != std::wstring::npos; pos += to.size(), ++count)
		s.replace(pos, from.size(), to);
	return count;
}

inline int Replace(std::wstring& s, const wchar_t from, const wchar_t to) noexcept
{
	int count = 0;
	for (auto& c : s)
		if (c == from)
		{
			c = to;
			++count;
		}
	return count;
}

inline void TrimLeft(std::wstring& s, const std::wstring_view chars = kDefaultTrimChars)
{
	s.erase(0, (std::min)(s.find_first_not_of(chars), s.size()));
}

inline void TrimRight(std::wstring& s, const std::wstring_view chars = kDefaultTrimChars)
{
	const size_t last = s.find_last_not_of(chars);
	s.erase(last == std::wstring::npos ? 0 : last + 1);
}

inline void Trim(std::wstring& s, const std::wstring_view chars = kDefaultTrimChars)
{
	TrimRight(s, chars);
	TrimLeft(s, chars);
}

inline void MakeLower(std::wstring& s)
{
	std::ranges::transform(s, s.begin(), [](const wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
}

inline void MakeUpper(std::wstring& s)
{
	std::ranges::transform(s, s.begin(), [](const wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
}

/// CString::CompareNoCase contract: <0, 0 or >0.
inline int CompareNoCase(const std::wstring_view lhs, const std::wstring_view rhs) noexcept
{
	// An index loop over the common prefix, not std::views::zip: instantiated in a TU
	// that also has <ranges> textually (TortoiseProc's stdafx.h), MSVC merges the
	// imported and the textual zip and rejects them as differing in noexcept (C2382).
	const size_t common = (std::min)(lhs.size(), rhs.size());
	for (size_t i = 0; i < common; ++i)
		if (std::towlower(lhs[i]) != std::towlower(rhs[i]))
			return std::towlower(lhs[i]) < std::towlower(rhs[i]) ? -1 : 1;
	if (lhs.size() == rhs.size())
		return 0;
	return lhs.size() < rhs.size() ? -1 : 1;
}

inline bool StartsWith(const std::wstring_view s, const std::wstring_view prefix) noexcept
{
	return s.starts_with(prefix);
}

inline bool EndsWith(const std::wstring_view s, const std::wstring_view suffix) noexcept
{
	return s.ends_with(suffix);
}

/// CString::Tokenize's contract, which IS strtok-like: leading delimiters are
/// skipped, so a run of them yields one token and never an empty one, and
/// "a//b" is two tokens rather than three. Returns the token and updates pos;
/// pos < 0 means exhausted, and the token returned with it is empty.
///
/// This is the one helper here whose behaviour I guessed wrong: the
/// differential test in WideStringTest.cpp caught it, which is the argument
/// for testing these against CString rather than against an expectation.
inline std::wstring Tokenize(const std::wstring_view s, const std::wstring_view delimiters, int& pos)
{
	if (pos < 0 || delimiters.empty() || static_cast<size_t>(pos) >= s.size())
	{
		pos = -1;
		return {};
	}
	const size_t start = s.find_first_not_of(delimiters, static_cast<size_t>(pos));
	if (start == std::wstring_view::npos)
	{
		pos = -1;
		return {};
	}
	size_t end = s.find_first_of(delimiters, start);
	if (end == std::wstring_view::npos)
		end = s.size();
	pos = static_cast<int>(end) + 1;
	return std::wstring(s.substr(start, end - start));
}

/// Copies \a source into the fixed-size buffer \a dest - a Win32 struct's
/// `wchar_t sz[MAX_PATH]`, say - truncating to fit and always NUL-terminating.
/// StringCchCopy's contract, without strsafe: returns false when it truncated.
/// \a source is a view, so it stops at its size, not at an embedded NUL.
inline bool CopyTruncating(const std::span<wchar_t> dest, const std::wstring_view source) noexcept
{
	if (dest.empty())
		return source.empty();
	const size_t count = (std::min)(source.size(), dest.size() - 1);
	std::ranges::copy(source.substr(0, count), dest.begin());
	dest[count] = L'\0';
	return count == source.size();
}

/// The GetBuffer/ReleaseBuffer pair, for the Win32 calls that fill a caller's
/// buffer. std::wstring guarantees contiguous storage, so this needs no copy;
/// what it does need is the resize back down, which ReleaseBuffer did.
inline void ReleaseBuffer(std::wstring& s, const int newLength = -1)
{
	if (newLength >= 0)
		s.resize((std::min)(static_cast<size_t>(newLength), s.size()));
	else
		s.resize(wcsnlen(s.c_str(), s.size()));
}


template <TStrOfWide TStr>
std::filesystem::path to_path(const TStr& sPath)
{
	return std::filesystem::path{StringView{sPath}.as_view()};
}


// end export
}


// The narrow side. CString is declared at the shim fork, at the top of this module.
export namespace tgit::zstr
{

/// The start of the record after the one at \a start, in a buffer of
/// NUL-separated records (git's `-z` output): skips to the next NUL, then past
/// every NUL in the run. npos when no record follows. This was
/// CGitByteArray::findNextString's loop; '\0', not 0, so the find() below takes
/// the char overload and cannot be read as find(const char*).
std::size_t FindNextString(const std::string_view buffer, const std::size_t start = 0) noexcept
{
	std::size_t pos = start;
	const std::size_t end = buffer.size();
	do {
		pos = buffer.find('\0', pos);
		if (pos == std::string_view::npos)
			break;
		++pos;
		if (pos >= end)
			return std::string_view::npos;
	}
	while (buffer[pos] == '\0');

	return pos;
}

}


// Lets std::format take a CString wherever it already takes a std::wstring.
//
// This is what makes std::format the right fix for a concatenation chain
// rather than a cosmetic rewrite of one. The chains in this codebase mix
// CString, std::wstring, wchar_t and const wchar_t*, and every conversion
// between them is a place to get it wrong - appending .c_str() to two of them
// silently produces "const wchar_t* + const wchar_t*", which is pointer
// arithmetic that compiles. std::format has no such failure mode: each
// argument is formatted independently, so there is no operator+ to resolve and
// no mixed-type expression to reason about.
//
// Explicit specializations for the flavor's CStrings, not the partial one
// constrained on IsStupidString this used to be. That one was never usable
// outside this module: MSVC did not find it from an importer (std::formattable<
// CString, wchar_t> was false in StringUtils, which imported wstr), and its body
// built one wchar_t from the whole string, so it had never been instantiated
// anywhere either. Keyed on the shim's aliases, so the TGIT_MFC build of this
// module specializes for the MFC types and the ATL build for the ATL ones. Not
// `export`ed: a specialization of a std template is reached through the primary.
template <>
struct std::formatter<tgit::wstr::CString, wchar_t> : std::formatter<std::wstring_view, wchar_t>
{
	auto format(const tgit::wstr::CString& value, auto& ctx) const
	{
		return std::formatter<std::wstring_view, wchar_t>::format(std::wstring_view(value.GetString(), value.GetLength()), ctx);
	}
};


template <>
struct std::formatter<tgit::zstr::CString, char> : std::formatter<std::string_view, char>
{
	auto format(const tgit::zstr::CString& value, auto& ctx) const
	{
		return std::formatter<std::string_view, char>::format(std::string_view(value.GetString(), value.GetLength()), ctx);
	}
};


export namespace CUnicodeUtils
{
// Safely convert from size_t to int.
// Throws a std::overflow_error exception if the conversion is impossible.
inline int SafeSizeToInt(const size_t sizeValue)
{
	// ReSharper disable once CppTooWideScopeInitStatement
	constexpr int kIntMax = (std::numeric_limits<int>::max)();
	if (sizeValue >= static_cast<size_t>(kIntMax))
		throw std::overflow_error("size_t value is too big to fit into an int.");

	return gsl::narrow<int>(sizeValue);
}

// The conversion core, one body each way. TOut is whichever string the caller
// wants back - std::string / std::wstring by default, a CStringT on request -
// and the API writes straight into its storage, so a call constructs exactly
// one string. (The CString adapters used to convert into a std::wstring and
// then copy that into the CString: two allocations and two writes per call.
// Copy elision cannot remove that copy, because it never crosses types.)
// Defined here, ahead of the adapters, because a non-dependent call inside a
// template is looked up where the template is *defined*, not where it is
// instantiated.
//
// defaultChar is the byte substituted for anything the target code page cannot
// represent; null means "whatever the system picks", normally '?'. It is
// dropped for CP_UTF8 and CP_UTF7, which reject both lpDefaultChar and
// lpUsedDefaultChar with ERROR_INVALID_PARAMETER - and have nothing to
// substitute for anyway, since they can encode every input. The paired
// lpUsedDefaultChar flag is deliberately not offered: no call site has ever
// read it.
template <TStrOfChar TOut = std::string>
TOut GetMulti(const std::wstring_view wide, const int acp = CP_UTF8, const char* const defaultChar = nullptr)
{
	TOut out;
	if (wide.empty())
		return out;

	const auto len = gsl::narrow<int>(wide.length());
	const char* const subst = acp == CP_UTF8 || acp == CP_UTF7 ? nullptr : defaultChar;
	// Both calls must agree on the substitution character, or the measured size
	// is not the size the second call writes.
	const int need = WideCharToMultiByte(acp, 0, wide.data(), len, nullptr, 0, subst, nullptr);
	if (need > 0)
		StringTraits<TOut>::overwrite(out, gsl::narrow<size_t>(need), [&](char* buf, const size_t cap) {
			return gsl::narrow<size_t>(WideCharToMultiByte(acp, 0, wide.data(), len, buf, gsl::narrow<int>(cap), subst, nullptr));
		});
	// Non-empty input never converts to nothing, so empty here is a failure of
	// either call.
	if (StringViewTraits<TOut>::size(out) == 0)
		throw std::exception("WideCharToMultiByte failed");
	return out;
}


std::string StdGetMulti(const std::wstring_view wide, const int acp = CP_UTF8, const char* const defaultChar = nullptr)
{
	return GetMulti(wide, acp, defaultChar);
}


int StdGetCPCode(const std::wstring_view codename)
{
	// Local: the table's row type is a detail of this lookup, not an export.
	struct CodeMap
	{
		int m_Code;
		const wchar_t* m_CodeName;
	};

	// ReSharper disable CppUseDesignatedInitializers
	static constexpr CodeMap map[] =
	{
		{ 37, L"IBM037"}, // IBM EBCDIC US-Canada
		{ 437, L"IBM437"}, // OEM United States
		{ 500, L"IBM500"},// IBM EBCDIC International
		{708, L"ASMO-708"},// Arabic (ASMO 708)
		{709, L"Arabic"},// (ASMO-449+, BCON V4)
		{710, L"Arabic"},// - Transparent Arabic
		{720, L"DOS-720"},// Arabic (Transparent ASMO); Arabic (DOS)
		{737, L"ibm737"},// OEM Greek (formerly 437G); Greek (DOS)
		{775, L"ibm775"},// OEM Baltic; Baltic (DOS)
		{850, L"ibm850"},// OEM Multilingual Latin 1; Western European (DOS)
		{852, L"ibm852"},// OEM Latin 2; Central European (DOS)
		{855, L"IBM855"},// OEM Cyrillic (primarily Russian)
		{857, L"ibm857"},// OEM Turkish; Turkish (DOS)
		{858, L"IBM00858"},// OEM Multilingual Latin 1 + Euro symbol
		{860, L"IBM860"},// OEM Portuguese; Portuguese (DOS)
		{861, L"ibm861"},// OEM Icelandic; Icelandic (DOS)
		{862, L"DOS-862"},// OEM Hebrew; Hebrew (DOS)
		{863, L"IBM863"},// OEM French Canadian; French Canadian (DOS)
		{864, L"IBM864"},// OEM Arabic; Arabic (864)
		{865, L"IBM865"},// OEM Nordic; Nordic (DOS)
		{866, L"cp866"},// OEM Russian; Cyrillic (DOS)
		{869, L"ibm869"},// OEM Modern Greek; Greek, Modern (DOS)
		{870, L"IBM870"},// IBM EBCDIC Multilingual/ROECE (Latin 2); IBM EBCDIC Multilingual Latin 2
		{874, L"windows-874"},// ANSI/OEM Thai (same as 28605, ISO 8859-15); Thai (Windows)
		{875, L"cp875"},// IBM EBCDIC Greek Modern
		{932, L"shift_jis"},// ANSI/OEM Japanese; Japanese (Shift-JIS)
		{936, L"gb2312"},// ANSI/OEM Simplified Chinese (PRC, Singapore); Chinese Simplified (GB2312)
		{949, L"ks_c_5601-1987"},// ANSI/OEM Korean (Unified Hangul Code)
		{949, L"cp949"},// ANSI/OEM Korean (Unified Hangul Code)
		{950, L"big5"},// ANSI/OEM Traditional Chinese (Taiwan; Hong Kong SAR, PRC); Chinese Traditional (Big5)
		{1026,L"IBM1026"},// IBM EBCDIC Turkish (Latin 5)
		{1047,L"IBM01047"},// IBM EBCDIC Latin 1/Open System
		{1140,L"IBM01140"},// IBM EBCDIC US-Canada (037 + Euro symbol); IBM EBCDIC (US-Canada-Euro)
		{1141, L"IBM01141"},// IBM EBCDIC Germany (20273 + Euro symbol); IBM EBCDIC (Germany-Euro)
		{1142, L"IBM01142"},// IBM EBCDIC Denmark-Norway (20277 + Euro symbol); IBM EBCDIC (Denmark-Norway-Euro)
		{1143, L"IBM01143"},// IBM EBCDIC Finland-Sweden (20278 + Euro symbol); IBM EBCDIC (Finland-Sweden-Euro)
		{1144, L"IBM01144"},// IBM EBCDIC Italy (20280 + Euro symbol); IBM EBCDIC (Italy-Euro)
		{1145, L"IBM01145"},// IBM EBCDIC Latin America-Spain (20284 + Euro symbol); IBM EBCDIC (Spain-Euro)
		{1146, L"IBM01146"},// IBM EBCDIC United Kingdom (20285 + Euro symbol); IBM EBCDIC (UK-Euro)
		{1147, L"IBM01147"},// IBM EBCDIC France (20297 + Euro symbol); IBM EBCDIC (France-Euro)
		{1148, L"IBM01148"},// IBM EBCDIC International (500 + Euro symbol); IBM EBCDIC (International-Euro)
		{1149, L"IBM01149"},// IBM EBCDIC Icelandic (20871 + Euro symbol); IBM EBCDIC (Icelandic-Euro)
		{1200, L"utf-16"},// Unicode UTF-16, little endian byte order (BMP of ISO 10646); available only to managed applications
		{1201, L"unicodeFFFE"},// Unicode UTF-16, big endian byte order; available only to managed applications
		{1250, L"windows-1250"},// ANSI Central European; Central European (Windows)
		{1251, L"windows-1251"},// ANSI Cyrillic; Cyrillic (Windows)
		{1251, L"cp1251"},
		{1251, L"cp-1251"},
		{1251, L"cp_1251"},
		{1252, L"windows-1252"},// ANSI Latin 1; Western European (Windows)
		{1253, L"windows-1253"},// ANSI Greek; Greek (Windows)
		{1254, L"windows-1254"},// ANSI Turkish; Turkish (Windows)
		{1255, L"windows-1255"},// ANSI Hebrew; Hebrew (Windows)
		{1256, L"windows-1256"},// ANSI Arabic; Arabic (Windows)
		{1257, L"windows-1257"},// ANSI Baltic; Baltic (Windows)
		{1258, L"windows-1258"},// ANSI/OEM Vietnamese; Vietnamese (Windows)
		{1361, L"Johab"},// Korean (Johab)
		{10000,L"macintosh"},// MAC Roman; Western European (Mac)
		{10001, L"x-mac-japanese"},// Japanese (Mac)
		{10002, L"x-mac-chinesetrad"},// MAC Traditional Chinese (Big5); Chinese Traditional (Mac)
		{10003, L"x-mac-korean"},// Korean (Mac)
		{10004, L"x-mac-arabic"},// Arabic (Mac)
		{10005, L"x-mac-hebrew"},// Hebrew (Mac)
		{10006, L"x-mac-greek"},// Greek (Mac)
		{10007, L"x-mac-cyrillic"},// Cyrillic (Mac)
		{10008, L"x-mac-chinesesimp"},// MAC Simplified Chinese (GB 2312); Chinese Simplified (Mac)
		{10010, L"x-mac-romanian"},// Romanian (Mac)
		{10017, L"x-mac-ukrainian"},// Ukrainian (Mac)
		{10021, L"x-mac-thai"},// Thai (Mac)
		{10029, L"x-mac-ce"},// MAC Latin 2; Central European (Mac)
		{10079, L"x-mac-icelandic"},// Icelandic (Mac)
		{10081, L"x-mac-turkish"},// Turkish (Mac)
		{10082, L"x-mac-croatian"},// Croatian (Mac)
		{12000, L"utf-32"},// Unicode UTF-32, little endian byte order; available only to managed applications
		{12001, L"utf-32BE"},// Unicode UTF-32, big endian byte order; available only to managed applications
		{20000, L"x-Chinese_CNS"},// CNS Taiwan; Chinese Traditional (CNS)
		{20001, L"x-cp20001"},// TCA Taiwan
		{20002, L"x_Chinese-Eten"},// Eten Taiwan; Chinese Traditional (Eten)
		{20003, L"x-cp20003"},// IBM5550 Taiwan
		{20004, L"x-cp20004"},// TeleText Taiwan
		{20005, L"x-cp20005"},// Wang Taiwan
		{20105, L"x-IA5"},// IA5 (IRV International Alphabet No. 5, 7-bit); Western European (IA5)
		{20106, L"x-IA5-German"},// IA5 German (7-bit)
		{20107, L"x-IA5-Swedish"},// IA5 Swedish (7-bit)
		{20108, L"x-IA5-Norwegian"},// IA5 Norwegian (7-bit)
		{20127, L"us-ascii"},// US-ASCII (7-bit)
		{20261, L"x-cp20261"},// T.61
		{20269, L"x-cp20269"},// ISO 6937 Non-Spacing Accent
		{20273, L"IBM273"},// IBM EBCDIC Germany
		{20277, L"IBM277"},//IBM EBCDIC Denmark-Norway
		{20278, L"IBM278"},// IBM EBCDIC Finland-Sweden
		{20280, L"IBM280"},// IBM EBCDIC Italy
		{20284, L"IBM284"},// IBM EBCDIC Latin America-Spain
		{20285, L"IBM285"},// IBM EBCDIC United Kingdom
		{20290, L"IBM290"},// IBM EBCDIC Japanese Katakana Extended
		{20297, L"IBM297"},// IBM EBCDIC France
		{20420, L"IBM420"},// IBM EBCDIC Arabic
		{20423, L"IBM423"},// IBM EBCDIC Greek
		{20424, L"IBM424"},// IBM EBCDIC Hebrew
		{20833, L"x-EBCDIC-KoreanExtended"},// IBM EBCDIC Korean Extended
		{20838, L"IBM-Thai"},// IBM EBCDIC Thai
		{20866, L"koi8-r"},// Russian (KOI8-R); Cyrillic (KOI8-R)
		{20871, L"IBM871"},// IBM EBCDIC Icelandic
		{20880, L"IBM880"},// IBM EBCDIC Cyrillic Russian
		{20905, L"IBM905"},// IBM EBCDIC Turkish
		{20924, L"IBM00924"},// IBM EBCDIC Latin 1/Open System (1047 + Euro symbol)
		{20932, L"EUC-JP"},// Japanese (JIS 0208-1990 and 0121-1990)
		{20936, L"x-cp20936"},// Simplified Chinese (GB2312); Chinese Simplified (GB2312-80)
		{20949, L"x-cp20949"},// Korean Wansung
		{21025, L"cp1025"},// IBM EBCDIC Cyrillic Serbian-Bulgarian
		{21027, L"21027"},// (deprecated)
		{21866, L"koi8-u"},// Ukrainian (KOI8-U); Cyrillic (KOI8-U)
		{28591, L"iso-8859-1"},// ISO 8859-1 Latin 1; Western European (ISO)
		{28592, L"iso-8859-2"},// ISO 8859-2 Central European; Central European (ISO)
		{28593, L"iso-8859-3"},// ISO 8859-3 Latin 3
		{28594, L"iso-8859-4"},// ISO 8859-4 Baltic
		{28595, L"iso-8859-5"},// ISO 8859-5 Cyrillic
		{28596, L"iso-8859-6"},// ISO 8859-6 Arabic
		{28597, L"iso-8859-7"},// ISO 8859-7 Greek
		{28598, L"iso-8859-8"},// ISO 8859-8 Hebrew; Hebrew (ISO-Visual)
		{28599, L"iso-8859-9"},// ISO 8859-9 Turkish
		{28603, L"iso-8859-13"},// ISO 8859-13 Estonian
		{28605, L"iso-8859-15"},// ISO 8859-15 Latin 9
		{29001, L"x-Europa"},// Europa 3
		{38598, L"iso-8859-8-i"},// ISO 8859-8 Hebrew; Hebrew (ISO-Logical)
		{50220, L"iso-2022-jp"},// ISO 2022 Japanese with no halfwidth Katakana; Japanese (JIS)
		{50221, L"csISO2022JP"},// ISO 2022 Japanese with halfwidth Katakana; Japanese (JIS-Allow 1 byte Kana)
		{50222, L"iso-2022-jp"},// ISO 2022 Japanese JIS X 0201-1989; Japanese (JIS-Allow 1 byte Kana - SO/SI)
		{50225, L"iso-2022-kr"},// ISO 2022 Korean
		{50227, L"x-cp50227"},// ISO 2022 Simplified Chinese; Chinese Simplified (ISO 2022)
		{50229, L"ISO"},// 2022 Traditional Chinese
		{50930, L"EBCDIC"},// Japanese (Katakana) Extended
		{50931, L"EBCDIC"},// US-Canada and Japanese
		{50933, L"EBCDIC"},// Korean Extended and Korean
		{50935, L"EBCDIC"},// Simplified Chinese Extended and Simplified Chinese
		{50936, L"EBCDIC"},// Simplified Chinese
		{50937, L"EBCDIC"},// US-Canada and Traditional Chinese
		{50939, L"EBCDIC"},// Japanese (Latin) Extended and Japanese
		{51932, L"euc-jp"},// EUC Japanese
		{51936, L"EUC-CN"},// EUC Simplified Chinese; Chinese Simplified (EUC)
		{51949, L"euc-kr"},// EUC Korean
		{51950, L"EUC"},// Traditional Chinese
		{52936, L"hz-gb-2312"},// HZ-GB2312 Simplified Chinese; Chinese Simplified (HZ)
		{54936, L"GB18030"},// Windows XP and later: GB18030 Simplified Chinese (4 byte); Chinese Simplified (GB18030)
		{57002, L"x-iscii-de"},// ISCII Devanagari
		{57003, L"x-iscii-be"},// ISCII Bengali
		{57004, L"x-iscii-ta"},// ISCII Tamil
		{57005, L"x-iscii-te"},// ISCII Telugu
		{57006, L"x-iscii-as"},// ISCII Assamese
		{57007, L"x-iscii-or"},// ISCII Oriya
		{57008, L"x-iscii-ka"},// ISCII Kannada
		{57009, L"x-iscii-ma"},// ISCII Malayalam
		{57010, L"x-iscii-gu"},// ISCII Gujarati
		{57011, L"x-iscii-pa"},// ISCII Punjabi
		{65000, L"utf-7"},// Unicode (UTF-7)
		{65001, L"utf-8"},// Unicode (UTF-8)
	};
	// ReSharper restore CppUseDesignatedInitializers
	if (codename.empty())
		return CP_UTF8;
	std::wstring code(codename);
	tgit::wstr::MakeLower(code);
	for (const auto &[m_Code, m_CodeName] : map)
	{
		std::wstring str(m_CodeName);
		tgit::wstr::MakeLower(str);

		if (str == code)
			return m_Code;
	}

	return CP_UTF8;
}

std::string StdGetUTF8(const std::wstring_view wide)
{
	return StdGetMulti(wide, CP_UTF8);
}

// Thin adapters over the above, kept while call sites move across. They
// used to carry their own copy of the WideCharToMultiByte /
// MultiByteToWideChar dance, which meant a fix to one encoding edge case
// had two places to land and no way to notice the second. Delete each of
// these once nothing calls it; nothing here needs porting first.
//
// The result type is a template parameter the caller spells -
// GetUnicode<CString>(msg), GetUTF8<CStringA>(path) - because the call
// sites need a real CStringT back (they pass it to a const char* parameter,
// call .GetBuffer() on it, or operator+ it onto a wide literal) and this
// module does not know which CStringT the caller's flavor is. The explicit
// <CString> is also the tell that a call site has not migrated yet, the
// same job tgit::wstr::StringView does on the way in.
//
// Inputs are taken as std views by the cores above; a CString reaches those
// through the IsStupidString overloads below, which cost an O(1) View rather
// than the O(n) wcslen a std::wstring_view(LPCWSTR) would. A const char* or a
// std::string lands on the std::string_view core directly - as before, a
// nullptr there is undefined, exactly as it was through
// std::string_view(string).
//
// The forwarders are constrained to IsStupidString on purpose: a CStringT
// cannot reach a std view implicitly (that would be two user-defined
// conversions), so without them it has no overload at all, and a StringView -
// which is a real string - must keep binding to the core rather than
// re-entering a forwarder forever.
template <TStrOfChar TOut, TStrOfWide TIn> requires IsStupidString<TIn>
TOut GetMulti(const TIn& string, const int acp = CP_UTF8, const char* const defaultChar = nullptr)
{
	return GetMulti<TOut>(StringView{ string }, acp, defaultChar);
}


template <TStrOfChar TOut, TStrOfWide TIn>
TOut GetUTF8(const TIn& string)
{
	const auto string_view = StringView{ string };
	return GetMulti<TOut>(string_view, CP_UTF8);
}


template <TStrOfWide TOut, TStrOfChar TIn>
TOut GetUnicode(const TIn& inStr)
{
	const auto fromUtf8 = ::winrt::to_hstring(std::string_view{ StringView{ inStr } });
	const std::wstring_view wide = fromUtf8;
	return TOut(wide.data(), StringViewTraits<std::wstring_view>::size<TOut>(wide));
}


/// Decodes \a bytes as UTF-8 text, stopping at the first NUL - what
/// CGitByteArray::Decode did, for a buffer held as a std::string. git's own
/// output is frequently NUL-separated (`-z`), so this yields only the first
/// record of such a buffer; that truncation is the contract, and the name says so.
std::wstring DecodeUtf8UpToNul(const std::string_view bytes)
{
	return GetUnicode<std::wstring>(bytes.substr(0, bytes.find('\0')));
}


// Convert2*: the verbs for owned inputs - anything with StringTraits,
// including a NUL-terminated pointer, which has no view of its own. The one
// O(n) step on our side is finding a pointer's length; after that it is the
// fast path, where MultiByteToWideChar / WideCharToMultiByte are the oracle
// and everything around them is O(1). A std string or CString lands here too,
// for a call site that would rather not care which kind it holds.
template <TStrOfWide TOut, typename TIn> requires HasStringTraitsOf<TOwnedKey<TIn>, char>
TOut Convert2Unicode(const TIn& inStr)
{
	using traits = StringTraits<TOwnedKey<TIn>>;
	return GetUnicode<TOut>(std::string_view{ traits::data(inStr), traits::size(inStr) });
}

template <TStrOfChar TOut, typename TIn> requires HasStringTraitsOf<TOwnedKey<TIn>, wchar_t>
TOut Convert2Multibyte(const TIn& inStr)
{
	using traits = StringTraits<TOwnedKey<TIn>>;
	return GetUTF8<TOut>(std::wstring_view{ traits::data(inStr), traits::size(inStr) });
}


template <TStrOfWide TOut = std::wstring>
TOut StdGetUnicode(const std::string_view multibyte, const int acp)
{
	TOut out;
	if (multibyte.empty())
		return out;

	const auto s_len = gsl::narrow<int>(multibyte.length());
	const int need = MultiByteToWideChar(acp, 0, multibyte.data(), s_len, nullptr, 0);
	if (need > 0)
		StringTraits<TOut>::overwrite(out, gsl::narrow<size_t>(need), [&](wchar_t* buf, const size_t cap) {
			return gsl::narrow<size_t>(MultiByteToWideChar(acp, 0, multibyte.data(), s_len, buf, gsl::narrow<int>(cap)));
		});
	if (StringViewTraits<TOut>::size(out) == 0)
		throw std::exception("MultiByteToWideChar failed");
	return out;
}



// The ANSI code page pair, for the half of IContextMenu that is still ANSI by
// interface: GetCommandString's GCS_HELPTEXTA / GCS_VERBA, and InvokeCommand's
// lpVerb. CP_ACP is the contract there, not an oversight, which is the only
// reason these are not plain StdGetUTF8 / StdGetUnicode calls.
//
// The "." is the substitution byte for anything the current ACP cannot
// represent. The old bodies sized their own buffer at len * 3 and len * 2 and
// trusted the guess; StdGetMulti / StdGetUnicode ask the API for the size
// instead, so an ACP that needs more bytes per character truncates here no
// longer, and a failed conversion throws rather than silently returning a
// prefix.
std::string WideToMultibyte(const std::wstring_view wide)
{
	return StdGetMulti(wide, CP_ACP, ".");
}

std::wstring MultibyteToWide(const std::string_view multibyte)
{
	return StdGetUnicode(multibyte, CP_ACP);
}


}


// ---- StringUtils: the UI-text and file helpers ----------------------------------
// Folded in from the StringUtils module. Qualified-only now: the module's
// `export using namespace StringUtils;` is gone, so a call site spells
// StringUtils::X (or tgit::wstr::X for the CString-contract bridges above).
//
// Not exported: the names this namespace uses unqualified.
namespace StringUtils
{
using tgit::wstr::CString;
}


// Functions std:: already does. Each names its std:: spelling; they stay only
// until their call sites move to it.
export namespace StringUtils
{

// std: std::views::split (+ std::from_chars for numeric containers)
// append = true as the default: a default value should never lose data!
template <typename Container, IsRealString StrT, typename CHARTYPE = StrT::value_type>
void stringtok(
	Container& container,
	const StrT& in,
	const bool trim,
	const CHARTYPE* const delimiters,
	const bool append = true
)
{
	const auto len = in.length();
	decltype(in.length()) i = 0;
	if (!append)
		container.clear();

	while (i < len)
	{
		if (trim)
		{
			// eat leading whitespace
			i = in.find_first_not_of(delimiters, i);
			if (i == StrT::npos)
				return; // nothing left but white space
		}

		// find the end of the token
		const auto j = in.find_first_of(delimiters, i);

		// push token
		if (j == StrT::npos)
		{
			if constexpr (std::is_same_v<typename Container::value_type, StrT>)
				container.push_back(in.substr(i));
			else if constexpr (std::is_same_v<CHARTYPE, wchar_t>)
				container.push_back(static_cast<Container::value_type>(_wtoi64(in.substr(i).c_str())));
			else if constexpr (std::is_same_v<CHARTYPE, char>)
				container.push_back(static_cast<Container::value_type>(_atoi64(in.substr(i).c_str())));
			else
				static_assert(false);
			return;
		}
		else
		{
			if constexpr (std::is_same_v<typename Container::value_type, StrT>)
				container.push_back(in.substr(i, j - i));
			else if constexpr (std::is_same_v<CHARTYPE, wchar_t>)
				container.push_back(static_cast<Container::value_type>(_wtoi64(in.substr(i, j - i).c_str())));
			else if constexpr (std::is_same_v<CHARTYPE, char>)
				container.push_back(static_cast<Container::value_type>(_atoi64(in.substr(i, j - i).c_str())));
			else
				static_assert(false);
		}

		// set up for next loop
		i = j + 1;
	}
}


// Every line ending - \r\n, a lone \r, a lone \n - becomes \r\n.
// (It took `const StringTraits<TStr>&`, where TStr cannot be deduced, so no call
// could reach it.)
CString EnsureCRLF(const std::wstring_view text)
{
	static const std::wregex lineEnd(L"\r\n|\r|\n");
	const std::wstring out = std::regex_replace(std::wstring(text), lineEnd, L"\r\n");
	return CString(out.data(), gsl::narrow<int>(out.size()));
}
template <IsStupidString TStr>
CString EnsureCRLF(const TStr& text)
{
	return EnsureCRLF(std::wstring_view(StringView{ text }));
}


/**
 * Find and return the number n of starting characters equal between
 * \ref lhs and \ref rhs. (max n: lhs.Left(n) == rhs.Left(n))
 */
// std: std::ranges::mismatch(lhs, rhs).in1 - lhs.begin()
int GetMatchingLength (const CString& lhs, const CString& rhs)
{
	const int lhsLength = lhs.GetLength();
	const int rhsLength = rhs.GetLength();
	const int maxResult = std::min(lhsLength, rhsLength);

	const LPCWSTR pLhs = lhs;
	const LPCWSTR pRhs = rhs;

	for (int i = 0; i < maxResult; ++i)
		if (pLhs[i] != pRhs[i])
			return i;

	return maxResult;
}


// std: std::ranges::all_of(text, [](wchar_t c) { return c >= 32 && c < 127; })
bool IsPlainReadableASCII(const CString& text)
{
	for (int i = 0; i < text.GetLength(); ++i)
	{
		if (text[i] < 32 || text[i] >= 127)
			return false;
	}
	return true;
}


// std: basic_string_view::starts_with / ends_with
bool StartsWith(const wchar_t* heystack, const CString& needle)
{
	return wcsncmp(heystack, needle, needle.GetLength()) == 0;
}

bool StartsWith(const char* heystack, const char* needle)
{
	return strncmp(heystack, needle, strlen(needle)) == 0;
}

bool EndsWith(const CString& heystack, const wchar_t* needle)
{
	const auto lenNeedle = wcslen(needle);
	const auto lenHeystack = static_cast<size_t>(heystack.GetLength());
	if (lenNeedle > lenHeystack)
		return false;
	return wcsncmp(static_cast<LPCWSTR>(heystack) + (lenHeystack - lenNeedle), needle, lenNeedle) == 0;
}

bool EndsWith(const CString& heystack, const wchar_t needle)
{
	const auto lenHeystack = heystack.GetLength();
	if (!lenHeystack)
		return false;
	return *(static_cast<LPCWSTR>(heystack) + (lenHeystack - 1)) == needle;
}

// std::wstring / std::wstring_view variants. Constrained rather than taking a
// plain std::wstring_view haystack: that would make EndsWith(L"literal", ...)
// ambiguous against the CString overloads (one user-defined conversion each).
template <IsRealString TStr>
	requires std::same_as<typename TStr::value_type, wchar_t>
bool EndsWith(const TStr& heystack, const std::wstring_view needle) noexcept
{
	return std::wstring_view(heystack).ends_with(needle);
}

template <IsRealString TStr>
	requires std::same_as<typename TStr::value_type, wchar_t>
bool EndsWith(const TStr& heystack, const wchar_t needle) noexcept
{
	return std::wstring_view(heystack).ends_with(needle);
}


// std: std::replace(buffer, buffer + length, L'|', L'\0')
void PipeToNull(wchar_t* ptr)
{
	if (*ptr == '|')
		*ptr = '\0';
}


/**
 * Replace all pipe (|) character in the string with a nullptr character. Used
 * for passing into Win32 functions that require such representation
 */
void PipesToNulls(wchar_t* buffer, const size_t length)
{
	wchar_t* ptr = buffer + length;
	while (ptr != buffer)
	{
		PipeToNull(ptr);
		ptr--;
	}
}

/// Up to the terminating NUL. This was a default argument `length = 0` on the
/// overload above, which made every one-argument call a no-op: that loop starts
/// at buffer + 0 and stops at buffer. TortoiseSVN had the two as overloads.
void PipesToNulls(wchar_t* buffer)
{
	std::replace(buffer, buffer + wcslen(buffer), L'|', L'\0');
}


// std: view.substr(view.find_first_not_of(...)) / view.substr(0, view.find_last_not_of(...) + 1)
void TrimLeft(std::string_view& view)
{
	while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front())))
		view.remove_prefix(1);
}


template <tgit::str::IsTCompString TStr>
TStr& TrimRight(TStr& obj, const std::optional<std::basic_string_view<typename StringViewTraits<TStr>::char_type>> sbj = std::nullopt)
{
	auto v = StringView<TStr>(obj).as_view();
	while (!v.empty())
	{
		const auto chr = v.back();
		const bool is_it_still = sbj ? sbj->find(chr) != sbj->npos
									 : is_space(chr);
		if (!is_it_still) break;
		v.remove_suffix(1);
	}
	if constexpr (requires { StringTraits<TStr>::resize(obj, v.size()); })
		StringTraits<TStr>::resize(obj, v.size());   // an owning string: shrink it
	else
		obj = obj.substr(0, v.size());               // a view: narrow it, there is nothing to resize
	return obj;   // CStringT::TrimRight returns *this; keeps chaining call sites compiling
}


/**
 * Reads the string \text from the file \path in utf8 encoding.
 */
// Win32 rather than CStdioFile, which is MFC-only: this module is compiled in
// both flavors. Bytes are read raw (binary - text mode drops some \r) and
// decoded as UTF-8.
bool ReadStringFromTextFile(const CString& path, CString& text)
{
	const wil::unique_hfile hFile{ CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
	if (!hFile)
		return false;

	LARGE_INTEGER size{};
	if (!GetFileSizeEx(hFile.get(), &size) || size.QuadPart >= INT_MAX)
		return false;

	std::string buf(static_cast<size_t>(size.QuadPart), '\0');
	DWORD bytesread = 0;
	if (!ReadFile(hFile.get(), buf.data(), static_cast<DWORD>(buf.size()), &bytesread, nullptr))
		return false;
	buf.resize(bytesread);

	text = CUnicodeUtils::GetUnicode<CString>(std::string_view{ buf });
	return true;
}


/**
 * Writes the string \text to the file \path, either in utf16 or utf8 encoding,
 * depending on the \c bUTF8 param.
 */
// std: std::ofstream(path, std::ios::binary) + CUnicodeUtils, modulo FILE_SHARE_DELETE
bool WriteStringToTextFile(const std::wstring_view path, const std::wstring_view text, const bool bUTF8 = true)
{
	DWORD dwWritten = 0;
	const wil::unique_hfile hFile{ CreateFile(path.data(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
	if (!hFile)
		return false;

	if (bUTF8)
	{
		const std::string buf = CUnicodeUtils::StdGetUTF8(text);
		if (!WriteFile(hFile.get(), buf.c_str(), static_cast<DWORD>(buf.length()), &dwWritten, nullptr))
		{
			return false;
		}
	}
	else
	{
		if (!WriteFile(hFile.get(), text.data(), static_cast<DWORD>(text.length() * sizeof(wchar_t)), &dwWritten, nullptr))
		{
			return false;
		}
	}
	return true;
}
bool WriteStringToTextFile(const LPCWSTR path, const LPCWSTR text, const bool bUTF8 = true)
{
	return WriteStringToTextFile(std::wstring_view{path}, std::wstring_view{text}, bUTF8);
}

}

// Functionality Splitter

// The extras: what std:: has no spelling for - Win32 UI text, case-insensitive
// compare, git's From: convention, and command-line quoting. (The clipboard
// writers went to wil::SetClipboardText.)
export namespace StringUtils
{

/**
 * Removes all '&' chars from a string.
 */
void RemoveAccelerators(CString& text)
{
	int pos = 0;
	while ((pos=text.Find('&',pos))>=0)
	{
		if (text.GetLength() > (pos-1))
		{
			if (text.GetAt(pos+1)!=' ')
				text.Delete(pos);
		}
		++pos;
	}
}

/**
 * Escapes all '&' chars from a string with another '&'.
 */
CString EscapeAccellerators(CString& text)
{
	text.Replace(L"&", L"&&");
	return text;
}

/**
 * Returns the accellerator used in the string or \0
 */
wchar_t GetAccellerator(const CString& text)
{
	int pos = 0;
	while ((pos = text.Find(L'&', pos)) >= 0 && pos + 1 < text.GetLength())
	{
		if (text.GetAt(pos + 1) == '&')
			++pos;
		else if (text.GetAt(pos + 1) != ' ')
			return towupper(text.GetAt(pos + 1));
		++pos;
	}
	return L'\0';
}


CString WordWrap(const CString& longstring, int limit, const bool bCompactPaths, const bool bForceWrap, const int tabSize)
{
	const int nLength = longstring.GetLength();
	CString retString;

	if (limit < 0)
		limit = 0;

	int nLineStart = 0;
	int nLineEnd = 0;
	int tabOffset = 0;
	for (int i = 0; i < nLength; ++i)
	{
		if (i-nLineStart+tabOffset >= limit)
		{
			if (nLineEnd == nLineStart)
			{
				if (bForceWrap)
					nLineEnd = i;
				else
				{
					while ((i < nLength) && (longstring[i] != ' ') && (longstring[i] != '\t'))
						++i;
					nLineEnd = i;
				}
			}
			if (bCompactPaths)
			{
				CString longline = longstring.Mid(nLineStart, nLineEnd-nLineStart).Left(MAX_PATH-1);
				if (longline.GetLength() < MAX_PATH)
				{
					if (((!PathIsFileSpec(longline))&&longline.Find(':')<3)||(PathIsURL(longline)))
					{
						auto buf = std::array<wchar_t, MAX_PATH>{};
						PathCompactPathEx(buf.data(), longline, limit+1, 0);
						longline = buf.data();
					}
				}
				retString += longline;
			}
			else
				retString += longstring.Mid(nLineStart, nLineEnd-nLineStart);
			retString += L'\n';
			tabOffset = 0;
			nLineStart = nLineEnd;
		}
		if (longstring[i] == ' ')
			nLineEnd = i;
		if (longstring[i] == '\t')
		{
			tabOffset += (tabSize - i % tabSize);
			nLineEnd = i;
		}
	}
	if (bCompactPaths)
	{
		CString longline = longstring.Mid(nLineStart).Left(MAX_PATH-1);
		if (longline.GetLength() < MAX_PATH)
		{
			if (((!PathIsFileSpec(longline))&&longline.Find(':')<3)||(PathIsURL(longline)))
			{
				auto buf = std::array<wchar_t, MAX_PATH>{};
				PathCompactPathEx(buf.data(), longline, limit+1, 0);
				longline = buf.data();
			}
		}
		retString += longline;
	}
	else
		retString += longstring.Mid(nLineStart);

	return retString;
}

std::vector<CString> WordWrap(const CString& longstring, int limit, const int tabSize)
{
	const int nLength = longstring.GetLength();
	std::vector<CString> retVec;

	if (limit < 0)
		limit = 0;

	int nLineStart = 0;
	int nLineEnd = 0;
	int tabOffset = 0;
	for (int i = 0; i < nLength; ++i)
	{
		if (i - nLineStart + tabOffset >= limit)
		{
			if (nLineEnd == nLineStart)
				nLineEnd = i;

			auto sMid = longstring.Mid(nLineStart, nLineEnd - nLineStart);
			retVec.push_back(sMid);

			tabOffset = 0;
			nLineStart = nLineEnd;
		}
		if (longstring[i] == ' ')
			nLineEnd = i;
		if (longstring[i] == '\t')
		{
			tabOffset += (tabSize - i % tabSize);
			nLineEnd = i;
		}
	}

	const auto sMid = longstring.Mid(nLineStart);
	retVec.push_back(sMid);

	return retVec;
}


CString LinesWrap(const CString& longstring, const int limit = 80, const bool bCompactPaths = true)
{
	CString retString;
	if ((longstring.GetLength() < limit) || (limit == 0))
		return longstring;  // no wrapping needed.
	// now start breaking the string into lines

	int linepos = 0;
	int lineposold = 0;
	CString temp;
	while ((linepos = longstring.Find('\n', linepos)) >= 0)
	{
		temp = longstring.Mid(lineposold, linepos-lineposold);
		if ((linepos+1)<longstring.GetLength())
			++linepos;
		else
			break;
		lineposold = linepos;
		if (!retString.IsEmpty())
			retString += L'\n';
		retString += WordWrap(temp, limit, bCompactPaths, false, 4);
	}
	temp = longstring.Mid(lineposold);
	if (!temp.IsEmpty())
		retString += L'\n';
	retString += WordWrap(temp, limit, bCompactPaths, false, 4);
	retString.Trim();
	return retString;
}


/**
 * Optimizing wrapper around CompareNoCase.
 */
int FastCompareNoCase (const CString& lhs, const CString& rhs)
{
	// attempt latin-only comparison

	const INT_PTR count = std::min(lhs.GetLength(), rhs.GetLength()+1);
	const wchar_t* left = lhs;
	const wchar_t* right = rhs;
	for (const wchar_t* last = left + count+1; left < last; ++left, ++right)
	{
		int leftChar = *left;
		int rightChar = *right;

		if (int diff = leftChar - rightChar; diff != 0)
		{
			// case-sensitive comparison found a difference

			if ((leftChar | rightChar) >= 0x80)
			{
				// non-latin char -> fall back to CRT code
				// (full comparison required as we might have
				// skipped special chars / UTF plane selectors)

				return _wcsicmp (lhs, rhs);
			}

			// normalize to lower case

			if ((leftChar >= 'A') && (leftChar <= 'Z'))
				leftChar += 'a' - 'A';
			if ((rightChar >= 'A') && (rightChar <= 'Z'))
				rightChar += 'a' - 'A';

			// compare again

			diff = leftChar - rightChar;
			if (diff != 0)
				return diff;
		}
	}

	// must be equal (both ended with a 0)

	return 0;
}


// Case-insensitive prefix/suffix tests: std:: has no locale-free spelling.
bool StartsWithI(const wchar_t* heystack, const CString& needle)
{
	return _wcsnicmp(heystack, needle, needle.GetLength()) == 0;
}

bool EndsWithI(const CString& heystack, const wchar_t* needle)
{
	const auto lenNeedle = wcslen(needle);
	const auto lenHeystack = static_cast<size_t>(heystack.GetLength());
	if (lenNeedle > lenHeystack)
		return false;
	return _wcsnicmp(static_cast<LPCWSTR>(heystack) + (lenHeystack - lenNeedle), needle, lenNeedle) == 0;
}

// Constrained for the same reason as the EndsWith templates above.
template <IsRealString TStr>
	requires std::same_as<typename TStr::value_type, wchar_t>
bool EndsWithI(const TStr& heystack, const std::wstring_view needle) noexcept
{
	const std::wstring_view h(heystack);
	return h.size() >= needle.size() && _wcsnicmp(h.data() + (h.size() - needle.size()), needle.data(), needle.size()) == 0;
}


struct EmailAddress
{
	std::wstring address;
	std::wstring name;
};

/**
 * Splits a From:-style string into address and display name, best effort.
 *
 * Handles what git's mailinfo.c handle_from() does, which this used to be a
 * line-by-line port of:
 *   John Doe <john.doe@xz>                   "John Doe" <john.doe@xz>
 *   john.doe@xz (John Doe)                   <john.doe@xz> "John Doe"
 *   John (zzz) Doe <john.doe@xz> (Comment)   John Doe <johndoe>   (no '@')
 * A leading quoted name is taken verbatim, with \" unescaped. Any other name
 * falls back to the address when it does not look like a name - under 3 or
 * over 60 characters, or containing @, < or > - which is git's get_sane_name().
 * Not an RFC 5322 parser: no groups, no encoded words, no nested comments.
 */
EmailAddress ParseEmailAddress(const std::wstring_view input)
{
	static const std::wregex quotedName(LR"re(^\s*"((?:[^"\\]|\\.)*)"([\s\S]*)$)re");
	static const std::wregex escapedQuote(LR"(\\")");
	static const std::wregex atToken(LR"(<?([^\s<]*@[^\s>]*)>?)");
	static const std::wregex bracketed(LR"(^([\s\S]*?)<([^>]*)>)");
	static const std::wregex whitespaceRun(LR"(\s+)");

	const auto trim = [](std::wstring s) {
		tgit::wstr::Trim(s);
		return s;
	};
	const auto saneName = [](std::wstring name, const std::wstring& address) -> std::wstring {
		if (name.size() >= 3 && name.size() <= 60 && name.find_first_of(L"@<>") == std::wstring::npos)
			return name;
		return address;
	};

	EmailAddress result;
	std::wstring rest = trim(std::wstring(input));
	if (rest.empty())
		return result;

	std::wstring quoted;
	if (std::wsmatch m; std::regex_match(rest, m, quotedName))
	{
		quoted = std::regex_replace(m[1].str(), escapedQuote, L"\"");
		rest = m[2].str();
	}

	if (std::wsmatch m; std::regex_search(rest, m, atToken))
	{
		result.address = m[1].str();
		rest = m.prefix().str() + L' ' + m.suffix().str();
	}
	else if (std::regex_search(rest, m, bracketed))
	{
		// "John Doe <johndoe>": an address without an '@'
		result.address = m[2].str();
		rest = m[1].str();
	}
	else
		return { .address = {}, .name = quoted };

	if (!quoted.empty())
	{
		result.name = std::move(quoted);
		return result;
	}

	// what is left is the name, possibly wrapped in () or "" (cases "john.doe@xz (John Doe)" and "<john.doe@xz> \"John Doe\"")
	rest = trim(std::regex_replace(rest, whitespaceRun, L" "));
	if (rest.size() >= 2 && ((rest.front() == L'(' && rest.back() == L')') || (rest.front() == L'"' && rest.back() == L'"')))
		rest = rest.substr(1, rest.size() - 2);
	result.name = saneName(std::move(rest), result.address);
	return result;
}


/**
 * Expands placeholders to corresponding values provided in the map replacements.
 * This method makes sure that values cannot contain other placeholders that may be replaced later on.
 */
CString ExpandPlaceholdersForCmd(const CString& input, const std::map<CString, CString>& values, CString (*escaper)(CString))
{
	CString result;
	const int len = input.GetLength();
	for (int i = 0; i < len; ++i)
	{
		if (input[i] != L'%')
		{
			result += input[i];
			continue;
		}

		const int start = i + 1;
		int j = start;
		while (j < len && std::iswalnum(input[j]))
			++j;

		if (j == start)
		{
			result += L'%';
			continue;
		}

		CString key = input.Mid(start, j - start);
		if (auto it = values.find(key); it != values.end())
		{
			if (i > 0 && input.GetAt(i - 1) == L'"' && j < len && input.GetAt(j) == L'"')
			{
				result.Truncate(result.GetLength() - 1);
				++i;
			}
			result += escaper(it->second);
			i += j - start;
			continue;
		}

		result += L'%';
		result += key;

		i = j - 1;
	}

	return result;
}


/**
 * Quotes one argv element the way CommandLineToArgvW / the MSVC runtime read
 * it back. Always adds the surrounding quotes; see \ref NeedsWindowsCliQuoting
 * for the "only when it matters" test the argv serializer applies first.
 *
 * Flavor-neutral on purpose: this is the building block CGit::SerializeArgv
 * needs, and CGit is on the list of sources the ATL side compiles too.
 */
// algorithm based on https://learn.microsoft.com/en-gb/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
// also cf. https://learn.microsoft.com/en-us/cpp/cpp/main-function-command-line-args
std::wstring EscapeWindowsCliArgument(const std::wstring_view argument)
{
	std::wstring result;
	result.reserve(argument.size() + 2);
	result += L'"';

	const size_t length = argument.size();

	for (size_t i = 0;; ++i)
	{
		size_t numberBackslashes = 0;

		// Count consecutive backslashes
		while (i < length && argument[i] == L'\\')
		{
			++i;
			++numberBackslashes;
		}

		// End of string
		if (i >= length)
		{
			// Escape all backslashes, but let the terminating double quotation mark we add below be interpreted as a metacharacter.
			result.append(numberBackslashes * 2, L'\\');
			break;
		}
		else if (argument[i] == L'"') // Escape all backslashes and the following double quotation mark.
			result.append(numberBackslashes * 2 + 1, L'\\');
		else // Backslashes aren't special here.
			result.append(numberBackslashes, L'\\');
		result += argument[i];
	}

	result += L'"';

	return result;
}


/**
 * True when \a argument would not survive the round trip unquoted. An empty
 * element must be quoted (otherwise it vanishes); whitespace and quotes are
 * the only other characters CommandLineToArgvW treats as structure.
 *
 * A lone backslash needs nothing: backslashes are only special immediately
 * before a quote, and there is no quote here to be before.
 */
bool NeedsWindowsCliQuoting(const std::wstring_view argument)
{
	// An empty element has to be quoted, or it disappears from the round trip.
	return argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
}


CString EscapeWindowsCliArguments(const CString& argument)
{
	const std::wstring escaped = EscapeWindowsCliArgument(std::wstring_view(argument, argument.GetLength()));
	return {escaped.data(), static_cast<int>(escaped.size())};
}

}


// UnescapeGitQuotePath(A), EscapePosixShellArgument and NeedsPosixShellQuoting
// were parked here unexported; git_h (TGitH) owns them now.