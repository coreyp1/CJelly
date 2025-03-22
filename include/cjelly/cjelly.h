/**
 * @file cjelly.h
 * @brief CJelly: A Simple Vulkan GUI Framework.
 *
 * @details
 * This file provides a simple framework for creating Vulkan applications.
 * It abstracts away the platform-specific code for creating windows, handling
 * events, and managing Vulkan objects, allowing developers to focus on
 * application-specific rendering and logic. The framework provides a clean
 * interface for initializing Vulkan, managing swap chains, and synchronizing
 * rendering operations across multiple windows.
 *
 * Key features include:
 * - Platform-independent window creation (supporting Win32 and Xlib).
 * - Initialization and management of Vulkan instances, devices, and resources.
 * - Utility functions for shader module creation and memory management.
 * - Support for validation layers and debug messaging.
 *
 * @author
 * Ghoti.io
 *
 * @date
 * 2025
 *
 * @copyright
 * Copyright (C) 2025 Ghoti.io
 */

#ifndef CJELLY_CJELLY_H
#define CJELLY_CJELLY_H

#ifdef _WIN32

  #define VK_USE_PLATFORM_WIN32_KHR
  #include <windows.h>

#else

  #define VK_USE_PLATFORM_XLIB_KHR
  #include <X11/Xatom.h>
  #include <X11/Xlib.h>

#endif

#include <stddef.h>    // For size_t and offsetof
#include <vulkan/vulkan.h>


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#ifdef _WIN32
/**
 * @brief OS-specific window handle (Windows).
 *
 * This handle represents the main window used for Vulkan rendering on Windows.
 */
extern HWND window;

/**
 * @brief Handle to the current application instance (Windows).
 *
 * This variable holds the instance handle for the current application,
 * required for various Win32 API calls.
 */
extern HINSTANCE hInstance;

#else
/**
 * @brief OS-specific display connection (Linux/Xlib).
 *
 * This pointer represents the connection to the X server on Linux,
 * used for creating and managing windows.
 */
extern Display * display;

/**
 * @brief OS-specific window handle (Linux/Xlib).
 *
 * This handle represents the main window used for Vulkan rendering on Linux.
 */
extern Window window;

#endif

/**
 * @brief Vulkan instance.
 *
 * This global variable holds the Vulkan instance used to initialize and manage
 * Vulkan resources across all windows.
 */
extern VkInstance instance;

/**
 * @brief Physical device (GPU) used for Vulkan rendering.
 *
 * This variable holds the selected GPU that supports Vulkan operations.
 */
extern VkPhysicalDevice physicalDevice;

/**
 * @brief Vulkan logical device.
 *
 * The logical device is created from the physical device and is used to manage
 * memory, queues, and resources in Vulkan.
 */
extern VkDevice device;

/**
 * @brief Graphics queue.
 *
 * This queue is used for submitting graphics commands to the GPU.
 */
extern VkQueue graphicsQueue;

/**
 * @brief Present queue.
 *
 * This queue is used to handle presentation of rendered images to the display.
 */
extern VkQueue presentQueue;

/**
 * @brief Vulkan render pass.
 *
 * The render pass defines the set of framebuffer attachments, how they are used,
 * and the operations performed on them during rendering.
 */
extern VkRenderPass renderPass;

/**
 * @brief Pipeline layout.
 *
 * This variable defines the interface between shader stages and shader resources,
 * such as descriptor sets and push constants.
 */
extern VkPipelineLayout pipelineLayout;

/**
 * @brief Graphics pipeline.
 *
 * The graphics pipeline encapsulates all the state required for rendering, including
 * shaders, fixed-function state, and dynamic states.
 */
extern VkPipeline graphicsPipeline;

/**
 * @brief Command pool.
 *
 * The command pool is used to allocate command buffers for recording rendering commands.
 */
extern VkCommandPool commandPool;

/**
 * @brief Vertex buffer.
 *
 * This buffer holds vertex data for rendering primitives.
 */
