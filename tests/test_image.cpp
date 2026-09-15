/**
 * @file test_image.cpp
 *
 * Unit tests for the image dispatch layer and the BMP loader.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"
#include <cjelly/format/image.h>
#include <cjelly/format/image/bmp.h>
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
// BMP loader
//

TEST(BmpLoad, MissingFileReportsNotFound) {
  CJellyFormatImage * image = nullptr;
  EXPECT_EQ(cjelly_format_image_bmp_load(cjtest::missing_path(), &image),
      CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND);
}

TEST(BmpLoad, NotABmpRejected) {
  TempFile f("XX not a bitmap header", ".bmp");
  CJellyFormatImage * image = nullptr;
  EXPECT_NE(cjelly_format_image_bmp_load(f.path(), &image),
      CJELLY_FORMAT_IMAGE_SUCCESS);
}

// A header claiming more pixel data than the file holds must be rejected,
// not trusted into an over-long read.
TEST(BmpLoad, TruncatedPixelDataRejected) {
  std::string bmp = make_bmp24(8, 8, 1, 2, 3);
  bmp.resize(bmp.size() / 2);
  TempFile f(bmp, ".bmp");
  CJellyFormatImage * image = nullptr;
  CJellyFormatImageError err = cjelly_format_image_bmp_load(f.path(), &image);
  EXPECT_NE(err, CJELLY_FORMAT_IMAGE_SUCCESS)
      << "header promised more pixel data than the file contains";
  if (err == CJELLY_FORMAT_IMAGE_SUCCESS) {
    cjelly_format_image_free(image);
  }
}

TEST(BmpLoad, HeaderOnlyRejected) {
  std::string bmp = make_bmp24(4, 4, 0, 0, 0);
  bmp.resize(14 + 40);
  TempFile f(bmp, ".bmp");
  CJellyFormatImage * image = nullptr;
  CJellyFormatImageError err = cjelly_format_image_bmp_load(f.path(), &image);
  EXPECT_NE(err, CJELLY_FORMAT_IMAGE_SUCCESS);
  if (err == CJELLY_FORMAT_IMAGE_SUCCESS) {
    cjelly_format_image_free(image);
  }
}

TEST(BmpLoad, Synthetic24BitImage) {
  std::string bmp = make_bmp24(3, 2, 0x10, 0x20, 0x30);
  TempFile f(bmp, ".bmp");
  CJellyFormatImage * image = nullptr;
  ASSERT_EQ(cjelly_format_image_bmp_load(f.path(), &image),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  ASSERT_NE(image, nullptr);
  ASSERT_NE(image->raw, nullptr);
  EXPECT_EQ(image->raw->width, 3);
  EXPECT_EQ(image->raw->height, 2);
  EXPECT_GT(image->raw->channels, 0);
  EXPECT_GT(image->raw->data_size, 0u);
  ASSERT_NE(image->raw->data, nullptr);
  // The decoded buffer must be large enough for the dimensions it reports.
  EXPECT_GE(image->raw->data_size,
      (size_t)(image->raw->width * image->raw->height * image->raw->channels));
  cjelly_format_image_free(image);
}

TEST(BmpFixture, Loads24BitImage) {
  CJellyFormatImage * image = nullptr;
  ASSERT_EQ(cjelly_format_image_bmp_load(
                asset("images/bmp/tang.bmp").c_str(), &image),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  ASSERT_NE(image, nullptr);
  ASSERT_NE(image->raw, nullptr);
  EXPECT_EQ(image->raw->width, 1024);
  EXPECT_EQ(image->raw->height, 1024);
  EXPECT_GE(image->raw->data_size,
      (size_t)(image->raw->width * image->raw->height * image->raw->channels));
  cjelly_format_image_free(image);
}

TEST(BmpFixture, Loads4BitPalettedImage) {
  CJellyFormatImage * image = nullptr;
  CJellyFormatImageError err = cjelly_format_image_bmp_load(
      asset("images/bmp/16Color.bmp").c_str(), &image);
  ASSERT_EQ(err, CJELLY_FORMAT_IMAGE_SUCCESS)
      << cjelly_format_image_strerror(err);
  ASSERT_NE(image, nullptr);
  ASSERT_NE(image->raw, nullptr);
  EXPECT_EQ(image->raw->width, 2);
  EXPECT_EQ(image->raw->height, 2);
  EXPECT_GE(image->raw->data_size,
      (size_t)(image->raw->width * image->raw->height * image->raw->channels));
  cjelly_format_image_free(image);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
