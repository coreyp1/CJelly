/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2025-2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io CJelly.
 *
 * Ghoti.io CJelly is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io CJelly is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file application.c
 * @brief CJelly Application API implementation.
 *
 * @details
 * This file implements the CJelly Application API, providing functions to
 * configure required and preferred constraints (such as Vulkan API version,
 * GPU memory, and device type), as well as required layers/extensions.
 * These options are stored internally and used during application
 * initialization.
 *
 * Author: Ghoti.io
 * Date: 2025
 */

// cjelly/macros.h, reached through application.h, defines _POSIX_C_SOURCE.
// It has to be seen before any system header is pulled in, so the CJelly
// includes come first and everything else follows.
#include <ghoti.io/cjelly/plat_internal.h>
#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/application.h>
#include <ghoti.io/cjelly/cj_window.h>
#include <ghoti.io/cjelly/window_internal.h>

#include <ghoti.io/cutil/hash.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


#define CJELLY_MINIMUM_VULKAN_VERSION VK_API_VERSION_1_2

/**
 * @brief Initial capacity for dynamic arrays of required and preferred layers.
 *
 * This value is used to allocate initial memory for the dynamic arrays that
 * store the names of required and preferred layers/extensions.
 *
 * It is only used in this file and is not exposed in the public API.
 */
#define INITIAL_EXTENSION_CAPACITY 10

/**
 * @brief Turn a platform window handle into a hash table key.
 *
 * GCU_Hash64 treats the key it is given as the identity of the entry, so two
 * distinct handles must never produce the same key or one would silently
 * displace the other and events would be routed to the wrong window. Both
 * mixers below are bijections on their width, which rules that out by
 * construction while still spreading the low bits - a raw pointer used as a
 * key would land in one of a handful of buckets, because allocation
 * alignment leaves its low bits zero.
 */
static size_t handle_hash(const void * handle) {
  uintptr_t raw = (uintptr_t)handle;
#if SIZE_MAX > 0xFFFFFFFFu
  // splitmix64 finalizer.
  uint64_t x = (uint64_t)raw;
  x ^= x >> 30;
  x *= 0xBF58476D1CE4E5B9ULL;
  x ^= x >> 27;
  x *= 0x94D049BB133111EBULL;
  x ^= x >> 31;
  return (size_t)x;
#else
  // 32-bit equivalent, for platforms where size_t cannot hold the 64-bit one.
  uint32_t x = (uint32_t)raw;
  x ^= x >> 16;
  x *= 0x7FEB352DU;
  x ^= x >> 15;
  x *= 0x846CA68BU;
  x ^= x >> 16;
  return (size_t)x;
#endif
}


/**
 * @brief Debug callback function for Vulkan validation layers.
 *
 * This callback is invoked by the validation layers when a message is
 * generated. It prints the validation message to standard error.
 *
 * @param messageSeverity Indicates the severity of the message.
 * @param messageTypes Indicates the type of the message.
 * @param pCallbackData Pointer to a structure containing details of the debug
 * message.
 * @param pUserData A user-defined pointer (unused in this implementation).
 * @return VkBool32 Always returns VK_FALSE.
 */
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    CJ_MAYBE_UNUSED(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity),
    CJ_MAYBE_UNUSED(VkDebugUtilsMessageTypeFlagsEXT messageTypes),
    const VkDebugUtilsMessengerCallbackDataEXT * pCallbackData,
    CJ_MAYBE_UNUSED(void * pUserData)) {
  fprintf(stderr, "Validation layer: %s\n", pCallbackData->pMessage);
  return VK_FALSE;
}


/**
 * @brief Dynamically loads and calls vkCreateDebugUtilsMessengerEXT.
 *
 * This helper function retrieves the function pointer for
 * vkCreateDebugUtilsMessengerEXT using vkGetInstanceProcAddr, and if available,
 * calls it to create a debug messenger.
 *
 * @param instance The Vulkan instance.
 * @param pCreateInfo Pointer to a VkDebugUtilsMessengerCreateInfoEXT structure
 * specifying the parameters of the debug messenger.
 * @param pAllocator Optional pointer to custom allocation callbacks.
 * @param pDebugMessenger Pointer to the variable that will receive the debug
 * messenger.
 * @return VkResult VK_SUCCESS on success, or VK_ERROR_EXTENSION_NOT_PRESENT if
 * the extension is not available.
 */
static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT * pCreateInfo,
    const VkAllocationCallbacks * pAllocator,
    VkDebugUtilsMessengerEXT * pDebugMessenger) {
  PFN_vkCreateDebugUtilsMessengerEXT func =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != NULL) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  }
  else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}


/**
 * @brief Dynamically loads and calls vkDestroyDebugUtilsMessengerEXT.
 *
 * This helper function retrieves the function pointer for
 * vkDestroyDebugUtilsMessengerEXT using vkGetInstanceProcAddr, and if
 * available, calls it to destroy a debug messenger.
 *
 * @param instance The Vulkan instance.
 * @param debugMessenger The debug messenger to destroy.
 * @param pAllocator Optional pointer to custom allocation callbacks.
 */
static void DestroyDebugUtilsMessengerEXT(VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks * pAllocator) {
  PFN_vkDestroyDebugUtilsMessengerEXT func =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != NULL) {
    func(instance, debugMessenger, pAllocator);
  }
}


/**
 * @brief Append an extension name to one of the options arrays.
 *
 * Duplicates are ignored rather than being an error: the same extension can
 * be required by CJelly and asked for again by the caller, and refusing the
 * second request would make the order they arrive in matter.
 *
 * The growth, the capacity and the reallocation are the array's now.  What
 * stood here doubled `*capacity`, which grows nothing from zero - reachable
 * only if an array ever arrived here uninitialised, which initialize_options
 * happened to prevent. There is no such edge on a GCU_Array.
 *
 * @param extensions The array to append to.
 * @param extension The name to append. Copied.
 * @return CJELLY_APPLICATION_ERROR_NONE, or an error code.
 */
