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
#include <cjelly/runtime.h>
#include <cjelly/engine_internal.h>
#include <cjelly/bindless_internal.h>
#include <cjelly/textured_internal.h>
#include <cjelly/bindless_state_internal.h>
#include <cjelly/basic_state_internal.h>
#include <cjelly/runtime.h>
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#else
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif
#include <cjelly/format/image.h>
#include <cjelly/macros.h>
#include <cjelly/runtime.h>
#include <shaders/basic.frag.h>
#include <shaders/basic.vert.h>
#include <shaders/color.vert.h>
#include <shaders/color.frag.h>
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

/* All Vulkan objects are engine-owned; read via engine getters */

/* Textured resources moved under engine; use cj_engine_textured() */

/* Bindless state moved under engine; use cj_engine_bindless() */
// Bindless texture atlas global (used by pipeline creation)
// Deprecated: global atlas removed in favor of resources->textureAtlas
// CJellyTextureAtlas * bindlessTextureAtlas = NULL;


// Global flag to indicate that the window should close.
int shouldClose;

// Global flag to enable validation layers.
int enableValidationLayers;

// Global debug messenger handle.
VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

// Global constants for window dimensions.
static const int WIDTH = 800;
static const int HEIGHT = 600;

/* Helpers to read from current engine (Phase 5: no legacy compat) */
static inline cj_engine_t* cur_eng(void) { return cj_engine_get_current(); }
static inline VkDevice cur_device(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_device(e) : VK_NULL_HANDLE; }
static inline VkRenderPass cur_render_pass(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_render_pass(e) : VK_NULL_HANDLE; }
static inline VkQueue cur_gfx_queue(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_graphics_queue(e) : VK_NULL_HANDLE; }
static inline VkQueue cur_present_queue(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_present_queue(e) : VK_NULL_HANDLE; }
static inline VkCommandPool cur_cmd_pool(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_command_pool(e) : VK_NULL_HANDLE; }
static inline CJellyTexturedResources* cur_tx(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_textured(e) : NULL; }
static inline CJellyBindlessState* cur_bl(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_bindless(e) : NULL; }
static inline CJellyBasicState* cur_basic(void) { cj_engine_t* e = cur_eng(); return e ? cj_engine_basic(e) : NULL; }


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
VkCommandBuffer beginSingleTimeCommands(void);
void endSingleTimeCommands(VkCommandBuffer commandBuffer);
void transitionImageLayout(VkImage image, VkFormat format,
    VkImageLayout oldLayout, VkImageLayout newLayout);
