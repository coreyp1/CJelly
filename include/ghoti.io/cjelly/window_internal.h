/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2025-2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io CJelly.
 *
 * Ghoti.io CJelly is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io CJelly is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * CJelly — Internal window API
 *
 * Internal window functions used between modules.
 * Not part of the public API.
 */
#pragma once


#include <ghoti.io/cjelly/macros.h>
#include "cj_window.h"
#include <stdbool.h>

/* Named here rather than left to whoever includes this: the declarations
 * below use VkDeviceMemory and VkExtent2D, and a header that borrows types
 * from its includer compiles in the files that happen to include Vulkan
 * first and nowhere else. check-headers only preprocesses, so it cannot see
 * an undeclared type - the first TU to get the order wrong does. */
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Internal helper to invoke close callback and destroy window if allowed.
 *  @param window The window to close.
 *  @param cancellable True if the close can be prevented, false if close is mandatory.
 */
void cj_window_close_with_callback(cj_window_t* window, bool cancellable);

/** Internal helper used by the framework event loop to run a window's per-frame callback. */
/** What a completed capture left behind, without exposing the platform
 *  window structure, which is private to window.c.
 */
typedef struct cj_window_readback_t {
  VkDeviceMemory memory;  /**< Host-visible, host-coherent, already bound. */
  VkDeviceSize size;      /**< Bytes written, extent.width * height * 4. */
  VkFormat format;        /**< The swapchain format the bytes are in. */
  VkExtent2D extent;      /**< Its size in pixels, as of the copy. */
} cj_window_readback_t;

/** Ask that the next frame this window presents also be copied for reading.
 *
 *  The copy is recorded into that frame and submitted with it, while the
 *  application still owns the swapchain image. It cannot be done afterwards:
 *  from vkQueuePresentKHR until the next acquire the image belongs to the
 *  presentation engine, and touching it there is a write-after-present
 *  hazard that synchronisation validation reports once per capture.
 *
 *  Asking twice before reading is not an error and does not queue a second
 *  copy; the window holds one frame at a time.
 */
void cj_window__request_capture(cj_window_t* window);

/** Collect the copy, if one has been made.
 *
 *  @param window The window.
 *  @param out_readback Receives the description on success.
 *  @return true when a frame had been copied and has now been handed over.
 *          The window then holds nothing until the next request.
 */
bool cj_window__take_capture(
    cj_window_t* window, cj_window_readback_t* out_readback);

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

/** Internal helper to dispatch move callback (called from window messages/events).
 *  @param window The window that was moved.
 *  @param new_x New X position in screen coordinates.
 *  @param new_y New Y position in screen coordinates.
 */
void cj_window__dispatch_move_callback(cj_window_t* window, int32_t new_x, int32_t new_y);

/** Render one frame immediately, without returning to the event loop.
 *
 *  For a window system whose resize is a modal loop that does not return to
 *  the event loop until the drag ends - Win32's is - so that the window
 *  keeps drawing while it is being resized.
 *
 *  @param window The window to draw.
 */
void cj_window__render_frame_immediate(cj_window_t* window);

/** Internal helper to dispatch state change callback (called from window messages/events).
 *  @param window The window whose state changed.
 *  @param new_state The new window state.
 */
void cj_window__dispatch_state_callback(cj_window_t* window, cj_window_state_t new_state);

/** Internal helper to get window position.
 *  @param window The window to query.
 *  @param out_x Pointer to receive X position. Can be NULL.
 *  @param out_y Pointer to receive Y position. Can be NULL.
 */
void cj_window__get_position(cj_window_t* window, int32_t* out_x, int32_t* out_y);

/** Internal helper to set window position (updates cache only, doesn't move window).
 *  @param window The window to update.
 *  @param x New X position.
 *  @param y New Y position.
 */
void cj_window__set_position(cj_window_t* window, int32_t x, int32_t y);

/** Internal helper to get window state.
 *  @param window The window to query.
 *  @return The current window state.
 */
cj_window_state_t cj_window__get_state(cj_window_t* window);

/** Internal helper to set window state (updates cache only, doesn't change state).
 *  @param window The window to update.
 *  @param state New window state.
 */
void cj_window__set_state(cj_window_t* window, cj_window_state_t state);

/** Internal helper to get DPI scale.
 *  @param window The window to query.
 *  @return DPI scale factor (1.0 = 96 DPI).
 */
float cj_window__get_dpi_scale(cj_window_t* window);

/** Internal helper to set DPI scale.
 *  @param window The window to update.
 *  @param dpi_scale New DPI scale factor.
 */
void cj_window__set_dpi_scale(cj_window_t* window, float dpi_scale);

