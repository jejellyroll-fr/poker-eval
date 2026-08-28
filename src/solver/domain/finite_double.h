#ifndef PE_SOLVER_FINITE_DOUBLE_H
#define PE_SOLVER_FINITE_DOUBLE_H

#include <float.h>

/* MinGW may resolve the C99 isfinite macro through a float overload even
   when the expression is double. Keep validation in the type being checked. */
static inline int pe_finite_double(double value)
{
    return value >= -DBL_MAX && value <= DBL_MAX;
}

#endif
