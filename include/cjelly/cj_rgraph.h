/*
 * CJelly — Minimal C API stubs
 * Copyright (c) 2025
 *
 * This is a design-time stub for headers. Implementation is TBD.
 * Licensed under the MIT license for prototype purposes.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include "cj_macros.h"
#include "cj_types.h"
#include "cj_result.h"
#include "cj_resources.h"

/** @file cj_rgraph.h
 *  @brief Minimal render-graph hooks (stub). Concrete API TBD.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Render graph creation descriptor. */
typedef struct cj_rgraph_desc_t {
  uint32_t reserved;  /**< Reserved for future use. */
} cj_rgraph_desc_t;

/** Create a render graph.
 *  The engine owns device resources; the graph is a logical plan for rendering.
 *  @param engine The engine to create the graph for.
 *  @param desc Graph creation descriptor. Can be NULL for defaults.
 *  @return Pointer to the created render graph, or NULL on failure.
 */
CJ_API cj_rgraph_t* cj_rgraph_create(cj_engine_t* engine, const cj_rgraph_desc_t* desc);

/** Destroy a render graph.
 *  @param graph The render graph to destroy. NULL is safe and will be ignored.
 */
CJ_API void         cj_rgraph_destroy(cj_rgraph_t* graph);

/** Recompile the graph after a window resize or pipeline cache change.
 *  @param graph The render graph to recompile.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_recompile(cj_rgraph_t* graph);

/** Bind a named external resource (e.g., texture) into the graph.
 *  @param graph The render graph to bind the resource to.
 *  @param name Name of the binding point.
 *  @param texture Handle to the texture resource.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_bind_texture(cj_rgraph_t* graph, cj_str_t name, cj_handle_t texture);

/** Set an integer parameter in the render graph (e.g., debug toggles, animation parameters).
 *  @param graph The render graph to set the parameter in.
 *  @param name Name of the parameter.
 *  @param value Integer value to set.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_set_i32(cj_rgraph_t* graph, cj_str_t name, int32_t value);

/** Add a blur post-processing node to the render graph.
 *  @param graph The render graph to add the node to.
 *  @param name Name for the blur node.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_add_blur_node(cj_rgraph_t* graph, const char* name);

/** Add a textured rendering node to the render graph.
 *  @param graph The render graph to add the node to.
 *  @param name Name for the textured node.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_add_textured_node(cj_rgraph_t* graph, const char* name);

/** Add a color rendering node to the render graph.
 *  @param graph The render graph to add the node to.
 *  @param name Name for the color node.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_add_color_node(cj_rgraph_t* graph, const char* name);

/** Add a node that draws a 3D model loaded from a Wavefront OBJ file.
 *
 *  The model is drawn into an offscreen target with its own depth buffer and
 *  composited into the window, because the engine's render pass has no depth
 *  attachment. Faces naming a vertex that does not exist are skipped, and
 *  reported on stderr.
 *
 *  A node added this way needs cj_rgraph_execute_prepass() to be called before
 *  the window's render pass begins; the window's own frame path does that.
 *
 *  @param graph The render graph to add the node to.
 *  @param name Name for the model node.
 *  @param obj_path Path to the .obj file.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_add_model_node(cj_rgraph_t* graph, const char* name, const char* obj_path);

/** Record the work that has to happen outside the window's render pass.
 *
 *  Nodes that render into their own targets - the model node, for one - cannot
 *  do so inside an already-active render pass. This is recorded after the
 *  command buffer begins and before the window's pass does. A graph with no
 *  such nodes records nothing.
 *
 *  @param graph The render graph.
 *  @param cmd Command buffer, with no render pass active.
 *  @param now_ms Current time in milliseconds, for time-based animation.
 *  @param extent The window's extent, for nodes whose output is stretched to
 *    fill it and which therefore have to know its shape.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_execute_prepass(cj_rgraph_t* graph, VkCommandBuffer cmd, uint64_t now_ms, VkExtent2D extent);

/** Execute the render graph with the given command buffer and extent.
 *  @param graph The render graph to execute.
 *  @param cmd Command buffer to record rendering commands into.
 *  @param extent Viewport extent for rendering.
 *  @return CJ_SUCCESS on success, or an error code.
 */
CJ_API cj_result_t  cj_rgraph_execute(cj_rgraph_t* graph, VkCommandBuffer cmd, VkExtent2D extent);

#ifdef __cplusplus
} /* extern "C" */
#endif
