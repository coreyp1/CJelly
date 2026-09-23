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
 * CJelly — Platform headers, for implementation files only
 *
 * The window system's own headers, and the VK_USE_PLATFORM_* selection that
 * has to precede the first <vulkan/vulkan.h> in a translation unit.
 *
 * This used to sit in application.h, which is installed, so every consumer
 * that included it got all of Xlib or all of windows.h whether it wanted
 * them or not - roughly 1,300 names on Linux, and the whole Win32 namespace
 * on Windows, including the macros that make `near`, `far` and `Rectangle`
 * unusable as identifiers. cj_platform.h already promises the opposite
 * ("opaque, no platform headers required"); this is what makes that true of
 * the rest of the API as well.
 *
 * Include it FIRST in an implementation file that needs a native type. The
 * VK_USE_PLATFORM_* define only has an effect before vulkan.h is first seen,
 * and a second inclusion of vulkan.h is a no-op that silently drops the
 * platform surface types.
 *
 * Not part of the public API. Nothing under include/ may include this.
 */
#pragma once

#include <ghoti.io/cjelly/macros.h>

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#else
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>

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

/** Route further mouse events to this window until released.
 *  @return true if the window system took the capture.
 */
bool cj_plat_capture_mouse(uintptr_t handle);

/** Undo cj_plat_capture_mouse.
 *  @return true if a capture was released.
 */
bool cj_plat_release_mouse(void);

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

#ifdef _WIN32
/** The window procedure, defined in src/platform/win32/window.c. */
LRESULT CALLBACK cj_win32_wnd_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/** Per-monitor DPI for the monitor this window is on. */
UINT cj_win32_window_dpi(HWND hwnd);

/** Convert a DPI value to a scale factor, 1.0 being 96 DPI. */
float cj_win32_dpi_to_scale(UINT dpi);
#endif

#ifndef _WIN32
/** Ask the window manager for a window's frame extents (decoration sizes).
 *  @return true if the window manager answered.
 */
bool cj_x11_get_frame_extents(Display* dpy, Window window,
    int32_t* left, int32_t* right, int32_t* top, int32_t* bottom);

/** The X display connection, owned by src/platform/x11/events.c.
 *
 *  Prefixed because it is a definition with external linkage: as plain
 *  `display` it squatted on that name in the static archive, where the
 *  visibility flags that hide it in the shared object do not apply.
 */
extern Display * cj_x11_display;

/** Open the X display connection, if it is not open already.
 *  @return true if a connection is open on return.
 */
bool cj_x11_open_display(void);

/** Close the X display connection, if one is open. Idempotent. */
void cj_x11_close_display(void);

/** Ask the X server for XInput2 events on this window, if XInput2 is there.
 *
 *  window.c calls this after creating a window. It reached it through an
 *  `extern bool cj_x11_select_xinput2_events(Window);` written inside the calling
 *  function, which is a declaration the compiler cannot check against the
 *  definition in another translation unit.
 *
 *  @param window The X window to select events on.
 *  @return true if XInput2 events were selected.
 */
bool cj_x11_select_xinput2_events(Window window);

/** Internal helper to get DPI scale for a window based on its position
 *  (Linux/XRandR).
 *
 *  Declared here rather than in window_internal.h because it names X11 types.
 *  It carried a `#ifndef GHOTI_IO_CJ_WINDOW_INTERNAL_H` guard there, which
 *  read as "only in some configuration" and was decorative: the header uses
 *  #pragma once, nothing defines that macro, so the condition was always
 *  true. The guard here is the real one.
 *
 *  @param dpy X11 display.
 *  @param root Root window.
 *  @param win_x Window X position.
 *  @param win_y Window Y position.
 *  @return DPI scale factor (1.0 = 96 DPI).
 */
float cj_window__get_dpi_scale_linux(Display* dpy, Window root, int32_t win_x, int32_t win_y);
#endif
