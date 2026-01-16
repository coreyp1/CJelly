/* Headers */

/* Platform includes */
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#else
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>
extern Display* display; /* provided by main on Linux */
#endif

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <cjelly/cj_window.h>
#include <cjelly/cj_platform.h>
#include <cjelly/runtime.h>
#include <cjelly/application.h>
#include <cjelly/engine_internal.h>
#include <cjelly/bindless_internal.h>
#include <cjelly/textured_internal.h>
#include <cjelly/cj_rgraph.h>
#include <cjelly/window_internal.h>

/* Forward declarations */
typedef struct CJPlatformWindow CJPlatformWindow;

/* Platform window struct - defined early so window procedure can access it */
typedef struct CJPlatformWindow {
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
  bool is_minimized;  /* Cached minimized state (updated via window messages) */
  bool needs_swapchain_recreate;  /* Flag to defer swapchain recreation until next frame */
} CJPlatformWindow;

/* Internal definition of the opaque window type */
struct cj_window_t {
  CJPlatformWindow * plat;
  uint64_t frame_index;
  cj_rgraph_t* render_graph;  /* Render graph for this window (not owned) */
  cj_window_close_callback_t close_callback;  /* Close callback (NULL if none) */
  void* close_callback_user_data;  /* User data for close callback */
  cj_window_frame_callback_t frame_callback;  /* Per-frame callback (NULL if none) */
  void* frame_callback_user_data; /* User data for per-frame callback */
  cj_window_resize_callback_t resize_callback;  /* Resize callback (NULL if none) */
  void* resize_callback_user_data; /* User data for resize callback */
  cj_redraw_policy_t redraw_policy;  /* Redraw policy for this window */
  uint32_t max_fps;  /* Maximum FPS for this window (0 = unlimited) */
  uint64_t last_render_time_us;  /* Last render time in microseconds (for FPS limiting) */
  cj_render_reason_t pending_render_reason;  /* Reason why window needs to render (if dirty) */
  bool is_destroyed;  /* Flag to prevent double-destruction */
};

/* Forward declarations for platform helpers - needed by window procedure */
static void plat_cleanupWindow(CJPlatformWindow * win);
static void plat_createSwapChainForWindow(CJPlatformWindow * win);
static void plat_recreateSwapChainForWindow(CJPlatformWindow * win);
static bool plat_createImageViewsForWindow(CJPlatformWindow * win);
static bool plat_createFramebuffersForWindow(CJPlatformWindow * win);
static bool createTexturedCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyVulkanContext* ctx);

#ifdef _WIN32
/* Timer ID for resize rendering */
#define CJ_RESIZE_TIMER_ID 1
#define CJ_RESIZE_TIMER_MS 16  /* ~60 FPS during resize */

/* Forward declaration for rendering during resize */
static void cj_window__render_frame_immediate(cj_window_t* window);

/*
 * Windows window procedure.
 *
 * Window closing flow:
 * 1. User clicks X -> WM_CLOSE sent
 * 2. WM_CLOSE calls close callback, then cj_window_destroy() if allowed
 * 3. cj_window_destroy() does all cleanup and calls DestroyWindow()
 * 4. WM_DESTROY is sent but is a no-op (cleanup already done)
 *
 * Resize handling:
 * Windows enters a modal loop during resize/move operations (WM_ENTERSIZEMOVE).
 * During this modal loop, our main event loop doesn't run. To keep rendering,
 * we start a timer that fires periodically to render frames.
 */
static LRESULT CALLBACK CjWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_CLOSE: {
      // User requested window close (clicked X button)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          // Invoke close callback if present
          cj_window_close_response_t response = CJ_WINDOW_CLOSE_ALLOW;
          if (window->close_callback) {
            response = window->close_callback(window, true, window->close_callback_user_data);
          }

          if (response == CJ_WINDOW_CLOSE_ALLOW) {
            // Destroy window - this handles all cleanup
            cj_window_destroy(window);
          }
          // Return 0 to indicate we handled the message (whether closed or not)
          return 0;
        }
      }
      // Window not found or invalid - use default behavior
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    case WM_ENTERSIZEMOVE: {
      // Windows is entering the modal resize/move loop.
      // Start a timer to keep rendering during this modal loop.
      SetTimer(hwnd, CJ_RESIZE_TIMER_ID, CJ_RESIZE_TIMER_MS, NULL);
      return 0;
    }

    case WM_EXITSIZEMOVE: {
      // Windows is exiting the modal resize/move loop.
      // Stop the timer.
      KillTimer(hwnd, CJ_RESIZE_TIMER_ID);
      return 0;
    }

    case WM_TIMER: {
      if (wParam == CJ_RESIZE_TIMER_ID) {
        // Timer fired during modal resize loop - render a frame
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
          if (window && !window->is_destroyed) {
            cj_window__render_frame_immediate(window);
          }
        }
        return 0;
      }
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    case WM_SIZE: {
      // Track minimized/restored state and handle resize
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && window->plat) {
          // wParam: SIZE_MINIMIZED, SIZE_MAXIMIZED, SIZE_RESTORED, SIZE_MAXSHOW, SIZE_MAXHIDE
          if (wParam == SIZE_MINIMIZED) {
            cj_window__set_minimized(window, true);
          } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
            cj_window__set_minimized(window, false);
            /* Mark window dirty when restored from minimized */
            window->plat->needsRedraw = 1;
            window->pending_render_reason = CJ_RENDER_REASON_EXPOSE;

            // Extract new width and height from lParam
            uint32_t new_width = (uint32_t)(LOWORD(lParam));
            uint32_t new_height = (uint32_t)(HIWORD(lParam));

            // Only update if size actually changed
            if ((int)new_width != window->plat->width || (int)new_height != window->plat->height) {
              // Update size and mark swapchain for recreation
              cj_window__update_size_and_mark_recreate(window, new_width, new_height);

              // Dispatch resize callback (user can do additional work)
              cj_window__dispatch_resize_callback(window, new_width, new_height);
            }
          }
        }
      }
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    case WM_DESTROY:
      // Window is being destroyed. Cleanup is already done by cj_window_destroy(),
      // so this is just a no-op. The user data should already be cleared.
      return DefWindowProc(hwnd, uMsg, wParam, lParam);

    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}
#else
// Linux window handling is done in processWindowEvents() in cjelly.c
// No window procedure needed for X11
#endif

