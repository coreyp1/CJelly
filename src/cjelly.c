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
#ifndef _WIN32
#include <dlfcn.h>  /* For dlsym to optionally load XInput2 functions */
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)0)
#endif
#endif

#include <cjelly/cjelly.h>
#include <cjelly/runtime.h>
#include <cjelly/application.h>
#include <cjelly/cj_window.h>
#include <cjelly/window_internal.h>
#include <cjelly/engine_internal.h>
#include <cjelly/cj_input.h>
#include <cjelly/bindless_internal.h>
#include <cjelly/textured_internal.h>
#include <cjelly/bindless_state_internal.h>
#include <cjelly/basic_state_internal.h>
#include <cjelly/format/image.h>
#include <cjelly/macros.h>
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#else
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XI.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XI2proto.h>
#endif
#include <shaders/basic.vert.h>
#include <shaders/color.vert.h>
#include <shaders/color.frag.h>
#include <shaders/textured.frag.h>
#include <shaders/bindless.vert.h>
#include <shaders/bindless.frag.h>

// Global Vulkan objects shared among all windows.
#ifndef _WIN32
Display * display; /* provided by main.c */
static int xinput2_available = -1; /* -1 = not checked, 0 = unavailable, 1 = available */
static int xinput2_major = 2;
static int xinput2_minor = 0;
static int xinput2_opcode = -1; /* XInput extension opcode */
static void* xinput2_lib_handle = NULL; /* Handle to libXi.so for dlopen */
#endif


// Global flag to enable validation layers.
int enableValidationLayers;


/* Helpers to read from current engine */
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


// Forward declarations for helper functions still in use:
void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer * buffer,
    VkDeviceMemory * bufferMemory);
void transitionImageLayout(VkImage image, VkFormat format,
    VkImageLayout oldLayout, VkImageLayout newLayout);
void createBindlessVertexBuffer(VkDevice device, VkCommandPool commandPool);
void createBindlessGraphicsPipeline(VkDevice device, VkRenderPass renderPass);
VkShaderModule createShaderModuleFromMemory(VkDevice device, const unsigned char * code, size_t codeSize);

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