extern VkBuffer vertexBuffer;

/**
 * @brief Device memory for the vertex buffer.
 *
 * This memory is allocated and bound to the vertex buffer to store vertex data.
 */
extern VkDeviceMemory vertexBufferMemory;

/**
 * @brief Global flag indicating whether the application should close.
 *
 * When set to a non-zero value, the main loop will terminate.
 */
extern int shouldClose;

/**
 * @brief Global flag to enable Vulkan validation layers.
 *
 * This flag controls whether validation layers are enabled for debugging Vulkan issues.
 */
extern int enableValidationLayers;

/**
 * @brief Vulkan debug messenger handle.
 *
 * This handle is used to receive and process debug messages from Vulkan's validation layers.
 */
extern VkDebugUtilsMessengerEXT debugMessenger;

// Global constants for window dimensions.
const int WIDTH = 800;
const int HEIGHT = 600;

typedef struct CJellyWindow CJellyWindow;

/**
 * @brief Callback type for per-window rendering.
 *
 * This function should record commands into the command buffers for the given window,
 * drawing whatever content is appropriate.
 *
 * @param win Pointer to the CJellyWindow that is being rendered.
 */
typedef void (*CJellyRenderCallback)(CJellyWindow *win);

/**
 * @brief Specifies the update mode for a CJelly window.
 *
 * This enumeration defines the different strategies for updating the window's content.
 * Depending on the chosen mode, the window can redraw continuously synchronized to VSync,
 * at a fixed frame rate, or only when an event indicates that a redraw is necessary.
 */
typedef enum {
  CJELLY_UPDATE_MODE_VSYNC,       /**< Redraw is synchronized with the display's refresh rate via VSync. */
  CJELLY_UPDATE_MODE_FIXED,       /**< Redraw at a fixed frame rate specified by the fixedFramerate field. */
  CJELLY_UPDATE_MODE_EVENT_DRIVEN /**< Redraw only when explicitly marked as needing an update. */
} CJellyUpdateMode;

/**
 * @brief Specifies the update mode for a CJelly window.
 *
 * This enumeration defines the different strategies for updating the window's content.
 * Depending on the chosen mode, the window can redraw continuously synchronized to VSync,
 * at a fixed frame rate, or only when an event indicates that a redraw is necessary.
 */
typedef enum {
  CJELLY_UPDATE_MODE_VSYNC,       /**< Redraw is synchronized with the display's refresh rate via VSync. */
  CJELLY_UPDATE_MODE_FIXED,       /**< Redraw at a fixed frame rate specified by the fixedFramerate field. */
  CJELLY_UPDATE_MODE_EVENT_DRIVEN /**< Redraw only when explicitly marked as needing an update. */
} CJellyUpdateMode;

