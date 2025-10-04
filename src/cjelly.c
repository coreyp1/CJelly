/**
 * @file cjelly.c
 * @brief Implementation of the CJelly Vulkan Framework.
 *
 * @details
 * This file contains the implementation of the functions declared in cjelly.h.
 * It includes platform-specific window creation, event processing, and the
 * initialization, management, and cleanup of Vulkan resources. This
 * implementation abstracts away the underlying OS-specific and Vulkan
 * boilerplate, allowing developers to focus on application-specific rendering
 * logic.
 *
 * @note
 * This file is part of the CJelly framework, developed by Ghoti.io.
 *
 * @date 2025
 * @copyright Copyright (C) 2025 Ghoti.io
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjelly/cjelly.h>
#include <cjelly/format/image.h>
#include <cjelly/macros.h>
#include <shaders/basic.frag.h>
#include <shaders/basic.vert.h>
#include <shaders/textured.frag.h>
#include <shaders/bindless.vert.h>
#include <shaders/bindless.frag.h>

// Global Vulkan objects shared among all windows.

#ifdef _WIN32
HWND window;
HINSTANCE hInstance;
#else
Display * display;
Window window;
#endif

// Global Vulkan objects shared among all windows.
VkInstance instance;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice device;
VkQueue graphicsQueue;
VkQueue presentQueue;
VkRenderPass renderPass;
VkPipelineLayout pipelineLayout;
VkPipeline graphicsPipeline;
VkCommandPool commandPool;
VkBuffer vertexBuffer;
VkDeviceMemory vertexBufferMemory;

// Global texture variables (declared in your header, defined here)
VkPipeline texturedPipeline;
VkPipelineLayout texturedPipelineLayout;
VkImage textureImage;
VkDeviceMemory textureImageMemory;
VkImageView textureImageView;
VkSampler textureSampler;
VkDescriptorPool textureDescriptorPool;
VkDescriptorSetLayout textureDescriptorSetLayout;
VkDescriptorSet textureDescriptorSet;
VkBuffer vertexBufferTextured;
VkDeviceMemory vertexBufferTexturedMemory;

// Bindless rendering variables
VkPipeline bindlessPipeline;
VkPipelineLayout bindlessPipelineLayout;
VkBuffer vertexBufferBindless;
VkDeviceMemory vertexBufferBindlessMemory;
CJellyTextureAtlas * bindlessTextureAtlas = NULL;


// Global flag to indicate that the window should close.
int shouldClose;

// Global flag to enable validation layers.
int enableValidationLayers;

// Global debug messenger handle.
VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

// Global constants for window dimensions.
const int WIDTH = 800;
const int HEIGHT = 600;


// Vertex structure for the square.
typedef struct Vertex {
  float pos[2];   // Position at location 0, a vec2.
  float color[3]; // Color at location 1, a vec3.
} Vertex;

// Vertex structure for a textured square.
typedef struct VertexTextured {
  float pos[2];      // Position (x, y)
  float texCoord[2]; // Texture coordinate (u, v)
} VertexTextured;

// Vertex structure for bindless rendering.
typedef struct VertexBindless {
  float pos[2];      // Position (x, y)
  float color[3];    // Color (r, g, b)
  uint32_t textureID; // Texture ID for bindless rendering
} VertexBindless;


// Forward declarations for helper functions (for texture loading):
void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer * buffer,
    VkDeviceMemory * bufferMemory);
void createImage(uint32_t width, uint32_t height, VkFormat format,
    VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage * image,
    VkDeviceMemory * imageMemory);
VkCommandBuffer beginSingleTimeCommands(void);
void endSingleTimeCommands(VkCommandBuffer commandBuffer);
void transitionImageLayout(VkImage image, VkFormat format,
    VkImageLayout oldLayout, VkImageLayout newLayout);
void copyBufferToImage(
    VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);


//
// === UTILITY FUNCTIONS ===
//

VkShaderModule createShaderModuleFromMemory(
    VkDevice device, const unsigned char * code, size_t codeSize) {

  VkShaderModuleCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = codeSize;
  createInfo.pCode = (const uint32_t *)code;

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create shader module from memory\n");
    return VK_NULL_HANDLE;
  }

  return shaderModule;
}


// Finds a suitable memory type based on typeFilter and desired properties.
uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      return i;
    }
  }
  fprintf(stderr, "Failed to find suitable memory type!\n");
  exit(EXIT_FAILURE);
}


// Debug callback function for validation layers.
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    GCJ_MAYBE_UNUSED(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity),
    GCJ_MAYBE_UNUSED(VkDebugUtilsMessageTypeFlagsEXT messageTypes),
    const VkDebugUtilsMessengerCallbackDataEXT * pCallbackData,
    GCJ_MAYBE_UNUSED(void * pUserData)) {

  fprintf(stderr, "Validation layer: %s\n", pCallbackData->pMessage);
  return VK_FALSE;
}


// Helper functions to load extension functions.
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
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


void DestroyDebugUtilsMessengerEXT(VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks * pAllocator) {

  PFN_vkDestroyDebugUtilsMessengerEXT func =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != NULL) {
    func(instance, debugMessenger, pAllocator);
  }
}


//
// === PLATFORM-SPECIFIC WINDOW CREATION ===
//

// Window procedure for Windows.
#ifdef _WIN32

LRESULT CALLBACK WindowProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
  case WM_CLOSE:
    shouldClose = 1;
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}

#endif


// Creates a platform-specific window and initializes the CJellyWindow
// structure.
void createPlatformWindow(
    CJellyWindow * win, const char * title, int width, int height) {
  win->width = width;
  win->height = height;

#ifdef _WIN32

  hInstance = GetModuleHandle(NULL);
  WNDCLASS wc = {0};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = "VulkanWindowClass";
  RegisterClass(&wc);
  win->handle = CreateWindowEx(0, "VulkanWindowClass", title,
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL,
      NULL, hInstance, NULL);
  ShowWindow(win->handle, SW_SHOW);

#else

  int screen = DefaultScreen(display);
  win->handle =
      XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, width,
          height, 1, BlackPixel(display, screen), WhitePixel(display, screen));
  Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XStoreName(display, win->handle, title);
  XSetWMProtocols(display, win->handle, &wmDelete, 1);
  XMapWindow(display, win->handle);

#endif

  win->needsRedraw = 1;
  win->nextFrameTime = 0;
}


//
// === EVENT PROCESSING (PLATFORM-SPECIFIC) ===
//

#ifdef _WIN32

void processWindowEvents() {
  MSG msg;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

#else

void processWindowEvents() {
  while (XPending(display)) {
    XEvent event;
    XNextEvent(display, &event);
    if (event.type == ClientMessage) {
      Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
      if ((Atom)event.xclient.data.l[0] == wmDelete) {
        shouldClose = 1;
      }
    }
  }
}

#endif


//
// === PER-WINDOW VULKAN OBJECTS ===
//

// Create a Vulkan surface for a given window.
void createSurfaceForWindow(CJellyWindow * win) {

#ifdef _WIN32

  VkWin32SurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  createInfo.hinstance = hInstance;
  createInfo.hwnd = win->handle;
  if (vkCreateWin32SurfaceKHR(instance, &createInfo, NULL, &win->surface) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create Win32 surface\n");
    exit(EXIT_FAILURE);
  }

#else

  VkXlibSurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  createInfo.dpy = display;
  createInfo.window = win->handle;
  if (vkCreateXlibSurfaceKHR(instance, &createInfo, NULL, &win->surface) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create Xlib surface\n");
    exit(EXIT_FAILURE);
  }

#endif
}


// Create the swap chain for a window.
void createSwapChainForWindow(CJellyWindow * win) {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      physicalDevice, win->surface, &capabilities);

  // Setting the swap chain extent to the window's extent.
  win->swapChainExtent = capabilities.currentExtent;

  VkSwapchainCreateInfoKHR createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = win->surface;
  createInfo.minImageCount = capabilities.minImageCount;
  createInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
  createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  createInfo.imageExtent = win->swapChainExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(device, &createInfo, NULL, &win->swapChain) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create swap chain\n");
    exit(EXIT_FAILURE);
  }
}


// Create image views for the swap chain images.
void createImageViewsForWindow(CJellyWindow * win) {
  vkGetSwapchainImagesKHR(
      device, win->swapChain, &win->swapChainImageCount, NULL);
  win->swapChainImages = malloc(sizeof(VkImage) * win->swapChainImageCount);
  vkGetSwapchainImagesKHR(
      device, win->swapChain, &win->swapChainImageCount, win->swapChainImages);

  win->swapChainImageViews =
      malloc(sizeof(VkImageView) * win->swapChainImageCount);
  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkImageViewCreateInfo viewInfo = {0};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = win->swapChainImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, NULL,
            &win->swapChainImageViews[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create image view\n");
      exit(EXIT_FAILURE);
    }
  }
}


// Create framebuffers for the window.
void createFramebuffersForWindow(CJellyWindow * win) {
  win->swapChainFramebuffers =
      malloc(sizeof(VkFramebuffer) * win->swapChainImageCount);
  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkImageView attachments[] = {win->swapChainImageViews[i]};
    VkFramebufferCreateInfo framebufferInfo = {0};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = win->swapChainExtent.width;
    framebufferInfo.height = win->swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, NULL,
            &win->swapChainFramebuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create framebuffer\n");
      exit(EXIT_FAILURE);
    }
  }
}


// Allocate and record command buffers for a window.
void createCommandBuffersForWindow(CJellyWindow * win) {
  win->commandBuffers =
      malloc(sizeof(VkCommandBuffer) * win->swapChainImageCount);
  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(device, &allocInfo, win->commandBuffers) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate command buffers\n");
    exit(EXIT_FAILURE);
  }

  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(win->commandBuffers[i], &beginInfo) !=
        VK_SUCCESS) {
      fprintf(stderr, "Failed to begin command buffer\n");
      exit(EXIT_FAILURE);
    }

    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = win->swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent = win->swapChainExtent;

    VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(
        win->commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Dynamically set the viewport using this window's swap chain extent.
    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)win->swapChainExtent.width;
    viewport.height = (float)win->swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(win->commandBuffers[i], 0, 1, &viewport);

    // Dynamically set the scissor using this window's swap chain extent.
    VkRect2D scissor = {0};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = win->swapChainExtent;
    vkCmdSetScissor(win->commandBuffers[i], 0, 1, &scissor);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(
        win->commandBuffers[i], 0, 1, &vertexBuffer, offsets);

    vkCmdBindPipeline(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphicsPipeline);
    vkCmdDraw(win->commandBuffers[i], 6, 1, 0, 0);
    vkCmdEndRenderPass(win->commandBuffers[i]);

    if (vkEndCommandBuffer(win->commandBuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to record command buffer\n");
      exit(EXIT_FAILURE);
    }
  }
}


// Create synchronization objects for a window.
void createSyncObjectsForWindow(CJellyWindow * win) {
  VkSemaphoreCreateInfo semaphoreInfo = {0};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  if (vkCreateSemaphore(device, &semaphoreInfo, NULL,
          &win->imageAvailableSemaphore) != VK_SUCCESS ||
      vkCreateSemaphore(device, &semaphoreInfo, NULL,
          &win->renderFinishedSemaphore) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create semaphores\n");
    exit(EXIT_FAILURE);
  }

  VkFenceCreateInfo fenceInfo = {0};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateFence(device, &fenceInfo, NULL, &win->inFlightFence) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create fence\n");
    exit(EXIT_FAILURE);
  }
}

//
// === DRAWING A FRAME PER WINDOW ===
//

void drawFrameForWindow(CJellyWindow * win) {
  vkWaitForFences(device, 1, &win->inFlightFence, VK_TRUE, UINT64_MAX);
  vkResetFences(device, 1, &win->inFlightFence);

  uint32_t imageIndex;
  vkAcquireNextImageKHR(device, win->swapChain, UINT64_MAX,
      win->imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

  VkSubmitInfo submitInfo = {0};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  VkSemaphore waitSemaphores[] = {win->imageAvailableSemaphore};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &win->commandBuffers[imageIndex];
  VkSemaphore signalSemaphores[] = {win->renderFinishedSemaphore};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, win->inFlightFence) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to submit draw command buffer\n");
  }

  VkPresentInfoKHR presentInfo = {0};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &win->swapChain;
  presentInfo.pImageIndices = &imageIndex;

  vkQueuePresentKHR(presentQueue, &presentInfo);
}


//
// === CLEANUP FOR A WINDOW ===
//

void cleanupWindow(CJellyWindow * win) {
  vkDestroySemaphore(device, win->renderFinishedSemaphore, NULL);
  vkDestroySemaphore(device, win->imageAvailableSemaphore, NULL);
  vkDestroyFence(device, win->inFlightFence, NULL);

  free(win->commandBuffers);

  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    vkDestroyFramebuffer(device, win->swapChainFramebuffers[i], NULL);
    vkDestroyImageView(device, win->swapChainImageViews[i], NULL);
  }

  free(win->swapChainFramebuffers);
  free(win->swapChainImageViews);
  free(win->swapChainImages);

  vkDestroySwapchainKHR(device, win->swapChain, NULL);
  vkDestroySurfaceKHR(instance, win->surface, NULL);

#ifdef _WIN32

  DestroyWindow(win->handle);

#else

  XDestroyWindow(display, win->handle);

#endif
}


//
// === VULKAN INITIALIZATION & RENDERING FUNCTIONS ===
//

void createInstance() {
  VkApplicationInfo appInfo = {0};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Vulkan Square";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "CjellyEngine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  // Specify required extensions for the platform.
  const char * extensions[10];
  uint32_t extCount = 0;
  extensions[extCount++] = "VK_KHR_surface";

#ifdef _WIN32

  extensions[extCount++] = "VK_KHR_win32_surface";

#else

  extensions[extCount++] = "VK_KHR_xlib_surface";

#endif

  if (enableValidationLayers) {
    extensions[extCount++] = "VK_EXT_debug_utils";
  }

  VkInstanceCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = extCount;
  createInfo.ppEnabledExtensionNames = extensions;

  if (enableValidationLayers) {
    const char * validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = validationLayers;

    // Set up debug messenger info so that it is used during instance creation.
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
    debugCreateInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
  }
  else {
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = NULL;
  }

  if (vkCreateInstance(&createInfo, NULL, &instance) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create Vulkan instance\n");
    exit(EXIT_FAILURE);
  }

  if (enableValidationLayers) {
    createDebugMessenger();
  }
}


void createDebugMessenger() {
  VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;

  if (CreateDebugUtilsMessengerEXT(
          instance, &createInfo, NULL, &debugMessenger) != VK_SUCCESS) {
    fprintf(stderr, "Failed to set up debug messenger!\n");
  }
}


void destroyDebugMessenger() {
  if (enableValidationLayers && debugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(instance, debugMessenger, NULL);
  }
}


void pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
  if (deviceCount == 0) {
    fprintf(stderr, "Failed to find GPUs with Vulkan support\n");
    exit(EXIT_FAILURE);
  }

  VkPhysicalDevice devices[deviceCount];
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices);

  VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
  int bestScore = -1;

  for (uint32_t i = 0; i < deviceCount; i++) {
    VkPhysicalDevice device = devices[i];

    // Retrieve device properties and features.
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    // Check for required extensions (e.g., VK_KHR_swapchain)
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);
    VkExtensionProperties availableExtensions[extensionCount];
    vkEnumerateDeviceExtensionProperties(
        device, NULL, &extensionCount, availableExtensions);

    int swapchainExtensionFound = 0;
    for (uint32_t j = 0; j < extensionCount; j++) {
      if (strcmp(availableExtensions[j].extensionName,
              VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
        swapchainExtensionFound = 1;
        break;
      }
    }
    if (!swapchainExtensionFound) {
      // Skip this device if it doesn't support swapchains.
      continue;
    }

    // Score the device:
    // Prefer discrete GPUs over integrated ones.
    int score = 0;
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      score += 1000;
    }
    // Use a property like maxImageDimension2D as a rough performance metric.
    score += deviceProperties.limits.maxImageDimension2D;

    if (score > bestScore) {
      bestScore = score;
      bestDevice = device;
    }
  }

  if (bestDevice == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to find a suitable GPU\n");
    exit(EXIT_FAILURE);
  }

  physicalDevice = bestDevice;
}


void createLogicalDevice() {
  uint32_t queueFamilyIndex =
      0; // Simplified: assume family 0 supports both graphics and present.
  float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo = {0};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  // Specify the swapchain extension.
  const char * deviceExtensions[] = {"VK_KHR_swapchain"};

  VkDeviceCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueCreateInfo;
  createInfo.enabledExtensionCount = 1;
  createInfo.ppEnabledExtensionNames = deviceExtensions;

  if (vkCreateDevice(physicalDevice, &createInfo, NULL, &device) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create logical device\n");
    exit(EXIT_FAILURE);
  }

  vkGetDeviceQueue(device, queueFamilyIndex, 0, &graphicsQueue);
  presentQueue = graphicsQueue;
}


// Creates a vertex buffer and uploads the vertex data for a square (two
// triangles).
void createVertexBuffer() {
  // Define 6 vertices to draw two triangles forming a square.
  Vertex vertices[] = {
      {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
      {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
      {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
      {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}},
      {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
  };
  VkDeviceSize bufferSize = sizeof(vertices);

  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = bufferSize;
  bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, NULL, &vertexBuffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create vertex buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(device, &allocInfo, NULL, &vertexBufferMemory) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate vertex buffer memory\n");
    exit(EXIT_FAILURE);
  }

  vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

  void * data;
  vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, vertices, (size_t)bufferSize);
  vkUnmapMemory(device, vertexBufferMemory);
}


void createRenderPass() {
  VkAttachmentDescription colorAttachment = {0};
  colorAttachment.format = VK_FORMAT_B8G8R8A8_SRGB;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef = {0};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {0};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkRenderPassCreateInfo renderPassInfo = {0};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  if (vkCreateRenderPass(device, &renderPassInfo, NULL, &renderPass) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create render pass\n");
    exit(EXIT_FAILURE);
  }
}


void createGraphicsPipeline() {
  // Load SPIR-V binaries and create shader modules.
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(device, basic_vert_spv, basic_vert_spv_len);
  VkShaderModule fragShaderModule =
      createShaderModuleFromMemory(device, basic_frag_spv, basic_frag_spv_len);

  if (vertShaderModule == VK_NULL_HANDLE ||
      fragShaderModule == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to create shader modules\n");
    exit(EXIT_FAILURE);
  }

  VkPipelineShaderStageCreateInfo shaderStages[2] = {0};

  // Vertex shader stage.
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertShaderModule;
  shaderStages[0].pName = "main";

  // Fragment shader stage.
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragShaderModule;
  shaderStages[1].pName = "main";

  // Define a binding description for our Vertex structure.
  VkVertexInputBindingDescription bindingDescription = {0};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  // Define attribute descriptions for the vertex shader inputs.
  VkVertexInputAttributeDescription attributeDescriptions[2] = {0};

  // Attribute 0: position (vec2)
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, pos);

  // Attribute 1: color (vec3)
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(Vertex, color);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = 2;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // Instead of hard-coding the viewport and scissor,
  // we define a viewport state with counts and use dynamic states to set actual
  // values later.
  VkPipelineViewportStateCreateInfo viewportState = {0};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1; // Only the count is used.
  viewportState.scissorCount = 1;

  VkDynamicState dynamicStates[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };

  VkPipelineDynamicStateCreateInfo dynamicState = {0};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkPipelineRasterizationStateCreateInfo rasterizer = {0};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo multisampling = {0};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending = {0};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  if (vkCreatePipelineLayout(
          device, &pipelineLayoutInfo, NULL, &pipelineLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create pipeline layout\n");
    exit(EXIT_FAILURE);
  }

  VkGraphicsPipelineCreateInfo pipelineInfo = {0};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
          &graphicsPipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create graphics pipeline\n");
    exit(EXIT_FAILURE);
  }

  // Clean up shader modules after pipeline creation.
  vkDestroyShaderModule(device, vertShaderModule, NULL);
  vkDestroyShaderModule(device, fragShaderModule, NULL);
}


void createCommandPool() {
  VkCommandPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = 0;
  if (vkCreateCommandPool(device, &poolInfo, NULL, &commandPool) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create command pool\n");
    exit(EXIT_FAILURE);
  }
}


//
// === TEXTURED SQUARE ===
//

/**
 * @brief Creates a descriptor pool for texture descriptor sets.
 *
 * This function creates a descriptor pool that can allocate descriptor sets
 * containing combined image samplers.
 */
