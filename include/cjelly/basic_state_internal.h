#pragma once
#include <vulkan/vulkan.h>

/* Internal-only basic pipeline state (migration) */
typedef struct CJellyBasicState {
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
} CJellyBasicState;


