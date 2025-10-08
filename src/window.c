/* Headers */
/* previous duplicate header block removed; consolidated above */

/* Platform includes */
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <cjelly/runtime.h>
#endif

#ifdef _WIN32
/* Minimal Win32 window proc: on close, mark app should close and destroy window */
static LRESULT CALLBACK CjWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_CLOSE:
      cj_set_should_close(1);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      return 0;
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}
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
#include <cjelly/engine_internal.h>
#include <cjelly/bindless_internal.h>
#include <cjelly/textured_internal.h>

/* Platform window struct */
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
} CJPlatformWindow;

/* === Platform helpers (migrated) === */
typedef struct CJPlatformWindow CJPlatformWindow;
static void plat_createPlatformWindow(CJPlatformWindow * win, const char * title, int width, int height) {
  if (!win) return;
  win->width = width; win->height = height;
#ifdef _WIN32
  HINSTANCE hInstance = GetModuleHandle(NULL);
  WNDCLASS wc = {0};
  wc.lpfnWndProc = CjWndProc; wc.hInstance = hInstance; wc.lpszClassName = "CJellyWindow";
  RegisterClass(&wc);
  win->handle = CreateWindowEx(0, "CJellyWindow", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, hInstance, NULL);
  ShowWindow(win->handle, SW_SHOW);
#else
  int screen = DefaultScreen(display);
  win->handle = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, (unsigned)width, (unsigned)height, 1, BlackPixel(display, screen), WhitePixel(display, screen));
  XSelectInput(display, win->handle, StructureNotifyMask | KeyPressMask | ExposureMask);
  Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XStoreName(display, win->handle, title);
  XSetWMProtocols(display, win->handle, &wmDelete, 1);
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

static void plat_createImageViewsForWindow(CJPlatformWindow * win) {
  if (!win) return;
  vkGetSwapchainImagesKHR(cj_engine_device(cj_engine_get_current()), win->swapChain, &win->swapChainImageCount, NULL);
  win->swapChainImages = (VkImage*)malloc(sizeof(VkImage)*win->swapChainImageCount);
  vkGetSwapchainImagesKHR(cj_engine_device(cj_engine_get_current()), win->swapChain, &win->swapChainImageCount, win->swapChainImages);
  win->swapChainImageViews = (VkImageView*)malloc(sizeof(VkImageView)*win->swapChainImageCount);
  for (uint32_t i=0;i<win->swapChainImageCount;i++) {
    VkImageViewCreateInfo vi = {0}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vi.image = win->swapChainImages[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_B8G8R8A8_SRGB; vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    vkCreateImageView(cj_engine_device(cj_engine_get_current()), &vi, NULL, &win->swapChainImageViews[i]);
  }
}

static void plat_createFramebuffersForWindow(CJPlatformWindow * win) {
  if (!win) return;
  win->swapChainFramebuffers = (VkFramebuffer*)malloc(sizeof(VkFramebuffer)*win->swapChainImageCount);
  for (uint32_t i=0;i<win->swapChainImageCount;i++) {
    VkImageView attachments[] = { win->swapChainImageViews[i] };
    VkFramebufferCreateInfo fi = {0}; fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fi.renderPass = cj_engine_render_pass(cj_engine_get_current()); fi.attachmentCount = 1; fi.pAttachments = attachments; fi.width = win->swapChainExtent.width; fi.height = win->swapChainExtent.height; fi.layers = 1;
    vkCreateFramebuffer(cj_engine_device(cj_engine_get_current()), &fi, NULL, &win->swapChainFramebuffers[i]);
  }
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
  if (!IsWindow(win->handle)) return;
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
  if (dev) vkDeviceWaitIdle(dev);
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
  if (win->handle && IsWindow(win->handle)) DestroyWindow(win->handle);
  win->handle = NULL;
#else
  if (display && win->handle) XDestroyWindow(display, win->handle);
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

/* duplicate struct removed (defined above) */

/* Internal helpers migrated from cjelly.c (static) */
static void plat_createPlatformWindow(CJPlatformWindow * win, const char * title, int width, int height);
static void plat_createSurfaceForWindow(CJPlatformWindow * win);
static void plat_createSwapChainForWindow(CJPlatformWindow * win);
static void plat_createImageViewsForWindow(CJPlatformWindow * win);
static void plat_createFramebuffersForWindow(CJPlatformWindow * win);
static void plat_createSyncObjectsForWindow(CJPlatformWindow * win);
static void plat_drawFrameForWindow(CJPlatformWindow * win);
static void plat_cleanupWindow(CJPlatformWindow * win);

/* Command buffer recorders using engine/ctx */
static void createTexturedCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyVulkanContext* ctx);
static void createBindlessCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyBindlessResources* resources, const CJellyVulkanContext* ctx);

