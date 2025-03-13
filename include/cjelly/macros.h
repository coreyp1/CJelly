#ifndef CJELLY_MACROS_H
#define CJELLY_MACROS_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


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


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CJELLY_MACROS_H