void createTextureDescriptorPool() {
  VkDescriptorPoolSize poolSize = {0};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 1; // Change this if you need more descriptors.

  VkDescriptorPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1; // Adjust if allocating multiple descriptor sets.

  if (vkCreateDescriptorPool(device, &poolInfo, NULL, &textureDescriptorPool) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture descriptor pool!\n");
    exit(EXIT_FAILURE);
  }
}

void createDescriptorSetLayouts() {
  // Define a descriptor set layout for the texture.
  VkDescriptorSetLayoutBinding layoutBinding = {0};
  layoutBinding.binding = 0;
  layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutBinding.descriptorCount = 1;
  layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &layoutBinding;

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, NULL,
          &textureDescriptorSetLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture descriptor set layout\n");
    exit(EXIT_FAILURE);
  }
}

/**
 * @brief Allocates a descriptor set for the texture.
 *
 * This function allocates a descriptor set from the descriptor pool using
 * the layout defined by textureDescriptorSetLayout.
 */
void allocateTextureDescriptorSet() {
  VkDescriptorSetAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = textureDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &textureDescriptorSetLayout;

  if (vkAllocateDescriptorSets(device, &allocInfo, &textureDescriptorSet) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate texture descriptor set!\n");
    exit(EXIT_FAILURE);
  }
}

