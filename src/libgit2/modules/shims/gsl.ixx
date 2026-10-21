module;
#include <gsl/gsl>

export module gsl;

export namespace gsl {


using ::gsl::at;
using ::gsl::copy;
using ::gsl::final_action;
using ::gsl::finally;
using ::gsl::narrow;
using ::gsl::narrow_cast;
using ::gsl::not_null;
using ::gsl::owner;
using ::gsl::span;
using ::gsl::index;
using ::gsl::basic_zstring;

constexpr auto win32_cast(auto u) noexcept { return ::gsl::narrow_cast<int>(u); }
constexpr auto size_t_cast(auto u) noexcept { return ::gsl::narrow_cast<size_t>(u); }

}

export using gsl::win32_cast;
export using gsl::size_t_cast;
