/*
 * CJelly — Minimal C API stubs
 * Copyright (c) 2025
 *
 * This is a design-time stub for headers. Implementation is TBD.
 * Licensed under the MIT license for prototype purposes.
 */
#ifndef CJELLY_API_MACROS_H
#define CJELLY_API_MACROS_H
#ifdef __cplusplus
  #define CJ_EXTERN extern "C"
#else
  #define CJ_EXTERN
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef CJELLY_BUILD
    #define CJ_API CJ_EXTERN __declspec(dllexport)
  #else
    #define CJ_API CJ_EXTERN __declspec(dllimport)
  #endif
#else
  #define CJ_API CJ_EXTERN __attribute__((visibility("default")))
#endif

#endif /* CJELLY_API_MACROS_H */

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
