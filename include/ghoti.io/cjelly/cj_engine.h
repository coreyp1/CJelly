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

/*
 * CJelly — Minimal C API stubs
 *
 * This is a design-time stub for headers. Implementation is TBD.
 */
#pragma once

#include <ghoti.io/cjelly/macros.h>
#include <stdint.h>
#include <stdbool.h>
#include <ghoti.io/cjelly/cj_allocator.h>
#include "cj_types.h"
#include "cj_result.h"
#include "runtime.h"

/** @file cj_engine.h
 *  @brief Engine creation, shutdown and global facilities.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** GPU device selection policy. */
typedef enum cj_device_select_t {
  CJ_DEVICE_SELECT_DEFAULT = 0,   /**< Prefer discrete, then integrated. */
  CJ_DEVICE_SELECT_DISCRETE_FIRST,
  CJ_DEVICE_SELECT_INTEGRATED_FIRST,
  CJ_DEVICE_SELECT_INDEX,         /**< Use `requested_device_index`. */
} cj_device_select_t;

/** Engine feature flags. */
typedef enum cj_engine_flags_t {
  CJ_ENGINE_ENABLE_VALIDATION   = CJ_BIT(0),
  CJ_ENGINE_ENABLE_DIAGNOSTICS  = CJ_BIT(1),
  CJ_ENGINE_ENABLE_THREADING    = CJ_BIT(2),
} cj_engine_flags_t;

/** Engine creation descriptor. */
typedef struct cj_engine_desc_t {
  cj_str_t           app_name;                 /**< Optional. */
  uint32_t           app_version;              /**< Optional semantic version. */

  cj_device_select_t device_select;            /**< Device selection policy. */
  uint32_t           requested_device_index;   /**< Used if DEVICE_SELECT_INDEX. */

  uint32_t           flags;                    /**< OR of cj_engine_flags_t. */

  uint32_t           bindless_limits_images;   /**< 0 = default. */
  uint32_t           bindless_limits_buffers;  /**< 0 = default. */

  const cj_allocator_t* allocator;             /**< Host allocator, or NULL for ::cj_allocator_default(). */
} cj_engine_desc_t;

/** Create the engine.
 *  @param desc Engine creation descriptor. Can be NULL for defaults.
 *  @return Pointer to the created engine, or NULL on failure.
 */
CJ_API cj_engine_t* cj_engine_create(const cj_engine_desc_t* desc);

/** Shut down the engine. Requires that all windows were destroyed.
 *  @param engine The engine to shut down.
 */
CJ_API void cj_engine_shutdown(cj_engine_t* engine);

/** The allocator this engine was created with.
 *
 *  Everything the engine and the objects under it allocate goes through it,
 *  so a caller that supplied one in ::cj_engine_desc_t can account for the
 *  library's memory. Never NULL: an engine created without one reports
 *  ::cj_allocator_default(), and so does a NULL engine.
 *
 *  @param engine The engine, or NULL.
 *  @return The allocator. Never NULL.
 */
CJ_API const cj_allocator_t* cj_engine_allocator(const cj_engine_t* engine);

/** Block until the device is idle.
 *  This waits for all pending GPU operations to complete.
 *  @param engine The engine to wait for.
 */
CJ_API void cj_engine_wait_idle(cj_engine_t* engine);

/** Return the selected device index.
 *  @param engine The engine to query.
 *  @return The index of the selected GPU device.
 */
CJ_API uint32_t cj_engine_device_index(const cj_engine_t* engine);

/** Global descriptor slot counts (bindless). */
typedef struct cj_bindless_info_t {
  uint32_t images_capacity;
  uint32_t buffers_capacity;
  uint32_t samplers_capacity;
} cj_bindless_info_t;

/** Query bindless resource capacities.
 *  @param engine The engine to query.
 *  @param out_info Pointer to receive bindless capacity information.
 */
CJ_API void cj_engine_get_bindless_info(const cj_engine_t* engine, cj_bindless_info_t* out_info);

/** Initialize GPU device and core Vulkan objects.
 *  @param engine The engine to initialize.
 *  @param use_validation Whether to enable Vulkan validation layers.
 *  @return 0 on success, non-zero on failure.
 */
CJ_API int  cj_engine_init(cj_engine_t* engine, int use_validation);

/** Destroy GPU device and core Vulkan objects.
 *  @param engine The engine whose device should be shut down.
 */
CJ_API void cj_engine_shutdown_device(cj_engine_t* engine);

/** Export a minimal Vulkan context snapshot for public helpers.
 *  @param engine The engine to export context from.
 *  @param out_ctx Pointer to receive the Vulkan context.
 */
CJ_API void cj_engine_export_context(cj_engine_t* engine, CJellyVulkanContext* out_ctx);

/** Set the process-wide current engine (for migration compatibility).
 *  @param engine The engine to set as current.
 */
CJ_API void        cj_engine_set_current(cj_engine_t* engine);

/** Get the process-wide current engine (for migration compatibility).
 *  @return Pointer to the current engine, or NULL if none is set.
 */
CJ_API cj_engine_t* cj_engine_get_current(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
