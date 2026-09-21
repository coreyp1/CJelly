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
 * CJelly — Minimal C API stubs
 *
 * This is a design-time stub for headers. Implementation is TBD.
 */
#pragma once

#include <ghoti.io/cjelly/macros.h>
#include <stdint.h>
#include "cj_types.h"

/** @file cj_platform.h
 *  @brief Platform surface descriptors (opaque, no platform headers required).
 *
 *  Provide one of these via cj_window_desc_t::native_surface_desc when you want
 *  CJelly to adopt an existing native surface instead of creating one.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Win32 HWND/HINSTANCE pair (no headers required). */
typedef struct cj_native_win32_t {
  void* hinstance;  /**< HINSTANCE */
  void* hwnd;       /**< HWND */
} cj_native_win32_t;

/** X11 Display/Window pair (no headers required). */
typedef struct cj_native_x11_t {
  void* display;    /**< Display* */
  uint64_t window;  /**< Window (XID) */
} cj_native_x11_t;

/** Wayland display/surface pointers. */
typedef struct cj_native_wayland_t {
  void* display;    /**< wl_display* */
  void* surface;    /**< wl_surface* */
} cj_native_wayland_t;

/** Cocoa/NSView or CAMetalLayer hosting Vk surface via NSView*. */
typedef struct cj_native_cocoa_t {
  void* ns_view;    /**< NSView* */
} cj_native_cocoa_t;

/** Tagged union header + storage. */
typedef enum cj_native_tag_t {
  CJ_NATIVE_NONE = 0,
  CJ_NATIVE_WIN32,
  CJ_NATIVE_X11,
  CJ_NATIVE_WAYLAND,
  CJ_NATIVE_COCOA,
} cj_native_tag_t;

typedef struct cj_native_surface_desc_t {
  cj_native_tag_t tag;
  union {
    cj_native_win32_t   win32;
    cj_native_x11_t     x11;
    cj_native_wayland_t wayland;
    cj_native_cocoa_t   cocoa;
  } u;
} cj_native_surface_desc_t;

#ifdef __cplusplus
} /* extern "C" */
#endif
