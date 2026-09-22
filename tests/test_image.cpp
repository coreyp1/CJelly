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

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using cjtest::asset;
using cjtest::CountingAllocator;
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
  ASSERT_EQ(cjelly_format_image_detect_type(asset("images/bmp/tang.bmp").c_str(), nullptr, &type),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_BMP);
}

TEST(ImageDetect, RecognisesPng) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  ASSERT_EQ(cjelly_format_image_detect_type(asset("images/png/pattern.png").c_str(), nullptr, &type),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_PNG);
}

TEST(ImageDetect, RecognisesJpeg) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  ASSERT_EQ(cjelly_format_image_detect_type(asset("images/jpeg/flat.jpg").c_str(), nullptr, &type),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_JPEG);
}

TEST(ImageDetect, MissingFileReportsNotFound) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_BMP;
  EXPECT_EQ(cjelly_format_image_detect_type(cjtest::missing_path(), nullptr, &type),
      CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND);
}

TEST(ImageDetect, UnknownSignatureIsUnknown) {
  TempFile f("not an image at all, just text");
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_BMP;
  cjelly_format_image_detect_type(f.path(), nullptr, &type);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_UNKNOWN);
}

// A file shorter than the signature must not read past what it holds.
TEST(ImageDetect, TruncatedFileIsUnknown) {
  TempFile f("B");
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_BMP;
  cjelly_format_image_detect_type(f.path(), nullptr, &type);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_UNKNOWN);
}

TEST(ImageDetect, NullArgumentsRejected) {
  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  EXPECT_EQ(cjelly_format_image_detect_type(nullptr, nullptr, &type),
      CJELLY_FORMAT_IMAGE_ERR_INVALID_ARGUMENT);
  // Must report the error rather than writing through the null pointer.
  EXPECT_EQ(cjelly_format_image_detect_type("whatever.bmp", nullptr, nullptr),
      CJELLY_FORMAT_IMAGE_ERR_INVALID_ARGUMENT);
}