/* === Platform helpers === */
static void plat_createPlatformWindow(CJPlatformWindow * win, const char * title, int width, int height) {
  if (!win) return;
  win->width = width; win->height = height;
  win->is_minimized = false;  /* Initialize minimized state */
#ifdef _WIN32
  HINSTANCE hInstance = GetModuleHandle(NULL);
  WNDCLASS wc = {0};
  wc.lpfnWndProc = CjWndProc; wc.hInstance = hInstance; wc.lpszClassName = "CJellyWindow";
  RegisterClass(&wc);
  win->handle = CreateWindowEx(0, "CJellyWindow", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, hInstance, NULL);
  ShowWindow(win->handle, SW_SHOW);
#else
  int screen = DefaultScreen(display);
  /* Use black background to reduce flickering during resize */
  win->handle = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, (unsigned)width, (unsigned)height, 0, BlackPixel(display, screen), BlackPixel(display, screen));
  XSelectInput(display, win->handle, StructureNotifyMask | KeyPressMask | ExposureMask);
  Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XStoreName(display, win->handle, title);
  XSetWMProtocols(display, win->handle, &wmDelete, 1);

  /* Set window background to None to prevent X11 from drawing background during resize.
   * This reduces flickering as the compositor won't show a solid background between frames. */
  XSetWindowBackgroundPixmap(display, win->handle, None);

  XMapWindow(display, win->handle);
  XFlush(display);
#endif
}

static void plat_createSurfaceForWindow(CJPlatformWindow * win) {
  if (!win) return;
#ifdef _WIN32
  VkWin32SurfaceCreateInfoKHR ci = {0}; ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR; ci.hinstance = GetModuleHandle(NULL); ci.hwnd = win->handle;
  vkCreateWin32SurfaceKHR(cj_engine_instance(cj_engine_get_current()), &ci, NULL, &win->surface);
#else
  VkXlibSurfaceCreateInfoKHR ci = {0}; ci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR; ci.dpy = display; ci.window = win->handle;
  vkCreateXlibSurfaceKHR(cj_engine_instance(cj_engine_get_current()), &ci, NULL, &win->surface);
#endif
}

static void plat_createSwapChainForWindow(CJPlatformWindow * win) {
  if (!win) return;
  VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(cj_engine_physical_device(cj_engine_get_current()), win->surface, &caps);
  win->swapChainExtent = caps.currentExtent;
  VkSwapchainCreateInfoKHR ci = {0}; ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR; ci.surface = win->surface; ci.minImageCount = caps.minImageCount; ci.imageFormat = VK_FORMAT_B8G8R8A8_SRGB; ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; ci.imageExtent = win->swapChainExtent; ci.imageArrayLayers = 1; ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; ci.clipped = VK_TRUE;
  /* Ensure we do not reference an invalid oldSwapchain */
  ci.oldSwapchain = VK_NULL_HANDLE;
  vkCreateSwapchainKHR(cj_engine_device(cj_engine_get_current()), &ci, NULL, &win->swapChain);
  cj_engine_ensure_render_pass(cj_engine_get_current(), ci.imageFormat);
}

/* Recreate swapchain for window resize. Destroys old swapchain resources and creates new ones. */
static void plat_recreateSwapChainForWindow(CJPlatformWindow * win) {
  if (!win || !win->swapChain) return;

  VkDevice dev = cj_engine_device(cj_engine_get_current());
  if (!dev) return;

  /* Wait for device to finish all operations before recreating swapchain */
  vkDeviceWaitIdle(dev);

  /* Save old swapchain for recreation */
  VkSwapchainKHR oldSwapchain = win->swapChain;

  /* Destroy old swapchain-dependent resources */
  VkCommandPool pool = cj_engine_command_pool(cj_engine_get_current());
  if (win->commandBuffers && win->swapChainImageCount > 0) {
    vkFreeCommandBuffers(dev, pool, win->swapChainImageCount, win->commandBuffers);
    free(win->commandBuffers);
    win->commandBuffers = NULL;
  }
  if (win->swapChainFramebuffers) {
    for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
      if (win->swapChainFramebuffers[i]) {
        vkDestroyFramebuffer(dev, win->swapChainFramebuffers[i], NULL);
      }
    }
    free(win->swapChainFramebuffers);
    win->swapChainFramebuffers = NULL;
  }
  if (win->swapChainImageViews) {
    for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
      if (win->swapChainImageViews[i]) {
        vkDestroyImageView(dev, win->swapChainImageViews[i], NULL);
      }
    }
    free(win->swapChainImageViews);
    win->swapChainImageViews = NULL;
  }
  if (win->swapChainImages) {
    free(win->swapChainImages);
    win->swapChainImages = NULL;
  }

  /* Query new surface capabilities */
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(cj_engine_physical_device(cj_engine_get_current()), win->surface, &caps);
  win->swapChainExtent = caps.currentExtent;

  /* Create new swapchain with old swapchain reference */
  VkSwapchainCreateInfoKHR ci = {0};
  ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  ci.surface = win->surface;
  ci.minImageCount = caps.minImageCount;
  ci.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
  ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  ci.imageExtent = win->swapChainExtent;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  ci.clipped = VK_TRUE;
  ci.oldSwapchain = oldSwapchain;  /* Reference old swapchain for proper recreation */

  if (vkCreateSwapchainKHR(dev, &ci, NULL, &win->swapChain) != VK_SUCCESS) {
    fprintf(stderr, "Error: Failed to recreate swapchain\n");
    return;
  }

  /* Destroy old swapchain after creating new one */
  vkDestroySwapchainKHR(dev, oldSwapchain, NULL);

  /* Recreate image views, framebuffers, and command buffers */
  if (!plat_createImageViewsForWindow(win)) {
    fprintf(stderr, "Error: Failed to recreate image views after resize\n");
    return;
  }
  if (!plat_createFramebuffersForWindow(win)) {
    fprintf(stderr, "Error: Failed to recreate framebuffers after resize\n");
    return;
  }

  /* Recreate command buffers */
  CJellyVulkanContext ctx = {0};
  cj_engine_t* e = cj_engine_get_current();
  ctx.instance = cj_engine_instance(e);
  ctx.physicalDevice = cj_engine_physical_device(e);
  ctx.device = cj_engine_device(e);
  ctx.graphicsQueue = cj_engine_graphics_queue(e);
  ctx.presentQueue = cj_engine_present_queue(e);
  ctx.renderPass = cj_engine_render_pass(e);
  ctx.commandPool = cj_engine_command_pool(e);

  if (!createTexturedCommandBuffersForWindowCtx(win, &ctx)) {
    fprintf(stderr, "Error: Failed to recreate command buffers after resize\n");
    return;
  }
}

