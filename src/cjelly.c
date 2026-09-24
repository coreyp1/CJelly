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

#include <ghoti.io/cjelly/plat_internal.h>
#include <ghoti.io/cjelly/cjelly.h>
#include <ghoti.io/cjelly/runtime.h>
#include <ghoti.io/cjelly/application.h>
#include <ghoti.io/cjelly/cj_window.h>
#include <ghoti.io/cjelly/window_internal.h>
#include <ghoti.io/cjelly/engine_internal.h>
#include <ghoti.io/cjelly/cj_input.h>
#include <ghoti.io/cjelly/bindless_internal.h>
#include <ghoti.io/cjelly/textured_internal.h>
#include <ghoti.io/cjelly/bindless_state_internal.h>
#include <ghoti.io/cjelly/basic_state_internal.h>
#include <ghoti.io/cjelly/format/image.h>
#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_log.h>
#include <shaders/basic.vert.h>

/* textured.vert lives in rgraph.c's translation unit: the generated SPIR-V
 * headers define their arrays with external linkage, so including one in a
 * second file is a duplicate-symbol link error. engine.c reaches the colour
 * shaders the same way. */
extern unsigned char textured_vert_spv[];
extern unsigned int textured_vert_spv_len;
#include <shaders/color.vert.h>
#include <shaders/color.frag.h>
#include <shaders/textured.frag.h>
#include <shaders/bindless.vert.h>
#include <shaders/bindless.frag.h>

// Global Vulkan objects shared among all windows.




/* Helpers to read from current engine */
static inline cj_engine_t* cur_eng(void) { return cj_engine_get_current(); }
static inline VkDevice cur_device(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_device(e) : VK_NULL_HANDLE; }
static inline VkRenderPass cur_render_pass(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_render_pass(e) : VK_NULL_HANDLE; }
static inline VkQueue cur_gfx_queue(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_graphics_queue(e) : VK_NULL_HANDLE; }
static inline VkCommandPool cur_cmd_pool(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_command_pool(e) : VK_NULL_HANDLE; }
static inline CJellyTexturedResources* cur_tx(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_textured(e) : NULL; }
static inline CJellyBindlessState* cur_bl(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_bindless(e) : NULL; }


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


// Forward declarations for helper functions still in use:
static CJ_MUST_CHECK bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer * buffer,
    VkDeviceMemory * bufferMemory);
static CJ_MUST_CHECK bool transitionImageLayout(VkImage image, VkFormat format,
    VkImageLayout oldLayout, VkImageLayout newLayout);
static CJ_MUST_CHECK bool createBindlessVertexBuffer(VkDevice device, VkCommandPool commandPool);
static CJ_MUST_CHECK bool createBindlessGraphicsPipeline(VkDevice device, VkRenderPass renderPass);
static VkShaderModule createShaderModuleFromMemory(VkDevice device, const unsigned char * code, size_t codeSize);

// Forward declarations/definitions for texture atlas and application
typedef struct CJellyTextureAtlas {
  VkImage atlasImage;
  VkDeviceMemory atlasImageMemory;
  VkImageView atlasImageView;
  VkSampler atlasSampler;
  VkDescriptorSetLayout bindlessDescriptorSetLayout;
  VkDescriptorPool bindlessDescriptorPool;
  VkDescriptorSet bindlessDescriptorSet;
  uint32_t atlasWidth;
  uint32_t atlasHeight;
  uint32_t nextTextureX;
  uint32_t nextTextureY;
  uint32_t currentRowHeight;
  uint32_t textureCount;
  struct CJellyTextureEntry * entries;
  uint32_t maxTextures;
} CJellyTextureAtlas;
struct CJellyApplication;

/* Atlas entry type used by bindless UI helpers */
typedef struct CJellyTextureEntry {
  uint32_t textureID;
  uint32_t x, y, width, height;
  float uMin, uMax, vMin, vMax;
} CJellyTextureEntry;

/* Forward declarations for atlas/helpers used before their definitions */
CJellyTextureAtlas * cjelly_create_texture_atlas(uint32_t width, uint32_t height);
void cjelly_destroy_texture_atlas(CJellyTextureAtlas * atlas);
uint32_t cjelly_atlas_add_texture(CJellyTextureAtlas * atlas, const char * filePath);
void cjelly_atlas_update_descriptor_set(CJellyTextureAtlas * atlas);
CJellyTextureEntry * cjelly_atlas_get_texture_entry(CJellyTextureAtlas * atlas, uint32_t textureID);
/* Context variants */
CJellyTextureAtlas * cjelly_create_texture_atlas_ctx(const CJellyVulkanContext* ctx, uint32_t width, uint32_t height);
void cjelly_destroy_texture_atlas_ctx(CJellyTextureAtlas * atlas, const CJellyVulkanContext* ctx);
uint32_t cjelly_atlas_add_texture_ctx(CJellyTextureAtlas * atlas, const char * filePath, const CJellyVulkanContext* ctx);
static void cjelly_atlas_update_descriptor_set_ctx(CJellyTextureAtlas * atlas, const CJellyVulkanContext* ctx);
/* Forward declaration for ctx-based textured command recording used earlier */
struct CJellyWindow; /* opaque forward */
void createTexturedCommandBuffersForWindowCtx(struct CJellyWindow * win, const CJellyVulkanContext* ctx);



// Context-based atlas helpers (defined later)
static void cjelly_atlas_update_descriptor_set_ctx(CJellyTextureAtlas * atlas, const CJellyVulkanContext* ctx);

/* Public wrappers for runtime.h. processWindowEvents is declared in
 * plat_internal.h and defined by whichever platform module was built. */
CJ_API void cj_poll_events(void) { processWindowEvents(); }

/* Public convenience setter for demo color updates without exposing struct layout */
CJ_API void cj_bindless_set_color(CJellyBindlessResources* resources, float r, float g, float b, float a) {
  if (!resources) return;
  resources->colorMul[0] = r;
  resources->colorMul[1] = g;
  resources->colorMul[2] = b;
  resources->colorMul[3] = a;
}

/* Update vertex colors for a left/right split based on current colorMul:
 * If red>green -> left red, right green; else left green, right red. */
CJ_API void cj_bindless_update_split_from_colorMul(CJellyBindlessResources* resources) {
  if (!resources || resources->vertexBufferMemory == VK_NULL_HANDLE) return;

  /* Use the actual color from colorMul */
  float r = resources->colorMul[0];
  float g = resources->colorMul[1];
  float b = resources->colorMul[2];

  /* The colour pipeline's layout, not the bindless one, despite the name
   * this function carries: its only caller passes the engine's colour
   * pipeline resources, and writing a bindless vertex here overran the
   * buffer the engine allocated for colour ones. */
  CJellyColorVertex vertices[] = {
    // Single quad matching textured size: [-0.5,0.5]
    {{-0.5f, -0.5f}, {r, g, b}},
    {{ 0.5f, -0.5f}, {r, g, b}},
    {{ 0.5f,  0.5f}, {r, g, b}},
    {{ 0.5f,  0.5f}, {r, g, b}},
    {{-0.5f,  0.5f}, {r, g, b}},
    {{-0.5f, -0.5f}, {r, g, b}},
  };
  void* data = NULL;
  VkDevice dev = cur_device();
  vkMapMemory(dev, resources->vertexBufferMemory, 0, sizeof(vertices), 0, &data);
  memcpy(data, vertices, sizeof(vertices));
  vkUnmapMemory(dev, resources->vertexBufferMemory);
}

// === Context-based utility functions ===
static uint32_t findMemoryTypeCtx(const CJellyVulkanContext* ctx, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  /* A sentinel, rather than ending the process. A memory type this device
   * cannot offer is a refusal the caller can act on, and which of its own
   * windows to give up on is not a decision a library gets to make. */
  CJ_ERRORF("Failed to find suitable memory type (ctx)!");
  return UINT32_MAX;
}

