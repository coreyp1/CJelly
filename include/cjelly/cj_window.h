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

/** Create a window bound to an engine.
 *  @param engine The engine to bind the window to.
 *  @param desc Window creation descriptor.
 *  @return Pointer to the created window, or NULL on failure.
 */
CJ_API cj_window_t* cj_window_create(cj_engine_t* engine, const cj_window_desc_t* desc);

/** Destroy a window. Safe to call with NULL.
 *  @param win The window to destroy. NULL is safe and will be ignored.
 */
CJ_API void cj_window_destroy(cj_window_t* win);

/** Resize a window (or defer to swapchain recreation).
 *  @param win The window to resize.
 *  @param width New width in pixels.
 *  @param height New height in pixels.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t cj_window_resize(cj_window_t* win, uint32_t width, uint32_t height);

/** Begin a frame for rendering.
 *  @param win The window to begin a frame for.
 *  @param out_frame_info Optional pointer to receive frame timing information. Can be NULL.
 *  @return CJ_SUCCESS on success, CJ_E_OUT_OF_DATE if swapchain needs recreation, or an error code.
 */
CJ_API cj_result_t cj_window_begin_frame(cj_window_t* win, cj_frame_info_t* out_frame_info);

/** Record and submit the window's render graph.
 *  This executes the render graph associated with the window, recording commands
 *  into the command buffer for the current frame.
 *  @param win The window whose render graph should be executed.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t cj_window_execute(cj_window_t* win);

/** Present the frame to the display.
 *  @param win The window to present the frame for.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t cj_window_present(cj_window_t* win);

/** Attach or replace the render graph used by this window.
 *  The window does not take ownership of the graph; it is merely referenced.
 *  @param win The window to set the render graph for.
 *  @param graph The render graph to use. Can be NULL to remove the graph.
 */
CJ_API void cj_window_set_render_graph(cj_window_t* win, cj_rgraph_t* graph);

/** Query the current client area size of a window.
 *  @param win The window to query.
 *  @param out_w Pointer to receive the width in pixels. Can be NULL.
 *  @param out_h Pointer to receive the height in pixels. Can be NULL.
 */
CJ_API void cj_window_get_size(const cj_window_t* win, uint32_t* out_w, uint32_t* out_h);

/** Get the per-window frame index (monotonically increasing).
 *  @param win The window to query.
 *  @return The current frame index for this window.
 */
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

/** Window redraw policy. Controls when a window is redrawn. */
typedef enum cj_redraw_policy_t {
  CJ_REDRAW_ALWAYS = 0,       /**< Always redraw every frame (for games/animations). */
  CJ_REDRAW_ON_DIRTY = 1,    /**< Only redraw when explicitly marked dirty (for static content). */
  CJ_REDRAW_ON_EVENTS = 2    /**< Redraw on resize/visibility changes + manual marking (default). */
} cj_redraw_policy_t;

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

/** Window resize callback function type.
 *  Called when a window is resized.
 *
 *  @param window The window that was resized.
 *  @param new_width New width of the window in pixels.
 *  @param new_height New height of the window in pixels.
 *  @param user_data User-provided data pointer.
 */
typedef void (*cj_window_resize_callback_t)(cj_window_t* window,
                                            uint32_t new_width,
                                            uint32_t new_height,
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

/** Register a resize callback for a window.
 *  @param window The window to register the callback for.
 *  @param callback Callback invoked when window is resized. NULL to remove callback.
 *  @param user_data User data pointer passed to callback.
 */
CJ_API void cj_window_on_resize(cj_window_t* window,
                                cj_window_resize_callback_t callback,
                                void* user_data);

/** Mark a window as needing redraw.
 *  The window will be rendered on the next frame (subject to redraw policy and FPS limits).
 *  The render reason is set to CJ_RENDER_REASON_FORCED, which bypasses per-window FPS limits.
 *  @param window The window to mark as dirty.
 */
CJ_API void cj_window_mark_dirty(cj_window_t* window);

/** Mark a window as needing redraw with a specific reason.
 *  @param window The window to mark as dirty.
 *  @param reason The reason for the redraw (affects FPS limiting behavior).
 */
CJ_API void cj_window_mark_dirty_with_reason(cj_window_t* window, cj_render_reason_t reason);

/** Clear the dirty flag for a window.
 *  @param window The window to clear the dirty flag for.
 */
CJ_API void cj_window_clear_dirty(cj_window_t* window);

/** Set the redraw policy for a window.
 *  @param window The window to set the policy for.
 *  @param policy The redraw policy to use.
 */
CJ_API void cj_window_set_redraw_policy(cj_window_t* window, cj_redraw_policy_t policy);

/** Set the maximum FPS for a window.
 *  When set to a value > 0, the window will only redraw if enough time has passed
 *  since the last render (1/fps seconds). This limit applies to timer-based renders
 *  but is bypassed for forced events (resize, expose, etc.).
 *  Set to 0 to disable per-window FPS limiting (window will respect global FPS limit and redraw policy).
 *  @param window The window to set the FPS limit for.
 *  @param max_fps Maximum frames per second (0 = unlimited, use global FPS limit).
 */
CJ_API void cj_window_set_max_fps(cj_window_t* window, uint32_t max_fps);

/** Re-record a color-only bindless command buffer set for a window.
 *  This is a temporary helper function during migration. It uses opaque pointers
 *  to avoid coupling public headers to legacy implementation types.
 *  @param win The window to re-record command buffers for.
 *  @param resources Opaque pointer to bindless resources.
 *  @param ctx Vulkan context for command buffer recording.
 */
struct CJellyVulkanContext; /* forward */
CJ_API void cj_window_rerecord_bindless_color(cj_window_t* win,
                                             const void* resources,
                                             const struct CJellyVulkanContext* ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif
