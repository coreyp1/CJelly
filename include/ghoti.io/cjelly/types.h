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
 */

#ifndef GHOTI_IO_CJ_TYPES_H
#define GHOTI_IO_CJ_TYPES_H

#include <ghoti.io/cjelly/macros.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


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

#endif // GHOTI_IO_CJ_TYPES_H
