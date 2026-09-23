/**
 * @file test_application_init.cpp
 *
 * Tests for cjelly_application_init(): the real one, through Vulkan, not the
 * bare-struct shortcut that test_application_handles.cpp takes.
 *
 * This is the only test that executes the function, which is why it exists.
 * On its own it asserts very little - the interesting question is not what
 * init() returns but what it leaves behind, and the answer to that comes from
 * `make test-asan`, where this same test is what carries the allocator and
 * the stack into the sanitizer's view. Two defects were sitting on these
 * paths until it was written: a use-after-scope on the queue priorities
 * vkCreateDevice reads, and a pointer read before its declaration on the
 * no-driver path.
 *
 * Both outcomes of init() are legitimate here. A machine with a working
 * driver initialises; one without - a headless builder, a container with no
 * ICD - reports INIT_FAILED, and that is a path worth exercising rather than
 * skipping, because it is the one an unlucky user hits. The test says which
 * of the two it took, so a run that only ever saw the failing path cannot be
 * mistaken for coverage of the other.
 *
 * Copyright 2026 by Corey Pennycuff
 */

// gtest has to be parsed before cjelly/application.h, which pulls in Xlib.h.
// Xlib defines None, Bool and Status as macros, and gtest declares types with
// those names; the other way round the test does not compile.
#include <gtest/gtest.h>

#include <ghoti.io/cjelly/application.h>

#include <cstdio>

namespace {

/**
 * Run one create/init/destroy cycle and report which arm init() took.
 */
CJellyApplicationError init_cycle(const char * what) {
  CJellyApplication * app = nullptr;
  EXPECT_EQ(cjelly_application_create(&app, "cjelly-tests", 1),
      CJELLY_APPLICATION_ERROR_NONE);
  EXPECT_NE(app, nullptr);
  if (!app) {
    return CJELLY_APPLICATION_ERROR_OUT_OF_MEMORY;
  }

  CJellyApplicationError err = cjelly_application_init(app);
  std::fprintf(stderr, "    [%s] init -> %s\n", what,
      err == CJELLY_APPLICATION_ERROR_NONE ? "a Vulkan device was found"
                                           : "no usable Vulkan device");

  cjelly_application_destroy(app);
  return err;
}

TEST(ApplicationInit, ACycleSucceedsOrFailsCleanly) {
  CJellyApplicationError err = init_cycle("cycle 1");

  // Anything else means init() invented an error code, or returned success
  // through a path that had already given up. OUT_OF_MEMORY is what the
  // error tail returns when a goto arrives without setting err, so seeing it
  // on a machine that is not out of memory would say a cleanup path had been
  // reached by accident.
  EXPECT_TRUE(err == CJELLY_APPLICATION_ERROR_NONE
      || err == CJELLY_APPLICATION_ERROR_INIT_FAILED)
      << "unexpected error code " << (int)err;
}

TEST(ApplicationInit, TwoCyclesInOneProcessAgree) {
  // The second cycle is the one that would catch state left behind in the
  // first: a device handle that outlived its application, or an allocation
  // freed twice. It must also reach the same verdict, because nothing about
  // the machine changed between them.
  CJellyApplicationError first = init_cycle("cycle 1");
  CJellyApplicationError second = init_cycle("cycle 2");
  EXPECT_EQ(first, second);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
