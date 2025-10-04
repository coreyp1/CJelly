#ifndef CJELLY_FORMAT_IMAGE_H
#define CJELLY_FORMAT_IMAGE_H

#include <cjelly/macros.h>


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
  CJELLY_FORMAT_IMAGE_ERR_IO               /**< I/O error while reading/writing the file */
} CJellyFormatImageError;

/**
 * @brief Enumeration of supported image formats.
 */
typedef enum {
  CJELLY_FORMAT_IMAGE_UNKNOWN, /**< Unknown image format */
  CJELLY_FORMAT_IMAGE_BMP,     /**< BMP image format */
} CJellyFormatImageType;

/**
 * @brief Represents a generic image.
 *
 * This structure holds general image properties such as dimensions,
 * bit depth, and a pointer to the actual pixel data.
 */
typedef struct CJellyFormatImageRaw {
    int width;                /**< The width of the image in pixels. */
    int height;               /**< The height of the image in pixels. */
    int channels;             /**< The number of color channels. */
    size_t bitdepth;          /**< The bit depth of the image. */
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
 * This function examines the provided file (by extension or header inspection)
 * and calls the appropriate format-specific loader.
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

#endif // CJELLY_FORMAT_IMAGE_H
