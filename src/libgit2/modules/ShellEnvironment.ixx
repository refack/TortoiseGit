module;
#include <processenv.h>
#include <tgitlib/sanity.h>

//
// Created by refac on 22/10/2026.
//

export module ShellEnvironment;
import std;
import gsl;
import wil;
import str_utils;

namespace fs = std::filesystem;


/// Ordering for environment variable names. Windows resolves them
/// case-insensitively - PATH and Path are one variable - so that has to be a
/// property of the container rather than something every lookup remembers to do.
/// It also gives the serialized block the case-insensitive name order Windows'
/// own environment blocks come in, which the flat block lost the moment anything
/// was added to it.
struct EnvNameLess {
	using is_transparent = void;

	[[nodiscard]] bool operator()(const std::wstring_view lhs, const std::wstring_view rhs) const noexcept {
		return tgit::wstr::CompareNoCase(lhs, rhs) < 0;
	}
};


constexpr auto Z3_EXTREME_BOUND_ON_SIZE = 1U << 23U;
constexpr auto KEY_VALUE_SEPARATOR = L"="sv;
constexpr auto ENTRY_SEPARATOR = L"\0"sv;
constexpr auto PATH_DELIMITER = L";"sv;


/**
 * The environment handed to CreateProcess and to gitdll.
 *
 * It is a map of name to value, because that is what an environment is. It used
 * to be the serialized form - a std::vector<wchar_t> of "name=value\0...\0\0" -
 * with every lookup a linear scan that re-parsed it, and SetEnv splicing
 * characters in and out of the middle by iterator.
 *
 * Three properties of the flat form are load-bearing and are preserved:
 *
 *  - `operator const LPWSTR*` returns the address of a member, not of the block.
 *    gitdll's git_init(const LPWSTR* env), the ssh subtransport and the filter
 *    driver all *retain* that address and dereference it later, so the extra
 *    indirection is how a later SetEnv() reaches them without re-registering.
 *    m_baseptr therefore has a stable address and is repointed at the freshly
 *    serialized block on every mutation. GitTest's CEnvironment test captures
 *    the pointer once at the top and re-checks it after every operation, which
 *    is the contract stated as clearly as a test can state it.
 *  - An empty environment serializes to nullptr, not to an empty block.
 *    CreateProcess reads nullptr as "inherit the parent's environment" and an
 *    empty block as "the child gets no environment at all"; the difference is a
 *    git.exe with no PATH.
 *  - Names are matched case-insensitively, and the spelling of the most recent
 *    SetEnv wins. The second half matters because the msys and cygwin builds run
 *    git through bash, and a shell is not as forgiving about name case as
 *    Windows is.
 */
export class CEnvironment {
	using map_t = std::map<std::wstring, std::wstring, EnvNameLess>;

	struct State {
		map_t vars;
		/// The serialized "name=value\0...\0\0" block, rebuilt on every mutation.
		std::vector<wchar_t> block;
	};

	/// Engaged on first use. MSVC's std::map allocates its sentinel node, and a
	/// debug std::vector its iterator proxy, in non-constexpr constructors; held
	/// directly they make g_Environment dynamically initialized - and g_Git's
	/// constructor, in another module, read it before that ran (a null map head).
	/// An empty optional is constexpr, so the global is constinit: the compiler
	/// now rejects any member that would bring the ordering problem back.
	mutable std::optional<State> m_state;
	[[nodiscard]] State& state() const {
		if (!m_state)
			m_state.emplace();
		return *m_state;
	}
	[[nodiscard]] map_t& vars() const { return state().vars; }

	/// Repointed at the block by every Serialize(); its *address* is what is handed out.
	mutable std::vector<wchar_t>::pointer m_baseptr{};


	void Serialize() const {
		auto& m_block = state().block;
		m_block.clear();
		if (vars().empty()) {
			m_baseptr = nullptr; // an empty environment is nullptr, not an empty block
			return;
		}

		for (const auto& [name, value] : vars()) {
			m_block.insert(m_block.end(), name.cbegin(), name.cend());
			m_block.push_back(KEY_VALUE_SEPARATOR.front());
			m_block.insert(m_block.end(), value.cbegin(), value.cend());
			m_block.push_back(ENTRY_SEPARATOR.front());
		}
		m_block.push_back(ENTRY_SEPARATOR.front()); // the block's own terminator, after the last entry's
		m_baseptr = m_block.data();
	}

public:
	constexpr CEnvironment() = default;	// constexpr: g_Environment is constinit

	// A copy serializes its own block. The defaulted copy took m_baseptr along,
	// leaving the copy's get() pointing into the *source's* buffer.
	CEnvironment(const CEnvironment& env) : m_state(env.m_state) { Serialize(); }
	CEnvironment& operator=(const CEnvironment& env) {
		if (this != &env) {
			m_state = env.m_state;
			Serialize();
		}
		return *this;
	}
	// m_baseptr's address is handed out and retained (see above), so a moved-from
	// object would leave its consumers pointing at an empty block.
	CEnvironment(CEnvironment&&) = delete;
	CEnvironment& operator=(CEnvironment&&) = delete;
	~CEnvironment() = default;