static CJellyApplicationError add_extension_generic(
    GCU_Array * extensions, const char * extension) {
  assert(extensions);

  if (!extension) {
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;
  }

  size_t count = gcu_array_count(extensions);
  for (size_t i = 0; i < count; i++) {
    char ** slot = (char **)gcu_array_at(extensions, i);
    if (slot && *slot && strcmp(*slot, extension) == 0) {
      return CJELLY_APPLICATION_ERROR_NONE;
    }
  }

  char * dup = strdup(extension);
  if (!dup) {
    fprintf(stderr, "Failed to duplicate extension name.\n");
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }
  if (!gcu_array_append(extensions, &dup)) {
    // The append is what takes ownership, so until it succeeds the copy is
    // still this function's to release.
    free(dup);
    fprintf(stderr, "Failed to grow the extension array.\n");
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }

  return CJELLY_APPLICATION_ERROR_NONE;
}


/** Release every name an extension array holds, then the array itself. */
static void free_extension_array(GCU_Array * array) {
  assert(array);
  size_t count = gcu_array_count(array);
  for (size_t i = 0; i < count; i++) {
    char ** slot = (char **)gcu_array_at(array, i);
    if (slot) {
      free(*slot);
    }
  }
  gcu_array_destroy_in_place(array);
}


/**
 * @brief Helper function to free the dynamic arrays in options.
 *
 * This function frees all memory allocated for required and preferred layers.
 *
 * @param opts Pointer to the options structure whose arrays will be freed.
 */
static void free_options(CJellyApplicationOptions * opts) {
  assert(opts);

  // The array owns the strings but not their contents, so the strdup'd names
  // come back one at a time before the storage does.
  free_extension_array(&opts->requiredInstanceExtensions);
  free_extension_array(&opts->requiredDeviceExtensions);
}


/**
 * @brief Helper function to initialize internal options.
 *
 * This function initializes the options structure with default values and
 * allocates memory for the dynamic arrays for required instance and device
 * extensions.
 *
 * @param opts Pointer to the options structure to initialize.
 * @return true on success, false if a memory allocation fails.
 */
static bool initialize_options(CJellyApplicationOptions * opts) {
  assert(opts);

  opts->enableValidation = true;
  opts->requiredVulkanVersion = CJELLY_MINIMUM_VULKAN_VERSION;
  opts->requiredGPUMemory = 512;
  opts->requiredDeviceType = CJELLY_DEVICE_TYPE_ANY;
  opts->preferredDeviceType = CJELLY_DEVICE_TYPE_ANY;

  // Both arrays hold `char *`, reserved at the size the old code allocated
  // up front.  A GCU_Array would grow from nothing just as well; the reserve
  // only avoids a reallocation for the handful of names added below.
  if (!gcu_array_create_in_place(&opts->requiredInstanceExtensions,
          sizeof(char *), INITIAL_EXTENSION_CAPACITY, NULL)) {
    fprintf(stderr,
        "Failed to allocate memory for required instance extensions.\n");
    goto ERROR_FREE_OPTIONS;
  }

  if (!gcu_array_create_in_place(&opts->requiredDeviceExtensions,
          sizeof(char *), INITIAL_EXTENSION_CAPACITY, NULL)) {
    fprintf(
        stderr, "Failed to allocate memory for required device extensions.\n");
    goto ERROR_FREE_OPTIONS;
  }

  // Add instance extensions required by CJelly.
  // (Instance extensions are enabled during vkCreateInstance.)
  const char * instanceExtensions[] = {
      VK_KHR_SURFACE_EXTENSION_NAME,
      cj_plat_surface_extension_name(),
  };
  size_t instanceExtCount =
      sizeof(instanceExtensions) / sizeof(instanceExtensions[0]);
  for (size_t i = 0; i < instanceExtCount; ++i) {
    if (add_extension_generic(&opts->requiredInstanceExtensions,
            instanceExtensions[i]) != CJELLY_APPLICATION_ERROR_NONE) {
      fprintf(stderr, "Failed to add required instance extension: %s\n",
          instanceExtensions[i]);
      goto ERROR_FREE_OPTIONS;
    }
  }

  // Add device extensions required by CJelly.
  // (Device extensions are enabled during vkCreateDevice.)
  const char * requiredDeviceExtensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };
  size_t requiredDeviceExtCount =
      sizeof(requiredDeviceExtensions) / sizeof(requiredDeviceExtensions[0]);
  for (size_t i = 0; i < requiredDeviceExtCount; ++i) {
    if (add_extension_generic(&opts->requiredDeviceExtensions,
            requiredDeviceExtensions[i]) != CJELLY_APPLICATION_ERROR_NONE) {
      fprintf(stderr, "Failed to add required device extension: %s\n",
          requiredDeviceExtensions[i]);
      goto ERROR_FREE_OPTIONS;
    }
  }

  return true;

ERROR_FREE_OPTIONS:
  free_options(opts);
  return false;
}


CJ_API CJellyApplicationError cjelly_application_create(
    CJellyApplication ** app, const char * appName, uint32_t appVersion) {

  CJellyApplicationError err = CJELLY_APPLICATION_ERROR_NONE;

  if (!app || !appName) {
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;
  }

  /* Declared before any window is created, where the platform has such a
   * thing to declare. */
  cj_plat_declare_dpi_awareness();

  CJellyApplication * newApp = malloc(sizeof(CJellyApplication));
  if (!newApp) {
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }
  memset(newApp, 0, sizeof(CJellyApplication));

  newApp->appName = strdup(appName);
  if (!newApp->appName) {
    goto ERROR_CLEANUP_NEWAPP;
  }
  newApp->appVersion = appVersion;

  if (!initialize_options(&newApp->options)) {
    goto ERROR_CLEANUP_APPNAME;
  }

  // Set the Vulkan inforation to NULL.
  newApp->instance = VK_NULL_HANDLE;
  newApp->physicalDevice = VK_NULL_HANDLE;
  newApp->logicalDevice = VK_NULL_HANDLE;
  newApp->graphicsCommandPool = VK_NULL_HANDLE;
  newApp->transferCommandPool = VK_NULL_HANDLE;
  newApp->computeCommandPool = VK_NULL_HANDLE;
  newApp->vkContext = NULL;
  newApp->debugMessenger = VK_NULL_HANDLE;
  newApp->graphicsQueue = VK_NULL_HANDLE;
  newApp->transferQueue = VK_NULL_HANDLE;
  newApp->computeQueue = VK_NULL_HANDLE;

  // Initialize window tracking.  Zero reserved: most applications open one
  // window, and the array allocates on the first append.
  if (!gcu_array_create_in_place(
          &newApp->windows, sizeof(void *), 0, NULL)) {
    goto ERROR_CLEANUP_APPNAME;
  }
  newApp->handle_map = NULL;  // Created on the first window registration.

  // Initialize signal handling fields
  newApp->shutdown_requested = 0;
  newApp->shutdown_callback = NULL;
  newApp->shutdown_callback_user_data = NULL;
  if (!gcu_array_create_in_place(&newApp->custom_signal_handlers,
          sizeof(CJellyApplicationSignalHandler), 0, NULL)) {
    goto ERROR_CLEANUP_WINDOWS;
  }
  newApp->signal_handlers_registered = false;

  *app = newApp;
  return CJELLY_APPLICATION_ERROR_NONE;

  // Error handling.
ERROR_CLEANUP_WINDOWS:
  gcu_array_destroy_in_place(&newApp->windows);

ERROR_CLEANUP_APPNAME:
  free(newApp->appName);
  newApp->appName = NULL;

ERROR_CLEANUP_NEWAPP:
  free(newApp);
  return err == CJELLY_APPLICATION_ERROR_NONE
      ? CJELLY_APPLICATION_ERROR_OUT_OF_MEMORY
      : err;
}


