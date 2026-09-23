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
 * @file rgraph_model.c
 *
 * The render-graph node that draws a 3D model, into an offscreen target with
 * its own depth buffer, and composites the result into the window.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_log.h>
#include <ghoti.io/cjelly/engine_internal.h>
#include <ghoti.io/cjelly/mat4.h>
#include <ghoti.io/cjelly/rgraph_model_internal.h>

#include "shaders/model.vert.h"
#include "shaders/model.frag.h"
#include "shaders/model_composite.vert.h"
#include "shaders/model_composite.frag.h"

/** The push constant block, matching ModelPush in model.vert. */
typedef struct {
  float mvp[16];
  float normal_row0[4];
  float normal_row1[4];
  float normal_row2[4];
  float base_color[4];
} model_push_t;

/** One vertex of the full-screen quad that composites the target. */
typedef struct {
  float pos[2];
  float uv[2];
} composite_vertex_t;

/** Find a memory type satisfying `properties`, or UINT32_MAX. */
static uint32_t find_memory_type(VkPhysicalDevice physical_device,
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

/**
 * Pick a depth format the device actually supports.
 *
 * Vulkan guarantees one of D32_SFLOAT or X8_D24_UNORM_PACK32, not both, so
 * hard-coding either is a portability bug that only shows up on someone
 * else's GPU.
 */
static VkFormat choose_depth_format(VkPhysicalDevice physical_device) {
  const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT_S8_UINT,
      VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    VkFormatProperties properties = {0};
    vkGetPhysicalDeviceFormatProperties(
        physical_device, candidates[i], &properties);
    if (properties.optimalTilingFeatures
        & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      return candidates[i];
    }
  }
  return VK_FORMAT_UNDEFINED;
}

/** Create a host-visible buffer and fill it. */
static int create_filled_buffer(VkDevice device,
    VkPhysicalDevice physical_device, const void * data, VkDeviceSize size,
    VkBufferUsageFlags usage, VkBuffer * out_buffer,
    VkDeviceMemory * out_memory) {
  VkBufferCreateInfo info = {0};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device, &info, NULL, out_buffer) != VK_SUCCESS) {
    return 0;
  }

  VkMemoryRequirements requirements = {0};
  vkGetBufferMemoryRequirements(device, *out_buffer, &requirements);

  uint32_t type = find_memory_type(physical_device, requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (type == UINT32_MAX) {
    vkDestroyBuffer(device, *out_buffer, NULL);
    *out_buffer = VK_NULL_HANDLE;
    return 0;
  }

  VkMemoryAllocateInfo allocation = {0};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = type;
  if (vkAllocateMemory(device, &allocation, NULL, out_memory) != VK_SUCCESS) {
    vkDestroyBuffer(device, *out_buffer, NULL);
    *out_buffer = VK_NULL_HANDLE;
    return 0;
  }
  vkBindBufferMemory(device, *out_buffer, *out_memory, 0);

  void * mapped = NULL;
  if (vkMapMemory(device, *out_memory, 0, size, 0, &mapped) != VK_SUCCESS) {
    return 0;
  }
  memcpy(mapped, data, (size_t)size);
  vkUnmapMemory(device, *out_memory);
  return 1;
}

