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
 * CJelly — the platform seam, for implementation files
 *
 * Every declaration here is portable: one name, no conditional, defined by
 * whichever module under src/platform the Makefile compiled. This header
 * names no window system, so a file that only needs to CALL across the seam
 * includes this one and stays free of Xlib and windows.h. The modules that
 * implement the seam include cjelly/platform_internal.h instead, which adds
 * the window system's own headers on top of this.
 *
 * Not part of the public API.
 */
#pragma once

#include <ghoti.io/cjelly/macros.h>

#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

#include <ghoti.io/cjelly/cj_result.h>
#include <ghoti.io/cjelly/cj_window.h>

/* The seam. Each of these is declared once, with no conditional, and defined
 * by both platform modules. A call site in window.c that uses them needs no
 * #ifdef of its own, which is the whole point: the conditional moves from
 * the middle of portable logic to the choice of which file gets compiled.
 *
 * Native window handles cross as uintptr_t rather than HWND or Window, so
 * that this part of the seam names no window system either. Both fit: a
 * Window is an XID, which is an unsigned long, and an HWND is a pointer.
 */

/** Milliseconds from a monotonic clock. Monotonic rather than wall clock: a
 *  clock adjustment must not make an animation jump or run backwards. */
uint64_t cj_plat_now_ms(void);

/** Microseconds from the same monotonic clock as cj_plat_now_ms.
 *
 *  Both exist because each platform computes them from its own source with
 *  its own arithmetic, and deriving one from the other would change the
 *  rounding on a clock the frame pacer reads.
 */
uint64_t cj_plat_now_us(void);

/** Sleep for approximately this many milliseconds. Returns at once for 0. */
void cj_plat_sleep_ms(uint32_t ms);

/** Route further mouse events to this window until released.
 *  @return true if the window system took the capture.
 */
bool cj_plat_capture_mouse(uintptr_t handle);

/** Undo cj_plat_capture_mouse.
 *  @return true if a capture was released.
 */
bool cj_plat_release_mouse(void);

/** What creating a native window tells the caller. Position and DPI are
 *  outputs because the window manager has the last word on both. */
typedef struct cj_plat_window_t {
  uintptr_t handle;    /**< 0 if the window could not be created. */
  int32_t x;           /**< Where it actually is, in CLIENT coordinates. */
  int32_t y;
  float dpi_scale;     /**< 1.0 being 96 DPI. */
} cj_plat_window_t;

/** Create, show and prepare a native window for rendering.
 *
 *  @param out Filled in on success; handle is 0 on failure. Its x, y and
 *             dpi_scale are read on entry as the values to keep if the
 *             window system does not report better ones.
 */
void cj_plat_create_window(const char* title, int width, int height,
    int32_t x, int32_t y, cj_window_state_t initial_state,
    cj_plat_window_t* out);

/** Create the Vulkan surface for this native window.
 *  @return whatever the window system's vkCreate*SurfaceKHR returned.
 */
VkResult cj_plat_create_surface(uintptr_t handle, VkInstance instance,
    VkSurfaceKHR* out_surface);

/** Attach the library's window pointer to the native window, where the
 *  window system offers a slot for it. Win32 needs this to find the window
 *  again in WM_DESTROY; X11 routes events by handle instead and does
 *  nothing. */
void cj_plat_bind_window_user_data(uintptr_t handle, void* user);

/** Undo cj_plat_bind_window_user_data, before the window is torn down. */
void cj_plat_unbind_window_user_data(uintptr_t handle);

/** Destroy the native window. Called once, after its Vulkan objects are
 *  gone. */
void cj_plat_destroy_native_window(uintptr_t handle);

/** Whether the window system still has this window.
 *  @return true if it is usable, or if the window system cannot say. */
bool cj_plat_window_is_alive(uintptr_t handle);

/** Ask the window system where this window actually is.
 *  @return true if it answered; false leaves the library's cache authoritative.
 */
bool cj_plat_query_position(uintptr_t handle, int32_t* out_x, int32_t* out_y);

/** Ask the window system what state this window is in.
 *  @return true if it answered; false leaves the library's cache authoritative.
 */
bool cj_plat_query_state(uintptr_t handle, cj_window_state_t* out_state);

/** Move the window, given the library's cached position for comparison.
 *  @return true if the caller should arm programmatic-move suppression, which
 *          only a window system that reports moves back to us needs.
 */
bool cj_plat_move_window(uintptr_t handle, int32_t x, int32_t y,
    int32_t cur_x, int32_t cur_y);

/** Ask the window manager to change this window's state. */
cj_result_t cj_plat_set_window_state(uintptr_t handle, cj_window_state_t state);

/** Drain the window system's event queue and dispatch to window callbacks.
 *
 *  Defined by whichever module under src/platform was built. Exported, and
 *  declared nowhere public - it was forward-declared inside cjelly.c to stop
 *  an implicit-declaration warning, which is a declaration no other caller
 *  can see.
 */
CJ_API void processWindowEvents(void);


/** Declare this process's DPI awareness to the window system, before any
 *  window exists. A window system that has no such declaration to make does
 *  nothing. */
void cj_plat_declare_dpi_awareness(void);

/** Ask the platform to call `on_shutdown` when the user asks the process to
 *  stop - a signal on POSIX, a console control event on Windows.
 *
 *  The callback runs in a context where almost nothing is safe: on POSIX it
 *  interrupts the main thread at an arbitrary point, on Windows it runs on
 *  another thread entirely. It may only set a flag. It returns false if
 *  there was nothing to tell, so a platform that distinguishes handled from
 *  unhandled can fall back to its own default.
 */
void cj_plat_register_shutdown_handler(bool (*on_shutdown)(void));

/** Open the window system's connection, if it has one to open.
 *  @return true if the library can go on to create windows. A window system
 *          with no connection to open answers true without doing anything.
 */
bool cj_plat_open_display(void);

/** Close what cj_plat_open_display opened. Idempotent. */
void cj_plat_close_display(void);

/** The Vulkan instance extension this window system's surfaces need, to go
 *  alongside VK_KHR_surface. */
const char* cj_plat_surface_extension_name(void);