CJ_API void cjelly_application_set_validation(CJellyApplication * app, bool enable) {
  if (!app)
    return;
  app->options.enableValidation = enable;
}


CJ_API void cjelly_application_set_required_vulkan_version(
    CJellyApplication * app, uint32_t version) {

  if (!app)
    return;

  // If the new version is lower than the current one, ignore it.
  if (version < app->options.requiredVulkanVersion) {
    return;
  }
  app->options.requiredVulkanVersion = version;
}


CJ_API void cjelly_application_set_required_gpu_memory(
    CJellyApplication * app, uint32_t memory) {

  if (!app)
    return;

  // If the new memory is lower than the current one, ignore it.
  if (memory < app->options.requiredGPUMemory) {
    return;
  }
  app->options.requiredGPUMemory = memory;
}


CJ_API void cjelly_application_set_device_type(
    CJellyApplication * app, CJellyApplicationDeviceType type, bool required) {

  if (!app)
    return;

  CJellyApplicationDeviceType * targetType = required
      ? &app->options.requiredDeviceType
      : &app->options.preferredDeviceType;

  // If the new type is lower than the current one, ignore it.
  if (type < *targetType) {
    return;
  }
  *targetType = type;
}


CJ_API CJellyApplicationError cjelly_application_add_instance_extension(
    CJellyApplication * app, const char * extension) {

  if (!app)
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;

  return add_extension_generic(&app->options.requiredInstanceExtensions,
            extension);
}


CJ_API CJellyApplicationError cjelly_application_add_device_extension(
    CJellyApplication * app, const char * extension) {

  if (!app)
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;

  return add_extension_generic(&app->options.requiredDeviceExtensions,
            extension);
}


