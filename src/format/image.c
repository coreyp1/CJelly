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
 * @file
 * Image loading for CJelly, backed by the Ghoti.io Image library.
 *
 * CJelly used to carry its own BMP reader.  That reader is now a codec in
 * `image` alongside PNG and JPEG, so this file is a thin adapter: it reads a
 * file into memory, hands it to the image library's codec registry, and
 * copies the decoded RGBA8 pixels into the CJellyFormatImage shape the
 * renderer expects.  Supporting PNG and JPEG came along with the move.
 *
 * Pixels always arrive as tightly packed RGBA8, four channels, no row
 * padding, regardless of what the file contained.  Callers upload them to
 * Vulkan as VK_FORMAT_R8G8B8A8_UNORM without converting anything.
 */

#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/file.h>

#include <ghoti.io/image/codec.h>
#include <ghoti.io/image/core.h>
#include <ghoti.io/image/doc.h>
#include <ghoti.io/image/raster.h>
#include <ghoti.io/image/stream.h>

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/format/image.h>

/** Translate an image library result into the CJelly error enum. */
static CJellyFormatImageError from_gimg(GIMG_Result r) {
  switch (r) {
    case GIMG_OK:
      return CJELLY_FORMAT_IMAGE_SUCCESS;
    case GIMG_ERR_IO:
      return CJELLY_FORMAT_IMAGE_ERR_IO;
    case GIMG_ERR_OOM:
      return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
    case GIMG_ERR_FORMAT:
    case GIMG_ERR_UNSUPPORTED:
    case GIMG_ERR_CORRUPT:
      return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
    case GIMG_ERR_LIMIT:
      /* This used to answer OUT_OF_MEMORY, because there was no limit code to
       * map onto and the two failures do look alike from here.  They are not
       * alike to a caller: one says to retry with less running, the other
       * says the file will never be accepted however much memory there is. */
      return CJELLY_FORMAT_IMAGE_ERR_LIMIT;
    default:
      return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }
}

/** Map a codec name from the registry onto the CJelly type enum. */
static CJellyFormatImageType type_from_codec_name(const char * name) {
  if (!name) {
    return CJELLY_FORMAT_IMAGE_UNKNOWN;
  }
  if (!strcmp(name, "bmp")) {
    return CJELLY_FORMAT_IMAGE_BMP;
  }
  if (!strcmp(name, "png")) {
    return CJELLY_FORMAT_IMAGE_PNG;
  }
  if (!strcmp(name, "jpeg")) {
    return CJELLY_FORMAT_IMAGE_JPEG;
  }
  return CJELLY_FORMAT_IMAGE_UNKNOWN;
}

/**
 * Read a whole file into a freshly allocated buffer.
 *
 * cutil owns whole-file reading for the suite, so this is the boundary
 * between its result enum and ours rather than a sixth copy of the loop.
 *
 * What stood here sized the file with fseek() and ftell() and then read that
 * many bytes, which reports an empty file for anything whose size the kernel
 * does not know in advance - a pipe, a FIFO, /dev/stdin, anything under
 * /proc.  Naming a texture that way is unusual but not forbidden, and the
 * failure was silent in the worst direction: the read "succeeded" with
 * nothing in it, and the caller reported a corrupt image.  cutil reads in
 * chunks instead, and on Windows it opens through the wide entry point, which
 * fopen() cannot do for a path whose bytes are UTF-8.
 *
 * It also gains the cap this reader never had.  See
 * ::CJELLY_FORMAT_IMAGE_MAX_FILE_BYTES for why a texture asset is not the
 * same thing as a trusted one.
 */
