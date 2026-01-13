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
 * Copyright (C) 2025 Ghoti.io
 */

#include <cjelly/application.h>
#include <cjelly/cj_window.h>
#include <cjelly/window_internal.h>

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

// Handle map entry type (matches anonymous struct in application.h)
typedef struct {
  void* handle;
  void* window;
} HandleMapEntry;


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
    GCJ_MAYBE_UNUSED(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity),
    GCJ_MAYBE_UNUSED(VkDebugUtilsMessageTypeFlagsEXT messageTypes),
    const VkDebugUtilsMessengerCallbackDataEXT * pCallbackData,
    GCJ_MAYBE_UNUSED(void * pUserData)) {
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
 * @brief Internal helper function to add an extension string to a dynamic
 * array.
 *
 * This function checks for duplicates, grows the array if needed, duplicates
 * the extension string, and appends it to the array.
 *
 * @param extensions A pointer to the dynamic array of extension strings.
 *                   (This pointer will be updated if the array is reallocated.)
 * @param count A pointer to the current number of extensions in the array.
 * @param capacity A pointer to the capacity of the array.
 * @param extension The extension string to add.
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success,
 *         or an error code on failure.
 */
static CJellyApplicationError add_extension_generic(const char *** extensions,
    size_t * count, size_t * capacity, const char * extension) {

  assert(extensions);
  assert(count);
  assert(capacity);
  assert(extensions);

  if (!extension)
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;

  // Check if the extension is already present.
  for (size_t i = 0; i < *count; i++) {
    if (strcmp((*extensions)[i], extension) == 0) {
      return CJELLY_APPLICATION_ERROR_NONE;
    }
  }

  // Expand the array if necessary.
  if (*count == *capacity) {
    size_t newCap = (*capacity) * 2;
    const char ** newArray = realloc(*extensions, sizeof(char *) * newCap);
    if (!newArray) {
      fprintf(stderr, "Failed to reallocate memory for extensions.\n");
      return CJELLY_APPLICATION_ERROR_INIT_FAILED;
    }
    *extensions = newArray;
    *capacity = newCap;
  }

  // Duplicate the extension string and add it to the array.
  char * dup = strdup(extension);
  if (!dup) {
    fprintf(stderr, "Failed to duplicate extension name.\n");
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }
  (*extensions)[*count] = dup;
  (*count)++;

  return CJELLY_APPLICATION_ERROR_NONE;
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

  // Instance Extensions.
  if (opts->requiredInstanceExtensions) {
    for (size_t i = 0; i < opts->requiredInstanceExtensionCount; i++) {
      free((void *)opts->requiredInstanceExtensions[i]);
    }
    free(opts->requiredInstanceExtensions);
  }
  opts->requiredInstanceExtensions = NULL;
  opts->requiredInstanceExtensionCount = 0;
  opts->requiredInstanceExtensionCapacity = 0;

  // Device Extensions.
  if (opts->requiredDeviceExtensions) {
    for (size_t i = 0; i < opts->requiredDeviceExtensionCount; i++) {
      free((void *)opts->requiredDeviceExtensions[i]);
    }
    free(opts->requiredDeviceExtensions);
  }
  opts->requiredDeviceExtensions = NULL;
  opts->requiredDeviceExtensionCount = 0;
  opts->requiredDeviceExtensionCapacity = 0;
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

  // Allocate the instance extension array.
  opts->requiredInstanceExtensionCount = 0;
  opts->requiredInstanceExtensionCapacity = INITIAL_EXTENSION_CAPACITY;
  opts->requiredInstanceExtensions =
      malloc(sizeof(char *) * opts->requiredInstanceExtensionCapacity);
  if (!opts->requiredInstanceExtensions) {
    fprintf(stderr,
        "Failed to allocate memory for required instance extensions.\n");
    goto ERROR_FREE_OPTIONS;
  }

  // Allocate the device extension array.
  opts->requiredDeviceExtensionCount = 0;
  opts->requiredDeviceExtensionCapacity = INITIAL_EXTENSION_CAPACITY;
  opts->requiredDeviceExtensions =
      malloc(sizeof(char *) * opts->requiredDeviceExtensionCapacity);
  if (!opts->requiredDeviceExtensions) {
    fprintf(
        stderr, "Failed to allocate memory for required device extensions.\n");
    goto ERROR_FREE_OPTIONS;
  }

  // Add instance extensions required by CJelly.
  // (Instance extensions are enabled during vkCreateInstance.)
  const char * instanceExtensions[] = {
      VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
      VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#else
      VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
  };
  size_t instanceExtCount =
      sizeof(instanceExtensions) / sizeof(instanceExtensions[0]);
  for (size_t i = 0; i < instanceExtCount; ++i) {
    if (add_extension_generic(&opts->requiredInstanceExtensions,
            &opts->requiredInstanceExtensionCount,
            &opts->requiredInstanceExtensionCapacity,
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
            &opts->requiredDeviceExtensionCount,
            &opts->requiredDeviceExtensionCapacity,
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

  // Initialize window tracking
  newApp->windows = NULL;
  newApp->window_count = 0;
  newApp->window_capacity = 0;
  newApp->handle_map = NULL;
  newApp->handle_map_count = 0;
  newApp->handle_map_capacity = 0;

  *app = newApp;
  return CJELLY_APPLICATION_ERROR_NONE;

  // Error handling.
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
      &app->options.requiredInstanceExtensionCount,
      &app->options.requiredInstanceExtensionCapacity, extension);
}


CJ_API CJellyApplicationError cjelly_application_add_device_extension(
    CJellyApplication * app, const char * extension) {

  if (!app)
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;

  return add_extension_generic(&app->options.requiredDeviceExtensions,
      &app->options.requiredDeviceExtensionCount,
      &app->options.requiredDeviceExtensionCapacity, extension);
}


CJ_API CJellyApplicationError cjelly_application_init(CJellyApplication * app) {
  CJellyApplicationError err = CJELLY_APPLICATION_ERROR_NONE;

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
  appInfo.pEngineName = CJELLY_ENGINE_NAME;
  appInfo.engineVersion = CJELLY_VERSION_UINT32;
  appInfo.apiVersion = app->options.requiredVulkanVersion;

  VkInstanceCreateInfo instanceCreateInfo = {0};
  instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCreateInfo.pApplicationInfo = &appInfo;
  instanceCreateInfo.enabledExtensionCount =
      app->options.requiredInstanceExtensionCount;
  instanceCreateInfo.ppEnabledExtensionNames =
      (const char **)app->options.requiredInstanceExtensions;

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
  VkPhysicalDevice * physicalDevices =
      malloc(sizeof(VkPhysicalDevice) * deviceCount);
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
    for (size_t e = 0; e < app->options.requiredDeviceExtensionCount; ++e) {
      const char * reqExt = app->options.requiredDeviceExtensions[e];
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
      float priority = 1.0f;                                                   \
      queueCreateInfos[queueCreateInfoCount].sType =                           \
          VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;                          \
      queueCreateInfos[queueCreateInfoCount].pNext = NULL;                     \
      queueCreateInfos[queueCreateInfoCount].flags = 0;                        \
      queueCreateInfos[queueCreateInfoCount].queueFamilyIndex = (family);      \
      queueCreateInfos[queueCreateInfoCount].queueCount = 1;                   \
      queueCreateInfos[queueCreateInfoCount].pQueuePriorities = &priority;     \
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
            &app->options.requiredDeviceExtensionCount,
            &app->options.requiredDeviceExtensionCapacity,
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
  deviceCreateInfo.enabledExtensionCount = app->options.requiredDeviceExtensionCount;
  deviceCreateInfo.ppEnabledExtensionNames = app->options.requiredDeviceExtensions;

  // Chain the descriptor indexing features if supported (minimal features only)
  if (descriptorIndexingSupported) {
    // Set up descriptor indexing features
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexingFeatures = {0};
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
  return app->window_count;
}

CJ_API uint32_t cjelly_application_get_windows(const CJellyApplication * app, 
                                        void** out_windows, 
                                        uint32_t window_count) {
  if (!app || !out_windows || window_count == 0)
    return 0;

  uint32_t count = (window_count < app->window_count) ? window_count : app->window_count;
  for (uint32_t i = 0; i < count; i++) {
    out_windows[i] = app->windows[i];
  }
  return count;
}

CJ_API void* cjelly_application_find_window_by_handle(CJellyApplication * app, void* handle) {
  if (!app || !handle || !app->handle_map)
    return NULL;

  HandleMapEntry* map = (HandleMapEntry*)app->handle_map;
  for (uint32_t i = 0; i < app->handle_map_count; i++) {
    if (map[i].handle == handle) {
      return map[i].window;
    }
  }
  return NULL;
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

  // Add to window list
  if (app->window_count >= app->window_capacity) {
    uint32_t new_capacity = (app->window_capacity == 0) ? 4 : app->window_capacity * 2;
    void** new_windows = realloc(app->windows, sizeof(void*) * new_capacity);
    if (!new_windows)
      return false;  // Out of memory - window list allocation failed
    app->windows = new_windows;
    app->window_capacity = new_capacity;
  }

  // Add to handle map BEFORE adding to window list, so we can rollback if handle map fails
  if (app->handle_map_count >= app->handle_map_capacity) {
    uint32_t new_capacity = (app->handle_map_capacity == 0) ? 4 : app->handle_map_capacity * 2;
    HandleMapEntry* new_map = realloc(app->handle_map, sizeof(HandleMapEntry) * new_capacity);
    if (!new_map)
      return false;  // Out of memory - handle map allocation failed
    // Cast through void* to work around anonymous struct type mismatch
    app->handle_map = (void*)new_map;
    app->handle_map_capacity = new_capacity;
  }

  // Both allocations succeeded, now add to both structures atomically
  HandleMapEntry* map = (HandleMapEntry*)app->handle_map;
  map[app->handle_map_count].handle = handle;
  map[app->handle_map_count].window = window;
  app->handle_map_count++;

  app->windows[app->window_count++] = window;

  return true;
}

static void remove_window_from_application(CJellyApplication * app, void* window, void* handle) {
  if (!app || !window)
    return;

  // Remove from window list
  for (uint32_t i = 0; i < app->window_count; i++) {
    if (app->windows[i] == window) {
      // Move last element to this position
      app->windows[i] = app->windows[app->window_count - 1];
      app->window_count--;
      break;
    }
  }

  // Remove from handle map
  if (handle && app->handle_map) {
    HandleMapEntry* map = (HandleMapEntry*)app->handle_map;
    for (uint32_t i = 0; i < app->handle_map_count; i++) {
      if (map[i].handle == handle) {
        // Move last element to this position
        map[i] = map[app->handle_map_count - 1];
        app->handle_map_count--;
        break;
      }
    }
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

  // Create a copy of the window list to avoid issues if windows are destroyed during iteration
  void** windows_copy = malloc(sizeof(void*) * app->window_count);
  if (!windows_copy)
    return;

  for (uint32_t i = 0; i < app->window_count; i++) {
    windows_copy[i] = app->windows[i];
  }
  uint32_t count = app->window_count;

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
