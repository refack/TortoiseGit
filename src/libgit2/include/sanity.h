#pragma once
#include <gsl/assert>

#define Implies(A,B) (Ensures(!(A) || (B)), (A))

