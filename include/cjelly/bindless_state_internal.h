#pragma once
#include <vulkan/vulkan.h>

/* Internal-only bindless state owned by the Engine during migration */
typedef struct CJellyBindlessState {
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
} CJellyBindlessState;