static CJ_MUST_CHECK bool createImageCtx(const CJellyVulkanContext* ctx, uint32_t width, uint32_t height, VkFormat format,
    VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
    VkImage * image, VkDeviceMemory * imageMemory) {
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

  if (vkCreateImage(ctx->device, &imageInfo, NULL, image) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create image (ctx)");
    return false;
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(ctx->device, *image, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryTypeCtx(ctx, memRequirements.memoryTypeBits, properties);
  if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    vkDestroyImage(ctx->device, *image, NULL);
    *image = VK_NULL_HANDLE;
    return false;
  }

  if (vkAllocateMemory(ctx->device, &allocInfo, NULL, imageMemory) != VK_SUCCESS) {
    CJ_ERRORF("Failed to allocate image memory (ctx)");
    vkDestroyImage(ctx->device, *image, NULL);
    *image = VK_NULL_HANDLE;
    return false;
  }

  vkBindImageMemory(ctx->device, *image, *imageMemory, 0);
  return true;
}

static VkCommandBuffer beginSingleTimeCommandsCtx(const CJellyVulkanContext* ctx) {
  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = ctx->commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(ctx->device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo = {0};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

static void endSingleTimeCommandsCtx(const CJellyVulkanContext* ctx, VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo = {0};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(ctx->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(ctx->graphicsQueue);

  vkFreeCommandBuffers(ctx->device, ctx->commandPool, 1, &commandBuffer);
}

static CJ_MUST_CHECK bool transitionImageLayoutCtx(const CJellyVulkanContext* ctx, VkImage image, VkFormat format,
    VkImageLayout oldLayout, VkImageLayout newLayout) {
  (void)format;
  VkCommandBuffer commandBuffer = beginSingleTimeCommandsCtx(ctx);

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

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    /* The command buffer is already open. Ending it submits an empty one,
     * which is the cheapest way to give it back - a bare return here would
     * leak it out of the pool on a path that already went wrong. */
    CJ_ERRORF("Unsupported layout transition (ctx)!");
    endSingleTimeCommandsCtx(ctx, commandBuffer);
    return false;
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &barrier);

  endSingleTimeCommandsCtx(ctx, commandBuffer);
  return true;
}

static void copyBufferToImageCtx(const CJellyVulkanContext* ctx, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommandsCtx(ctx);

  VkBufferImageCopy region = {0};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = (VkOffset3D){0, 0, 0};
  region.imageExtent = (VkExtent3D){width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  endSingleTimeCommandsCtx(ctx, commandBuffer);
}
// Context-friendly vertex buffer creation for bindless vertices
static CJ_MUST_CHECK bool createBindlessVertexBufferCtx(
    VkDevice device,
    VkCommandPool commandPool,
    VkBuffer* outBuffer,
    VkDeviceMemory* outMemory) {
  (void)commandPool;
  VertexBindless verticesBindless[] = {
    {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{ 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{-0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 1},
  };
  VkDeviceSize bufferSize = sizeof(verticesBindless);
  if (!createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          outBuffer, outMemory)) {
    return false;
  }
  void* data = NULL;
  vkMapMemory(device, *outMemory, 0, bufferSize, 0, &data);
  memcpy(data, verticesBindless, (size_t)bufferSize);
  vkUnmapMemory(device, *outMemory);
  return true;
}

// Forward decl for context-friendly pipeline helper
static VkResult createBindlessGraphicsPipelineWithLayout(
    VkDevice device,
    VkRenderPass renderPass,
    VkDescriptorSetLayout descriptorSetLayout,
    VkPipelineLayout* outPipelineLayout,
    VkPipeline* outPipeline);

// Initialize bindless rendering resources
CJellyBindlessResources* cjelly_create_bindless_resources(void) {
    CJ_DEBUGF("Creating bindless resources...");
    const char* stageEnv = getenv("CJELLY_BINDLESS_STAGE");
    int stage = stageEnv ? atoi(stageEnv) : 2; // default full
    CJ_DEBUGF("CJELLY_BINDLESS_STAGE=%d", stage);

    CJellyBindlessResources* resources = malloc(sizeof(CJellyBindlessResources));
    if (!resources) {
        CJ_ERRORF("Failed to allocate bindless resources");
        return NULL;
    }

    memset(resources, 0, sizeof(CJellyBindlessResources));

    // Create texture atlas
    CJellyTextureAtlas* atlas = cjelly_create_texture_atlas(2048, 2048);
    if (!atlas) {
        CJ_ERRORF("Failed to create texture atlas");
        free(resources);
        return NULL;
    }

    CJ_DEBUGF("Texture atlas created");

    // Add textures to atlas
    CJ_DEBUGF("Adding textures to atlas...");
    uint32_t tex1 = cjelly_atlas_add_texture(atlas, "test/images/bmp/tang.bmp");
    CJ_DEBUGF("tex1 id=%u", tex1);
    uint32_t tex2 = cjelly_atlas_add_texture(atlas, "test/images/bmp/16Color.bmp");
    CJ_DEBUGF("tex2 id=%u", tex2);

    if (tex1 == 0 || tex2 == 0) {
        CJ_ERRORF("Failed to add textures to atlas");
        cjelly_destroy_texture_atlas(atlas);
        free(resources);
        return NULL;
    }

    CJ_DEBUGF("Textures added to atlas");

    CJ_DEBUGF("Transition atlas to SHADER_READ_ONLY");
    // The atlas image was filled per texture by the upload path; ensure final layout
    if (!transitionImageLayout(atlas->atlasImage, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        cjelly_destroy_texture_atlas(atlas);
        free(resources);
        return NULL;
    }
    CJ_DEBUGF("Update descriptor set");
    // Update descriptor set
    cjelly_atlas_update_descriptor_set(atlas);

    // Create vertex buffer
    CJ_DEBUGF("Create bindless vertex buffer");
    if (!createBindlessVertexBuffer(cur_device(), cur_cmd_pool())) {
        cjelly_destroy_texture_atlas(atlas);
        free(resources);
        return NULL;
    }
    CJellyBindlessState* bl = cur_bl();
    resources->vertexBuffer = bl->vertexBuffer;
    resources->vertexBufferMemory = bl->vertexBufferMemory;

    // Expose atlas via resources only
    resources->textureAtlas = atlas;
    // Initialize default push values
    resources->uv[0]=1.0f; resources->uv[1]=1.0f; resources->uv[2]=0.0f; resources->uv[3]=0.0f;
    resources->colorMul[0]=1.0f; resources->colorMul[1]=1.0f; resources->colorMul[2]=1.0f; resources->colorMul[3]=1.0f;

    if (stage >= 2) {
      // Create graphics pipeline
      CJ_DEBUGF("Create bindless graphics pipeline");
      if (!createBindlessGraphicsPipeline(cur_device(), cur_render_pass())) {
        cjelly_destroy_texture_atlas(atlas);
        free(resources);
        return NULL;
      }
      CJellyBindlessState* bl2 = cur_bl();
      CJ_DEBUGF("After pipeline creation, bindless pipeline=%p layout=%p", (void*)bl2->pipeline, (void*)bl2->pipelineLayout);
      resources->pipeline = bl2->pipeline;
      CJ_DEBUGF("Assigned resources->pipeline");
      resources->pipelineLayout = bl2->pipelineLayout;
      CJ_DEBUGF("Assigned resources->pipelineLayout");
    } else {
      CJ_DEBUGF("Skipping bindless pipeline creation due to stage %d", stage);
      resources->pipeline = VK_NULL_HANDLE;
      resources->pipelineLayout = VK_NULL_HANDLE;
    }


    CJ_DEBUGF("Bindless resources created successfully");
    CJ_DEBUGF("About to return resources=%p", (void*)resources);
    return resources;
}


CJ_API CJellyBindlessResources* cjelly_create_bindless_resources_ctx(const CJellyVulkanContext* ctx) {
    if (!ctx || ctx->device == VK_NULL_HANDLE || ctx->commandPool == VK_NULL_HANDLE || ctx->renderPass == VK_NULL_HANDLE) {
        CJ_ERRORF("Invalid Vulkan context passed to cjelly_create_bindless_resources_ctx");
        return NULL;
    }

    CJellyBindlessResources* resources = (CJellyBindlessResources*)calloc(1, sizeof(CJellyBindlessResources));
    if (!resources) return NULL;

    // Create atlas and add textures using context
    CJellyTextureAtlas* atlas = cjelly_create_texture_atlas_ctx(ctx, 2048, 2048);
    if (!atlas) {
        free(resources);
        return NULL;
    }
    uint32_t tex1 = cjelly_atlas_add_texture_ctx(atlas, "test/images/bmp/tang.bmp", ctx);
    uint32_t tex2 = cjelly_atlas_add_texture_ctx(atlas, "test/images/bmp/16Color.bmp", ctx);
    if (tex1 == 0 || tex2 == 0) {
        cjelly_destroy_texture_atlas_ctx(atlas, ctx);
        free(resources);
        return NULL;
    }
    // Transition atlas to shader-read after all copies
    if (!transitionImageLayoutCtx(ctx, atlas->atlasImage, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        cjelly_destroy_texture_atlas_ctx(atlas, ctx);
        free(resources);
        return NULL;
    }
    cjelly_atlas_update_descriptor_set_ctx(atlas, ctx);

    // Create vertex buffer into resources using context
    VkBuffer vb = VK_NULL_HANDLE; VkDeviceMemory vm = VK_NULL_HANDLE;
    if (!createBindlessVertexBufferCtx(ctx->device, ctx->commandPool, &vb, &vm)) {
        cjelly_destroy_texture_atlas_ctx(atlas, ctx);
        free(resources);
        return NULL;
    }
    resources->vertexBuffer = vb;
    resources->vertexBufferMemory = vm;
  /* Optionally mirror into engine bindless state for fallback paths */
  CJellyBindlessState* blm = cur_bl();
  if (blm && blm->vertexBuffer == VK_NULL_HANDLE) {
    blm->vertexBuffer = vb;
    blm->vertexBufferMemory = vm;
  }

    // Create pipeline using the atlas' descriptor set layout (context-friendly)
    VkPipelineLayout outLayout = VK_NULL_HANDLE;
    VkPipeline outPipeline = VK_NULL_HANDLE;
    if (createBindlessGraphicsPipelineWithLayout(ctx->device, ctx->renderPass, atlas->bindlessDescriptorSetLayout, &outLayout, &outPipeline) != VK_SUCCESS) {
        cjelly_destroy_texture_atlas(atlas);
        free(resources);
        return NULL;
    }
    resources->pipeline = outPipeline;
    resources->pipelineLayout = outLayout;

    // Store atlas and defaults
    resources->textureAtlas = atlas;
    resources->uv[0]=1.0f; resources->uv[1]=1.0f; resources->uv[2]=0.0f; resources->uv[3]=0.0f;
    resources->colorMul[0]=1.0f; resources->colorMul[1]=1.0f; resources->colorMul[2]=1.0f; resources->colorMul[3]=1.0f;

    return resources;
}

// Destroy bindless rendering resources
void cjelly_destroy_bindless_resources(CJellyBindlessResources* resources) {
    if (!resources) return;

  /* Destroy per-resource pipeline objects if they were created via ctx path */
  if (resources->pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(cur_device(), resources->pipeline, NULL);
    resources->pipeline = VK_NULL_HANDLE;
  }
  if (resources->pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(cur_device(), resources->pipelineLayout, NULL);
    resources->pipelineLayout = VK_NULL_HANDLE;
  }
  CJellyBindlessState* bl = cur_bl();
  if (resources->vertexBuffer != VK_NULL_HANDLE && (!bl || resources->vertexBuffer != bl->vertexBuffer)) {
    vkDestroyBuffer(cur_device(), resources->vertexBuffer, NULL);
    resources->vertexBuffer = VK_NULL_HANDLE;
  }
  if (resources->vertexBufferMemory != VK_NULL_HANDLE && (!bl || resources->vertexBufferMemory != bl->vertexBufferMemory)) {
    vkFreeMemory(cur_device(), resources->vertexBufferMemory, NULL);
    resources->vertexBufferMemory = VK_NULL_HANDLE;
  }

    if (resources->textureAtlas) {
        cjelly_destroy_texture_atlas(resources->textureAtlas);
    }

    free(resources);
}

// Minimal bindless-capable resources for a color-only square (no texture)
CJellyBindlessResources* cjelly_create_bindless_color_square_resources(void) {
    CJellyBindlessResources* resources = malloc(sizeof(CJellyBindlessResources));
    if (!resources) return NULL;
    memset(resources, 0, sizeof(CJellyBindlessResources));

    // Create a dedicated vertex buffer with textureID=0 (no sampling) and multicolor vertices
    VertexBindless verticesBindless[] = {
      {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{ 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{-0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    };
    VkDeviceSize vbSize = sizeof(verticesBindless);
    if (!createBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &resources->vertexBuffer, &resources->vertexBufferMemory)) {
        free(resources);
        return NULL;
    }
    void* vdata = NULL;
    vkMapMemory(cur_device(), resources->vertexBufferMemory, 0, vbSize, 0, &vdata);
    memcpy(vdata, verticesBindless, (size_t)vbSize);
    vkUnmapMemory(cur_device(), resources->vertexBufferMemory);

    // Prefer using the existing atlas descriptor set layout if available for layout compatibility
    VkDescriptorSetLayout tempSetLayout = VK_NULL_HANDLE;
    bool useNoSetLayout = (resources->textureAtlas == NULL) || (resources->textureAtlas->bindlessDescriptorSetLayout == VK_NULL_HANDLE);
    if (!useNoSetLayout) {
        /* Atlas provides a descriptor set layout; use it */
        tempSetLayout = resources->textureAtlas->bindlessDescriptorSetLayout;
    }

    // Create a pipeline layout with that set layout and push constants allowed
    VkPushConstantRange pushRange = {0};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 8; // uv vec4 + colorMul vec4

    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[] = { tempSetLayout };
    if (useNoSetLayout) {
        /* No descriptor sets required for color-only path */
        pli.setLayoutCount = 0;
        pli.pSetLayouts = NULL;
    } else {
    pli.setLayoutCount = 1;
    pli.pSetLayouts = setLayouts;
    }
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(cur_device(), &pli, NULL, &resources->pipelineLayout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(cur_device(), tempSetLayout, NULL);
        free(resources);
        return NULL;
    }
    // Descriptor set layout can be destroyed after pipeline layout creation if we created a temp one
    /* Do not destroy atlas-provided layout; only destroy temporary ones (none created in no-set path) */

    // If global atlas exists, attach it so descriptor binding works
    // no-op: resources->textureAtlas already set if available

    // Create a simple color-only pipeline using basic shaders (no descriptor sets)
    VkShaderModule vert = createShaderModuleFromMemory(cur_device(), color_vert_spv, color_vert_spv_len);
    VkShaderModule frag = createShaderModuleFromMemory(cur_device(), color_frag_spv, color_frag_spv_len);
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(cur_device(), resources->pipelineLayout, NULL);
        free(resources);
        return NULL;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {0};
    binding.binding = 0;
    binding.stride = sizeof(VertexBindless);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3] = {0};
    attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(VertexBindless, pos);
    attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = offsetof(VertexBindless, color);
    attrs[2].binding = 0; attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32_UINT; attrs[2].offset = offsetof(VertexBindless, textureID);
    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp = {0}; vp.x=0; vp.y=0; vp.width=1.0f; vp.height=1.0f; vp.minDepth=0; vp.maxDepth=1;
    VkRect2D sc = {0}; sc.extent.width = 1; sc.extent.height = 1;
    VkPipelineViewportStateCreateInfo vps = {0};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_BACK_BIT; rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {0}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo gp = {0};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vps; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
    gp.layout = resources->pipelineLayout; gp.renderPass = cur_render_pass(); gp.subpass = 0;
    if (vkCreateGraphicsPipelines(cur_device(), VK_NULL_HANDLE, 1, &gp, NULL, &resources->pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(cur_device(), resources->pipelineLayout, NULL);
        vkDestroyShaderModule(cur_device(), vert, NULL);
        vkDestroyShaderModule(cur_device(), frag, NULL);
        free(resources);
        return NULL;
    }

    vkDestroyShaderModule(cur_device(), vert, NULL);
    vkDestroyShaderModule(cur_device(), frag, NULL);
    // Defaults for color-only path
    resources->uv[0]=1.0f; resources->uv[1]=1.0f; resources->uv[2]=0.0f; resources->uv[3]=0.0f;
    resources->colorMul[0]=1.0f; resources->colorMul[1]=1.0f; resources->colorMul[2]=1.0f; resources->colorMul[3]=1.0f;
    return resources;
}

CJ_API CJellyBindlessResources* cjelly_create_bindless_color_square_resources_ctx(const CJellyVulkanContext* ctx) {
    if (!ctx || ctx->device == VK_NULL_HANDLE) return NULL;
    CJellyBindlessResources* resources = (CJellyBindlessResources*)calloc(1, sizeof(CJellyBindlessResources));
    if (!resources) return NULL;

    // Build vertex buffer on ctx device
    VertexBindless verticesBindless[] = {
      // Single quad at same size as textured image: [-0.5,0.5]
      {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{ 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{-0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
      {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    };
    VkDeviceSize vbSize = sizeof(verticesBindless);
    if (!createBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &resources->vertexBuffer, &resources->vertexBufferMemory)) {
        free(resources);
        return NULL;
    }
    void* vdata = NULL;
    vkMapMemory(ctx->device, resources->vertexBufferMemory, 0, vbSize, 0, &vdata);
    memcpy(vdata, verticesBindless, (size_t)vbSize);
    vkUnmapMemory(ctx->device, resources->vertexBufferMemory);

    VkPipelineLayout outLayout = VK_NULL_HANDLE;
    VkPipeline outPipeline = VK_NULL_HANDLE;
    // Create a pipeline layout with ONLY push constants (no descriptor sets)
    VkPushConstantRange pushRange = {0};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 8; // uv vec4 + colorMul vec4

    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 0;
    pli.pSetLayouts = NULL;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(ctx->device, &pli, NULL, &outLayout) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, resources->vertexBuffer, NULL);
        vkFreeMemory(ctx->device, resources->vertexBufferMemory, NULL);
        free(resources);
        return NULL;
    }

    // Create a simple color-only pipeline using basic shaders (no descriptor sets)
    VkShaderModule vert = createShaderModuleFromMemory(ctx->device, color_vert_spv, color_vert_spv_len);
    VkShaderModule frag = createShaderModuleFromMemory(ctx->device, color_frag_spv, color_frag_spv_len);
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx->device, outLayout, NULL);
        vkDestroyBuffer(ctx->device, resources->vertexBuffer, NULL);
        vkFreeMemory(ctx->device, resources->vertexBufferMemory, NULL);
        free(resources);
        return NULL;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {0};
    binding.binding = 0;
    binding.stride = sizeof(VertexBindless);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3] = {0};
    attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(VertexBindless, pos);
    attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = offsetof(VertexBindless, color);
    attrs[2].binding = 0; attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32_UINT; attrs[2].offset = offsetof(VertexBindless, textureID);
    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp = {0}; vp.x=0; vp.y=0; vp.width=1.0f; vp.height=1.0f; vp.minDepth=0; vp.maxDepth=1;
    VkRect2D sc = {0}; sc.extent.width = 1; sc.extent.height = 1;
    VkPipelineViewportStateCreateInfo vps = {0};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_BACK_BIT; rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {0}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo gp = {0};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vps; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
    gp.layout = outLayout; gp.renderPass = ctx->renderPass; gp.subpass = 0;
    if (vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &gp, NULL, &outPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(ctx->device, outLayout, NULL);
        vkDestroyShaderModule(ctx->device, vert, NULL);
        vkDestroyShaderModule(ctx->device, frag, NULL);
        vkDestroyBuffer(ctx->device, resources->vertexBuffer, NULL);
        vkFreeMemory(ctx->device, resources->vertexBufferMemory, NULL);
        free(resources);
        return NULL;
    }

    vkDestroyShaderModule(ctx->device, vert, NULL);
    vkDestroyShaderModule(ctx->device, frag, NULL);

    resources->pipeline = outPipeline;
    resources->pipelineLayout = outLayout;
    resources->uv[0]=1.0f; resources->uv[1]=1.0f; resources->uv[2]=0.0f; resources->uv[3]=0.0f;
    resources->colorMul[0]=1.0f; resources->colorMul[1]=1.0f; resources->colorMul[2]=1.0f; resources->colorMul[3]=1.0f;
    return resources;
}
static CJ_MUST_CHECK bool createImage(uint32_t width, uint32_t height, VkFormat format,
    VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage * image,
    VkDeviceMemory * imageMemory);
static VkCommandBuffer beginSingleTimeCommands(void);
static void endSingleTimeCommands(VkCommandBuffer commandBuffer);
static CJ_MUST_CHECK bool transitionImageLayout(VkImage image, VkFormat format,
    VkImageLayout oldLayout, VkImageLayout newLayout);


//
// === UTILITY FUNCTIONS ===
//

static VkShaderModule createShaderModuleFromMemory(
    VkDevice device, const unsigned char * code, size_t codeSize) {

  VkShaderModuleCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = codeSize;
  createInfo.pCode = (const uint32_t *)code;

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) !=
      VK_SUCCESS) {
    CJ_ERRORF("Failed to create shader module from memory");
    return VK_NULL_HANDLE;
  }

  return shaderModule;
}


// Finds a suitable memory type based on typeFilter and desired properties.
static uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(cj_engine_physical_device(cur_eng()), &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      return i;
    }
  }
  CJ_ERRORF("Failed to find suitable memory type!");
  return UINT32_MAX;
}





//
// === EVENT PROCESSING ===
//
// Platform event processing lives in src/platform/<platform>/events.c.
// It was an 800-line #ifdef _WIN32 / #else conditional here.
//






//
// === TEXTURED SQUARE ===
//


// Context-based textured helpers (transition away from globals)
static CJ_MUST_CHECK bool createTextureDescriptorPoolCtx(const CJellyVulkanContext* ctx) {
  VkDescriptorPoolSize poolSize = {0};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 1;

  VkDescriptorPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;

  CJellyTexturedResources* tx1 = cur_tx();
  if (vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &tx1->descriptorPool) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create texture descriptor pool (ctx)!");
    return false;
  }
  return true;
}


static CJ_MUST_CHECK bool createDescriptorSetLayoutsCtx(const CJellyVulkanContext* ctx) {
  VkDescriptorSetLayoutBinding layoutBinding = {0};
  layoutBinding.binding = 0;
  layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutBinding.descriptorCount = 1;
  layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &layoutBinding;

  CJellyTexturedResources* tx3 = cur_tx();
  if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &tx3->descriptorSetLayout) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create texture descriptor set layout (ctx)");
    return false;
  }
  return true;
}


static CJ_MUST_CHECK bool allocateTextureDescriptorSetCtx(const CJellyVulkanContext* ctx) {
  VkDescriptorSetAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  CJellyTexturedResources* tx5 = cur_tx();
  allocInfo.descriptorPool = tx5->descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &tx5->descriptorSetLayout;

  if (vkAllocateDescriptorSets(ctx->device, &allocInfo, &tx5->descriptorSet) != VK_SUCCESS) {
    CJ_ERRORF("Failed to allocate texture descriptor set (ctx)!");
    return false;
  }
  return true;
}


static CJ_MUST_CHECK bool createTexturedGraphicsPipelineCtx(const CJellyVulkanContext* ctx) {
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(ctx->device, textured_vert_spv, textured_vert_spv_len);
  VkShaderModule fragShaderModule = createShaderModuleFromMemory(
      ctx->device, textured_frag_spv, textured_frag_spv_len);
  if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
    CJ_ERRORF("Failed to create textured shader modules (ctx)");
    return false;
  }

  VkPipelineShaderStageCreateInfo shaderStages[2] = {0};
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertShaderModule;
  shaderStages[0].pName = "main";
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragShaderModule;
  shaderStages[1].pName = "main";

  VkVertexInputBindingDescription bindingDescription = {0};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(VertexTextured);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributeDescriptions[2] = {0};
  attributeDescriptions[0].binding = 0; attributeDescriptions[0].location = 0; attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT; attributeDescriptions[0].offset = offsetof(VertexTextured, pos);
  attributeDescriptions[1].binding = 0; attributeDescriptions[1].location = 1; attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT; attributeDescriptions[1].offset = offsetof(VertexTextured, texCoord);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = 2;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState = {0};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState = {0};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkPipelineRasterizationStateCreateInfo rasterizer = {0};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo multisampling = {0};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo colorBlending = {0};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  CJellyTexturedResources* tx7 = cur_tx();
  VkDescriptorSetLayout descriptorSetLayouts[] = {tx7->descriptorSetLayout};
  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;
  if (vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &tx7->pipelineLayout) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create textured pipeline layout (ctx)");
    return false;
  }

  VkGraphicsPipelineCreateInfo pipelineInfo = {0};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2; pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = tx7->pipelineLayout;
  pipelineInfo.renderPass = ctx->renderPass;
  pipelineInfo.subpass = 0;
  if (vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &tx7->pipeline) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create textured graphics pipeline (ctx)");
    return false;
  }

  vkDestroyShaderModule(ctx->device, vertShaderModule, NULL);
  vkDestroyShaderModule(ctx->device, fragShaderModule, NULL);
  return true;
}

static CJ_MUST_CHECK bool createBindlessGraphicsPipeline(VkDevice device __attribute__((unused)), VkRenderPass renderPass) {
  CJ_DEBUGF("enter createBindlessGraphicsPipeline");
  // Load SPIR-V binaries and create shader modules for bindless rendering.
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(cur_device(), bindless_vert_spv, bindless_vert_spv_len);
  VkShaderModule fragShaderModule = createShaderModuleFromMemory(
      cur_device(), bindless_frag_spv, bindless_frag_spv_len);

  if (vertShaderModule == VK_NULL_HANDLE ||
      fragShaderModule == VK_NULL_HANDLE) {
    CJ_ERRORF("Failed to create bindless shader modules");
    return false;
  }
  CJ_DEBUGF("shader modules created");

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
  viewport.width = 1.0f;
  viewport.height = 1.0f;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {0};
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent.width = 1;
  scissor.extent.height = 1;

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
  VkDescriptorSetLayout descriptorSetLayouts[] = {VK_NULL_HANDLE};
  VkPushConstantRange pushRange = {0};
  pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(float) * 8; // uv vec4 + colorMul vec4

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;

  CJellyBindlessState* bl = cur_bl();
  if (vkCreatePipelineLayout(cur_device(), &pipelineLayoutInfo, NULL,
          &bl->pipelineLayout) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create bindless pipeline layout");
    return false;
  }
  CJ_DEBUGF("pipeline layout created");

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
  pipelineInfo.layout = bl->pipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(cur_device(), VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
          &bl->pipeline) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create bindless graphics pipeline");
    return false;
  }

  vkDestroyShaderModule(cur_device(), vertShaderModule, NULL);
  vkDestroyShaderModule(cur_device(), fragShaderModule, NULL);
  CJ_DEBUGF("exit createBindlessGraphicsPipeline");
  return true;
}

