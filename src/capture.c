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

/**
 * @file capture.c
 *
 * Reading a copy of a rendered frame out into ordinary memory.
 *
 * The copy itself is made in window.c, inside the frame that drew it, for
 * the reason cj_window_capture() gives. What is here is the half that runs
 * on the CPU: unpacking the swapchain's channel order, and writing a PNG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#include <ghoti.io/cutil/file.h>

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_log.h>
#include <ghoti.io/cjelly/cj_capture.h>
#include <ghoti.io/cjelly/engine_internal.h>
#include <ghoti.io/cjelly/window_internal.h>

#include <ghoti.io/image/codec.h>
#include <ghoti.io/image/doc.h>
#include <ghoti.io/image/raster.h>
#include <ghoti.io/image/stream.h>

/** Where the red channel sits in the swapchain's own byte order. */
typedef struct {
  int red;
  int green;
  int blue;
  int alpha;
  bool understood;
} capture_swizzle_t;

/**
 * Work out the channel order of a swapchain format.
 *
 * Only the 8-bit four-channel formats are handled: those are what a swapchain
 * uses in practice, and guessing at a format that is not one of them would
 * produce a plausible-looking image with the colours wrong, which is worse
 * than refusing.
 */
static capture_swizzle_t capture_swizzle_for(VkFormat format) {
  capture_swizzle_t s = {0, 1, 2, 3, true};
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
      return s;
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB: {
      capture_swizzle_t bgra = {2, 1, 0, 3, true};
      return bgra;
    }
    default: {
      capture_swizzle_t unknown = {0, 1, 2, 3, false};
      return unknown;
    }
  }
}

CJ_API cj_result_t cj_window_capture(
    cj_window_t * window, cj_capture_t * out_capture) {
  if (!out_capture) {
    return CJ_E_INVALID_ARGUMENT;
  }
  memset(out_capture, 0, sizeof(*out_capture));
  if (!window) {
    return CJ_E_INVALID_ARGUMENT;
  }

  cj_window_readback_t readback = {0};
  if (!cj_window__take_capture(window, &readback)) {
    /* Nothing copied yet. Ask for one; the next frame this window presents
     * will record the copy alongside the drawing, and the call after that
     * returns it.
     *
     * The frame is the whole reason this is two calls. A swapchain image
     * belongs to the presentation engine from vkQueuePresentKHR until it is
     * acquired again, so the copy has to be recorded into the frame that
     * drew it. Reading it afterwards - which is what this function used to
     * do - is a write-after-present hazard, reported once per capture by
     * synchronisation validation, and nothing says the image that comes back
     * from the next acquire is the one that was just presented. */
    cj_window__request_capture(window);
    return CJ_E_NOT_READY;
  }

  capture_swizzle_t swizzle = capture_swizzle_for(readback.format);
  if (!swizzle.understood) {
    CJ_ERRORF("cj_window_capture: swapchain format %d is not one this knows "
        "how to unpack",
        (int)readback.format);
    return CJ_E_UNKNOWN;
  }
  if (readback.extent.width == 0 || readback.extent.height == 0) {
    return CJ_E_INVALID_ARGUMENT;
  }

  cj_engine_t * engine = cj_engine_get_current();
  VkDevice device = cj_engine_device(engine);
  if (device == VK_NULL_HANDLE) {
    return CJ_E_UNKNOWN;
  }

  /* The frame that wrote the buffer may still be in flight. A capture is a
   * diagnostic taken occasionally, so waiting for the device is the right
   * trade against threading a fence out of the window. */
  vkDeviceWaitIdle(device);

  void * mapped = NULL;
  if (vkMapMemory(device, readback.memory, 0, readback.size, 0, &mapped)
      != VK_SUCCESS) {
    return CJ_E_UNKNOWN;
  }

  uint8_t * pixels = (uint8_t *)malloc((size_t)readback.size);
  if (!pixels) {
    vkUnmapMemory(device, readback.memory);
    return CJ_E_OUT_OF_MEMORY;
  }

  {
    const uint8_t * src = (const uint8_t *)mapped;
    size_t count = (size_t)readback.extent.width * readback.extent.height;
    for (size_t i = 0; i < count; i++) {
      const uint8_t * in = src + i * 4;
      uint8_t * out = pixels + i * 4;
      out[0] = in[swizzle.red];
      out[1] = in[swizzle.green];
      out[2] = in[swizzle.blue];
      out[3] = in[swizzle.alpha];
    }
  }
  vkUnmapMemory(device, readback.memory);

  out_capture->pixels = pixels;
  out_capture->width = readback.extent.width;
  out_capture->height = readback.extent.height;
  out_capture->stride = (size_t)readback.extent.width * 4u;
  return CJ_SUCCESS;
}