void createTexturedGraphicsPipeline() {
  // Load SPIR-V binaries and create shader modules for texturing.
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(device, basic_vert_spv, basic_vert_spv_len);
  VkShaderModule fragShaderModule = createShaderModuleFromMemory(
      device, textured_frag_spv, textured_frag_spv_len);

  if (vertShaderModule == VK_NULL_HANDLE ||
      fragShaderModule == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to create textured shader modules\n");
    exit(EXIT_FAILURE);
  }

  VkPipelineShaderStageCreateInfo shaderStages[2] = {0};

  // Vertex shader stage (expects texture coordinates).
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertShaderModule;
  shaderStages[0].pName = "main";

  // Fragment shader stage (samples from texture).
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragShaderModule;
  shaderStages[1].pName = "main";

  // Define a binding description for our textured vertex structure.
  VkVertexInputBindingDescription bindingDescription = {0};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(VertexTextured);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  // Define attribute descriptions for the textured vertex shader inputs.
  VkVertexInputAttributeDescription attributeDescriptions[2] = {0};

  // Attribute 0: position (vec2)
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(VertexTextured, pos);

  // Attribute 1: texture coordinate (vec2)
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(VertexTextured, texCoord);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = 2;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // Keep viewport and dynamic state as before.
  VkPipelineViewportStateCreateInfo viewportState = {0};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkDynamicState dynamicStates[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };

  VkPipelineDynamicStateCreateInfo dynamicState = {0};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkPipelineRasterizationStateCreateInfo rasterizer = {0};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo multisampling = {0};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending = {0};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkDescriptorSetLayout descriptorSetLayouts[] = {textureDescriptorSetLayout};
  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL,
          &texturedPipelineLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create textured pipeline layout\n");
    exit(EXIT_FAILURE);
  }

  VkGraphicsPipelineCreateInfo pipelineInfo = {0};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = texturedPipelineLayout; // Use the new layout.
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
          &texturedPipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create textured graphics pipeline\n");
    exit(EXIT_FAILURE);
  }

  // Clean up shader modules after pipeline creation.
  vkDestroyShaderModule(device, vertShaderModule, NULL);
  vkDestroyShaderModule(device, fragShaderModule, NULL);
}

