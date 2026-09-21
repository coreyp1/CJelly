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

/** @file cj_result.h
 *  @brief Error codes and result helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Result codes returned by CJelly API functions. */
typedef enum cj_result_t {
  CJ_SUCCESS                = 0,   /**< Operation succeeded. */
  CJ_E_UNKNOWN              = -1,  /**< Unknown error occurred. */
  CJ_E_INVALID_ARGUMENT     = -2,  /**< Invalid argument provided. */
  CJ_E_OUT_OF_MEMORY        = -3,  /**< Out of memory. */
  CJ_E_NOT_READY            = -4,  /**< Operation not ready. */
  CJ_E_TIMEOUT              = -5,  /**< Operation timed out. */
  CJ_E_DEVICE_LOST          = -6,  /**< GPU device was lost. */
  CJ_E_SURFACE_LOST         = -7,  /**< Surface was lost. */
  CJ_E_OUT_OF_DATE          = -8,  /**< Swapchain is out of date (needs recreation). */
  CJ_E_UNSUPPORTED          = -9,  /**< Operation or feature is unsupported. */
  CJ_E_ALREADY_EXISTS       = -10, /**< Resource already exists. */
  CJ_E_NOT_FOUND            = -11, /**< Resource not found. */
  CJ_E_BUSY                 = -12, /**< Resource is busy. */
} cj_result_t;

/** Convert a result code to a human-readable string.
 *  @param r The result code to convert.
 *  @return A null-terminated string describing the result code.
 */
CJ_API const char* cj_result_str(cj_result_t r);

#ifdef __cplusplus
} /* extern "C" */
#endif