void copyBufferToImage(
    VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
VkShaderModule createShaderModuleFromMemory(VkDevice device, const unsigned char * code, size_t codeSize);
void createDebugMessenger(void);
/* legacy cleanup removed; engine owns shutdown */

// Forward declarations for bindless functions:
void createBindlessVertexBuffer(VkDevice device, VkCommandPool commandPool);
void createBindlessGraphicsPipeline(VkDevice device, VkRenderPass renderPass);

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

/* duplicate removed */

/* Bindless internal layout moved to include/cjelly/bindless_internal.h */

// Context-based atlas helpers (defined later)
static void cjelly_atlas_update_descriptor_set_ctx(CJellyTextureAtlas * atlas, const CJellyVulkanContext* ctx);

/* Public wrappers for runtime.h */
/* forward declare OS function to avoid implicit warning */
CJ_API void processWindowEvents(void);
CJ_API void cj_poll_events(void) { processWindowEvents(); }
CJ_API int  cj_should_close(void) { return shouldClose; }
CJ_API void cj_set_should_close(int v) { shouldClose = v; }

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
  VertexBindless vertices[] = {
    // Single quad matching textured size: [-0.5,0.5]
    {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{ 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{-0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
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
  fprintf(stderr, "Failed to find suitable memory type (ctx)!\n");
  exit(EXIT_FAILURE);
}

static void createImageCtx(const CJellyVulkanContext* ctx, uint32_t width, uint32_t height, VkFormat format,
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
    fprintf(stderr, "Failed to create image (ctx)\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(ctx->device, *image, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryTypeCtx(ctx, memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(ctx->device, &allocInfo, NULL, imageMemory) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate image memory (ctx)\n");
    exit(EXIT_FAILURE);
  }

  vkBindImageMemory(ctx->device, *image, *imageMemory, 0);
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

static void transitionImageLayoutCtx(const CJellyVulkanContext* ctx, VkImage image, VkFormat format,
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
    fprintf(stderr, "Unsupported layout transition (ctx)!\n");
    exit(EXIT_FAILURE);
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &barrier);

  endSingleTimeCommandsCtx(ctx, commandBuffer);
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
static void createBindlessVertexBufferCtx(
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
  createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               outBuffer, outMemory);
  void* data = NULL;
  vkMapMemory(device, *outMemory, 0, bufferSize, 0, &data);
  memcpy(data, verticesBindless, (size_t)bufferSize);
  vkUnmapMemory(device, *outMemory);
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
    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) printf("DEBUG: Creating bindless resources...\n");
    const char* stageEnv = getenv("CJELLY_BINDLESS_STAGE");
    int stage = stageEnv ? atoi(stageEnv) : 2; // default full
    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: CJELLY_BINDLESS_STAGE=%d\n", stage);

    CJellyBindlessResources* resources = malloc(sizeof(CJellyBindlessResources));
    if (!resources) {
        fprintf(stderr, "Failed to allocate bindless resources\n");
        return NULL;
    }

    memset(resources, 0, sizeof(CJellyBindlessResources));

    // Create texture atlas
    CJellyTextureAtlas* atlas = cjelly_create_texture_atlas(2048, 2048);
    if (!atlas) {
        fprintf(stderr, "Failed to create texture atlas\n");
        free(resources);
        return NULL;
    }

    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) printf("DEBUG: Texture atlas created\n");

    // Add textures to atlas
    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Adding textures to atlas...\n");
    uint32_t tex1 = cjelly_atlas_add_texture(atlas, "test/images/bmp/tang.bmp");
    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: tex1 id=%u\n", tex1);
    uint32_t tex2 = cjelly_atlas_add_texture(atlas, "test/images/bmp/16Color.bmp");
    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: tex2 id=%u\n", tex2);

    if (tex1 == 0 || tex2 == 0) {
        fprintf(stderr, "Failed to add textures to atlas\n");
        cjelly_destroy_texture_atlas(atlas);
        free(resources);
        return NULL;
    }

    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Textures added to atlas\n");

    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Transition atlas to SHADER_READ_ONLY\n");
    // Atlas image was created and filled per-texture via copyBufferToImage; ensure final layout
    transitionImageLayout(atlas->atlasImage, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Update descriptor set\n");
    // Update descriptor set
    cjelly_atlas_update_descriptor_set(atlas);

    // Create vertex buffer
    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Create bindless vertex buffer\n");
    createBindlessVertexBuffer(cur_device(), cur_cmd_pool());
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
      /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Create bindless graphics pipeline\n");
      createBindlessGraphicsPipeline(cur_device(), cur_render_pass());
      CJellyBindlessState* bl2 = cur_bl();
      /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: After pipeline creation, bindless pipeline=%p layout=%p\n", (void*)bl2->pipeline, (void*)bl2->pipelineLayout);
      resources->pipeline = bl2->pipeline;
      /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Assigned resources->pipeline\n");
      resources->pipelineLayout = bl2->pipelineLayout;
      /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Assigned resources->pipelineLayout\n");
    } else {
      /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: Skipping bindless pipeline creation due to stage %d\n", stage);
      resources->pipeline = VK_NULL_HANDLE;
      resources->pipelineLayout = VK_NULL_HANDLE;
    }


    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) printf("DEBUG: Bindless resources created successfully\n");
    /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: About to return resources=%p\n", (void*)resources);
    return resources;
}

// Deprecated shim removed; using full ctx version below

CJ_API CJellyBindlessResources* cjelly_create_bindless_resources_ctx(const CJellyVulkanContext* ctx) {
    if (!ctx || ctx->device == VK_NULL_HANDLE || ctx->commandPool == VK_NULL_HANDLE || ctx->renderPass == VK_NULL_HANDLE) {
        fprintf(stderr, "Invalid Vulkan context passed to cjelly_create_bindless_resources_ctx\n");
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
    transitionImageLayoutCtx(ctx, atlas->atlasImage, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cjelly_atlas_update_descriptor_set_ctx(atlas, ctx);

    // Create vertex buffer into resources using context
    VkBuffer vb = VK_NULL_HANDLE; VkDeviceMemory vm = VK_NULL_HANDLE;
  createBindlessVertexBufferCtx(ctx->device, ctx->commandPool, &vb, &vm);
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
    createBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &resources->vertexBuffer, &resources->vertexBufferMemory);
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

    VkViewport vp = {0}; vp.x=0; vp.y=0; vp.width=(float)WIDTH; vp.height=(float)HEIGHT; vp.minDepth=0; vp.maxDepth=1;
    VkRect2D sc = {0}; sc.extent.width = WIDTH; sc.extent.height = HEIGHT;
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
    createBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &resources->vertexBuffer, &resources->vertexBufferMemory);
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

    VkViewport vp = {0}; vp.x=0; vp.y=0; vp.width=(float)WIDTH; vp.height=(float)HEIGHT; vp.minDepth=0; vp.maxDepth=1;
    VkRect2D sc = {0}; sc.extent.width = WIDTH; sc.extent.height = HEIGHT;
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
  vkGetPhysicalDeviceMemoryProperties(cj_engine_physical_device(cur_eng()), &memProperties);
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

  if (getenv("CJELLY_DEBUG")) fprintf(stderr, "Validation layer: %s\n", pCallbackData->pMessage);
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

/* Use shared internal window definition */
#include <cjelly/window_internal.h>
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
  // Select basic events so we receive destroy notifications and key presses
  XSelectInput(display, win->handle, StructureNotifyMask | KeyPressMask | ExposureMask);
  Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XStoreName(display, win->handle, title);
  XSetWMProtocols(display, win->handle, &wmDelete, 1);
  XMapWindow(display, win->handle);
  XFlush(display);

#endif

  win->needsRedraw = 1;
  win->nextFrameTime = 0;
}


