#pragma once
#include <vulkan/vulkan.h>

/* Forward declaration for atlas used by bindless resources */
typedef struct CJellyTextureAtlas CJellyTextureAtlas;

/* Internal-only layout for bindless resources (opaque publicly) */
typedef struct CJellyBindlessResources {
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  CJellyTextureAtlas* textureAtlas;
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
  float uv[4];
  float colorMul[4];
} CJellyBindlessResources;


