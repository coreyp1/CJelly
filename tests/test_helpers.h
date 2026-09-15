/**
 * @file test_helpers.h
 *
 * Shared helpers for the CJelly unit tests.
 *
 * The tests here cover the parts of CJelly that do not need a GPU or a
 * display: the OBJ, MTL and BMP parsers and the image dispatch layer. They
 * run anywhere, which is the point - the demo under `make demo` needs Vulkan
 * and a window, so it cannot serve as the project's test suite.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef CJELLY_TEST_HELPERS_H
#define CJELLY_TEST_HELPERS_H

#include <cstdio>
#include <cstdlib>
#include <string>
#include <gtest/gtest.h>

namespace cjtest {

/**
 * Directory holding the checked-in sample assets (test/ in the repository).
 * The Makefile passes it as CJELLY_TEST_DIR so the tests can be run from
 * anywhere; it falls back to the conventional relative path.
 */
inline std::string asset_dir() {
  const char * env = std::getenv("CJELLY_TEST_DIR");
  return env ? std::string(env) : std::string("test");
}

/** Path to a checked-in sample asset. */
inline std::string asset(const std::string & relative) {
  return asset_dir() + "/" + relative;
}

/**
 * A file written to a temporary path and removed when the object goes out of
 * scope. Lets a test state its input inline rather than hiding it in a
 * fixture file, which matters most for the malformed cases.
 */
class TempFile {
public:
  explicit TempFile(const std::string & contents, const char * suffix = ".tmp") {
    char name[] = "/tmp/cjelly_test_XXXXXX";
    int fd = mkstemp(name);
    if (fd >= 0) {
      close(fd);
      ::remove(name);
    }
    path_ = std::string(name) + suffix;
    FILE * f = fopen(path_.c_str(), "wb");
    if (f) {
      if (!contents.empty()) {
        fwrite(contents.data(), 1, contents.size(), f);
      }
      fclose(f);
      valid_ = true;
    }
  }

  TempFile(const TempFile &) = delete;
  TempFile & operator=(const TempFile &) = delete;

  ~TempFile() {
    if (valid_) {
      ::remove(path_.c_str());
    }
  }

  const char * path() const { return path_.c_str(); }
  bool valid() const { return valid_; }

private:
  std::string path_;
  bool valid_ = false;
};

/** A path that is guaranteed not to exist. */
inline const char * missing_path() {
  return "/nonexistent/cjelly/definitely/not/here.dat";
}

} // namespace cjtest

#endif // CJELLY_TEST_HELPERS_H
