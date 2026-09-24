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
#include <vulkan/vulkan.h>

/* The vertex layout the colour pipeline reads.
 *
 * Three places fill a buffer that pipeline draws from - the engine creates
 * one, the render graph's colour node makes its own, and
 * cj_bindless_update_split_from_colorMul() rewrites it in place - and the
 * stride the pipeline was built with has to match every one of them. Each
 * declared the layout for itself, and all four copies carried a textureID
 * field that color.vert does not read. Correcting three of them left the
 * fourth writing 24-byte vertices into a buffer allocated at 20, which
 * vkMapMemory reported as overstepping the allocation. So: one declaration,
 * because a layout agreed on by copy is one that only stays agreed until
 * somebody is right about it.
 */
typedef struct CJellyColorVertex {
  float pos[2];
  float color[3];
} CJellyColorVertex;

/* Forward declaration for atlas used by bindless resources */
typedef struct CJellyTextureAtlas CJellyTextureAtlas;

/* Internal-only layout for bindless resources (opaque publicly) */
typedef struct CJellyBindlessResources {
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  CJellyTextureAtlas* textureAtlas;
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
  float uv[4];
  float colorMul[4];
} CJellyBindlessResources;