// Context-friendly pipeline creation that does not touch globals for layout or atlas
static VkResult createBindlessGraphicsPipelineWithLayout(
    VkDevice device,
    VkRenderPass renderPass,
    VkDescriptorSetLayout descriptorSetLayout,
    VkPipelineLayout* outPipelineLayout,
    VkPipeline* outPipeline) {
  // Load SPIR-V binaries and create shader modules for bindless rendering.
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(device, bindless_vert_spv, bindless_vert_spv_len);
  VkShaderModule fragShaderModule = createShaderModuleFromMemory(
      device, bindless_frag_spv, bindless_frag_spv_len);
  if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkPipelineShaderStageCreateInfo shaderStages[2] = {0};
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertShaderModule;
  shaderStages[0].pName = "main";
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragShaderModule;
  shaderStages[1].pName = "main";

  VkVertexInputBindingDescription bindingDescription = {0};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(VertexBindless);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributeDescriptions[3] = {0};
  attributeDescriptions[0].binding = 0; attributeDescriptions[0].location = 0; attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT; attributeDescriptions[0].offset = offsetof(VertexBindless, pos);
  attributeDescriptions[1].binding = 0; attributeDescriptions[1].location = 1; attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; attributeDescriptions[1].offset = offsetof(VertexBindless, color);
  attributeDescriptions[2].binding = 0; attributeDescriptions[2].location = 2; attributeDescriptions[2].format = VK_FORMAT_R32_UINT; attributeDescriptions[2].offset = offsetof(VertexBindless, textureID);

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
  viewport.x = 0.0f; viewport.y = 0.0f; viewport.width = 1.0f; viewport.height = 1.0f; viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
  VkRect2D scissor = {0}; scissor.offset.x = 0; scissor.offset.y = 0; scissor.extent.width = 1; scissor.extent.height = 1;
  VkPipelineViewportStateCreateInfo viewportState = {0};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

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

  VkPushConstantRange pushRange = {0};
  pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(float) * 8;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, outPipelineLayout) != VK_SUCCESS) {
    vkDestroyShaderModule(device, vertShaderModule, NULL);
    vkDestroyShaderModule(device, fragShaderModule, NULL);
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkGraphicsPipelineCreateInfo pipelineInfo = {0};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2; pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = *outPipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, outPipeline);
  vkDestroyShaderModule(device, vertShaderModule, NULL);
  vkDestroyShaderModule(device, fragShaderModule, NULL);
  return res;
}