CJ_API CJellyApplicationError cjelly_application_init(CJellyApplication * app) {
  CJellyApplicationError err = CJELLY_APPLICATION_ERROR_NONE;

  // Declared here rather than beside its malloc, because ERROR_RETURN reads
  // it and two of the gotos that reach ERROR_RETURN are above that malloc.
  // Jumping past a declaration is legal C and leaves the object
  // uninitialized, so those two paths - no Vulkan instance, and no physical
  // device, which is what any machine without a working driver does - read
  // an indeterminate pointer and free() it.
  VkPhysicalDevice * physicalDevices = NULL;

  if (!app) {
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;
  }

  // Check if the application has already been initialized.
  if ((app->instance != VK_NULL_HANDLE) ||
      (app->physicalDevice != VK_NULL_HANDLE) ||
      (app->graphicsCommandPool != VK_NULL_HANDLE)) {
    // The application has already been initialized.
    fprintf(stderr, "Application already initialized.\n");
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }

  // Check required Vulkan version.
  assert(app->options.requiredVulkanVersion);
  assert(app->options.requiredVulkanVersion >= CJELLY_MINIMUM_VULKAN_VERSION);

  // Query the Vulkan version supported by the driver.
  uint32_t installedVulkanVersion;
  PFN_vkEnumerateInstanceVersion pfnEnumerateInstanceVersion =
      (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(
          NULL, "vkEnumerateInstanceVersion");
  if (pfnEnumerateInstanceVersion) {
    VkResult res = pfnEnumerateInstanceVersion(&installedVulkanVersion);
    if (res != VK_SUCCESS) {
      fprintf(stderr, "Failed to query Vulkan version. Defaulting to 1.0.\n");
      return CJELLY_APPLICATION_ERROR_INIT_FAILED;
    }
  }
  else {
    fprintf(stderr, "vkEnumerateInstanceVersion not supported.\n");
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }

  // Check if the required Vulkan version is supported.
  if (app->options.requiredVulkanVersion > installedVulkanVersion) {
    fprintf(stderr, "Required Vulkan version (%u) not supported.\n",
        app->options.requiredVulkanVersion);
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;
  }

  // If validation is enabled, then add the debug extension.
  if (app->options.enableValidation) {
    err = cjelly_application_add_instance_extension(
        app, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (err != CJELLY_APPLICATION_ERROR_NONE) {
      fprintf(stderr, "Failed to add debug extension.\n");
      return CJELLY_APPLICATION_ERROR_OUT_OF_MEMORY;
    }
  }

  // Assemble the information needed to create the Vulkan instance.
  assert(app->instance == VK_NULL_HANDLE);
  VkApplicationInfo appInfo = {0};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = app->appName;
  appInfo.applicationVersion = app->appVersion;
  appInfo.pEngineName = CJ_VK_ENGINE_NAME;
  appInfo.engineVersion = CJ_VK_ENGINE_VERSION;
  appInfo.apiVersion = app->options.requiredVulkanVersion;

  VkInstanceCreateInfo instanceCreateInfo = {0};
  instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCreateInfo.pApplicationInfo = &appInfo;
  // `data` is the contiguous `char *` block Vulkan wants; the array exists
  // so that growing it is not this file's problem.
  instanceCreateInfo.enabledExtensionCount =
      (uint32_t)gcu_array_count(&app->options.requiredInstanceExtensions);
  instanceCreateInfo.ppEnabledExtensionNames =
      (const char * const *)app->options.requiredInstanceExtensions.data;

  // If validation layers are enabled, add the required layers and extensions.
  const char * validationLayers[] = {
      "VK_LAYER_KHRONOS_validation",
  };
  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
  if (app->options.enableValidation) {
    instanceCreateInfo.enabledLayerCount =
        sizeof(validationLayers) / sizeof(validationLayers[0]);
    instanceCreateInfo.ppEnabledLayerNames = validationLayers;

    // Set up debug messenger info so that it is used during instance creation.
    debugCreateInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;
    instanceCreateInfo.pNext =
        (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
  }

  // Create the Vulkan instance.
  VkResult res = vkCreateInstance(&instanceCreateInfo, NULL, &app->instance);
  if (res != VK_SUCCESS || app->instance == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to create Vulkan instance.\n");
    err = CJELLY_APPLICATION_ERROR_INIT_FAILED;
    goto ERROR_RETURN;
  }

  // Find out how many physical devices exist.
  uint32_t deviceCount = 0;
  res = vkEnumeratePhysicalDevices(app->instance, &deviceCount, NULL);
  if (res != VK_SUCCESS || deviceCount == 0) {
    fprintf(stderr, "Failed to enumerate physical devices.\n");
    err = CJELLY_APPLICATION_ERROR_INIT_FAILED;
    goto ERROR_RETURN;
  }

  // Allocate memory for the list of physical devices.
  physicalDevices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
  if (!physicalDevices) {
    fprintf(stderr, "Memory allocation failure for device list.\n");
    goto ERROR_RETURN;
  }

  // Retrieve the list of physical devices.
  res =
      vkEnumeratePhysicalDevices(app->instance, &deviceCount, physicalDevices);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "Failed to retrieve physical devices.\n");
    err = CJELLY_APPLICATION_ERROR_INIT_FAILED;
    goto ERROR_RETURN;
  }

  // Filter physical devices based on required options.
  // At the same time, find the best device based on preferred options.
  VkPhysicalDevice bestPhysicalDevice = VK_NULL_HANDLE;
  int64_t bestScore = INT64_MIN;

  for (uint32_t i = 0; i < deviceCount; i++) {
    VkPhysicalDevice physicalDevice = physicalDevices[i];
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    // Check physical device type requirement (if set).
    if (app->options.requiredDeviceType != CJELLY_DEVICE_TYPE_ANY) {
      if (app->options.requiredDeviceType == CJELLY_DEVICE_TYPE_DISCRETE &&
          properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        goto CONTINUE;
      }
      if (app->options.requiredDeviceType == CJELLY_DEVICE_TYPE_INTEGRATED &&
          properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        goto CONTINUE;
      }
    }

    // Check for required queue families.
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &queueFamilyCount, NULL);
    if (queueFamilyCount == 0) {
      goto CONTINUE;
    }

    VkQueueFamilyProperties * queueFamilies =
        malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    if (!queueFamilies) {
      fprintf(stderr, "Memory allocation failure for queue family list.\n");
      goto CONTINUE;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &queueFamilyCount, queueFamilies);

    bool graphicsFound = false;
    bool presentFound = false;
    // bool computeFound = false;

    // Check if the device supports graphics and compute capabilities.
    for (uint32_t j = 0; j < queueFamilyCount; ++j) {
      VkQueueFlags flags = queueFamilies[j].queueFlags;
      if (flags & VK_QUEUE_GRAPHICS_BIT) {
        graphicsFound = true;
      }
      // if (flags & VK_QUEUE_COMPUTE_BIT) {
      //   computeFound = true;
      // }
      presentFound = true; // Assume presentation support for now
    }

    // Free the queue family properties array.
    free(queueFamilies);

    // If required, ensure that at least one graphics and presentation queue are
    // available.
    if (!graphicsFound) {
      goto CONTINUE;
    }
    if (!presentFound) {
      goto CONTINUE;
    }

    // Query memory properties.
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint64_t totalMemory = 0;
    for (uint32_t j = 0; j < memProps.memoryHeapCount; ++j) {
      if (memProps.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        totalMemory += memProps.memoryHeaps[j].size;
      }
    }
    uint32_t totalMemoryMB = (uint32_t)(totalMemory / (1024 * 1024));
    if (app->options.requiredGPUMemory > totalMemoryMB) {
      goto CONTINUE;
    }

    // Query the number of available device extensions.
    uint32_t availableExtensionCount = 0;
    VkResult res = vkEnumerateDeviceExtensionProperties(
        physicalDevice, NULL, &availableExtensionCount, NULL);
    if (res != VK_SUCCESS) {
      fprintf(stderr, "Failed to enumerate device extensions.\n");
      goto CONTINUE;
    }

    // Allocate memory for the list of available extensions.
    VkExtensionProperties * availableExtensions =
        malloc(sizeof(VkExtensionProperties) * availableExtensionCount);
    if (!availableExtensions) {
      fprintf(stderr,
          "Memory allocation failure while enumerating device extensions.\n");
      goto CONTINUE;
    }

    // Retrieve the list of available extensions.
    res = vkEnumerateDeviceExtensionProperties(
        physicalDevice, NULL, &availableExtensionCount, availableExtensions);
    if (res != VK_SUCCESS) {
      fprintf(stderr, "Failed to retrieve device extensions.\n");
      goto FREE_AVAILABLE_EXTENSIONS;
    }

    // Print the list of available extensions.
    // printf("Available device extensions:\n");
    // for (uint32_t j = 0; j < availableExtensionCount; ++j) {
    //   printf("  %s\n", availableExtensions[j].extensionName);
    // }

    // For each required extension, check if it is present in the device's list.
    bool extensionsSupported = true;
    size_t requiredExtCount =
        gcu_array_count(&app->options.requiredDeviceExtensions);
    for (size_t e = 0; e < requiredExtCount; ++e) {
      const char * reqExt =
          *(char **)gcu_array_at(&app->options.requiredDeviceExtensions, e);
      bool found = false;
      for (uint32_t j = 0; j < availableExtensionCount; ++j) {
        if (strcmp(reqExt, availableExtensions[j].extensionName) == 0) {
          found = true;
          break;
        }
      }
      if (!found) {
        // fprintf(stderr, "Required extension '%s' is not supported.\n",
        // reqExt);
        extensionsSupported = false;
        break;
      }
    }

    if (extensionsSupported) {
      // If we made it this far, the physical device meets the required
      // constraints. Now, we can evaluate the physical device against the
      // preferred options.

      // Calculate the score for this physical device.
      int64_t score = 0;
      if (app->options.preferredDeviceType == CJELLY_DEVICE_TYPE_DISCRETE &&
          properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
      }
      else if (app->options.preferredDeviceType ==
              CJELLY_DEVICE_TYPE_INTEGRATED &&
          properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 500;
      }
      score += (totalMemoryMB - app->options.requiredGPUMemory) / 2;

      // If this device has a higher score, select it as the best device.
      // Note: As long as one device meets the required constraints, we are
      // assured that a valid device will be selected.
      if (score > bestScore) {
        bestScore = score;
        bestPhysicalDevice = physicalDevice;
      }
    }

  FREE_AVAILABLE_EXTENSIONS:
    free(availableExtensions);
  CONTINUE:
    // Continue the loop and evaluate the next device.
    continue;
  }

  // The list has served its purpose: the chosen device is held by handle in
  // bestPhysicalDevice, which does not point into the array. Freeing it here
  // rather than only in ERROR_RETURN is what stops the success path leaking
  // it, and leaves NULL behind for the gotos further down.
  free(physicalDevices);
  physicalDevices = NULL;

  // Check if a suitable physical device was found.
  if (bestPhysicalDevice == VK_NULL_HANDLE) {
    fprintf(stderr, "No physical device meets the required constraints.\n");
    err = CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;
    goto ERROR_RETURN;
  }
  app->physicalDevice = bestPhysicalDevice;

  // Create the logical device.
  err = cjelly_application_create_logical_device(app);
  if (err != CJELLY_APPLICATION_ERROR_NONE) {
    fprintf(stderr, "Failed to create logical device.\n");
    goto ERROR_RETURN;
  }

  // Create the command pool.
  err = cjelly_application_create_command_pools(app);
  if (err != CJELLY_APPLICATION_ERROR_NONE) {
    fprintf(stderr, "Failed to create command pool.\n");
    goto ERROR_RETURN;
  }

  // If validation layers are enabled, create the debug messenger.
  if (app->options.enableValidation) {
    // Prepare the debug messenger create info.
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    // Create the debug messenger.
    if (CreateDebugUtilsMessengerEXT(app->instance, &createInfo, NULL,
            &app->debugMessenger) != VK_SUCCESS) {
      fprintf(stderr, "Failed to set up debug messenger!\n");
      err = CJELLY_APPLICATION_ERROR_INIT_FAILED;
      goto ERROR_RETURN;
    }
  }

  return CJELLY_APPLICATION_ERROR_NONE;

  // Error handling.
ERROR_RETURN:
  // Destroy the list of physical devices.
  if (physicalDevices) {
    free(physicalDevices);
  }

  // Destroy the debug messenger if it was created.
  if (app->debugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(app->instance, app->debugMessenger, NULL);
    app->debugMessenger = VK_NULL_HANDLE;
  }

  // Destroy the logical device.
  if (app->logicalDevice != VK_NULL_HANDLE) {
    vkDestroyDevice(app->logicalDevice, NULL);
    app->logicalDevice = VK_NULL_HANDLE;
  }

  // Destroy the Vulkan instance.
  if (app->instance != VK_NULL_HANDLE) {
    vkDestroyInstance(app->instance, NULL);
    app->instance = VK_NULL_HANDLE;
  }

  // Return the error code.
  // For convenience, we return an out-of-memory error if the error code is
  // CJELLY_APPLICATION_ERROR_NONE.
  return err == CJELLY_APPLICATION_ERROR_NONE
      ? CJELLY_APPLICATION_ERROR_OUT_OF_MEMORY
      : err;
}


