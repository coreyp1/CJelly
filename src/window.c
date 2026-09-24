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

/* Headers */

/* clock_gettime() is POSIX, and the platform headers below are included before
 * cjelly/macros.h gets a chance to ask for it. Declaring the level here, at
 * the top of the file, is the only place it takes effect. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

/* The platform seam, and Vulkan through it. This file no longer names a
 * window system: the surface-creation headers went with the code that
 * created surfaces. It still has to come before anything else that might
 * pull in vulkan.h, because the VK_USE_PLATFORM_* selection inside it only
 * takes effect before vulkan.h is first seen. */
#include <ghoti.io/cjelly/plat_internal.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_log.h>
#include <ghoti.io/cjelly/cj_window.h>
#include <ghoti.io/cjelly/cj_platform.h>
#include <ghoti.io/cjelly/runtime.h>
#include <ghoti.io/cjelly/application.h>
#include <ghoti.io/cjelly/engine_internal.h>
#include <ghoti.io/cjelly/bindless_internal.h>
#include <ghoti.io/cjelly/textured_internal.h>
#include <ghoti.io/cjelly/cj_rgraph.h>
#include <ghoti.io/cjelly/window_internal.h>
#include <ghoti.io/cjelly/cj_input.h>

/* Forward declarations */
typedef struct CJPlatformWindow CJPlatformWindow;

