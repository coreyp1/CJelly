/**
 * @file test_vk_debug.cpp
 *
 * Tests for the half of "turn the validation layers on" that is easy to
 * leave out.
 *
 * Enabling VK_LAYER_KHRONOS_validation makes the layer run its checks. It
 * does not make anyone hear about them: the layer reports through a debug
 * messenger the application registers, and an instance that enables the layer
 * and registers nothing runs every check and discards every answer. There is
 * no error, no warning and no difference in behaviour - it looks exactly like
 * an instance with nothing wrong.
 *
 * cj_engine_init_vulkan() was in that state. The demo's documented headless
 * run reported zero validation errors, and it was a run that could not have
 * reported one; with a messenger registered the same run reports a pipeline
 * warning, four write-after-present hazards from the capture path and seven
 * objects outliving the memory they were bound to.
 *
 * So the assertions here are mostly about absence being distinguishable from
 * silence, which is the part that had no test.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <gtest/gtest.h>

#include <vulkan/vulkan.h>

#include <ghoti.io/cjelly/cj_engine.h>
#include <ghoti.io/cjelly/engine_internal.h>
#include <ghoti.io/cjelly/vk_debug_internal.h>

#include <cstdio>

namespace {

/**
 * Create a bare instance, optionally asking for the debug-utils extension.
 *
 * No layers: the point of these two is the extension, and requiring the
 * validation layer to be installed would make the test unrunnable on a
 * machine that simply has not got it - which is a different question.
 */
VkResult make_instance(bool with_debug_utils, VkInstance * out) {
  *out = VK_NULL_HANDLE;

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "cjelly-test";
  app.apiVersion = VK_API_VERSION_1_0;

  const char * extensions[] = {CJ_VK_DEBUG_EXTENSION_NAME};

  VkInstanceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &app;
  if (with_debug_utils) {
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = extensions;
  }
  return vkCreateInstance(&ci, nullptr, out);
}

} // namespace

/* The description every messenger in the library is built from. Its severity
 * mask is the whole of the library's policy on what can ever be heard, so
 * dropping a bit from it is a silent loss of exactly that severity. */
TEST(VkDebugDescribe, SubscribesToWarningsAndErrors) {
  VkDebugUtilsMessengerCreateInfoEXT info{};
  cj_vk_debug__describe(&info);

  EXPECT_EQ(info.sType,
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
  EXPECT_TRUE(info.messageSeverity
      & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
  EXPECT_TRUE(info.messageSeverity
      & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT);
  EXPECT_NE(info.pfnUserCallback, nullptr);
}

/* All three types, because a validation error and a performance warning
 * arrive on different ones and subscribing to a subset drops a whole
 * category without ever saying so. */
TEST(VkDebugDescribe, SubscribesToEveryMessageType) {
  VkDebugUtilsMessengerCreateInfoEXT info{};
  cj_vk_debug__describe(&info);

  EXPECT_TRUE(info.messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT);
  EXPECT_TRUE(
      info.messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT);
  EXPECT_TRUE(
      info.messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
}

TEST(VkDebugDescribe, IgnoresANullDestination) {
  cj_vk_debug__describe(nullptr); /* must not crash */
}

/* The exact shape of the defect: an instance without the extension cannot
 * register a messenger, and says so rather than appearing to succeed. */
TEST(VkDebugCreate, RefusesAnInstanceWithoutTheExtension) {
  VkInstance instance = VK_NULL_HANDLE;
  if (make_instance(false, &instance) != VK_SUCCESS) {
    GTEST_SKIP() << "no Vulkan instance could be created on this machine";
  }

  VkDebugUtilsMessengerEXT messenger = reinterpret_cast<
      VkDebugUtilsMessengerEXT>(static_cast<uintptr_t>(0xdeadbeef));
  EXPECT_EQ(cj_vk_debug__create(instance, &messenger),
      VK_ERROR_EXTENSION_NOT_PRESENT);
  /* Cleared even on the failing path, so a caller can hand it straight to
   * the destroy call without first asking whether the create worked. */
  EXPECT_EQ(messenger, VK_NULL_HANDLE);

  vkDestroyInstance(instance, nullptr);
}

TEST(VkDebugCreate, RegistersAgainstAnInstanceWithTheExtension) {
  VkInstance instance = VK_NULL_HANDLE;
  if (make_instance(true, &instance) != VK_SUCCESS) {
    GTEST_SKIP() << "VK_EXT_debug_utils is not available on this machine";
  }

  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  EXPECT_EQ(cj_vk_debug__create(instance, &messenger), VK_SUCCESS);
  EXPECT_NE(messenger, VK_NULL_HANDLE);

  cj_vk_debug__destroy(instance, messenger);
  vkDestroyInstance(instance, nullptr);
}

TEST(VkDebugCreate, RefusesANullInstance) {
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  EXPECT_NE(cj_vk_debug__create(VK_NULL_HANDLE, &messenger), VK_SUCCESS);
  EXPECT_EQ(messenger, VK_NULL_HANDLE);
}

TEST(VkDebugDestroy, AcceptsNullHandles) {
  cj_vk_debug__destroy(VK_NULL_HANDLE, VK_NULL_HANDLE); /* must not crash */
}

/* The regression guard proper. An engine asked for validation must come back
 * with somewhere for the layer to report to; one not asked must not. */
TEST(EngineValidation, InitialisingWithValidationRegistersAMessenger) {
  cj_engine_desc_t desc{};
  cj_engine_t * engine = cj_engine_create(&desc);
  ASSERT_NE(engine, nullptr);

  if (!cj_engine_init(engine, 1)) {
    /* No driver, or no validation layer installed. Nothing was created, so
     * there is nothing to assert about - and saying which arm ran keeps a
     * run that only ever took this one from reading as coverage. */
    std::printf(
        "EngineValidation: init(validation) failed here; nothing asserted\n");
    cj_engine_shutdown(engine);
    GTEST_SKIP() << "Vulkan with validation is not available on this machine";
  }

  EXPECT_NE(cj_engine_debug_messenger(engine), VK_NULL_HANDLE)
      << "the validation layer was enabled with nowhere to report to";

  cj_engine_shutdown_device(engine);
  EXPECT_EQ(cj_engine_debug_messenger(engine), VK_NULL_HANDLE);
  cj_engine_shutdown(engine);
}

TEST(EngineValidation, InitialisingWithoutValidationRegistersNothing) {
  cj_engine_desc_t desc{};
  cj_engine_t * engine = cj_engine_create(&desc);
  ASSERT_NE(engine, nullptr);

  if (!cj_engine_init(engine, 0)) {
    cj_engine_shutdown(engine);
    GTEST_SKIP() << "Vulkan is not available on this machine";
  }

  EXPECT_EQ(cj_engine_debug_messenger(engine), VK_NULL_HANDLE);

  cj_engine_shutdown_device(engine);
  cj_engine_shutdown(engine);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