CJ_API void cjelly_application_destroy(CJellyApplication * app) {
  if (!app)
    return;

  // Destroy the debug messenger if it was created.
  if (app->debugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(app->instance, app->debugMessenger, NULL);
    app->debugMessenger = VK_NULL_HANDLE;
  }

  // Destroy the command pools.
  // If multiple pools are shared (i.e. point to the same handle), ensure that
  // you only destroy them once.
  if (app->graphicsCommandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(app->logicalDevice, app->graphicsCommandPool, NULL);
  }
  if (app->transferCommandPool != VK_NULL_HANDLE &&
      app->transferCommandPool != app->graphicsCommandPool) {
    vkDestroyCommandPool(app->logicalDevice, app->transferCommandPool, NULL);
  }
  if (app->computeCommandPool != VK_NULL_HANDLE &&
      app->computeCommandPool != app->graphicsCommandPool &&
      app->computeCommandPool != app->transferCommandPool) {
    vkDestroyCommandPool(app->logicalDevice, app->computeCommandPool, NULL);
  }
  app->graphicsCommandPool = VK_NULL_HANDLE;
  app->transferCommandPool = VK_NULL_HANDLE;
  app->computeCommandPool = VK_NULL_HANDLE;

  // Destroy the logical device.
  if (app->logicalDevice != VK_NULL_HANDLE) {
    vkDestroyDevice(app->logicalDevice, NULL);
    app->logicalDevice = VK_NULL_HANDLE;
  }

  // Destroy the Vulkan instance.
  if (app->instance != VK_NULL_HANDLE) {
    vkDestroyInstance(app->instance, NULL);
  }

  // Destroy the options.
  free_options(&app->options);

  // Free window tracking
  {
    gcu_array_destroy_in_place(&app->windows);
  }
  if (app->handle_map) {
    gcu_hash64_destroy((GCU_Hash64 *)app->handle_map);
    app->handle_map = NULL;
  }

  // Free signal handling
  {
    gcu_array_destroy_in_place(&app->custom_signal_handlers);
  }

  // Free the application name if it was allocated.
  if (app->appName) {
    free(app->appName);
  }
  free(app);
}


