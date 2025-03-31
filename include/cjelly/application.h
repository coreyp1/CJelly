/**
 * @file application.h
 * @brief CJelly Application API.
 *
 * @details
 * This header defines the CJelly Application API, which provides a higher-level
 * interface for configuring and initializing a Vulkan context. The application
 * object encapsulates both required and preferred constraints for the Vulkan
 * instance and device. For each constraint (e.g., Vulkan API version, GPU
 * memory, and device type), a single function is provided that accepts the
 * constraint value along with a boolean indicating whether it is required. In
 * addition, functions are available to add required layers (e.g.,
 * VK_EXT_descriptor_indexing).
 *
 * Author: Ghoti.io
 * Date: 2025
 * Copyright (C) 2025 Ghoti.io
 */

#ifndef CJELLY_APPLICATION_H
#define CJELLY_APPLICATION_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#include <cjelly/types.h>

// Include platform-specific headers for Vulkan and window management first.
#ifdef _WIN32

#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>

#else

#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#endif

// Include all other headers.
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>


/**
 * @brief Enum representing possible error codes for CJelly Application
 * operations.
 *
 * @var CJELLY_APPLICATION_ERROR_NONE
 *  Operation completed successfully.
 *
 * @var CJELLY_APPLICATION_ERROR_INIT_FAILED
 *  Initialization of the application failed.
 *
 * @var CJELLY_APPLICATION_ERROR_INVALID_OPTIONS
 *  One or more of the provided options are invalid.
 */
typedef enum CJellyApplicationError {
  CJELLY_APPLICATION_ERROR_NONE = 0,
  CJELLY_APPLICATION_ERROR_OUT_OF_MEMORY,
  CJELLY_APPLICATION_ERROR_INIT_FAILED,
  CJELLY_APPLICATION_ERROR_INVALID_OPTIONS,
} CJellyApplicationError;


/**
 * @brief Internal structure to store application configuration options.
 *
 * This structure holds both required and preferred constraints for Vulkan
 * context initialization. These include flags for validation, the required
 * Vulkan API version, GPU memory, and device type, along with separate lists
 * for required and preferred layers/extensions and other options.
 *
 * If a required option is not met, the application will fail to initialize.
 *
 * If a preferred option is not met, the application will still initialize.
 * Given a choice between multiple devices, the application will prefer the one
 * that meets the stated preferred options.
 *
 * @var enableValidation
 *  Indicates whether Vulkan validation layers are enabled.
 *
 * @var requiredVulkanVersion
 *  The Vulkan API version that is required or preferred.
 *
 * @var requiredGPUMemory
 *  The minimum amount of GPU memory that is required, in megabytes.
 *
 * @var requiredDeviceType
 *  The required device type (e.g., discrete or integrated).
 *
 * @var preferredDeviceType
 *  The preferred device type (e.g., discrete or integrated).
 *
 * @var requiredExtensions
 *  A dynamic array of names of layers/extensions that are required.
 *
 * @var requiredExtensionCount
 *  The current number of required layers/extensions.
 *
 * @var requiredExtensionCapacity
 *  The allocated capacity for the required layers array.
 */
typedef struct CJellyApplicationOptions {
  uint32_t requiredVulkanVersion;
  uint32_t requiredGPUMemory;
  const char ** requiredExtensions;
  size_t requiredExtensionCount;
  size_t requiredExtensionCapacity;
  CJellyApplicationDeviceType requiredDeviceType;
  CJellyApplicationDeviceType preferredDeviceType;
  bool enableValidation;
} CJellyApplicationOptions;


/**
 * @brief Internal representation of the CJelly Application.
 *
 * This structure encapsulates the state of the CJelly application, including
 * its configuration options, the associated Vulkan context, and a Vulkan
 * command pool for global command buffer allocation.
 *
 * @var appName
 *  The name of the application.
 *
 * @var appVersion
 *  The version of the application.
 *
 * @var options
 *  The internal configuration options used during application initialization.
 *
 * @var instance
 *  The Vulkan instance handle.
 *
 * @var physicalDevice
 *  The selected physical device handle.
 *
 * @var commandPool
 *  A Vulkan command pool used for allocating command buffers globally.
 *
 * @var vkContext
 *  The Vulkan context associated with the application.
 *
 * @var debugMessenger
 *  The Vulkan debug messenger handle, used for validation layers.
 */
struct CJellyApplication {
  char * appName;
  uint32_t appVersion;
  CJellyApplicationOptions options;
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkCommandPool commandPool;
  CJellyVulkanContext * vkContext;
  VkDebugUtilsMessengerEXT debugMessenger;
};


/**
 * @brief Create the CJelly application object with default options.
 *
 * @param[out] app A pointer to a pointer that will hold the allocated
 * application object.
 * @param appName The name of the application.
 * @param appVersion The version of the application.
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success, or
 * an error code on failure.
 */
CJellyApplicationError cjelly_application_create(
    CJellyApplication ** app, const char * appName, uint32_t appVersion);


/**
 * @brief Set the Vulkan API version constraint.
 *
 * If the version is lower than the currently set version, it will be ignored.
 *
 * @param app A pointer to the application object.
 * @param version The Vulkan API version (e.g., VK_API_VERSION_1_1).
 */
void cjelly_application_set_required_vulkan_version(
    CJellyApplication * app, uint32_t version);


/**
 * @brief Set the GPU memory constraint.
 *
 * When choosing a Vulkan device, the application will not select a device with
 * less than the required amount of GPU memory.
 *
 * If the amount is lower than the currently set value, it will be ignored.
 *
 * @param app A pointer to the application object.
 * @param memory The specified GPU memory in megabytes.
 */
void cjelly_application_set_required_gpu_memory(
    CJellyApplication * app, uint32_t memory);


/**
 * @brief Set the device type constraint.
 *
 * When choosing a Vulkan device, the application will not select a device
 * that does not match the specified required type.  If multiple devices are
 * available, then the preferred type will be taken into account.
 *
 * @param app A pointer to the application object.
 * @param type The specified device type (discrete or integrated).
 * @param required Set to true if this type is required; false if it is only
 * preferred.
 */
void cjelly_application_set_device_type(
    CJellyApplication * app, CJellyApplicationDeviceType type, bool required);


/**
 * @brief Add an extension requirement to the physical device selection.
 *
 * This function adds an extension requirement to the application. The
 * application will not select a Vulkan device that does not support the
 * required extensions.
 *
 * @param app A pointer to the application object.
 * @param extension The name of the required extension (e.g.,
 *   "VK_EXT_descriptor_indexing").
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success, or
 *   an error code on failure.
 */
CJellyApplicationError cjelly_application_add_extension(
    CJellyApplication * app, const char * extension);


/**
 * @brief Initialize the CJelly application.
 *
 * This function initializes the application by checking constraints, selecting
 * a Vulkan device that meets the required criteria, and performing additional
 * setup (e.g., command pool creation).
 *
 * If multiple devices are available, the application will select the one that
 * best meets the stated preferred options.  If no device is found that meets
 * the required options, the function will return an error.
 *
 * @param app A pointer to the application object.
 * @param appName The name of the application.
 * @param appVersion The version of the application.
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success, or
 * an error code on failure.
 */
CJellyApplicationError cjelly_application_init(
    CJellyApplication * app, const char * appName, uint32_t appVersion);


/**
 * @brief Destroy the CJelly application and free associated resources.
 *
 * @param app A pointer to the application object to destroy.
 */
void cjelly_application_destroy(CJellyApplication * app);


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CJELLY_APPLICATION_H
