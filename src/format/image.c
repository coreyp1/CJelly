#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <cjelly/format/image.h>
#include <cjelly/format/image/bmp.h>

CJellyFormatImageError cjelly_format_image_load(const char * filename, CJellyFormatImage * * out_image) {
  *out_image = NULL;

  // Detect the image type so that we can call the appropriate loader.
  CJellyFormatImageType type;
  CJellyFormatImageError err = cjelly_format_image_detect_type(filename, &type);
  if (err != CJELLY_FORMAT_IMAGE_SUCCESS) return err;

  // Load the image based on the detected type.
  switch (type) {
    case CJELLY_FORMAT_IMAGE_BMP:
      err = cjelly_format_image_bmp_load(filename, out_image);
      break;
    default:
      err = CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
      goto ERROR_IMAGE_CLEANUP;
  }
  if (err != CJELLY_FORMAT_IMAGE_SUCCESS) return err;

  // Lastly, opy the filename.
  size_t len = strlen(filename);
  (*out_image)->name = (unsigned char *)malloc(len + 1);
  if (!(*out_image)->name) goto ERROR_IMAGE_CLEANUP;
  memcpy((*out_image)->name, filename, len + 1);

  return CJELLY_FORMAT_IMAGE_SUCCESS;

ERROR_IMAGE_CLEANUP:
  if (err == CJELLY_FORMAT_IMAGE_SUCCESS) {
    err = CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
  }
  return err;
}


void cjelly_format_image_free(CJellyFormatImage * image) {
  if (!image) return;

  switch (image->type) {
    case CJELLY_FORMAT_IMAGE_BMP:
      //cjelly_format_image_bmp_free((CJellyFormatImageBmp *)image);
      break;
    default:
      break;
  }
  free(image->name);
  free(image);
}


/**
 * @brief Structure for holding image signature information.
 *
 * This structure is a helper struct that is only used in the
 * cjelly_format_image_detect_type function.
 */ 
typedef struct {
  CJellyFormatImageType type;      /**< The image type associated with the signature */
  const unsigned char * signature; /**< Pointer to the signature bytes */
  size_t length;                   /**< Number of bytes in the signature */
} ImageSignature;  


// Define known image signatures.
static const unsigned char signature_bmp[] = {'B', 'M'};
// static const unsigned char signature_png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
// static const unsigned char signature_jpg[] = {0xFF, 0xD8, 0xFF};


static ImageSignature signatures[] = {
  { CJELLY_FORMAT_IMAGE_BMP, signature_bmp, sizeof(signature_bmp) },
  // { CJELLY_FORMAT_IMAGE_PNG, signature_png, sizeof(signature_png) },
  // { CJELLY_FORMAT_IMAGE_JPG, signature_jpg, sizeof(signature_jpg) },
};

CJellyFormatImageError cjelly_format_image_detect_type(const char * path, CJellyFormatImageType * out_type) {
  *out_type = CJELLY_FORMAT_IMAGE_UNKNOWN;

  if (!path || !out_type) {
      // Invalid arguments; for simplicity, return an invalid format error.
      return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND;
  }

  // Determine the maximum signature length required.
  size_t max_sig_length = 0;
  size_t num_signatures = sizeof(signatures) / sizeof(signatures[0]);
  for (size_t i = 0; i < num_signatures; i++) {
    if (signatures[i].length > max_sig_length) {
      max_sig_length = signatures[i].length;
    }
  }

  // Allocate a buffer to read the header.
  unsigned char * buffer = (unsigned char *)malloc(max_sig_length);
  if (!buffer) {
    fclose(fp);
    return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
  }

  // Read the required number of bytes.
  size_t bytes_read = fread(buffer, sizeof(unsigned char), max_sig_length, fp);
  fclose(fp);
  if (bytes_read) {
    // Iterate over each known signature and check for a match.
    for (size_t i = 0; i < num_signatures; i++) {
      if ((bytes_read >= signatures[i].length) && !memcmp(buffer, signatures[i].signature, signatures[i].length)) {
        *out_type = signatures[i].type;
        free(buffer);
        return CJELLY_FORMAT_IMAGE_SUCCESS;
      }
    }
  }

  free(buffer);
  return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
}


const char * cjelly_format_image_strerror(CJellyFormatImageError err) {
  switch (err) {
    case CJELLY_FORMAT_IMAGE_SUCCESS:
      return "No error";
    case CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND:
      return "OBJ file not found";
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