/* Public wrappers for runtime.h */
/* forward declare OS function to avoid implicit warning */
CJ_API void processWindowEvents(void);
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

  VertexBindless vertices[] = {
    // Single quad matching textured size: [-0.5,0.5]
    {{-0.5f, -0.5f}, {r, g, b}, 0},
    {{ 0.5f, -0.5f}, {r, g, b}, 0},
    {{ 0.5f,  0.5f}, {r, g, b}, 0},
    {{ 0.5f,  0.5f}, {r, g, b}, 0},
    {{-0.5f,  0.5f}, {r, g, b}, 0},
    {{-0.5f, -0.5f}, {r, g, b}, 0},
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
// === EVENT PROCESSING (PLATFORM-SPECIFIC) ===
//

#ifdef _WIN32

CJ_API void processWindowEvents() {
  MSG msg;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    // Handle WM_QUIT specially - this is posted when the last window closes
    // We should not dispatch it, but rather let the application handle it
    // Since we used PM_REMOVE, the message is already removed from the queue
    if (msg.message == WM_QUIT) {
      // WM_QUIT indicates the application should exit
      // Don't dispatch it, just break out of the loop
      // The application's main loop should check for this condition
      // Note: The message has already been removed from the queue by PeekMessage
      break;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

#else

/* Initialize XInput2 if available. Returns true if XInput2 is available and initialized.
 * Note: XInput2 is kept available for future touch support, but scroll events
 * are handled via traditional X11 ButtonPress events (Button4/Button5).
 */
static bool init_xinput2(void) {
  if (xinput2_available != -1) {
    return xinput2_available == 1;
  }

  xinput2_available = 0; /* Assume unavailable until proven otherwise */

  if (!display) return false;

  int event, error;
  if (!XQueryExtension(display, "XInputExtension", &xinput2_opcode, &event, &error)) {
    /* XInput extension not available */
    return false;
  }

  /* Query XInput2 version - try to load libXi dynamically */
  typedef int (*XIQueryVersionFunc)(Display*, int*, int*);
  XIQueryVersionFunc xi_query_version = NULL;

  /* Try to open libXi if not already opened */
  if (!xinput2_lib_handle) {
    /* Try common library names */
    const char* lib_names[] = {
      "libXi.so.6",
      "libXi.so",
      "libXi.so.6.1.0",
      NULL
    };

    for (int i = 0; lib_names[i] != NULL; i++) {
      xinput2_lib_handle = dlopen(lib_names[i], RTLD_LAZY | RTLD_LOCAL);
      if (xinput2_lib_handle) {
        break;
      }
    }
  }

  if (xinput2_lib_handle) {
    void* sym = dlsym(xinput2_lib_handle, "XIQueryVersion");
    if (sym) {
      /* Use union to avoid pedantic warning about function pointer conversion */
      union { void* p; XIQueryVersionFunc f; } u = {.p = sym};
      xi_query_version = u.f;
    }
  }

  if (!xi_query_version) {
    /* libXi not available - skip XInput2 */
    return false;
  }

  int major = 2, minor = 0;
  Status result = xi_query_version(display, &major, &minor);
  if (result == BadRequest || result != Success) {
    /* XInput2 not available */
    return false;
  }

  xinput2_major = major;
  xinput2_minor = minor;

  xinput2_available = 1;
  return true;
}

/* Select XInput2 events for a window. Returns true if XInput2 events were selected. */
bool select_xinput2_events(Window window) {
  if (!init_xinput2() || !display) {
    return false;
  }

  /* Define XIEventMask structure locally since header may not expose it properly */
  typedef struct {
    int deviceid;
    int mask_len;
    unsigned char* mask;
  } LocalXIEventMask;

  /* Load XInput2 function dynamically */
  typedef Status (*XISelectEventsFunc)(Display*, Window, void*, int);
  XISelectEventsFunc xi_select_events = NULL;

  if (xinput2_lib_handle) {
    void* sym = dlsym(xinput2_lib_handle, "XISelectEvents");
    if (sym) {
      union { void* p; XISelectEventsFunc f; } u = {.p = sym};
      xi_select_events = u.f;
    }
  }

  if (!xi_select_events) {
    /* Function not available */
    return false;
  }

  LocalXIEventMask event_mask;
  unsigned char mask[32] = {0}; /* Allocate enough space for XI_LASTEVENT */

  event_mask.deviceid = XIAllMasterDevices;
  event_mask.mask_len = sizeof(mask);
  event_mask.mask = mask;

  /* Select touch events for future touch support (XI_TouchBegin, XI_TouchUpdate, XI_TouchEnd) */
  /* Note: Scroll events are handled via traditional X11 ButtonPress (Button4/Button5) */
  /* We don't select XI_ButtonPress/XI_ButtonRelease here to avoid consuming scroll events */

  Status result = xi_select_events(display, window, &event_mask, 1);
  if (result != Success) {
    return false;
  }

  XFlush(display);
  return true;
}

CJ_API void processWindowEvents() {
  /* Initialize XInput2 on first call */
  if (xinput2_available == -1) {
    init_xinput2();
  }

  while (XPending(display)) {
    XEvent event;
    XNextEvent(display, &event);

    /* Check for XInput2 events (reserved for future touch support) */
    if (xinput2_available == 1 && event.type == GenericEvent) {
      XGenericEventCookie* cookie = &event.xcookie;
      if (XGetEventData(display, cookie)) {
        if (cookie->extension == xinput2_opcode) {
          /* TODO: Handle XI_TouchBegin, XI_TouchUpdate, XI_TouchEnd for touch support */
          /* For now, just free the event data and let traditional X11 handle everything */
          XFreeEventData(display, cookie);
        } else {
          XFreeEventData(display, cookie);
        }
      }
    }

    if (event.type == ClientMessage) {
      Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
      if ((Atom)event.xclient.data.l[0] == wmDelete) {
        // Look up window from handle via application
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xclient.window);
          if (window) {
            bool cancellable = true;  // User-initiated close
            cj_window_close_with_callback(window, cancellable);
          }
        }
      }
    }
    if (event.type == DestroyNotify) {
      // Window already destroyed, nothing to do
    }
    if (event.type == MapNotify) {
      // Window mapped (restored from minimized)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xmap.window);
        if (window) {
          cj_window__set_minimized(window, false);
          /* Mark window dirty when restored from minimized (EXPOSE reason bypasses FPS limit) */
          cj_window_mark_dirty_with_reason(window, CJ_RENDER_REASON_EXPOSE);
        }
      }
    }
    if (event.type == UnmapNotify) {
      // Window unmapped (minimized)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xunmap.window);
        if (window) {
          cj_window__set_minimized(window, true);
        }
      }
    }
    if (event.type == ConfigureNotify) {
      // Window resized or moved
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xconfigure.window);
        if (window) {
          uint32_t new_width = (uint32_t)event.xconfigure.width;
          uint32_t new_height = (uint32_t)event.xconfigure.height;

          // Get current size to check if it changed
          uint32_t current_width = 0, current_height = 0;
          cj_window_get_size(window, &current_width, &current_height);

          // Only dispatch if size actually changed
          if (new_width != current_width || new_height != current_height) {
            // Update size and mark swapchain for recreation (deferred until next frame to avoid blocking)
            cj_window__update_size_and_mark_recreate(window, new_width, new_height);

            // Dispatch resize callback (user can do additional work)
            cj_window__dispatch_resize_callback(window, new_width, new_height);
          }
        }
      }
    }
    if (event.type == KeyPress) {
      // Handle keyboard input
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xkey.window);
          if (window) {
          KeySym sym = XLookupKeysym(&event.xkey, 0);

          // Map X11 keysym to cj_keycode_t
          cj_keycode_t keycode = CJ_KEY_UNKNOWN;
          if (sym >= XK_a && sym <= XK_z) keycode = (cj_keycode_t)(CJ_KEY_A + (sym - XK_a));
          else if (sym >= XK_A && sym <= XK_Z) keycode = (cj_keycode_t)(CJ_KEY_A + (sym - XK_A));
          else if (sym >= XK_0 && sym <= XK_9) keycode = (cj_keycode_t)(CJ_KEY_0 + (sym - XK_0));
          else {
            switch (sym) {
              case XK_F1: keycode = CJ_KEY_F1; break;
              case XK_F2: keycode = CJ_KEY_F2; break;
              case XK_F3: keycode = CJ_KEY_F3; break;
              case XK_F4: keycode = CJ_KEY_F4; break;
              case XK_F5: keycode = CJ_KEY_F5; break;
              case XK_F6: keycode = CJ_KEY_F6; break;
              case XK_F7: keycode = CJ_KEY_F7; break;
              case XK_F8: keycode = CJ_KEY_F8; break;
              case XK_F9: keycode = CJ_KEY_F9; break;
              case XK_F10: keycode = CJ_KEY_F10; break;
              case XK_F11: keycode = CJ_KEY_F11; break;
              case XK_F12: keycode = CJ_KEY_F12; break;
              case XK_Up: keycode = CJ_KEY_UP; break;
              case XK_Down: keycode = CJ_KEY_DOWN; break;
              case XK_Left: keycode = CJ_KEY_LEFT; break;
              case XK_Right: keycode = CJ_KEY_RIGHT; break;
              case XK_Home: keycode = CJ_KEY_HOME; break;
              case XK_End: keycode = CJ_KEY_END; break;
              case XK_Page_Up: keycode = CJ_KEY_PAGE_UP; break;
              case XK_Page_Down: keycode = CJ_KEY_PAGE_DOWN; break;
              case XK_BackSpace: keycode = CJ_KEY_BACKSPACE; break;
              case XK_Delete: keycode = CJ_KEY_DELETE; break;
              case XK_Insert: keycode = CJ_KEY_INSERT; break;
              case XK_Return: keycode = CJ_KEY_ENTER; break;
              case XK_Tab: keycode = CJ_KEY_TAB; break;
              case XK_Escape: keycode = CJ_KEY_ESCAPE; break;
              case XK_Shift_L: keycode = CJ_KEY_LEFT_SHIFT; break;
              case XK_Shift_R: keycode = CJ_KEY_RIGHT_SHIFT; break;
              case XK_Control_L: keycode = CJ_KEY_LEFT_CTRL; break;
              case XK_Control_R: keycode = CJ_KEY_RIGHT_CTRL; break;
              case XK_Alt_L: keycode = CJ_KEY_LEFT_ALT; break;
              case XK_Alt_R: keycode = CJ_KEY_RIGHT_ALT; break;
              case XK_Super_L: keycode = CJ_KEY_LEFT_META; break;
              case XK_Super_R: keycode = CJ_KEY_RIGHT_META; break;
              case XK_space: keycode = CJ_KEY_SPACE; break;
              case XK_minus: keycode = CJ_KEY_MINUS; break;
              case XK_equal: keycode = CJ_KEY_EQUALS; break;
              case XK_bracketleft: keycode = CJ_KEY_BRACKET_LEFT; break;
              case XK_bracketright: keycode = CJ_KEY_BRACKET_RIGHT; break;
              case XK_backslash: keycode = CJ_KEY_BACKSLASH; break;
              case XK_semicolon: keycode = CJ_KEY_SEMICOLON; break;
              case XK_apostrophe: keycode = CJ_KEY_APOSTROPHE; break;
              case XK_grave: keycode = CJ_KEY_GRAVE; break;
              case XK_comma: keycode = CJ_KEY_COMMA; break;
              case XK_period: keycode = CJ_KEY_PERIOD; break;
              case XK_slash: keycode = CJ_KEY_SLASH; break;
              case XK_KP_0: keycode = CJ_KEY_NUMPAD_0; break;
              case XK_KP_1: keycode = CJ_KEY_NUMPAD_1; break;
              case XK_KP_2: keycode = CJ_KEY_NUMPAD_2; break;
              case XK_KP_3: keycode = CJ_KEY_NUMPAD_3; break;
              case XK_KP_4: keycode = CJ_KEY_NUMPAD_4; break;
              case XK_KP_5: keycode = CJ_KEY_NUMPAD_5; break;
              case XK_KP_6: keycode = CJ_KEY_NUMPAD_6; break;
              case XK_KP_7: keycode = CJ_KEY_NUMPAD_7; break;
              case XK_KP_8: keycode = CJ_KEY_NUMPAD_8; break;
              case XK_KP_9: keycode = CJ_KEY_NUMPAD_9; break;
              case XK_KP_Add: keycode = CJ_KEY_NUMPAD_ADD; break;
              case XK_KP_Subtract: keycode = CJ_KEY_NUMPAD_SUBTRACT; break;
              case XK_KP_Multiply: keycode = CJ_KEY_NUMPAD_MULTIPLY; break;
              case XK_KP_Divide: keycode = CJ_KEY_NUMPAD_DIVIDE; break;
              case XK_KP_Decimal: keycode = CJ_KEY_NUMPAD_DECIMAL; break;
              case XK_KP_Enter: keycode = CJ_KEY_NUMPAD_ENTER; break;
              case XK_Caps_Lock: keycode = CJ_KEY_CAPS_LOCK; break;
              case XK_Num_Lock: keycode = CJ_KEY_NUM_LOCK; break;
              case XK_Scroll_Lock: keycode = CJ_KEY_SCROLL_LOCK; break;
              case XK_Print: keycode = CJ_KEY_PRINT_SCREEN; break;
              case XK_Pause: keycode = CJ_KEY_PAUSE; break;
              default: keycode = CJ_KEY_UNKNOWN; break;
            }
          }

          // Get modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xkey.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xkey.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xkey.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xkey.state & Mod4Mask) modifiers |= CJ_MOD_META;
          // Lock keys: LockMask = Caps Lock, Mod2Mask = Num Lock (typical, may vary)
          if (event.xkey.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xkey.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          // Check for auto-repeat: if key is already marked as pressed, this is a repeat
          bool is_repeat = cj_window__is_key_pressed(window, keycode);
          // Mark key as pressed
          cj_window__set_key_pressed(window, keycode, true);

          // Dispatch keyboard callback
          cj_window__dispatch_key_callback(window, keycode, (cj_scancode_t)event.xkey.keycode,
                                           CJ_KEY_ACTION_DOWN, modifiers, is_repeat);
        }
      }
    }
    if (event.type == KeyRelease) {
      // X11 auto-repeat detection: When a key is held, X11 generates KeyRelease immediately
      // followed by KeyPress. We detect this by peeking at the next event. If it's a KeyPress
      // for the same key with the same or very close timestamp, skip this KeyRelease (it's fake).
      if (XPending(display) > 0) {
        XEvent next_event;
        XPeekEvent(display, &next_event);
        if (next_event.type == KeyPress &&
            next_event.xkey.keycode == event.xkey.keycode &&
            next_event.xkey.time == event.xkey.time) {
          // This is a fake KeyRelease from auto-repeat - skip it
          // The next KeyPress will be handled and marked as repeat
          continue;
        }
      }
      // Handle key release
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xkey.window);
        if (window) {
          KeySym sym = XLookupKeysym(&event.xkey, 0);

          // Map X11 keysym to cj_keycode_t (same mapping as KeyPress)
          cj_keycode_t keycode = CJ_KEY_UNKNOWN;
          if (sym >= XK_a && sym <= XK_z) keycode = (cj_keycode_t)(CJ_KEY_A + (sym - XK_a));
          else if (sym >= XK_A && sym <= XK_Z) keycode = (cj_keycode_t)(CJ_KEY_A + (sym - XK_A));
          else if (sym >= XK_0 && sym <= XK_9) keycode = (cj_keycode_t)(CJ_KEY_0 + (sym - XK_0));
          else {
            switch (sym) {
              case XK_F1: keycode = CJ_KEY_F1; break;
              case XK_F2: keycode = CJ_KEY_F2; break;
              case XK_F3: keycode = CJ_KEY_F3; break;
              case XK_F4: keycode = CJ_KEY_F4; break;
              case XK_F5: keycode = CJ_KEY_F5; break;
              case XK_F6: keycode = CJ_KEY_F6; break;
              case XK_F7: keycode = CJ_KEY_F7; break;
              case XK_F8: keycode = CJ_KEY_F8; break;
              case XK_F9: keycode = CJ_KEY_F9; break;
              case XK_F10: keycode = CJ_KEY_F10; break;
              case XK_F11: keycode = CJ_KEY_F11; break;
              case XK_F12: keycode = CJ_KEY_F12; break;
              case XK_Up: keycode = CJ_KEY_UP; break;
              case XK_Down: keycode = CJ_KEY_DOWN; break;
              case XK_Left: keycode = CJ_KEY_LEFT; break;
              case XK_Right: keycode = CJ_KEY_RIGHT; break;
              case XK_Home: keycode = CJ_KEY_HOME; break;
              case XK_End: keycode = CJ_KEY_END; break;
              case XK_Page_Up: keycode = CJ_KEY_PAGE_UP; break;
              case XK_Page_Down: keycode = CJ_KEY_PAGE_DOWN; break;
              case XK_BackSpace: keycode = CJ_KEY_BACKSPACE; break;
              case XK_Delete: keycode = CJ_KEY_DELETE; break;
              case XK_Insert: keycode = CJ_KEY_INSERT; break;
              case XK_Return: keycode = CJ_KEY_ENTER; break;
              case XK_Tab: keycode = CJ_KEY_TAB; break;
              case XK_Escape: keycode = CJ_KEY_ESCAPE; break;
              case XK_Shift_L: keycode = CJ_KEY_LEFT_SHIFT; break;
              case XK_Shift_R: keycode = CJ_KEY_RIGHT_SHIFT; break;
              case XK_Control_L: keycode = CJ_KEY_LEFT_CTRL; break;
              case XK_Control_R: keycode = CJ_KEY_RIGHT_CTRL; break;
              case XK_Alt_L: keycode = CJ_KEY_LEFT_ALT; break;
              case XK_Alt_R: keycode = CJ_KEY_RIGHT_ALT; break;
              case XK_Super_L: keycode = CJ_KEY_LEFT_META; break;
              case XK_Super_R: keycode = CJ_KEY_RIGHT_META; break;
              case XK_space: keycode = CJ_KEY_SPACE; break;
              case XK_minus: keycode = CJ_KEY_MINUS; break;
              case XK_equal: keycode = CJ_KEY_EQUALS; break;
              case XK_bracketleft: keycode = CJ_KEY_BRACKET_LEFT; break;
              case XK_bracketright: keycode = CJ_KEY_BRACKET_RIGHT; break;
              case XK_backslash: keycode = CJ_KEY_BACKSLASH; break;
              case XK_semicolon: keycode = CJ_KEY_SEMICOLON; break;
              case XK_apostrophe: keycode = CJ_KEY_APOSTROPHE; break;
              case XK_grave: keycode = CJ_KEY_GRAVE; break;
              case XK_comma: keycode = CJ_KEY_COMMA; break;
              case XK_period: keycode = CJ_KEY_PERIOD; break;
              case XK_slash: keycode = CJ_KEY_SLASH; break;
              case XK_KP_0: keycode = CJ_KEY_NUMPAD_0; break;
              case XK_KP_1: keycode = CJ_KEY_NUMPAD_1; break;
              case XK_KP_2: keycode = CJ_KEY_NUMPAD_2; break;
              case XK_KP_3: keycode = CJ_KEY_NUMPAD_3; break;
              case XK_KP_4: keycode = CJ_KEY_NUMPAD_4; break;
              case XK_KP_5: keycode = CJ_KEY_NUMPAD_5; break;
              case XK_KP_6: keycode = CJ_KEY_NUMPAD_6; break;
              case XK_KP_7: keycode = CJ_KEY_NUMPAD_7; break;
              case XK_KP_8: keycode = CJ_KEY_NUMPAD_8; break;
              case XK_KP_9: keycode = CJ_KEY_NUMPAD_9; break;
              case XK_KP_Add: keycode = CJ_KEY_NUMPAD_ADD; break;
              case XK_KP_Subtract: keycode = CJ_KEY_NUMPAD_SUBTRACT; break;
              case XK_KP_Multiply: keycode = CJ_KEY_NUMPAD_MULTIPLY; break;
              case XK_KP_Divide: keycode = CJ_KEY_NUMPAD_DIVIDE; break;
              case XK_KP_Decimal: keycode = CJ_KEY_NUMPAD_DECIMAL; break;
              case XK_KP_Enter: keycode = CJ_KEY_NUMPAD_ENTER; break;
              case XK_Caps_Lock: keycode = CJ_KEY_CAPS_LOCK; break;
              case XK_Num_Lock: keycode = CJ_KEY_NUM_LOCK; break;
              case XK_Scroll_Lock: keycode = CJ_KEY_SCROLL_LOCK; break;
              case XK_Print: keycode = CJ_KEY_PRINT_SCREEN; break;
              case XK_Pause: keycode = CJ_KEY_PAUSE; break;
              default: keycode = CJ_KEY_UNKNOWN; break;
            }
          }

          // Get modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xkey.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xkey.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xkey.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xkey.state & Mod4Mask) modifiers |= CJ_MOD_META;
          // Lock keys: LockMask = Caps Lock, Mod2Mask = Num Lock (typical, may vary)
          if (event.xkey.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xkey.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          // Clear key state (mark as not pressed)
          cj_window__set_key_pressed(window, keycode, false);

          // Dispatch keyboard callback
          cj_window__dispatch_key_callback(window, keycode, (cj_scancode_t)event.xkey.keycode,
                                           CJ_KEY_ACTION_UP, modifiers, false);
        }
      }
    }
    if (event.type == FocusIn) {
      // Window gained focus
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xfocus.window);
        if (window) {
          cj_window__dispatch_focus_callback(window, CJ_FOCUS_GAINED);
        }
      }
    }
    if (event.type == FocusOut) {
      // Window lost focus
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xfocus.window);
        if (window) {
          cj_window__dispatch_focus_callback(window, CJ_FOCUS_LOST);
        }
      }
    }
    if (event.type == MotionNotify) {
      // Mouse moved
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xmotion.window);
        if (window) {
          int32_t x = (int32_t)event.xmotion.x;
          int32_t y = (int32_t)event.xmotion.y;
          int32_t old_x = 0, old_y = 0;
          cj_window__get_mouse_position(window, &old_x, &old_y);
          int32_t dx = x - old_x;
          int32_t dy = y - old_y;

          // Extract modifiers from event state
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xmotion.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xmotion.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xmotion.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xmotion.state & Mod4Mask) modifiers |= CJ_MOD_META;
          if (event.xmotion.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xmotion.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          cj_mouse_event_t mouse_event = {0};
          mouse_event.type = CJ_MOUSE_MOVE;
          mouse_event.x = x;
          mouse_event.y = y;
          mouse_event.dx = dx;
          mouse_event.dy = dy;
          mouse_event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &mouse_event);
        }
      }
    }
    if (event.type == ButtonPress) {
      // Check for scroll buttons first (Button4 = scroll up, Button5 = scroll down)
      if (event.xbutton.button == Button4 || event.xbutton.button == Button5) {
        // Mouse wheel scroll via traditional X11 button events
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xbutton.window);
          if (window) {
            int32_t x = (int32_t)event.xbutton.x;
            int32_t y = (int32_t)event.xbutton.y;
            float scroll_delta = (event.xbutton.button == Button4) ? 1.0f : -1.0f;

            // Extract modifiers
            cj_modifiers_t modifiers = CJ_MOD_NONE;
            if (event.xbutton.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
            if (event.xbutton.state & ControlMask) modifiers |= CJ_MOD_CTRL;
            if (event.xbutton.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
            if (event.xbutton.state & Mod4Mask) modifiers |= CJ_MOD_META;
            if (event.xbutton.state & LockMask) modifiers |= CJ_MOD_CAPS;
            if (event.xbutton.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

            cj_mouse_event_t mouse_event = {0};
            mouse_event.type = CJ_MOUSE_SCROLL;
            mouse_event.x = x;
            mouse_event.y = y;
            mouse_event.scroll_y = scroll_delta;
            mouse_event.modifiers = modifiers;
            cj_window__dispatch_mouse_callback(window, &mouse_event);
          }
        }
        // Important: continue to next event after handling scroll, don't process as regular button
        continue;
      } else {
        // Regular mouse button pressed
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xbutton.window);
          if (window) {
            cj_mouse_button_t button = CJ_MOUSE_BUTTON_LEFT;
            if (event.xbutton.button == Button2) button = CJ_MOUSE_BUTTON_MIDDLE;
            else if (event.xbutton.button == Button3) button = CJ_MOUSE_BUTTON_RIGHT;
            else if (event.xbutton.button == 8) button = CJ_MOUSE_BUTTON_4;
            else if (event.xbutton.button == 9) button = CJ_MOUSE_BUTTON_5;

            int32_t x = (int32_t)event.xbutton.x;
            int32_t y = (int32_t)event.xbutton.y;

            // Extract modifiers
            cj_modifiers_t modifiers = CJ_MOD_NONE;
            if (event.xbutton.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
            if (event.xbutton.state & ControlMask) modifiers |= CJ_MOD_CTRL;
            if (event.xbutton.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
            if (event.xbutton.state & Mod4Mask) modifiers |= CJ_MOD_META;
            if (event.xbutton.state & LockMask) modifiers |= CJ_MOD_CAPS;
            if (event.xbutton.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

            cj_mouse_event_t mouse_event = {0};
            mouse_event.type = CJ_MOUSE_BUTTON_DOWN;
            mouse_event.x = x;
            mouse_event.y = y;
            mouse_event.button = button;
            mouse_event.modifiers = modifiers;
            cj_window__dispatch_mouse_callback(window, &mouse_event);
          }
        }
      }
    }
    if (event.type == ButtonRelease) {
      // Skip ButtonRelease for scroll buttons (Button4/Button5) - scroll events only use ButtonPress
      if (event.xbutton.button == Button4 || event.xbutton.button == Button5) {
        // Scroll buttons don't generate BUTTON_UP events, only SCROLL events via ButtonPress
        continue;
      }
      // Mouse button released
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xbutton.window);
        if (window) {
          cj_mouse_button_t button = CJ_MOUSE_BUTTON_LEFT;
          if (event.xbutton.button == Button2) button = CJ_MOUSE_BUTTON_MIDDLE;
          else if (event.xbutton.button == Button3) button = CJ_MOUSE_BUTTON_RIGHT;
          else if (event.xbutton.button == 8) button = CJ_MOUSE_BUTTON_4;
          else if (event.xbutton.button == 9) button = CJ_MOUSE_BUTTON_5;

          int32_t x = (int32_t)event.xbutton.x;
          int32_t y = (int32_t)event.xbutton.y;

          // Extract modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xbutton.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xbutton.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xbutton.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xbutton.state & Mod4Mask) modifiers |= CJ_MOD_META;
          if (event.xbutton.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xbutton.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          cj_mouse_event_t mouse_event = {0};
          mouse_event.type = CJ_MOUSE_BUTTON_UP;
          mouse_event.x = x;
          mouse_event.y = y;
          mouse_event.button = button;
          mouse_event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &mouse_event);
        }
      }
    }
    if (event.type == EnterNotify) {
      // Mouse entered window
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xcrossing.window);
        if (window) {
          int32_t x = (int32_t)event.xcrossing.x;
          int32_t y = (int32_t)event.xcrossing.y;

          // Extract modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xcrossing.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xcrossing.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xcrossing.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xcrossing.state & Mod4Mask) modifiers |= CJ_MOD_META;
          if (event.xcrossing.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xcrossing.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          cj_mouse_event_t mouse_event = {0};
          mouse_event.type = CJ_MOUSE_ENTER;
          mouse_event.x = x;
          mouse_event.y = y;
          mouse_event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &mouse_event);
        }
      }
    }
    if (event.type == LeaveNotify) {
      // Mouse left window
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xcrossing.window);
        if (window) {
          int32_t x = (int32_t)event.xcrossing.x;
          int32_t y = (int32_t)event.xcrossing.y;

          // Extract modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xcrossing.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xcrossing.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xcrossing.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xcrossing.state & Mod4Mask) modifiers |= CJ_MOD_META;
          if (event.xcrossing.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xcrossing.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          cj_mouse_event_t mouse_event = {0};
          mouse_event.type = CJ_MOUSE_LEAVE;
          mouse_event.x = x;
          mouse_event.y = y;
          mouse_event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &mouse_event);
        }
      }
    }
  }
}

