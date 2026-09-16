#ifndef GHOTI_IO_CJ_MACROS_H
#define GHOTI_IO_CJ_MACROS_H

#include <ghoti.io/cjelly/namespace.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// We want to use the C11 standard library, so we need to define
// _POSIX_C_SOURCE to 200809L. This is required for the C11 standard library
// functions to be available.
//
// The __STDC_ALLOC_LIB__ macro is defined by the C11 standard library, so we
// can use it to check if the C11 standard library is available.
// If it is, we define __STDC_WANT_LIB_EXT2__ to 1, which enables the C11
// standard library functions.
// If it is not, we define _POSIX_C_SOURCE to 200809L, which enables the C11
// standard library functions in POSIX-compliant systems.
//
// This is a workaround for the fact that some compilers (like MSVC and GCC) do
// not support the C11 standard library functions by default.
#ifdef __STDC_ALLOC_LIB__
#ifndef __STDC_WANT_LIB_EXT2__
#define __STDC_WANT_LIB_EXT2__ 1
#endif // __STDC_WANT_LIB_EXT2__
#else
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif // _POSIX_C_SOURCE
#endif // __STDC_ALLOC_LIB__

#include <ghoti.io/cjelly/libver.h>

//-----------------------------------------------------------------------------
// Visibility
//-----------------------------------------------------------------------------
//
// Merged here from cj_macros.h, which defined CJ_API inside an include guard
// that it closed early, leaving CJ_ARRAY_SIZE and CJ_BIT outside it. Two
// macro headers for one library was one too many.

#ifdef __cplusplus
#define CJ_EXTERN extern "C"
#else
#define CJ_EXTERN
#endif

/**
 * Marks a declaration as part of the public API.
 *
 * The library is built with -fvisibility=hidden, so a symbol without this is
 * not exported at all.  See CONVENTIONS.md section 4.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef CJELLY_BUILD
#define CJ_API CJ_EXTERN __declspec(dllexport)
#else
#define CJ_API CJ_EXTERN __declspec(dllimport)
#endif
#else
#define CJ_API CJ_EXTERN __attribute__((visibility("default")))
#endif

/**
 * Marks an exported *variable* as part of the public API.
 *
 * Same visibility as CJ_API, but without the `extern "C"`.  A variable
 * declaration cannot carry a redundant linkage specification - `extern "C"
 * extern int x;` is ill-formed in C++ - so a declaration that needs both
 * `extern` and export uses this and sits inside the header's `extern "C"`
 * block like every other declaration.  See CONVENTIONS.md section 4.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef CJELLY_BUILD
#define CJ_API_DATA __declspec(dllexport)
#else
#define CJ_API_DATA __declspec(dllimport)
#endif
#else
#define CJ_API_DATA __attribute__((visibility("default")))
#endif

/**
 * Marks an internal declaration that the test suite needs to reach.
 *
 * Exported only when CJELLY_TEST_BUILD is defined, which the Makefile does not
 * do for the shipped library.
 */
#ifdef CJELLY_TEST_BUILD
#define CJ_INTERNAL_API CJ_API
#else
#define CJ_INTERNAL_API
#endif

/** @def CJ_ARRAY_SIZE
 *  @brief Compile-time array size helper.
 */
#ifndef CJ_ARRAY_SIZE
#define CJ_ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

/** @def CJ_BIT
 *  @brief Bit macro.
 */
#ifndef CJ_BIT
#define CJ_BIT(x) (1u << (x))
#endif

/**
 * Typedef prototypes.
 */
typedef struct CJellyFormatImageRaw CJellyFormatImageRaw;
typedef struct CJellyFormatImage CJellyFormatImage;
typedef struct CJellyModelMesh CJellyModelMesh;

/**
 * A cross-compiler macro for marking a function parameter as unused.
 */
#if defined(__GNUC__) || defined(__clang__)
#define CJ_MAYBE_UNUSED(X) __attribute__((unused)) X

#elif defined(_MSC_VER)
#define CJ_MAYBE_UNUSED(X) (void)(X)

#else
#define CJ_MAYBE_UNUSED(X) X

#endif


/**
 * A cross-compiler macro for marking a function as deprecated.
 */
#if defined(__GNUC__) || defined(__clang__)
#define CJ_DEPRECATED __attribute__((deprecated))

#elif defined(_MSC_VER)
#define CJ_DEPRECATED __declspec(deprecated)

#else
#define CJ_DEPRECATED

#endif

/**
 * @brief Endianness conversion macros.
 */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
// On little-endian systems, no conversion is needed.
#define CJ_LE16_TO_HOST(x) (x)
#define CJ_LE32_TO_HOST(x) (x)
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
// On big-endian systems, swap the bytes.
#define CJ_LE16_TO_HOST(x) (((x) >> 8) | (((x) & 0xff) << 8))
#define CJ_LE32_TO_HOST(x)                                                    \
  ((((x) >> 24) & 0xff) | (((x) >> 8) & 0xff00) | (((x) & 0xff00) << 8) |      \
      (((x) & 0xff) << 24))
#else
#error "Endianness not defined"
#endif


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // GHOTI_IO_CJ_MACROS_H
