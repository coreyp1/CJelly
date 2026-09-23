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

/** Drain the window system's event queue and dispatch to window callbacks.
 *
 *  Defined by whichever module under src/platform was built. Exported, and
 *  declared nowhere public - it was forward-declared inside cjelly.c to stop
 *  an implicit-declaration warning, which is a declaration no other caller
 *  can see.
 */
CJ_API void processWindowEvents(void);

#ifndef _WIN32
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