/** Create an image, its memory and a view, for use as an attachment. */
static int create_attachment(VkDevice device, VkPhysicalDevice physical_device,
    VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
    VkImageAspectFlags aspect, VkImage * out_image, VkDeviceMemory * out_memory,
    VkImageView * out_view) {
  VkImageCreateInfo info = {0};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = format;
  info.extent.width = extent.width;
  info.extent.height = extent.height;
  info.extent.depth = 1;
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device, &info, NULL, out_image) != VK_SUCCESS) {
    return 0;
  }

  VkMemoryRequirements requirements = {0};
  vkGetImageMemoryRequirements(device, *out_image, &requirements);
  uint32_t type = find_memory_type(physical_device, requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == UINT32_MAX) {
    vkDestroyImage(device, *out_image, NULL);
    *out_image = VK_NULL_HANDLE;
    return 0;
  }

  VkMemoryAllocateInfo allocation = {0};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = type;
  if (vkAllocateMemory(device, &allocation, NULL, out_memory) != VK_SUCCESS) {
    vkDestroyImage(device, *out_image, NULL);
    *out_image = VK_NULL_HANDLE;
    return 0;
  }
  vkBindImageMemory(device, *out_image, *out_memory, 0);

  VkImageViewCreateInfo view = {0};
  view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view.image = *out_image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = format;
  view.subresourceRange.aspectMask = aspect;
  view.subresourceRange.levelCount = 1;
  view.subresourceRange.layerCount = 1;
  return vkCreateImageView(device, &view, NULL, out_view) == VK_SUCCESS;
}

/** The offscreen render pass: colour written then sampled, plus depth. */
static int create_offscreen_render_pass(
    VkDevice device, VkFormat color_format, VkFormat depth_format,
    VkRenderPass * out_pass) {
  VkAttachmentDescription attachments[2] = {0};

  attachments[0].format = color_format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  // The composite step samples it, so the pass leaves it ready for that.
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  attachments[1].format = depth_format;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depth_ref = {
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpass = {0};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;
  subpass.pDepthStencilAttachment = &depth_ref;

  // Two dependencies: the first orders the clear after any previous sampling
  // of the image, the second makes the written colour visible to the fragment
  // shader that composites it.
  VkSubpassDependency dependencies[2] = {0};
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  VkRenderPassCreateInfo info = {0};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 2;
  info.pAttachments = attachments;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 2;
  info.pDependencies = dependencies;
  return vkCreateRenderPass(device, &info, NULL, out_pass) == VK_SUCCESS;
}

/** Create a shader module from an embedded SPIR-V blob. */
static VkShaderModule create_module(
    VkDevice device, const unsigned char * code, unsigned int length) {
  VkShaderModuleCreateInfo info = {0};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = length;
  info.pCode = (const uint32_t *)code;
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &info, NULL, &module) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  return module;
}

