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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/image/codec.h>
#include <ghoti.io/image/core.h>
#include <ghoti.io/image/doc.h>
#include <ghoti.io/image/raster.h>
#include <ghoti.io/image/stream.h>

#include <cjelly/format/image.h>

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
      return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
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
 * The image library works from memory streams, and these are texture assets
 * rather than arbitrary user input, so reading the file whole keeps the
 * adapter simple.
 */
static CJellyFormatImageError read_file(
    const char * path, unsigned char ** out_data, size_t * out_size) {
  *out_data = NULL;
  *out_size = 0;

  FILE * fp = fopen(path, "rb");
  if (!fp) {
    return CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return CJELLY_FORMAT_IMAGE_ERR_IO;
  }
  long length = ftell(fp);
  if (length < 0) {
    fclose(fp);
    return CJELLY_FORMAT_IMAGE_ERR_IO;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return CJELLY_FORMAT_IMAGE_ERR_IO;
  }
  if (length == 0) {
    fclose(fp);
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }

  unsigned char * data = (unsigned char *)malloc((size_t)length);
  if (!data) {
    fclose(fp);
    return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
  }

  size_t read = fread(data, 1, (size_t)length, fp);
  fclose(fp);
  if (read != (size_t)length) {
    free(data);
    return CJELLY_FORMAT_IMAGE_ERR_IO;
  }

  *out_data = data;
  *out_size = (size_t)length;
  return CJELLY_FORMAT_IMAGE_SUCCESS;
}

/** Copy a decoded raster into a newly allocated tightly packed RGBA buffer. */
static CJellyFormatImageError pack_raster(
    const GIMG_Raster * raster, CJellyFormatImageRaw * raw) {
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

  unsigned char * data = (unsigned char *)malloc(total);
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

CJellyFormatImageError cjelly_format_image_load(
    const char * filename, CJellyFormatImage ** out_image) {
  if (!filename || !out_image) {
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }
  *out_image = NULL;

  unsigned char * file_data = NULL;
  size_t file_size = 0;
  CJellyFormatImageError err = read_file(filename, &file_data, &file_size);
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

  image = (CJellyFormatImage *)calloc(1, sizeof(CJellyFormatImage));
  if (!image) {
    err = CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  image->raw = (CJellyFormatImageRaw *)calloc(1, sizeof(CJellyFormatImageRaw));
  if (!image->raw) {
    err = CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  image->type = type;

  err = pack_raster(raster, image->raw);
  if (err != CJELLY_FORMAT_IMAGE_SUCCESS) {
    goto cleanup;
  }

  size_t name_len = strlen(filename);
  image->name = (unsigned char *)malloc(name_len + 1);
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
  free(file_data);
  return err;
}

void cjelly_format_image_free(CJellyFormatImage * image) {
  if (!image) {
    return;
  }
  if (image->raw) {
    free(image->raw->data);
    image->raw->data = NULL;
    image->raw->data_size = 0;
    free(image->raw);
    image->raw = NULL;
  }
  free(image->name);
  image->name = NULL;
  image->type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  free(image);
}

CJellyFormatImageError cjelly_format_image_detect_type(
    const char * path, CJellyFormatImageType * out_type) {
  // Validate before writing: the assignment used to come first, so passing a
  // NULL out_type crashed instead of returning the error it checks for.
  if (!path || !out_type) {
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }
  *out_type = CJELLY_FORMAT_IMAGE_UNKNOWN;

  unsigned char * file_data = NULL;
  size_t file_size = 0;
  CJellyFormatImageError err = read_file(path, &file_data, &file_size);
  if (err != CJELLY_FORMAT_IMAGE_SUCCESS) {
    return err;
  }

  GIMG_Stream * stream = NULL;
  if (gimg_stream_create_memory(file_data, file_size, &stream) != GIMG_OK) {
    free(file_data);
    return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
  }

  GIMG_Probe_Result probe = {NULL, 0, {0, 0, 0, 0}};
  GIMG_Result r = gimg_probe(stream, &probe);
  if (r == GIMG_OK && probe.format_name) {
    *out_type = type_from_codec_name(probe.format_name);
  }

  gimg_stream_destroy(stream);
  free(file_data);

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
    default:
      return "Unknown error";
  }
}
