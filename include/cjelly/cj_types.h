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

/** @brief Opaque engine handle. */
typedef struct cj_engine_t      cj_engine_t;

/** @brief Opaque window handle. */
typedef struct cj_window_t      cj_window_t;

/** @brief Opaque render graph handle. */
typedef struct cj_rgraph_t      cj_rgraph_t;

/** @brief Optional custom allocator callbacks. */
typedef struct cj_allocator_t   cj_allocator_t;

/** Generic handle: (index:32 | generation:32).
 *  Used to reference resources in a type-safe way with generation tracking.
 */
typedef struct cj_handle_t {
  uint32_t idx;  /**< Resource index. */
  uint32_t gen;  /**< Generation counter for validation. */
} cj_handle_t;

/** Create an invalid handle constant.
 *  @return A handle with index and generation both set to 0.
 */
static inline cj_handle_t cj_handle_nil(void) { cj_handle_t h = {0u,0u}; return h; }

/** Simple string view (not null-terminated).
 *  Used throughout CJelly for string parameters to avoid allocations.
 */
typedef struct cj_str_t {
  const char* ptr;  /**< Pointer to string data. */
  size_t      len;  /**< Length of string in bytes. */
} cj_str_t;

/** Reason why a frame is being rendered. */
typedef enum cj_render_reason_t {
  CJ_RENDER_REASON_TIMER = 0,              /**< Regular frame update (subject to FPS limit) */
  CJ_RENDER_REASON_RESIZE = 1,             /**< Window was resized (bypasses FPS limit) */
  CJ_RENDER_REASON_EXPOSE = 2,             /**< Window was exposed/shown (bypasses FPS limit) */
  CJ_RENDER_REASON_FORCED = 3,             /**< Explicitly marked dirty by user (bypasses FPS limit) */
  CJ_RENDER_REASON_SWAPCHAIN_RECREATE = 4, /**< Swapchain was recreated (bypasses FPS limit) */
} cj_render_reason_t;

/** Frame timing information passed to frame callbacks and begin_frame.
 *  Contains timing and context information about the current frame.
 */
typedef struct cj_frame_info_t {
  uint64_t frame_index;              /**< Monotonically increasing frame number for this window. */
  double   delta_seconds;            /**< Time since last frame in seconds (currently stubbed). */
  cj_render_reason_t render_reason;   /**< Why this frame is being rendered. */
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