/// Creates a texture image from a BMP file.
static CJ_MUST_CHECK bool createTextureImageCtx(const CJellyVulkanContext* ctx, const char * filePath) {
  // Load BMP data (assumed to be in 24-bit RGB format)
  CJellyFormatImage * image;
  CJellyFormatImageError error = cjelly_format_image_load(filePath, NULL, &image);
  if (error != CJELLY_FORMAT_IMAGE_SUCCESS) {
    /* One record, not two: these were always a single sentence split across
     * two calls, which a sink receiving them separately could not reunite. */
    CJ_ERRORF("failed to load image %s: %s", filePath,
        cjelly_format_image_strerror(error));
    return false;
  }

  int texWidth = image->raw->width;
  int texHeight = image->raw->height;
  unsigned char * pixels = image->raw->data;
  if (!pixels) {
    CJ_ERRORF("Failed to load image file: %s", filePath);
    cjelly_format_image_free(image);
    return false;
  }

  // The loader already hands back tightly packed RGBA8, which is exactly the
  // VK_FORMAT_R8G8B8A8_UNORM layout used below, so there is nothing to
  // convert and no second buffer to allocate.
  VkDeviceSize bufferSize = image->raw->data_size;

  // Create a staging buffer to hold the pixel data.
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  if (!createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          &stagingBuffer, &stagingBufferMemory)) {
    cjelly_format_image_free(image);
    return false;
  }

  // Map memory and copy the pixel data.
  void * data;
  vkMapMemory(ctx->device, stagingBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, pixels, (size_t)bufferSize);
  vkUnmapMemory(ctx->device, stagingBufferMemory);
  cjelly_format_image_free(image);

  // Create the Vulkan texture image.
  // We choose VK_FORMAT_R8G8B8A8_UNORM for the RGBA data.
  CJellyTexturedResources* tx = cur_tx();
  bool ok = createImageCtx(ctx, texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &tx->image, &tx->imageMemory);

  // Transition image layout to prepare for the data copy.
  if (ok) {
    ok = transitionImageLayoutCtx(ctx, tx->image, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  }

  // Copy the pixel data from the staging buffer into the texture image.
  if (ok) {
    copyBufferToImageCtx(ctx, stagingBuffer, tx->image, texWidth, texHeight);

    // Transition the image layout for shader access.
    ok = transitionImageLayoutCtx(ctx, tx->image, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  vkDestroyBuffer(ctx->device, stagingBuffer, NULL);
  vkFreeMemory(ctx->device, stagingBufferMemory, NULL);

  if (!ok) {
    /* Leave nothing half-built behind: the next caller checks these handles
     * against VK_NULL_HANDLE to decide whether the textured path is set up. */
    if (tx->image != VK_NULL_HANDLE) {
      vkDestroyImage(ctx->device, tx->image, NULL);
      tx->image = VK_NULL_HANDLE;
    }
    if (tx->imageMemory != VK_NULL_HANDLE) {
      vkFreeMemory(ctx->device, tx->imageMemory, NULL);
      tx->imageMemory = VK_NULL_HANDLE;
    }
    return false;
  }
  return true;
}

/// Creates an image view for the texture image.
static CJ_MUST_CHECK bool createTextureImageViewCtx(const CJellyVulkanContext* ctx) {
  VkImageViewCreateInfo viewInfo = {0};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  CJellyTexturedResources* tx = cur_tx();
  viewInfo.image = tx->image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(ctx->device, &viewInfo, NULL, &tx->imageView) !=
      VK_SUCCESS) {
    CJ_ERRORF("Failed to create texture image view");
    return false;
  }
  return true;
}

/// Creates a texture sampler.
static CJ_MUST_CHECK bool createTextureSamplerCtx(const CJellyVulkanContext* ctx) {
  VkSamplerCreateInfo samplerInfo = {0};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;

  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  CJellyTexturedResources* tx2 = cur_tx();
  if (vkCreateSampler(ctx->device, &samplerInfo, NULL, &tx2->sampler) !=
      VK_SUCCESS) {
    CJ_ERRORF("Failed to create texture sampler");
    return false;
  }
  return true;
}

/// Updates a descriptor set with the texture image view and sampler.
static void updateTextureDescriptorSetCtx(const CJellyVulkanContext* ctx, VkDescriptorSet descriptorSet) {
  VkDescriptorImageInfo imageInfo = {0};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  CJellyTexturedResources* tx = cur_tx();
  imageInfo.imageView = tx->imageView;
  imageInfo.sampler = tx->sampler;

  VkWriteDescriptorSet descriptorWrite = {0};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = descriptorSet;
  descriptorWrite.dstBinding =
      0; // Must match the binding in the descriptor set layout.
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(ctx->device, 1, &descriptorWrite, 0, NULL);
}

static CJ_MUST_CHECK bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer * buffer,
    VkDeviceMemory * bufferMemory) {
  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(cur_device(), &bufferInfo, NULL, buffer) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create buffer");
    return false;
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(cur_device(), *buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits, properties);
  if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    vkDestroyBuffer(cur_device(), *buffer, NULL);
    *buffer = VK_NULL_HANDLE;
    return false;
  }

  if (vkAllocateMemory(cur_device(), &allocInfo, NULL, bufferMemory) != VK_SUCCESS) {
    CJ_ERRORF("Failed to allocate buffer memory");
    vkDestroyBuffer(cur_device(), *buffer, NULL);
    *buffer = VK_NULL_HANDLE;
    return false;
  }

  vkBindBufferMemory(cur_device(), *buffer, *bufferMemory, 0);
  return true;
}

