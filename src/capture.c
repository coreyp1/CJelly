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
 * @file capture.c
 *
 * Copying a presented swapchain image into ordinary memory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* window_internal.h declares a helper taking Xlib types on Linux, so the
 * platform headers have to come first - the same order window.c uses. */
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan.h>
#else
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>
#endif

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_capture.h>
#include <ghoti.io/cjelly/engine_internal.h>
#include <ghoti.io/cjelly/window_internal.h>

#include <ghoti.io/image/codec.h>
#include <ghoti.io/image/doc.h>
#include <ghoti.io/image/raster.h>
#include <ghoti.io/image/stream.h>

/** Where the red channel sits in the swapchain's own byte order. */
typedef struct {
  int red;
  int green;
  int blue;
  int alpha;
  bool understood;
} capture_swizzle_t;

/**
 * Work out the channel order of a swapchain format.
 *
 * Only the 8-bit four-channel formats are handled: those are what a swapchain
 * uses in practice, and guessing at a format that is not one of them would
 * produce a plausible-looking image with the colours wrong, which is worse
 * than refusing.
 */
static capture_swizzle_t capture_swizzle_for(VkFormat format) {
  capture_swizzle_t s = {0, 1, 2, 3, true};
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
      return s;
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB: {
      capture_swizzle_t bgra = {2, 1, 0, 3, true};
      return bgra;
    }
    default: {
      capture_swizzle_t unknown = {0, 1, 2, 3, false};
      return unknown;
    }
  }
}

/** Find a memory type satisfying `properties`, or UINT32_MAX. */
static uint32_t capture_memory_type(VkPhysicalDevice physical_device,
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

/** Move a swapchain image between the presentable and readable layouts. */
static void capture_transition(VkCommandBuffer cmd, VkImage image,
    VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
    VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage) {
  VkImageMemoryBarrier barrier = {0};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = src_access;
  barrier.dstAccessMask = dst_access;
  vkCmdPipelineBarrier(
      cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

CJ_API cj_result_t cj_window_capture(
    cj_window_t * window, cj_capture_t * out_capture) {
  if (!out_capture) {
    return CJ_E_INVALID_ARGUMENT;
  }
  memset(out_capture, 0, sizeof(*out_capture));
  if (!window) {
    return CJ_E_INVALID_ARGUMENT;
  }

  cj_window_frame_source_t source = {0};
  if (!cj_window__last_presented_frame(window, &source)) {
    /* Nothing has been presented yet, so there is nothing to read. */
    return CJ_E_INVALID_ARGUMENT;
  }

  capture_swizzle_t swizzle = capture_swizzle_for(source.format);
  if (!swizzle.understood) {
    fprintf(stderr,
        "cj_window_capture: swapchain format %d is not one this knows how to "
        "unpack\n",
        (int)source.format);
    return CJ_E_UNKNOWN;
  }
  if (source.extent.width == 0 || source.extent.height == 0) {
    return CJ_E_INVALID_ARGUMENT;
  }

  cj_engine_t * engine = cj_engine_get_current();
  VkDevice device = cj_engine_device(engine);
  VkPhysicalDevice physical_device = cj_engine_physical_device(engine);
  VkCommandPool pool = cj_engine_command_pool(engine);
  VkQueue queue = cj_engine_graphics_queue(engine);
  if (device == VK_NULL_HANDLE || pool == VK_NULL_HANDLE
      || queue == VK_NULL_HANDLE) {
    return CJ_E_UNKNOWN;
  }

  /* The frame being read may still be in flight. A capture is a diagnostic
   * taken occasionally, so waiting for the device is the right trade against
   * threading a fence through the present path. */
  vkDeviceWaitIdle(device);

  VkDeviceSize size =
      (VkDeviceSize)source.extent.width * source.extent.height * 4u;

  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  uint8_t * pixels = NULL;
  cj_result_t result = CJ_E_UNKNOWN;

  VkBufferCreateInfo buffer_info = {0};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device, &buffer_info, NULL, &buffer) != VK_SUCCESS) {
    goto done;
  }

  VkMemoryRequirements requirements = {0};
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  uint32_t type = capture_memory_type(physical_device,
      requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (type == UINT32_MAX) {
    goto done;
  }

  VkMemoryAllocateInfo allocation = {0};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = type;
  if (vkAllocateMemory(device, &allocation, NULL, &memory) != VK_SUCCESS) {
    result = CJ_E_OUT_OF_MEMORY;
    goto done;
  }
  vkBindBufferMemory(device, buffer, memory, 0);

  VkCommandBufferAllocateInfo cmd_info = {0};
  cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmd_info.commandPool = pool;
  cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmd_info.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(device, &cmd_info, &cmd) != VK_SUCCESS) {
    goto done;
  }

  VkCommandBufferBeginInfo begin = {0};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
    goto done;
  }

  capture_transition(cmd, source.image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_READ_BIT,
      VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy region = {0};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = source.extent.width;
  region.imageExtent.height = source.extent.height;
  region.imageExtent.depth = 1;
  vkCmdCopyImageToBuffer(cmd, source.image,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

  /* Put it back, or the presentation engine is handed an image in a layout it
   * does not expect the next time this index comes round. */
  capture_transition(cmd, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT,
      VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT);

  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    goto done;
  }

  VkFenceCreateInfo fence_info = {0};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if (vkCreateFence(device, &fence_info, NULL, &fence) != VK_SUCCESS) {
    goto done;
  }

  VkSubmitInfo submit = {0};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) {
    goto done;
  }
  if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
    goto done;
  }

  void * mapped = NULL;
  if (vkMapMemory(device, memory, 0, size, 0, &mapped) != VK_SUCCESS) {
    goto done;
  }

  pixels = (uint8_t *)malloc((size_t)size);
  if (!pixels) {
    vkUnmapMemory(device, memory);
    result = CJ_E_OUT_OF_MEMORY;
    goto done;
  }

  {
    const uint8_t * src = (const uint8_t *)mapped;
    size_t count = (size_t)source.extent.width * source.extent.height;
    for (size_t i = 0; i < count; i++) {
      const uint8_t * in = src + i * 4;
      uint8_t * out = pixels + i * 4;
      out[0] = in[swizzle.red];
      out[1] = in[swizzle.green];
      out[2] = in[swizzle.blue];
      out[3] = in[swizzle.alpha];
    }
  }
  vkUnmapMemory(device, memory);

  out_capture->pixels = pixels;
  out_capture->width = source.extent.width;
  out_capture->height = source.extent.height;
  out_capture->stride = (size_t)source.extent.width * 4u;
  pixels = NULL;
  result = CJ_SUCCESS;

