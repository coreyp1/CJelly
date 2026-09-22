/**
 * @file test_capture.cpp
 *
 * Unit tests for frame capture.
 *
 * Reading a frame out of a window needs a window, a device and a presented
 * frame, so that part is exercised by the demo's CJELLY_DEMO_CAPTURE path
 * rather than here. Everything either side of it - the pixel accessor and the
 * PNG writer - is ordinary code operating on a buffer, and is tested properly.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"
#include <ghoti.io/cjelly/cj_capture.h>
#include <gtest/gtest.h>

#include <ghoti.io/image/codec.h>
#include <ghoti.io/image/doc.h>
#include <ghoti.io/image/raster.h>
#include <ghoti.io/image/stream.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using cjtest::TempFile;

namespace {

/** A capture filled with a recognisable pattern. */
class SyntheticCapture {
public:
  SyntheticCapture(uint32_t width, uint32_t height)
      : storage_(static_cast<size_t>(width) * height * 4) {
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        uint8_t * p = &storage_[(static_cast<size_t>(y) * width + x) * 4];
        p[0] = static_cast<uint8_t>(x * 7 + 1);
        p[1] = static_cast<uint8_t>(y * 5 + 2);
        p[2] = static_cast<uint8_t>((x + y) * 3 + 3);
        p[3] = 255;
      }
    }
    capture_.pixels = storage_.data();
    capture_.width = width;
    capture_.height = height;
    capture_.stride = static_cast<size_t>(width) * 4;
  }

  const cj_capture_t * get() const { return &capture_; }

private:
  std::vector<uint8_t> storage_;
  cj_capture_t capture_{};
};

} // namespace

//
// Lifetime
//

TEST(Capture, FreeToleratesNullAndZeroed) {
  cj_capture_free(nullptr);
  cj_capture_t empty = {};
  cj_capture_free(&empty);
  cj_capture_free(&empty) /* twice is fine */;
  EXPECT_EQ(empty.pixels, nullptr);
  EXPECT_EQ(empty.width, 0u);
}

TEST(Capture, CaptureRejectsNullArguments) {
  cj_capture_t capture = {};
  EXPECT_EQ(cj_window_capture(nullptr, &capture), CJ_E_INVALID_ARGUMENT);
  EXPECT_EQ(cj_window_capture(nullptr, nullptr), CJ_E_INVALID_ARGUMENT);
  EXPECT_EQ(capture.pixels, nullptr) << "nothing handed back on failure";
}

//
// The pixel accessor
//

TEST(CapturePixel, ReadsWhatIsThere) {
  SyntheticCapture synthetic(8, 4);
  uint8_t rgba[4] = {};
  ASSERT_TRUE(cj_capture_pixel(synthetic.get(), 3, 2, rgba));
  EXPECT_EQ(rgba[0], 3 * 7 + 1);
  EXPECT_EQ(rgba[1], 2 * 5 + 2);
  EXPECT_EQ(rgba[2], (3 + 2) * 3 + 3);
  EXPECT_EQ(rgba[3], 255);
}

TEST(CapturePixel, RefusesCoordinatesOutsideTheImage) {
  SyntheticCapture synthetic(8, 4);
  uint8_t rgba[4] = {};
  EXPECT_FALSE(cj_capture_pixel(synthetic.get(), 8, 0, rgba));
  EXPECT_FALSE(cj_capture_pixel(synthetic.get(), 0, 4, rgba));
  EXPECT_TRUE(cj_capture_pixel(synthetic.get(), 7, 3, rgba))
      << "the last pixel is inside";
}

TEST(CapturePixel, RefusesNullArguments) {
  SyntheticCapture synthetic(4, 4);
  uint8_t rgba[4] = {};
  EXPECT_FALSE(cj_capture_pixel(nullptr, 0, 0, rgba));
  EXPECT_FALSE(cj_capture_pixel(synthetic.get(), 0, 0, nullptr));

  cj_capture_t empty = {};
  EXPECT_FALSE(cj_capture_pixel(&empty, 0, 0, rgba));
}

//
// The PNG writer
//

TEST(CaptureWritePng, RejectsNullArguments) {
  SyntheticCapture synthetic(4, 4);
  EXPECT_EQ(cj_capture_write_png(nullptr, "x.png"), CJ_E_INVALID_ARGUMENT);
  EXPECT_EQ(cj_capture_write_png(synthetic.get(), nullptr),
      CJ_E_INVALID_ARGUMENT);

  cj_capture_t empty = {};
  EXPECT_EQ(cj_capture_write_png(&empty, "x.png"), CJ_E_INVALID_ARGUMENT);
}

TEST(CaptureWritePng, ReportsAPathItCannotOpen) {
  SyntheticCapture synthetic(4, 4);
  EXPECT_NE(cj_capture_write_png(synthetic.get(),
                "/nonexistent/directory/output.png"),
      CJ_SUCCESS);
}