static bool plat_createImageViewsForWindow(CJPlatformWindow * win) {
  if (!win) return false;
  vkGetSwapchainImagesKHR(cj_engine_device(cj_engine_get_current()), win->swapChain, &win->swapChainImageCount, NULL);
  win->swapChainImages = (VkImage*)malloc(sizeof(VkImage)*win->swapChainImageCount);
  if (!win->swapChainImages) {
    fprintf(stderr, "Error: Failed to allocate swapChainImages\n");
    return false;
  }
  vkGetSwapchainImagesKHR(cj_engine_device(cj_engine_get_current()), win->swapChain, &win->swapChainImageCount, win->swapChainImages);
  win->swapChainImageViews = (VkImageView*)malloc(sizeof(VkImageView)*win->swapChainImageCount);
  if (!win->swapChainImageViews) {
    fprintf(stderr, "Error: Failed to allocate swapChainImageViews\n");
    free(win->swapChainImages);
    win->swapChainImages = NULL;
    return false;
  }
  for (uint32_t i=0;i<win->swapChainImageCount;i++) {
    VkImageViewCreateInfo vi = {0}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vi.image = win->swapChainImages[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_B8G8R8A8_SRGB; vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(cj_engine_device(cj_engine_get_current()), &vi, NULL, &win->swapChainImageViews[i]) != VK_SUCCESS) {
      fprintf(stderr, "Error: Failed to create image view %u\n", i);
      // Clean up already created image views
      VkDevice dev = cj_engine_device(cj_engine_get_current());
      for (uint32_t j = 0; j < i; j++) {
        if (win->swapChainImageViews[j] != VK_NULL_HANDLE) {
          vkDestroyImageView(dev, win->swapChainImageViews[j], NULL);
        }
      }
      free(win->swapChainImageViews);
      win->swapChainImageViews = NULL;
      free(win->swapChainImages);
      win->swapChainImages = NULL;
      return false;
    }
  }
  return true;
}

static bool plat_createFramebuffersForWindow(CJPlatformWindow * win) {
  if (!win) return false;
  win->swapChainFramebuffers = (VkFramebuffer*)malloc(sizeof(VkFramebuffer)*win->swapChainImageCount);
  if (!win->swapChainFramebuffers) {
    fprintf(stderr, "Error: Failed to allocate swapChainFramebuffers\n");
    return false;
  }
  VkDevice dev = cj_engine_device(cj_engine_get_current());
  for (uint32_t i=0;i<win->swapChainImageCount;i++) {
    VkImageView attachments[] = { win->swapChainImageViews[i] };
    VkFramebufferCreateInfo fi = {0}; fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fi.renderPass = cj_engine_render_pass(cj_engine_get_current()); fi.attachmentCount = 1; fi.pAttachments = attachments; fi.width = win->swapChainExtent.width; fi.height = win->swapChainExtent.height; fi.layers = 1;
    if (vkCreateFramebuffer(dev, &fi, NULL, &win->swapChainFramebuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Error: Failed to create framebuffer %u\n", i);
      // Clean up already created framebuffers
      for (uint32_t j = 0; j < i; j++) {
        if (win->swapChainFramebuffers[j] != VK_NULL_HANDLE) {
          vkDestroyFramebuffer(dev, win->swapChainFramebuffers[j], NULL);
        }
      }
      free(win->swapChainFramebuffers);
      win->swapChainFramebuffers = NULL;
      return false;
    }
  }
  return true;
}

static void plat_createSyncObjectsForWindow(CJPlatformWindow * win) {
  if (!win) return;
  VkSemaphoreCreateInfo si = {0}; si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  vkCreateSemaphore(cj_engine_device(cj_engine_get_current()), &si, NULL, &win->imageAvailableSemaphore);
  vkCreateSemaphore(cj_engine_device(cj_engine_get_current()), &si, NULL, &win->renderFinishedSemaphore);
  VkFenceCreateInfo fi = {0}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; vkCreateFence(cj_engine_device(cj_engine_get_current()), &fi, NULL, &win->inFlightFence);
}

static void plat_drawFrameForWindow(CJPlatformWindow * win) {
  if (!win) return;
  VkDevice dev = cj_engine_device(cj_engine_get_current());
#ifdef _WIN32
  /* Skip draw if window has been destroyed */
  if (!win->handle || !IsWindow(win->handle)) return;
#endif
  vkWaitForFences(dev, 1, &win->inFlightFence, VK_TRUE, UINT64_MAX);
  vkResetFences(dev, 1, &win->inFlightFence);
  uint32_t imageIndex; vkAcquireNextImageKHR(dev, win->swapChain, UINT64_MAX, win->imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
  VkSemaphore waitS[] = { win->imageAvailableSemaphore }; VkPipelineStageFlags stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  VkSubmitInfo si = {0}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.waitSemaphoreCount = 1; si.pWaitSemaphores = waitS; si.pWaitDstStageMask = stages; si.commandBufferCount = 1; si.pCommandBuffers = &win->commandBuffers[imageIndex]; VkSemaphore sigS[] = { win->renderFinishedSemaphore }; si.signalSemaphoreCount = 1; si.pSignalSemaphores = sigS;
  vkQueueSubmit(cj_engine_graphics_queue(cj_engine_get_current()), 1, &si, win->inFlightFence);
  VkPresentInfoKHR pi = {0}; pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = sigS; pi.swapchainCount = 1; pi.pSwapchains = &win->swapChain; pi.pImageIndices = &imageIndex; vkQueuePresentKHR(cj_engine_present_queue(cj_engine_get_current()), &pi);
}

static void plat_cleanupWindow(CJPlatformWindow * win) {
  if (!win) return;
  VkDevice dev = cj_engine_device(cj_engine_get_current()); VkInstance inst = cj_engine_instance(cj_engine_get_current()); VkCommandPool pool = cj_engine_command_pool(cj_engine_get_current());

  // Wait for device to be idle before destroying resources
  if (dev) vkDeviceWaitIdle(dev);

  // Destroy Vulkan resources
  if (dev && win->renderFinishedSemaphore) vkDestroySemaphore(dev, win->renderFinishedSemaphore, NULL);
  if (dev && win->imageAvailableSemaphore) vkDestroySemaphore(dev, win->imageAvailableSemaphore, NULL);
  if (dev && win->inFlightFence) vkDestroyFence(dev, win->inFlightFence, NULL);
  if (dev && pool && win->commandBuffers && win->swapChainImageCount) vkFreeCommandBuffers(dev, pool, win->swapChainImageCount, win->commandBuffers);
  if (win->commandBuffers) { free(win->commandBuffers); win->commandBuffers = NULL; }
  if (dev && win->swapChainFramebuffers) { for (uint32_t i=0;i<win->swapChainImageCount;i++) if (win->swapChainFramebuffers[i]) vkDestroyFramebuffer(dev, win->swapChainFramebuffers[i], NULL); }
  if (dev && win->swapChainImageViews) { for (uint32_t i=0;i<win->swapChainImageCount;i++) if (win->swapChainImageViews[i]) vkDestroyImageView(dev, win->swapChainImageViews[i], NULL); }
  if (win->swapChainFramebuffers) { free(win->swapChainFramebuffers); win->swapChainFramebuffers = NULL; }
  if (win->swapChainImageViews) { free(win->swapChainImageViews); win->swapChainImageViews = NULL; }
  if (win->swapChainImages) { free(win->swapChainImages); win->swapChainImages = NULL; }
  if (dev && win->swapChain) { vkDestroySwapchainKHR(dev, win->swapChain, NULL); win->swapChain = VK_NULL_HANDLE; }
  if (inst && win->surface) { vkDestroySurfaceKHR(inst, win->surface, NULL); win->surface = VK_NULL_HANDLE; }

#ifdef _WIN32
  // DestroyWindow is called in cj_window_destroy, not here
  win->handle = NULL;
#else
  if (display && win->handle) XDestroyWindow(display, win->handle);
  win->handle = 0;
#endif
}
/* C library and cjelly headers */
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <cjelly/cj_window.h>
#include <cjelly/cj_platform.h>
#include <cjelly/runtime.h>
#include <cjelly/engine_internal.h>
#include <cjelly/bindless_internal.h>
#include <cjelly/textured_internal.h>

/* textured pipeline helper (defined in cjelly.c) */
void cjelly_init_textured_pipeline_ctx(const CJellyVulkanContext* ctx);


/* Internal helpers (static) */
static void plat_createPlatformWindow(CJPlatformWindow * win, const char * title, int width, int height);
static void plat_createSurfaceForWindow(CJPlatformWindow * win);
static void plat_createSwapChainForWindow(CJPlatformWindow * win);
static void plat_recreateSwapChainForWindow(CJPlatformWindow * win);
static bool plat_createImageViewsForWindow(CJPlatformWindow * win);
static bool plat_createFramebuffersForWindow(CJPlatformWindow * win);
static bool createTexturedCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyVulkanContext* ctx);
static void plat_createSyncObjectsForWindow(CJPlatformWindow * win);
static void plat_drawFrameForWindow(CJPlatformWindow * win);
static void plat_cleanupWindow(CJPlatformWindow * win);

/* Command buffer recorders using engine/ctx */
static bool createTexturedCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyVulkanContext* ctx);
static void createBindlessCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyBindlessResources* resources, const CJellyVulkanContext* ctx);

