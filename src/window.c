#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <stdio.h>
#include <assert.h>
#include <cjelly/cj_window.h>
#include <cjelly/cj_platform.h>
#include <cjelly/runtime.h>
#include <cjelly/engine_internal.h>
#include <cjelly/window_internal.h>
#include <cjelly/bindless_internal.h>

/* Refer to existing engine-scoped textured globals for now */
extern VkBuffer vertexBufferTextured;
extern VkPipeline texturedPipeline;
extern VkPipelineLayout texturedPipelineLayout;
extern VkDescriptorSet textureDescriptorSet;

/* Private forward declarations to call internal window helpers in cjelly.c */
#include <cjelly/window_internal.h>
void createPlatformWindow(CJellyWindow * win, const char * title, int width, int height);
void createSurfaceForWindow(CJellyWindow * win);
void createSwapChainForWindow(CJellyWindow * win);
void createImageViewsForWindow(CJellyWindow * win);
void createFramebuffersForWindow(CJellyWindow * win);
void createTexturedCommandBuffersForWindowCtx(CJellyWindow * win, const CJellyVulkanContext* ctx);
void createSyncObjectsForWindow(CJellyWindow * win);
void drawFrameForWindow(CJellyWindow * win);
void cleanupWindow(CJellyWindow * win);
/* textured pipeline helper (defined in cjelly.c) */
void cjelly_init_textured_pipeline_ctx(const CJellyVulkanContext* ctx);
/* bindless command buffers (defined in cjelly.c) */
void createBindlessCommandBuffersForWindowCtx(CJellyWindow * win, const CJellyBindlessResources* resources, const CJellyVulkanContext* ctx);

/* Bridge wrapper: implement cj_window_t in terms of legacy CJellyWindow
 * so we can migrate callers incrementally. */

/* Internal definition of the opaque window type */
struct cj_window_t {
  CJellyWindow * legacy;
  uint64_t frame_index;
};

CJ_API cj_window_t* cj_window_create(cj_engine_t* engine, const cj_window_desc_t* desc) {
  (void)engine; /* not used yet; legacy path */
  if (!desc) return NULL;
  cj_window_t* win = (cj_window_t*)calloc(1, sizeof(*win));
  if (!win) return NULL;
  win->legacy = (CJellyWindow*)calloc(1, sizeof(CJellyWindow));
  if (!win->legacy) { free(win); return NULL; }

  /* Create OS window and per-window Vulkan resources via legacy functions */
  const char* title = desc->title.ptr ? desc->title.ptr : "CJelly Window";
  createPlatformWindow(win->legacy, title, (int)desc->width, (int)desc->height);
  createSurfaceForWindow(win->legacy);
  createSwapChainForWindow(win->legacy);
  createImageViewsForWindow(win->legacy);
  createFramebuffersForWindow(win->legacy);

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
  createTexturedCommandBuffersForWindowCtx(win->legacy, &ctx);
  createSyncObjectsForWindow(win->legacy);

  win->frame_index = 0u;
  return win;
}

CJ_API void cj_window_destroy(cj_window_t* win) {
  if (!win) return;
  if (win->legacy) {
    cleanupWindow(win->legacy);
    free(win->legacy);
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
  if (!win || !win->legacy) return CJ_E_INVALID_ARGUMENT;
  drawFrameForWindow(win->legacy);
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
  if (!win || !win->legacy) return;
  if (out_w) *out_w = (uint32_t)win->legacy->width;
  if (out_h) *out_h = (uint32_t)win->legacy->height;
}

CJ_API uint64_t cj_window_frame_index(const cj_window_t* win) {
  return win ? win->frame_index : 0u;
}

/* Re-record color-only bindless commands for a window */
CJ_API void cj_window_rerecord_bindless_color(cj_window_t* win,
                                       const void* resources,
                                       const CJellyVulkanContext* ctx) {
  if (!win || !win->legacy || !resources || !ctx) return;
  const CJellyBindlessResources* r = (const CJellyBindlessResources*)resources;
  /* Ensure GPU is idle before re-record to avoid freeing in-use buffers */
  {
    cj_engine_t* e2 = cj_engine_get_current();
    if (e2 && cj_engine_device(e2) != VK_NULL_HANDLE) vkDeviceWaitIdle(cj_engine_device(e2));
  }
  if (win->legacy->commandBuffers && win->legacy->swapChainImageCount > 0) {
    cj_engine_t* e3 = cj_engine_get_current();
    vkFreeCommandBuffers(cj_engine_device(e3), cj_engine_command_pool(e3), win->legacy->swapChainImageCount, win->legacy->commandBuffers);
    free(win->legacy->commandBuffers);
    win->legacy->commandBuffers = NULL;
  }
  createBindlessCommandBuffersForWindowCtx(win->legacy, r, ctx);
}

/* Per-window textured command buffer recording using explicit ctx */
void createTexturedCommandBuffersForWindowCtx(CJellyWindow * win, const CJellyVulkanContext* ctx) {
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

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(win->commandBuffers[i], 0, 1, &vertexBufferTextured, offsets);

    vkCmdBindPipeline(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, texturedPipeline);

    assert(textureDescriptorSet != VK_NULL_HANDLE);
    assert(texturedPipelineLayout != VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, texturedPipelineLayout, 0, 1, &textureDescriptorSet, 0, NULL);

    vkCmdDraw(win->commandBuffers[i], 6, 1, 0, 0);
    vkCmdEndRenderPass(win->commandBuffers[i]);

    if (vkEndCommandBuffer(win->commandBuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to record textured (ctx) command buffer\n");
      exit(EXIT_FAILURE);
    }
  }
}

/* Per-window bindless command buffer recording using explicit ctx */
void createBindlessCommandBuffersForWindowCtx(CJellyWindow * win, const CJellyBindlessResources* resources, const CJellyVulkanContext* ctx) {
  if (!win || !ctx || !resources) return;
  if (!ctx->device || !ctx->commandPool || !ctx->renderPass) return;

  if (!resources->pipeline) {
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
