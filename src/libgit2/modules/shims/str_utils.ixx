module;
#include <windows.h>
#ifdef TGIT_MFC
#include <afx.h>
#endif
#include <atlstr.h>


export module wstr;
import std;
import gsl;
import wil;


export namespace tgit::zstr
{

/// std::basic_string and std::basic_string_view, by shape.
template <typename TStr>
concept IsRealString = requires(const TStr& s, std::size_t l) {
	typename TStr::value_type;
	typename TStr::const_pointer;
//	typename TStr::traits_type;
	{ s.data() } -> std::convertible_to<typename TStr::const_pointer>;
	{ s.size() } -> std::convertible_to<size_t>;
};

// The shape of CStringT<XCHAR, StrTraits> and nothing ATL-specific: this module
template <typename TStr>
concept IsStupidString = requires(const TStr& s, std::size_t l) {
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
	static constexpr int          rsize(const TStr& s) noexcept { return gsl::narrow_cast<int>(s.size()); }

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
	static constexpr int          rsize(const TStr& s) noexcept { return s.GetLength(); }

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
using TCharType = StringViewTraits<TStr>::char_type;

template <typename TStr>
using TViewType = std::basic_string_view<typename StringViewTraits<TStr>::char_type>;


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

	constexpr auto                 copy_as_std() {
		return std::basic_string<typename cnv_view_t::char_type>{this->data(), this->data() + this->size()};
	}
	constexpr auto    as_view()    noexcept { return std_view_t{this->data(), this->size()}; }

	constexpr static cnv_view_t    as_traits(const TStr& s)  noexcept { return cnv_view_t{cnv_view_t::data(s), cnv_view_t::size(s)}; }
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
using namespace tgit::zstr;


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
// ::CStringW, not ATL::CStringW: under _AFX (TGIT_MFC) atlstr.h does not declare
// ATL::CStringW and afxstr.h declares the MFC one globally; without _AFX the ATL one
// reaches global scope through atldef's `using namespace ATL`. Either way this is
// the flavor's CString.
using CString = ::CStringW;




/// Whitespace CString::Trim() removes, so a converted call site keeps behaving.
inline constexpr std::wstring_view kDefaultTrimChars = L" \t\r\n";


struct CodeMap
{
	int m_Code;
	const wchar_t* m_CodeName;
};



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
using namespace tgit::wstr;


export namespace tgit::str8 {
	using CStringA = ::CStringA;	// the flavor's narrow CString, see tgit::wstr::CString
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
// Explicit specializations for the ATL CStrings, not the partial one constrained
// on IsStupidString this used to be. That one was never usable outside this
// module: MSVC did not find it from an importer (std::formattable<CString,
// wchar_t> was false in StringUtils, which imports wstr), and its body built a
// TCharType - one wchar_t - from the whole string, so it had never been
// instantiated anywhere either. They name the flavor's CStrings (::CStringW and
// ::CStringA, see tgit::wstr::CString), so the TGIT_MFC build of this module
// specializes for the MFC types and the ATL build for the ATL ones. Not
// `export`ed: a specialization of a std template is reached through the primary.
template <>
struct std::formatter<::CStringW, wchar_t> : std::formatter<std::wstring_view, wchar_t>
{
	auto format(const ::CStringW& value, auto& ctx) const
	{
		return std::formatter<std::wstring_view, wchar_t>::format(std::wstring_view(value.GetString(), value.GetLength()), ctx);
	}
};


template <>
struct std::formatter<::CStringA, char> : std::formatter<std::string_view, char>
{
	auto format(const ::CStringA& value, auto& ctx) const
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

int SafeIntMult(const int val1, const int val2)
{
	const auto bufferSize = std::multiplies<size_t>{}(val1, val2);
	if (bufferSize > std::numeric_limits<int>::max())
		throw std::overflow_error("int multiplication result too big for an int.");
	return gsl::narrow<int>(bufferSize);
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
	MakeLower(code);
	for (const auto &[m_Code, m_CodeName] : map)
	{
		std::wstring str(m_CodeName);
		MakeLower(str);

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
	const auto fromUtf8 = wil::winrt::to_hstring(std::string_view{ StringView{ inStr } });
	const std::wstring_view wide = fromUtf8;
	return TOut(wide.data(), StringViewTraits<std::wstring_view>::size<TOut>(wide));
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