/**
 * @brief Represents a window and its associated Vulkan resources in the CJelly framework.
 *
 * The CJellyWindow struct encapsulates both the OS-specific window handle and all the Vulkan
 * objects required for rendering within that window. This includes the Vulkan surface, swapchain,
 * image views, framebuffers, command buffers, and synchronization primitives.
 *
 * Additional fields allow each window to specify its update strategy, including whether it should
 * redraw continuously or only when necessary.
 *
 * @note On Windows, the window handle is an HWND, while on Linux it is an Xlib Window.
 *
 * @struct CJellyWindow
 *
 * @var CJellyWindow::handle
 *  OS-specific window handle. (HWND on Windows, Window on Linux.)
 *
 * @var CJellyWindow::surface
 *  The Vulkan surface associated with this window.
 *
 * @var CJellyWindow::swapChain
 *  The Vulkan swapchain used for presenting rendered images.
 *
 * @var CJellyWindow::swapChainImageCount
 *  The number of images available in the swapchain.
 *
 * @var CJellyWindow::swapChainImages
 *  An array of Vulkan images that are part of the swapchain.
 *
 * @var CJellyWindow::swapChainImageViews
 *  An array of Vulkan image views corresponding to the swapchain images.
 *
 * @var CJellyWindow::swapChainFramebuffers
 *  An array of framebuffers used for rendering, each corresponding to a swapchain image view.
 *
 * @var CJellyWindow::commandBuffers
 *  An array of command buffers allocated for recording rendering commands for this window.
 *
 * @var CJellyWindow::imageAvailableSemaphore
 *  A semaphore used to signal that a swapchain image is available for rendering.
 *
 * @var CJellyWindow::renderFinishedSemaphore
 *  A semaphore used to signal that rendering has completed.
 *
 * @var CJellyWindow::inFlightFence
 *  A fence used to synchronize command buffer submission and rendering completion.
 *
 * @var CJellyWindow::swapChainExtent
 *  The dimensions (width and height) of the swapchain images.
 *
 * @var CJellyWindow::width
 *  The width of the window in pixels.
 *
 * @var CJellyWindow::height
 *  The height of the window in pixels.
 *
 * @var CJellyWindow::updateMode
 *   Specifies how frequently the window's content is updated (e.g., VSync, fixed, or event-driven).
 *
 * @var CJellyWindow::fixedFramerate
 *   The target frame rate (in frames per second) when using fixed update mode.
 *
 * @var CJellyWindow::needsRedraw
 *   A flag that indicates whether a redraw is needed when using event-driven updates.
 *
 * @var CJellyWindow::nextFrameTime
 *  The timestamp (in milliseconds) when the next frame should be rendered (for fixed update mode).
 *
 * @var CJellyWindow::renderCallback
 *   Function pointer for the custom rendering callback for this window.
 */
typedef struct CJellyWindow {
#ifdef _WIN32
  HWND handle;            /**< OS-specific window handle (HWND for Windows) */
#else
  Window handle;          /**< OS-specific window handle (Xlib Window for Linux) */
#endif
  VkSurfaceKHR surface;   /**< Vulkan surface associated with the window */
  VkSwapchainKHR swapChain;               /**< Vulkan swapchain for image presentation */
  uint32_t swapChainImageCount;           /**< Number of images in the swapchain */
  VkImage* swapChainImages;               /**< Array of Vulkan images from the swapchain */
  VkImageView* swapChainImageViews;       /**< Array of image views corresponding to swapchain images */
  VkFramebuffer* swapChainFramebuffers;   /**< Array of framebuffers for rendering */
  VkCommandBuffer* commandBuffers;        /**< Array of command buffers allocated for the window */
  VkSemaphore imageAvailableSemaphore;    /**< Semaphore signaling image availability */
  VkSemaphore renderFinishedSemaphore;    /**< Semaphore signaling that rendering is finished */
  VkFence inFlightFence;                  /**< Fence used for synchronizing frame submissions */
  VkExtent2D swapChainExtent;             /**< Dimensions of the swapchain images */
  int width;                              /**< Window width in pixels */
  int height;                             /**< Window height in pixels */
  CJellyUpdateMode updateMode;            /**< Update mode for this window (e.g., VSync, fixed, or event-driven) */
  uint32_t fixedFramerate;                /**< Target frame rate (FPS) when in fixed update mode */
  int needsRedraw;                        /**< Flag indicating a redraw is needed in event-driven mode */
  uint64_t nextFrameTime;                 /**< Timestamp (in milliseconds) when the next frame should be rendered (for fixed mode) */
  CJellyRenderCallback renderCallback;    /**< Custom render function for this window */
} CJellyWindow;


/* === UTILITY FUNCTIONS === */

/**
 * @brief Creates a Vulkan shader module from SPIR-V code in memory.
 *
 * This function creates a shader module using the provided Vulkan device and SPIR-V bytecode.
 *
 * @param device The Vulkan logical device.
 * @param code Pointer to the SPIR-V bytecode.
 * @param codeSize The size of the SPIR-V code in bytes.
 * @return VkShaderModule The created shader module, or VK_NULL_HANDLE on failure.
 */