CJ_API CJellyApplicationError cjelly_application_create_logical_device(
    CJellyApplication * app) {
  if (!app || app->physicalDevice == VK_NULL_HANDLE) {
    fprintf(stderr, "Invalid application or physical device not set.\n");
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;
  }

  // Query queue family properties.
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(
      app->physicalDevice, &queueFamilyCount, NULL);
  if (queueFamilyCount == 0) {
    fprintf(stderr, "No queue families found.\n");
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }

  VkQueueFamilyProperties * queueFamilies =
      malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
  if (!queueFamilies) {
    fprintf(stderr, "Memory allocation failure for queue families.\n");
    return CJELLY_APPLICATION_ERROR_OUT_OF_MEMORY;
  }
  vkGetPhysicalDeviceQueueFamilyProperties(
      app->physicalDevice, &queueFamilyCount, queueFamilies);

  // Variables to hold chosen queue family indices.
  int graphicsFamily = -1;
  int transferFamily = -1;
  int computeFamily = -1;

  // Loop through queue families and select candidates.
  for (uint32_t i = 0; i < queueFamilyCount; ++i) {
    VkQueueFlags flags = queueFamilies[i].queueFlags;

    // For graphics: require VK_QUEUE_GRAPHICS_BIT.
    // Presentation support will be checked when actual surfaces are created.
    if (graphicsFamily < 0 && (flags & VK_QUEUE_GRAPHICS_BIT)) {
      graphicsFamily = i;
    }

    // For transfer: prefer a queue that supports transfer only.
    if (transferFamily < 0 && (flags & VK_QUEUE_TRANSFER_BIT)) {
      if (!(flags & VK_QUEUE_GRAPHICS_BIT) && !(flags & VK_QUEUE_COMPUTE_BIT)) {
        transferFamily = i;
      }
    }

    // For compute: require VK_QUEUE_COMPUTE_BIT; prefer one that doesn't
    // support graphics.
    if (computeFamily < 0 && (flags & VK_QUEUE_COMPUTE_BIT)) {
      if (!(flags & VK_QUEUE_GRAPHICS_BIT)) {
        computeFamily = i;
      }
    }
  }
  free(queueFamilies);

  // Fallback: if a dedicated transfer queue wasn't found, use the graphics
  // queue.
  if (transferFamily < 0) {
    transferFamily = graphicsFamily;
  }
  // Fallback: if a dedicated compute queue wasn't found, use the graphics
  // queue.
  if (computeFamily < 0) {
    computeFamily = graphicsFamily;
  }

  // Build an array of unique queue families to be used for device creation.
  VkDeviceQueueCreateInfo queueCreateInfos[3] = {0}; // Zero-initialize the entire array
  uint32_t queueCreateInfoCount = 0;
  int usedFamilies[3];
  int usedCount = 0;

  // The priority every queue is created with. It lives here, at function
  // scope, because the macro below stores its address in
  // pQueuePriorities and Vulkan does not read through that pointer until
  // vkCreateDevice, hundreds of lines later. Declared inside the macro's
  // `do { } while (0)` it was dead by then: three separate objects, each
  // one gone at the closing brace, and vkCreateDevice read whatever the
  // frames in between had left on the stack. ASan calls it a
  // stack-use-after-scope; Vulkan called it a priority.
  const float queuePriority = 1.0f;

  // Macro to add a queue family only once.
#define ADD_QUEUE_INFO(family)                                                 \
  do {                                                                         \
    bool alreadyAdded = false;                                                 \
    for (int i = 0; i < usedCount; ++i) {                                      \
      if (usedFamilies[i] == (family)) {                                       \
        alreadyAdded = true;                                                   \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
    if (!alreadyAdded) {                                                       \
      queueCreateInfos[queueCreateInfoCount].sType =                           \
          VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;                          \
      queueCreateInfos[queueCreateInfoCount].pNext = NULL;                     \
      queueCreateInfos[queueCreateInfoCount].flags = 0;                        \
      queueCreateInfos[queueCreateInfoCount].queueFamilyIndex = (family);      \
      queueCreateInfos[queueCreateInfoCount].queueCount = 1;                   \
      queueCreateInfos[queueCreateInfoCount].pQueuePriorities =                \
          &queuePriority;                                                      \
      usedFamilies[usedCount++] = (family);                                    \
      queueCreateInfoCount++;                                                  \
    }                                                                          \
  } while (0)

  ADD_QUEUE_INFO(graphicsFamily);
  ADD_QUEUE_INFO(transferFamily);
  ADD_QUEUE_INFO(computeFamily);
#undef ADD_QUEUE_INFO

  // Check for descriptor indexing extension support

  // Query available device extensions
  uint32_t availableExtensionCount = 0;
  vkEnumerateDeviceExtensionProperties(app->physicalDevice, NULL, &availableExtensionCount, NULL);
  VkExtensionProperties *availableExtensions = malloc(sizeof(VkExtensionProperties) * availableExtensionCount);
  vkEnumerateDeviceExtensionProperties(app->physicalDevice, NULL, &availableExtensionCount, availableExtensions);

  bool descriptorIndexingSupported = false;
  for (uint32_t i = 0; i < availableExtensionCount; i++) {
    if (strcmp(availableExtensions[i].extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0) {
      descriptorIndexingSupported = true;
      break;
    }
  }

  // Add descriptor indexing extension if supported
  if (descriptorIndexingSupported) {
    if (add_extension_generic(&app->options.requiredDeviceExtensions,
            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) != CJELLY_APPLICATION_ERROR_NONE) {
      fprintf(stderr, "Failed to add descriptor indexing extension\n");
      free(availableExtensions);
      return CJELLY_APPLICATION_ERROR_INIT_FAILED;
    }
  }

  app->supportsBindlessRendering = descriptorIndexingSupported;

  VkDeviceCreateInfo deviceCreateInfo = {0};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.pNext = NULL;  // Ensure clean pNext chain
  deviceCreateInfo.flags = 0;     // No special device creation flags
  deviceCreateInfo.queueCreateInfoCount = queueCreateInfoCount;
  deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;

  // Enable the required extensions
  deviceCreateInfo.enabledExtensionCount =
      (uint32_t)gcu_array_count(&app->options.requiredDeviceExtensions);
  deviceCreateInfo.ppEnabledExtensionNames =
      (const char * const *)app->options.requiredDeviceExtensions.data;

  // Chain the descriptor indexing features if supported (minimal features only)
  //
  // Declared out here, not inside the `if`, for the same reason
  // queuePriority is declared at the top of the function: vkCreateDevice
  // walks the pNext chain, and a struct scoped to the `if` is gone by the
  // time it does. What it read then was whatever had since been written over
  // that slot, starting with an sType the loader does not recognise - a
  // segmentation fault inside libvulkan, from a frame that looked correct.
  VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexingFeatures = {0};
  if (descriptorIndexingSupported) {
    // Set up descriptor indexing features
    descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
    descriptorIndexingFeatures.pNext = NULL;

    // Enable only the essential features for bindless rendering
    descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    deviceCreateInfo.pNext = &descriptorIndexingFeatures;
  }

  // Finally, create the logical device.
  VkResult result = vkCreateDevice(
      app->physicalDevice, &deviceCreateInfo, NULL, &app->logicalDevice);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Failed to create logical device. VkResult: %d\n", result);
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }

  // Retrieve the queues.
  vkGetDeviceQueue(app->logicalDevice, graphicsFamily, 0, &app->graphicsQueue);
  vkGetDeviceQueue(app->logicalDevice, transferFamily, 0, &app->transferQueue);
  vkGetDeviceQueue(app->logicalDevice, computeFamily, 0, &app->computeQueue);

  // Clean up allocated memory
  free(availableExtensions);

  return CJELLY_APPLICATION_ERROR_NONE;
}


CJ_API bool cjelly_application_supports_bindless_rendering(CJellyApplication * app) {
  if (!app) {
    return false;
  }
  return app->supportsBindlessRendering;
}

// Window tracking implementation

CJ_API uint32_t cjelly_application_window_count(const CJellyApplication * app) {
  if (!app)
    return 0;
  return (uint32_t)gcu_array_count(&app->windows);
}

CJ_API uint32_t cjelly_application_get_windows(const CJellyApplication * app,
                                        void** out_windows,
                                        uint32_t window_count) {
  if (!app || !out_windows || window_count == 0)
    return 0;

  uint32_t held = (uint32_t)gcu_array_count(&app->windows);
  uint32_t count = (window_count < held) ? window_count : held;
  for (uint32_t i = 0; i < count; i++) {
    out_windows[i] = *(void **)gcu_array_at(&app->windows, i);
  }
  return count;
}

CJ_API void* cjelly_application_find_window_by_handle(CJellyApplication * app, void* handle) {
  if (!app || !handle || !app->handle_map)
    return NULL;

  GCU_Hash64_Value found =
      gcu_hash64_get((GCU_Hash64 *)app->handle_map, handle_hash(handle));
  return found.exists ? found.value.p : NULL;
}

// Global current application pointer (similar to engine)
static CJellyApplication* g_current_application = NULL;

CJ_API CJellyApplication* cjelly_application_get_current(void) {
  return g_current_application;
}

CJ_API void cjelly_application_set_current(CJellyApplication* app) {
  g_current_application = app;
}

// Returns true on success, false on failure (OOM)
static bool add_window_to_application(CJellyApplication * app, void* window, void* handle) {
  if (!app || !window || !handle)
    return false;

  // Reserve room in the window list first, but do not append yet.  If the
  // handle map insertion then fails, the list is simply left with spare
  // capacity, so there is nothing to roll back.
  if (!gcu_array_reserve(&app->windows, gcu_array_count(&app->windows) + 1))
    return false;  // Out of memory - window list allocation failed

  if (!app->handle_map) {
    app->handle_map = gcu_hash64_create(0);
    if (!app->handle_map)
      return false;  // Out of memory - handle map allocation failed
  }

  if (!gcu_hash64_set((GCU_Hash64 *)app->handle_map, handle_hash(handle),
          GCU_TYPE64_P(window)))
    return false;  // Out of memory - handle map insertion failed

  // Reserved above, so this cannot fail and cannot leave the map and the
  // list disagreeing.
  (void)gcu_array_append(&app->windows, &window);

  return true;
}

static void remove_window_from_application(CJellyApplication * app, void* window, void* handle) {
  if (!app || !window)
    return;

  // Remove from window list.  The order of this list is not meaningful, so
  // the last element moves into the gap - which is what swap_remove does.
  size_t held = gcu_array_count(&app->windows);
  for (size_t i = 0; i < held; i++) {
    if (*(void **)gcu_array_at(&app->windows, i) == window) {
      (void)gcu_array_swap_remove(&app->windows, i, NULL);
      break;
    }
  }

  // Remove from handle map
  if (handle && app->handle_map) {
    (void)gcu_hash64_remove(
        (GCU_Hash64 *)app->handle_map, handle_hash(handle));
  }
}

CJ_API bool cjelly_application_register_window(CJellyApplication * app, void* window, void* handle) {
  if (!app) app = cjelly_application_get_current();
  if (app) {
    return add_window_to_application(app, window, handle);
  }
  return false;
}

CJ_API void cjelly_application_unregister_window(CJellyApplication * app, void* window, void* handle) {
  if (!app) app = cjelly_application_get_current();
  if (app) {
    remove_window_from_application(app, window, handle);
  }
}

CJ_API void cjelly_application_close_all_windows(CJellyApplication * app, bool cancellable) {
  if (!app)
    return;

  // Copy the list first: closing a window unregisters it, which mutates the
  // array being walked.
  uint32_t count = (uint32_t)gcu_array_count(&app->windows);
  if (count == 0)
    return;
  void ** windows_copy = malloc(sizeof(void *) * count);
  if (!windows_copy)
    return;

  for (uint32_t i = 0; i < count; i++) {
    windows_copy[i] = *(void **)gcu_array_at(&app->windows, i);
  }

  // Close each window
  for (uint32_t i = 0; i < count; i++) {
    cj_window_t* window = (cj_window_t*)windows_copy[i];
    if (window) {
      cj_window_close_with_callback(window, cancellable);
    }
  }

  free(windows_copy);
}


CJ_API CJellyApplicationError cjelly_application_create_command_pools(
    CJellyApplication * app) {

  if (!app || app->logicalDevice == VK_NULL_HANDLE) {
    fprintf(stderr, "Invalid application or logical device not set.\n");
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;
  }

  VkCommandPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  VkResult result;

  // Create the graphics command pool.
  poolInfo.queueFamilyIndex = app->graphicsQueueFamilyIndex;
  result = vkCreateCommandPool(
      app->logicalDevice, &poolInfo, NULL, &app->graphicsCommandPool);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Failed to create graphics command pool. VkResult: %d\n",
        result);
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }

  // Create the transfer command pool.
  // If the transfer queue is in the same family as graphics, reuse the graphics
  // pool.
  if (app->transferQueueFamilyIndex == app->graphicsQueueFamilyIndex) {
    app->transferCommandPool = app->graphicsCommandPool;
  }
  else {
    poolInfo.queueFamilyIndex = app->transferQueueFamilyIndex;
    result = vkCreateCommandPool(
        app->logicalDevice, &poolInfo, NULL, &app->transferCommandPool);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Failed to create transfer command pool. VkResult: %d\n",
          result);
      return CJELLY_APPLICATION_ERROR_INIT_FAILED;
    }
  }

  // Create the compute command pool.
  // If the compute queue is in the same family as graphics or transfer, reuse
  // that pool.
  if (app->computeQueueFamilyIndex == app->graphicsQueueFamilyIndex) {
    app->computeCommandPool = app->graphicsCommandPool;
  }
  else if (app->computeQueueFamilyIndex == app->transferQueueFamilyIndex) {
    app->computeCommandPool = app->transferCommandPool;
  }
  else {
    poolInfo.queueFamilyIndex = app->computeQueueFamilyIndex;
    result = vkCreateCommandPool(
        app->logicalDevice, &poolInfo, NULL, &app->computeCommandPool);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Failed to create compute command pool. VkResult: %d\n",
          result);
      return CJELLY_APPLICATION_ERROR_INIT_FAILED;
    }
  }

  return CJELLY_APPLICATION_ERROR_NONE;
}