/** The pipeline that draws the mesh into the offscreen target. */
static int create_model_pipeline(
    VkDevice device, cj_rgraph_model_node_t * model) {
  VkShaderModule vert = create_module(device, model_vert_spv, model_vert_spv_len);
  VkShaderModule frag = create_module(device, model_frag_spv, model_frag_spv_len);
  if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
    if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(device, vert, NULL);
    if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(device, frag, NULL);
    return 0;
  }

  VkPushConstantRange push = {0};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push.offset = 0;
  push.size = sizeof(model_push_t);

  VkPipelineLayoutCreateInfo layout = {0};
  layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout.pushConstantRangeCount = 1;
  layout.pPushConstantRanges = &push;
  if (vkCreatePipelineLayout(device, &layout, NULL, &model->pipeline_layout)
      != VK_SUCCESS) {
    vkDestroyShaderModule(device, vert, NULL);
    vkDestroyShaderModule(device, frag, NULL);
    return 0;
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
  binding.stride = sizeof(CJellyModelVertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributes[3] = {0};
  attributes[0].location = 0;
  attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[0].offset = offsetof(CJellyModelVertex, position);
  attributes[1].location = 1;
  attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[1].offset = offsetof(CJellyModelVertex, normal);
  attributes[2].location = 2;
  attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
  attributes[2].offset = offsetof(CJellyModelVertex, texcoord);

  VkPipelineVertexInputStateCreateInfo vertex_input = {0};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 3;
  vertex_input.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo assembly = {0};
  assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport = {0};
  viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster = {0};
  raster.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  // No culling: OBJ files are not reliably wound, and the fragment shader
  // lights both sides, so a reversed triangle is dim rather than missing.
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample = {0};
  multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth = {0};
  depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_LESS;
  depth.minDepthBounds = 0.0f;
  depth.maxDepthBounds = 1.0f;

  VkPipelineColorBlendAttachmentState blend_attachment = {0};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo blend = {0};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[2] = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic = {0};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo info = {0};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depth;
  info.pColorBlendState = &blend;
  info.pDynamicState = &dynamic;
  info.layout = model->pipeline_layout;
  info.renderPass = model->render_pass;
  info.subpass = 0;

  VkResult result = vkCreateGraphicsPipelines(
      device, VK_NULL_HANDLE, 1, &info, NULL, &model->pipeline);
  vkDestroyShaderModule(device, vert, NULL);
  vkDestroyShaderModule(device, frag, NULL);
  return result == VK_SUCCESS;
}

/** The pipeline that draws the offscreen target as a full-screen quad. */
static int create_composite_pipeline(
    VkDevice device, VkRenderPass window_pass, cj_rgraph_model_node_t * model) {
  VkShaderModule vert =
      create_module(device, model_composite_vert_spv, model_composite_vert_spv_len);
  VkShaderModule frag = create_module(
      device, model_composite_frag_spv, model_composite_frag_spv_len);
  if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
    if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(device, vert, NULL);
    if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(device, frag, NULL);
    return 0;
  }

  VkPipelineLayoutCreateInfo layout = {0};
  layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout.setLayoutCount = 1;
  layout.pSetLayouts = &model->composite_desc_layout;
  if (vkCreatePipelineLayout(
          device, &layout, NULL, &model->composite_pipeline_layout)
      != VK_SUCCESS) {
    vkDestroyShaderModule(device, vert, NULL);
    vkDestroyShaderModule(device, frag, NULL);
    return 0;
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
  binding.stride = sizeof(composite_vertex_t);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributes[2] = {0};
  attributes[0].location = 0;
  attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributes[0].offset = offsetof(composite_vertex_t, pos);
  attributes[1].location = 1;
  attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributes[1].offset = offsetof(composite_vertex_t, uv);

  VkPipelineVertexInputStateCreateInfo vertex_input = {0};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 2;
  vertex_input.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo assembly = {0};
  assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport = {0};
  viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster = {0};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample = {0};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend_attachment = {0};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo blend = {0};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[2] = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic = {0};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo info = {0};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pColorBlendState = &blend;
  info.pDynamicState = &dynamic;
  info.layout = model->composite_pipeline_layout;
  info.renderPass = window_pass;
  info.subpass = 0;

  VkResult result = vkCreateGraphicsPipelines(
      device, VK_NULL_HANDLE, 1, &info, NULL, &model->composite_pipeline);
  vkDestroyShaderModule(device, vert, NULL);
  vkDestroyShaderModule(device, frag, NULL);
  return result == VK_SUCCESS;
}

int cj_rgraph_model_create(cj_engine_t * engine,
    cj_rgraph_model_node_t * model, const CJellyModelMesh * mesh) {
  if (!engine || !model || !mesh || mesh->index_count == 0) {
    return 0;
  }

  VkDevice device = cj_engine_device(engine);
  VkPhysicalDevice physical_device = cj_engine_physical_device(engine);
  if (device == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE) {
    return 0;
  }

  memset(model, 0, sizeof(*model));
  model->target_extent.width = CJ_MODEL_TARGET_WIDTH;
  model->target_extent.height = CJ_MODEL_TARGET_HEIGHT;
  model->base_color[0] = 0.78f;
  model->base_color[1] = 0.76f;
  model->base_color[2] = 0.70f;
  model->base_color[3] = 1.0f;
  model->rotation_speed = 0.6f;

  // Keep the mesh's bounds; the camera distance follows from them and from
  // the window's aspect ratio, worked out per frame.
  memcpy(model->center, mesh->center, sizeof(model->center));
  model->radius = mesh->radius > 1e-6f ? mesh->radius : 1.0f;

  VkFormat depth_format = choose_depth_format(physical_device);
  if (depth_format == VK_FORMAT_UNDEFINED) {
    CJ_ERRORF("model node: no usable depth format");
    return 0;
  }

  if (!create_attachment(device, physical_device, model->target_extent,
          VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT, &model->color_image, &model->color_memory,
          &model->color_view)) {
    CJ_ERRORF("model node: failed to create the colour target");
    goto failed;
  }
  if (!create_attachment(device, physical_device, model->target_extent,
          depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
          VK_IMAGE_ASPECT_DEPTH_BIT, &model->depth_image, &model->depth_memory,
          &model->depth_view)) {
    CJ_ERRORF("model node: failed to create the depth target");
    goto failed;
  }
  if (!create_offscreen_render_pass(
          device, VK_FORMAT_R8G8B8A8_UNORM, depth_format,
          &model->render_pass)) {
    CJ_ERRORF("model node: failed to create the offscreen render pass");
    goto failed;
  }

  {
    VkImageView attachments[2] = {model->color_view, model->depth_view};
    VkFramebufferCreateInfo framebuffer = {0};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = model->render_pass;
    framebuffer.attachmentCount = 2;
    framebuffer.pAttachments = attachments;
    framebuffer.width = model->target_extent.width;
    framebuffer.height = model->target_extent.height;
    framebuffer.layers = 1;
    if (vkCreateFramebuffer(device, &framebuffer, NULL, &model->framebuffer)
        != VK_SUCCESS) {
      CJ_ERRORF("model node: failed to create the framebuffer");
      goto failed;
    }
  }

  if (!create_model_pipeline(device, model)) {
    CJ_ERRORF("model node: failed to create the model pipeline");
    goto failed;
  }

  if (!create_filled_buffer(device, physical_device, mesh->vertices,
          (VkDeviceSize)mesh->vertex_count * sizeof(CJellyModelVertex),
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &model->vertex_buffer,
          &model->vertex_memory)) {
    CJ_ERRORF("model node: failed to upload the vertices");
    goto failed;
  }
  if (!create_filled_buffer(device, physical_device, mesh->indices,
          (VkDeviceSize)mesh->index_count * sizeof(uint32_t),
          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &model->index_buffer,
          &model->index_memory)) {
    CJ_ERRORF("model node: failed to upload the indices");
    goto failed;
  }
  model->index_count = mesh->index_count;

  {
    // The composite quad, in clip space with matching texture coordinates.
    const composite_vertex_t quad[6] = {
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f}, {1.0f, 1.0f}},
        {{1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
    };
    if (!create_filled_buffer(device, physical_device, quad, sizeof(quad),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &model->quad_buffer,
            &model->quad_memory)) {
      CJ_ERRORF("model node: failed to create the composite quad");
      goto failed;
    }
  }

  {
    VkSamplerCreateInfo sampler = {0};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    sampler.maxLod = 1.0f;
    if (vkCreateSampler(device, &sampler, NULL, &model->sampler)
        != VK_SUCCESS) {
      CJ_ERRORF("model node: failed to create the sampler");
      goto failed;
    }
  }

  {
    VkDescriptorSetLayoutBinding binding = {0};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout = {0};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(
            device, &layout, NULL, &model->composite_desc_layout)
        != VK_SUCCESS) {
      CJ_ERRORF("model node: failed to create the descriptor layout");
      goto failed;
    }

    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool = {0};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    if (vkCreateDescriptorPool(device, &pool, NULL, &model->composite_desc_pool)
        != VK_SUCCESS) {
      CJ_ERRORF("model node: failed to create the descriptor pool");
      goto failed;
    }

    VkDescriptorSetAllocateInfo allocation = {0};
    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = model->composite_desc_pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &model->composite_desc_layout;
    if (vkAllocateDescriptorSets(device, &allocation, &model->composite_desc_set)
        != VK_SUCCESS) {
      CJ_ERRORF("model node: failed to allocate the descriptor set");
      goto failed;
    }

    VkDescriptorImageInfo image = {0};
    image.sampler = model->sampler;
    image.imageView = model->color_view;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {0};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = model->composite_desc_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
  }

  if (!create_composite_pipeline(
          device, cj_engine_render_pass(engine), model)) {
    CJ_ERRORF("model node: failed to create the composite pipeline");
    goto failed;
  }

  return 1;

failed:
  cj_rgraph_model_destroy(engine, model);
  return 0;
}

