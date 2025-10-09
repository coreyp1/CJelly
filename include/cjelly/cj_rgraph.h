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

typedef struct cj_rgraph_desc_t {
  uint32_t reserved;
} cj_rgraph_desc_t;

/** Create/destroy a render graph. The engine owns device resources; the graph is a logical plan. */
CJ_API cj_rgraph_t* cj_rgraph_create(cj_engine_t* engine, const cj_rgraph_desc_t* desc);
CJ_API void         cj_rgraph_destroy(cj_rgraph_t* graph);

/** Recompile the graph after a window resize or pipeline cache change. */
CJ_API cj_result_t  cj_rgraph_recompile(cj_rgraph_t* graph);

/** Bind a named external resource (e.g., texture) into the graph. */
CJ_API cj_result_t  cj_rgraph_bind_texture(cj_rgraph_t* graph, cj_str_t name, cj_handle_t texture);

/** Set an integer parameter (e.g., debug toggles). */
CJ_API cj_result_t  cj_rgraph_set_i32(cj_rgraph_t* graph, cj_str_t name, int32_t value);

/** Add a blur post-processing node to the render graph. */
CJ_API cj_result_t  cj_rgraph_add_blur_node(cj_rgraph_t* graph, const char* name);

/** Add a textured rendering node to the render graph. */
CJ_API cj_result_t  cj_rgraph_add_textured_node(cj_rgraph_t* graph, const char* name);

/** Add a color rendering node to the render graph. */
CJ_API cj_result_t  cj_rgraph_add_color_node(cj_rgraph_t* graph, const char* name);

/** Execute the render graph with the given command buffer and extent. */
CJ_API cj_result_t  cj_rgraph_execute(cj_rgraph_t* graph, VkCommandBuffer cmd, VkExtent2D extent);

#ifdef __cplusplus
} /* extern "C" */
#endif
