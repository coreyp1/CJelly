#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjelly/format/image/bmp.h>

// BMP file header structures.
// The structures are packed so that they exactly match the file layout.
#pragma pack(push, 1)
typedef struct {
  unsigned short bfType;      /**< File type; must be 'BM' (0x4D42) */
  unsigned int   bfSize;      /**< Size of the BMP file in bytes */
  unsigned short bfReserved1; /**< Reserved; must be zero */
  unsigned short bfReserved2; /**< Reserved; must be zero */
  unsigned int   bfOffBits;   /**< Offset to start of pixel data */
} BMPFileHeader;

typedef struct {
  unsigned int   biSize;          /**< Size of this header (40 bytes) */
  int            biWidth;         /**< Bitmap width in pixels */
  int            biHeight;        /**< Bitmap height in pixels */
  unsigned short biPlanes;        /**< Number of color planes (must be 1) */
  unsigned short biBitCount;      /**< Bits per pixel (e.g., 1, 4, 8, 16, 24, or 32) */
  unsigned int   biCompression;   /**< Compression method (0 for uncompressed, 1 for RLE8, 2 for RLE4) */
  unsigned int   biSizeImage;     /**< Image size (may be 0 for uncompressed images) */
  int            biXPelsPerMeter; /**< Horizontal resolution */
  int            biYPelsPerMeter; /**< Vertical resolution */
  unsigned int   biClrUsed;       /**< Number of colors in the palette (0 means default) */
  unsigned int   biClrImportant;  /**< Important colors (0 = all) */
} BMPInfoHeader;

typedef struct {
  unsigned char rgbBlue;
  unsigned char rgbGreen;
  unsigned char rgbRed;
  unsigned char rgbReserved;
} RGBQuad;
#pragma pack(pop)

// Helper: calculate the row size (in bytes) for a given width and bits-per-pixel.
static int calcRowSize(int width, int bitsPerPixel) {
  return (((width * bitsPerPixel) + 31) / 32) * 4;
}

// Helper: read a palette of numColors entries.
static RGBQuad* readPalette(FILE * fp, unsigned int numColors) {
  RGBQuad * palette = (RGBQuad *)malloc(numColors * sizeof(RGBQuad));
  if (!palette) {
    return NULL;
  }
  if (fread(palette, sizeof(RGBQuad), numColors, fp) != numColors) {
    free(palette);
    return NULL;
  }
  return palette;
}

// Helper: process uncompressed rows. This function seeks through each row,
// flips the image vertically, and calls a conversion callback for each row.
static CJellyFormatImageError processUncompressedRows(FILE * fp, int bitsPerPixel,
    int width, int height, unsigned char *dest, int destPixelSize, bool topDown,
    void (*convertRow)(const unsigned char *src, int width, unsigned char * dest)) {
  int rowSize = calcRowSize(width, bitsPerPixel);
  unsigned char *rowBuffer = (unsigned char *)malloc((size_t)rowSize);
  if (!rowBuffer) {
    return CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
  }
  for (int y = 0; y < height; ++y) {
    if (fread(rowBuffer, 1, (size_t)rowSize, fp) != (size_t)rowSize) {
      free(rowBuffer);
      return CJELLY_FORMAT_IMAGE_ERR_IO;
    }
    int destRow = topDown ? y : ((height - 1) - y);
    unsigned char * destRowPtr = dest + (destRow * width * destPixelSize);
    convertRow(rowBuffer, width, destRowPtr);
  }
  free(rowBuffer);
  return CJELLY_FORMAT_IMAGE_SUCCESS;
}

// Conversion callback for 24-bit rows: convert from BGR to RGB.
static void convert24BitRow(const unsigned char * src, int width, unsigned char *dest) {
  for (int x = 0; x < width; ++x) {
    int pixelIndex = x * 3;
    unsigned char blue  = src[pixelIndex + 0];
    unsigned char green = src[pixelIndex + 1];
    unsigned char red   = src[pixelIndex + 2];
    dest[(x * 3) + 0] = red;
    dest[(x * 3) + 1] = green;
    dest[(x * 3) + 2] = blue;
  }
}

// Conversion callback for 16-bit rows (assumed 5-6-5 format).
static void convert16BitRow(const unsigned char * src, int width, unsigned char * dest) {
  for (int x = 0; x < width; ++x) {
    int pixelIndex = x * 2;
    unsigned short pixel = (unsigned short)(src[pixelIndex] | (src[pixelIndex + 1] << 8));
    unsigned char r = (unsigned char)(((pixel >> 11) & 0x1F) * 255 / 31);
    unsigned char g = (unsigned char)(((pixel >> 5)  & 0x3F) * 255 / 63);
    unsigned char b = (unsigned char)((pixel & 0x1F) * 255 / 31);
    dest[(x * 3) + 0] = r;
    dest[(x * 3) + 1] = g;
    dest[(x * 3) + 2] = b;
  }
}

// Conversion callback for 32-bit rows (assumed BGRA to RGBA).
static void convert32BitRow(const unsigned char * src, int width, unsigned char * dest) {
  for (int x = 0; x < width; ++x) {
    int pixelIndex = x * 4;
    unsigned char blue  = src[pixelIndex + 0];
    unsigned char green = src[pixelIndex + 1];
    unsigned char red   = src[pixelIndex + 2];
    unsigned char alpha = src[pixelIndex + 3];
    dest[(x * 4) + 0] = red;
    dest[(x * 4) + 1] = green;
    dest[(x * 4) + 2] = blue;
    dest[(x * 4) + 3] = alpha;
  }
}

CJellyFormatImageError cjelly_format_image_bmp_load(const char * filename, CJellyFormatImage * * out_image) {
  CJellyFormatImageError err = CJELLY_FORMAT_IMAGE_SUCCESS;
  unsigned char * rowBuffer = NULL;
  RGBQuad * palette = NULL;
  *out_image = NULL;

  // Validate input parameters.
  if (!filename || !out_image) {
    return CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
  }

  // Open the BMP file for reading.
  FILE * fp = fopen(filename, "rb");
  if (!fp) {
    return CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND;
  }

  // Read the BMP file header.
  BMPFileHeader fileHeader;
  if (fread(&fileHeader, sizeof(BMPFileHeader), 1, fp) != 1) {
    err = CJELLY_FORMAT_IMAGE_ERR_IO;
    goto ERROR_FILE_CLOSE;
  }

  // Convert the BMP file header fields to host byte order.
  fileHeader.bfType    = GCJ_LE16_TO_HOST(fileHeader.bfType);
  fileHeader.bfSize    = GCJ_LE32_TO_HOST(fileHeader.bfSize);
  fileHeader.bfOffBits = GCJ_LE32_TO_HOST(fileHeader.bfOffBits);

  // Check the BMP file signature.
  if (fileHeader.bfType != 0x4D42) {
    err = CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
    goto ERROR_FILE_CLOSE;
  }

  // Read the BMP info header.
  BMPInfoHeader infoHeader;
  if (fread(&infoHeader, sizeof(BMPInfoHeader), 1, fp) != 1) {
    err = CJELLY_FORMAT_IMAGE_ERR_IO;
    goto ERROR_FILE_CLOSE;
  }

  // Convert the BMP info header fields to host byte order.
  infoHeader.biSize         = GCJ_LE32_TO_HOST(infoHeader.biSize);
  infoHeader.biWidth        = GCJ_LE32_TO_HOST(infoHeader.biWidth);
  infoHeader.biHeight       = GCJ_LE32_TO_HOST(infoHeader.biHeight);
  infoHeader.biPlanes       = GCJ_LE16_TO_HOST(infoHeader.biPlanes);
  infoHeader.biBitCount     = GCJ_LE16_TO_HOST(infoHeader.biBitCount);
  infoHeader.biCompression  = GCJ_LE32_TO_HOST(infoHeader.biCompression);
  infoHeader.biSizeImage    = GCJ_LE32_TO_HOST(infoHeader.biSizeImage);
  infoHeader.biClrUsed      = GCJ_LE32_TO_HOST(infoHeader.biClrUsed);
  infoHeader.biClrImportant = GCJ_LE32_TO_HOST(infoHeader.biClrImportant);

  // Determine if the bitmap is top-down.
  bool topDown = false;
  if (infoHeader.biHeight < 0) {
    topDown = true;
    infoHeader.biHeight = -infoHeader.biHeight;
  }

  // Allocate the BMP image structure.
  CJellyFormatImageBMP * bmpImage = (CJellyFormatImageBMP *)malloc(sizeof(CJellyFormatImageBMP));
  if (!bmpImage) {
    goto ERROR_FILE_CLOSE;
  }
  memset(bmpImage, 0, sizeof(CJellyFormatImageBMP));

  // Allocate the raw image data structure.
  bmpImage->base.raw = (CJellyFormatImageRaw *)malloc(sizeof(CJellyFormatImageRaw));
  if (!bmpImage->base.raw) {
    goto ERROR_FREE_BMP_IMAGE;
  }
  memset(bmpImage->base.raw, 0, sizeof(CJellyFormatImageRaw));

  // Populate the BMP image structure.
  bmpImage->base.type = CJELLY_FORMAT_IMAGE_BMP;
  unsigned int width = infoHeader.biWidth;
  unsigned int height = infoHeader.biHeight;
  bmpImage->base.raw->width  = width;
  bmpImage->base.raw->height = height;

  // All modes will produce either a 24-bit (RGB) or 32-bit (RGBA) image.
  bmpImage->base.raw->channels = infoHeader.biBitCount == 32 ? 4 : 3;
  bmpImage->base.raw->bitdepth = infoHeader.biBitCount == 32 ? 32 : 24;
  int rowSize = calcRowSize(width, infoHeader.biBitCount);
  size_t dataSize = width * height * bmpImage->base.raw->channels;

  // Allocate and zero out memory for the raw image data.
  bmpImage->base.raw->data = (unsigned char *)malloc(dataSize);
  if (!bmpImage->base.raw->data) {
    goto ERROR_FREE_BASE_RAW;
  }
  bmpImage->base.raw->data_size = dataSize;
  memset(bmpImage->base.raw->data, 0, dataSize);

   // Actually read the image data.
  if (infoHeader.biCompression == 0) {
    // Uncompressed BMPs.

    if ((infoHeader.biBitCount == 16 || infoHeader.biBitCount == 24 || infoHeader.biBitCount == 32)) {
      // These are true-color uncompressed BMPs (16-bit, 24-bit, or 32-bit).

      // Seek to the start of the pixel data.
      if (fseek(fp, fileHeader.bfOffBits, SEEK_SET) != 0) {
        err = CJELLY_FORMAT_IMAGE_ERR_IO;
        goto ERROR_FREE_BASE_RAW_DATA;
      }

      // Process the uncompressed rows.
      err = infoHeader.biBitCount == 24
        ? processUncompressedRows(fp, 24, width, height, bmpImage->base.raw->data, 3, topDown, convert24BitRow)
        : infoHeader.biBitCount == 16
          ? processUncompressedRows(fp, 16, width, height, bmpImage->base.raw->data, 3, topDown, convert16BitRow)
          : processUncompressedRows(fp, 32, width, height, bmpImage->base.raw->data, 4, topDown, convert32BitRow);
      if (err != CJELLY_FORMAT_IMAGE_SUCCESS) {
        goto ERROR_FREE_BASE_RAW_DATA;
      }
    }
    else if (infoHeader.biBitCount == 8 || infoHeader.biBitCount == 1 || infoHeader.biBitCount == 4) {
      // Palette-based uncompressed (8-bit or 1/4-bit).
      int bits = infoHeader.biBitCount;
      unsigned int expectedColors = (bits == 1 ? 2 : (bits == 4 ? 16 : 0));
      unsigned int num_colors = (infoHeader.biClrUsed != 0 ? infoHeader.biClrUsed : (bits == 8 ? 256 : expectedColors));

      // The palette is stored immediately after the info header.
      palette = readPalette(fp, num_colors);
      if (!palette) {
        err = CJELLY_FORMAT_IMAGE_ERR_IO;
        goto ERROR_FREE_BASE_RAW_DATA;
      }

      // Seek to the start of the pixel data.
      if (fseek(fp, fileHeader.bfOffBits, SEEK_SET) != 0) {
        err = CJELLY_FORMAT_IMAGE_ERR_IO;
        goto ERROR_FREE_PALETTE;
      }

      // Allocate a buffer for reading rows.
      rowBuffer = (unsigned char *)malloc((size_t)rowSize);
      if (!rowBuffer) {
        goto ERROR_FREE_PALETTE;
      }

      // Process the uncompressed, palette-based rows.
      for (unsigned int y = 0; y < height; ++y) {
        // Read the row.
        if (fread(rowBuffer, 1, (size_t)rowSize, fp) != (size_t)rowSize) {
          err = CJELLY_FORMAT_IMAGE_ERR_IO;
          goto ERROR_FREE_ROWBUFFER;
        }

        // Flip the image vertically (in the output).
        int destRow = topDown ? y : ((height - 1) - y);
        unsigned char * dest = bmpImage->base.raw->data + (destRow * width * 3);

        // Decode the row.
        for (unsigned int x = 0; x < width; ++x) {
          unsigned char index;
          if (bits == 8) {
            // 8-bit mode.
            index = rowBuffer[x];
          }
          else {
            // 1-bit and 4-bit modes.
            int bitIndex = x * bits;
            int byteIndex = bitIndex / 8;
            int shift = (8 - bits) - (bitIndex % 8);
            index = (rowBuffer[byteIndex] >> shift) & ((1 << bits) - 1);
          }
          if (index >= num_colors) {
            err = CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
            goto ERROR_FREE_ROWBUFFER;
          }
          dest[(x * 3) + 0] = palette[index].rgbRed;
          dest[(x * 3) + 1] = palette[index].rgbGreen;
          dest[(x * 3) + 2] = palette[index].rgbBlue;
        }
      }
      free(rowBuffer);
      free(palette);
    }
    else {
      err = CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
      goto ERROR_FREE_BASE_RAW_DATA;
    }
  }
  // --- RLE Compressed Modes ---
  else if ((infoHeader.biBitCount == 8 && infoHeader.biCompression == 1)
    || ((infoHeader.biBitCount == 1 || infoHeader.biBitCount == 4) && infoHeader.biCompression == 2)) {

    // RLE-compressed BMP (RLE8 for 8-bit, RLE4 for 1-/4-bit).
    int mode = infoHeader.biBitCount;

    // The palette is stored immediately after the info header.
    // Read the palette.
    unsigned int num_colors = infoHeader.biClrUsed
      ? infoHeader.biClrUsed
      : mode == 8
        ? 256
        : mode == 1
          ? 2
          : 16;
    palette = readPalette(fp, num_colors);
    if (!palette) {
      err = CJELLY_FORMAT_IMAGE_ERR_IO;
      goto ERROR_FREE_BASE_RAW_DATA;
    }

    unsigned int x = 0, y = 0;
    while (y < height) {
      // Process a row.

      // Read the RLE pair.
      int count = fgetc(fp);
      if (count == EOF) {
        err = CJELLY_FORMAT_IMAGE_ERR_IO;
        goto ERROR_FREE_PALETTE;
      }
      int value = fgetc(fp);
      if (value == EOF) {
        err = CJELLY_FORMAT_IMAGE_ERR_IO;
        goto ERROR_FREE_PALETTE;
      }

      // Rows are flipped vertically.
      int destRow = topDown ? y : ((height - 1) - y);
      int destOffset = destRow * width * 3;

      // Process the RLE pair.
      if (count) {
        // `count` is greater than zero, so this is Encoded mode.

        // Process the RLE pair.
        if (mode == 8) {
          // RLE8: output 'count' copies of the single color.
          for (int i = 0; i < count; ++i) {
            if (x < width) {
              unsigned char * dest = bmpImage->base.raw->data + destOffset + (x * 3);
              dest[0] = palette[(unsigned char)value].rgbRed;
              dest[1] = palette[(unsigned char)value].rgbGreen;
              dest[2] = palette[(unsigned char)value].rgbBlue;
            }
            x++;
          }
        }
        else {
          // RLE4: each encoded byte holds two nibbles.

          // `nibbles` will hold the two nibbles from the byte
          // in the order [high, low].  We can then use `i & 1`
          // to select the appropriate nibble.
          unsigned char nibbles[2] = { (unsigned char)((value >> 4) & 0x0F), (unsigned char)(value & 0x0F) };
          for (int i = 0; i < count; ++i) {
            unsigned char nibble = nibbles[i & 1];
            if (x < width)  {
              unsigned char * dest = bmpImage->base.raw->data + destOffset + (x * 3);
              dest[0] = palette[nibble].rgbRed;
              dest[1] = palette[nibble].rgbGreen;
              dest[2] = palette[nibble].rgbBlue;
            }
            x++;
          }
        }
      }
      else {
        // `count` is 0, so this is Escape mode.
        if (value == 0) {
          // End-of-line.
          x = 0;
          y++;
        }
        else if (value == 1) {
          // End-of-bitmap.
          break;
        }
        else if (value == 2) {
          // Delta.
          int dx = fgetc(fp);
          int dy = fgetc(fp);
          if (dx == EOF || dy == EOF) {
            err = CJELLY_FORMAT_IMAGE_ERR_IO;
            goto ERROR_FREE_PALETTE;
          }
          x += (unsigned char)dx;
          y += (unsigned char)dy;
        }
        else {
          // Absolute mode.
          int n = value;
          if (mode == 8) {
            // RLE8 absolute mode.
            // In RLE8 absolute mode, that many 8‑bit color indices are read
            // directly. If the count is odd, a padding byte is added.
            for (int i = 0; i < n; ++i) {
              int pixel = fgetc(fp);
              if (pixel == EOF) {
                err = CJELLY_FORMAT_IMAGE_ERR_IO;
                goto ERROR_FREE_PALETTE;
              }
              if (x < width) {
                unsigned char * dest = bmpImage->base.raw->data + destOffset + (x * 3);
                dest[0] = palette[(unsigned char)pixel].rgbRed;
                dest[1] = palette[(unsigned char)pixel].rgbGreen;
                dest[2] = palette[(unsigned char)pixel].rgbBlue;
              }
              x++;
            }
            if (n & 1) {
              // Padding byte.
              fgetc(fp);
            }
          }
          else {
            // RLE4 absolute mode.
            // In RLE4 absolute mode, the literal data is stored as packed
            // nibbles (two per byte); if the count is odd, an extra nibble
            // (or pad byte) is included to maintain word alignment.
            for (int i = 0; i < n; ++i) {
              if ((i & 1) == 0) {
                // Read a new byte.
                int byteVal = fgetc(fp);
                if (byteVal == EOF) {
                  err = CJELLY_FORMAT_IMAGE_ERR_IO;
                  goto ERROR_FREE_PALETTE;
                }

                // Process the high nibble.
                unsigned char nibble = (unsigned char)((byteVal >> 4) & 0x0F);
                if (x < width) {
                  unsigned char *dest = bmpImage->base.raw->data + destOffset + (x * 3);
                  dest[0] = palette[nibble].rgbRed;
                  dest[1] = palette[nibble].rgbGreen;
                  dest[2] = palette[nibble].rgbBlue;
                }
                x++;

                if (i + 1 < n) {
                  // There are more nibbles to process.
                  // Process the low nibble.
                  unsigned char nibble2 = (unsigned char)(byteVal & 0x0F);
                  if (x < width) {
                    unsigned char *dest = bmpImage->base.raw->data + destOffset + (x * 3);
                    dest[0] = palette[nibble2].rgbRed;
                    dest[1] = palette[nibble2].rgbGreen;
                    dest[2] = palette[nibble2].rgbBlue;
                  }
                  x++;
                }
              }
            }
            if (n & 1) {
              // Padding byte.
              fgetc(fp);
            }
          }
        }
      }
    }
    free(palette);
  }

  // If we're here, then we don't know how to interpret the BMP.
  else {
    err = CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT;
    goto ERROR_FREE_BASE_RAW;
  }

  fclose(fp);
  *out_image = (CJellyFormatImage *)bmpImage;
  return CJELLY_FORMAT_IMAGE_SUCCESS;

ERROR_FREE_ROWBUFFER:
  free(rowBuffer);
ERROR_FREE_PALETTE:
  if (palette) {
    free(palette);
  }
ERROR_FREE_BASE_RAW_DATA:
  free(bmpImage->base.raw->data);
ERROR_FREE_BASE_RAW:
  free(bmpImage->base.raw);
ERROR_FREE_BMP_IMAGE:
  free(bmpImage);
ERROR_FILE_CLOSE:
  fclose(fp);
  if (err == CJELLY_FORMAT_IMAGE_SUCCESS) {
    err = CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY;
  }
  return err;
}


void cjelly_format_image_bmp_dump(const CJellyFormatImageBMP * imageBmp) {
  const CJellyFormatImage * image = (const CJellyFormatImage *)imageBmp;
  if (!image || !image->raw || !image->raw->data) {
      fprintf(stderr, "Invalid image data.\n");
      return;
  }

  // Dump header values from the loaded image.
  printf("Image Type: BMP\n");
  printf("Width: %d\n", image->raw->width);
  printf("Height: %d\n", image->raw->height);
  printf("Bit Depth: %zu\n", image->raw->bitdepth);
  printf("Channels: %d\n", image->raw->channels);
  printf("Data Size: %zu bytes\n", image->raw->data_size);
  printf("\n");

  // int width = image->raw->width;
  // int height = image->raw->height;
  // int channels = image->raw->channels;

  // Dump pixel data.
  // for (int y = 0; y < height; ++y) {
  //   for (int x = 0; x < width; ++x) {
  //     int idx = (y * width + x) * channels;
  //     if (channels == 3) {
  //       // Print in RRGGBB format.
  //       printf("%02X%02X%02X ", image->raw->data[idx],
  //         image->raw->data[idx + 1],
  //         image->raw->data[idx + 2]);
  //   }
  //   else if (channels == 4) {
  //       // Print in RRGGBBAA format.
  //       printf("%02X%02X%02X%02X ", image->raw->data[idx],
  //         image->raw->data[idx + 1],
  //         image->raw->data[idx + 2],
  //         image->raw->data[idx + 3]);
  //     }
  //     else {
  //         printf("?? ");
  //     }
  //   }
  //   printf("\n");
  // }
}

void cjelly_format_image_bmp_free(CJellyFormatImageBMP * imageBmp) {
  // Everything that needs to be freed is handled in the
  // cjelly_format_image_free() function.
  (void)imageBmp;
  return;
}