/* Bridge wrapper: implement cj_window_t in terms of legacy CJellyWindow
 * so we can migrate callers incrementally. */

CJ_API cj_window_t* cj_window_create(cj_engine_t* engine, const cj_window_desc_t* desc) {
  (void)engine; /* not used yet; legacy path */
  if (!desc) return NULL;
  cj_window_t* win = (cj_window_t*)calloc(1, sizeof(*win));
  if (!win) return NULL;
  win->plat = (CJPlatformWindow*)calloc(1, sizeof(CJPlatformWindow));
  if (!win->plat) { free(win); return NULL; }

  /* Create OS window and per-window Vulkan resources */
  const char* title = desc->title.ptr ? desc->title.ptr : "CJelly Window";
  plat_createPlatformWindow(win->plat, title, (int)desc->width, (int)desc->height);
  plat_createSurfaceForWindow(win->plat);
  plat_createSwapChainForWindow(win->plat);
  if (!plat_createImageViewsForWindow(win->plat)) {
    fprintf(stderr, "Error: Failed to create image views for window\n");
    cj_window_destroy(win);
    return NULL;
  }
  if (!plat_createFramebuffersForWindow(win->plat)) {
    fprintf(stderr, "Error: Failed to create framebuffers for window\n");
    cj_window_destroy(win);
    return NULL;
  }

  /* Initialize textured pipeline/resources via public ctx wrapper */
  CJellyVulkanContext ctx = {0};
  cj_engine_t* e = cj_engine_get_current();
  ctx.instance = cj_engine_instance(e);
  ctx.physicalDevice = cj_engine_physical_device(e);
  ctx.device = cj_engine_device(e);
  ctx.graphicsQueue = cj_engine_graphics_queue(e);
  ctx.presentQueue = cj_engine_present_queue(e);
  ctx.renderPass = cj_engine_render_pass(e);
  ctx.commandPool = cj_engine_command_pool(e);
  cjelly_init_textured_pipeline_ctx(&ctx);

  /* Record textured command buffers using ctx variants */
  if (!createTexturedCommandBuffersForWindowCtx(win->plat, &ctx)) {
    fprintf(stderr, "Error: Failed to create textured command buffers for window\n");
    cj_window_destroy(win);
    return NULL;
  }
  plat_createSyncObjectsForWindow(win->plat);

  win->frame_index = 0u;
  win->close_callback = NULL;
  win->close_callback_user_data = NULL;
  win->frame_callback = NULL;
  win->frame_callback_user_data = NULL;
  win->resize_callback = NULL;
  win->resize_callback_user_data = NULL;
  win->redraw_policy = CJ_REDRAW_ON_EVENTS;  /* Default: redraw on events */
  win->max_fps = 0;  /* Default: unlimited (use global FPS limit) */
  win->last_render_time_us = 0;  /* Initialize to 0 (will be set on first render) */
  win->pending_render_reason = CJ_RENDER_REASON_FORCED;  /* Initial render is forced */
  win->is_destroyed = false;
  win->plat->needs_swapchain_recreate = false;
  win->plat->needsRedraw = 1;  /* Window starts dirty (needs initial render) */
  win->pending_render_reason = CJ_RENDER_REASON_FORCED;  /* Initial render is forced */

  // Automatically register window with current application (if one exists)
  // If registration fails (OOM), we must fail window creation to avoid zombie windows
  void* handle = (void*)win->plat->handle;
  if (!cjelly_application_register_window(NULL, win, handle)) {
    // Registration failed (OOM) - destroy window and return NULL
    // This prevents creating untracked "zombie" windows
    fprintf(stderr, "Error: Failed to register window with application (out of memory). Destroying window.\n");
    // Clean up the window we just created (use destroy function for proper cleanup)
    // Note: is_destroyed is false, so destroy will proceed, but unregister will be a no-op
    cj_window_destroy(win);
    return NULL;
  }

#ifdef _WIN32
  // Store window pointer in window's user data so we can retrieve it in WM_DESTROY
  // even after unregistering from the application
  SetWindowLongPtr(win->plat->handle, GWLP_USERDATA, (LONG_PTR)win);
#endif

  return win;
}

