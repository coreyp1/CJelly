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
 * for required instance extensions and required device extensions.
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
 * @var requiredInstanceExtensions
 *  A dynamic array of names of instance extensions that are required.
 *
 * @var requiredInstanceExtensionCount
 *  The current number of required instance extensions.
 *
 * @var requiredInstanceExtensionCapacity
 *  The allocated capacity for the required instance extensions array.
 *
 * @var requiredDeviceExtensions
 *  A dynamic array of names of device extensions that are required.
 *
 * @var requiredDeviceExtensionCount
 *  The current number of required device extensions.
 *
 * @var requiredDeviceExtensionCapacity
 *  The allocated capacity for the required device extensions array.
 */
typedef struct CJellyApplicationOptions {
  uint32_t requiredVulkanVersion;
  uint32_t requiredGPUMemory;
  CJellyApplicationDeviceType requiredDeviceType;
  CJellyApplicationDeviceType preferredDeviceType;
  bool enableValidation;

  // Instance extensions (enabled during vkCreateInstance)
  const char ** requiredInstanceExtensions;
  size_t requiredInstanceExtensionCount;
  size_t requiredInstanceExtensionCapacity;

  // Device extensions (enabled during vkCreateDevice)
  const char ** requiredDeviceExtensions;
  size_t requiredDeviceExtensionCount;
  size_t requiredDeviceExtensionCapacity;
} CJellyApplicationOptions;


/**
 * @brief Internal representation of the CJelly Application.
 *
 * This structure encapsulates the state of the CJelly application, including
 * its configuration options, the associated Vulkan instance, physical device,
 * logical device, command pools, debug messenger, and individual queue handles
 * for graphics, transfer, and compute operations.
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
 * @var logicalDevice
 *  The Vulkan logical device handle created from the selected physical device.
 *
 * @var graphicsCommandPool
 *  A Vulkan command pool used for allocating command buffers for graphics
 * operations.
 *
 * @var transferCommandPool
 *  A Vulkan command pool used for allocating command buffers for transfer
 * operations.
 *
 * @var computeCommandPool
 *  A Vulkan command pool used for allocating command buffers for compute
 * operations.
 *
 * @var vkContext
 *  The Vulkan context associated with the application.
 *
 * @var debugMessenger
 *  The Vulkan debug messenger handle, used for validation layers.
 *
 * @var graphicsQueue
 *  The Vulkan queue handle used for graphics (and presentation) operations.
 *
 * @var transferQueue
 *  The Vulkan queue handle used for transfer (data copy) operations.
 *
 * @var computeQueue
 *  The Vulkan queue handle used for compute operations.
 *
 * @var graphicsQueueFamilyIndex
 *  The queue family index used for graphics operations.
 *
 * @var transferQueueFamilyIndex
 *  The queue family index used for transfer operations.
 *
 * @var computeQueueFamilyIndex
 *  The queue family index used for compute operations.
 */
struct CJellyApplication {
  char * appName;
  uint32_t appVersion;
  CJellyApplicationOptions options;
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice logicalDevice;
  VkCommandPool graphicsCommandPool;
  VkCommandPool transferCommandPool;
  VkCommandPool computeCommandPool;
  CJellyVulkanContext * vkContext;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkQueue graphicsQueue;
  VkQueue transferQueue;
  VkQueue computeQueue;
  int graphicsQueueFamilyIndex;
  int transferQueueFamilyIndex;
  int computeQueueFamilyIndex;
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
 * @brief Add a required instance extension to the application.
 *
 * This function adds a required instance extension for the Vulkan instance
 * creation. The application will fail to initialize if the instance extension
 * is not available.
 *
 * @param app A pointer to the application object.
 * @param extension The name of the required instance extension (e.g.,
 * "VK_EXT_debug_utils").
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success, or
 * an error code on failure.
 */
CJellyApplicationError cjelly_application_add_instance_extension(
    CJellyApplication * app, const char * extension);


/**
 * @brief Add a required device extension to the application.
 *
 * This function adds a required device extension for the Vulkan logical device
 * creation. The application will not select a Vulkan device that does not
 * support the required device extension.
 *
 * @param app A pointer to the application object.
 * @param extension The name of the required device extension (e.g.,
 * "VK_EXT_descriptor_indexing").
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success, or
 * an error code on failure.
 */
CJellyApplicationError cjelly_application_add_device_extension(
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
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success, or
 * an error code on failure.
 */
CJellyApplicationError cjelly_application_init(CJellyApplication * app);


/**
 * @brief Destroy the CJelly application and free associated resources.
 *
 * @param app A pointer to the application object to destroy.
 */
void cjelly_application_destroy(CJellyApplication * app);


/**
 * @brief Create the Vulkan logical device and retrieve queue handles.
 *
 * This function queries the physical device for available queue families,
 * selects appropriate families for graphics, transfer, and compute operations,
 * and creates a logical device. It then retrieves and stores queue handles
 * for each type in the CJellyApplication structure.
 *
 * If dedicated queues are not available, the function may assign the same queue
 * handle to more than one purpose.
 *
 * @param app A pointer to the CJellyApplication structure.
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success, or
 *   an appropriate error code on failure.
 */
CJellyApplicationError cjelly_application_create_logical_device(
    CJellyApplication * app);


/**
 * @brief Create command pools for graphics, transfer, and compute operations.
 *
 * This function creates separate Vulkan command pools for each queue type using
 * the queue family indices that were selected during logical device creation.
 * If two queue types share the same family, the same command pool handle is
 * used.
 *
 * The command pools are created with the flag
 * VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, allowing individual command
 * buffers to be reset.
 *
 * @param app A pointer to the CJellyApplication structure which must have a
 * valid logical device and valid queue family indices.
 * @return CJellyApplicationError CJELLY_APPLICATION_ERROR_NONE on success, or
 * an appropriate error code on failure.
 */
CJellyApplicationError cjelly_application_create_command_pools(
    CJellyApplication * app);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CJELLY_APPLICATION_H
