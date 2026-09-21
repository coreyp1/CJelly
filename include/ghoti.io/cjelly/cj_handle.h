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

#pragma once

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_types.h>

typedef enum cj_handle_kind_t {
  CJ_HANDLE_TEX = 0,
  CJ_HANDLE_BUF = 1,
  CJ_HANDLE_SMP = 2
} cj_handle_kind_t;

/* Allocate/release/retain/query resource handles via the engine */
CJ_API cj_handle_t cj_handle_alloc(cj_engine_t* e, cj_handle_kind_t kind, uint32_t* out_slot);
CJ_API void        cj_handle_retain(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h);
CJ_API void        cj_handle_release(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h);
CJ_API uint32_t    cj_handle_slot(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h);