/*
 * =============================================================================
 * SIGNAL HANDLING
 * =============================================================================
 *
 * Signal handlers require special care because they execute in unsafe contexts
 * where most operations (malloc, free, Vulkan calls, etc.) are prohibited.
 *
 * WINDOWS vs LINUX - Different Mechanisms, Same Solution
 * -------------------------------------------------------
 *
 * WINDOWS (SetConsoleCtrlHandler):
 *   - Handler runs in a SEPARATE THREAD created by Windows
 *   - Main thread continues running concurrently
 *   - Problem: True race condition - two threads accessing same data
 *   - If handler destroys windows while main thread renders: CRASH
 *
 * LINUX/UNIX (signal()):
 *   - Handler runs in the SAME THREAD, but at an arbitrary interruption point
 *   - Kernel interrupts main thread (e.g., mid-malloc, mid-Vulkan call)
 *   - Handler executes, then main thread resumes where it was interrupted
 *   - Problem: Main thread state may be inconsistent
 *   - Only "async-signal-safe" functions can be called (very limited set)
 *   - malloc, free, printf, and ALL Vulkan functions are NOT safe
 *
 * THE SOLUTION (both platforms):
 *   - Signal handlers ONLY set a flag: app->shutdown_requested = 1
 *   - Main loop checks cjelly_application_should_shutdown() each iteration
 *   - Cleanup happens safely in the main thread after the loop exits
 *
 * WHY volatile sig_atomic_t:
 *   - volatile: prevents compiler from caching the value in a register
 *   - sig_atomic_t: guaranteed atomic read/write even when interrupted
 *   - This type is safe to access from signal handlers on all platforms
 *
 * =============================================================================
 */

