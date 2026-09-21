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
#include <ghoti.io/cjelly/cj_resources.h>

/* Forward declaration to avoid circular dependency */
typedef struct cj_engine_t cj_engine_t;

/* Vulkan resource creation helpers */
CJ_API int cj_engine_create_texture(cj_engine_t* e, uint32_t slot, const cj_texture_desc_t* desc);
CJ_API int cj_engine_create_buffer(cj_engine_t* e, uint32_t slot, const cj_buffer_desc_t* desc);
CJ_API int cj_engine_create_sampler(cj_engine_t* e, uint32_t slot, const cj_sampler_desc_t* desc);
CJ_API void cj_engine_destroy_texture(cj_engine_t* e, uint32_t slot);
CJ_API void cj_engine_destroy_buffer(cj_engine_t* e, uint32_t slot);
CJ_API void cj_engine_destroy_sampler(cj_engine_t* e, uint32_t slot);
