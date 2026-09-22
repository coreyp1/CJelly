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

#include <ghoti.io/cjelly/cj_allocator.h>
#include <ghoti.io/cutil/file.h>
#include <ghoti.io/cutil/path.h>

namespace cjtest {

/**
 * Resolve @p base to the absolute path of the fixture directory, or return
 * an empty string if it is not one.
 *
 * The lookup this replaces read CJELLY_TEST_DIR and fell back to the relative
 * string `"test"`, which is right when the binary is invoked from the library
 * directory - what `make test` does - and wrong everywhere else. A fixture
 * that did not resolve was then nobody's problem until some test opened it,
 * and the failure named that test's assertion about the contents of a file
 * that had never been read. **The relative fallback is what made it silent:**
 * an absolute one would have been wrong identically everywhere, which is a
 * bug someone notices on the first day. A relative one misbehaves only for
 * whoever is not using the blessed invocation, which is exactly the set of
 * people already debugging something.
 *
 * Two questions are asked, because "a directory of that name exists" is the
 * weaker one: the path has to resolve through the filesystem, and it has to
 * contain a fixture we know belongs to this repository. Some other `test/`
 * fails the second.
 */
inline std::string resolve_asset_dir(const char * base) {
  // Canonicalize rather than make absolute: this one consults the
  // filesystem, so a directory that is not there is a failure here instead
  // of a surprise at the first fopen().
  char * dir = nullptr;
  if (gcu_path_canonicalize(base, nullptr, &dir) != GCU_PATH_OK) {
    return std::string();
  }
  std::string resolved(dir);
  gcu_path_free(nullptr, dir);

  char sentinel[1024];
  if (gcu_path_join(GCU_PATH_NATIVE, resolved.c_str(), "models/cube.obj",
          sentinel, sizeof(sentinel), nullptr)
      != GCU_PATH_OK) {
    return std::string();
  }

  char * found = nullptr;
  if (gcu_path_canonicalize(sentinel, nullptr, &found) != GCU_PATH_OK) {
    return std::string();
  }
  gcu_path_free(nullptr, found);

  return resolved;
}

/**
 * Directory holding the checked-in sample assets (test/ in the repository),
 * as an absolute path.
 *
 * The Makefile passes it as CJELLY_TEST_DIR so the tests can be run from
 * anywhere; `test` relative to the working directory is tried when it is
 * unset, so a binary invoked from the library directory still works. What
 * does not happen any more is running without fixtures: an unresolvable
 * directory stops the binary with a message about the directory, rather
 * than letting every test that reads one fail on its own assertion.
 */
inline std::string asset_dir() {
  static const std::string dir = [] {
    const char * env = std::getenv("CJELLY_TEST_DIR");
    const char * base = env ? env : "test";
    std::string resolved = resolve_asset_dir(base);
    if (!resolved.empty()) {
      return resolved;
    }

    // Lexical, because the whole problem is that this path does not resolve;
    // it says what the relative string meant from here.
    char * meant = nullptr;
    if (gcu_path_absolute(base, nullptr, &meant) != GCU_PATH_OK) {
      meant = nullptr;
    }
    std::fprintf(stderr,
        "\n"
        "This test binary cannot find its fixtures.\n"
        "  looked in: %s%s\n"
        "  meaning:   %s\n"
        "\n"
        "Set CJELLY_TEST_DIR to the test/ directory of the cjelly checkout,\n"
        "or run `make test`, which sets it. Refusing to run: without the\n"
        "fixtures every test that reads one fails on its own assertion, and\n"
        "reads as a bug in the library rather than in the invocation.\n",
        base, env ? "" : "  (the fallback; CJELLY_TEST_DIR is not set)",
        meant ? meant : "(could not be resolved)");
    gcu_path_free(nullptr, meant);
    std::exit(1);
  }();
  return dir;
}

/** Path to a checked-in sample asset. */
inline std::string asset(const std::string & relative) {
  // Joined through cutil rather than with a literal '/': the separator is
  // not the same everywhere, and the concatenation also doubled it whenever
  // CJELLY_TEST_DIR was given with a trailing one.
  char joined[1024];
  if (gcu_path_join(GCU_PATH_NATIVE, asset_dir().c_str(), relative.c_str(),
          joined, sizeof(joined), nullptr)
      != GCU_PATH_OK) {
    ADD_FAILURE() << "could not join asset path: " << relative;
    return relative;
  }
  return std::string(joined);
}

/**
 * A file written to a temporary path and removed when the object goes out of
 * scope. Lets a test state its input inline rather than hiding it in a
 * fixture file, which matters most for the malformed cases.
 *
 * Created through cutil.  What stood here invented a name with mkstemp(),
 * deleted it, and reopened it with an extension appended - so between the
 * delete and the reopen the name was anybody's to take, and a symbolic link
 * left there would have been followed.  It also wrote to /tmp whatever
 * $TMPDIR said.  gcu_file_temp_create() chooses the name and creates the
 * file in one step that fails if the name is taken, in whatever directory
 * gcu_path_temp_dir() names.
 *
 * The decorative extension went with it: nothing in CJelly decides anything
 * from a file's name.  Images are identified by signature and the mesh
 * loader is handed a format, so a test that passes a `.bmp` name is
 * describing its intent to the reader and nothing else.
 */
class TempFile {
public:
  explicit TempFile(const std::string & contents) {
    if (gcu_file_temp_create(&temp_, nullptr, "cjelly_test", nullptr)
        != GCU_FILE_OK) {
      return;
    }
    path_ = gcu_file_temp_path(&temp_);

    FILE * f = gcu_file_temp_stream(&temp_);
    if (!contents.empty()
        && fwrite(contents.data(), 1, contents.size(), f) != contents.size()) {
      return;
    }
    // The handle keeps the file open; the code under test opens it again by
    // name, so what is buffered here has to have reached the file first.
    if (fflush(f) != 0) {
      return;
    }
    valid_ = true;
  }

