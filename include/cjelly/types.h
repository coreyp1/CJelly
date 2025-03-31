/**
 * @file types.h
 * @brief CJelly common type definitions.
 *
 * @details
 * This header includes forward declarations for opaque structures used in the
 * CJelly library.
 *
 * @author
 * Ghoti.io
 *
 * @date
 * 2025
 *
 * @copyright
 * Copyright (C) 2025 Ghoti.io
 */

#ifndef CJELLY_TYPES_H
#define CJELLY_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#include <cjelly/macros.h>


/**
 * @brief Opaque structure representing the CJelly Application.
 */
typedef struct CJellyApplication CJellyApplication;


/**
 * @brief Enum representing desired device types.
 *
 * @var CJELLY_DEVICE_TYPE_ANY
 *  Device type is unspecified.
 *
 * @var CJELLY_DEVICE_TYPE_DISCRETE
 *  Prefer a discrete GPU.
 *
 * @var CJELLY_DEVICE_TYPE_INTEGRATED
 *  Prefer an integrated GPU.
 */
typedef enum CJellyApplicationDeviceType {
  CJELLY_DEVICE_TYPE_ANY = 0,
  CJELLY_DEVICE_TYPE_DISCRETE,
  CJELLY_DEVICE_TYPE_INTEGRATED,
} CJellyApplicationDeviceType;


/**
 * @brief Opaque structure representing the CJelly Vulkan context.
 */
typedef struct CJellyVulkanContext CJellyVulkanContext;


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CJELLY_TYPES_H