/*
 * Destroy a window and free all associated resources.
 *
 * This is the single cleanup path for windows on all platforms.
 * It handles: Vulkan resources, platform window, and the cj_window_t structure.
 */
CJ_API void cj_window_destroy(cj_window_t* win) {
  // Guard against null or double-destruction
  if (!win || win->is_destroyed) {
    return;
  }

  // Mark as destroyed immediately to prevent re-entry
  win->is_destroyed = true;

  // Unregister from application (need handle for lookup)
  void* handle = win->plat ? (void*)win->plat->handle : NULL;
  cjelly_application_unregister_window(NULL, win, handle);

  // Clean up platform window and Vulkan resources
  if (win->plat) {
    // Wait for GPU to finish before destroying resources
    VkDevice dev = cj_engine_device(cj_engine_get_current());
    if (dev) {
      vkDeviceWaitIdle(dev);
    }

#ifdef _WIN32
    // Save and clear Windows-specific data before destruction
    HWND hwnd = win->plat->handle;
    if (hwnd && IsWindow(hwnd)) {
      SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    }
#endif

    // Clean up Vulkan resources (swapchain, surfaces, etc.)
    plat_cleanupWindow(win->plat);

#ifdef _WIN32
    // Destroy the Windows window
    if (hwnd && IsWindow(hwnd)) {
      DestroyWindow(hwnd);
    }
#endif

    free(win->plat);
  }

  free(win);
}

CJ_API cj_result_t cj_window_resize(cj_window_t* win, uint32_t width, uint32_t height) {
  (void)width; (void)height; /* legacy path will handle via swapchain recreation elsewhere */
  return win ? CJ_SUCCESS : CJ_E_INVALID_ARGUMENT;
}

CJ_API cj_result_t cj_window_begin_frame(cj_window_t* win, cj_frame_info_t* out_frame_info) {
  if (!win || win->is_destroyed) return CJ_E_INVALID_ARGUMENT;
  if (out_frame_info) {
    out_frame_info->frame_index = ++win->frame_index;
    out_frame_info->delta_seconds = 0.0; /* stub */
    /* Set render reason from pending reason, or default to TIMER if not dirty */
    out_frame_info->render_reason = cj_window__get_pending_render_reason(win);
    /* Clear pending reason after reading it (will be set again if needed) */
    if (win->plat && win->plat->needsRedraw == 0) {
      win->pending_render_reason = CJ_RENDER_REASON_TIMER;
    }
  } else {
    win->frame_index++;
  }
  return CJ_SUCCESS;
}

CJ_API cj_result_t cj_window_execute(cj_window_t* win) {
  if (!win || win->is_destroyed || !win->plat) return CJ_E_INVALID_ARGUMENT;

#ifdef _WIN32
  /* Critical: Check if window handle is still valid before using Vulkan resources */
  if (!win->plat->handle || !IsWindow(win->plat->handle)) {
    return CJ_E_INVALID_ARGUMENT;
  }
#endif

  /* Check if swapchain needs recreation (deferred from resize event) */
  if (win->plat->needs_swapchain_recreate) {
    plat_recreateSwapChainForWindow(win->plat);
    win->plat->needs_swapchain_recreate = false;
    /* Mark dirty after swapchain recreation (content needs refresh) */
    win->plat->needsRedraw = 1;
    win->pending_render_reason = CJ_RENDER_REASON_SWAPCHAIN_RECREATE;
  }

  /* Use render graph if available, otherwise fall back to legacy drawing */
  if (win->render_graph) {
    /* Execute render graph nodes */
    VkExtent2D extent = {win->plat->swapChainExtent.width, win->plat->swapChainExtent.height};

    /* Get the current command buffer from the window's command buffers */
    if (win->plat->commandBuffers && win->plat->swapChainImageCount > 0) {
      /* Acquire the next swapchain image first to get the correct command buffer index */
      VkDevice dev = cj_engine_device(cj_engine_get_current());
      vkWaitForFences(dev, 1, &win->plat->inFlightFence, VK_TRUE, UINT64_MAX);
      vkResetFences(dev, 1, &win->plat->inFlightFence);
      uint32_t imageIndex;
      vkAcquireNextImageKHR(dev, win->plat->swapChain, UINT64_MAX, win->plat->imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
      VkCommandBuffer cmd = win->plat->commandBuffers[imageIndex]; // Use the correct command buffer for this frame

      /* CRITICAL FIX: Properly prepare command buffer for render graph execution */
      VkCommandBufferBeginInfo beginInfo = {0};
      beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

      if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        printf("WINDOWS FIX: Failed to begin command buffer for render graph\n");
        /* Fall back to legacy drawing if command buffer begin fails */
        plat_drawFrameForWindow(win->plat);
        return CJ_SUCCESS;
      }

      /* Begin render pass for render graph */
      VkRenderPassBeginInfo renderPassInfo = {0};
      renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      renderPassInfo.renderPass = cj_engine_render_pass(cj_engine_get_current());
      renderPassInfo.framebuffer = win->plat->swapChainFramebuffers[imageIndex]; // Use the correct framebuffer for this frame
      renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
      renderPassInfo.renderArea.extent = win->plat->swapChainExtent;
      VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
      renderPassInfo.clearValueCount = 1;
      renderPassInfo.pClearValues = &clearColor;
      vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

      /* Set viewport and scissor */
      VkViewport viewport = {0};
      viewport.x = 0.0f;
      viewport.y = 0.0f;
      viewport.width = (float)win->plat->swapChainExtent.width;
      viewport.height = (float)win->plat->swapChainExtent.height;
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;
      vkCmdSetViewport(cmd, 0, 1, &viewport);

      VkRect2D scissor = {0};
      scissor.offset = (VkOffset2D){0, 0};
      scissor.extent = win->plat->swapChainExtent;
      vkCmdSetScissor(cmd, 0, 1, &scissor);

      /* Execute render graph */
      cj_result_t result = cj_rgraph_execute(win->render_graph, cmd, extent);

      /* End render pass and command buffer */
      vkCmdEndRenderPass(cmd);
      if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        printf("WINDOWS FIX: Failed to end command buffer for render graph\n");
        /* Fall back to legacy drawing if command buffer end fails */
        plat_drawFrameForWindow(win->plat);
        return CJ_SUCCESS;
      }

      if (result != CJ_SUCCESS) {
        printf("WINDOWS FIX: Render graph execution failed (result=%d), falling back to legacy\n", result);
        /* Fall back to legacy drawing if render graph execution fails */
        plat_drawFrameForWindow(win->plat);
      } else {
        /* Submit and present the command buffer (same as legacy path) */
        VkSemaphore waitS[] = { win->plat->imageAvailableSemaphore };
        VkPipelineStageFlags stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo si = {0};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = waitS;
        si.pWaitDstStageMask = stages;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &win->plat->commandBuffers[imageIndex];
        VkSemaphore sigS[] = { win->plat->renderFinishedSemaphore };
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = sigS;
        vkQueueSubmit(cj_engine_graphics_queue(cj_engine_get_current()), 1, &si, win->plat->inFlightFence);
        VkPresentInfoKHR pi = {0};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = sigS;
        pi.swapchainCount = 1;
        pi.pSwapchains = &win->plat->swapChain;
        pi.pImageIndices = &imageIndex;
        vkQueuePresentKHR(cj_engine_present_queue(cj_engine_get_current()), &pi);
      }
    } else {
      /* Fall back to legacy drawing if no command buffers available */
      plat_drawFrameForWindow(win->plat);
    }
  } else {
    /* Legacy path: direct drawing */
    printf("DEBUG: Using legacy rendering path\n");
    plat_drawFrameForWindow(win->plat);
  }

  return CJ_SUCCESS;
}

