/**
 * @file test_resources.cpp
 *
 * Unit tests for the engine's generation-counted resource handle table.
 *
 * cj_engine_create() is a plain allocation - it touches no Vulkan - so the
 * handle allocator can be exercised without a device or a display. The point
 * of the generation counter is that a handle to a freed slot stays rejected
 * once that slot is reused, which is exactly the kind of thing that is easy
 * to get subtly wrong and impossible to notice by running the demo.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"
#include <cjelly/cj_engine.h>
#include <cjelly/cj_handle.h>
#include <cjelly/engine_internal.h>
#include <gtest/gtest.h>
#include <set>
#include <vector>

namespace {

/** An engine that is destroyed with the test. */
class Engine {
public:
  Engine() { e_ = cj_engine_create(nullptr); }
  ~Engine() { cj_engine_shutdown(e_); }
  Engine(const Engine &) = delete;
  Engine & operator=(const Engine &) = delete;
  cj_engine_t * get() const { return e_; }

private:
  cj_engine_t * e_ = nullptr;
};

const cj_res_kind_t kKinds[] = {CJ_RES_TEX, CJ_RES_BUF, CJ_RES_SMP};

} // namespace

TEST(EngineLifecycle, CreateAndShutdown) {
  cj_engine_t * e = cj_engine_create(nullptr);
  ASSERT_NE(e, nullptr);
  cj_engine_shutdown(e);
}

TEST(EngineLifecycle, ShutdownNullIsSafe) {
  cj_engine_shutdown(nullptr);
}

//
// Allocation
//

TEST(ResourceTable, AllocReturnsUsableHandleAndSlot) {
  Engine e;
  for (cj_res_kind_t kind : kKinds) {
    uint32_t slot = 0;
    uint64_t h = cj_engine_res_alloc(e.get(), kind, &slot);
    EXPECT_NE(h, 0u) << "kind " << (int)kind;
    // Slot 0 is reserved so that a zero handle can mean "none".
    EXPECT_NE(slot, 0u) << "kind " << (int)kind;
    EXPECT_EQ(cj_engine_res_slot(e.get(), kind, h), slot);
  }
}

TEST(ResourceTable, DistinctAllocationsGetDistinctSlots) {
  Engine e;
  std::set<uint32_t> slots;
  std::set<uint64_t> handles;
  for (int i = 0; i < 64; i++) {
    uint32_t slot = 0;
    uint64_t h = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
    ASSERT_NE(h, 0u) << "allocation " << i;
    EXPECT_TRUE(slots.insert(slot).second) << "slot " << slot << " reused";
    EXPECT_TRUE(handles.insert(h).second) << "handle repeated";
  }
}

TEST(ResourceTable, KindsHaveIndependentTables) {
  Engine e;
  uint32_t tex_slot = 0, buf_slot = 0;
  uint64_t tex = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &tex_slot);
  uint64_t buf = cj_engine_res_alloc(e.get(), CJ_RES_BUF, &buf_slot);
  ASSERT_NE(tex, 0u);
  ASSERT_NE(buf, 0u);
  // Releasing a texture must not disturb the buffer table.
  cj_engine_res_release(e.get(), CJ_RES_TEX, tex);
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, tex), 0u);
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_BUF, buf), buf_slot);
}

//
// Generations: the reason the table exists
//

TEST(ResourceTable, ReleasedHandleStopsResolving) {
  Engine e;
  uint32_t slot = 0;
  uint64_t h = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
  ASSERT_NE(h, 0u);
  ASSERT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, h), slot);
  cj_engine_res_release(e.get(), CJ_RES_TEX, h);
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, h), 0u);
}

// The slot comes back, but the old handle must not: this is what the
// generation counter is for.
TEST(ResourceTable, StaleHandleRejectedAfterSlotReuse) {
  Engine e;
  uint32_t first_slot = 0;
  uint64_t first = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &first_slot);
  ASSERT_NE(first, 0u);
  cj_engine_res_release(e.get(), CJ_RES_TEX, first);

  uint32_t second_slot = 0;
  uint64_t second = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &second_slot);
  ASSERT_NE(second, 0u);
  EXPECT_EQ(second_slot, first_slot) << "the freed slot should be reused";
  EXPECT_NE(second, first) << "but the handle must differ";
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, first), 0u)
      << "the stale handle still resolves";
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, second), second_slot);
}

// Releasing through a stale handle must not free whoever owns the slot now.
TEST(ResourceTable, StaleReleaseDoesNotFreeTheCurrentOwner) {
  Engine e;
  uint32_t slot = 0;
  uint64_t first = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
  cj_engine_res_release(e.get(), CJ_RES_TEX, first);
  uint64_t second = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
  ASSERT_NE(second, 0u);

  cj_engine_res_release(e.get(), CJ_RES_TEX, first); // stale: must be ignored
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, second), slot)
      << "a stale release freed the live handle";
}

//
// Reference counting
//

TEST(ResourceTable, RetainKeepsHandleAliveForOneExtraRelease) {
  Engine e;
  uint32_t slot = 0;
  uint64_t h = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
  ASSERT_NE(h, 0u);
  cj_engine_res_retain(e.get(), CJ_RES_TEX, h);

  cj_engine_res_release(e.get(), CJ_RES_TEX, h);
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, h), slot)
      << "one retain should survive one release";

  cj_engine_res_release(e.get(), CJ_RES_TEX, h);
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, h), 0u);
}