//
// === EVENT PROCESSING (PLATFORM-SPECIFIC) ===
//

#ifdef _WIN32

CJ_API void processWindowEvents() {
  MSG msg;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

#else

CJ_API void processWindowEvents() {
  while (XPending(display)) {
    XEvent event;
    XNextEvent(display, &event);
    if (event.type == ClientMessage) {
      Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
      if ((Atom)event.xclient.data.l[0] == wmDelete) {
        shouldClose = 1;
      }
    }
    if (event.type == DestroyNotify) {
      shouldClose = 1;
    }
    if (event.type == KeyPress) {
      // Close on Escape for convenience
      KeySym sym = XLookupKeysym(&event.xkey, 0);
      if (sym == XK_Escape) shouldClose = 1;
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
  if (vkCreateWin32SurfaceKHR(cj_engine_instance(cur_eng()), &createInfo, NULL, &win->surface) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create Win32 surface\n");
    exit(EXIT_FAILURE);
  }

#else

  VkXlibSurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  createInfo.dpy = display;
  createInfo.window = win->handle;
  if (vkCreateXlibSurfaceKHR(cj_engine_instance(cur_eng()), &createInfo, NULL, &win->surface) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create Xlib surface\n");
    exit(EXIT_FAILURE);
  }

#endif
  if (getenv("CJELLY_DEBUG")) fprintf(stderr, "createSurfaceForWindow: surface=%p\n", (void*)win->surface);
}


// Create the swap chain for a window.
void createSwapChainForWindow(CJellyWindow * win) {
  // In a full implementation, we would query physical device surface support
  // and choose formats/present modes. For now, we use defaults.
  VkSurfaceCapabilitiesKHR capabilities;
  VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      cj_engine_physical_device(cur_eng()), win->surface, &capabilities);
  if (getenv("CJELLY_DEBUG")) fprintf(stderr, "getSurfaceCaps: res=%d curExtent=%ux%u minImages=%u\n", r,
      capabilities.currentExtent.width, capabilities.currentExtent.height, capabilities.minImageCount);

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

  r = vkCreateSwapchainKHR(cur_device(), &createInfo, NULL, &win->swapChain);
  /* Ensure engine render pass matches swapchain format before creating views/fbos */
  cj_engine_ensure_render_pass(cur_eng(), createInfo.imageFormat);
  if (getenv("CJELLY_DEBUG")) fprintf(stderr, "vkCreateSwapchainKHR: res=%d swap=%p\n", r, (void*)win->swapChain);
  if (r != VK_SUCCESS || win->swapChain == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to create swap chain (res=%d)\n", r);
    exit(EXIT_FAILURE);
  }
}


// Create image views for the swap chain images.
void createImageViewsForWindow(CJellyWindow * win) {
  VkResult r = vkGetSwapchainImagesKHR(
      cur_device(), win->swapChain, &win->swapChainImageCount, NULL);
  if (getenv("CJELLY_DEBUG")) fprintf(stderr, "getSwapchainImages(count): res=%d count=%u\n", r, win->swapChainImageCount);
  win->swapChainImages = malloc(sizeof(VkImage) * win->swapChainImageCount);
  r = vkGetSwapchainImagesKHR(
      cur_device(), win->swapChain, &win->swapChainImageCount, win->swapChainImages);
  if (getenv("CJELLY_DEBUG")) fprintf(stderr, "getSwapchainImages(images): res=%d\n", r);
  if (r != VK_SUCCESS || !win->swapChainImages) { fprintf(stderr, "Failed to get swapchain images\n"); exit(EXIT_FAILURE);} 

  win->swapChainImageViews =
      malloc(sizeof(VkImageView) * win->swapChainImageCount);
  if (cur_device() == VK_NULL_HANDLE) { fprintf(stderr, "ERROR: cur_device() is NULL before creating image views\n"); exit(EXIT_FAILURE);} 
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

    VkResult vr = vkCreateImageView(cur_device(), &viewInfo, NULL,
            &win->swapChainImageViews[i]);
    if (vr != VK_SUCCESS) {
      fprintf(stderr, "Failed to create image view\n");
      exit(EXIT_FAILURE);
    }
    if (getenv("CJELLY_DEBUG")) fprintf(stderr, "created image view[%u]=%p\n", i, (void*)win->swapChainImageViews[i]);
  }
}


// Create framebuffers for the window.
void createFramebuffersForWindow(CJellyWindow * win) {
  win->swapChainFramebuffers =
      malloc(sizeof(VkFramebuffer) * win->swapChainImageCount);
  if (getenv("CJELLY_DEBUG")) fprintf(stderr, "createFramebuffers: renderPass=%p\n", (void*)cur_render_pass());
  for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
    VkImageView attachments[] = {win->swapChainImageViews[i]};
    VkFramebufferCreateInfo framebufferInfo = {0};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = cur_render_pass();
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = win->swapChainExtent.width;
    framebufferInfo.height = win->swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(cj_engine_device(cur_eng()), &framebufferInfo, NULL,
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
  allocInfo.commandPool = cur_cmd_pool();
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = win->swapChainImageCount;

  if (vkAllocateCommandBuffers(cur_device(), &allocInfo, win->commandBuffers) !=
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
    renderPassInfo.renderPass = cur_render_pass();
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
    CJellyBasicState* bs = cur_basic();
    VkBuffer vb = bs ? bs->vertexBuffer : VK_NULL_HANDLE;
    vkCmdBindVertexBuffers(
        win->commandBuffers[i], 0, 1, &vb, offsets);

    vkCmdBindPipeline(win->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
        bs ? bs->pipeline : VK_NULL_HANDLE);
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

  if (vkCreateSemaphore(cur_device(), &semaphoreInfo, NULL,
          &win->imageAvailableSemaphore) != VK_SUCCESS ||
      vkCreateSemaphore(cur_device(), &semaphoreInfo, NULL,
          &win->renderFinishedSemaphore) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create semaphores\n");
    exit(EXIT_FAILURE);
  }

  VkFenceCreateInfo fenceInfo = {0};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateFence(cur_device(), &fenceInfo, NULL, &win->inFlightFence) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create fence\n");
    exit(EXIT_FAILURE);
  }
}

//
// === DRAWING A FRAME PER WINDOW ===
//

void drawFrameForWindow(CJellyWindow * win) {
  vkWaitForFences(cur_device(), 1, &win->inFlightFence, VK_TRUE, UINT64_MAX);
  vkResetFences(cur_device(), 1, &win->inFlightFence);

  uint32_t imageIndex;
  vkAcquireNextImageKHR(cur_device(), win->swapChain, UINT64_MAX,
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

  if (vkQueueSubmit(cur_gfx_queue(), 1, &submitInfo, win->inFlightFence) !=
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

  vkQueuePresentKHR(cur_present_queue(), &presentInfo);
}


//
// === CLEANUP FOR A WINDOW ===
//

void cleanupWindow(CJellyWindow * win) {
  if (!win) return;
  VkDevice dev = cur_device();
  VkCommandPool pool = cur_cmd_pool();
  VkInstance inst = cj_engine_instance(cur_eng());

  if (dev != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(dev);
  }

  if (dev != VK_NULL_HANDLE) {
    if (win->renderFinishedSemaphore) { vkDestroySemaphore(dev, win->renderFinishedSemaphore, NULL); win->renderFinishedSemaphore = VK_NULL_HANDLE; }
    if (win->imageAvailableSemaphore) { vkDestroySemaphore(dev, win->imageAvailableSemaphore, NULL); win->imageAvailableSemaphore = VK_NULL_HANDLE; }
    if (win->inFlightFence) { vkDestroyFence(dev, win->inFlightFence, NULL); win->inFlightFence = VK_NULL_HANDLE; }
  }

  // Free command buffers allocated from the engine command pool
  if (dev != VK_NULL_HANDLE && pool != VK_NULL_HANDLE && win->commandBuffers && win->swapChainImageCount > 0) {
    vkFreeCommandBuffers(dev, pool, win->swapChainImageCount, win->commandBuffers);
  }
  if (win->commandBuffers) { free(win->commandBuffers); win->commandBuffers = NULL; }

  if (dev != VK_NULL_HANDLE) {
    if (win->swapChainFramebuffers) {
      for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
        if (win->swapChainFramebuffers[i]) vkDestroyFramebuffer(dev, win->swapChainFramebuffers[i], NULL);
      }
    }
    if (win->swapChainImageViews) {
      for (uint32_t i = 0; i < win->swapChainImageCount; i++) {
        if (win->swapChainImageViews[i]) vkDestroyImageView(dev, win->swapChainImageViews[i], NULL);
      }
    }
  }
  if (win->swapChainFramebuffers) { free(win->swapChainFramebuffers); win->swapChainFramebuffers = NULL; }
  if (win->swapChainImageViews) { free(win->swapChainImageViews); win->swapChainImageViews = NULL; }
  if (win->swapChainImages) { free(win->swapChainImages); win->swapChainImages = NULL; }

  if (dev != VK_NULL_HANDLE && win->swapChain) { vkDestroySwapchainKHR(dev, win->swapChain, NULL); win->swapChain = VK_NULL_HANDLE; }
  if (inst != VK_NULL_HANDLE && win->surface) { vkDestroySurfaceKHR(inst, win->surface, NULL); win->surface = VK_NULL_HANDLE; }

#ifdef _WIN32
  if (win->handle) { DestroyWindow(win->handle); win->handle = NULL; }
#else
  extern Display* display;
  if (display && win->handle) { XDestroyWindow(display, win->handle); win->handle = 0; }
#endif
  win->swapChainImageCount = 0;
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

  CJellyTexturedResources* tx0 = cur_tx();
  if (vkCreateDescriptorPool(cur_device(), &poolInfo, NULL, &tx0->descriptorPool) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture descriptor pool!\n");
    exit(EXIT_FAILURE);
  }
}

// Context-based textured helpers (transition away from globals)
static void createTextureDescriptorPoolCtx(const CJellyVulkanContext* ctx) {
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
    fprintf(stderr, "Failed to create texture descriptor pool (ctx)!\n");
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

  CJellyTexturedResources* tx2 = cur_tx();
  if (vkCreateDescriptorSetLayout(cur_device(), &layoutInfo, NULL,
          &tx2->descriptorSetLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture descriptor set layout\n");
    exit(EXIT_FAILURE);
  }
}

static void createDescriptorSetLayoutsCtx(const CJellyVulkanContext* ctx) {
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
    fprintf(stderr, "Failed to create texture descriptor set layout (ctx)\n");
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
  CJellyTexturedResources* tx4 = cur_tx();
  allocInfo.descriptorPool = tx4->descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &tx4->descriptorSetLayout;

  if (vkAllocateDescriptorSets(cur_device(), &allocInfo, &tx4->descriptorSet) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate texture descriptor set!\n");
    exit(EXIT_FAILURE);
  }
}

static void allocateTextureDescriptorSetCtx(const CJellyVulkanContext* ctx) {
  VkDescriptorSetAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  CJellyTexturedResources* tx5 = cur_tx();
  allocInfo.descriptorPool = tx5->descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &tx5->descriptorSetLayout;

  if (vkAllocateDescriptorSets(ctx->device, &allocInfo, &tx5->descriptorSet) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate texture descriptor set (ctx)!\n");
    exit(EXIT_FAILURE);
  }
}

void createTexturedGraphicsPipeline() {
  // Load SPIR-V binaries and create shader modules for texturing.
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(cur_device(), basic_vert_spv, basic_vert_spv_len);
  VkShaderModule fragShaderModule = createShaderModuleFromMemory(
      cur_device(), textured_frag_spv, textured_frag_spv_len);

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

  CJellyTexturedResources* tx6 = cur_tx();
  VkDescriptorSetLayout descriptorSetLayouts[] = {tx6->descriptorSetLayout};
  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;

  if (vkCreatePipelineLayout(cur_device(), &pipelineLayoutInfo, NULL,
          &tx6->pipelineLayout) != VK_SUCCESS) {
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
  pipelineInfo.layout = tx6->pipelineLayout; // Use the new layout.
  pipelineInfo.renderPass = cur_render_pass();
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(cur_device(), VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
          &tx6->pipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create textured graphics pipeline\n");
    exit(EXIT_FAILURE);
  }

  // Clean up shader modules after pipeline creation.
  vkDestroyShaderModule(cur_device(), vertShaderModule, NULL);
  vkDestroyShaderModule(cur_device(), fragShaderModule, NULL);
}

static void createTexturedGraphicsPipelineCtx(const CJellyVulkanContext* ctx) {
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(ctx->device, basic_vert_spv, basic_vert_spv_len);
  VkShaderModule fragShaderModule = createShaderModuleFromMemory(
      ctx->device, textured_frag_spv, textured_frag_spv_len);
  if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to create textured shader modules (ctx)\n");
    exit(EXIT_FAILURE);
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
    fprintf(stderr, "Failed to create textured pipeline layout (ctx)\n");
    exit(EXIT_FAILURE);
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
    fprintf(stderr, "Failed to create textured graphics pipeline (ctx)\n");
    exit(EXIT_FAILURE);
  }

  vkDestroyShaderModule(ctx->device, vertShaderModule, NULL);
  vkDestroyShaderModule(ctx->device, fragShaderModule, NULL);
}

void createBindlessGraphicsPipeline(VkDevice device __attribute__((unused)), VkRenderPass renderPass) {
  /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: enter createBindlessGraphicsPipeline\n");
  // Load SPIR-V binaries and create shader modules for bindless rendering.
  VkShaderModule vertShaderModule =
      createShaderModuleFromMemory(cur_device(), bindless_vert_spv, bindless_vert_spv_len);
  VkShaderModule fragShaderModule = createShaderModuleFromMemory(
      cur_device(), bindless_frag_spv, bindless_frag_spv_len);

  if (vertShaderModule == VK_NULL_HANDLE ||
      fragShaderModule == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to create bindless shader modules\n");
    exit(EXIT_FAILURE);
  }
  /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: shader modules created\n");

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
  // Legacy path retained but global atlas removed; do nothing here
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
    fprintf(stderr, "Failed to create bindless pipeline layout\n");
    exit(EXIT_FAILURE);
  }
  /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: pipeline layout created\n");

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
    fprintf(stderr, "Failed to create bindless graphics pipeline\n");
    exit(EXIT_FAILURE);
  }

  vkDestroyShaderModule(cur_device(), vertShaderModule, NULL);
  vkDestroyShaderModule(cur_device(), fragShaderModule, NULL);
  /*DEBUG*/ if(getenv("CJELLY_DEBUG")) fprintf(stderr, "DEBUG: exit createBindlessGraphicsPipeline\n");
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
  viewport.x = 0.0f; viewport.y = 0.0f; viewport.width = (float)WIDTH; viewport.height = (float)HEIGHT; viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
  VkRect2D scissor = {0}; scissor.offset.x = 0; scissor.offset.y = 0; scissor.extent.width = WIDTH; scissor.extent.height = HEIGHT;
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
static void createTextureImageCtx(const CJellyVulkanContext* ctx, const char * filePath) {
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
  vkMapMemory(ctx->device, stagingBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, pixels, (size_t)bufferSize);
  vkUnmapMemory(ctx->device, stagingBufferMemory);
  free(pixels);

  // Create the Vulkan texture image.
  // We choose VK_FORMAT_R8G8B8A8_UNORM for the RGBA data.
  CJellyTexturedResources* tx = cur_tx();
  createImageCtx(ctx, texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &tx->image, &tx->imageMemory);

  // Transition image layout to prepare for the data copy.
  transitionImageLayoutCtx(ctx, tx->image, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  // Copy the pixel data from the staging buffer into the texture image.
  copyBufferToImageCtx(ctx, stagingBuffer, tx->image, texWidth, texHeight);

  // Transition the image layout for shader access.
  transitionImageLayoutCtx(ctx, tx->image, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(ctx->device, stagingBuffer, NULL);
  vkFreeMemory(ctx->device, stagingBufferMemory, NULL);
}

/// Creates an image view for the texture image.
static void createTextureImageViewCtx(const CJellyVulkanContext* ctx) {
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
    fprintf(stderr, "Failed to create texture image view\n");
    exit(EXIT_FAILURE);
  }
}

/// Creates a texture sampler.
static void createTextureSamplerCtx(const CJellyVulkanContext* ctx) {
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
    fprintf(stderr, "Failed to create texture sampler\n");
    exit(EXIT_FAILURE);
  }
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

void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer * buffer,
    VkDeviceMemory * bufferMemory) {
  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(cur_device(), &bufferInfo, NULL, buffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(cur_device(), *buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(cur_device(), &allocInfo, NULL, bufferMemory) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate buffer memory\n");
    exit(EXIT_FAILURE);
  }

  vkBindBufferMemory(cur_device(), *buffer, *bufferMemory, 0);
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

  if (vkCreateImage(cur_device(), &imageInfo, NULL, image) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create image\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(cur_device(), *image, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(cur_device(), &allocInfo, NULL, imageMemory) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate image memory\n");
    exit(EXIT_FAILURE);
  }

  vkBindImageMemory(cur_device(), *image, *imageMemory, 0);
}

VkCommandBuffer beginSingleTimeCommands(void) {
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

void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo = {0};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(cur_gfx_queue(), 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(cur_gfx_queue());

  vkFreeCommandBuffers(cur_device(), cur_cmd_pool(), 1, &commandBuffer);
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

/* legacy textured recorder removed */

/* moved to window.c: createBindlessCommandBuffersForWindowCtx */

/* moved to window.c */

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

  CJellyTexturedResources* txV = cur_tx();
  if (vkCreateBuffer(cur_device(), &bufferInfo, NULL, &txV->vertexBuffer) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create textured vertex buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(cur_device(), txV->vertexBuffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(cur_device(), &allocInfo, NULL, &txV->vertexBufferMemory) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate textured vertex buffer memory\n");
    exit(EXIT_FAILURE);
  }

  vkBindBufferMemory(
      cur_device(), txV->vertexBuffer, txV->vertexBufferMemory, 0);

  void * data;
  vkMapMemory(cur_device(), txV->vertexBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, verticesTextured, (size_t)bufferSize);
  vkUnmapMemory(cur_device(), txV->vertexBufferMemory);
}

/* Context-based textured command buffers for a window */
/* moved to window.c */

void createBindlessVertexBuffer(VkDevice device __attribute__((unused)), VkCommandPool commandPool __attribute__((unused))) {
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
  createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               &bl->vertexBuffer, &bl->vertexBufferMemory);

  void * data;
  vkMapMemory(cur_device(), bl->vertexBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, verticesBindless, (size_t)bufferSize);
  vkUnmapMemory(cur_device(), bl->vertexBufferMemory);
}

//
// === GLOBAL VULKAN INITIALIZATION & CLEANUP ===
//

/* legacy global init removed; engine owns bootstrap */

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
  // Transition to TRANSFER_DST for subsequent copies
  transitionImageLayout(atlas->atlasImage, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  
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
    fprintf(stderr, "Failed to create atlas image view\n");
    vkDestroyImage(cur_device(), atlas->atlasImage, NULL);
    vkFreeMemory(cur_device(), atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }
  
  // Create sampler (reuse textured resources' sampler)
  {
    CJellyTexturedResources* tx = cur_tx();
    atlas->atlasSampler = tx ? tx->sampler : VK_NULL_HANDLE;
  }
  
  // Create descriptor set layout (single combined sampler)
  VkDescriptorSetLayoutBinding layoutBinding = {0};
  layoutBinding.binding = 0;
  layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutBinding.descriptorCount = 1;
  layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutBinding.pImmutableSamplers = NULL;
  
  VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &layoutBinding;
  layoutInfo.pNext = NULL;
  
  if (vkCreateDescriptorSetLayout(cur_device(), &layoutInfo, NULL, &atlas->bindlessDescriptorSetLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create bindless descriptor set layout\n");
    vkDestroyImageView(cur_device(), atlas->atlasImageView, NULL);
    vkDestroyImage(cur_device(), atlas->atlasImage, NULL);
    vkFreeMemory(cur_device(), atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }
  
  // Create descriptor pool
  VkDescriptorPoolSize poolSize = {0};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = maxTextures;
  
  VkDescriptorPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;
  poolInfo.flags = 0;
  
  if (vkCreateDescriptorPool(cur_device(), &poolInfo, NULL, &atlas->bindlessDescriptorPool) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create bindless descriptor pool\n");
    vkDestroyDescriptorSetLayout(cur_device(), atlas->bindlessDescriptorSetLayout, NULL);
    vkDestroyImageView(cur_device(), atlas->atlasImageView, NULL);
    vkDestroyImage(cur_device(), atlas->atlasImage, NULL);
    vkFreeMemory(cur_device(), atlas->atlasImageMemory, NULL);
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
  allocInfo.pNext = NULL;
  
  if (vkAllocateDescriptorSets(cur_device(), &allocInfo, &atlas->bindlessDescriptorSet) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate bindless descriptor set\n");
    vkDestroyDescriptorPool(cur_device(), atlas->bindlessDescriptorPool, NULL);
    vkDestroyDescriptorSetLayout(cur_device(), atlas->bindlessDescriptorSetLayout, NULL);
    vkDestroyImageView(cur_device(), atlas->atlasImageView, NULL);
    vkDestroyImage(cur_device(), atlas->atlasImage, NULL);
    vkFreeMemory(cur_device(), atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }
  
  return atlas;
}

// Context-based atlas creation (uses context device instead of global)
CJellyTextureAtlas * cjelly_create_texture_atlas_ctx(const CJellyVulkanContext* ctx, uint32_t width, uint32_t height) {
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

  // Create the atlas image using context device
  createImageCtx(ctx, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                  VK_IMAGE_TILING_OPTIMAL,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  &atlas->atlasImage, &atlas->atlasImageMemory);
  // Transition to TRANSFER_DST for subsequent copies
  transitionImageLayoutCtx(ctx, atlas->atlasImage, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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
    fprintf(stderr, "Failed to create atlas image view (ctx)\n");
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
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
    fprintf(stderr, "Failed to create atlas sampler (ctx)\n");
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }

  // Create descriptor set layout using context device
  VkDescriptorSetLayoutBinding layoutBinding = {0};
  layoutBinding.binding = 0;
  layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutBinding.descriptorCount = 1;
  layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutBinding.pImmutableSamplers = NULL;

  VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &layoutBinding;
  layoutInfo.pNext = NULL;

  if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &atlas->bindlessDescriptorSetLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create bindless descriptor set layout (ctx)\n");
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }

  // Create descriptor pool using context device
  VkDescriptorPoolSize poolSize = {0};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = maxTextures;

  VkDescriptorPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;
  poolInfo.flags = 0;

  if (vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &atlas->bindlessDescriptorPool) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create bindless descriptor pool (ctx)\n");
    vkDestroyDescriptorSetLayout(ctx->device, atlas->bindlessDescriptorSetLayout, NULL);
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
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
    fprintf(stderr, "Failed to allocate bindless descriptor set (ctx)\n");
    vkDestroyDescriptorPool(ctx->device, atlas->bindlessDescriptorPool, NULL);
    vkDestroyDescriptorSetLayout(ctx->device, atlas->bindlessDescriptorSetLayout, NULL);
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(textureEntries);
    free(atlas);
    return NULL;
  }

  return atlas;
}

// Context-based atlas destruction (uses context device instead of global)
void cjelly_destroy_texture_atlas_ctx(CJellyTextureAtlas * atlas, const CJellyVulkanContext* ctx) {
  if (!atlas) return;

  vkDestroySampler(ctx->device, atlas->atlasSampler, NULL);
  vkDestroyDescriptorSetLayout(ctx->device, atlas->bindlessDescriptorSetLayout, NULL);
  vkDestroyDescriptorPool(ctx->device, atlas->bindlessDescriptorPool, NULL);
  vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
  vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
  vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);

  if (textureEntries) {
    free(textureEntries);
    textureEntries = NULL;
  }

  free(atlas);
}

void cjelly_destroy_texture_atlas(CJellyTextureAtlas * atlas) {
  if (!atlas) return;

  vkDestroyDescriptorSetLayout(cur_device(), atlas->bindlessDescriptorSetLayout, NULL);
  vkDestroyDescriptorPool(cur_device(), atlas->bindlessDescriptorPool, NULL);
  vkDestroyImageView(cur_device(), atlas->atlasImageView, NULL);
  vkDestroyImage(cur_device(), atlas->atlasImage, NULL);
  vkFreeMemory(cur_device(), atlas->atlasImageMemory, NULL);

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
  vkMapMemory(cur_device(), stagingBufferMemory, 0, imageSize, 0, &data);
  
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
  vkDestroyBuffer(cur_device(), stagingBuffer, NULL);
  vkFreeMemory(cur_device(), stagingBufferMemory, NULL);
  cjelly_format_image_free(image);
  
  return textureID;
}

// Context-based texture addition (uses context device instead of global)
uint32_t cjelly_atlas_add_texture_ctx(CJellyTextureAtlas * atlas, const char * filePath, const CJellyVulkanContext* ctx) {
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

  // Create staging buffer for the texture using context device
  VkDeviceSize imageSize = texWidth * texHeight * 4; // RGBA
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               &stagingBuffer, &stagingBufferMemory);

  // Copy image data to staging buffer using context device
  void * data;
  vkMapMemory(ctx->device, stagingBufferMemory, 0, imageSize, 0, &data);

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
  vkDestroyBuffer(ctx->device, stagingBuffer, NULL);
  vkFreeMemory(ctx->device, stagingBufferMemory, NULL);
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

/* Public wrapper used by window API to build textured path using a context */
void cjelly_init_textured_pipeline_ctx(const CJellyVulkanContext* ctx) {
  if (!ctx || ctx->device == VK_NULL_HANDLE) return;
  // Avoid recreating global textured resources multiple times (multi-window init)
  CJellyTexturedResources* tx = cur_tx();
  if (tx && (tx->pipeline != VK_NULL_HANDLE || tx->image != VK_NULL_HANDLE)) return;
  createTextureImageCtx(ctx, "test/images/bmp/tang.bmp");
  createTexturedVertexBuffer();
  createTextureImageViewCtx(ctx);
  createTextureSamplerCtx(ctx);
  createDescriptorSetLayoutsCtx(ctx);
  createTextureDescriptorPoolCtx(ctx);
  allocateTextureDescriptorSetCtx(ctx);
  updateTextureDescriptorSetCtx(ctx, tx ? tx->descriptorSet : VK_NULL_HANDLE);
  createTexturedGraphicsPipelineCtx(ctx);
}
