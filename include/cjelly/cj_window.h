/*
 * CJelly — Minimal C API stubs
 * Copyright (c) 2025
 *
 * This is a design-time stub for headers. Implementation is TBD.
 * Licensed under the MIT license for prototype purposes.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "cj_macros.h"
#include "cj_types.h"
#include "cj_result.h"

/** @file cj_window.h
 *  @brief Per-window lifecycle and frame submission.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Swapchain present mode preference. */
typedef enum cj_present_mode_t {
  CJ_PRESENT_VSYNC = 0,     /**< FIFO. */
  CJ_PRESENT_MAILBOX,       /**< Low-latency, potentially higher power. */
  CJ_PRESENT_IMMEDIATE,     /**< Tearing allowed. */
  CJ_PRESENT_DEFAULT = CJ_PRESENT_VSYNC
} cj_present_mode_t;

/** Window creation descriptor (platform-agnostic). */
typedef struct cj_window_desc_t {
  uint32_t width;
  uint32_t height;
  cj_str_t title;                 /**< Optional; UTF-8. */

  cj_present_mode_t present_mode; /**< Preference; backend may override. */
  uint32_t frames_in_flight;      /**< 0 = default (typically 2 or 3). */

  /** Optional native surface hookup (created externally).
   *  If NULL, CJelly will create an OS surface for you (where supported).
   *  The meaning of this pointer depends on platform and is defined in cj_platform.h.
   */
  const void* native_surface_desc;
} cj_window_desc_t;

/** Create a window bound to an engine. */
CJ_API cj_window_t* cj_window_create(cj_engine_t* engine, const cj_window_desc_t* desc);

/** Destroy a window. Safe to call with NULL. */
CJ_API void cj_window_destroy(cj_window_t* win);

/** Resize (or defer to swapchain recreation). */
CJ_API cj_result_t cj_window_resize(cj_window_t* win, uint32_t width, uint32_t height);

/** Begin a frame. Returns CJ_SUCCESS, CJ_E_OUT_OF_DATE (needs resize), or error. */
CJ_API cj_result_t cj_window_begin_frame(cj_window_t* win, cj_frame_info_t* out_frame_info);

/** Record & submit the window's render-graph. */
CJ_API cj_result_t cj_window_execute(cj_window_t* win);

/** Present the frame. */
CJ_API cj_result_t cj_window_present(cj_window_t* win);

/** Attach/replace the render graph used by this window. (win does not own `graph`.) */
CJ_API void cj_window_set_render_graph(cj_window_t* win, cj_rgraph_t* graph);

/** Query current client area size. */
CJ_API void cj_window_get_size(const cj_window_t* win, uint32_t* out_w, uint32_t* out_h);

/** Per-window frame index (monotonic). */
CJ_API uint64_t cj_window_frame_index(const cj_window_t* win);

/* Temporary helper during migration: re-record a color-only bindless command
 * buffer set for a window using provided resources/context. We intentionally
 * use an opaque pointer for resources here to avoid coupling public headers
 * to legacy implementation types. */
struct CJellyVulkanContext; /* forward */
CJ_API void cj_window_rerecord_bindless_color(cj_window_t* win,
                                             const void* resources,
                                             const struct CJellyVulkanContext* ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif
