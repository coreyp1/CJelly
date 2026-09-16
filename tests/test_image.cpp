/**
 * @file test_image.cpp
 *
 * Unit tests for the image adapter.
 *
 * Decoding itself belongs to the Ghoti.io Image library and is covered by
 * that library's own suite, which goes far deeper into each format than
 * would be useful to repeat here.  What these tests pin down is the contract
 * CJelly depends on: which formats are accepted, that the pixels arrive
 * tightly packed as RGBA8 ready for Vulkan, that the reported type and name
 * are right, and that bad input is refused rather than half-loaded.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"
#include <ghoti.io/cjelly/format/image.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using cjtest::asset;
using cjtest::TempFile;

namespace {

/** A minimal, valid 24-bit BMP of the given size, every pixel one colour. */
std::string make_bmp24(int width, int height, unsigned char b, unsigned char g,
    unsigned char r) {
  const int row_raw = width * 3;
  const int row_padded = (row_raw + 3) & ~3;
  const int pixel_bytes = row_padded * height;
  const int offset = 14 + 40;
  const int file_size = offset + pixel_bytes;

  std::string out;
  auto u16 = [&out](unsigned v) {
    out.push_back((char)(v & 0xFF));
    out.push_back((char)((v >> 8) & 0xFF));
  };
  auto u32 = [&out](unsigned v) {
    for (int i = 0; i < 4; i++) {
      out.push_back((char)((v >> (8 * i)) & 0xFF));
    }
  };

  out += "BM";            // signature
  u32((unsigned)file_size);
  u16(0);
  u16(0);
  u32((unsigned)offset);  // pixel data offset

  u32(40);                // DIB header size (BITMAPINFOHEADER)
  u32((unsigned)width);
  u32((unsigned)height);
  u16(1);                 // planes
  u16(24);                // bits per pixel
  u32(0);                 // compression: BI_RGB
  u32((unsigned)pixel_bytes);
  u32(2835);              // x pixels per metre
  u32(2835);              // y pixels per metre
  u32(0);                 // palette colours used
  u32(0);                 // important colours

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      out.push_back((char)b);
      out.push_back((char)g);
      out.push_back((char)r);
    }
    for (int pad = row_raw; pad < row_padded; pad++) {
      out.push_back('\0');
    }
  }
  return out;
}

} // namespace

//
// Format detection
//

TEST(ImageDetect, RecognisesBmp) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  ASSERT_EQ(cjelly_format_image_detect_type(
                asset("images/bmp/tang.bmp").c_str(), &type),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_BMP);
}

TEST(ImageDetect, RecognisesPng) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  ASSERT_EQ(cjelly_format_image_detect_type(
                asset("images/png/pattern.png").c_str(), &type),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_PNG);
}

TEST(ImageDetect, RecognisesJpeg) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  ASSERT_EQ(cjelly_format_image_detect_type(
                asset("images/jpeg/flat.jpg").c_str(), &type),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_JPEG);
}

TEST(ImageDetect, MissingFileReportsNotFound) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_BMP;
  EXPECT_EQ(cjelly_format_image_detect_type(cjtest::missing_path(), &type),
      CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND);
}

TEST(ImageDetect, UnknownSignatureIsUnknown) {
  TempFile f("not an image at all, just text", ".dat");
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_BMP;
  cjelly_format_image_detect_type(f.path(), &type);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_UNKNOWN);
}

// A file shorter than the signature must not read past what it holds.
TEST(ImageDetect, TruncatedFileIsUnknown) {
  TempFile f("B", ".bmp");
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_BMP;
  cjelly_format_image_detect_type(f.path(), &type);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_UNKNOWN);
}

TEST(ImageDetect, NullArgumentsRejected) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  EXPECT_EQ(cjelly_format_image_detect_type(nullptr, &type),
      CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT);
  // Must report the error rather than writing through the null pointer.
  EXPECT_EQ(cjelly_format_image_detect_type("whatever.bmp", nullptr),
      CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT);
}

TEST(ImageStrerror, CoversEveryCode) {
  const CJellyFormatImageError codes[] = {CJELLY_FORMAT_IMAGE_SUCCESS,
      CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND,
      CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY,
      CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT, CJELLY_FORMAT_IMAGE_ERR_IO};
  for (CJellyFormatImageError c : codes) {
    const char * msg = cjelly_format_image_strerror(c);
    ASSERT_NE(msg, nullptr);
    EXPECT_GT(strlen(msg), 0u);
  }
}

