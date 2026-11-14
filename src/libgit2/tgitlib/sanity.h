#pragma once

#include <gsl/assert>

#define Implies(A,B) (Ensures(!(A) || (B)), (A))


namespace tgit::detail {
template <class F>
class [[nodiscard]] scope_exit {
	F f_;
public:
	explicit scope_exit(F&& f) : f_(static_cast<F&&>(f)) {}
	scope_exit(const scope_exit&) = delete;            // also suppresses the implicit move
	scope_exit& operator=(const scope_exit&) = delete;
	~scope_exit() { f_(); }
};
struct scope_exit_maker {
	template <class F>
	scope_exit<F> operator<<(F f) const { return scope_exit<F>(static_cast<F&&>(f)); }
};
}


#define SCOPE_EXIT_CAT2(x, y) x##y
#define SCOPE_EXIT_CAT1(x, y) SCOPE_EXIT_CAT2(x, y)
// #define SCOPE_EXIT auto SCOPE_EXIT_CAT1(scope_exit_, __COUNTER__) = ::tgit::detail::scope_exit_maker{} << [&]

// Not yet 2026, but good enough;
#define SCOPE_EXIT auto _ = wil::scope_guard() << [&]