CJ_API void cj_capture_free(cj_capture_t * capture) {
  if (!capture) {
    return;
  }
  free(capture->pixels);
  memset(capture, 0, sizeof(*capture));
}

CJ_API bool cj_capture_pixel(const cj_capture_t * capture, uint32_t x,
    uint32_t y, uint8_t out_rgba[4]) {
  if (!capture || !capture->pixels || !out_rgba) {
    return false;
  }
  if (x >= capture->width || y >= capture->height) {
    return false;
  }
  const uint8_t * pixel = capture->pixels + (size_t)y * capture->stride + x * 4u;
  out_rgba[0] = pixel[0];
  out_rgba[1] = pixel[1];
  out_rgba[2] = pixel[2];
  out_rgba[3] = pixel[3];
  return true;
}

CJ_API cj_result_t cj_capture_write_png(
    const cj_capture_t * capture, const char * path) {
  if (!capture || !capture->pixels || !path) {
    return CJ_E_INVALID_ARGUMENT;
  }

  cj_result_t result = CJ_E_UNKNOWN;
  GIMG_Raster * raster = NULL;
  GIMG_Doc * doc = NULL;
  GIMG_Stream * stream = NULL;

  /* Borrowed: the raster reads the capture's buffer and does not take it. */
  if (gimg_raster_create(capture->width, capture->height, &GIMG_PIXEL_RGBA8,
          GIMG_RASTER_BORROWED, capture->pixels, capture->stride, &raster)
      != GIMG_OK) {
    goto done;
  }
  if (gimg_doc_from_raster(raster, &doc) != GIMG_OK) {
    goto done;
  }
  if (gimg_stream_create_memory_output(&stream) != GIMG_OK) {
    goto done;
  }

  GIMG_Save_Report report = {0};
  if (gimg_doc_save(doc, stream, "png", NULL, &report) != GIMG_OK) {
    goto done;
  }

  const void * encoded = NULL;
  size_t encoded_size = 0;
  gimg_stream_output_buffer(stream, &encoded, &encoded_size);
  if (!encoded || encoded_size == 0) {
    goto done;
  }

  /*
   * Written through cutil rather than fopen/fwrite/fclose.  Two things come
   * with that.  The close was never checked, and it is the call that reports
   * a full disk for everything stdio still had buffered - so a capture that
   * did not reach the disk returned CJ_SUCCESS.  And the bytes went straight
   * to their final name, so anything watching the directory could pick up a
   * half-written PNG; gcu_file_write_atomic() writes a temporary beside it
   * and renames, so the path either does not exist yet or is a whole image.
   *
   * PERMS_DEFAULT, not the PERMS_PRIVATE zero value: a capture is an output
   * file the caller asked for, in a directory the caller named, and nothing
   * in it is a secret.  The first version of this conversion had no such
   * argument and inherited the temporary's 0600, which quietly made every
   * screenshot more restrictive than the fopen() it replaced - that defect
   * is why cutil grew the parameter.
   */
  switch (gcu_file_write_atomic(path, encoded, encoded_size,
      GCU_FILE_SYNC_FULL, GCU_FILE_PERMS_DEFAULT, NULL)) {
    case GCU_FILE_OK:
      result = CJ_SUCCESS;
      break;
    case GCU_FILE_ERR_INVALID:
      result = CJ_E_INVALID_ARGUMENT;
      break;
    case GCU_FILE_ERR_OOM:
      result = CJ_E_OUT_OF_MEMORY;
      break;
    case GCU_FILE_ERR_NOT_FOUND:
      /* The destination's directory does not exist.  cutil does not create
       * one, deliberately, so this is the caller naming somewhere that is
       * not there rather than a failure to write. */
      result = CJ_E_NOT_FOUND;
      break;
    case GCU_FILE_ERR_EXISTS:
      result = CJ_E_ALREADY_EXISTS;
      break;
    case GCU_FILE_ERR_ACCESS:
      /* cj_result_t has no permission code, so this is the honest floor.
       * Worth a code of its own if anything ever needs to branch on it. */
      result = CJ_E_UNKNOWN;
      break;
    case GCU_FILE_ERR_LIMIT:
    case GCU_FILE_ERR_NOT_EMPTY:
    case GCU_FILE_ERR_IO:
    case GCU_FILE_RESULT_COUNT:
      result = CJ_E_UNKNOWN;
      break;
  }

done:
  gimg_stream_destroy(stream);
  gimg_doc_destroy(doc);
  gimg_raster_destroy(raster);
  return result;
}
