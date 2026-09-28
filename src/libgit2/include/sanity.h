#pragma once
#define GSL_MSVC_USE_STL_NOEXCEPTION_WORKAROUND 1
#define _KERNEL_MODE 1

#include <gsl/assert>

#define Implies(A,B) (Ensures(!(A) || (B)), (A))