CJ_API cj_result_t cj_window_present(cj_window_t* win) {
  (void)win; /* drawFrameForWindow presents already */
  return CJ_SUCCESS;
}

CJ_API void cj_window_mark_dirty(cj_window_t* window) {
  cj_window_mark_dirty_with_reason(window, CJ_RENDER_REASON_FORCED);
}

CJ_API void cj_window_mark_dirty_with_reason(cj_window_t* window, cj_render_reason_t reason) {
  if (!window || !window->plat || window->is_destroyed) return;
  window->plat->needsRedraw = 1;
  window->pending_render_reason = reason;
}

CJ_API void cj_window_clear_dirty(cj_window_t* window) {
  if (!window || !window->plat || window->is_destroyed) return;
  window->plat->needsRedraw = 0;
  /* Reset render reason to TIMER for next render */
  window->pending_render_reason = CJ_RENDER_REASON_TIMER;
}

CJ_API void cj_window_set_redraw_policy(cj_window_t* window, cj_redraw_policy_t policy) {
  if (!window || window->is_destroyed) return;
  window->redraw_policy = policy;
}

CJ_API void cj_window_set_max_fps(cj_window_t* window, uint32_t max_fps) {
  if (!window || window->is_destroyed) return;
  window->max_fps = max_fps;
  /* Reset last render time so window can render immediately if needed */
  window->last_render_time_us = 0;
}

CJ_API void cj_window_set_render_graph(cj_window_t* win, cj_rgraph_t* graph) {
  if (!win) return;
  win->render_graph = graph;
  // Render graph attached to window
}

CJ_API void cj_window_get_size(const cj_window_t* win, uint32_t* out_w, uint32_t* out_h) {
  if (!win || !win->plat) return;
  if (out_w) *out_w = (uint32_t)win->plat->width;
  if (out_h) *out_h = (uint32_t)win->plat->height;
}

CJ_API uint64_t cj_window_frame_index(const cj_window_t* win) {
  return win ? win->frame_index : 0u;
}

CJ_API void cj_window_on_close(cj_window_t* window,
                                 cj_window_close_callback_t callback,
                                 void* user_data) {
  if (!window) return;
  window->close_callback = callback;
  window->close_callback_user_data = user_data;
}

CJ_API void cj_window_on_frame(cj_window_t* window,
                               cj_window_frame_callback_t callback,
                               void* user_data) {
  if (!window) return;
  window->frame_callback = callback;
  window->frame_callback_user_data = user_data;
}

CJ_API void cj_window_on_resize(cj_window_t* window,
                                cj_window_resize_callback_t callback,
                                void* user_data) {
  if (!window) return;
  window->resize_callback = callback;
  window->resize_callback_user_data = user_data;
}

/* Internal helper for the framework event loop. */
cj_frame_result_t cj_window__dispatch_frame_callback(cj_window_t* window,
                                                    const cj_frame_info_t* frame_info) {
  if (!window || window->is_destroyed) return CJ_FRAME_SKIP;
  if (!window->frame_callback) return CJ_FRAME_CONTINUE;
  return window->frame_callback(window, frame_info, window->frame_callback_user_data);
}

/* Internal helper to check if a window is minimized.
 * Uses cached state updated via window messages (no OS polling).
 */
bool cj_window__is_minimized(cj_window_t* window) {
  if (!window || !window->plat || window->is_destroyed) return false;
  return window->plat->is_minimized;
}

/* Internal helper to check if a window uses VSync (FIFO present mode).
 * Currently hardcoded to FIFO, but could query swapchain in the future.
 */
bool cj_window__uses_vsync(cj_window_t* window) {
  if (!window || !window->plat || window->is_destroyed) return false;
  // For now, we always use FIFO (VSync) mode. In the future, we could
  // store the present mode when creating the swapchain and check it here.
  (void)window;  // Suppress unused warning
  return true;  // FIFO is VSync
}

/* Internal helper to check if a window needs redraw. */
bool cj_window__needs_redraw(cj_window_t* window) {
  if (!window || !window->plat || window->is_destroyed) return false;

  /* Check redraw policy */
  switch (window->redraw_policy) {
    case CJ_REDRAW_ALWAYS:
      /* Always redraw */
      return true;

    case CJ_REDRAW_ON_DIRTY:
    case CJ_REDRAW_ON_EVENTS:
      /* Only redraw if dirty flag is set */
      return (window->plat->needsRedraw != 0);

    default:
      /* Unknown policy: default to always redraw for safety */
      return true;
  }
}

/* Internal helper to set minimized state (called from window messages/events). */
void cj_window__set_minimized(cj_window_t* window, bool minimized) {
  if (!window || !window->plat || window->is_destroyed) return;
  window->plat->is_minimized = minimized;
}

/* Internal helper to update window size and mark swapchain for recreation. */
void cj_window__update_size_and_mark_recreate(cj_window_t* window, uint32_t new_width, uint32_t new_height) {
  if (!window || !window->plat || window->is_destroyed) return;
  window->plat->width = (int)new_width;
  window->plat->height = (int)new_height;
  window->plat->needs_swapchain_recreate = true;
  /* Mark window dirty for redraw after resize */
  window->plat->needsRedraw = 1;
  window->pending_render_reason = CJ_RENDER_REASON_RESIZE;
}