/* Platform window struct - defined early so window procedure can access it */
typedef struct CJPlatformWindow {
  /* The native window, as uintptr_t rather than HWND or Window, so that this
   * struct names no window system and the casts stay in the platform
   * modules. A Window is an XID, an unsigned long; an HWND is a pointer. */
  uintptr_t handle;
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
  VkFormat swapChainFormat;
  /* Which swapchain image was last handed to the presentation engine, and
   * whether anything has been presented yet. */
  uint32_t lastPresentedImage;
  bool hasPresentedImage;

  /* Frame capture.
   *
   * The copy is recorded into the frame that draws the image and submitted
   * with it, while the application still owns that image. Reading it after
   * vkQueuePresentKHR - which is what this did - is a write-after-present
   * hazard: the image belongs to the presentation engine from the moment it
   * is presented until it is acquired again, and nothing says the next
   * acquire returns the same one. It produced a correct-looking screenshot
   * on every driver tried, which is why it survived.
   *
   * So a capture costs a frame: ask on one, read on the next. That is the
   * honest shape of a readback and it is what cj_window_capture() does. */
  VkCommandBuffer captureCommandBuffer;
  VkBuffer captureBuffer;
  VkDeviceMemory captureMemory;
  VkDeviceSize captureCapacity;
  VkFormat captureFormat;
  VkExtent2D captureExtent;
  bool captureRequested;  /**< Asked for; the next frame records the copy. */
  bool captureReady;      /**< Copied, and not yet read. */
  int width;
  int height;
  int updateMode;
  uint32_t fixedFramerate;
  int needsRedraw;
  uint64_t nextFrameTime;
  bool is_minimized;  /* Cached minimized state (updated via window messages) */
  bool needs_swapchain_recreate;  /* Flag to defer swapchain recreation until next frame */

  /* Position and state tracking */
  int32_t x;                    /* Current X position (screen coordinates, logical pixels) */
  int32_t y;                    /* Current Y position (screen coordinates, logical pixels) */
  cj_window_state_t state;      /* Current window state */
  float dpi_scale;              /* DPI scale factor for this window */
  bool is_programmatic_move;    /* True when we're programmatically moving the window (suppress ConfigureNotify feedback) */
  int32_t last_mouse_root_x;    /* Last mouse root X coordinate (for screen-space delta calculation) */
  int32_t last_mouse_root_y;    /* Last mouse root Y coordinate (for screen-space delta calculation) */
  bool has_seen_mouse_move;     /* True if we've seen at least one mouse move event (for delta calculation) */
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
  cj_window_move_callback_t move_callback;  /* Move callback (NULL if none) */
  void* move_callback_user_data; /* User data for move callback */
  cj_window_state_callback_t state_callback;  /* State change callback (NULL if none) */
  void* state_callback_user_data; /* User data for state callback */
  cj_key_callback_t key_callback;  /* Keyboard callback (NULL if none) */
  void* key_callback_user_data; /* User data for keyboard callback */
  /* Key state tracking for repeat detection (X11) and future key state queries (Phase 2) */
  /* Simple bitfield: each bit represents a keycode. Size = (max_keycode + 7) / 8 bytes */
  /* We track keys 0-255 (32 bytes), which covers all current keycodes */
  uint8_t pressed_keys_bitfield[32];  /* Bitfield tracking which keys are currently pressed */
  cj_mouse_callback_t mouse_callback;  /* Mouse callback (NULL if none) */
  void* mouse_callback_user_data; /* User data for mouse callback */
  cj_focus_callback_t focus_callback;  /* Focus callback (NULL if none) */
  void* focus_callback_user_data; /* User data for focus callback */
  /* Mouse state tracking */
  int32_t mouse_x, mouse_y;  /* Current mouse position in window coordinates */
  uint8_t pressed_mouse_buttons;  /* Bitfield: bit 0 = left, bit 1 = middle, bit 2 = right, bit 3 = button 4, bit 4 = button 5 */
  bool has_mouse_capture;  /* True if window has captured mouse input */
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

/* Keycode mapping functions */


/**
 * @brief Convert logical pixels to physical pixels
 * @param logical Logical pixel value
 * @param dpi_scale DPI scale factor
 * @return Physical pixel value
 */
static int32_t logical_to_physical(int32_t logical, float dpi_scale) {
  return (int32_t)(logical * dpi_scale + 0.5f);  // Round to nearest
}


/* === Platform helpers === */
static void plat_createPlatformWindow(CJPlatformWindow * win, const char * title, int width, int height, int32_t x, int32_t y, cj_window_state_t initial_state) {
  if (!win) return;
  win->width = width; win->height = height;
  win->is_minimized = false;  /* Initialize minimized state */
  win->x = x;
  win->y = y;
  win->is_programmatic_move = false;
  win->last_mouse_root_x = 0;
  win->last_mouse_root_y = 0;
  win->has_seen_mouse_move = false;
  win->state = initial_state;
  win->dpi_scale = 1.0f;  /* Replaced below if the window system knows better */

  cj_plat_window_t created = { 0, win->x, win->y, win->dpi_scale };
  cj_plat_create_window(title, width, height, x, y, initial_state, &created);
  win->handle = created.handle;
  win->x = created.x;
  win->y = created.y;
  win->dpi_scale = created.dpi_scale;
}

static void plat_createSurfaceForWindow(CJPlatformWindow * win) {
  if (!win) return;
  cj_plat_create_surface((uintptr_t)win->handle,
      cj_engine_instance(cj_engine_get_current()), &win->surface);
}

static void plat_createSwapChainForWindow(CJPlatformWindow * win) {
  if (!win) return;
  VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(cj_engine_physical_device(cj_engine_get_current()), win->surface, &caps);

  /* Calculate physical size from logical size for swapchain */
  uint32_t physical_width = (uint32_t)logical_to_physical((int32_t)win->width, win->dpi_scale);
  uint32_t physical_height = (uint32_t)logical_to_physical((int32_t)win->height, win->dpi_scale);

  /* Prefer what the surface says it is, which is the drawable area rather
   * than the size that was asked for. The two differ: Win32's CreateWindowEx
   * sizes the whole frame and fits the client area inside it, while X11 sizes
   * the client area and the window manager hangs the decoration outside, so
   * the same window description produces a smaller drawable on Windows than
   * on Linux. 0xFFFFFFFF means the surface has no opinion and the size is
   * ours to choose.
   *
   * The clamp below happened to correct this, because drivers normally report
   * minImageExtent == maxImageExtent == currentExtent for a windowed surface.
   * Depending on that is depending on a coincidence. */
  if (caps.currentExtent.width != 0xFFFFFFFFu) {
    physical_width = caps.currentExtent.width;
    physical_height = caps.currentExtent.height;
  }

  /* Clamp to surface capabilities */
  if (physical_width < caps.minImageExtent.width) physical_width = caps.minImageExtent.width;
  if (physical_width > caps.maxImageExtent.width) physical_width = caps.maxImageExtent.width;
  if (physical_height < caps.minImageExtent.height) physical_height = caps.minImageExtent.height;
  if (physical_height > caps.maxImageExtent.height) physical_height = caps.maxImageExtent.height;

  win->swapChainExtent.width = physical_width;
  win->swapChainExtent.height = physical_height;
  win->swapChainFormat = VK_FORMAT_B8G8R8A8_SRGB;

  /* Reading a frame back needs the swapchain images usable as a transfer
   * source. Every implementation worth the name supports it, but it is
   * optional, so ask only when the surface says yes - requesting an
   * unsupported usage fails swapchain creation outright, and a window that
   * will not open is a bad trade for a capture that may never be taken. */
  VkImageUsageFlags transfer_src_usage =
      (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
      ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT
      : 0;
  VkSwapchainCreateInfoKHR ci = {0}; ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR; ci.surface = win->surface; ci.minImageCount = caps.minImageCount; ci.imageFormat = VK_FORMAT_B8G8R8A8_SRGB; ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; ci.imageExtent = win->swapChainExtent; ci.imageArrayLayers = 1; ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | transfer_src_usage; ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; ci.clipped = VK_TRUE;
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

  /* Calculate physical size from logical size for swapchain */
  uint32_t physical_width = (uint32_t)logical_to_physical((int32_t)win->width, win->dpi_scale);
  uint32_t physical_height = (uint32_t)logical_to_physical((int32_t)win->height, win->dpi_scale);

  /* Clamp to surface capabilities */
  if (physical_width < caps.minImageExtent.width) physical_width = caps.minImageExtent.width;
  if (physical_width > caps.maxImageExtent.width) physical_width = caps.maxImageExtent.width;
  if (physical_height < caps.minImageExtent.height) physical_height = caps.minImageExtent.height;
  if (physical_height > caps.maxImageExtent.height) physical_height = caps.maxImageExtent.height;

  /* Same as on first creation: the surface knows the drawable size, and the
   * requested size does not account for window chrome the same way on every
   * platform. */
  if (caps.currentExtent.width != 0xFFFFFFFFu) {
    physical_width = caps.currentExtent.width;
    physical_height = caps.currentExtent.height;
  }

  win->swapChainExtent.width = physical_width;
  win->swapChainExtent.height = physical_height;
  win->swapChainFormat = VK_FORMAT_B8G8R8A8_SRGB;

  /* A recreated swapchain has to stay readable, or a capture would work until
   * the first resize and then stop. */
  VkImageUsageFlags transfer_src_usage =
      (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
      ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT
      : 0;

  /* Create new swapchain with old swapchain reference */
  VkSwapchainCreateInfoKHR ci = {0};
  ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  ci.surface = win->surface;
  ci.minImageCount = caps.minImageCount;
  ci.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
  ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  ci.imageExtent = win->swapChainExtent;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | transfer_src_usage;
  ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  ci.clipped = VK_TRUE;
  ci.oldSwapchain = oldSwapchain;  /* Reference old swapchain for proper recreation */

  if (vkCreateSwapchainKHR(dev, &ci, NULL, &win->swapChain) != VK_SUCCESS) {
    CJ_ERRORF("Error: Failed to recreate swapchain");
    return;
  }

  /* Destroy old swapchain after creating new one */
  vkDestroySwapchainKHR(dev, oldSwapchain, NULL);

  /* Recreate image views, framebuffers, and command buffers */
  if (!plat_createImageViewsForWindow(win)) {
    CJ_ERRORF("Error: Failed to recreate image views after resize");
    return;
  }
  if (!plat_createFramebuffersForWindow(win)) {
    CJ_ERRORF("Error: Failed to recreate framebuffers after resize");
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
    CJ_ERRORF("Error: Failed to recreate command buffers after resize");
    return;
  }
}

static bool plat_createImageViewsForWindow(CJPlatformWindow * win) {
  if (!win) return false;
  vkGetSwapchainImagesKHR(cj_engine_device(cj_engine_get_current()), win->swapChain, &win->swapChainImageCount, NULL);
  win->swapChainImages = (VkImage*)malloc(sizeof(VkImage)*win->swapChainImageCount);
  if (!win->swapChainImages) {
    CJ_ERRORF("Error: Failed to allocate swapChainImages");
    return false;
  }
  vkGetSwapchainImagesKHR(cj_engine_device(cj_engine_get_current()), win->swapChain, &win->swapChainImageCount, win->swapChainImages);
  win->swapChainImageViews = (VkImageView*)malloc(sizeof(VkImageView)*win->swapChainImageCount);
  if (!win->swapChainImageViews) {
    CJ_ERRORF("Error: Failed to allocate swapChainImageViews");
    free(win->swapChainImages);
    win->swapChainImages = NULL;
    return false;
  }
  for (uint32_t i=0;i<win->swapChainImageCount;i++) {
    VkImageViewCreateInfo vi = {0}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vi.image = win->swapChainImages[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_B8G8R8A8_SRGB; vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(cj_engine_device(cj_engine_get_current()), &vi, NULL, &win->swapChainImageViews[i]) != VK_SUCCESS) {
      CJ_ERRORF("Error: Failed to create image view %u", i);
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
    CJ_ERRORF("Error: Failed to allocate swapChainFramebuffers");
    return false;
  }
  VkDevice dev = cj_engine_device(cj_engine_get_current());
  for (uint32_t i=0;i<win->swapChainImageCount;i++) {
    VkImageView attachments[] = { win->swapChainImageViews[i] };
    VkFramebufferCreateInfo fi = {0}; fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fi.renderPass = cj_engine_render_pass(cj_engine_get_current()); fi.attachmentCount = 1; fi.pAttachments = attachments; fi.width = win->swapChainExtent.width; fi.height = win->swapChainExtent.height; fi.layers = 1;
    if (vkCreateFramebuffer(dev, &fi, NULL, &win->swapChainFramebuffers[i]) != VK_SUCCESS) {
      CJ_ERRORF("Error: Failed to create framebuffer %u", i);
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

/* ---------------------------------------------------------------- capture */

/** Find a memory type satisfying `properties`, or UINT32_MAX. */
static uint32_t plat_captureMemoryType(VkPhysicalDevice physical_device,
    uint32_t type_bits, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memory = {0};
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory);
  for (uint32_t i = 0; i < memory.memoryTypeCount; i++) {
    if ((type_bits & (1u << i))
        && (memory.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  return UINT32_MAX;
}

/** Release the staging buffer and its memory. Safe to call twice. */
static void plat_releaseCaptureBuffer(CJPlatformWindow * win, VkDevice dev) {
  if (!win || dev == VK_NULL_HANDLE) return;
  if (win->captureBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(dev, win->captureBuffer, NULL);
    win->captureBuffer = VK_NULL_HANDLE;
  }
  if (win->captureMemory != VK_NULL_HANDLE) {
    vkFreeMemory(dev, win->captureMemory, NULL);
    win->captureMemory = VK_NULL_HANDLE;
  }
  win->captureCapacity = 0;
  win->captureReady = false;
}

/**
 * Make sure there is a staging buffer big enough for this window's frame,
 * and a command buffer to record the copy into.
 *
 * Both are kept between captures rather than allocated per frame: a capture
 * is usually either never taken or taken every frame, and the second case is
 * the one that would notice.
 */
static bool plat_ensureCaptureBuffer(CJPlatformWindow * win) {
  if (!win) return false;
  cj_engine_t * engine = cj_engine_get_current();
  VkDevice dev = cj_engine_device(engine);
  VkPhysicalDevice phys = cj_engine_physical_device(engine);
  VkCommandPool pool = cj_engine_command_pool(engine);
  if (dev == VK_NULL_HANDLE || pool == VK_NULL_HANDLE) return false;
  if (win->swapChainExtent.width == 0 || win->swapChainExtent.height == 0) {
    return false;
  }

  VkDeviceSize needed = (VkDeviceSize)win->swapChainExtent.width
      * win->swapChainExtent.height * 4u;

  if (win->captureBuffer != VK_NULL_HANDLE && win->captureCapacity >= needed) {
    /* Already big enough - a window that shrank keeps the larger buffer. */
  }
  else {
    /* A resize went through, so whatever was in there describes the old
     * size. Nothing has read it, or captureReady would be clear. */
    plat_releaseCaptureBuffer(win, dev);

    VkBufferCreateInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = needed;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bi, NULL, &win->captureBuffer) != VK_SUCCESS) {
      win->captureBuffer = VK_NULL_HANDLE;
      return false;
    }

    VkMemoryRequirements req = {0};
    vkGetBufferMemoryRequirements(dev, win->captureBuffer, &req);
    uint32_t type = plat_captureMemoryType(phys, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) {
      plat_releaseCaptureBuffer(win, dev);
      return false;
    }

    VkMemoryAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(dev, &ai, NULL, &win->captureMemory) != VK_SUCCESS) {
      plat_releaseCaptureBuffer(win, dev);
      return false;
    }
    if (vkBindBufferMemory(dev, win->captureBuffer, win->captureMemory, 0)
        != VK_SUCCESS) {
      plat_releaseCaptureBuffer(win, dev);
      return false;
    }
    win->captureCapacity = needed;
  }

  if (win->captureCommandBuffer == VK_NULL_HANDLE) {
    VkCommandBufferAllocateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ci.commandPool = pool;
    ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ci.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(dev, &ci, &win->captureCommandBuffer)
        != VK_SUCCESS) {
      win->captureCommandBuffer = VK_NULL_HANDLE;
      return false;
    }
  }
  return true;
}

/** Move a swapchain image between the presentable and readable layouts. */
static void plat_captureTransition(VkCommandBuffer cmd, VkImage image,
    VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
    VkAccessFlags dst_access) {
  VkImageMemoryBarrier barrier = {0};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = src_access;
  barrier.dstAccessMask = dst_access;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
}

/**
 * Record this frame's copy, if one was asked for.
 *
 * Returns the command buffer to submit after the frame's own, or
 * VK_NULL_HANDLE when there is nothing to do. Submitting it in the same
 * vkQueueSubmit as the frame is what keeps the image owned: the two run in
 * order on the queue, and the present that follows has not happened yet.
 */
static VkCommandBuffer plat_recordCaptureForFrame(
    CJPlatformWindow * win, uint32_t imageIndex) {
  if (!win || !win->captureRequested) return VK_NULL_HANDLE;
  if (!win->swapChainImages || imageIndex >= win->swapChainImageCount) {
    return VK_NULL_HANDLE;
  }
  if (!plat_ensureCaptureBuffer(win)) {
    /* Asked for and cannot be done. Drop the request rather than retrying
     * every frame for as long as the program runs. */
    win->captureRequested = false;
    CJ_WARNF("capture: no staging buffer could be made for this window");
    return VK_NULL_HANDLE;
  }

  VkImage image = win->swapChainImages[imageIndex];
  VkCommandBuffer cmd = win->captureCommandBuffer;

  VkCommandBufferBeginInfo begin = {0};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
    win->captureRequested = false;
    return VK_NULL_HANDLE;
  }

  /* The render pass leaves the image in PRESENT_SRC, which is where the
   * frame's own command buffer stops. */
  plat_captureTransition(cmd, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_READ_BIT,
      VK_ACCESS_TRANSFER_READ_BIT);

  VkBufferImageCopy region = {0};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = win->swapChainExtent.width;
  region.imageExtent.height = win->swapChainExtent.height;
  region.imageExtent.depth = 1;
  vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      win->captureBuffer, 1, &region);

  /* Back to PRESENT_SRC before the present that follows: this command buffer
   * is submitted ahead of it, so the image has to be presentable again by the
   * time it finishes. */
  plat_captureTransition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT,
      VK_ACCESS_MEMORY_READ_BIT);

  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    win->captureRequested = false;
    return VK_NULL_HANDLE;
  }

  /* Recorded against this frame's size and format, which is what the reader
   * has to unpack it with - not whatever the window is by the time it looks. */
  win->captureFormat = win->swapChainFormat;
  win->captureExtent = win->swapChainExtent;
  win->captureRequested = false;
  win->captureReady = true;
  return cmd;
}

static void plat_drawFrameForWindow(CJPlatformWindow * win) {
  if (!win) return;
  VkDevice dev = cj_engine_device(cj_engine_get_current());
  /* Skip the draw if the window is already gone, where the window system
   * can say so. */
  if (!cj_plat_window_is_alive((uintptr_t)win->handle)) return;
  vkWaitForFences(dev, 1, &win->inFlightFence, VK_TRUE, UINT64_MAX);
  vkResetFences(dev, 1, &win->inFlightFence);
  uint32_t imageIndex; vkAcquireNextImageKHR(dev, win->swapChain, UINT64_MAX, win->imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
  VkSemaphore waitS[] = { win->imageAvailableSemaphore }; VkPipelineStageFlags stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  /* Two at most: this frame's, then the copy of what it drew. */
  VkCommandBuffer submitted[2] = { win->commandBuffers[imageIndex], VK_NULL_HANDLE };
  uint32_t submittedCount = 1;
  VkCommandBuffer captureCmd = plat_recordCaptureForFrame(win, imageIndex);
  if (captureCmd != VK_NULL_HANDLE) submitted[submittedCount++] = captureCmd;
  VkSubmitInfo si = {0}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.waitSemaphoreCount = 1; si.pWaitSemaphores = waitS; si.pWaitDstStageMask = stages; si.commandBufferCount = submittedCount; si.pCommandBuffers = submitted; VkSemaphore sigS[] = { win->renderFinishedSemaphore }; si.signalSemaphoreCount = 1; si.pSignalSemaphores = sigS;
  vkQueueSubmit(cj_engine_graphics_queue(cj_engine_get_current()), 1, &si, win->inFlightFence);
  VkPresentInfoKHR pi = {0}; pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = sigS; pi.swapchainCount = 1; pi.pSwapchains = &win->swapChain; pi.pImageIndices = &imageIndex; vkQueuePresentKHR(cj_engine_present_queue(cj_engine_get_current()), &pi);
  win->lastPresentedImage = imageIndex;
  win->hasPresentedImage = true;
}

static void plat_cleanupWindow(CJPlatformWindow * win) {
  if (!win) return;
  VkDevice dev = cj_engine_device(cj_engine_get_current()); VkInstance inst = cj_engine_instance(cj_engine_get_current()); VkCommandPool pool = cj_engine_command_pool(cj_engine_get_current());

  // Wait for device to be idle before destroying resources
  if (dev) vkDeviceWaitIdle(dev);

  // Destroy Vulkan resources
  /* The capture staging buffer first, and its command buffer with the pool's
   * other ones below - the buffer has to go before the memory it is bound to,
   * which is what plat_releaseCaptureBuffer gets in the right order. */
  if (dev && win->captureCommandBuffer) {
    vkFreeCommandBuffers(dev, pool, 1, &win->captureCommandBuffer);
    win->captureCommandBuffer = VK_NULL_HANDLE;
  }
  plat_releaseCaptureBuffer(win, dev);
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

  /* The native window itself is destroyed by cj_window_destroy, once this
   * has finished with its Vulkan objects. X11 used to do it here and Win32
   * one call later; those are the same point in the sequence, and it is now
   * written once. */
  win->handle = 0;
}
/* C library and cjelly headers */
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ghoti.io/cjelly/cj_window.h>
#include <ghoti.io/cjelly/cj_platform.h>
#include <ghoti.io/cjelly/runtime.h>
#include <ghoti.io/cjelly/engine_internal.h>
#include <ghoti.io/cjelly/bindless_internal.h>
#include <ghoti.io/cjelly/textured_internal.h>

/* textured pipeline helper (defined in cjelly.c) */
void cjelly_init_textured_pipeline_ctx(const CJellyVulkanContext* ctx);


/* Internal helpers (static) */
static void plat_createPlatformWindow(CJPlatformWindow * win, const char * title, int width, int height, int32_t x, int32_t y, cj_window_state_t initial_state);
static void plat_createSurfaceForWindow(CJPlatformWindow * win);
static void plat_createSwapChainForWindow(CJPlatformWindow * win);
static void plat_recreateSwapChainForWindow(CJPlatformWindow * win);
static bool plat_createImageViewsForWindow(CJPlatformWindow * win);
static bool plat_createFramebuffersForWindow(CJPlatformWindow * win);
static bool createTexturedCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyVulkanContext* ctx);
static void plat_createSyncObjectsForWindow(CJPlatformWindow * win);
static void plat_drawFrameForWindow(CJPlatformWindow * win);

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
  int32_t x = desc->x;
  int32_t y = desc->y;
  cj_window_state_t initial_state = desc->initial_state;
  /* If x/y are uninitialized (0) and not explicitly set, use default position */
  /* Note: We can't distinguish between uninitialized 0 and explicit (0,0), so we assume 0 means default */
  /* Users who want (0,0) can set it after creation */
  if (x == 0) {
    x = CJ_WINDOW_POSITION_DEFAULT;
  }
  if (y == 0) {
    y = CJ_WINDOW_POSITION_DEFAULT;
  }
  /* If initial_state is 0 (uninitialized), use normal */
  if (initial_state == 0) {
    initial_state = CJ_WINDOW_STATE_NORMAL;
  }
  plat_createPlatformWindow(win->plat, title, (int)desc->width, (int)desc->height, x, y, initial_state);
  plat_createSurfaceForWindow(win->plat);
  plat_createSwapChainForWindow(win->plat);
  if (!plat_createImageViewsForWindow(win->plat)) {
    CJ_ERRORF("Error: Failed to create image views for window");
    cj_window_destroy(win);
    return NULL;
  }
  if (!plat_createFramebuffersForWindow(win->plat)) {
    CJ_ERRORF("Error: Failed to create framebuffers for window");
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
    CJ_ERRORF("Error: Failed to create textured command buffers for window");
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
  win->key_callback = NULL;
  win->key_callback_user_data = NULL;
  memset(win->pressed_keys_bitfield, 0, sizeof(win->pressed_keys_bitfield));  /* Initialize key state tracking */
  win->mouse_x = 0;
  win->mouse_y = 0;
  win->pressed_mouse_buttons = 0;
  win->has_mouse_capture = false;
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
    CJ_ERRORF("Error: Failed to register window with application (out of memory). Destroying window.");
    // Clean up the window we just created (use destroy function for proper cleanup)
    // Note: is_destroyed is false, so destroy will proceed, but unregister will be a no-op
    cj_window_destroy(win);
    return NULL;
  }

  /* So the platform can find this window again during teardown, where the
   * window system has somewhere to put it. */
  cj_plat_bind_window_user_data((uintptr_t)win->plat->handle, win);

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
  /* Clear key state tracking */
  memset(win->pressed_keys_bitfield, 0, sizeof(win->pressed_keys_bitfield));
  win->pressed_mouse_buttons = 0;
  win->has_mouse_capture = false;

  if (win->plat) {
    // Wait for GPU to finish before destroying resources
    VkDevice dev = cj_engine_device(cj_engine_get_current());
    if (dev) {
      vkDeviceWaitIdle(dev);
    }

    /* Saved because plat_cleanupWindow clears it. */
    uintptr_t native = (uintptr_t)win->plat->handle;
    cj_plat_unbind_window_user_data(native);

    // Clean up Vulkan resources (swapchain, surfaces, etc.)
    plat_cleanupWindow(win->plat);

    cj_plat_destroy_native_window(native);

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

/* Milliseconds from a monotonic clock, for render-graph nodes that animate.
 * Monotonic rather than wall clock: a clock adjustment must not make a model
 * jump or run backwards. */
static uint64_t cj_window_now_ms(void) {
  return cj_plat_now_ms();
}

void cj_window__request_capture(cj_window_t* window) {
  if (!window || window->is_destroyed || !window->plat) return;
  window->plat->captureRequested = true;
  /* A window that renders only when something changed would otherwise sit
   * there with the request pending and never take a frame to serve it. */
  window->plat->needsRedraw = 1;
}

bool cj_window__take_capture(
    cj_window_t* window, cj_window_readback_t* out_readback) {
  if (!window || !out_readback || !window->plat) return false;
  CJPlatformWindow* plat = window->plat;
  if (!plat->captureReady || plat->captureMemory == VK_NULL_HANDLE) {
    return false;
  }

  out_readback->memory = plat->captureMemory;
  out_readback->size =
      (VkDeviceSize)plat->captureExtent.width * plat->captureExtent.height * 4u;
  out_readback->format = plat->captureFormat;
  out_readback->extent = plat->captureExtent;
  /* Handed over. Holding it would mean a second call to cj_window_capture()
   * returning the same frame and looking like a window that stopped
   * animating. */
  plat->captureReady = false;
  return true;
}

CJ_API cj_result_t cj_window_execute(cj_window_t* win) {
  if (!win || win->is_destroyed || !win->plat) return CJ_E_INVALID_ARGUMENT;

  /* Critical: the window may already be gone, and its Vulkan objects with
   * it. Where the window system can say, ask it. */
  if (!cj_plat_window_is_alive((uintptr_t)win->plat->handle)) {
    return CJ_E_INVALID_ARGUMENT;
  }

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
        CJ_ERRORF("WINDOWS FIX: Failed to begin command buffer for render graph");
        /* Fall back to legacy drawing if command buffer begin fails */
        plat_drawFrameForWindow(win->plat);
        return CJ_SUCCESS;
      }

      /* Anything that renders into its own target has to be recorded before
       * the window's render pass begins, because a render pass cannot be
       * nested inside another. A graph with no such nodes records nothing. */
      cj_rgraph_execute_prepass(win->render_graph, cmd, cj_window_now_ms(), extent);

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
        CJ_ERRORF("WINDOWS FIX: Failed to end command buffer for render graph");
        /* Fall back to legacy drawing if command buffer end fails */
        plat_drawFrameForWindow(win->plat);
        return CJ_SUCCESS;
      }

      if (result != CJ_SUCCESS) {
        CJ_WARNF("WINDOWS FIX: Render graph execution failed (result=%d), falling back to legacy", result);
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
        /* Two at most: this frame's, then the copy of what it drew. The
         * copy has to travel with the frame - after the present below the
         * image is the presentation engine's and reading it is a hazard. */
        VkCommandBuffer submitted[2] = { win->plat->commandBuffers[imageIndex], VK_NULL_HANDLE };
        uint32_t submittedCount = 1;
        VkCommandBuffer captureCmd = plat_recordCaptureForFrame(win->plat, imageIndex);
        if (captureCmd != VK_NULL_HANDLE) submitted[submittedCount++] = captureCmd;
        si.commandBufferCount = submittedCount;
        si.pCommandBuffers = submitted;
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
        win->plat->lastPresentedImage = imageIndex;
        win->plat->hasPresentedImage = true;
      }
    } else {
      /* Fall back to legacy drawing if no command buffers available */
      plat_drawFrameForWindow(win->plat);
    }
  } else {
    /* Legacy path: direct drawing */
    CJ_DEBUGF("DEBUG: Using legacy rendering path");
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

CJ_API void cj_window_get_position(const cj_window_t* window, int32_t* out_x, int32_t* out_y) {
  if (!window || !window->plat) {
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    return;
  }
  /* The window may have been moved by the user or the window manager. Where
   * the window system will say so, refresh the cache first; where it will
   * not, the cache is what we have. */
  int32_t px = 0, py = 0;
  if (cj_plat_query_position((uintptr_t)window->plat->handle, &px, &py)) {
    window->plat->x = px;
    window->plat->y = py;
  }
  if (out_x) *out_x = window->plat->x;
  if (out_y) *out_y = window->plat->y;
}

CJ_API cj_window_state_t cj_window_get_state(const cj_window_t* window) {
  if (!window || !window->plat) return CJ_WINDOW_STATE_NORMAL;
  cj_window_state_t st = CJ_WINDOW_STATE_NORMAL;
  if (cj_plat_query_state((uintptr_t)window->plat->handle, &st)) {
    window->plat->state = st;
  }
  return window->plat->state;
}

CJ_API float cj_window_get_dpi_scale(const cj_window_t* window) {
  if (!window || !window->plat) return 1.0f;
  return window->plat->dpi_scale;
}

CJ_API bool cj_window_is_high_dpi(const cj_window_t* window) {
  if (!window || !window->plat) return false;
  return window->plat->dpi_scale > 1.0f;
}

CJ_API cj_result_t cj_window_set_position(cj_window_t* window, int32_t x, int32_t y) {
  if (!window || !window->plat || window->is_destroyed) return CJ_E_INVALID_ARGUMENT;
  /* x,y are CLIENT coordinates on both platforms. What the window system
   * needs doing to them, and whether it will report the move back to us, is
   * its own business. */
  if (cj_plat_move_window((uintptr_t)window->plat->handle, x, y,
          window->plat->x, window->plat->y)) {
    cj_window__set_programmatic_move(window, true);
  }
  window->plat->x = x;
  window->plat->y = y;
  return CJ_SUCCESS;
}

CJ_API cj_result_t cj_window_set_state(cj_window_t* window, cj_window_state_t state) {
  if (!window || !window->plat || window->is_destroyed) return CJ_E_INVALID_ARGUMENT;
  cj_result_t r = cj_plat_set_window_state((uintptr_t)window->plat->handle, state);
  /* The state-change callback is dispatched by the platform's own event
   * handler, not here. */
  if (r == CJ_SUCCESS) window->plat->state = state;
  return r;
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

CJ_API void cj_window_on_move(cj_window_t* window,
                              cj_window_move_callback_t callback,
                              void* user_data) {
  if (!window) return;
  window->move_callback = callback;
  window->move_callback_user_data = user_data;
}

CJ_API void cj_window_on_state_change(cj_window_t* window,
                                      cj_window_state_callback_t callback,
                                      void* user_data) {
  if (!window) return;
  window->state_callback = callback;
  window->state_callback_user_data = user_data;
}

CJ_API void cj_window_on_key(cj_window_t* window,
                             cj_key_callback_t callback,
                             void* user_data) {
  if (!window) return;
  window->key_callback = callback;
  window->key_callback_user_data = user_data;
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

/* Internal helper to dispatch move callback. */
void cj_window__dispatch_move_callback(cj_window_t* window, int32_t new_x, int32_t new_y) {
  if (!window || window->is_destroyed || !window->plat) return;
  if (window->move_callback) {
    window->move_callback(window, new_x, new_y, window->move_callback_user_data);
  }
}

/* Internal helper to dispatch state change callback. */
void cj_window__dispatch_state_callback(cj_window_t* window, cj_window_state_t new_state) {
  if (!window || window->is_destroyed || !window->plat) return;
  if (window->state_callback) {
    window->state_callback(window, new_state, window->state_callback_user_data);
  }
}

/* Internal helper functions for key state tracking (for repeat detection on X11) */
bool cj_window__is_key_pressed(cj_window_t* window, cj_keycode_t keycode) {
  if (!window || keycode < 0 || keycode >= 256) return false;
  uint8_t byte_idx = (uint8_t)(keycode / 8);
  uint8_t bit_idx = (uint8_t)(keycode % 8);
  return (window->pressed_keys_bitfield[byte_idx] & (1 << bit_idx)) != 0;
}

void cj_window__set_key_pressed(cj_window_t* window, cj_keycode_t keycode, bool pressed) {
  if (!window || keycode < 0 || keycode >= 256) return;
  uint8_t byte_idx = (uint8_t)(keycode / 8);
  uint8_t bit_idx = (uint8_t)(keycode % 8);
  if (pressed) {
    window->pressed_keys_bitfield[byte_idx] |= (1 << bit_idx);
  } else {
    window->pressed_keys_bitfield[byte_idx] &= ~(1 << bit_idx);
  }
}

/* Internal helper to dispatch keyboard callback. */
void cj_window__dispatch_key_callback(cj_window_t* window,
                                     cj_keycode_t keycode,
                                     cj_scancode_t scancode,
                                     cj_key_action_t action,
                                     cj_modifiers_t modifiers,
                                     bool is_repeat) {
  if (!window || window->is_destroyed) return;

  if (window->key_callback) {
    cj_key_event_t event = {0};
    event.keycode = keycode;
    event.scancode = scancode;
    event.action = action;
    event.modifiers = modifiers;
    event.is_repeat = is_repeat;

    window->key_callback(window, &event, window->key_callback_user_data);
  }
}

/* Mouse callback registration */
CJ_API void cj_window_on_mouse(cj_window_t* window, cj_mouse_callback_t callback, void* user_data) {
  if (!window) return;
  window->mouse_callback = callback;
  window->mouse_callback_user_data = user_data;
}

/* Focus callback registration */
CJ_API void cj_window_on_focus(cj_window_t* window, cj_focus_callback_t callback, void* user_data) {
  if (!window) return;
  window->focus_callback = callback;
  window->focus_callback_user_data = user_data;
}

/* Internal helper functions for mouse button state tracking */
bool cj_window__is_mouse_button_pressed(cj_window_t* window, cj_mouse_button_t button) {
  if (!window || button > CJ_MOUSE_BUTTON_5) return false;
  return (window->pressed_mouse_buttons & (1 << button)) != 0;
}

void cj_window__set_mouse_button_pressed(cj_window_t* window, cj_mouse_button_t button, bool pressed) {
  if (!window || button > CJ_MOUSE_BUTTON_5) return;
  if (pressed) {
    window->pressed_mouse_buttons |= (1 << button);
  } else {
    window->pressed_mouse_buttons &= ~(1 << button);
  }
}

/* Internal helper to dispatch mouse callback. */
void cj_window__dispatch_mouse_callback(cj_window_t* window, const cj_mouse_event_t* event) {
  if (!window || window->is_destroyed || !event) return;

  /* Update mouse position for MOVE events */
  if (event->type == CJ_MOUSE_MOVE || event->type == CJ_MOUSE_BUTTON_DOWN || event->type == CJ_MOUSE_BUTTON_UP) {
    window->mouse_x = event->x;
    window->mouse_y = event->y;
  }

  /* Update button state */
  if (event->type == CJ_MOUSE_BUTTON_DOWN) {
    cj_window__set_mouse_button_pressed(window, event->button, true);
  } else if (event->type == CJ_MOUSE_BUTTON_UP) {
    cj_window__set_mouse_button_pressed(window, event->button, false);
  }

  if (window->mouse_callback) {
    window->mouse_callback(window, event, window->mouse_callback_user_data);
  }
}

/* Internal helper to dispatch focus callback. */
void cj_window__dispatch_focus_callback(cj_window_t* window, cj_focus_action_t action) {
  if (!window || window->is_destroyed) return;

  if (action == CJ_FOCUS_LOST) {
    /* Clear all input state on focus loss */
    cj_window__clear_input_state(window);
  }

  if (window->focus_callback) {
    cj_focus_event_t event = {0};
    event.action = action;
    window->focus_callback(window, &event, window->focus_callback_user_data);
  }
}

/* Internal helper to clear all input state (keys and mouse buttons) on focus loss. */
void cj_window__clear_input_state(cj_window_t* window) {
  if (!window) return;
  memset(window->pressed_keys_bitfield, 0, sizeof(window->pressed_keys_bitfield));
  window->pressed_mouse_buttons = 0;
}

/* Internal helper to get current mouse position (for calculating deltas). */
void cj_window__get_position(cj_window_t* window, int32_t* out_x, int32_t* out_y) {
  if (!window || !window->plat) {
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    return;
  }
  if (out_x) *out_x = window->plat->x;
  if (out_y) *out_y = window->plat->y;
}

void cj_window__set_position(cj_window_t* window, int32_t x, int32_t y) {
  if (!window || !window->plat) return;
  window->plat->x = x;
  window->plat->y = y;
}

cj_window_state_t cj_window__get_state(cj_window_t* window) {
  if (!window || !window->plat) return CJ_WINDOW_STATE_NORMAL;
  return window->plat->state;
}

void cj_window__set_state(cj_window_t* window, cj_window_state_t state) {
  if (!window || !window->plat) return;
  window->plat->state = state;
}

float cj_window__get_dpi_scale(cj_window_t* window) {
  if (!window || !window->plat) return 1.0f;
  return window->plat->dpi_scale;
}

void cj_window__set_dpi_scale(cj_window_t* window, float dpi_scale) {
  if (!window || !window->plat) return;
  window->plat->dpi_scale = dpi_scale;
}

void cj_window__mark_swapchain_for_recreation(cj_window_t* window) {
  if (!window || !window->plat) return;
  window->plat->needs_swapchain_recreate = true;
}

void cj_window__get_mouse_position(cj_window_t* window, int32_t* out_x, int32_t* out_y) {
  if (!window) {
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    return;
  }
  if (out_x) *out_x = window->mouse_x;
  if (out_y) *out_y = window->mouse_y;
}

bool cj_window__is_programmatic_move(cj_window_t* window) {
  if (!window || !window->plat) return false;
  return window->plat->is_programmatic_move;
}

void cj_window__set_programmatic_move(cj_window_t* window, bool is_programmatic) {
  if (!window || !window->plat) return;
  window->plat->is_programmatic_move = is_programmatic;
}

void cj_window__update_mouse_root(cj_window_t* window, int32_t root_x, int32_t root_y) {
  if (!window || !window->plat) return;
  window->plat->last_mouse_root_x = root_x;
  window->plat->last_mouse_root_y = root_y;
  window->plat->has_seen_mouse_move = true;
}

/** Internal helper to reset mouse root tracking (call when starting drag).
 *  @param window The window to reset.
 */
void cj_window__reset_mouse_root(cj_window_t* window) {
  if (!window || !window->plat) return;
  window->plat->has_seen_mouse_move = false;
  window->plat->last_mouse_root_x = 0;
  window->plat->last_mouse_root_y = 0;
}

void cj_window__get_mouse_root(cj_window_t* window, int32_t* out_root_x, int32_t* out_root_y, bool* out_has_seen_move) {
  if (!window || !window->plat) {
    if (out_root_x) *out_root_x = 0;
    if (out_root_y) *out_root_y = 0;
    if (out_has_seen_move) *out_has_seen_move = false;
    return;
  }
  if (out_root_x) *out_root_x = window->plat->last_mouse_root_x;
  if (out_root_y) *out_root_y = window->plat->last_mouse_root_y;
  if (out_has_seen_move) *out_has_seen_move = window->plat->has_seen_mouse_move;
}

/* Mouse state polling functions */
CJ_API void cj_mouse_get_position(cj_window_t* window, int32_t* out_x, int32_t* out_y) {
  if (!window) {
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    return;
  }
  if (out_x) *out_x = window->mouse_x;
  if (out_y) *out_y = window->mouse_y;
}

CJ_API bool cj_mouse_button_is_pressed(cj_window_t* window, cj_mouse_button_t button) {
  if (!window) return false;
  return cj_window__is_mouse_button_pressed(window, button);
}

/* Mouse capture functions */
CJ_API void cj_window_capture_mouse(cj_window_t* window) {
  if (!window || !window->plat || window->is_destroyed) return;
  if (cj_plat_capture_mouse((uintptr_t)window->plat->handle)) {
    window->has_mouse_capture = true;
  }
}

CJ_API void cj_window_release_mouse(cj_window_t* window) {
  if (!window || !window->plat || window->is_destroyed) return;
  if (!window->has_mouse_capture) return;
  if (cj_plat_release_mouse()) {
    window->has_mouse_capture = false;
  }
}

CJ_API bool cj_window_has_mouse_capture(cj_window_t* window) {
  if (!window) return false;
  return window->has_mouse_capture;
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

/*
 * Render a single frame immediately, bypassing the event loop. Windows calls
 * this from its modal resize loop, which does not return to the event loop
 * until the drag finishes, so without it the window stops drawing while it
 * is being resized.
 *
 * The body is portable and the caller is not; it lives here rather than in
 * the Win32 module because it needs the platform window struct and the
 * swapchain helpers, which are window.c's.
 */
void cj_window__render_frame_immediate(cj_window_t* window) {
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
    CJ_ERRORF("Error: Failed to allocate command buffers");
    return false;
  }

  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = ctx->commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(ctx->device, &allocInfo, win->commandBuffers) != VK_SUCCESS) {
    CJ_ERRORF("Error: Failed to allocate textured (ctx) command buffers");
    free(win->commandBuffers);
    win->commandBuffers = NULL;
    return false;
  }

  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(win->commandBuffers[i], &beginInfo) != VK_SUCCESS) {
      CJ_ERRORF("Failed to begin textured (ctx) command buffer");
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
      CJ_ERRORF("Error: Failed to record textured (ctx) command buffer %u", i);
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
    CJ_WARNF("Bindless pipeline is NULL, falling back to textured");
    /* Fallback to textured recorder if bindless pipeline missing */
    createTexturedCommandBuffersForWindowCtx(win, ctx);
    return;
  }

  win->commandBuffers = (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer) * win->swapChainImageCount);
  if (!win->commandBuffers) {
    CJ_ERRORF("Error: Failed to allocate bindless command buffers");
    // Fallback to textured
    if (!createTexturedCommandBuffersForWindowCtx(win, ctx)) {
      CJ_ERRORF("Error: Failed to create textured command buffers (fallback)");
    }
    return;
  }

  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = ctx->commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(ctx->device, &allocInfo, win->commandBuffers) != VK_SUCCESS) {
    CJ_WARNF("Error: Failed to allocate bindless command buffers, falling back to textured (ctx)");
    free(win->commandBuffers);
    win->commandBuffers = NULL;
    if (!createTexturedCommandBuffersForWindowCtx(win, ctx)) {
      CJ_ERRORF("Error: Failed to create textured command buffers (fallback)");
    }
    return;
  }

  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(win->commandBuffers[i], &beginInfo) != VK_SUCCESS) {
      CJ_ERRORF("Failed to begin bindless command buffer");
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

    if (ctx->renderPass == VK_NULL_HANDLE) { CJ_ERRORF("ERROR: renderPass is NULL!"); exit(EXIT_FAILURE); }
    if (ctx->device == VK_NULL_HANDLE) { CJ_ERRORF("ERROR: device is NULL!"); exit(EXIT_FAILURE); }
    if (ctx->commandPool == VK_NULL_HANDLE) { CJ_ERRORF("ERROR: commandPool is NULL!"); exit(EXIT_FAILURE); }

    vkCmdBindPipeline(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, resources->pipeline);

    float push[8] = { resources->uv[0], resources->uv[1], resources->uv[2], resources->uv[3],
                      resources->colorMul[0], resources->colorMul[1], resources->colorMul[2], resources->colorMul[3] };
    vkCmdPushConstants(win->commandBuffers[i], resources->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(win->commandBuffers[i], 0, 1, &resources->vertexBuffer, offsets);

    vkCmdDraw(win->commandBuffers[i], 6, 1, 0, 0);

    vkCmdEndRenderPass(win->commandBuffers[i]);

    if (vkEndCommandBuffer(win->commandBuffers[i]) != VK_SUCCESS) {
      CJ_ERRORF("Failed to record bindless command buffer");
      exit(EXIT_FAILURE);
    }
  }
}
