#ifndef CJELLY_MACROS_H
#define CJELLY_MACROS_H

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

#include <cjelly/libver.h>

/**
 * Typedef prototypes.
 */
typedef struct CJellyFormatImageRaw CJellyFormatImageRaw;
typedef struct CJellyFormatImage CJellyFormatImage;
typedef struct CJellyFormat3dMtlMaterial CJellyFormat3dMtlMaterial;
typedef struct CJellyFormat3dMtl CJellyFormat3dMtl;
typedef struct CJellyFormat3dObjVertex CJellyFormat3dObjVertex;
typedef struct CJellyFormat3dObjTexCoord CJellyFormat3dObjTexCoord;
typedef struct CJellyFormat3dObjNormal CJellyFormat3dObjNormal;
typedef struct CJellyFormat3dObjFaceOverflow CJellyFormat3dObjFaceOverflow;
typedef struct CJellyFormat3dObjFace CJellyFormat3dObjFace;
typedef struct CJellyFormat3dObjGroup CJellyFormat3dObjGroup;
typedef struct CJellyFormat3dObjMaterialMapping
    CJellyFormat3dObjMaterialMapping;
typedef struct CJellyFormat3dObjModel CJellyFormat3dObjModel;

/**
 * A cross-compiler macro for marking a function parameter as unused.
 */
#if defined(__GNUC__) || defined(__clang__)
#define GCJ_MAYBE_UNUSED(X) __attribute__((unused)) X

#elif defined(_MSC_VER)
#define GCJ_MAYBE_UNUSED(X) (void)(X)

#else
#define GCJ_MAYBE_UNUSED(X) X

#endif


/**
 * A cross-compiler macro for marking a function as deprecated.
 */
#if defined(__GNUC__) || defined(__clang__)
#define GCJ_DEPRECATED __attribute__((deprecated))

#elif defined(_MSC_VER)
#define GCJ_DEPRECATED __declspec(deprecated)

#else
#define GCJ_DEPRECATED

#endif

/**
 * @brief Endianness conversion macros.
 */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
// On little-endian systems, no conversion is needed.
#define GCJ_LE16_TO_HOST(x) (x)
#define GCJ_LE32_TO_HOST(x) (x)
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
// On big-endian systems, swap the bytes.
#define GCJ_LE16_TO_HOST(x) (((x) >> 8) | (((x) & 0xff) << 8))
#define GCJ_LE32_TO_HOST(x)                                                    \
  ((((x) >> 24) & 0xff) | (((x) >> 8) & 0xff00) | (((x) & 0xff00) << 8) |      \
      (((x) & 0xff) << 24))
#else
#error "Endianness not defined"
#endif


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CJELLY_MACROS_H