void createBindlessGraphicsPipeline() {
  // Load SPIR-V binaries and create shader modules for bindless rendering.
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(device, bindless_vert_spv, bindless_vert_spv_len);
  VkShaderModule fragShaderModule = createShaderModuleFromMemory(
      device, bindless_frag_spv, bindless_frag_spv_len);

  if (vertShaderModule == VK_NULL_HANDLE ||
      fragShaderModule == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to create bindless shader modules\n");
    exit(EXIT_FAILURE);
  }

  VkPipelineShaderStageCreateInfo shaderStages[2] = {0};

  // Vertex shader stage (expects position, color, and texture ID).
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertShaderModule;
  shaderStages[0].pName = "main";

  // Fragment shader stage (uses bindless texture array).
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragShaderModule;
  shaderStages[1].pName = "main";

  // Define a binding description for our bindless vertex structure.
  VkVertexInputBindingDescription bindingDescription = {0};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(VertexBindless);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  // Define attribute descriptions for the bindless vertex shader inputs.
  VkVertexInputAttributeDescription attributeDescriptions[3] = {0};

  // Attribute 0: position (vec2)
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(VertexBindless, pos);

  // Attribute 1: color (vec3)
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(VertexBindless, color);

  // Attribute 2: texture ID (uint)
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32_UINT;
  attributeDescriptions[2].offset = offsetof(VertexBindless, textureID);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = 3;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {0};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)WIDTH;
  viewport.height = (float)HEIGHT;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {0};
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent.width = WIDTH;
  scissor.extent.height = HEIGHT;

  VkPipelineViewportStateCreateInfo viewportState = {0};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer = {0};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling = {0};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending = {0};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  // Use the bindless descriptor set layout
  if (!bindlessTextureAtlas) {
    fprintf(stderr, "Bindless texture atlas not initialized\n");
    exit(EXIT_FAILURE);
  }
  
  VkDescriptorSetLayout descriptorSetLayouts[] = {bindlessTextureAtlas->bindlessDescriptorSetLayout};
  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL,
          &bindlessPipelineLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create bindless pipeline layout\n");
    exit(EXIT_FAILURE);
  }

  VkGraphicsPipelineCreateInfo pipelineInfo = {0};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = bindlessPipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
          &bindlessPipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create bindless graphics pipeline\n");
    exit(EXIT_FAILURE);
  }

  vkDestroyShaderModule(device, vertShaderModule, NULL);
  vkDestroyShaderModule(device, fragShaderModule, NULL);
}

