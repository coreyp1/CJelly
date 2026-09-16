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

/** Copy the frame a window is currently displaying.
 *
 *  Captures the image last handed to the presentation engine - what is on
 *  screen, not what is part-way through being drawn. The window must have
 *  presented at least one frame.
 *
 *  The caller owns the result and releases it with cj_capture_free().
 *
 *  @param window The window to read from.
 *  @param out_capture Receives the frame on success.
 *  @return CJ_SUCCESS, CJ_E_INVALID_ARGUMENT for a NULL argument or a window
 *          that has not presented, CJ_E_OUT_OF_MEMORY, or CJ_E_UNKNOWN when
 *          the device refuses the readback.
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
