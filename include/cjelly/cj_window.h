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

/** Window close callback response. */
typedef enum cj_window_close_response_t {
  CJ_WINDOW_CLOSE_ALLOW = 0,    /**< Allow the window to close. */
  CJ_WINDOW_CLOSE_PREVENT = 1,  /**< Prevent the window from closing. */
} cj_window_close_response_t;

/** Per-frame callback result. */
typedef enum cj_frame_result_t {
  CJ_FRAME_CONTINUE = 0,      /**< Normal: execute and present this frame. */
  CJ_FRAME_SKIP = 1,          /**< Skip rendering this frame (window still alive). */
  CJ_FRAME_CLOSE_WINDOW = 2,  /**< Request this window be closed. */
  CJ_FRAME_STOP_LOOP = 3      /**< Request cj_run() to exit. */
} cj_frame_result_t;

/** Window close callback function type.
 *  @param window The window requesting to close.
 *  @param cancellable True if the close can be prevented, false if close is mandatory (e.g., application shutdown).
 *  @param user_data User-provided data pointer.
 *  @return CJ_WINDOW_CLOSE_ALLOW to allow close, CJ_WINDOW_CLOSE_PREVENT to prevent (only honored if cancellable is true).
 */
typedef cj_window_close_response_t (*cj_window_close_callback_t)(cj_window_t* window, bool cancellable, void* user_data);

/** Window per-frame callback function type.
 *  Called once per frame for windows that are able to begin a frame.
 *
 *  The callback may return a value to control the framework's behavior.
 *
 *  @param window The window to render.
 *  @param frame_info Frame timing information.
 *  @param user_data User-provided data pointer.
 *  @return A cj_frame_result_t controlling what the framework does next.
 */
typedef cj_frame_result_t (*cj_window_frame_callback_t)(cj_window_t* window,
                                                        const cj_frame_info_t* frame_info,
                                                        void* user_data);

/** Register a close callback for a window.
 *  @param window The window to register the callback for.
 *  @param callback Callback function to invoke when close is requested. NULL to remove callback.
 *  @param user_data User data pointer passed to callback.
 */
CJ_API void cj_window_on_close(cj_window_t* window,
                                 cj_window_close_callback_t callback,
                                 void* user_data);

/** Register a per-frame callback for a window.
 *  @param window The window to register the callback for.
 *  @param callback Callback invoked once per frame. NULL to remove callback.
 *  @param user_data User data pointer passed to callback.
 */
CJ_API void cj_window_on_frame(cj_window_t* window,
                               cj_window_frame_callback_t callback,
                               void* user_data);

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