/// Creates a texture image from a BMP file.
void createTextureImage(const char * filePath) {
  // Load BMP data (assumed to be in 24-bit RGB format)
  CJellyFormatImage * image;
  CJellyFormatImageError error = cjelly_format_image_load(filePath, &image);
  if (error != CJELLY_FORMAT_IMAGE_SUCCESS) {
    fprintf(stderr, "Failed to load BMP file: %s\n", filePath);
    fprintf(stderr, "Error: %s\n", cjelly_format_image_strerror(error));
    exit(EXIT_FAILURE);
  }

  // Convert RGB to RGBA.
  int texWidth = image->raw->width;
  int texHeight = image->raw->height;
  unsigned char * pixelsRGB = image->raw->data;
  if (!pixelsRGB) {
    fprintf(stderr, "Failed to load BMP file: %s\n", filePath);
    exit(EXIT_FAILURE);
  }

  // Convert RGB to RGBA.
  size_t pixelCount = texWidth * texHeight;
  size_t rgbaImageSize = pixelCount * 4; // 4 bytes per pixel.
  unsigned char * pixels = malloc(rgbaImageSize);
  if (!pixels) {
    fprintf(stderr, "Failed to allocate memory for RGBA image\n");
    exit(EXIT_FAILURE);
  }
  for (size_t i = 0; i < pixelCount; ++i) {
    pixels[i * 4 + 0] = pixelsRGB[i * 3 + 0];
    pixels[i * 4 + 1] = pixelsRGB[i * 3 + 1];
    pixels[i * 4 + 2] = pixelsRGB[i * 3 + 2];
    pixels[i * 4 + 3] = 255; // Fully opaque.
  }

  // Clean up the original RGB image.
  cjelly_format_image_free(image);

  VkDeviceSize bufferSize = rgbaImageSize;

  // Create a staging buffer to hold the pixel data.
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      &stagingBuffer, &stagingBufferMemory);

  // Map memory and copy the pixel data.
  void * data;
  vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, pixels, (size_t)bufferSize);
  vkUnmapMemory(device, stagingBufferMemory);
  free(pixels);

  // Create the Vulkan texture image.
  // We choose VK_FORMAT_R8G8B8A8_UNORM for the RGBA data.
  createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &textureImage, &textureImageMemory);

  // Transition image layout to prepare for the data copy.
  transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  // Copy the pixel data from the staging buffer into the texture image.
  copyBufferToImage(stagingBuffer, textureImage, texWidth, texHeight);

  // Transition the image layout for shader access.
  transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(device, stagingBuffer, NULL);
  vkFreeMemory(device, stagingBufferMemory, NULL);
}

/// Creates an image view for the texture image.
void createTextureImageView() {
  VkImageViewCreateInfo viewInfo = {0};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = textureImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, NULL, &textureImageView) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture image view\n");
    exit(EXIT_FAILURE);
  }
}

/// Creates a texture sampler.
void createTextureSampler() {
  VkSamplerCreateInfo samplerInfo = {0};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;

  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = 16;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  if (vkCreateSampler(device, &samplerInfo, NULL, &textureSampler) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture sampler\n");
    exit(EXIT_FAILURE);
  }
}

/// Updates a descriptor set with the texture image view and sampler.
void updateTextureDescriptorSet(VkDescriptorSet descriptorSet) {
  VkDescriptorImageInfo imageInfo = {0};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = textureImageView;
  imageInfo.sampler = textureSampler;

  VkWriteDescriptorSet descriptorWrite = {0};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = descriptorSet;
  descriptorWrite.dstBinding =
      0; // Must match the binding in the descriptor set layout.
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, NULL);
}

void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer * buffer,
    VkDeviceMemory * bufferMemory) {
  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, NULL, buffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, *buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, NULL, bufferMemory) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate buffer memory\n");
    exit(EXIT_FAILURE);
  }

  vkBindBufferMemory(device, *buffer, *bufferMemory, 0);
}

void createImage(uint32_t width, uint32_t height, VkFormat format,
    VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage * image,
    VkDeviceMemory * imageMemory) {
  VkImageCreateInfo imageInfo = {0};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, NULL, image) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create image\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, *image, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, NULL, imageMemory) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate image memory\n");
    exit(EXIT_FAILURE);
  }

  vkBindImageMemory(device, *image, *imageMemory, 0);
}

VkCommandBuffer beginSingleTimeCommands(void) {
  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo = {0};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo = {0};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void transitionImageLayout(VkImage image, GCJ_MAYBE_UNUSED(VkFormat format),
    VkImageLayout oldLayout, VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier = {0};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;

  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  }
  else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
      newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  else {
    fprintf(stderr, "Unsupported layout transition!\n");
    exit(EXIT_FAILURE);
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, NULL,
      0, NULL, 1, &barrier);

  endSingleTimeCommands(commandBuffer);
}