void cj_rgraph_model_destroy(
    cj_engine_t * engine, cj_rgraph_model_node_t * model) {
  if (!engine || !model) {
    return;
  }
  VkDevice device = cj_engine_device(engine);
  if (device == VK_NULL_HANDLE) {
    return;
  }

#define DESTROY(handle, fn)                                                    \
  do {                                                                         \
    if ((handle) != VK_NULL_HANDLE) {                                          \
      fn(device, (handle), NULL);                                              \
      (handle) = VK_NULL_HANDLE;                                               \
    }                                                                          \
  } while (0)

  DESTROY(model->composite_pipeline, vkDestroyPipeline);
  DESTROY(model->composite_pipeline_layout, vkDestroyPipelineLayout);
  DESTROY(model->composite_desc_pool, vkDestroyDescriptorPool);
  DESTROY(model->composite_desc_layout, vkDestroyDescriptorSetLayout);
  DESTROY(model->sampler, vkDestroySampler);
  DESTROY(model->quad_buffer, vkDestroyBuffer);
  DESTROY(model->quad_memory, vkFreeMemory);
  DESTROY(model->index_buffer, vkDestroyBuffer);
  DESTROY(model->index_memory, vkFreeMemory);
  DESTROY(model->vertex_buffer, vkDestroyBuffer);
  DESTROY(model->vertex_memory, vkFreeMemory);
  DESTROY(model->pipeline, vkDestroyPipeline);
  DESTROY(model->pipeline_layout, vkDestroyPipelineLayout);
  DESTROY(model->framebuffer, vkDestroyFramebuffer);
  DESTROY(model->render_pass, vkDestroyRenderPass);
  DESTROY(model->depth_view, vkDestroyImageView);
  DESTROY(model->depth_image, vkDestroyImage);
  DESTROY(model->depth_memory, vkFreeMemory);
  DESTROY(model->color_view, vkDestroyImageView);
  DESTROY(model->color_image, vkDestroyImage);
  DESTROY(model->color_memory, vkFreeMemory);

#undef DESTROY

  model->index_count = 0;
}

