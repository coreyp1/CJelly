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