void copyBufferToImage(
    VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferImageCopy region = {0};
  region.bufferOffset = 0;
  region.bufferRowLength = 0; // Tightly packed.
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = (VkOffset3D){0, 0, 0};
  region.imageExtent = (VkExtent3D){width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  endSingleTimeCommands(commandBuffer);
}

void createTexturedCommandBuffersForWindow(CJellyWindow * win) {
  win->commandBuffers =
      malloc(sizeof(VkCommandBuffer) * win->swapChainImageCount);
  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(device, &allocInfo, win->commandBuffers) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate textured command buffers\n");
    exit(EXIT_FAILURE);
  }

  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(win->commandBuffers[i], &beginInfo) !=
        VK_SUCCESS) {
      fprintf(stderr, "Failed to begin textured command buffer\n");
      exit(EXIT_FAILURE);
    }

    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass; // Assume same render pass is used.
    renderPassInfo.framebuffer = win->swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
    renderPassInfo.renderArea.extent = win->swapChainExtent;
    VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(
        win->commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set dynamic viewport and scissor.
    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)win->swapChainExtent.width;
    viewport.height = (float)win->swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(win->commandBuffers[i], 0, 1, &viewport);

    VkRect2D scissor = {0};
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = win->swapChainExtent;
    vkCmdSetScissor(win->commandBuffers[i], 0, 1, &scissor);

    VkDeviceSize offsets[] = {0};
    // Bind the vertex buffer containing textured vertices.
    vkCmdBindVertexBuffers(
        win->commandBuffers[i], 0, 1, &vertexBufferTextured, offsets);

    // Bind the textured pipeline instead of the original one.
    vkCmdBindPipeline(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
        texturedPipeline);

    // Bind descriptor sets containing the texture (assuming descriptor set is
    // allocated and updated).
    assert(textureDescriptorSet != VK_NULL_HANDLE);
    assert(texturedPipelineLayout != VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(win->commandBuffers[i],
        VK_PIPELINE_BIND_POINT_GRAPHICS, texturedPipelineLayout, 0, 1,
        &textureDescriptorSet, 0, NULL);

    // Issue draw call (the count may vary based on your vertex buffer).
    vkCmdDraw(win->commandBuffers[i], 6, 1, 0, 0);

    vkCmdEndRenderPass(win->commandBuffers[i]);

    if (vkEndCommandBuffer(win->commandBuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to record textured command buffer\n");
      exit(EXIT_FAILURE);
    }
  }
}

void createBindlessCommandBuffersForWindow(CJellyWindow * win) {
  win->commandBuffers =
      malloc(sizeof(VkCommandBuffer) * win->swapChainImageCount);

  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(device, &allocInfo, win->commandBuffers) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate bindless command buffers\n");
    exit(EXIT_FAILURE);
  }

  for (size_t i = 0; i < win->swapChainImageCount; i++) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(win->commandBuffers[i], &beginInfo) !=
        VK_SUCCESS) {
      fprintf(stderr, "Failed to begin bindless command buffer\n");
      exit(EXIT_FAILURE);
    }

    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = win->swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent = win->swapChainExtent;

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(win->commandBuffers[i], &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    // Bind the bindless pipeline.
    vkCmdBindPipeline(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
        bindlessPipeline);

    // Bind descriptor sets containing the bindless texture atlas.
    if (bindlessTextureAtlas && bindlessTextureAtlas->bindlessDescriptorSet != VK_NULL_HANDLE) {
      vkCmdBindDescriptorSets(win->commandBuffers[i],
          VK_PIPELINE_BIND_POINT_GRAPHICS, bindlessPipelineLayout, 0, 1,
          &bindlessTextureAtlas->bindlessDescriptorSet, 0, NULL);
    }

    VkDeviceSize offsets[] = {0};
    // Bind the vertex buffer containing bindless vertices.
    vkCmdBindVertexBuffers(
        win->commandBuffers[i], 0, 1, &vertexBufferBindless, offsets);

    // Issue draw call for multiple squares with different textures.
    vkCmdDraw(win->commandBuffers[i], 12, 1, 0, 0); // 12 vertices for 2 squares

    vkCmdEndRenderPass(win->commandBuffers[i]);

    if (vkEndCommandBuffer(win->commandBuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to record bindless command buffer\n");
      exit(EXIT_FAILURE);
    }
  }
}

/**
 * @brief Creates a vertex buffer for a textured square.
 *
 * This function allocates a Vulkan vertex buffer and uploads vertex data
 * from the global 'verticesTextured' array. The VertexTextured structure
 * includes both position and texture coordinates.
 */
void createTexturedVertexBuffer() {
  // Vertices for a textured square.
  VertexTextured verticesTextured[] = {
      {{-0.5f, -0.5f}, {0.0f, 0.0f}}, {{0.5f, -0.5f}, {1.0f, 0.0f}},
      {{0.5f, 0.5f}, {1.0f, 1.0f}},
      {{0.5f, 0.5f}, {1.0f, 1.0f}}, // Duplicate the top-right vertex.
      {{-0.5f, 0.5f}, {0.0f, 1.0f}},
      {{-0.5f, -0.5f}, {0.0f, 0.0f}} // Duplicate the bottom-left vertex.
  };

  VkDeviceSize bufferSize = sizeof(verticesTextured);

  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = bufferSize;
  bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, NULL, &vertexBufferTextured) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create textured vertex buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, vertexBufferTextured, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(device, &allocInfo, NULL, &vertexBufferTexturedMemory) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate textured vertex buffer memory\n");
    exit(EXIT_FAILURE);
  }

  vkBindBufferMemory(
      device, vertexBufferTextured, vertexBufferTexturedMemory, 0);

  void * data;
  vkMapMemory(device, vertexBufferTexturedMemory, 0, bufferSize, 0, &data);
  memcpy(data, verticesTextured, (size_t)bufferSize);
  vkUnmapMemory(device, vertexBufferTexturedMemory);
}