TEST(ImageFree, NullIsSafe) {
  cjelly_format_image_free(nullptr);
}

//
// Generic load
//

TEST(ImageLoad, LoadsBmpAndRecordsName) {
  CJellyFormatImage * image = nullptr;
  std::string path = asset("images/bmp/tang.bmp");
  ASSERT_EQ(cjelly_format_image_load(path.c_str(), &image),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->type, CJELLY_FORMAT_IMAGE_BMP);
  ASSERT_NE(image->name, nullptr);
  EXPECT_STREQ((const char *)image->name, path.c_str());
  ASSERT_NE(image->raw, nullptr);
  EXPECT_EQ(image->raw->width, 1024);
  EXPECT_EQ(image->raw->height, 1024);
  cjelly_format_image_free(image);
}

TEST(ImageLoad, UnknownFormatRejected) {
  TempFile f("definitely not an image", ".dat");
  CJellyFormatImage * image = nullptr;
  EXPECT_EQ(cjelly_format_image_load(f.path(), &image),
      CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT);
}

TEST(ImageLoad, MissingFileReportsNotFound) {
  CJellyFormatImage * image = nullptr;
  EXPECT_EQ(cjelly_format_image_load(cjtest::missing_path(), &image),
      CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND);
}

//
// Formats
//

TEST(ImageLoad, NotABitmapRejected) {
  TempFile f("XX not a bitmap header", ".bmp");
  CJellyFormatImage * image = nullptr;
  EXPECT_NE(cjelly_format_image_load(f.path(), &image),
      CJELLY_FORMAT_IMAGE_SUCCESS);
}

// A header claiming more pixel data than the file holds must be rejected,
// not trusted into an over-long read.
TEST(ImageLoad, TruncatedPixelDataRejected) {
  std::string bmp = make_bmp24(8, 8, 1, 2, 3);
  bmp.resize(bmp.size() / 2);
  TempFile f(bmp, ".bmp");
  CJellyFormatImage * image = nullptr;
  CJellyFormatImageError err = cjelly_format_image_load(f.path(), &image);
  EXPECT_NE(err, CJELLY_FORMAT_IMAGE_SUCCESS)
      << "header promised more pixel data than the file contains";
  if (err == CJELLY_FORMAT_IMAGE_SUCCESS) {
    cjelly_format_image_free(image);
  }
}

TEST(ImageLoad, HeaderOnlyRejected) {
  std::string bmp = make_bmp24(4, 4, 0, 0, 0);
  bmp.resize(14 + 40);
  TempFile f(bmp, ".bmp");
  CJellyFormatImage * image = nullptr;
  CJellyFormatImageError err = cjelly_format_image_load(f.path(), &image);
  EXPECT_NE(err, CJELLY_FORMAT_IMAGE_SUCCESS);
  if (err == CJELLY_FORMAT_IMAGE_SUCCESS) {
    cjelly_format_image_free(image);
  }
}