static CJ_MUST_CHECK bool createImage(uint32_t width, uint32_t height, VkFormat format,
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

  if (vkCreateImage(cur_device(), &imageInfo, NULL, image) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create image");
    return false;
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(cur_device(), *image, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits, properties);
  if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    vkDestroyImage(cur_device(), *image, NULL);
    *image = VK_NULL_HANDLE;
    return false;
  }

  if (vkAllocateMemory(cur_device(), &allocInfo, NULL, imageMemory) != VK_SUCCESS) {
    CJ_ERRORF("Failed to allocate image memory");
    vkDestroyImage(cur_device(), *image, NULL);
    *image = VK_NULL_HANDLE;
    return false;
  }

  vkBindImageMemory(cur_device(), *image, *imageMemory, 0);
  return true;
}

static VkCommandBuffer beginSingleTimeCommands(void) {
  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = cur_cmd_pool();
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(cur_device(), &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo = {0};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

static void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo = {0};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(cur_gfx_queue(), 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(cur_gfx_queue());

  vkFreeCommandBuffers(cur_device(), cur_cmd_pool(), 1, &commandBuffer);
}

static CJ_MUST_CHECK bool transitionImageLayout(VkImage image, CJ_MAYBE_UNUSED(VkFormat format),
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
    /* Give the open command buffer back before leaving - see the ctx twin. */
    CJ_ERRORF("Unsupported layout transition!");
    endSingleTimeCommands(commandBuffer);
    return false;
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, NULL,
      0, NULL, 1, &barrier);

  endSingleTimeCommands(commandBuffer);
  return true;
}



/**
 * @brief Creates a vertex buffer for a textured square.
 *
 * This function allocates a Vulkan vertex buffer and uploads vertex data
 * from the global 'verticesTextured' array. The VertexTextured structure
 * includes both position and texture coordinates.
 */
static CJ_MUST_CHECK bool createTexturedVertexBuffer(void) {
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

  CJellyTexturedResources* txV = cur_tx();
  if (vkCreateBuffer(cur_device(), &bufferInfo, NULL, &txV->vertexBuffer) !=
      VK_SUCCESS) {
    CJ_ERRORF("Failed to create textured vertex buffer");
    return false;
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(cur_device(), txV->vertexBuffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    vkDestroyBuffer(cur_device(), txV->vertexBuffer, NULL);
    txV->vertexBuffer = VK_NULL_HANDLE;
    return false;
  }

  if (vkAllocateMemory(cur_device(), &allocInfo, NULL, &txV->vertexBufferMemory) !=
      VK_SUCCESS) {
    CJ_ERRORF("Failed to allocate textured vertex buffer memory");
    vkDestroyBuffer(cur_device(), txV->vertexBuffer, NULL);
    txV->vertexBuffer = VK_NULL_HANDLE;
    return false;
  }

  vkBindBufferMemory(
      cur_device(), txV->vertexBuffer, txV->vertexBufferMemory, 0);

  void * data;
  vkMapMemory(cur_device(), txV->vertexBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, verticesTextured, (size_t)bufferSize);
  vkUnmapMemory(cur_device(), txV->vertexBufferMemory);
  return true;
}

/* Context-based textured command buffers for a window */

static CJ_MUST_CHECK bool createBindlessVertexBuffer(VkDevice device __attribute__((unused)), VkCommandPool commandPool __attribute__((unused))) {
  // Create vertices for bindless rendering - single square with dynamic color switching
  VertexBindless verticesBindless[] = {
    // Single square - use white so texture colors pass through
    {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, 1},
    {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 1},
  };

  VkDeviceSize bufferSize = sizeof(verticesBindless);

  CJellyBindlessState* bl = cur_bl();
  if (!createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          &bl->vertexBuffer, &bl->vertexBufferMemory)) {
    return false;
  }

  void * data;
  vkMapMemory(cur_device(), bl->vertexBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, verticesBindless, (size_t)bufferSize);
  vkUnmapMemory(cur_device(), bl->vertexBufferMemory);
  return true;
}

//
// === GLOBAL VULKAN INITIALIZATION & CLEANUP ===
//

/* engine owns bootstrap */

//
// === BINDLESS TEXTURE ATLAS MANAGEMENT ===
//


CJellyTextureAtlas * cjelly_create_texture_atlas(uint32_t width, uint32_t height) {
  CJellyTextureAtlas * atlas = malloc(sizeof(CJellyTextureAtlas));
  if (!atlas) {
    CJ_ERRORF("Failed to allocate memory for texture atlas");
    return NULL;
  }

  memset(atlas, 0, sizeof(CJellyTextureAtlas));
  atlas->atlasWidth = width;
  atlas->atlasHeight = height;
  atlas->nextTextureX = 0;
  atlas->nextTextureY = 0;
  atlas->currentRowHeight = 0;
  atlas->textureCount = 0;

  // Allocate memory for texture entries (per-atlas)
  atlas->maxTextures = 1024;
  atlas->entries = malloc(sizeof(CJellyTextureEntry) * atlas->maxTextures);
  if (!atlas->entries) {
    CJ_ERRORF("Failed to allocate memory for texture entries");
    free(atlas);
    return NULL;
  }
  memset(atlas->entries, 0, sizeof(CJellyTextureEntry) * atlas->maxTextures);

  // Create the atlas image
  if (!createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          &atlas->atlasImage, &atlas->atlasImageMemory)
      // Transition to TRANSFER_DST for subsequent copies
      || !transitionImageLayout(atlas->atlasImage, VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
    CJ_ERRORF("Failed to create the texture atlas image");
    free(atlas->entries);
    free(atlas);
    return NULL;
  }

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

  if (vkCreateImageView(cur_device(), &viewInfo, NULL, &atlas->atlasImageView) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create atlas image view");
    vkDestroyImage(cur_device(), atlas->atlasImage, NULL);
    vkFreeMemory(cur_device(), atlas->atlasImageMemory, NULL);
    free(atlas->entries);
    free(atlas);
    return NULL;
  }

  // Create sampler (reuse textured resources' sampler)
  {
    CJellyTexturedResources* tx = cur_tx();
    atlas->atlasSampler = tx ? tx->sampler : VK_NULL_HANDLE;
  }

  // Create descriptor set layout (single combined sampler)
  atlas->bindlessDescriptorSetLayout = cj_engine_bindless_layout(cur_eng());

  // Create descriptor pool
  atlas->bindlessDescriptorPool = cj_engine_bindless_pool(cur_eng());

  // Allocate descriptor set
  VkDescriptorSetAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = cj_engine_bindless_pool(cur_eng());
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &atlas->bindlessDescriptorSetLayout;
  allocInfo.pNext = NULL;

  if (vkAllocateDescriptorSets(cur_device(), &allocInfo, &atlas->bindlessDescriptorSet) != VK_SUCCESS) {
    CJ_ERRORF("Failed to allocate bindless descriptor set");
    vkDestroyDescriptorPool(cur_device(), atlas->bindlessDescriptorPool, NULL);
    vkDestroyDescriptorSetLayout(cur_device(), atlas->bindlessDescriptorSetLayout, NULL);
    vkDestroyImageView(cur_device(), atlas->atlasImageView, NULL);
    vkDestroyImage(cur_device(), atlas->atlasImage, NULL);
    vkFreeMemory(cur_device(), atlas->atlasImageMemory, NULL);
    free(atlas->entries);
    free(atlas);
    return NULL;
  }

  return atlas;
}

// Context-based atlas creation (uses context device instead of global)
CJellyTextureAtlas * cjelly_create_texture_atlas_ctx(const CJellyVulkanContext* ctx, uint32_t width, uint32_t height) {
  CJellyTextureAtlas * atlas = malloc(sizeof(CJellyTextureAtlas));
  if (!atlas) {
    CJ_ERRORF("Failed to allocate memory for texture atlas");
    return NULL;
  }

  memset(atlas, 0, sizeof(CJellyTextureAtlas));
  atlas->atlasWidth = width;
  atlas->atlasHeight = height;
  atlas->nextTextureX = 0;
  atlas->nextTextureY = 0;
  atlas->currentRowHeight = 0;
  atlas->textureCount = 0;

  // Allocate memory for texture entries (per-atlas)
  atlas->maxTextures = 1024;
  atlas->entries = malloc(sizeof(CJellyTextureEntry) * atlas->maxTextures);
  if (!atlas->entries) {
    CJ_ERRORF("Failed to allocate memory for texture entries");
    free(atlas);
    return NULL;
  }
  memset(atlas->entries, 0, sizeof(CJellyTextureEntry) * atlas->maxTextures);

  // Create the atlas image using context device
  if (!createImageCtx(ctx, width, height, VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          &atlas->atlasImage, &atlas->atlasImageMemory)
      // Transition to TRANSFER_DST for subsequent copies
      || !transitionImageLayoutCtx(ctx, atlas->atlasImage, VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
    CJ_ERRORF("Failed to create the texture atlas image");
    free(atlas->entries);
    free(atlas);
    return NULL;
  }

  // Create image view using context device
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

  if (vkCreateImageView(ctx->device, &viewInfo, NULL, &atlas->atlasImageView) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create atlas image view (ctx)");
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(atlas->entries);
    free(atlas);
    return NULL;
  }

  // Create sampler using context device
  VkSamplerCreateInfo samplerInfo = {0};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  if (vkCreateSampler(ctx->device, &samplerInfo, NULL, &atlas->atlasSampler) != VK_SUCCESS) {
    CJ_ERRORF("Failed to create atlas sampler (ctx)");
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(atlas->entries);
    free(atlas);
    return NULL;
  }

  // Use the engine's shared bindless descriptor set layout and pool
  atlas->bindlessDescriptorSetLayout = cj_engine_bindless_layout(cur_eng());
  if (atlas->bindlessDescriptorSetLayout == VK_NULL_HANDLE) {
    CJ_ERRORF("Failed to get engine bindless descriptor set layout (ctx)");
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(atlas->entries);
    free(atlas);
    return NULL;
  }
  atlas->bindlessDescriptorPool = cj_engine_bindless_pool(cur_eng());
  if (atlas->bindlessDescriptorPool == VK_NULL_HANDLE) {
    CJ_ERRORF("Failed to get engine bindless descriptor pool (ctx)");
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(atlas->entries);
    free(atlas);
    return NULL;
  }

  // Allocate descriptor set using context device
  VkDescriptorSetAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = atlas->bindlessDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &atlas->bindlessDescriptorSetLayout;
  allocInfo.pNext = NULL;

  if (vkAllocateDescriptorSets(ctx->device, &allocInfo, &atlas->bindlessDescriptorSet) != VK_SUCCESS) {
    CJ_ERRORF("Failed to allocate bindless descriptor set (ctx)");
  /* engine-owned pool/layout are not destroyed here */
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(atlas->entries);
    free(atlas);
    return NULL;
  }

  return atlas;
}

// Context-based atlas destruction (uses context device instead of global)
void cjelly_destroy_texture_atlas_ctx(CJellyTextureAtlas * atlas, const CJellyVulkanContext* ctx) {
  if (!atlas) return;

  vkDestroySampler(ctx->device, atlas->atlasSampler, NULL);
  /* layout/pool are engine-owned; do not destroy here */
  vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
  vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
  vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);

  if (atlas->entries) { free(atlas->entries); atlas->entries = NULL; }

  free(atlas);
}

void cjelly_destroy_texture_atlas(CJellyTextureAtlas * atlas) {
  if (!atlas) return;

  /* layout/pool are engine-owned; do not destroy here */
  vkDestroyImageView(cur_device(), atlas->atlasImageView, NULL);
  vkDestroyImage(cur_device(), atlas->atlasImage, NULL);
  vkFreeMemory(cur_device(), atlas->atlasImageMemory, NULL);

  if (atlas->entries) { free(atlas->entries); atlas->entries = NULL; }

  free(atlas);
}

uint32_t cjelly_atlas_add_texture(CJellyTextureAtlas * atlas, const char * filePath) {
  if (!atlas || atlas->textureCount >= atlas->maxTextures) {
    return 0; // Invalid texture ID
  }

  // Load the image
  CJellyFormatImage * image;
  if (cjelly_format_image_load(filePath, NULL, &image) != CJELLY_FORMAT_IMAGE_SUCCESS) {
    CJ_ERRORF("Failed to load texture: %s", filePath);
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
    CJ_ERRORF("Texture atlas is full");
    cjelly_format_image_free(image);
    return 0;
  }

  // Create staging buffer for the texture
  VkDeviceSize imageSize = texWidth * texHeight * 4; // RGBA
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  if (!createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          &stagingBuffer, &stagingBufferMemory)) {
    /* 0 is "no texture" here, which is what the callers already check. */
    cjelly_format_image_free(image);
    return 0;
  }

  // Copy image data to staging buffer
  void * data;
  vkMapMemory(cur_device(), stagingBufferMemory, 0, imageSize, 0, &data);

  // The loader hands back tightly packed RGBA8, the same layout the atlas
  // image uses, so the pixels copy straight across.
  memcpy(data, image->raw->data, (size_t)imageSize);

  vkUnmapMemory(cur_device(), stagingBufferMemory);

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
  CJellyTextureEntry * entry = &atlas->entries[atlas->textureCount];
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
  vkDestroyBuffer(cur_device(), stagingBuffer, NULL);
  vkFreeMemory(cur_device(), stagingBufferMemory, NULL);
  cjelly_format_image_free(image);

  return textureID;
}