VkShaderModule createShaderModuleFromMemory(VkDevice device, const unsigned char * code, size_t codeSize);

/**
 * @brief Finds a suitable memory type based on a type filter and desired memory properties.
 *
 * This function queries the physical device's memory properties and iterates through the available
 * memory types to find one that matches both the provided type filter and the required property flags.
 * If no suitable memory type is found, the function prints an error message and terminates the program.
 *
 * @param typeFilter A bitmask specifying the acceptable memory types.
 * @param properties The desired memory property flags (e.g., VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT).
 * @return uint32_t The index of the memory type that meets the criteria.
 *
 * @note This function will call exit() if no matching memory type is found.
 */
uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

/**
 * @brief Debug callback function for Vulkan validation layers.
 *
 * This callback is invoked by the Vulkan validation layers to report messages.
 *
 * @param messageSeverity The severity level of the message.
 * @param messageTypes The type of the message.
 * @param pCallbackData Pointer to detailed callback data.
 * @param pUserData Optional pointer to user data.
 * @return VkBool32 Returns VK_FALSE to indicate that Vulkan should not abort the call.
 */
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                               VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                               const VkDebugUtilsMessengerCallbackDataEXT * pCallbackData,
                                               void * pUserData);

/**
 * @brief Loads and calls vkCreateDebugUtilsMessengerEXT to create a debug messenger.
 *
 * This function retrieves the extension function to create a debug messenger and uses it.
 *
 * @param instance The Vulkan instance.
 * @param pCreateInfo Pointer to the debug messenger creation info.
 * @param pAllocator Optional allocation callbacks.
 * @param pDebugMessenger Pointer to store the created debug messenger handle.
 * @return VkResult VK_SUCCESS on success, or an error code on failure.
 */
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
                                      const VkDebugUtilsMessengerCreateInfoEXT * pCreateInfo,
                                      const VkAllocationCallbacks * pAllocator,
                                      VkDebugUtilsMessengerEXT * pDebugMessenger);

/**
 * @brief Loads and calls vkDestroyDebugUtilsMessengerEXT to destroy a debug messenger.
 *
 * This function retrieves the extension function to destroy a debug messenger and uses it.
 *
 * @param instance The Vulkan instance.
 * @param debugMessenger The debug messenger to destroy.
 * @param pAllocator Optional allocation callbacks.
 */
void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks * pAllocator);

/* === PLATFORM-SPECIFIC WINDOW CREATION === */

#ifdef _WIN32
/**
 * @brief Windows window procedure callback.
 *
 * This callback processes messages for the window.
 *
 * @param hwnd The window handle.
 * @param uMsg The message identifier.
 * @param wParam Additional message information.
 * @param lParam Additional message information.
 * @return LRESULT The result of message processing.
 */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif

/**
 * @brief Creates a platform-specific window and initializes a CJellyWindow structure.
 *
 * This function creates a window with the specified title and dimensions.
 *
 * @param win Pointer to a CJellyWindow structure to initialize.
 * @param title The title of the window.
 * @param width The width of the window in pixels.
 * @param height The height of the window in pixels.
 */
void createPlatformWindow(CJellyWindow * win, const char * title, int width, int height);


/* === EVENT PROCESSING (PLATFORM-SPECIFIC) === */

/**
 * @brief Processes OS-specific window events.
 *
 * This function processes pending events for the underlying window system.
 */
void processWindowEvents(void);

/* === PER-WINDOW VULKAN OBJECTS === */

/**
 * @brief Creates a Vulkan surface for the specified window.
 *
 * This function creates a platform-specific Vulkan surface using the window's handle.
 *
 * @param win Pointer to the CJellyWindow structure.
 */
void createSurfaceForWindow(CJellyWindow * win);

/**
 * @brief Creates the swap chain for the specified window.
 *
 * This function queries the surface capabilities and creates a swap chain for the window.
 *
 * @param win Pointer to the CJellyWindow structure.
 */