int cj_rgraph_model_prepass(cj_rgraph_model_node_t * model,
    VkCommandBuffer cmd, uint64_t now_ms, float aspect) {
  if (!model || cmd == VK_NULL_HANDLE
      || model->render_pass == VK_NULL_HANDLE
      || model->pipeline == VK_NULL_HANDLE || model->index_count == 0) {
    return 0;
  }

  // Advance the rotation by elapsed time rather than per frame, so the model
  // turns at the same rate whatever the frame rate is.
  if (model->last_tick_ms != 0 && now_ms > model->last_tick_ms) {
    float elapsed = (float)(now_ms - model->last_tick_ms) * 0.001f;
    model->rotation += model->rotation_speed * elapsed;
    const float two_pi = 6.2831853072f;
    while (model->rotation > two_pi) {
      model->rotation -= two_pi;
    }
  }
  model->last_tick_ms = now_ms;

  VkClearValue clears[2];
  memset(clears, 0, sizeof(clears));
  clears[0].color.float32[0] = 0.06f;
  clears[0].color.float32[1] = 0.07f;
  clears[0].color.float32[2] = 0.09f;
  clears[0].color.float32[3] = 1.0f;
  clears[1].depthStencil.depth = 1.0f;

  VkRenderPassBeginInfo begin = {0};
  begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  begin.renderPass = model->render_pass;
  begin.framebuffer = model->framebuffer;
  begin.renderArea.extent = model->target_extent;
  begin.clearValueCount = 2;
  begin.pClearValues = clears;
  vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {0};
  viewport.width = (float)model->target_extent.width;
  viewport.height = (float)model->target_extent.height;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);

  VkRect2D scissor = {0};
  scissor.extent = model->target_extent;
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model->pipeline);

  // Centre the model on the origin, spin it, then look at it from far enough
  // away that its bounding sphere fits.
  CJellyMat4 recentre = cjelly_mat4_translation(
      -model->center[0], -model->center[1], -model->center[2]);
  CJellyMat4 spin = cjelly_mat4_rotation_y(model->rotation);
  CJellyMat4 tilt = cjelly_mat4_rotation_x(-0.35f);
  CJellyMat4 world =
      cjelly_mat4_multiply(tilt, cjelly_mat4_multiply(spin, recentre));

  // The projection is built for the window's shape, not the target's. The
  // target is square and gets stretched to fill the window, so projecting for
  // the window pre-distorts the image by exactly the amount that stretch
  // undoes. Projecting for the square instead leaves the model squashed by
  // the window's aspect ratio - which is also why this cannot be a constant:
  // the same window description produces different client areas on different
  // platforms.
  if (!(aspect > 0.0f)) {
    aspect = 1.0f;
  }
  const float fovy = 0.7853981634f;

  // Reframe for that aspect too, so the model still fits along whichever axis
  // is the tighter one.
  float distance =
      cjelly_camera_distance_for_sphere(model->radius, fovy, aspect) * 1.15f;

  const float eye[3] = {0.0f, 0.0f, distance};
  const float target[3] = {0.0f, 0.0f, 0.0f};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  CJellyMat4 view = cjelly_mat4_look_at(eye, target, up);
  CJellyMat4 projection = cjelly_mat4_perspective(
      fovy, aspect, distance * 0.01f, distance * 4.0f);

  CJellyMat4 mvp = cjelly_mat4_multiply(
      projection, cjelly_mat4_multiply(view, world));

  model_push_t push;
  memset(&push, 0, sizeof(push));
  memcpy(push.mvp, mvp.m, sizeof(push.mvp));
  // The rotation part of the world transform, by row, for the normals. There
  // is no non-uniform scale in it, so the inverse transpose is unnecessary.
  for (int row = 0; row < 3; row++) {
    float * destination = row == 0 ? push.normal_row0
        : (row == 1 ? push.normal_row1 : push.normal_row2);
    for (int col = 0; col < 3; col++) {
      destination[col] = world.m[col * 4 + row];
    }
  }
  memcpy(push.base_color, model->base_color, sizeof(push.base_color));

  vkCmdPushConstants(cmd, model->pipeline_layout,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
      sizeof(push), &push);

  VkDeviceSize offsets[1] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, &model->vertex_buffer, offsets);
  vkCmdBindIndexBuffer(cmd, model->index_buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, model->index_count, 1, 0, 0, 0);

  vkCmdEndRenderPass(cmd);
  return 1;
}

int cj_rgraph_model_composite(
    cj_rgraph_model_node_t * model, VkCommandBuffer cmd, VkExtent2D extent) {
  if (!model || cmd == VK_NULL_HANDLE
      || model->composite_pipeline == VK_NULL_HANDLE) {
    return 0;
  }

  VkViewport viewport = {0};
  viewport.width = (float)extent.width;
  viewport.height = (float)extent.height;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);

  VkRect2D scissor = {0};
  scissor.extent = extent;
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdBindPipeline(
      cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model->composite_pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
      model->composite_pipeline_layout, 0, 1, &model->composite_desc_set, 0,
      NULL);

  VkDeviceSize offsets[1] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, &model->quad_buffer, offsets);
  vkCmdDraw(cmd, 6, 1, 0, 0);
  return 1;
}