static CJellyFormatImageError read_file(const char * path,
    const cj_allocator_t * allocator, unsigned char ** out_data,
    size_t * out_size) {
  *out_data = NULL;
  *out_size = 0;

  void * data = NULL;
  size_t length = 0;
  switch (gcu_file_read(
      path, CJELLY_FORMAT_IMAGE_MAX_FILE_BYTES, allocator, &data, &length)) {
    case GCU_FILE_OK:
      break;
    case GCU_FILE_ERR_OOM:
      return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
    case GCU_FILE_ERR_LIMIT:
      return CJELLY_FORMAT_IMAGE_ERR_LIMIT;
    case GCU_FILE_ERR_NOT_FOUND:
      return CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND;
    case GCU_FILE_ERR_INVALID:
      /* ENAMETOOLONG lands here.  It says the path cannot name a file on this
       * filesystem, which is a statement about the argument and not about any
       * bytes - nothing was opened, so there is no format to call invalid. */
      return CJELLY_FORMAT_IMAGE_ERR_INVALID_ARGUMENT;
    case GCU_FILE_ERR_ACCESS:
    case GCU_FILE_ERR_EXISTS:
    case GCU_FILE_ERR_NOT_EMPTY:
    case GCU_FILE_ERR_IO:
    case GCU_FILE_RESULT_COUNT:
      /* EXISTS and NOT_EMPTY cannot arise from a read; they are named so that
       * this switch stays exhaustive and the next added code is a compile
       * error here rather than a silent fall-through. */
      return CJELLY_FORMAT_IMAGE_ERR_IO;
  }

  /* An empty file is not a format this library has a codec for, and saying so
   * here keeps the probe from having to describe zero bytes. */
  if (length == 0) {
    gcu_file_free(allocator, data);
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }

  *out_data = (unsigned char *)data;
  *out_size = length;
  return CJELLY_FORMAT_IMAGE_SUCCESS;
}

/** Copy a decoded raster into a newly allocated tightly packed RGBA buffer. */
static CJellyFormatImageError pack_raster(const GIMG_Raster * raster,
    const cj_allocator_t * allocator, CJellyFormatImageRaw * raw) {
  uint32_t width = gimg_raster_width(raster);
  uint32_t height = gimg_raster_height(raster);
  const GIMG_Pixel_Format * format = gimg_raster_format(raster);

  if (!format || format->channel_model != GIMG_CHANNEL_RGBA ||
      format->channel_count != 4 || format->bits_per_channel[0] != 8) {
    // Every codec in the library decodes color images to RGBA8; anything else
    // would silently produce garbage if copied blindly.
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }

  size_t row_bytes = (size_t)width * 4u;
  size_t total = row_bytes * (size_t)height;
  if (!width || !height || total / 4u / (size_t)width != (size_t)height) {
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }

  unsigned char * data =
      (unsigned char *)gcu_allocator_malloc(allocator, total);
  if (!data) {
    return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
  }

  // The raster may carry row padding; the CJelly buffer never does.
  const unsigned char * src =
      (const unsigned char *)gimg_raster_pixels_const(raster);
  size_t src_stride = gimg_raster_stride_bytes(raster);
  for (uint32_t y = 0; y < height; y++) {
    memcpy(data + ((size_t)y * row_bytes), src + ((size_t)y * src_stride),
        row_bytes);
  }

  raw->width = (int)width;
  raw->height = (int)height;
  raw->channels = 4;
  raw->bitdepth = 32;
  raw->data = data;
  raw->data_size = total;
  return CJELLY_FORMAT_IMAGE_SUCCESS;
}

