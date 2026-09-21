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
 * @file rgraph_model_internal.h
 * @brief The render-graph node that draws a 3D model. Internal.
 *
 * Why this node is shaped the way it is: the engine's render pass has a single
 * colour attachment and no depth buffer, because everything CJelly drew until
 * now was a flat quad. Geometry needs depth testing, and adding a depth
 * attachment to the shared render pass would mean touching every pipeline in
 * the engine - each one would need a depth-stencil state it does not have
 * today, and a mistake in any of them breaks the windows that already work.
 *
 * So the model renders into its own offscreen target, which has its own render
 * pass with its own colour and depth attachments, and the result is composited
 * into the window as a textured quad. That is purely additive: nothing that
 * already renders changes. The cost is one extra full-screen blit per frame,
 * and the offscreen target is a fixed size rather than the window's.
 */

#ifndef GHOTI_IO_CJ_RGRAPH_MODEL_INTERNAL_H
#define GHOTI_IO_CJ_RGRAPH_MODEL_INTERNAL_H

#include <ghoti.io/cjelly/macros.h>

#include <vulkan/vulkan.h>

#include <ghoti.io/cjelly/format/3d/mesh.h>
#include <ghoti.io/cjelly/types.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/** Width of the offscreen target the model is drawn into. */
#define CJ_MODEL_TARGET_WIDTH 1024u

/** Height of the offscreen target the model is drawn into. */
#define CJ_MODEL_TARGET_HEIGHT 1024u

/**
 * @brief Everything the model node owns.
 */
typedef struct cj_rgraph_model_node_t {
  /* The offscreen target: colour, depth, and the pass that writes them. */
  VkImage color_image;
  VkDeviceMemory color_memory;
  VkImageView color_view;
  VkImage depth_image;
  VkDeviceMemory depth_memory;
  VkImageView depth_view;
  VkRenderPass render_pass;
  VkFramebuffer framebuffer;
  VkExtent2D target_extent;

  /* Drawing the mesh into that target. */
  VkPipeline pipeline;
  VkPipelineLayout pipeline_layout;
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_memory;
  VkBuffer index_buffer;
  VkDeviceMemory index_memory;
  uint32_t index_count;

  /* Compositing the target into the window's pass. */
  VkSampler sampler;
  VkDescriptorSetLayout composite_desc_layout;
  VkDescriptorPool composite_desc_pool;
  VkDescriptorSet composite_desc_set;
  VkPipeline composite_pipeline;
  VkPipelineLayout composite_pipeline_layout;
  VkBuffer quad_buffer;
  VkDeviceMemory quad_memory;

  /* What is being drawn, and where the camera sits to see it. */
  float center[3];
  /* Radius of the mesh's bounding sphere. The camera distance follows from it
   * and from the window's aspect ratio, so it is worked out per frame rather
   * than stored. */
  float radius;
  float base_color[4];
  float rotation;       /**< Radians about Y, advanced each frame. */
  float rotation_speed; /**< Radians per second. */
  uint64_t last_tick_ms;
} cj_rgraph_model_node_t;

/**
 * @brief Build the node's resources around a mesh.
 *
 * The mesh is uploaded and not retained; only its bounds are kept.
 *
 * @param engine The engine, for the device and its queues.
 * @param model The node data to populate. Zeroed on entry.
 * @param mesh The mesh to draw.
 * @return Non-zero on success.
 */
int cj_rgraph_model_create(
    cj_engine_t * engine, cj_rgraph_model_node_t * model,
    const CJellyModelMesh * mesh);

/**
 * @brief Release everything the node owns. Safe on a partly-built node.
 *
 * @param engine The engine.
 * @param model The node data.
 */
void cj_rgraph_model_destroy(
    cj_engine_t * engine, cj_rgraph_model_node_t * model);

/**
 * @brief Draw the model into the offscreen target.
 *
 * Must be recorded outside any render pass, because it begins one of its own.
 *
 * @param model The node data.
 * @param cmd The command buffer.
 * @param now_ms Current time, for the rotation.
 * @param aspect Width divided by height of the window the target will be
 *   stretched to fill. The projection is built for that shape rather than for
 *   the square target, so the stretch cancels out instead of squashing the
 *   model.
 * @return Non-zero on success.
 */
int cj_rgraph_model_prepass(cj_rgraph_model_node_t * model,
    VkCommandBuffer cmd, uint64_t now_ms, float aspect);

/**
 * @brief Composite the offscreen target into the window.
 *
 * Recorded inside the window's render pass.
 *
 * @param model The node data.
 * @param cmd The command buffer.
 * @param extent The window's extent.
 * @return Non-zero on success.
 */
int cj_rgraph_model_composite(
    cj_rgraph_model_node_t * model, VkCommandBuffer cmd, VkExtent2D extent);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // GHOTI_IO_CJ_RGRAPH_MODEL_INTERNAL_H