/** Internal helper to mark swapchain for recreation.
 *  @param window The window to mark.
 */
void cj_window__mark_swapchain_for_recreation(cj_window_t* window);

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

/** Internal helper to check if a key is currently pressed (for repeat detection).
 *  @param window The window to check.
 *  @param keycode The keycode to check.
 *  @return true if key is pressed, false otherwise.
 */
bool cj_window__is_key_pressed(cj_window_t* window, cj_keycode_t keycode);

/** Internal helper to set key pressed state (for repeat detection).
 *  @param window The window to update.
 *  @param keycode The keycode to update.
 *  @param pressed True to mark as pressed, false to mark as released.
 */
void cj_window__set_key_pressed(cj_window_t* window, cj_keycode_t keycode, bool pressed);

/** Internal helper to dispatch keyboard event callback.
 *  @param window The window that received the keyboard event.
 *  @param keycode The platform-independent keycode.
 *  @param scancode The physical key scancode.
 *  @param action The key action (DOWN, UP, REPEAT).
 *  @param modifiers The modifier keys held during the event.
 *  @param is_repeat True if this is an auto-repeat event.
 */
void cj_window__dispatch_key_callback(cj_window_t* window,
                                     cj_keycode_t keycode,
                                     cj_scancode_t scancode,
                                     cj_key_action_t action,
                                     cj_modifiers_t modifiers,
                                     bool is_repeat);

/** Internal helper to check if a mouse button is currently pressed.
 *  @param window The window to check.
 *  @param button The mouse button to check.
 *  @return true if button is pressed, false otherwise.
 */
bool cj_window__is_mouse_button_pressed(cj_window_t* window, cj_mouse_button_t button);

/** Internal helper to set mouse button pressed state.
 *  @param window The window to update.
 *  @param button The mouse button to update.
 *  @param pressed True to mark as pressed, false to mark as released.
 */
void cj_window__set_mouse_button_pressed(cj_window_t* window, cj_mouse_button_t button, bool pressed);

/** Internal helper to dispatch mouse event callback.
 *  @param window The window that received the mouse event.
 *  @param event The mouse event to dispatch.
 */
void cj_window__dispatch_mouse_callback(cj_window_t* window, const cj_mouse_event_t* event);

/** Internal helper to dispatch focus event callback.
 *  @param window The window that received the focus event.
 *  @param action The focus action (GAINED or LOST).
 */
void cj_window__dispatch_focus_callback(cj_window_t* window, cj_focus_action_t action);

/** Internal helper to clear all input state (keys and mouse buttons) on focus loss.
 *  @param window The window to clear state for.
 */
void cj_window__clear_input_state(cj_window_t* window);

/** Internal helper to get current mouse position (for calculating deltas).
 *  @param window The window to query.
 *  @param out_x Pointer to receive X coordinate. Can be NULL.
 *  @param out_y Pointer to receive Y coordinate. Can be NULL.
 */
void cj_window__get_mouse_position(cj_window_t* window, int32_t* out_x, int32_t* out_y);

/** Internal helper to check if window is being programmatically moved (suppress ConfigureNotify feedback).
 *  @param window The window to check.
 *  @return true if window is being programmatically moved, false otherwise.
 */
bool cj_window__is_programmatic_move(cj_window_t* window);

/** Internal helper to set programmatic move flag (suppress ConfigureNotify feedback).
 *  @param window The window to update.
 *  @param is_programmatic True to set flag, false to clear it.
 */
void cj_window__set_programmatic_move(cj_window_t* window, bool is_programmatic);

/** Internal helper to update mouse root coordinates (for screen-space delta calculation).
 *  @param window The window to update.
 *  @param root_x Root X coordinate.
 *  @param root_y Root Y coordinate.
 */
void cj_window__update_mouse_root(cj_window_t* window, int32_t root_x, int32_t root_y);

/** Internal helper to get last mouse root coordinates and check if we've seen a move.
 *  @param window The window to query.
 *  @param out_root_x Pointer to receive root X coordinate. Can be NULL.
 *  @param out_root_y Pointer to receive root Y coordinate. Can be NULL.
 *  @param out_has_seen_move Pointer to receive flag indicating if we've seen a move. Can be NULL.
 */
void cj_window__get_mouse_root(cj_window_t* window, int32_t* out_root_x, int32_t* out_root_y, bool* out_has_seen_move);

/** Internal helper to reset mouse root tracking (call when starting drag or moving window).
 *  @param window The window to reset.
 */
void cj_window__reset_mouse_root(cj_window_t* window);

#ifdef __cplusplus
}
#endif
