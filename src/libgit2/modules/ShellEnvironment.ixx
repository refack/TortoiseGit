module;
#include <processenv.h>
#include "sanity.h"

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
export struct CEnvironment {
	using MAP_ENVIRONMENT = std::map<std::wstring, std::wstring, EnvNameLess>;


	~CEnvironment() = default;
	CEnvironment() {
		CopyProcessEnvironment();
	}

	CEnvironment(const CEnvironment& env)
	: m_vars(env.m_vars)
	{ Serialize(); }

	CEnvironment& operator=(const CEnvironment& env) {
		if (this != &env) {
			m_vars = env.m_vars;
			Serialize();
		}
		return *this;
	}


	// Deleted for the same reason they were before: m_baseptr's address is handed
	// out and retained, so an object that has been moved from would leave its
	// consumers pointing at an empty block.
	CEnvironment(CEnvironment&& env) = delete;
	CEnvironment& operator=(CEnvironment&& env) = delete;

	/// Merges the process's own environment in. Variables already set here win,
	/// which is what the flat block did by returning the first match on lookup.
	void CopyProcessEnvironment() {
		constexpr auto EXTREME_Z3_UPPER_BOUND_ON_SIZE_OF_ENV_BLOCK = 1U << 23U;
		constexpr auto BLOCK_END_SEP = std::array<wchar_t, 2>{};

		const auto porig = wil::unique_environment_block{ GetEnvironmentStrings() };
		if (!porig)
			return;

		const auto env_block_span = std::ranges::subrange{ porig.get(), std::unreachable_sentinel };
		for (const auto& keyval_subrange : std::views::split(env_block_span, L'\0')) {
			const auto keyval = std::wstring_view{keyval_subrange};
			if (keyval.length() <= 1)
				continue;
			// Split on the first '=' that is not at offset 0. Windows keeps a hidden
			// per-drive working directory in each environment block as "=C:=C:\some\dir",
			// and splitting on the very first '=' would give every one of those the same
			// empty name - so a map would keep exactly one of them.
			const auto key_len = keyval.find(L'=', 1);
			if (key_len == keyval.npos)
				continue;

			const auto key_sv = keyval.substr(0, key_len);
			Expects(keyval[key_len] == L'=');
			const auto val_sv = keyval.substr(key_len + 1);
			// emplace, not insert_or_assign: what is already set here wins, which is what the flat block did by returning the first match on lookup.
			m_vars.emplace(key_sv, val_sv);
		}

		Serialize();
	}

	void clear() {
		m_vars.clear();
		Serialize(); // it cleared m_vars twice and never this, so the block kept the old environment
	}
	[[nodiscard]] bool empty() const { return m_vars.empty(); }


	[[nodiscard]] std::wstring GetEnv(std::wstring_view name) const;
	void SetEnv(std::wstring_view name, std::wstring_view value);
	/// Removes a variable. This used to be SetEnv(name, nullptr) - a deletion
	/// spelled as an assignment, and invisible at the ~20 call sites that use it.
	void UnsetEnv(std::wstring_view name);
	void AddToPath(std::wstring_view value_view);


	/// The block itself, for CreateProcess and git_init: nullptr when empty, which
	/// CreateProcess reads as "inherit" (see above).
	/// The stable address the ssh subtransport, the filter driver and gitdll's
	/// wget_msysgit_etc retain and dereference later (see above).
	/// operator LPWSTR() const noexcept { return m_baseptr; }
	[[nodiscard]] auto get() const noexcept { return m_block.data(); }
	/// operator const LPWSTR*() const noexcept { return &m_baseptr; }
	/// put() used to be reinterpret_cast<wchar_t**>(m_block.data()) - the
	/// block itself posing as a pointer-to-pointer, so every consumer's *pEnv read
	/// the first characters of "NAME=value" as an address. It is &m_baseptr.
	[[nodiscard]] auto* put() noexcept { return &m_baseptr; }

private:
	void Serialize();

	MAP_ENVIRONMENT m_vars;
	/// The serialized "name=value\0...\0\0" block, rebuilt on every mutation.
	std::vector<wchar_t> m_block;
	/// Repointed at m_block by every Serialize(); its *address* is what is handed out.
	std::span<wchar_t>::pointer m_baseptr = {};
};



void CEnvironment::Serialize() {
	m_block.clear();
	if (m_vars.empty()) {
		m_baseptr = nullptr; // an empty environment is nullptr, not an empty block
		return;
	}

	for (const auto& [name, value] : m_vars) {
		m_block.insert(m_block.end(), name.cbegin(), name.cend());
		m_block.push_back(L'=');
		m_block.insert(m_block.end(), value.cbegin(), value.cend());
		m_block.push_back(L'\0');
	}
	m_block.push_back(L'\0'); // the block's own terminator, after the last entry's
	m_baseptr = m_block.data();
}






std::wstring CEnvironment::GetEnv(const std::wstring_view name) const {
	const auto it = m_vars.find(name);
	return it == m_vars.cend() ? std::wstring() : it->second;
}


void CEnvironment::SetEnv(const std::wstring_view name, const std::wstring_view value) {
	// Erase before inserting rather than insert_or_assign, so the *passed* spelling
	// of the name survives when it differs in case from what is already stored. The
	// map is case-insensitive, so insert_or_assign would keep the older spelling,
	// and the cygwin/msys builds hand this block to a shell that is not as relaxed
	// about that as Windows is.
	m_vars.erase(name);
	m_vars.emplace(name, value);
	Serialize();
}


void CEnvironment::UnsetEnv(const std::wstring_view name) {
	if (m_vars.erase(std::wstring(name)) == 0)
		return; // nothing removed, so the block cannot have changed
	Serialize();
}


void CEnvironment::AddToPath(const std::wstring_view value_view) {
	const auto value = fs::path(value_view);
	if (value.empty())
		return;


	std::wstring value_of_PATH = GetEnv(L"PATH");
	tgit::wstr::TrimRight(value_of_PATH, L";");

	// do not double add paths to %PATH%
	for (const auto& part : tgit::str::stringtok(value_of_PATH, true, L";"))
		if (fs::path{ part } == value)
			return;

	value_of_PATH += L";" + value.wstring();

	SetEnv(L"PATH", value_of_PATH);
}
