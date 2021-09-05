#pragma once

// Source: https://www.fluentcpp.com/2019/08/30/how-to-disable-a-warning-in-cpp/
// GCC to MSVC codes: https://github.com/srz-zumix/awesome-cpp-warning

// Defines for MSVC
#ifdef _MSC_VER
#define DISABLE_WARNING_PUSH           __pragma(warning(push))
#define DISABLE_WARNING_POP            __pragma(warning(pop))
#define DISABLE_WARNING(warningNumber) __pragma(warning(disable : warningNumber))

#define DISABLE_WARNING_BOOL_COMPARE
#define DISABLE_WARNING_CONVERSION DISABLE_WARNING(C4244)
#define DISABLE_WARNING_IMPL_INT_TO_FLOAT
#define DISABLE_WARNING_SIGN_COMPARE    DISABLE_WARNING(C4018, C4287, C4388, C4389)
#define DISABLE_WARNING_SIGN_CONVERSION DISABLE_WARNING(C4308, C4245, C4365)
#define DISABLE_WARNING_USELESS_CAST
#endif

// Defines for GCC and Clang
#if defined(__GNUC__) || defined(__clang__)
#define DO_PRAGMA(X)                    _Pragma(#X)
#define DISABLE_WARNING_PUSH            DO_PRAGMA(GCC diagnostic push)
#define DISABLE_WARNING_POP             DO_PRAGMA(GCC diagnostic pop)
#define DISABLE_WARNING(warningName)    DO_PRAGMA(GCC diagnostic ignored warningName)

#define DISABLE_WARNING_SIGN_CONVERSION DISABLE_WARNING("-Wsign-conversion")
#define DISABLE_WARNING_SIGN_COMPARE    DISABLE_WARNING("-Wsign-compare")
#define DISABLE_WARNING_CONVERSION      DISABLE_WARNING("-Wconversion")
#endif

// Defines specific to Clang
#ifdef __clang__
#define DISABLE_WARNING_BOOL_COMPARE DISABLE_WARNING("-Wtautological-constant-out-of-range-compare")
#define DISABLE_WARNING_USELESS_CAST

#if defined(__APPLE__) || (__clang_major__ < 11)
#define DISABLE_WARNING_IMPL_INT_TO_FLOAT
#else
#define DISABLE_WARNING_IMPL_INT_TO_FLOAT DISABLE_WARNING("-Wimplicit-const-int-float-conversion")
#endif
#endif

// Defines specific to GCC
#if defined(__GNUC__) && !defined(__clang__)
#define DISABLE_WARNING_BOOL_COMPARE      DISABLE_WARNING("-Wbool-compare")
#define DISABLE_WARNING_IMPL_INT_TO_FLOAT DISABLE_WARNING("-Wfloat-conversion")
#define DISABLE_WARNING_USELESS_CAST      DISABLE_WARNING("-Wuseless-cast")
#endif

// Defines for unknown/unsupported compilers
#if !defined(_MSC_VER) && !defined(__GNUC__) && !defined(__clang__)
#define DISABLE_WARNING
#define DISABLE_WARNING_PUSH
#define DISABLE_WARNING_POP

#define DISABLE_WARNING_BOOL_COMPARE
#define DISABLE_WARNING_CONVERSION
#define DISABLE_WARNING_IMPL_INT_TO_FLOAT
#define DISABLE_WARNING_SIGN_COMPARE
#define DISABLE_WARNING_SIGN_CONVERSION
#define DISABLE_WARNING_USELESS_CAST

#endif