void createBindlessVertexBuffer() {
  // Create vertices for bindless rendering - multiple squares with different textures
  VertexBindless verticesBindless[] = {
    // Square 1 - Red with texture ID 1
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, 1},
    {{0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, 1},
    {{0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 1},
    {{0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 1},
    {{-0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 1},
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, 1},
    
    // Square 2 - Green with texture ID 2 (if available)
    {{-0.3f, -0.3f}, {0.0f, 1.0f, 0.0f}, 2},
    {{0.3f, -0.3f}, {0.0f, 1.0f, 0.0f}, 2},
    {{0.3f, 0.3f}, {0.0f, 1.0f, 0.0f}, 2},
    {{0.3f, 0.3f}, {0.0f, 1.0f, 0.0f}, 2},
    {{-0.3f, 0.3f}, {0.0f, 1.0f, 0.0f}, 2},
    {{-0.3f, -0.3f}, {0.0f, 1.0f, 0.0f}, 2},
  };

  VkDeviceSize bufferSize = sizeof(verticesBindless);

  createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               &vertexBufferBindless, &vertexBufferBindlessMemory);

  void * data;
  vkMapMemory(device, vertexBufferBindlessMemory, 0, bufferSize, 0, &data);
  memcpy(data, verticesBindless, (size_t)bufferSize);
  vkUnmapMemory(device, vertexBufferBindlessMemory);
}

//
// === GLOBAL VULKAN INITIALIZATION & CLEANUP ===
//

void initVulkanGlobal() {
  createInstance();
  if (enableValidationLayers)
    createDebugMessenger();
  pickPhysicalDevice();
  createLogicalDevice();
  createRenderPass();
  createCommandPool();

  createVertexBuffer();
  createGraphicsPipeline();

  // Textured square setup.
  createTextureImage("test/images/bmp/tang.bmp");
  createTexturedVertexBuffer();
  createTextureImageView();
  createTextureSampler();
  createDescriptorSetLayouts();
  createTextureDescriptorPool();
  allocateTextureDescriptorSet();
  updateTextureDescriptorSet(textureDescriptorSet);
  createTexturedGraphicsPipeline();

  // Bindless rendering setup (simplified approach)
  bindlessTextureAtlas = cjelly_create_texture_atlas(2048, 2048); // Larger atlas for 1024x1024 textures
  if (bindlessTextureAtlas) {
    // Add textures to the atlas
    cjelly_atlas_add_texture(bindlessTextureAtlas, "test/images/bmp/tang.bmp");
    cjelly_atlas_add_texture(bindlessTextureAtlas, "test/images/bmp/16Color.bmp");

    // Update the descriptor set with the atlas
    cjelly_atlas_update_descriptor_set(bindlessTextureAtlas);

    // Create bindless vertex buffer and pipeline
    createBindlessVertexBuffer();
    createBindlessGraphicsPipeline();
  }
}

void cleanupVulkanGlobal() {
  // Destroy pipeline and related objects.
  vkDestroyPipeline(device, graphicsPipeline, NULL);
  vkDestroyPipelineLayout(device, pipelineLayout, NULL);
  vkDestroyRenderPass(device, renderPass, NULL);

  // Clean up the vertex buffer for the colorful square.
  vkDestroyBuffer(device, vertexBuffer, NULL);
  vkFreeMemory(device, vertexBufferMemory, NULL);

  // --- Begin Texture Cleanup ---
  // Destroy the textured pipeline and layout.
  vkDestroyPipeline(device, texturedPipeline, NULL);
  vkDestroyPipelineLayout(device, texturedPipelineLayout, NULL);

  // Destroy the textured vertex buffer.
  vkDestroyBuffer(device, vertexBufferTextured, NULL);
  vkFreeMemory(device, vertexBufferTexturedMemory, NULL);

  // --- Begin Bindless Cleanup ---
  // Only clean up if bindless resources were created
  if (bindlessPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, bindlessPipeline, NULL);
  }
  if (bindlessPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, bindlessPipelineLayout, NULL);
  }
  if (vertexBufferBindless != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, vertexBufferBindless, NULL);
  }
  if (vertexBufferBindlessMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, vertexBufferBindlessMemory, NULL);
  }
  if (bindlessTextureAtlas != NULL) {
    cjelly_destroy_texture_atlas(bindlessTextureAtlas);
  }

  // Destroy the texture sampler and image view.
  vkDestroySampler(device, textureSampler, NULL);
  vkDestroyImageView(device, textureImageView, NULL);

  // Destroy the texture image and free its memory.
  vkDestroyImage(device, textureImage, NULL);
  vkFreeMemory(device, textureImageMemory, NULL);

  // Destroy the descriptor pool and layout for the texture.
  vkDestroyDescriptorPool(device, textureDescriptorPool, NULL);
  vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout, NULL);
  // Note: The textureDescriptorSet is automatically freed when the descriptor
  // pool is destroyed.
  // --- End Texture Cleanup ---

  // Clean up the command pool.
  vkDestroyCommandPool(device, commandPool, NULL);

  // Destroy the debug messenger if validation layers are enabled.
  if (enableValidationLayers) {
    destroyDebugMessenger();
  }

  // Destroy the device and instance.
  vkDestroyDevice(device, NULL);
  vkDestroyInstance(instance, NULL);
}

//
// === BINDLESS TEXTURE ATLAS MANAGEMENT ===
//

// Global texture atlas for bindless rendering
static CJellyTextureEntry * textureEntries = NULL;
static uint32_t maxTextures = 1024; // Maximum number of textures in atlas