TEST(ResourceTable, ReleasingTwiceIsHarmless) {
  Engine e;
  uint32_t slot = 0;
  uint64_t h = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
  cj_engine_res_release(e.get(), CJ_RES_TEX, h);
  cj_engine_res_release(e.get(), CJ_RES_TEX, h);
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, h), 0u);
  // The table must still work afterwards.
  uint64_t again = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
  EXPECT_NE(again, 0u);
}

TEST(ResourceTable, RetainOnStaleHandleIsIgnored) {
  Engine e;
  uint32_t slot = 0;
  uint64_t h = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
  cj_engine_res_release(e.get(), CJ_RES_TEX, h);
  cj_engine_res_retain(e.get(), CJ_RES_TEX, h);
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, h), 0u)
      << "retaining a dead handle revived it";
}

//
// Exhaustion and bounds
//

TEST(ResourceTable, ExhaustionReportsFailureThenRecovers) {
  Engine e;
  std::vector<uint64_t> handles;
  for (;;) {
    uint32_t slot = 0;
    uint64_t h = cj_engine_res_alloc(e.get(), CJ_RES_SMP, &slot);
    if (h == 0) {
      break;
    }
    handles.push_back(h);
    ASSERT_LT(handles.size(), (size_t)CJ_ENGINE_MAX_SAMPLERS + 1)
        << "allocator handed out more than the table holds";
  }
  // Slot 0 is reserved, so capacity - 1 entries are usable.
  EXPECT_EQ(handles.size(), (size_t)CJ_ENGINE_MAX_SAMPLERS - 1);

  cj_engine_res_release(e.get(), CJ_RES_SMP, handles.back());
  uint32_t slot = 0;
  EXPECT_NE(cj_engine_res_alloc(e.get(), CJ_RES_SMP, &slot), 0u)
      << "a freed slot should be handed out again";
}

TEST(ResourceTable, OutOfRangeAndZeroHandlesRejected) {
  Engine e;
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, 0), 0u);
  // An index far past the table.
  uint64_t bogus = ((uint64_t)0xFFFFFFu << 32) | 1u;
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, bogus), 0u);
  cj_engine_res_release(e.get(), CJ_RES_TEX, bogus); // must not fault
  cj_engine_res_retain(e.get(), CJ_RES_TEX, bogus);
}

TEST(ResourceTable, NullEngineIsSafe) {
  uint32_t slot = 123;
  EXPECT_EQ(cj_engine_res_alloc(nullptr, CJ_RES_TEX, &slot), 0u);
  EXPECT_EQ(cj_engine_res_slot(nullptr, CJ_RES_TEX, 1), 0u);
  cj_engine_res_release(nullptr, CJ_RES_TEX, 1);
  cj_engine_res_retain(nullptr, CJ_RES_TEX, 1);
}

TEST(ResourceTable, AllocToleratesNullOutSlot) {
  Engine e;
  uint64_t h = cj_engine_res_alloc(e.get(), CJ_RES_TEX, nullptr);
  EXPECT_NE(h, 0u);
}

//
// The cj_handle_* wrappers over the same table
//

TEST(HandleApi, RoundTripsThroughTheSameTable) {
  Engine e;
  uint32_t slot = 0;
  cj_handle_t h = cj_handle_alloc(e.get(), CJ_HANDLE_TEX, &slot);
  EXPECT_NE(slot, 0u);
  EXPECT_EQ(cj_handle_slot(e.get(), CJ_HANDLE_TEX, h), slot);
  cj_handle_release(e.get(), CJ_HANDLE_TEX, h);
  EXPECT_EQ(cj_handle_slot(e.get(), CJ_HANDLE_TEX, h), 0u);
}

TEST(HandleApi, RetainMatchesRelease) {
  Engine e;
  uint32_t slot = 0;
  cj_handle_t h = cj_handle_alloc(e.get(), CJ_HANDLE_BUF, &slot);
  cj_handle_retain(e.get(), CJ_HANDLE_BUF, h);
  cj_handle_release(e.get(), CJ_HANDLE_BUF, h);
  EXPECT_EQ(cj_handle_slot(e.get(), CJ_HANDLE_BUF, h), slot);
  cj_handle_release(e.get(), CJ_HANDLE_BUF, h);
  EXPECT_EQ(cj_handle_slot(e.get(), CJ_HANDLE_BUF, h), 0u);
}

// The handle is documented as (index:32 | generation:32).
TEST(HandleApi, PacksIndexAndGeneration) {
  Engine e;
  uint32_t slot = 0;
  uint64_t raw = cj_engine_res_alloc(e.get(), CJ_RES_TEX, &slot);
  ASSERT_NE(raw, 0u);
  cj_handle_t h = cj_handle_alloc(e.get(), CJ_HANDLE_TEX, &slot);
  EXPECT_NE(h.gen, 0u) << "generation 0 is reserved for 'never allocated'";
  uint64_t rebuilt = ((uint64_t)h.idx << 32) | (uint64_t)h.gen;
  EXPECT_EQ(cj_engine_res_slot(e.get(), CJ_RES_TEX, rebuilt), slot);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