done:
  free(pixels);
  if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, NULL);
  if (cmd != VK_NULL_HANDLE) vkFreeCommandBuffers(device, pool, 1, &cmd);
  if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, NULL);
  if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, NULL);
  return result;
}

CJ_API void cj_capture_free(cj_capture_t * capture) {
  if (!capture) {
    return;
  }
  free(capture->pixels);
  memset(capture, 0, sizeof(*capture));
}

CJ_API bool cj_capture_pixel(const cj_capture_t * capture, uint32_t x,
    uint32_t y, uint8_t out_rgba[4]) {
  if (!capture || !capture->pixels || !out_rgba) {
    return false;
  }
  if (x >= capture->width || y >= capture->height) {
    return false;
  }
  const uint8_t * pixel = capture->pixels + (size_t)y * capture->stride + x * 4u;
  out_rgba[0] = pixel[0];
  out_rgba[1] = pixel[1];
  out_rgba[2] = pixel[2];
  out_rgba[3] = pixel[3];
  return true;
}

CJ_API cj_result_t cj_capture_write_png(
    const cj_capture_t * capture, const char * path) {
  if (!capture || !capture->pixels || !path) {
    return CJ_E_INVALID_ARGUMENT;
  }

  cj_result_t result = CJ_E_UNKNOWN;
  GIMG_Raster * raster = NULL;
  GIMG_Doc * doc = NULL;
  GIMG_Stream * stream = NULL;
  FILE * file = NULL;

  /* Borrowed: the raster reads the capture's buffer and does not take it. */
  if (gimg_raster_create(capture->width, capture->height, &GIMG_PIXEL_RGBA8,
          GIMG_RASTER_BORROWED, capture->pixels, capture->stride, &raster)
      != GIMG_OK) {
    goto done;
  }
  if (gimg_doc_from_raster(raster, &doc) != GIMG_OK) {
    goto done;
  }
  if (gimg_stream_create_memory_output(&stream) != GIMG_OK) {
    goto done;
  }

  GIMG_Save_Report report = {0};
  if (gimg_doc_save(doc, stream, "png", NULL, &report) != GIMG_OK) {
    goto done;
  }

  const void * encoded = NULL;
  size_t encoded_size = 0;
  gimg_stream_output_buffer(stream, &encoded, &encoded_size);
  if (!encoded || encoded_size == 0) {
    goto done;
  }

  file = fopen(path, "wb");
  if (!file) {
    result = CJ_E_INVALID_ARGUMENT;
    goto done;
  }
  if (fwrite(encoded, 1, encoded_size, file) != encoded_size) {
    goto done;
  }
  result = CJ_SUCCESS;

done:
  if (file) fclose(file);
  gimg_stream_destroy(stream);
  gimg_doc_destroy(doc);
  gimg_raster_destroy(raster);
  return result;
}
