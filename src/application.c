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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * @brief Helper function to add an extension to the options.
 *
 * @param opts Pointer to the options structure.
 * @param extension The name of the extension to add.
 * @return true on success, false on failure (e.g. memory allocation failure).
 */
static bool add_extension_internal(
    CJellyApplicationOptions * opts, const char * extension) {

  assert(opts);
  assert(extension);
  assert(opts->requiredExtensions);

  // Check if the layer name is already present in the required layers.
  for (size_t i = 0; i < opts->requiredExtensionCount; i++) {
    if (strcmp(opts->requiredExtensions[i], extension) == 0) {
      // Layer already exists, no need to add it again.
      return true;
    }
  }

  // Reserve space for the new layer.
  if (opts->requiredExtensionCount == opts->requiredExtensionCapacity) {
    size_t newCap = opts->requiredExtensionCapacity * 2;
    const char ** newArray =
        realloc(opts->requiredExtensions, sizeof(char *) * newCap);
    if (!newArray) {
      fprintf(stderr, "Failed to reallocate memory for layers.\n");
      return false;
    }
    opts->requiredExtensions = newArray;
    opts->requiredExtensionCapacity = newCap;
  }

  // Duplicate the layer name and add it to the array.
  char * dup = strdup(extension);
  if (!dup) {
    fprintf(stderr, "Failed to duplicate layer name.\n");
    return false;
  }
  opts->requiredExtensions[opts->requiredExtensionCount] = dup;
  opts->requiredExtensionCount++;

  // Everything went well, return success.
  return true;
}