TEST(ImageLoad, Synthetic24BitBmp) {
  // The source is 24-bit BGR; what comes back must be RGBA in channel order,
  // with the blue and red the file stored swapped back into place.
  std::string bmp = make_bmp24(3, 2, 0x10, 0x20, 0x30);
  TempFile f(bmp, ".bmp");
  CJellyFormatImage * image = nullptr;
  ASSERT_EQ(cjelly_format_image_load(f.path(), &image),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  ASSERT_NE(image, nullptr);
  ASSERT_NE(image->raw, nullptr);
  EXPECT_EQ(image->raw->width, 3);
  EXPECT_EQ(image->raw->height, 2);
  ASSERT_NE(image->raw->data, nullptr);

  EXPECT_EQ(image->raw->data[0], 0x30) << "red";
  EXPECT_EQ(image->raw->data[1], 0x20) << "green";
  EXPECT_EQ(image->raw->data[2], 0x10) << "blue";
  EXPECT_EQ(image->raw->data[3], 0xFF) << "opaque alpha";
  cjelly_format_image_free(image);
}

TEST(ImageLoad, Loads4BitPalettedBmp) {
  CJellyFormatImage * image = nullptr;
  CJellyFormatImageError err = cjelly_format_image_load(
      asset("images/bmp/16Color.bmp").c_str(), &image);
  ASSERT_EQ(err, CJELLY_FORMAT_IMAGE_SUCCESS)
      << cjelly_format_image_strerror(err);
  ASSERT_NE(image, nullptr);
  ASSERT_NE(image->raw, nullptr);
  EXPECT_EQ(image->raw->width, 2);
  EXPECT_EQ(image->raw->height, 2);
  cjelly_format_image_free(image);
}

// PNG and JPEG came with the move to the shared image library; before it,
// only BMP loaded at all.
TEST(ImageLoad, LoadsPng) {
  CJellyFormatImage * image = nullptr;
  std::string path = asset("images/png/pattern.png");
  CJellyFormatImageError err = cjelly_format_image_load(path.c_str(), &image);
  ASSERT_EQ(err, CJELLY_FORMAT_IMAGE_SUCCESS)
      << cjelly_format_image_strerror(err);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->type, CJELLY_FORMAT_IMAGE_PNG);
  ASSERT_NE(image->raw, nullptr);
  EXPECT_EQ(image->raw->width, 8);
  EXPECT_EQ(image->raw->height, 4);

  // The asset encodes (x*30, y*60, (x+y)*20).
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 8; x++) {
      const unsigned char * px =
          image->raw->data + (((size_t)y * 8u + (size_t)x) * 4u);
      EXPECT_EQ(px[0], (unsigned char)(x * 30)) << "red at " << x << "," << y;
      EXPECT_EQ(px[1], (unsigned char)(y * 60)) << "green at " << x << "," << y;
      EXPECT_EQ(px[2], (unsigned char)((x + y) * 20))
          << "blue at " << x << "," << y;
      EXPECT_EQ(px[3], 0xFF) << "alpha at " << x << "," << y;
    }
  }
  cjelly_format_image_free(image);
}

TEST(ImageLoad, LoadsJpeg) {
  CJellyFormatImage * image = nullptr;
  std::string path = asset("images/jpeg/flat.jpg");
  CJellyFormatImageError err = cjelly_format_image_load(path.c_str(), &image);
  ASSERT_EQ(err, CJELLY_FORMAT_IMAGE_SUCCESS)
      << cjelly_format_image_strerror(err);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->type, CJELLY_FORMAT_IMAGE_JPEG);
  ASSERT_NE(image->raw, nullptr);
  EXPECT_EQ(image->raw->width, 8);
  EXPECT_EQ(image->raw->height, 4);

  // A flat colour, so lossy compression cannot move it far.
  const unsigned char * px = image->raw->data;
  EXPECT_NEAR(px[0], 128, 4);
  EXPECT_NEAR(px[1], 64, 4);
  EXPECT_NEAR(px[2], 32, 4);
  EXPECT_EQ(px[3], 0xFF);
  cjelly_format_image_free(image);
}

//
// Pixel layout contract
//

TEST(ImageLoad, PixelsAreTightlyPackedRgba) {
  // Widths that are not a multiple of four are where a decoder's row padding
  // would leak through; the adapter has to strip it.
  for (int width = 1; width <= 5; width++) {
    std::string bmp = make_bmp24(width, 3, 0x11, 0x22, 0x33);
    TempFile f(bmp, ".bmp");
    CJellyFormatImage * image = nullptr;
    ASSERT_EQ(cjelly_format_image_load(f.path(), &image),
        CJELLY_FORMAT_IMAGE_SUCCESS)
        << "width " << width;
    ASSERT_NE(image->raw, nullptr);
    EXPECT_EQ(image->raw->channels, 4) << "width " << width;
    EXPECT_EQ(image->raw->bitdepth, 32u) << "width " << width;
    EXPECT_EQ(image->raw->data_size, (size_t)width * 3u * 4u)
        << "no row padding, width " << width;

    // Every pixel is the same colour, so any stride slip shows immediately.
    for (size_t i = 0; i < image->raw->data_size; i += 4) {
      ASSERT_EQ(image->raw->data[i + 0], 0x33) << "width " << width << " at " << i;
      ASSERT_EQ(image->raw->data[i + 1], 0x22) << "width " << width << " at " << i;
      ASSERT_EQ(image->raw->data[i + 2], 0x11) << "width " << width << " at " << i;
      ASSERT_EQ(image->raw->data[i + 3], 0xFF) << "width " << width << " at " << i;
    }
    cjelly_format_image_free(image);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
