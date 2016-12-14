/*
    Compile time assert implementation.
*/

#ifndef STATIC_ASSERT_H_FUTILS
#define STATIC_ASSERT_H_FUTILS

#define FSTATIC_ASSERT4(expr, msg) typedef char fstatic_assertion_##msg[(expr) ? 1: -1]
#define FSTATIC_ASSERT3(expr, L)   FSTATIC_ASSERT4(expr, static_assertion_at_##L)
#define FSTATIC_ASSERT2(expr, L)   FSTATIC_ASSERT3(expr, L)
#define FSTATIC_ASSERT(expr)       FSTATIC_ASSERT2(expr, __LINE__)

#endif