// The written file has to be a real PNG carrying the exact pixels, not merely
// a file that appeared. Decoding it back is the only way to know.
TEST(CaptureWritePng, RoundTripsThroughTheImageLibrary) {
  const uint32_t kWidth = 17; // Deliberately not a round number, to catch a
  const uint32_t kHeight = 9; // stride confusion between width and row bytes.
  SyntheticCapture synthetic(kWidth, kHeight);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  ASSERT_EQ(cj_capture_write_png(synthetic.get(), out.path()), CJ_SUCCESS);

  // Read the file back.
  FILE * f = fopen(out.path(), "rb");
  ASSERT_NE(f, nullptr);
  std::vector<uint8_t> encoded;
  uint8_t chunk[4096];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    encoded.insert(encoded.end(), chunk, chunk + got);
  }
  fclose(f);
  ASSERT_GT(encoded.size(), 8u);
  EXPECT_EQ(memcmp(encoded.data(), "\x89PNG\r\n\x1a\n", 8), 0)
      << "that is not a PNG";

  GIMG_Stream * stream = nullptr;
  ASSERT_EQ(gimg_stream_create_memory(encoded.data(), encoded.size(), &stream),
      GIMG_OK);
  GIMG_Doc * doc = nullptr;
  ASSERT_EQ(gimg_doc_load(stream, nullptr, nullptr, &doc), GIMG_OK);
  gimg_stream_destroy(stream);
  ASSERT_NE(doc, nullptr);
  ASSERT_GT(gimg_doc_item_count(doc), 0u);

  GIMG_Raster * raster = nullptr;
  ASSERT_EQ(gimg_item_decode(gimg_doc_item(doc, 0), nullptr, &raster), GIMG_OK);
  ASSERT_NE(raster, nullptr);
  EXPECT_EQ(gimg_raster_width(raster), kWidth);
  EXPECT_EQ(gimg_raster_height(raster), kHeight);

  // Every pixel, exactly: a PNG is lossless, so anything else is a bug in the
  // channel order or the stride.
  const uint8_t * decoded = (const uint8_t *)gimg_raster_pixels_const(raster);
  size_t decoded_stride = gimg_raster_stride_bytes(raster);
  ASSERT_NE(decoded, nullptr);
  for (uint32_t y = 0; y < kHeight; y++) {
    for (uint32_t x = 0; x < kWidth; x++) {
      uint8_t expected[4] = {};
      ASSERT_TRUE(cj_capture_pixel(synthetic.get(), x, y, expected));
      const uint8_t * actual = decoded + y * decoded_stride + x * 4;
      EXPECT_EQ(actual[0], expected[0]) << "red at " << x << "," << y;
      EXPECT_EQ(actual[1], expected[1]) << "green at " << x << "," << y;
      EXPECT_EQ(actual[2], expected[2]) << "blue at " << x << "," << y;
      EXPECT_EQ(actual[3], expected[3]) << "alpha at " << x << "," << y;
    }
  }

  gimg_raster_destroy(raster);
  gimg_doc_destroy(doc);
}

#ifndef _WIN32

namespace {

/** A directory in the system temporary area, removed with its contents. */
class TempDir {
public:
  TempDir() {
    char * tmp = nullptr;
    if (gcu_path_temp_dir(nullptr, &tmp) != GCU_PATH_OK) {
      return;
    }
    char joined[1024];
    std::string name = "cjelly_capture_" + std::to_string((long)getpid());
    GCU_Path_Result r = gcu_path_join(
        GCU_PATH_NATIVE, tmp, name.c_str(), joined, sizeof(joined), nullptr);
    gcu_path_free(nullptr, tmp);
    if (r != GCU_PATH_OK || mkdir(joined, 0700) != 0) {
      return;
    }
    path_ = joined;
    made_ = true;
  }

  TempDir(const TempDir &) = delete;
  TempDir & operator=(const TempDir &) = delete;

  ~TempDir() {
    if (!made_) {
      return;
    }
    chmod(path_.c_str(), 0700);
    ::remove(file("out.png").c_str());
    rmdir(path_.c_str());
  }

  std::string file(const char * name) const {
    char joined[1024];
    if (gcu_path_join(GCU_PATH_NATIVE, path_.c_str(), name, joined,
            sizeof(joined), nullptr)
        != GCU_PATH_OK) {
      return std::string();
    }
    return std::string(joined);
  }

  const char * path() const { return path_.c_str(); }
  bool valid() const { return made_; }

private:
  std::string path_;
  bool made_ = false;
};

} // namespace

// The writer opened the destination directly, so it truncated whatever was
// there before it knew whether the new image could be written at all - and
// it never checked the close, which is the call that reports a full disk for
// everything stdio still held.  Writing through a temporary and renaming
// means the destination is either the old file or the whole new one.
//
// A directory the process may not write to is the deterministic way to make
// the write fail: creating the temporary in it is refused, while opening an
// existing writable file inside it is not, which is exactly the gap the old
// code fell through.
TEST(CaptureWritePng, LeavesTheExistingFileAloneWhenTheWriteFails) {
  TempDir dir;
  ASSERT_TRUE(dir.valid());
  const std::string dest = dir.file("out.png");

  const std::string sentinel = "this is not a png, and must survive";
  FILE * f = fopen(dest.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(fwrite(sentinel.data(), 1, sentinel.size(), f), sentinel.size());
  ASSERT_EQ(fclose(f), 0);

  // The file stays writable; only the directory is closed off.
  ASSERT_EQ(chmod(dir.path(), 0500), 0);
  SyntheticCapture synthetic(4, 4);
  const cj_result_t result = cj_capture_write_png(synthetic.get(), dest.c_str());
  ASSERT_EQ(chmod(dir.path(), 0700), 0);

  EXPECT_NE(result, CJ_SUCCESS) << "the write could not have succeeded";

  std::string after;
  f = fopen(dest.c_str(), "rb");
  ASSERT_NE(f, nullptr);
  char chunk[256];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    after.append(chunk, got);
  }
  fclose(f);
  EXPECT_EQ(after, sentinel)
      << "a failed write must not destroy what was already there";
}

#endif // _WIN32

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