CJellyFormatImageError cjelly_format_image_load(const char * filename,
    const cj_allocator_t * allocator, CJellyFormatImage ** out_image) {
  if (!filename || !out_image) {
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_ARGUMENT;
  }
  *out_image = NULL;

  unsigned char * file_data = NULL;
  size_t file_size = 0;
  CJellyFormatImageError err =
      read_file(filename, allocator, &file_data, &file_size);
  if (err != CJELLY_FORMAT_IMAGE_SUCCESS) {
    return err;
  }

  GIMG_Stream * stream = NULL;
  GIMG_Doc * doc = NULL;
  GIMG_Raster * raster = NULL;
  CJellyFormatImage * image = NULL;

  GIMG_Result r = gimg_stream_create_memory(file_data, file_size, &stream);
  if (r != GIMG_OK) {
    err = from_gimg(r);
    goto cleanup;
  }

  // Identify the format before loading so the type can be reported even
  // though the codec registry is what actually dispatches.
  GIMG_Probe_Result probe = {NULL, 0, {0, 0, 0, 0}};
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  if (gimg_probe(stream, &probe) == GIMG_OK) {
    type = type_from_codec_name(probe.format_name);
  }

  r = gimg_doc_load(stream, NULL, NULL, &doc);
  if (r != GIMG_OK) {
    err = from_gimg(r);
    goto cleanup;
  }

  GIMG_Item * item = gimg_doc_item(doc, 0);
  if (!item) {
    err = CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
    goto cleanup;
  }
  r = gimg_item_decode(item, NULL, &raster);
  if (r != GIMG_OK) {
    err = from_gimg(r);
    goto cleanup;
  }

  image = (CJellyFormatImage *)gcu_allocator_calloc(
      allocator, 1, sizeof(CJellyFormatImage));
  if (!image) {
    err = CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  /* Recorded before anything else is attached to the image, so that the
   * cleanup path below frees through the same allocator on every branch. */
  image->allocator = allocator;
  image->raw = (CJellyFormatImageRaw *)gcu_allocator_calloc(
      allocator, 1, sizeof(CJellyFormatImageRaw));
  if (!image->raw) {
    err = CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  image->type = type;

  err = pack_raster(raster, allocator, image->raw);
  if (err != CJELLY_FORMAT_IMAGE_SUCCESS) {
    goto cleanup;
  }

  size_t name_len = strlen(filename);
  image->name = (unsigned char *)gcu_allocator_malloc(allocator, name_len + 1);
  if (!image->name) {
    err = CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  memcpy(image->name, filename, name_len + 1);

  *out_image = image;
  image = NULL;
  err = CJELLY_FORMAT_IMAGE_SUCCESS;

cleanup:
  cjelly_format_image_free(image);
  if (raster) {
    gimg_raster_destroy(raster);
  }
  if (doc) {
    gimg_doc_destroy(doc);
  }
  if (stream) {
    gimg_stream_destroy(stream);
  }
  gcu_file_free(allocator, file_data);
  return err;
}

void cjelly_format_image_free(CJellyFormatImage * image) {
  if (!image) {
    return;
  }
  /* The allocator the image was built with, not whatever is current: a
   * caller may free an image long after moving on to a different one. */
  const cj_allocator_t * allocator = image->allocator;
  if (image->raw) {
    gcu_allocator_free(allocator, image->raw->data);
    image->raw->data = NULL;
    image->raw->data_size = 0;
    gcu_allocator_free(allocator, image->raw);
    image->raw = NULL;
  }
  gcu_allocator_free(allocator, image->name);
  image->name = NULL;
  image->type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  gcu_allocator_free(allocator, image);
}

CJellyFormatImageError cjelly_format_image_detect_type(const char * path,
    const cj_allocator_t * allocator, CJellyFormatImageType * out_type) {
  // Validate before writing: the assignment used to come first, so passing a
  // NULL out_type crashed instead of returning the error it checks for.
  if (!path || !out_type) {
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_ARGUMENT;
  }
  *out_type = CJELLY_FORMAT_IMAGE_UNKNOWN;

  unsigned char * file_data = NULL;
  size_t file_size = 0;
  CJellyFormatImageError err =
      read_file(path, allocator, &file_data, &file_size);
  if (err != CJELLY_FORMAT_IMAGE_SUCCESS) {
    return err;
  }

  GIMG_Stream * stream = NULL;
  if (gimg_stream_create_memory(file_data, file_size, &stream) != GIMG_OK) {
    gcu_file_free(allocator, file_data);
    return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
  }

  GIMG_Probe_Result probe = {NULL, 0, {0, 0, 0, 0}};
  GIMG_Result r = gimg_probe(stream, &probe);
  if (r == GIMG_OK && probe.format_name) {
    *out_type = type_from_codec_name(probe.format_name);
  }

  gimg_stream_destroy(stream);
  gcu_file_free(allocator, file_data);

  return *out_type == CJELLY_FORMAT_IMAGE_UNKNOWN
      ? CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT
      : CJELLY_FORMAT_IMAGE_SUCCESS;
}

const char * cjelly_format_image_strerror(CJellyFormatImageError err) {
  switch (err) {
    case CJELLY_FORMAT_IMAGE_SUCCESS:
      return "No error";
    case CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND:
      return "Image file not found";
    case CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY:
      return "Out of memory";
    case CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT:
      return "Invalid image file format";
    case CJELLY_FORMAT_IMAGE_ERR_IO:
      return "I/O error when reading/writing the image file";
    case CJELLY_FORMAT_IMAGE_ERR_LIMIT:
      return "Image file is larger than the reader's limit";
    case CJELLY_FORMAT_IMAGE_ERR_INVALID_ARGUMENT:
      return "Invalid argument";
    default:
      return "Unknown error";
  }
}