// Context-based texture addition (uses context device instead of global)
uint32_t cjelly_atlas_add_texture_ctx(CJellyTextureAtlas * atlas, const char * filePath, const CJellyVulkanContext* ctx) {
  if (!atlas || atlas->textureCount >= atlas->maxTextures) {
    return 0; // Invalid texture ID
  }

  // Load the image
  CJellyFormatImage * image;
  if (cjelly_format_image_load(filePath, NULL, &image) != CJELLY_FORMAT_IMAGE_SUCCESS) {
    CJ_ERRORF("Failed to load texture: %s", filePath);
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
    CJ_ERRORF("Texture atlas is full");
    cjelly_format_image_free(image);
    return 0;
  }

  // Create staging buffer for the texture using context device
  VkDeviceSize imageSize = texWidth * texHeight * 4; // RGBA
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  if (!createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          &stagingBuffer, &stagingBufferMemory)) {
    /* 0 is "no texture" here, which is what the callers already check. */
    cjelly_format_image_free(image);
    return 0;
  }

  // Copy image data to staging buffer using context device
  void * data;
  vkMapMemory(ctx->device, stagingBufferMemory, 0, imageSize, 0, &data);

  // The loader hands back tightly packed RGBA8, the same layout the atlas
  // image uses, so the pixels copy straight across.
  memcpy(data, image->raw->data, (size_t)imageSize);

  vkUnmapMemory(ctx->device, stagingBufferMemory);

  // Copy staging buffer to atlas image at the correct position using context command buffer
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

  VkCommandBuffer commandBuffer = beginSingleTimeCommandsCtx(ctx);
  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, atlas->atlasImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  endSingleTimeCommandsCtx(ctx, commandBuffer);

  // Store texture entry
  uint32_t textureID = atlas->textureCount + 1; // Start from 1, 0 means no texture
  CJellyTextureEntry * entry = &atlas->entries[atlas->textureCount];
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
  vkDestroyBuffer(ctx->device, stagingBuffer, NULL);
  vkFreeMemory(ctx->device, stagingBufferMemory, NULL);
  cjelly_format_image_free(image);

  return textureID;
}