/* Internal helper to dispatch resize callback. */
void cj_window__dispatch_resize_callback(cj_window_t* window, uint32_t new_width, uint32_t new_height) {
  if (!window || window->is_destroyed || !window->plat) return;

  /* Note: Swapchain recreation is deferred until next frame (via needs_swapchain_recreate flag)
   * to avoid blocking the window message handler during resize drag. The flag should be set
   * by the caller (WM_SIZE/ConfigureNotify handler) before calling this function. */

  /* Dispatch user callback */
  if (window->resize_callback) {
    window->resize_callback(window, new_width, new_height, window->resize_callback_user_data);
  }
}

/* Internal helper to check if dirty flag should be cleared after frame render.
 * For CJ_REDRAW_ALWAYS, we MUST clear the dirty flag after rendering so that
 * subsequent frames use TIMER reason (which respects FPS limits) instead of
 * FORCED reason (which bypasses FPS limits).
 */
bool cj_window__should_clear_dirty_after_render(cj_window_t* window) {
  if (!window || window->is_destroyed) return false;
  /* Clear dirty flag for all policies - this ensures:
   * - CJ_REDRAW_ALWAYS: uses TIMER reason (respects per-window FPS limit)
   * - CJ_REDRAW_ON_EVENTS: only re-renders when new events mark it dirty
   * - CJ_REDRAW_ON_DIRTY: only re-renders when explicitly marked dirty
   */
  return true;
}

/* Internal helper to check if frame callback should be called (even if not dirty).
 * For CJ_REDRAW_ON_EVENTS, callbacks are always called so they can check time and mark dirty.
 */
bool cj_window__should_call_callback(cj_window_t* window) {
  if (!window || window->is_destroyed) return false;

  switch (window->redraw_policy) {
    case CJ_REDRAW_ALWAYS:
    case CJ_REDRAW_ON_EVENTS:
      /* Always call callback - it can check time and mark dirty if needed */
      return true;

    case CJ_REDRAW_ON_DIRTY:
      /* Only call callback if dirty (to avoid unnecessary work for static content) */
      return (window->plat && window->plat->needsRedraw != 0);

    default:
      /* Unknown policy: default to calling callback for safety */
      return true;
  }
}

/* Internal helper to check if enough time has passed since last render for per-window FPS limiting. */
bool cj_window__can_render_at_fps(cj_window_t* window, uint64_t current_time_us) {
  if (!window || window->is_destroyed) return false;

  /* If FPS limit is disabled (0), always allow render */
  if (window->max_fps == 0) {
    return true;
  }

  /* If never rendered before, allow render */
  if (window->last_render_time_us == 0) {
    return true;
  }

  /* Calculate minimum time between frames */
  uint64_t min_frame_time_us = (1000000ULL / (uint64_t)window->max_fps);
  uint64_t time_since_last_render = current_time_us - window->last_render_time_us;

  /* Allow render if enough time has passed */
  return (time_since_last_render >= min_frame_time_us);
}

/* Internal helper to update the last render time for a window (used for FPS limiting). */
void cj_window__update_last_render_time(cj_window_t* window, uint64_t render_time_us) {
  if (!window || window->is_destroyed) return;
  window->last_render_time_us = render_time_us;
}

/* Internal helper to get the pending render reason for a window. */
cj_render_reason_t cj_window__get_pending_render_reason(cj_window_t* window) {
  if (!window || window->is_destroyed) return CJ_RENDER_REASON_TIMER;
  if (!window->plat || window->plat->needsRedraw == 0) {
    return CJ_RENDER_REASON_TIMER;  /* Not dirty, so it's a timer-based render */
  }
  return window->pending_render_reason;
}

/* Internal helper to set the pending render reason for a window. */
void cj_window__set_pending_render_reason(cj_window_t* window, cj_render_reason_t reason) {
  if (!window || window->is_destroyed) return;
  window->pending_render_reason = reason;
}

/* Internal helper to check if a window uses CJ_REDRAW_ALWAYS policy. */
bool cj_window__uses_always_redraw(cj_window_t* window) {
  if (!window || window->is_destroyed) return false;
  return (window->redraw_policy == CJ_REDRAW_ALWAYS);
}

/* Internal helper to check if a render reason should bypass FPS limiting. */
bool cj_window__should_bypass_fps_limit(cj_render_reason_t reason) {
  switch (reason) {
    case CJ_RENDER_REASON_TIMER:
      return false;  /* Timer-based renders respect FPS limit */
    case CJ_RENDER_REASON_RESIZE:
    case CJ_RENDER_REASON_EXPOSE:
    case CJ_RENDER_REASON_FORCED:
    case CJ_RENDER_REASON_SWAPCHAIN_RECREATE:
      return true;  /* All other reasons bypass FPS limit */
    default:
      return true;  /* Unknown reasons: bypass for safety */
  }
}

#ifdef _WIN32
/*
 * Render a single frame immediately. Used during Windows modal resize loop.
 * This bypasses the main event loop to keep the window responsive during resize.
 */
static void cj_window__render_frame_immediate(cj_window_t* window) {
  if (!window || window->is_destroyed || !window->plat) return;

  /* Recreate swapchain if needed */
  if (window->plat->needs_swapchain_recreate) {
    plat_recreateSwapChainForWindow(window->plat);
    window->plat->needs_swapchain_recreate = false;
  }

  /* Skip if minimized */
  if (window->plat->is_minimized) return;

  /* Begin frame */
  cj_frame_info_t frame = {0};
  if (cj_window_begin_frame(window, &frame) != CJ_SUCCESS) {
    return;
  }

  /* Call frame callback if present */
  if (window->frame_callback) {
    cj_frame_result_t result = window->frame_callback(window, &frame, window->frame_callback_user_data);
    if (result == CJ_FRAME_SKIP || result == CJ_FRAME_CLOSE_WINDOW || result == CJ_FRAME_STOP_LOOP) {
      return;
    }
  }

  /* Execute and present */
  cj_window_execute(window);
  cj_window_present(window);
}
#endif

// Internal helper to invoke close callback and destroy window if allowed
void cj_window_close_with_callback(cj_window_t* window, bool cancellable) {
  if (!window)
    return;

  cj_window_close_response_t response = CJ_WINDOW_CLOSE_ALLOW;

  // Invoke callback if present
  if (window->close_callback) {
    response = window->close_callback(window, cancellable, window->close_callback_user_data);
  }

  // Destroy window if allowed (or if not cancellable)
  if (!cancellable || response == CJ_WINDOW_CLOSE_ALLOW) {
    cj_window_destroy(window);
  }
  // If cancellable and response is PREVENT, window stays open
}

