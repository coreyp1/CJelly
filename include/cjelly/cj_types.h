/*
 * CJelly — Minimal C API stubs
 * Copyright (c) 2025
 *
 * This is a design-time stub for headers. Implementation is TBD.
 * Licensed under the MIT license for prototype purposes.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cj_macros.h"

/** @file cj_types.h
 *  @brief Common CJelly types and opaque handles.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cj_engine_t      cj_engine_t;   /**< Opaque engine. */
typedef struct cj_window_t      cj_window_t;   /**< Opaque window.  */
typedef struct cj_rgraph_t      cj_rgraph_t;   /**< Opaque render-graph. */

typedef struct cj_allocator_t   cj_allocator_t;/**< Optional custom allocator callbacks. */

/** Generic handle: (index:32 | generation:32). */
typedef struct cj_handle_t {
  uint32_t idx;
  uint32_t gen;
} cj_handle_t;

/** Invalid handle constant. */
static inline cj_handle_t cj_handle_nil(void) { cj_handle_t h = {0u,0u}; return h; }

/** Simple string view (not null-terminated). */
typedef struct cj_str_t {
  const char* ptr;
  size_t      len;
} cj_str_t;

/** Frame timing info passed to begin_frame. */
typedef struct cj_frame_info_t {
  uint64_t frame_index;
  double   delta_seconds;
} cj_frame_info_t;

/** Bool tri-state for feature requests. */
typedef enum cj_feature_t {
  CJ_FEATURE_DEFAULT = 0,
  CJ_FEATURE_FORCE_OFF = 1,
  CJ_FEATURE_FORCE_ON  = 2,
} cj_feature_t;

#ifdef __cplusplus
} /* extern "C" */
#endif
