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

#ifdef __cplusplus
}
#endif
