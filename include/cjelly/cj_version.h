/*
 * CJelly — Minimal C API stubs
 * Copyright (c) 2025
 *
 * This is a design-time stub for headers. Implementation is TBD.
 * Licensed under the MIT license for prototype purposes.
 */
#pragma once
#include <stdint.h>
#include "cj_macros.h"

/** @file cj_version.h
 *  @brief CJelly semantic version helpers.
 */

#define CJ_VERSION_MAJOR 0
#define CJ_VERSION_MINOR 1
#define CJ_VERSION_PATCH 0

/** Pack version into a 32-bit integer: MMmmpp (8 bits each). */
#define CJ_MAKE_VERSION(M,m,p) (((uint32_t)(M) << 24) | ((uint32_t)(m) << 16) | ((uint32_t)(p) << 8))

/** Current CJelly header version. */
#define CJ_HEADER_VERSION CJ_MAKE_VERSION(CJ_VERSION_MAJOR, CJ_VERSION_MINOR, CJ_VERSION_PATCH)

/** Return the runtime version (implementation). */
CJ_API uint32_t cj_version_runtime(void);