TEST(ImageStrerror, CoversEveryCode) {
  const CJellyFormatImageError codes[] = {CJELLY_FORMAT_IMAGE_SUCCESS,
      CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND,
      CJELLY_FORMAT_IMAGE_ERR_OUT_OF_MEMORY,
      CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT, CJELLY_FORMAT_IMAGE_ERR_IO,
      CJELLY_FORMAT_IMAGE_ERR_LIMIT,
      CJELLY_FORMAT_IMAGE_ERR_INVALID_ARGUMENT};
  for (CJellyFormatImageError c : codes) {
    const char * msg = cjelly_format_image_strerror(c);
    ASSERT_NE(msg, nullptr);
    EXPECT_GT(strlen(msg), 0u);
    // The default arm answers "Unknown error", so a code with no case of its
    // own passes a non-empty check while saying nothing.
    EXPECT_STRNE(msg, "Unknown error") << "code " << (int)c;
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
  ASSERT_EQ(cjelly_format_image_load(path.c_str(), nullptr, &image),
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
  TempFile f("definitely not an image");
  CJellyFormatImage * image = nullptr;
  EXPECT_EQ(cjelly_format_image_load(f.path(), nullptr, &image),
      CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT);
}

TEST(ImageLoad, MissingFileReportsNotFound) {
  CJellyFormatImage * image = nullptr;
  EXPECT_EQ(cjelly_format_image_load(cjtest::missing_path(), nullptr, &image),
      CJELLY_FORMAT_IMAGE_ERR_FILE_NOT_FOUND);
}

//
// Formats
//

TEST(ImageLoad, NotABitmapRejected) {
  TempFile f("XX not a bitmap header");
  CJellyFormatImage * image = nullptr;
  EXPECT_NE(cjelly_format_image_load(f.path(), nullptr, &image),
      CJELLY_FORMAT_IMAGE_SUCCESS);
}

// A header claiming more pixel data than the file holds must be rejected,
// not trusted into an over-long read.
TEST(ImageLoad, TruncatedPixelDataRejected) {
  std::string bmp = make_bmp24(8, 8, 1, 2, 3);
  bmp.resize(bmp.size() / 2);
  TempFile f(bmp);
  CJellyFormatImage * image = nullptr;
  CJellyFormatImageError err = cjelly_format_image_load(f.path(), nullptr, &image);
  EXPECT_NE(err, CJELLY_FORMAT_IMAGE_SUCCESS)
      << "header promised more pixel data than the file contains";
  if (err == CJELLY_FORMAT_IMAGE_SUCCESS) {
    cjelly_format_image_free(image);
  }
}

TEST(ImageLoad, HeaderOnlyRejected) {
  std::string bmp = make_bmp24(4, 4, 0, 0, 0);
  bmp.resize(14 + 40);
  TempFile f(bmp);
  CJellyFormatImage * image = nullptr;
  CJellyFormatImageError err = cjelly_format_image_load(f.path(), nullptr, &image);
  EXPECT_NE(err, CJELLY_FORMAT_IMAGE_SUCCESS);
  if (err == CJELLY_FORMAT_IMAGE_SUCCESS) {
    cjelly_format_image_free(image);
  }
}

TEST(ImageLoad, Synthetic24BitBmp) {
  // The source is 24-bit BGR; what comes back must be RGBA in channel order,
  // with the blue and red the file stored swapped back into place.
  std::string bmp = make_bmp24(3, 2, 0x10, 0x20, 0x30);
  TempFile f(bmp);
  CJellyFormatImage * image = nullptr;
  ASSERT_EQ(cjelly_format_image_load(f.path(), nullptr, &image),
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
  CJellyFormatImageError err = cjelly_format_image_load(asset("images/bmp/16Color.bmp").c_str(), nullptr, &image);
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
  CJellyFormatImageError err = cjelly_format_image_load(path.c_str(), nullptr, &image);
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
  CJellyFormatImageError err = cjelly_format_image_load(path.c_str(), nullptr, &image);
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
    TempFile f(bmp);
    CJellyFormatImage * image = nullptr;
    ASSERT_EQ(cjelly_format_image_load(f.path(), nullptr, &image),
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

//
// Reading the file
//
// These cover the boundary onto cutil's whole-file reader rather than any
// codec.  The reader here used to size the file with fseek/ftell and then
// read that many bytes, which has two consequences the tests below name.
//

#ifndef _WIN32

namespace {

/**
 * A named pipe in the system temporary directory, fed by a forked writer.
 *
 * A FIFO is the cheapest thing that reports no size and cannot be seeked,
 * which is the shape the old reader could not handle.  The writer is a
 * separate process so that the reader in this one can block on it.
 */
class Fifo {
public:
  explicit Fifo(std::string payload) : payload_(std::move(payload)) {
    char * tmp = nullptr;
    if (gcu_path_temp_dir(nullptr, &tmp) != GCU_PATH_OK) {
      return;
    }
    char joined[1024];
    std::string name = "cjelly_fifo_" + std::to_string((long)getpid());
    GCU_Path_Result r = gcu_path_join(
        GCU_PATH_NATIVE, tmp, name.c_str(), joined, sizeof(joined), nullptr);
    gcu_path_free(nullptr, tmp);
    if (r != GCU_PATH_OK || mkfifo(joined, 0600) != 0) {
      return;
    }
    path_ = joined;
    made_ = true;
  }

  /** Fork a writer.  Call immediately before the read that drains it. */
  bool start() {
    if (!made_) {
      return false;
    }
    child_ = fork();
    if (child_ < 0) {
      return false;
    }
    if (child_ == 0) {
      // Child: EPIPE is expected once the reader has had enough, and it must
      // not raise a signal that the test runner would report.
      signal(SIGPIPE, SIG_IGN);
      int fd = open(path_.c_str(), O_WRONLY);
      if (fd >= 0) {
        size_t sent = 0;
        while (sent < payload_.size()) {
          ssize_t n = write(fd, payload_.data() + sent, payload_.size() - sent);
          if (n <= 0) {
            break;
          }
          sent += (size_t)n;
        }
        close(fd);
      }
      _exit(0);
    }
    return true;
  }

  Fifo(const Fifo &) = delete;
  Fifo & operator=(const Fifo &) = delete;

  ~Fifo() {
    if (child_ > 0) {
      int status = 0;
      waitpid(child_, &status, 0);
    }
    if (made_) {
      ::remove(path_.c_str());
    }
  }

  const char * path() const { return path_.c_str(); }
  bool valid() const { return made_; }

private:
  std::string payload_;
  std::string path_;
  bool made_ = false;
  pid_t child_ = -1;
};

} // namespace

// The reader sized the file before reading it, so anything whose size the
// kernel does not know in advance - a pipe, a FIFO, /dev/stdin, anything
// under /proc - was refused outright.  cutil reads in chunks instead.
TEST(ImageRead, LoadsFromAnInputThatReportsNoSize) {
  std::string bmp = make_bmp24(4, 4, 0x10, 0x20, 0x30);
  Fifo fifo(bmp);
  ASSERT_TRUE(fifo.valid());
  ASSERT_TRUE(fifo.start());

  CJellyFormatImage * image = nullptr;
  ASSERT_EQ(cjelly_format_image_load(fifo.path(), nullptr, &image),
      CJELLY_FORMAT_IMAGE_SUCCESS)
      << "a FIFO reports no size; the reader must not ask for one";
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->type, CJELLY_FORMAT_IMAGE_BMP);
  EXPECT_EQ(image->raw->width, 4);
  EXPECT_EQ(image->raw->height, 4);
  cjelly_format_image_free(image);
}

// The cap is a promise, so it is worth checking against the bound rather
// than against a file that is merely enormous.  A FIFO carries the bytes
// without any of them reaching a disk.
TEST(ImageRead, RefusesAnInputLargerThanTheLimit) {
  // One byte past the cap is the byte that proves the input is too big.
  std::string payload(CJELLY_FORMAT_IMAGE_MAX_FILE_BYTES + 1, '\0');
  Fifo fifo(std::move(payload));
  ASSERT_TRUE(fifo.valid());
  ASSERT_TRUE(fifo.start());

  CJellyFormatImage * image = nullptr;
  EXPECT_EQ(cjelly_format_image_load(fifo.path(), nullptr, &image),
      CJELLY_FORMAT_IMAGE_ERR_LIMIT);
  EXPECT_EQ(image, nullptr) << "nothing is handed back to free";
}

#endif // _WIN32

// cutil reports one error for "could not open" and "could not read", but
// this library has always told a caller the two apart.  A directory exists
// and is not readable as a file, so it is the case that separates them
// without depending on the test not running as root.
TEST(ImageRead, ReportsIoRatherThanNotFoundForSomethingThatExists) {
  CJellyFormatImage * image = nullptr;
  EXPECT_EQ(cjelly_format_image_load(cjtest::asset_dir().c_str(), nullptr, &image),
      CJELLY_FORMAT_IMAGE_ERR_IO);
  EXPECT_EQ(image, nullptr);

  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  EXPECT_EQ(
      cjelly_format_image_detect_type(cjtest::asset_dir().c_str(), nullptr, &type),
      CJELLY_FORMAT_IMAGE_ERR_IO);
}

//
// The allocator
//

// The engine descriptor carried an unread allocator field for a long time, so
// "the call succeeded" is not evidence the allocator was used.  These assert
// the counters moved, not merely that they balanced.
TEST(ImageAllocator, LoadAndFreeGoThroughTheCallersAllocator) {
  std::string bmp = make_bmp24(8, 8, 0x11, 0x22, 0x33);
  TempFile f(bmp);
  CountingAllocator alloc;

  CJellyFormatImage * image = nullptr;
  ASSERT_EQ(cjelly_format_image_load(f.path(), alloc.get(), &image),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  ASSERT_NE(image, nullptr);

  EXPECT_GT(alloc.allocations(), 0u)
      << "the image was built without asking the allocator for anything";
  EXPECT_GT(alloc.live(), 0)
      << "nothing is outstanding, so the image cannot be holding its memory";
  EXPECT_EQ(image->allocator, alloc.get())
      << "the image must record what it was built with, for free() to use";

  const size_t after_load = alloc.allocations();
  cjelly_format_image_free(image);

  EXPECT_EQ(alloc.live(), 0) << "every block must come back";
  EXPECT_EQ(alloc.allocations(), after_load) << "free must not allocate";
  EXPECT_EQ(alloc.frees(), after_load);
}

// The transient read buffer is the caller's business too, even though nothing
// is handed back.
TEST(ImageAllocator, DetectTypeBorrowsAndReturnsEverything) {
  std::string bmp = make_bmp24(4, 4, 1, 2, 3);
  TempFile f(bmp);
  CountingAllocator alloc;

  CJellyFormatImageType type = CJELLY_FORMAT_IMAGE_UNKNOWN;
  ASSERT_EQ(cjelly_format_image_detect_type(f.path(), alloc.get(), &type),
      CJELLY_FORMAT_IMAGE_SUCCESS);
  EXPECT_EQ(type, CJELLY_FORMAT_IMAGE_BMP);

  EXPECT_GT(alloc.allocations(), 0u) << "the file was read through malloc()";
  EXPECT_EQ(alloc.live(), 0) << "detect_type hands nothing back to free";
}

// A failed load must not leave the caller's allocator holding anything.
TEST(ImageAllocator, AFailedLoadReturnsEveryBlock) {
  TempFile f("definitely not an image");
  CountingAllocator alloc;

  CJellyFormatImage * image = nullptr;
  EXPECT_EQ(cjelly_format_image_load(f.path(), alloc.get(), &image),
      CJELLY_FORMAT_IMAGE_ERR_INVALID_FORMAT);
  EXPECT_EQ(image, nullptr);
  EXPECT_GT(alloc.allocations(), 0u) << "it got far enough to read the file";
  EXPECT_EQ(alloc.live(), 0) << "the error path leaked";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