CJellyTextureEntry * cjelly_atlas_get_texture_entry(CJellyTextureAtlas * atlas, uint32_t textureID) {
  if (!atlas || textureID == 0 || textureID > atlas->textureCount) {
    return NULL;
  }

  return &atlas->entries[textureID - 1];
}

void cjelly_atlas_update_descriptor_set(CJellyTextureAtlas * atlas) {
  if (!atlas) return;

  // Update the descriptor set with the atlas image view
  VkDescriptorImageInfo imageInfo = {0};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  // Use atlas-provided view and sampler
  imageInfo.imageView = atlas->atlasImageView;
  imageInfo.sampler = atlas->atlasSampler;

  VkWriteDescriptorSet descriptorWrite = {0};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = atlas->bindlessDescriptorSet;
  descriptorWrite.dstBinding = 0;
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(cur_device(), 1, &descriptorWrite, 0, NULL);
}

// Context-based descriptor set update (uses context device instead of global)
static void cjelly_atlas_update_descriptor_set_ctx(CJellyTextureAtlas * atlas, const CJellyVulkanContext* ctx) {
  if (!atlas) return;

  // Update the descriptor set with the atlas image view
  VkDescriptorImageInfo imageInfo = {0};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = atlas->atlasImageView;
  imageInfo.sampler = atlas->atlasSampler;

  VkWriteDescriptorSet descriptorWrite = {0};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = atlas->bindlessDescriptorSet;
  descriptorWrite.dstBinding = 0;
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(ctx->device, 1, &descriptorWrite, 0, NULL);
}

/* Public wrapper used by window API to build textured path using a context.
 *
 * Nine steps, each of which used to end the host process if it failed. A
 * library does not get to make that decision - a program that could have
 * carried on without a textured window instead died inside a call it made,
 * with no return value to inspect and no way to prevent it. They report
 * now, and this stops at the first one rather than building the rest on top
 * of something that is not there.
 *
 * @return true when the whole textured path is ready to draw with. */
CJ_MUST_CHECK bool cjelly_init_textured_pipeline_ctx(const CJellyVulkanContext* ctx) {
  if (!ctx || ctx->device == VK_NULL_HANDLE) return false;
  // Avoid recreating global textured resources multiple times (multi-window init)
  CJellyTexturedResources* tx = cur_tx();
  if (tx && (tx->pipeline != VK_NULL_HANDLE || tx->image != VK_NULL_HANDLE)) return true;
  if (!createTextureImageCtx(ctx, "test/images/bmp/tang.bmp")) return false;
  if (!createTexturedVertexBuffer()) return false;
  if (!createTextureImageViewCtx(ctx)) return false;
  if (!createTextureSamplerCtx(ctx)) return false;
  if (!createDescriptorSetLayoutsCtx(ctx)) return false;
  if (!createTextureDescriptorPoolCtx(ctx)) return false;
  if (!allocateTextureDescriptorSetCtx(ctx)) return false;
  updateTextureDescriptorSetCtx(ctx, tx ? tx->descriptorSet : VK_NULL_HANDLE);
  return createTexturedGraphicsPipelineCtx(ctx);
}
