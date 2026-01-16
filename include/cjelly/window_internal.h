/*
 * CJelly — Internal window API
 * Copyright (c) 2025
 *
 * Internal window functions used between modules.
 * Not part of the public API.
 */
#pragma once

#include "cj_window.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Internal helper to invoke close callback and destroy window if allowed.
 *  @param window The window to close.
 *  @param cancellable True if the close can be prevented, false if close is mandatory.
 */
void cj_window_close_with_callback(cj_window_t* window, bool cancellable);

/** Internal helper used by the framework event loop to run a window's per-frame callback. */
cj_frame_result_t cj_window__dispatch_frame_callback(cj_window_t* window,
                                                    const cj_frame_info_t* frame_info);

/** Internal helper to check if a window is minimized.
 *  @param window The window to check.
 *  @return true if window is minimized, false otherwise.
 */
bool cj_window__is_minimized(cj_window_t* window);

/** Internal helper to check if a window uses VSync (FIFO present mode).
 *  @param window The window to check.
 *  @return true if window uses VSync, false otherwise.
 */
bool cj_window__uses_vsync(cj_window_t* window);

/** Internal helper to check if a window needs redraw.
 *  @param window The window to check.
 *  @return true if window needs redraw, false otherwise.
 */
bool cj_window__needs_redraw(cj_window_t* window);

/** Internal helper to set minimized state (called from window messages/events).
 *  @param window The window to update.
 *  @param minimized True if window is minimized, false if restored.
 */
void cj_window__set_minimized(cj_window_t* window, bool minimized);

/** Internal helper to update window size and mark swapchain for recreation.
 *  @param window The window that was resized.
 *  @param new_width New width in pixels.
 *  @param new_height New height in pixels.
 */
void cj_window__update_size_and_mark_recreate(cj_window_t* window, uint32_t new_width, uint32_t new_height);

/** Internal helper to dispatch resize callback (called from window messages/events).
 *  @param window The window that was resized.
 *  @param new_width New width in pixels.
 *  @param new_height New height in pixels.
 */
void cj_window__dispatch_resize_callback(cj_window_t* window, uint32_t new_width, uint32_t new_height);

/** Internal helper to check if dirty flag should be cleared after frame render.
 *  @param window The window to check.
 *  @return true if dirty flag should be cleared, false otherwise.
 */
bool cj_window__should_clear_dirty_after_render(cj_window_t* window);

/** Internal helper to check if frame callback should be called (even if not dirty).
 *  For CJ_REDRAW_ON_EVENTS, callbacks are always called so they can check time and mark dirty.
 *  @param window The window to check.
 *  @return true if callback should be called, false otherwise.
 */
bool cj_window__should_call_callback(cj_window_t* window);

/** Internal helper to check if enough time has passed since last render for per-window FPS limiting.
 *  @param window The window to check.
 *  @param current_time_us Current time in microseconds.
 *  @return true if window can render (enough time has passed or FPS limit is disabled), false otherwise.
 */
bool cj_window__can_render_at_fps(cj_window_t* window, uint64_t current_time_us);

/** Internal helper to get the pending render reason for a window.
 *  @param window The window to check.
 *  @return The reason why the window needs to render, or CJ_RENDER_REASON_TIMER if not dirty.
 */
cj_render_reason_t cj_window__get_pending_render_reason(cj_window_t* window);

/** Internal helper to set the pending render reason for a window.
 *  @param window The window to update.
 *  @param reason The render reason to set.
 */
void cj_window__set_pending_render_reason(cj_window_t* window, cj_render_reason_t reason);

/** Internal helper to check if a window uses CJ_REDRAW_ALWAYS policy.
 *  @param window The window to check.
 *  @return true if window uses CJ_REDRAW_ALWAYS, false otherwise.
 */
bool cj_window__uses_always_redraw(cj_window_t* window);

/** Internal helper to check if a render reason should bypass FPS limiting.
 *  @param reason The render reason to check.
 *  @return true if this reason should bypass FPS limit, false otherwise.
 */
bool cj_window__should_bypass_fps_limit(cj_render_reason_t reason);

/** Internal helper to update the last render time for a window (used for FPS limiting).
 *  @param window The window to update.
 *  @param render_time_us The time when the frame was rendered (in microseconds).
 */
void cj_window__update_last_render_time(cj_window_t* window, uint64_t render_time_us);

#ifdef __cplusplus
}
#endif