void createSwapChainForWindow(CJellyWindow * win);

/**
 * @brief Creates image views for the swap chain images of the specified window.
 *
 * This function retrieves the swap chain images and creates image views for them.
 *
 * @param win Pointer to the CJellyWindow structure.
 */
void createImageViewsForWindow(CJellyWindow * win);

/**
 * @brief Creates framebuffers for the specified window.
 *
 * This function creates framebuffers from the swap chain image views for rendering.
 *
 * @param win Pointer to the CJellyWindow structure.
 */
void createFramebuffersForWindow(CJellyWindow * win);

/**
 * @brief Allocates and records command buffers for the specified window.
 *
 * This function allocates command buffers from the global command pool and records
 * commands for rendering a frame for the window.
 *
 * @param win Pointer to the CJellyWindow structure.
 */
void createCommandBuffersForWindow(CJellyWindow * win);

/**
 * @brief Creates synchronization objects for the specified window.
 *
 * This function creates semaphores and a fence to synchronize rendering operations.
 *
 * @param win Pointer to the CJellyWindow structure.
 */
void createSyncObjectsForWindow(CJellyWindow * win);

/**
 * @brief Renders a frame for the specified window.
 *
 * This function submits the recorded command buffer for rendering and presents the image.
 *
 * @param win Pointer to the CJellyWindow structure.
 */
void drawFrameForWindow(CJellyWindow * win);

/**
 * @brief Cleans up and destroys per-window Vulkan and OS resources.
 *
 * This function destroys swap chain, image views, framebuffers, command buffers,
 * and other resources associated with the window.
 *
 * @param win Pointer to the CJellyWindow structure.
 */
void cleanupWindow(CJellyWindow * win);


/* === GLOBAL VULKAN INITIALIZATION & CLEANUP === */

/**
 * @brief Creates the Vulkan instance.
 *
 * This function creates a Vulkan instance with the required extensions and,
 * optionally, validation layers.
 */
void createInstance(void);

/**
 * @brief Creates the Vulkan debug messenger.
 *
 * This function sets up the debug messenger for validation layers.
 */
void createDebugMessenger(void);

/**
 * @brief Destroys the Vulkan debug messenger.
 *
 * This function destroys the debug messenger if validation layers are enabled.
 */
void destroyDebugMessenger(void);

/**
 * @brief Selects a suitable physical device (GPU) for Vulkan.
 *
 * This function enumerates available physical devices and selects one that supports Vulkan.
 */
void pickPhysicalDevice(void);

/**
 * @brief Creates a logical device and retrieves the graphics and present queues.
 *
 * This function creates a logical device for the selected physical device and obtains
 * handles to the graphics and present queues.
 */
void createLogicalDevice(void);

/**
 * @brief Creates a vertex buffer and uploads vertex data.
 *
 * This function creates a vertex buffer for drawing a square and uploads the vertex data.
 */
void createVertexBuffer(void);

/**
 * @brief Creates the render pass used for rendering.
 *
 * This function sets up a render pass with a single color attachment.
 */
void createRenderPass(void);

/**
 * @brief Creates the graphics pipeline.
 *
 * This function loads shader modules, configures both fixed and dynamic states,
 * and creates the graphics pipeline for rendering.
 */
void createGraphicsPipeline(void);

/**
 * @brief Creates the command pool for allocating command buffers.
 *
 * This function creates a command pool from which command buffers are allocated.
 */
void createCommandPool(void);

/**
 * @brief Initializes global Vulkan resources.
 *
 * This function creates the Vulkan instance, selects a physical device, creates a logical device,
 * and initializes global resources such as the vertex buffer, render pass, graphics pipeline, and command pool.
 */
void initVulkanGlobal(void);

/**
 * @brief Cleans up global Vulkan resources.
 *
 * This function destroys the graphics pipeline, render pass, vertex buffer,
 * command pool, and other global Vulkan objects.
 */
void cleanupVulkanGlobal(void);


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CJELLY_CJELLY_H
