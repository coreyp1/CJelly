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

#ifndef GHOTI_IO_CJ_FORMAT_IMAGE_H
#define GHOTI_IO_CJ_FORMAT_IMAGE_H

#include <stddef.h>

#include <ghoti.io/cjelly/macros.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @file cjelly_format_image.h
 * @brief Generic image structure and loader interface for the CJelly library.
 */

/**
 * @brief Enumeration of error codes for the image object.
 */
typedef enum {
  CJELLY_FORMAT_IMAGE_SUCCESS = 0,         /**< No error */
  CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND,  /**< Unable to open the file */
  CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY,   /**< Memory allocation failure */
  CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT,  /**< File contains an invalid format */
  CJELLY_FORMAT_IMAGE_ERR_IO,              /**< I/O error while reading/writing the file */
  CJELLY_FORMAT_IMAGE_ERR_LIMIT,           /**< File is larger than ::CJELLY_FORMAT_IMAGE_MAX_FILE_BYTES */
  CJELLY_FORMAT_IMAGE_ERR_INVALID_ARGUMENT /**< A caller-supplied argument is wrong */
} CJellyFormatImageError;

/**
 * @brief The largest file ::cjelly_format_image_load() will read.
 *
 * The reader used to have no cap at all, on the reasoning that these are
 * texture assets rather than arbitrary user input.  That is a statement about
 * how the caller is expected to behave, not something this library can check,
 * and the cost of being wrong is paid before anything has looked at the
 * bytes: the file is in memory by the time the first header field is read.
 *
 * 256 MiB is an uncompressed 8192x8192 RGBA8 BMP exactly, which is the
 * largest thing that is plausibly a texture in any format handled here.  A
 * file past it yields ::CJELLY_FORMAT_IMAGE_ERR_LIMIT with nothing allocated;
 * the limit is a promise and not a truncation.
 *
 * It is a constant because CJelly has no limits structure to hang it on yet.
 * When it grows one, this becomes its default rather than the whole rule.
 */
#define CJELLY_FORMAT_IMAGE_MAX_FILE_BYTES ((size_t)256 * 1024 * 1024)

/**
 * @brief Enumeration of supported image formats.
 */
typedef enum {
  CJELLY_FORMAT_IMAGE_UNKNOWN, /**< Unknown image format */
  CJELLY_FORMAT_IMAGE_BMP,     /**< BMP image format */
  CJELLY_FORMAT_IMAGE_PNG,     /**< PNG image format (including APNG) */
  CJELLY_FORMAT_IMAGE_JPEG,    /**< JPEG image format */
} CJellyFormatImageType;

/**
 * @brief Represents a generic image.
 *
 * Pixels are always tightly packed 8-bit RGBA with no row padding, whatever
 * the source file contained, so the buffer can be handed to Vulkan as
 * VK_FORMAT_R8G8B8A8_UNORM directly.  `channels` is therefore always 4 and
 * `bitdepth` always 32; both are kept for clarity at call sites.
 */
typedef struct CJellyFormatImageRaw {
    int width;                /**< The width of the image in pixels. */
    int height;               /**< The height of the image in pixels. */
    int channels;             /**< The number of color channels; always 4. */
    size_t bitdepth;          /**< Bits per pixel; always 32. */
    size_t data_size;         /**< The size of the pixel data in bytes. */
    unsigned char * data;     /**< The raw pixel data. */
} CJellyFormatImageRaw;

/**
 * @brief Represents a generic image.
 *
 * This structure holds general image information such as the file name and
 * image type, which is expanded on by the format-specific loaders.
 */
typedef struct CJellyFormatImage {
  unsigned char * name;       /**< The name of the file. */
  CJellyFormatImageRaw * raw; /**< The raw image data. */
  CJellyFormatImageType type; /**< Image format type. */
} CJellyFormatImage;

/**
 * @brief Loads an image from file.
 *
 * The file is identified by its signature and decoded by the Ghoti.io Image
 * library, so every format that library has a codec for is accepted: BMP,
 * PNG, and JPEG.
 *
 * @param filename Path to the image file.
 * @param out_image Output pointer that will point to the allocated CJellyFormatImage on success.
 * @return 0 on success, non-zero error code on failure.
 */
CJellyFormatImageError cjelly_format_image_load(const char * filename, CJellyFormatImage * * out_image);

/**
 * @brief Deallocates the memory used by an image.  The image pointer will be
 * set to NULL.
 *
 * @param image Pointer to the CJellyFormatImage to be freed.
 */
void cjelly_format_image_free(CJellyFormatImage * image);

/**
 * @brief Detect the type of image file at the given path.
 *
 * @param path The path to the image file.
 * @param out_type The detected image type.
 * @return CJellyFormatImageError
 */
CJellyFormatImageError cjelly_format_image_detect_type(const char * path, CJellyFormatImageType * out_type);

/**
 * @brief Converts an Image error code to a human-readable error message.
 *
 * @param err The CJellyFormatImageError code.
 * @return A constant string describing the error.
 */
const char * cjelly_format_image_strerror(CJellyFormatImageError err);


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // GHOTI_IO_CJ_FORMAT_IMAGE_H