/* Bridge wrapper: implement cj_window_t in terms of legacy CJellyWindow
 * so we can migrate callers incrementally. */

/* Internal definition of the opaque window type */
struct cj_window_t {
  CJPlatformWindow * plat;
  uint64_t frame_index;
};

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
  plat_createImageViewsForWindow(win->plat);
  plat_createFramebuffersForWindow(win->plat);

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
  createTexturedCommandBuffersForWindowCtx(win->plat, &ctx);
  plat_createSyncObjectsForWindow(win->plat);

  win->frame_index = 0u;
  return win;
}

CJ_API void cj_window_destroy(cj_window_t* win) {
  if (!win) return;
  if (win->plat) {
    plat_cleanupWindow(win->plat);
    free(win->plat);
  }
  free(win);
}

CJ_API cj_result_t cj_window_resize(cj_window_t* win, uint32_t width, uint32_t height) {
  (void)width; (void)height; /* legacy path will handle via swapchain recreation elsewhere */
  return win ? CJ_SUCCESS : CJ_E_INVALID_ARGUMENT;
}

CJ_API cj_result_t cj_window_begin_frame(cj_window_t* win, cj_frame_info_t* out_frame_info) {
  if (!win) return CJ_E_INVALID_ARGUMENT;
  if (out_frame_info) {
    out_frame_info->frame_index = ++win->frame_index;
    out_frame_info->delta_seconds = 0.0; /* stub */
  } else {
    win->frame_index++;
  }
  return CJ_SUCCESS;
}

CJ_API cj_result_t cj_window_execute(cj_window_t* win) {
  if (!win || !win->plat) return CJ_E_INVALID_ARGUMENT;
  plat_drawFrameForWindow(win->plat);
  return CJ_SUCCESS;
}

CJ_API cj_result_t cj_window_present(cj_window_t* win) {
  (void)win; /* drawFrameForWindow presents already */
  return CJ_SUCCESS;
}

CJ_API void cj_window_set_render_graph(cj_window_t* win, cj_rgraph_t* graph) {
  (void)win; (void)graph;
}

CJ_API void cj_window_get_size(const cj_window_t* win, uint32_t* out_w, uint32_t* out_h) {
  if (!win || !win->plat) return;
  if (out_w) *out_w = (uint32_t)win->plat->width;
  if (out_h) *out_h = (uint32_t)win->plat->height;
}

CJ_API uint64_t cj_window_frame_index(const cj_window_t* win) {
  return win ? win->frame_index : 0u;
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
static void createTexturedCommandBuffersForWindowCtx(CJPlatformWindow * win, const CJellyVulkanContext* ctx) {
  if (!win || !ctx || ctx->device == VK_NULL_HANDLE || ctx->commandPool == VK_NULL_HANDLE || ctx->renderPass == VK_NULL_HANDLE) return;
  win->commandBuffers =
      (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer) * win->swapChainImageCount);

  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = ctx->commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(ctx->device, &allocInfo, win->commandBuffers) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate textured (ctx) command buffers\n");
    exit(EXIT_FAILURE);
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
      fprintf(stderr, "Failed to record textured (ctx) command buffer\n");
      exit(EXIT_FAILURE);
    }
  }
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

  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = ctx->commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(ctx->device, &allocInfo, win->commandBuffers) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate bindless command buffers, falling back to textured (ctx)\n");
    createTexturedCommandBuffersForWindowCtx(win, ctx);
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