	[[nodiscard]] bool empty() const { return vars().empty(); }
	[[nodiscard]] std::wstring GetEnv(const std::wstring_view name) const {
		const auto it = vars().find(name);
		return it == vars().cend() ? std::wstring() : it->second;
	}
	void SetEnv(const std::wstring_view name, const std::wstring_view value) const {
		// Erase before inserting rather than insert_or_assign, so the *passed* spelling
		// of the name survives when it differs in case from what is already stored. The
		// map is case-insensitive, so insert_or_assign would keep the older spelling,
		// and the cygwin/msys builds hand this block to a shell that is not as relaxed
		// about that as Windows is.
		vars().erase(name);
		vars().emplace(name, value);
		Serialize();
	}
	/// Removes a variable. This used to be SetEnv(name, nullptr) - a deletion
	/// spelled as an assignment, and invisible at the ~20 call sites that use it.
	void UnsetEnv(const std::wstring_view name) const {
		if (vars().erase(std::wstring(name)) == 0)
			return; // nothing removed, so the block cannot have changed
		Serialize();
	}
	/// The block itself, for CreateProcess and git_init: nullptr when empty, which
	/// CreateProcess reads as "inherit" (see above).
	/// The stable address the ssh subtransport, the filter driver and gitdll's
	/// wget_msysgit_etc retain and dereference later (see above).
	/// operator LPWSTR() const noexcept { return m_baseptr; }
	/// m_baseptr, not m_block.data(): after clear() the vector keeps its capacity,
	/// so data() would hand CreateProcess a stale block instead of nullptr.
	[[nodiscard]] auto get() const noexcept { return m_baseptr; }
	/// operator const LPWSTR*() const noexcept { return &m_baseptr; }
	/// put() used to be reinterpret_cast<wchar_t**>(m_block.data()) - the
	/// block itself posing as a pointer-to-pointer, so every consumer's *pEnv read
	/// the first characters of "NAME=value" as an address. It is &m_baseptr.
	[[nodiscard]] const LPWSTR* put() const noexcept { return &m_baseptr; }


	void AddToPath(const std::wstring_view value_view) const {
		// PATH entries compare the way Windows reads them: case-insensitively, and
		// with a trailing separator ignored. fs::path's == does neither - it is
		// case-sensitive, and "c:\windows\" has one element more than "c:\windows".
		// The separator after a drive letter stays: "c:\" and "c:" are different.
		const auto strip = [](std::wstring_view s) {
			while (s.size() > 1 && (s.back() == L'\\' || s.back() == L'/') && !(s.size() == 3 && s[1] == L':'))
				s.remove_suffix(1);
			return s;
		};
		const auto value = strip(value_view);
		if (value.empty())
			return;

		std::wstring value_of_PATH = GetEnv(L"PATH");
		tgit::wstr::TrimRight(value_of_PATH, PATH_DELIMITER);

		// do not double add paths to %PATH%
		for (const auto& part : tgit::str::stringtok(value_of_PATH, true, PATH_DELIMITER))
			if (tgit::wstr::CompareNoCase(strip(part), value) == 0)
				return;

		value_of_PATH += PATH_DELIMITER.front();
		value_of_PATH += value;

		SetEnv(L"PATH", value_of_PATH);
	}


	/// Merges the process's own environment in. Variables already set here win,
	/// which is what the flat block did by returning the first match on lookup.
	void CopyProcessEnvironment() const {
		const auto porig = wil::unique_environment_block{ GetEnvironmentStrings() };
		if (!porig)
			return;

		// std::unreachable_sentinel freaks me out so I'm using Z3_EXTREME_BOUND_ON_SIZE as an anti-OOB amulet
		for (const auto& keyval_subrange : std::views::counted(porig.get(), Z3_EXTREME_BOUND_ON_SIZE) | std::views::split(ENTRY_SEPARATOR)) {
			// the simplest way to search for the L"\0\0"
			if (keyval_subrange.empty())
				break;
			// weirdo, either a single char name with no value, or a floating '='. find(,1) would kick it out anyway.
			if (keyval_subrange.size() == 1)
				continue;

			const auto keyval_pair = std::wstring_view{keyval_subrange};
			// Split on the first '=' that is not at offset 0. Windows keeps a hidden
			// per-drive working directory in each environment block as "=C:=C:\some\dir",
			// and splitting on the very first '=' would give every one of those the same
			// empty name - so a map would keep exactly one of them.
			const auto key_len = keyval_pair.find(KEY_VALUE_SEPARATOR, 1);
			if (key_len == keyval_pair.npos)
				continue;

			const auto key_sv = keyval_pair.substr(0, key_len);
			Expects(keyval_pair[key_len] == KEY_VALUE_SEPARATOR.front());
			const auto val_sv = keyval_pair.substr(key_len + KEY_VALUE_SEPARATOR.size());
			// emplace, not insert_or_assign: what is already set here wins, which is what the flat block did by returning the first match on lookup.
			vars().emplace(key_sv, val_sv);
		}

		Serialize();
	}
};

export namespace Environment {
	constinit const CEnvironment g_Environment;
}