/*
 * Asked for by the platform when the user asks the process to stop.
 *
 * IMPORTANT: on POSIX this interrupts the main thread at an arbitrary point
 * - it could be inside malloc(), a Vulkan call, or anything else
 * non-reentrant - and on Windows it runs on a SEPARATE THREAD from the main
 * loop. Either way it can only set a flag. Destroying a window or freeing
 * memory here would race with whatever the main loop is doing.
 *
 * @return false if there is no application to tell, so that a platform which
 *         distinguishes "handled" from "not handled" can fall back to its
 *         own default.
 */
static bool app_request_shutdown(void) {
  CJellyApplication* app = cjelly_application_get_current();
  if (!app)
    return false;

  app->shutdown_requested = 1;
  return true;
}

CJ_API void cjelly_application_register_signal_handlers(CJellyApplication* app) {
  if (!app || app->signal_handlers_registered)
    return;

  cj_plat_register_shutdown_handler(app_request_shutdown);

  app->signal_handlers_registered = true;
}

CJ_API bool cjelly_application_should_shutdown(const CJellyApplication* app) {
  if (!app)
    return false;
  return app->shutdown_requested != 0;
}

CJ_API void cjelly_application_on_shutdown(CJellyApplication* app,
                                            cjelly_shutdown_callback_t callback,
                                            void* user_data) {
  if (!app)
    return;
  app->shutdown_callback = callback;
  app->shutdown_callback_user_data = user_data;
}

CJ_API void cjelly_application_on_signal(CJellyApplication* app,
                                          int signal,
                                          cjelly_signal_handler_t handler,
                                          void* user_data) {
  if (!app)
    return;

  // Replace the handler for a signal already registered, so that calling
  // this twice for one signal does not leave two entries with the first
  // winning the lookup.
  size_t count = gcu_array_count(&app->custom_signal_handlers);
  for (size_t i = 0; i < count; i++) {
    CJellyApplicationSignalHandler * entry =
        (CJellyApplicationSignalHandler *)gcu_array_at(
            &app->custom_signal_handlers, i);
    if (entry->signal == signal) {
      entry->handler = handler;
      entry->user_data = user_data;
      return;
    }
  }

  // The element type is named now, so this is an ordinary append rather than
  // the cast through void* the anonymous struct used to require.
  CJellyApplicationSignalHandler entry = {signal, handler, user_data};
  (void)gcu_array_append(&app->custom_signal_handlers, &entry);
}
