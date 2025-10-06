/*
 * CJelly — Minimal C API stubs
 * Copyright (c) 2025
 *
 * This is a design-time stub for headers. Implementation is TBD.
 * Licensed under the MIT license for prototype purposes.
 */
#pragma once
#include "cj_macros.h"
#include <stdint.h>

/** @file cj_result.h
 *  @brief Error codes and result helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cj_result_t {
  CJ_SUCCESS                = 0,
  CJ_E_UNKNOWN              = -1,
  CJ_E_INVALID_ARGUMENT     = -2,
  CJ_E_OUT_OF_MEMORY        = -3,
  CJ_E_NOT_READY            = -4,
  CJ_E_TIMEOUT              = -5,
  CJ_E_DEVICE_LOST          = -6,
  CJ_E_SURFACE_LOST         = -7,
  CJ_E_OUT_OF_DATE          = -8,
  CJ_E_UNSUPPORTED          = -9,
  CJ_E_ALREADY_EXISTS       = -10,
  CJ_E_NOT_FOUND            = -11,
  CJ_E_BUSY                 = -12,
} cj_result_t;

/** Convert a result to a human-readable const string. */
CJ_API const char* cj_result_str(cj_result_t r);

#ifdef __cplusplus
} /* extern "C" */
#endif
