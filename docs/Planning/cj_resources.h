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

/** @file cj_resources.h
 *  @brief Opaque resource handles and creation descriptors (textures, buffers, samplers).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Image formats (subset; expanded later). */
typedef enum cj_format_t {
  CJ_FORMAT_UNDEFINED = 0,
  CJ_FORMAT_R8_UNORM,
  CJ_FORMAT_RG8_UNORM,
  CJ_FORMAT_RGBA8_UNORM,
  CJ_FORMAT_BGRA8_UNORM,
  CJ_FORMAT_R16_FLOAT,
  CJ_FORMAT_RG16_FLOAT,
  CJ_FORMAT_RGBA16_FLOAT,
  CJ_FORMAT_R32_FLOAT,
  CJ_FORMAT_RG32_FLOAT,
  CJ_FORMAT_RGBA32_FLOAT,
  CJ_FORMAT_D24S8,
  CJ_FORMAT_D32F,
} cj_format_t;

typedef enum cj_image_usage_t {
  CJ_IMAGE_SAMPLED   = CJ_BIT(0),
  CJ_IMAGE_STORAGE   = CJ_BIT(1),
  CJ_IMAGE_COLOR_RT  = CJ_BIT(2),
  CJ_IMAGE_DEPTH_RT  = CJ_BIT(3),
} cj_image_usage_t;

typedef enum cj_buffer_usage_t {
  CJ_BUFFER_VERTEX   = CJ_BIT(0),
  CJ_BUFFER_INDEX    = CJ_BIT(1),
  CJ_BUFFER_UNIFORM  = CJ_BIT(2),
  CJ_BUFFER_STORAGE  = CJ_BIT(3),
  CJ_BUFFER_TRANSFER_SRC = CJ_BIT(4),
  CJ_BUFFER_TRANSFER_DST = CJ_BIT(5),
} cj_buffer_usage_t;

typedef enum cj_sampler_filter_t {
  CJ_FILTER_NEAREST = 0,
  CJ_FILTER_LINEAR,
} cj_sampler_filter_t;

typedef enum cj_sampler_address_t {
  CJ_ADDRESS_CLAMP = 0,
  CJ_ADDRESS_REPEAT,
  CJ_ADDRESS_MIRROR,
  CJ_ADDRESS_BORDER,
} cj_sampler_address_t;

/** Texture descriptor. */
typedef struct cj_texture_desc_t {
  uint32_t width, height, layers;
  uint32_t mips;
  cj_format_t format;
  uint32_t usage;    /**< OR of cj_image_usage_t. */
  bool     cube;     /**< Treat layers=6 as cubemap if true. */
  bool     transient;/**< Swapchain-dependent or temp (hint). */
  cj_str_t debug_name;
} cj_texture_desc_t;

/** Buffer descriptor. */
typedef struct cj_buffer_desc_t {
  uint64_t size;
  uint32_t usage;     /**< OR of cj_buffer_usage_t. */
  bool     host_visible;
  cj_str_t debug_name;
} cj_buffer_desc_t;

/** Sampler descriptor (cached; identical descriptors dedup). */
typedef struct cj_sampler_desc_t {
  cj_sampler_filter_t min_filter;
  cj_sampler_filter_t mag_filter;
  cj_sampler_address_t address_u;
  cj_sampler_address_t address_v;
  cj_sampler_address_t address_w;
  float mip_lod_bias;
  float max_anisotropy;   /**< 0 = disabled. */
  cj_str_t debug_name;
} cj_sampler_desc_t;

/** Create, retain, release, and descriptor slot queries. */
CJ_API cj_handle_t cj_texture_create(cj_engine_t*, const cj_texture_desc_t*);
CJ_API void        cj_texture_retain(cj_engine_t*, cj_handle_t);
CJ_API void        cj_texture_release(cj_engine_t*, cj_handle_t);
CJ_API uint32_t    cj_texture_descriptor_slot(cj_engine_t*, cj_handle_t);

CJ_API cj_handle_t cj_buffer_create(cj_engine_t*, const cj_buffer_desc_t*);
CJ_API void        cj_buffer_retain(cj_engine_t*, cj_handle_t);
CJ_API void        cj_buffer_release(cj_engine_t*, cj_handle_t);
CJ_API uint32_t    cj_buffer_descriptor_slot(cj_engine_t*, cj_handle_t);

CJ_API cj_handle_t cj_sampler_create(cj_engine_t*, const cj_sampler_desc_t*);
CJ_API void        cj_sampler_retain(cj_engine_t*, cj_handle_t);
CJ_API void        cj_sampler_release(cj_engine_t*, cj_handle_t);
CJ_API uint32_t    cj_sampler_descriptor_slot(cj_engine_t*, cj_handle_t);

#ifdef __cplusplus
} /* extern "C" */
#endif
