/**
 * @file test_application_handles.cpp
 *
 * Tests for the window handle map: the platform-handle -> window lookup the
 * event loop performs for every arriving event.
 *
 * These drive a zeroed CJellyApplication directly rather than going through
 * cjelly_application_create(), which would need a Vulkan instance. The
 * registration path touches only the window list and the handle map, so a
 * bare struct is enough and the whole thing stays runnable without a GPU.
 *
 * Copyright 2026 by Corey Pennycuff
 */

// gtest has to be parsed before cjelly/application.h, which pulls in Xlib.h.
// Xlib defines None, Bool and Status as macros, and gtest declares types with
// those names; the other way round the test does not compile.
#include <gtest/gtest.h>

#include <ghoti.io/cjelly/application.h>

#include <ghoti.io/cutil/hash.h>
#include <cstdint>
#include <set>
#include <vector>

namespace {

/**
 * A CJellyApplication with nothing but the window-tracking fields live.
 *
 * Releases exactly what the registration path allocates, so the tests can run
 * under Valgrind without a real application teardown.
 */
class BareApp {
public:
  BareApp() {
    app_ = {};
    // The window list is a GCU_Array now, and a zeroed one has an element
    // size of zero - so it has to be initialised here exactly as
    // cjelly_application_create() does, not merely zeroed.
    EXPECT_TRUE(
        gcu_array_create_in_place(&app_.windows, sizeof(void *), 0, nullptr));
  }

  ~BareApp() {
    gcu_array_destroy_in_place(&app_.windows);
    if (app_.handle_map) {
      gcu_hash64_destroy((GCU_Hash64 *)app_.handle_map);
    }
  }

  BareApp(const BareApp &) = delete;
  BareApp & operator=(const BareApp &) = delete;

  CJellyApplication * get() { return &app_; }

private:
  CJellyApplication app_;
};

/** A handle value that is not a real pointer, in the style of an X11 XID. */
void * xid(uintptr_t value) {
  return reinterpret_cast<void *>(value);
}

} // namespace

TEST(HandleMap, RegisteredHandleResolves) {
  BareApp app;
  int window = 0;

  ASSERT_TRUE(
      cjelly_application_register_window(app.get(), &window, xid(0x2000001)));
  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), xid(0x2000001)),
      &window);
  EXPECT_EQ(cjelly_application_window_count(app.get()), 1u);
}

TEST(HandleMap, UnknownHandleResolvesToNull) {
  BareApp app;
  int window = 0;
  ASSERT_TRUE(
      cjelly_application_register_window(app.get(), &window, xid(0x2000001)));

  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), xid(0x2000002)),
      nullptr);
}

TEST(HandleMap, LookupOnAnEmptyApplicationIsNull) {
  BareApp app;
  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), xid(1)),
      nullptr);
}

TEST(HandleMap, NullArgumentsAreRejected) {
  BareApp app;
  int window = 0;

  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), nullptr),
      nullptr);
  EXPECT_FALSE(
      cjelly_application_register_window(app.get(), nullptr, xid(1)));
  EXPECT_FALSE(
      cjelly_application_register_window(app.get(), &window, nullptr));
  EXPECT_EQ(cjelly_application_window_count(app.get()), 0u);
}

TEST(HandleMap, EveryHandleInALargeSetResolvesToItsOwnWindow) {
  // The point of the test: the map keys entries by a hash of the handle, and
  // two handles colliding would silently route one window's events to the
  // other. Registering many at once, of the shapes that actually occur -
  // small sequential XIDs and aligned heap pointers - is what would expose
  // that.
  BareApp app;
  const size_t count = 512;

  std::vector<int> windows(count);
  std::vector<void *> handles;
  handles.reserve(count);

  // Half sequential XID-like values, half real (16-byte aligned) pointers.
  for (size_t i = 0; i < count / 2; i++) {
    handles.push_back(xid(0x2000001 + i));
  }
  for (size_t i = count / 2; i < count; i++) {
    handles.push_back(&windows[i]);
  }

  // Distinct handles, or the test would be checking nothing.
  std::set<void *> unique(handles.begin(), handles.end());
  ASSERT_EQ(unique.size(), count);

  for (size_t i = 0; i < count; i++) {
    ASSERT_TRUE(cjelly_application_register_window(
        app.get(), &windows[i], handles[i]))
        << "registering handle " << i;
  }
  ASSERT_EQ(cjelly_application_window_count(app.get()), count);

  for (size_t i = 0; i < count; i++) {
    EXPECT_EQ(
        cjelly_application_find_window_by_handle(app.get(), handles[i]),
        &windows[i])
        << "handle " << i << " resolved to the wrong window";
  }
}

TEST(HandleMap, UnregisterRemovesOnlyThatEntry) {
  BareApp app;
  int a = 0, b = 0, c = 0;

  ASSERT_TRUE(cjelly_application_register_window(app.get(), &a, xid(0x10)));
  ASSERT_TRUE(cjelly_application_register_window(app.get(), &b, xid(0x20)));
  ASSERT_TRUE(cjelly_application_register_window(app.get(), &c, xid(0x30)));

  cjelly_application_unregister_window(app.get(), &b, xid(0x20));

  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), xid(0x20)),
      nullptr);
  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), xid(0x10)), &a);
  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), xid(0x30)), &c);
  EXPECT_EQ(cjelly_application_window_count(app.get()), 2u);
}

TEST(HandleMap, HandleCanBeReusedAfterUnregistering) {
  // A closed window's handle can come back for a new window. The map has to
  // give the slot up rather than keep answering with the old entry.
  BareApp app;
  int first = 0, second = 0;

  ASSERT_TRUE(
      cjelly_application_register_window(app.get(), &first, xid(0x40)));
  cjelly_application_unregister_window(app.get(), &first, xid(0x40));
  ASSERT_TRUE(
      cjelly_application_register_window(app.get(), &second, xid(0x40)));

  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), xid(0x40)),
      &second);
}

TEST(HandleMap, ChurnLeavesTheSurvivorsIntact) {
  // Repeated open/close cycles are where a table that never reclaims removed
  // slots would grow without bound or start missing entries.
  BareApp app;
  const size_t rounds = 200;
  std::vector<int> keep(8);

  for (size_t i = 0; i < keep.size(); i++) {
    ASSERT_TRUE(cjelly_application_register_window(
        app.get(), &keep[i], xid(0x1000 + i)));
  }

  int transient = 0;
  for (size_t r = 0; r < rounds; r++) {
    void * handle = xid(0x9000 + r);
    ASSERT_TRUE(
        cjelly_application_register_window(app.get(), &transient, handle));
    EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), handle),
        &transient);
    cjelly_application_unregister_window(app.get(), &transient, handle);
    EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), handle),
        nullptr);
  }

  for (size_t i = 0; i < keep.size(); i++) {
    EXPECT_EQ(
        cjelly_application_find_window_by_handle(app.get(), xid(0x1000 + i)),
        &keep[i])
        << "survivor " << i << " lost during churn";
  }
  EXPECT_EQ(cjelly_application_window_count(app.get()), keep.size());
}

TEST(HandleMap, UnregisteringAnUnknownHandleIsHarmless) {
  BareApp app;
  int window = 0;
  ASSERT_TRUE(
      cjelly_application_register_window(app.get(), &window, xid(0x50)));

  cjelly_application_unregister_window(app.get(), &window, xid(0x99));

  EXPECT_EQ(cjelly_application_find_window_by_handle(app.get(), xid(0x50)),
      &window);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
