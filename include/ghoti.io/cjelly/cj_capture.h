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
 * CJelly — reading a rendered frame back out of a window.
 */
#pragma once

#include <ghoti.io/cjelly/macros.h>
#include <stdint.h>
#include <stddef.h>
#include "cj_types.h"
#include "cj_result.h"

/** @file cj_capture.h
 *  @brief Copying what a window is displaying into ordinary memory.
 *
 *  This exists because a toolkit that draws every pixel itself is otherwise
 *  opaque: there is no native control for a test to query, and no way to
 *  assert on what was drawn beyond a person looking at it. Capturing the
 *  presented frame turns rendering into something a test can make statements
 *  about.
 *
 *  It is deliberately part of the shipped API rather than a test-only hook.
 *  An application built on CJelly needs exactly the same facility to test its
 *  own interface, and a side channel that only CJelly's tests reach is a side
 *  channel that only CJelly's tests keep working.
 *
 *  Reading back a frame stalls the device. It is a diagnostic, not something
 *  to do every frame.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** A captured frame.
 *
 *  Pixels are 8-bit RGBA, tightly packed, first row at the top, whatever the
 *  swapchain's own format and channel order happen to be. The values are in
 *  the same colour space the display received them in - usually sRGB-encoded,
 *  which is what an image file wants.
 */
typedef struct cj_capture_t {
  uint8_t* pixels;   /**< width * height * 4 bytes, or NULL if empty. */
  uint32_t width;    /**< Width in pixels. */
  uint32_t height;   /**< Height in pixels. */
  size_t stride;     /**< Bytes per row; always width * 4. */
} cj_capture_t;

/** Copy a frame the window has drawn.
 *
 *  **This takes two calls, one frame apart.** The first returns
 *  CJ_E_NOT_READY and asks the window for a copy; the next frame it presents
 *  records that copy alongside the drawing; a later call returns it. So:
 *
 *  @code
 *  cj_capture_t shot = {0};
 *  if (cj_window_capture(window, &shot) == CJ_SUCCESS) {
 *    cj_capture_write_png(&shot, "frame.png");
 *    cj_capture_free(&shot);
 *  }
 *  // ...otherwise let the window render a frame and call again.
 *  @endcode
 *
 *  The frame in between is not a limitation of this implementation; it is
 *  what a swapchain readback is. A swapchain image belongs to the
 *  presentation engine from the moment it is presented until it is acquired
 *  again, so the copy has to be recorded into the frame that draws it - and
 *  that frame has not happened yet when the first call is made. Reading the
 *  last presented image instead, which is what this function did until the
 *  hazard was reported, works on every driver tried and is still a
 *  write-after-present.
 *
 *  What comes back is therefore the frame that followed the request, not the
 *  one on screen when it was made. For anything animating they differ.
 *
 *  Requesting twice before reading does not queue a second copy: a window
 *  holds one frame at a time, and reading it hands it over, so the call after
 *  a successful one asks again.
 *
 *  The caller owns the result and releases it with cj_capture_free().
 *
 *  @param window The window to read from.
 *  @param out_capture Receives the frame on success, and is zeroed otherwise.
 *  @return CJ_SUCCESS with a frame; CJ_E_NOT_READY having asked for one, so
 *          call again after the window renders; CJ_E_INVALID_ARGUMENT for a
 *          NULL argument; CJ_E_OUT_OF_MEMORY; or CJ_E_UNKNOWN when the
 *          swapchain format is not one this can unpack or the device refuses
 *          the mapping.
 */
CJ_API cj_result_t cj_window_capture(cj_window_t* window, cj_capture_t* out_capture);

/** Release a captured frame. A zeroed or already-released capture is fine.
 *  @param capture The capture to release.
 */
CJ_API void cj_capture_free(cj_capture_t* capture);

/** Read one pixel, as a convenience for assertions.
 *
 *  @param capture The capture.
 *  @param x Column, from the left.
 *  @param y Row, from the top.
 *  @param out_rgba Receives four bytes: red, green, blue, alpha.
 *  @return true when the coordinates are inside the capture.
 */
CJ_API bool cj_capture_pixel(const cj_capture_t* capture, uint32_t x, uint32_t y,
                             uint8_t out_rgba[4]);

/** Write a capture to a PNG file.
 *
 *  Uses the Ghoti.io image library, which CJelly already depends on, so a
 *  captured frame can be looked at without any separate tooling.
 *
 *  @param capture The capture.
 *  @param path Destination path.
 *  @return CJ_SUCCESS, or an error code.
 */
CJ_API cj_result_t cj_capture_write_png(const cj_capture_t* capture, const char* path);

#ifdef __cplusplus
} /* extern "C" */
#endif