  TempFile(const TempFile &) = delete;
  TempFile & operator=(const TempFile &) = delete;

  ~TempFile() {
    // Closes the stream and removes the file.  Accepts a zeroed handle, so
    // it does not need to know whether the constructor got that far.
    gcu_file_temp_abort(&temp_);
  }

  const char * path() const { return path_.c_str(); }
  bool valid() const { return valid_; }

private:
  GCU_File_Temp temp_ {};
  std::string path_;
  bool valid_ = false;
};


/**
 * An allocator that counts what passes through it.
 *
 * The point is not instrumentation for its own sake.  CJelly's engine
 * descriptor carried an `allocator` field for a long time that nothing ever
 * read, so a caller could supply one and every allocation still went to
 * malloc().  Nothing caught it because no test ever asked whether the
 * allocator was used - only whether the call succeeded, which it did.
 *
 * So these counters exist to be asserted non-zero.  A test that only checks
 * the balance returns to zero would pass against a library that ignored the
 * allocator completely.
 */
class CountingAllocator {
public:
  CountingAllocator() {
    vtable_.ctx = this;
    vtable_.malloc_fn = &CountingAllocator::do_malloc;
    vtable_.calloc_fn = &CountingAllocator::do_calloc;
    vtable_.realloc_fn = &CountingAllocator::do_realloc;
    vtable_.free_fn = &CountingAllocator::do_free;
  }

  CountingAllocator(const CountingAllocator &) = delete;
  CountingAllocator & operator=(const CountingAllocator &) = delete;

  const cj_allocator_t * get() const { return &vtable_; }

  size_t allocations() const { return allocations_; }
  size_t frees() const { return frees_; }
  /** Blocks handed out and not yet returned. */
  long live() const { return (long)allocations_ - (long)frees_; }

private:
  static CountingAllocator * self(void * ctx) {
    return static_cast<CountingAllocator *>(ctx);
  }

  static void * do_malloc(void * ctx, size_t size) {
    self(ctx)->allocations_++;
    return std::malloc(size ? size : 1);
  }
  static void * do_calloc(void * ctx, size_t nitems, size_t size) {
    // The contract requires overflow to be a failure rather than a short
    // allocation, so the counting wrapper has to honour it too.
    if (nitems && size > (size_t)-1 / nitems) {
      return nullptr;
    }
    self(ctx)->allocations_++;
    return std::calloc(nitems ? nitems : 1, size ? size : 1);
  }
  static void * do_realloc(void * ctx, void * ptr, size_t size) {
    if (!ptr) {
      self(ctx)->allocations_++;
    }
    return std::realloc(ptr, size ? size : 1);
  }
  static void do_free(void * ctx, void * ptr) {
    if (ptr) {
      self(ctx)->frees_++;
    }
    std::free(ptr);
  }

  cj_allocator_t vtable_ {};
  size_t allocations_ = 0;
  size_t frees_ = 0;
};

/** A path that is guaranteed not to exist. */
inline const char * missing_path() {
  return "/nonexistent/cjelly/definitely/not/here.dat";
}

} // namespace cjtest

#endif // CJELLY_TEST_HELPERS_H