#endif






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

/* engine owns bootstrap */

//
// === BINDLESS TEXTURE ATLAS MANAGEMENT ===
//


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

  // Allocate memory for texture entries (per-atlas)
  atlas->maxTextures = 1024;
  atlas->entries = malloc(sizeof(CJellyTextureEntry) * atlas->maxTextures);
  if (!atlas->entries) {
    fprintf(stderr, "Failed to allocate memory for texture entries\n");
    free(atlas);
    return NULL;
  }
  memset(atlas->entries, 0, sizeof(CJellyTextureEntry) * atlas->maxTextures);

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
    fprintf(stderr, "Failed to allocate bindless descriptor set\n");
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

  // Allocate memory for texture entries (per-atlas)
  atlas->maxTextures = 1024;
  atlas->entries = malloc(sizeof(CJellyTextureEntry) * atlas->maxTextures);
  if (!atlas->entries) {
    fprintf(stderr, "Failed to allocate memory for texture entries\n");
    free(atlas);
    return NULL;
  }
  memset(atlas->entries, 0, sizeof(CJellyTextureEntry) * atlas->maxTextures);

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
    fprintf(stderr, "Failed to create atlas sampler (ctx)\n");
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
    fprintf(stderr, "Failed to get engine bindless descriptor set layout (ctx)\n");
    vkDestroyImageView(ctx->device, atlas->atlasImageView, NULL);
    vkDestroyImage(ctx->device, atlas->atlasImage, NULL);
    vkFreeMemory(ctx->device, atlas->atlasImageMemory, NULL);
    free(atlas->entries);
    free(atlas);
    return NULL;
  }
  atlas->bindlessDescriptorPool = cj_engine_bindless_pool(cur_eng());
  if (atlas->bindlessDescriptorPool == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to get engine bindless descriptor pool (ctx)\n");
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
    fprintf(stderr, "Failed to allocate bindless descriptor set (ctx)\n");
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