/* Re-record color-only bindless commands for a window */
CJ_API void cj_window_rerecord_bindless_color(cj_window_t* win,
                                       const void* resources,
                                       const CJellyVulkanContext* ctx) {
  if (!win || !win->plat || !resources || !ctx) return;
  const CJellyBindlessResources* r = (const CJellyBindlessResources*)resources;
  /* Ensure GPU is idle before re-record to avoid freeing in-use buffers */
  {
    cj_engine_t* e2 = cj_engine_get_current();
    if (e2 && cj_engine_device(e2) != VK_NULL_HANDLE) vkDeviceWaitIdle(cj_engine_device(e2));
  }
  if (win->plat->commandBuffers && win->plat->swapChainImageCount > 0) {
    cj_engine_t* e3 = cj_engine_get_current();
    vkFreeCommandBuffers(cj_engine_device(e3), cj_engine_command_pool(e3), win->plat->swapChainImageCount, win->plat->commandBuffers);
    free(win->plat->commandBuffers);
    win->plat->commandBuffers = NULL;
  }
  createBindlessCommandBuffersForWindowCtx(win->plat, r, ctx);
}

/* Per-window textured command buffer recording using explicit ctx */
static bool createTexturedCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyVulkanContext* ctx) {
  if (!win || !ctx || ctx->device == VK_NULL_HANDLE || ctx->commandPool == VK_NULL_HANDLE || ctx->renderPass == VK_NULL_HANDLE) return false;
  win->commandBuffers =
      (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer) * win->swapChainImageCount);
  if (!win->commandBuffers) {
    fprintf(stderr, "Error: Failed to allocate command buffers\n");
    return false;
  }

  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = ctx->commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(ctx->device, &allocInfo, win->commandBuffers) != VK_SUCCESS) {
    fprintf(stderr, "Error: Failed to allocate textured (ctx) command buffers\n");
    free(win->commandBuffers);
    win->commandBuffers = NULL;
    return false;
  }

  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(win->commandBuffers[i], &beginInfo) != VK_SUCCESS) {
      fprintf(stderr, "Failed to begin textured (ctx) command buffer\n");
      exit(EXIT_FAILURE);
    }

    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = ctx->renderPass;
    renderPassInfo.framebuffer = win->swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
    renderPassInfo.renderArea.extent = win->swapChainExtent;
    VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(win->commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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

  CJellyTexturedResources* tx = cj_engine_textured(cj_engine_get_current());
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(win->commandBuffers[i], 0, 1, &tx->vertexBuffer, offsets);

  vkCmdBindPipeline(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, tx->pipeline);

  assert(tx->descriptorSet != VK_NULL_HANDLE);
  assert(tx->pipelineLayout != VK_NULL_HANDLE);
  vkCmdBindDescriptorSets(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, tx->pipelineLayout, 0, 1, &tx->descriptorSet, 0, NULL);

    vkCmdDraw(win->commandBuffers[i], 6, 1, 0, 0);
    vkCmdEndRenderPass(win->commandBuffers[i]);

    if (vkEndCommandBuffer(win->commandBuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Error: Failed to record textured (ctx) command buffer %u\n", i);
      // Clean up already allocated command buffers
      VkDevice dev = ctx->device;
      VkCommandPool pool = ctx->commandPool;
      if (dev != VK_NULL_HANDLE && pool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(dev, pool, i + 1, win->commandBuffers);
      }
      free(win->commandBuffers);
      win->commandBuffers = NULL;
      return false;
    }
  }
  return true;
}

/* Per-window bindless command buffer recording using explicit ctx */
static void createBindlessCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyBindlessResources* resources, const CJellyVulkanContext* ctx) {
  if (!win || !ctx || !resources) return;
  if (!ctx->device || !ctx->commandPool || !ctx->renderPass) return;

  if (!resources->pipeline) {
    fprintf(stderr, "Bindless pipeline is NULL, falling back to textured\n");
    /* Fallback to textured recorder if bindless pipeline missing */
    createTexturedCommandBuffersForWindowCtx(win, ctx);
    return;
  }

  win->commandBuffers = (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer) * win->swapChainImageCount);
  if (!win->commandBuffers) {
    fprintf(stderr, "Error: Failed to allocate bindless command buffers\n");
    // Fallback to textured
    if (!createTexturedCommandBuffersForWindowCtx(win, ctx)) {
      fprintf(stderr, "Error: Failed to create textured command buffers (fallback)\n");
    }
    return;
  }

  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = ctx->commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(ctx->device, &allocInfo, win->commandBuffers) != VK_SUCCESS) {
    fprintf(stderr, "Error: Failed to allocate bindless command buffers, falling back to textured (ctx)\n");
    free(win->commandBuffers);
    win->commandBuffers = NULL;
    if (!createTexturedCommandBuffersForWindowCtx(win, ctx)) {
      fprintf(stderr, "Error: Failed to create textured command buffers (fallback)\n");
    }
    return;
  }

  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(win->commandBuffers[i], &beginInfo) != VK_SUCCESS) {
      fprintf(stderr, "Failed to begin bindless command buffer\n");
      exit(EXIT_FAILURE);
    }

    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = ctx->renderPass;
    renderPassInfo.framebuffer = win->swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
    renderPassInfo.renderArea.extent = win->swapChainExtent;
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(win->commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {0};
    viewport.x = 0.0f; viewport.y = 0.0f;
    viewport.width = (float)win->swapChainExtent.width;
    viewport.height = (float)win->swapChainExtent.height;
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
    vkCmdSetViewport(win->commandBuffers[i], 0, 1, &viewport);

    VkRect2D scissor = {0};
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = win->swapChainExtent;
    vkCmdSetScissor(win->commandBuffers[i], 0, 1, &scissor);

    if (ctx->renderPass == VK_NULL_HANDLE) { fprintf(stderr, "ERROR: renderPass is NULL!\n"); exit(EXIT_FAILURE); }
    if (ctx->device == VK_NULL_HANDLE) { fprintf(stderr, "ERROR: device is NULL!\n"); exit(EXIT_FAILURE); }
    if (ctx->commandPool == VK_NULL_HANDLE) { fprintf(stderr, "ERROR: commandPool is NULL!\n"); exit(EXIT_FAILURE); }

    vkCmdBindPipeline(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, resources->pipeline);

    float push[8] = { resources->uv[0], resources->uv[1], resources->uv[2], resources->uv[3],
                      resources->colorMul[0], resources->colorMul[1], resources->colorMul[2], resources->colorMul[3] };
    vkCmdPushConstants(win->commandBuffers[i], resources->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(win->commandBuffers[i], 0, 1, &resources->vertexBuffer, offsets);

    vkCmdDraw(win->commandBuffers[i], 6, 1, 0, 0);

    vkCmdEndRenderPass(win->commandBuffers[i]);

    if (vkEndCommandBuffer(win->commandBuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to record bindless command buffer\n");
      exit(EXIT_FAILURE);
    }
  }
}
