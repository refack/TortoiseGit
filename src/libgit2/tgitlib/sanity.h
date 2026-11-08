#pragma once
#define GSL_MSVC_USE_STL_NOEXCEPTION_WORKAROUND 1
#define _KERNEL_MODE 1 // NOLINT(*-reserved-identifier)

#include <gsl/assert>

// gsl/assert has latched its kernel-mode (fastfail) contract behavior behind its
// header guard; leaving _KERNEL_MODE defined makes every LATER header that reads
// it (wil/result.h refuses outright) treat a Win32 app as kernel code.
#undef _KERNEL_MODE

#define Implies(A,B) (Ensures(!(A) || (B)), (A))


#define SCOPE_EXIT_CAT2(x, y) x##y
#define SCOPE_EXIT_CAT1(x, y) SCOPE_EXIT_CAT2(x, y)
#define SCOPE_EXIT auto SCOPE_EXIT_CAT1(scope_exit_, __COUNTER__) = wil::scope_guard() << [&]

// Not yet 2026, but good enough;
// #define SCOPE_EXIT auto _ = wil::scope_guard() << [&]