/**
 * @brief Helper function to initialize internal options.
 *
 * This function initializes the options structure with default values and
 * allocates memory for the dynamic arrays for required and preferred layers.
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
  opts->requiredExtensionCount = 0;
  opts->requiredExtensionCapacity = INITIAL_EXTENSION_CAPACITY;
  opts->requiredExtensions =
      malloc(sizeof(char *) * opts->requiredExtensionCapacity);
  if (!opts->requiredExtensions) {
    fprintf(stderr, "Failed to allocate memory for required layers.\n");
    return false;
  }

  // Add extensions required by CJelly.
  const char * cjellyRequiredExtensions[] = {
      VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      VK_KHR_MAINTENANCE3_EXTENSION_NAME,
      VK_KHR_SURFACE_EXTENSION_NAME,
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef _WIN32
      VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#else
      VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
  };
  for (size_t i = 0; i <
      sizeof(cjellyRequiredExtensions) / sizeof(cjellyRequiredExtensions[0]);
      ++i) {
    if (!add_extension_internal(opts, cjellyRequiredExtensions[i])) {
      fprintf(stderr, "Failed to add CJelly required layer: %s\n",
          cjellyRequiredExtensions[i]);
      goto ERROR_FREE_OPTIONS;
    }
  }

  return true;

  // Error handling.
ERROR_FREE_OPTIONS:
  free(opts->requiredExtensions);
  return false;
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

  if (opts->requiredExtensions) {
    for (size_t i = 0; i < opts->requiredExtensionCount; i++) {
      free((void *)opts->requiredExtensions[i]);
    }
    free(opts->requiredExtensions);
  }
}


CJellyApplicationError cjelly_application_create(
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
  newApp->commandPool = VK_NULL_HANDLE;
  newApp->vkContext = NULL;
  newApp->debugMessenger = VK_NULL_HANDLE;
  newApp->graphicsQueue = VK_NULL_HANDLE;
  newApp->transferQueue = VK_NULL_HANDLE;
  newApp->computeQueue = VK_NULL_HANDLE;
  newApp->headlessSurface = VK_NULL_HANDLE;

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


void cjelly_application_set_validation(CJellyApplication * app, bool enable) {
  if (!app)
    return;
  app->options.enableValidation = enable;
}


void cjelly_application_set_required_vulkan_version(
    CJellyApplication * app, uint32_t version) {

  if (!app)
    return;

  // If the new version is lower than the current one, ignore it.
  if (version < app->options.requiredVulkanVersion) {
    return;
  }
  app->options.requiredVulkanVersion = version;
}


void cjelly_application_set_required_gpu_memory(
    CJellyApplication * app, uint32_t memory) {

  if (!app)
    return;

  // If the new memory is lower than the current one, ignore it.
  if (memory < app->options.requiredGPUMemory) {
    return;
  }
  app->options.requiredGPUMemory = memory;
}


void cjelly_application_set_device_type(
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


CJellyApplicationError cjelly_application_add_extension(
    CJellyApplication * app, const char * extension) {

  if (!app || !extension)
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;

  if (!add_extension_internal(&app->options, extension)) {
    fprintf(stderr, "Failed to add layer: %s\n", extension);
    return CJELLY_APPLICATION_ERROR_INIT_FAILED;
  }

  return CJELLY_APPLICATION_ERROR_NONE;
}


CJellyApplicationError cjelly_application_init(
    CJellyApplication * app, const char * appName, uint32_t appVersion) {

  CJellyApplicationError err = CJELLY_APPLICATION_ERROR_NONE;

  if (!app) {
    return CJELLY_APPLICATION_ERROR_INVALID_OPTIONS;
  }

  // Check if the application has already been initialized.
  if ((app->instance != VK_NULL_HANDLE) ||
      (app->physicalDevice != VK_NULL_HANDLE) ||
      (app->commandPool != VK_NULL_HANDLE)) {
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
    if (!add_extension_internal(
            &app->options, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      fprintf(stderr, "Failed to add debug extension.\n");
      return CJELLY_APPLICATION_ERROR_OUT_OF_MEMORY;
    }
  }

  // Copy the application name to the app structure.
  if (app->appName) {
    free(app->appName);
  }
  app->appName = strdup(appName);
  if (!app->appName) {
    fprintf(stderr, "Failed to copy application name.\n");
    goto ERROR_RETURN;
  }

  // Assemble the information needed to create the Vulkan instance.
  assert(app->instance == VK_NULL_HANDLE);
  VkApplicationInfo appInfo = {0};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = appName;
  appInfo.applicationVersion = appVersion;
  appInfo.pEngineName = CJELLY_ENGINE_NAME;
  appInfo.engineVersion = CJELLY_VERSION_UINT32;
  appInfo.apiVersion = app->options.requiredVulkanVersion;

  VkInstanceCreateInfo instanceCreateInfo = {0};
  instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCreateInfo.pApplicationInfo = &appInfo;

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
    fprintf(stderr,
        "Failed to create temporary Vulkan instance for device enumeration.\n");
    err = CJELLY_APPLICATION_ERROR_INIT_FAILED;
    goto ERROR_RETURN;
  }

  // Create a temporary, headless surface for device enumeration.
  VkHeadlessSurfaceCreateInfoEXT headlessCreateInfo = {0};
  headlessCreateInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
  headlessCreateInfo.flags = 0;
  headlessCreateInfo.pNext = NULL;

  PFN_vkCreateHeadlessSurfaceEXT pfnCreateHeadlessSurface =
      (PFN_vkCreateHeadlessSurfaceEXT)vkGetInstanceProcAddr(
          app->instance, "vkCreateHeadlessSurfaceEXT");
  if (!pfnCreateHeadlessSurface) {
    fprintf(stderr, "Failed to load vkCreateHeadlessSurfaceEXT.\n");
    err = CJELLY_APPLICATION_ERROR_INIT_FAILED;
    goto ERROR_RETURN;
  }
  res = pfnCreateHeadlessSurface(
      app->instance, &headlessCreateInfo, NULL, &app->headlessSurface);
  if (res != VK_SUCCESS || app->headlessSurface == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to create headless surface.\n");
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

    // Check if the device supports graphics, compute, and presentation
    // capabilities.
    for (uint32_t j = 0; j < queueFamilyCount; ++j) {
      VkQueueFlags flags = queueFamilies[j].queueFlags;
      if (flags & VK_QUEUE_GRAPHICS_BIT) {
        graphicsFound = true;
      }
      // if (flags & VK_QUEUE_COMPUTE_BIT) {
      //   computeFound = true;
      // }
      VkBool32 presentSupport = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(
          physicalDevice, j, app->headlessSurface, &presentSupport);
      if (presentSupport == VK_TRUE) {
        presentFound = true;
      }
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
    printf("Available device extensions:\n");
    for (uint32_t j = 0; j < availableExtensionCount; ++j) {
      printf("  %s\n", availableExtensions[j].extensionName);
    }

    // For each required extension, check if it is present in the device's list.
    bool extensionsSupported = true;
    for (size_t e = 0; e < app->options.requiredExtensionCount; ++e) {
      const char * reqExt = app->options.requiredExtensions[e];
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

  // The `physicalDevices` array is no longer needed.
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

  // Free the application name if it was allocated.
  if (app->appName) {
    free(app->appName);
    app->appName = NULL;
  }

  // Destroy the debug messenger if it was created.
  if (app->debugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(app->instance, app->debugMessenger, NULL);
    app->debugMessenger = VK_NULL_HANDLE;
  }

  // Free the headless surface.
  if (app->headlessSurface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(app->instance, app->headlessSurface, NULL);
    app->headlessSurface = VK_NULL_HANDLE;
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


void cjelly_application_destroy(CJellyApplication * app) {
  if (!app)
    return;

  // Destroy the debug messenger if it was created.
  if (app->debugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(app->instance, app->debugMessenger, NULL);
    app->debugMessenger = VK_NULL_HANDLE;
  }

  // Free the headless surface.
  if (app->headlessSurface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(app->instance, app->headlessSurface, NULL);
  }

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


CJellyApplicationError cjelly_application_create_logical_device(
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

    // For graphics: require VK_QUEUE_GRAPHICS_BIT and presentation support.
    if (graphicsFamily < 0 && (flags & VK_QUEUE_GRAPHICS_BIT)) {
      VkBool32 presentSupport = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(
          app->physicalDevice, i, app->headlessSurface, &presentSupport);
      if (presentSupport == VK_TRUE) {
        graphicsFamily = i;
      }
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
  VkDeviceQueueCreateInfo queueCreateInfos[3];
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

  VkDeviceCreateInfo deviceCreateInfo = {0};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount = queueCreateInfoCount;
  deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;

  // Enable the required extensions specified in the application options.
  deviceCreateInfo.enabledExtensionCount = app->options.requiredExtensionCount;
  deviceCreateInfo.ppEnabledExtensionNames = app->options.requiredExtensions;

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

  return CJELLY_APPLICATION_ERROR_NONE;
}