CJellyTextureAtlas * cjelly_create_texture_atlas(uint32_t width, uint32_t height) {
  CJellyTextureAtlas * atlas = malloc(sizeof(CJellyTextureAtlas));
  if (!atlas) {
    fprintf(stderr, "Failed to allocate memory for texture atlas\n");
    return NULL;
  }
  
  memset(atlas, 0, sizeof(CJellyTextureAtlas));
  atlas->atlasWidth = width;
  atlas->atlasHeight = height;
  atlas->nextTextureX = 0;
  atlas->nextTextureY = 0;
  atlas->currentRowHeight = 0;
  atlas->textureCount = 0;
  
  // Allocate memory for texture entries
  textureEntries = malloc(sizeof(CJellyTextureEntry) * maxTextures);
  if (!textureEntries) {
    fprintf(stderr, "Failed to allocate memory for texture entries\n");
    free(atlas);
    return NULL;
  }
  memset(textureEntries, 0, sizeof(CJellyTextureEntry) * maxTextures);
  
  // Create the atlas image
  createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
              &atlas->atlasImage, &atlas->atlasImageMemory);
  
  // Create image view
  VkImageViewCreateInfo viewInfo = {0};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = atlas->atlasImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  
  if (vkCreateImageView(device, &viewInfo, NULL, &atlas->atlasImageView) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create atlas image view\n");
    vkDestroyImage(device, atlas->atlasImage, NULL);
    vkFreeMemory(device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }
  
  // Create sampler (reuse the existing texture sampler for now)
  atlas->atlasSampler = textureSampler;
  
  // Create bindless descriptor set layout
  VkDescriptorSetLayoutBinding layoutBinding = {0};
  layoutBinding.binding = 0;
  layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  layoutBinding.descriptorCount = maxTextures; // Maximum number of textures
  layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutBinding.pImmutableSamplers = NULL;
  
  VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &layoutBinding;
  
  // Enable descriptor indexing features
  VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlags = {0};
  bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
  bindingFlags.bindingCount = 1;
  VkDescriptorBindingFlagsEXT flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | 
                                      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
  bindingFlags.pBindingFlags = &flags;
  layoutInfo.pNext = &bindingFlags;
  
  if (vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &atlas->bindlessDescriptorSetLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create bindless descriptor set layout\n");
    vkDestroyImageView(device, atlas->atlasImageView, NULL);
    vkDestroyImage(device, atlas->atlasImage, NULL);
    vkFreeMemory(device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }
  
  // Create descriptor pool
  VkDescriptorPoolSize poolSize = {0};
  poolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  poolSize.descriptorCount = maxTextures;
  
  VkDescriptorPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
  
  if (vkCreateDescriptorPool(device, &poolInfo, NULL, &atlas->bindlessDescriptorPool) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create bindless descriptor pool\n");
    vkDestroyDescriptorSetLayout(device, atlas->bindlessDescriptorSetLayout, NULL);
    vkDestroyImageView(device, atlas->atlasImageView, NULL);
    vkDestroyImage(device, atlas->atlasImage, NULL);
    vkFreeMemory(device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }
  
  // Allocate descriptor set
  VkDescriptorSetAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = atlas->bindlessDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &atlas->bindlessDescriptorSetLayout;
  
  if (vkAllocateDescriptorSets(device, &allocInfo, &atlas->bindlessDescriptorSet) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate bindless descriptor set\n");
    vkDestroyDescriptorPool(device, atlas->bindlessDescriptorPool, NULL);
    vkDestroyDescriptorSetLayout(device, atlas->bindlessDescriptorSetLayout, NULL);
    vkDestroyImageView(device, atlas->atlasImageView, NULL);
    vkDestroyImage(device, atlas->atlasImage, NULL);
    vkFreeMemory(device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }
  
  return atlas;
}

void cjelly_destroy_texture_atlas(CJellyTextureAtlas * atlas) {
  if (!atlas) return;
  
  vkDestroyDescriptorSetLayout(device, atlas->bindlessDescriptorSetLayout, NULL);
  vkDestroyDescriptorPool(device, atlas->bindlessDescriptorPool, NULL);
  vkDestroyImageView(device, atlas->atlasImageView, NULL);
  vkDestroyImage(device, atlas->atlasImage, NULL);
  vkFreeMemory(device, atlas->atlasImageMemory, NULL);
  
  if (textureEntries) {
    free(textureEntries);
    textureEntries = NULL;
  }
  
  free(atlas);
}

uint32_t cjelly_atlas_add_texture(CJellyTextureAtlas * atlas, const char * filePath) {
  if (!atlas || atlas->textureCount >= maxTextures) {
    return 0; // Invalid texture ID
  }
  
  // Load the image
  CJellyFormatImage * image;
  if (cjelly_format_image_load(filePath, &image) != CJELLY_FORMAT_IMAGE_SUCCESS) {
    fprintf(stderr, "Failed to load texture: %s\n", filePath);
    return 0;
  }
  
  uint32_t texWidth = image->raw->width;
  uint32_t texHeight = image->raw->height;
  
  // Check if texture fits in current row
  if (atlas->nextTextureX + texWidth > atlas->atlasWidth) {
    // Move to next row
    atlas->nextTextureX = 0;
    atlas->nextTextureY += atlas->currentRowHeight;
    atlas->currentRowHeight = 0;
  }
  
  // Check if texture fits in atlas
  if (atlas->nextTextureY + texHeight > atlas->atlasHeight) {
    fprintf(stderr, "Texture atlas is full\n");
    cjelly_format_image_free(image);
    return 0;
  }
  
  // Create staging buffer for the texture
  VkDeviceSize imageSize = texWidth * texHeight * 4; // RGBA
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  
  createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               &stagingBuffer, &stagingBufferMemory);
  
  // Copy image data to staging buffer
  void * data;
  vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
  
  // Convert RGB to RGBA and copy to staging buffer
  uint8_t * pixels = (uint8_t *)data;
  for (uint32_t y = 0; y < texHeight; y++) {
    for (uint32_t x = 0; x < texWidth; x++) {
      uint32_t srcIndex = (y * texWidth + x) * 3; // RGB
      uint32_t dstIndex = (y * texWidth + x) * 4; // RGBA
      pixels[dstIndex] = image->raw->data[srcIndex];     // R
      pixels[dstIndex + 1] = image->raw->data[srcIndex + 1]; // G
      pixels[dstIndex + 2] = image->raw->data[srcIndex + 2]; // B
      pixels[dstIndex + 3] = 255; // A
    }
  }
  
  vkUnmapMemory(device, stagingBufferMemory);
  
  // Copy staging buffer to atlas image at the correct position
  VkBufferImageCopy region = {0};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset.x = atlas->nextTextureX;
  region.imageOffset.y = atlas->nextTextureY;
  region.imageOffset.z = 0;
  region.imageExtent.width = texWidth;
  region.imageExtent.height = texHeight;
  region.imageExtent.depth = 1;
  
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();
  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, atlas->atlasImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  endSingleTimeCommands(commandBuffer);
  
  // Store texture entry
  uint32_t textureID = atlas->textureCount + 1; // Start from 1, 0 means no texture
  CJellyTextureEntry * entry = &textureEntries[atlas->textureCount];
  entry->textureID = textureID;
  entry->x = atlas->nextTextureX;
  entry->y = atlas->nextTextureY;
  entry->width = texWidth;
  entry->height = texHeight;
  
  // Calculate UV coordinates
  entry->uMin = (float)atlas->nextTextureX / (float)atlas->atlasWidth;
  entry->uMax = (float)(atlas->nextTextureX + texWidth) / (float)atlas->atlasWidth;
  entry->vMin = (float)atlas->nextTextureY / (float)atlas->atlasHeight;
  entry->vMax = (float)(atlas->nextTextureY + texHeight) / (float)atlas->atlasHeight;
  
  // Update atlas position
  atlas->nextTextureX += texWidth;
  if (texHeight > atlas->currentRowHeight) {
    atlas->currentRowHeight = texHeight;
  }
  atlas->textureCount++;
  
  // Clean up
  vkDestroyBuffer(device, stagingBuffer, NULL);
  vkFreeMemory(device, stagingBufferMemory, NULL);
  cjelly_format_image_free(image);
  
  return textureID;
}

CJellyTextureEntry * cjelly_atlas_get_texture_entry(CJellyTextureAtlas * atlas, uint32_t textureID) {
  if (!atlas || textureID == 0 || textureID > atlas->textureCount) {
    return NULL;
  }
  
  return &textureEntries[textureID - 1];
}

void cjelly_atlas_update_descriptor_set(CJellyTextureAtlas * atlas) {
  if (!atlas) return;
  
  // Update the descriptor set with the atlas image view
  VkDescriptorImageInfo imageInfo = {0};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = atlas->atlasImageView;
  imageInfo.sampler = VK_NULL_HANDLE; // No sampler for sampled images
  
  VkWriteDescriptorSet descriptorWrite = {0};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = atlas->bindlessDescriptorSet;
  descriptorWrite.dstBinding = 0;
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pImageInfo = &imageInfo;
  
  vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, NULL);
}
