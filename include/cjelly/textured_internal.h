#pragma once
#include <vulkan/vulkan.h>

/* Internal-only textured resources owned by the Engine during migration */
typedef struct CJellyTexturedResources {
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  VkImage image;
  VkDeviceMemory imageMemory;
  VkImageView imageView;
  VkSampler sampler;
  VkDescriptorPool descriptorPool;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorSet descriptorSet;
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
} CJellyTexturedResources;


