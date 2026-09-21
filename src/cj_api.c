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

#include <stdint.h>
#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_result.h>
#include <ghoti.io/cjelly/cj_version.h>

CJ_API const char* cj_result_str(cj_result_t r) {
  switch (r) {
    case CJ_SUCCESS: return "CJ_SUCCESS";
    case CJ_E_UNKNOWN: return "CJ_E_UNKNOWN";
    case CJ_E_INVALID_ARGUMENT: return "CJ_E_INVALID_ARGUMENT";
    case CJ_E_OUT_OF_MEMORY: return "CJ_E_OUT_OF_MEMORY";
    case CJ_E_NOT_READY: return "CJ_E_NOT_READY";
    case CJ_E_TIMEOUT: return "CJ_E_TIMEOUT";
    case CJ_E_DEVICE_LOST: return "CJ_E_DEVICE_LOST";
    case CJ_E_SURFACE_LOST: return "CJ_E_SURFACE_LOST";
    case CJ_E_OUT_OF_DATE: return "CJ_E_OUT_OF_DATE";
    case CJ_E_UNSUPPORTED: return "CJ_E_UNSUPPORTED";
    case CJ_E_ALREADY_EXISTS: return "CJ_E_ALREADY_EXISTS";
    case CJ_E_NOT_FOUND: return "CJ_E_NOT_FOUND";
    case CJ_E_BUSY: return "CJ_E_BUSY";
    default: return "CJ_E_???";
  }
}

CJ_API uint32_t cj_version_runtime(void) {
  return CJ_HEADER_VERSION;
}
