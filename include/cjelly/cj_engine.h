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
#include "cj_macros.h"
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

/** Optional custom allocator. All fields optional. */
struct cj_allocator_t {
  void* (*alloc)(void* user, size_t size, size_t align);
  void  (*free)(void* user, void* ptr);
  void* user;
};

/** Engine creation descriptor. */
typedef struct cj_engine_desc_t {
  cj_str_t           app_name;                 /**< Optional. */
  uint32_t           app_version;              /**< Optional semantic version. */

  cj_device_select_t device_select;            /**< Device selection policy. */
  uint32_t           requested_device_index;   /**< Used if DEVICE_SELECT_INDEX. */

  uint32_t           flags;                    /**< OR of cj_engine_flags_t. */

  uint32_t           bindless_limits_images;   /**< 0 = default. */
  uint32_t           bindless_limits_buffers;  /**< 0 = default. */

  const cj_allocator_t* allocator;             /**< Optional custom allocator. */
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
