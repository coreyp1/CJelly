#pragma once
#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <X11/Xlib.h>
#endif

typedef struct CJellyWindow {
#ifdef _WIN32
  HWND handle;
#else
  Window handle;
#endif
  VkSurfaceKHR surface;
  VkSwapchainKHR swapChain;
  uint32_t swapChainImageCount;
  VkImage * swapChainImages;
  VkImageView * swapChainImageViews;
  VkFramebuffer * swapChainFramebuffers;
  VkCommandBuffer * commandBuffers;
  VkSemaphore imageAvailableSemaphore;
  VkSemaphore renderFinishedSemaphore;
  VkFence inFlightFence;
  VkExtent2D swapChainExtent;
  int width;
  int height;
  int updateMode;
  uint32_t fixedFramerate;
  int needsRedraw;
  uint64_t nextFrameTime;
  void (*renderCallback)(struct CJellyWindow *);
} CJellyWindow;

struct CJellyVulkanContext; /* from runtime.h */
struct CJellyBindlessResources; /* from runtime.h */

/* Internal functions defined in cjelly.c */
void createPlatformWindow(CJellyWindow * win, const char * title, int width, int height);
void createSurfaceForWindow(CJellyWindow * win);
void createSwapChainForWindow(CJellyWindow * win);
void createImageViewsForWindow(CJellyWindow * win);
void createFramebuffersForWindow(CJellyWindow * win);
void createSyncObjectsForWindow(CJellyWindow * win);
void drawFrameForWindow(CJellyWindow * win);
void cleanupWindow(CJellyWindow * win);
void createBindlessCommandBuffersForWindowCtx(CJellyWindow * win, const struct CJellyBindlessResources* resources, const struct CJellyVulkanContext* ctx);


