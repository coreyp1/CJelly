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

/** @file cj_version.h
 *  @brief CJelly semantic version helpers.
 */

/*
 * CJ_VERSION_MAJOR / _MINOR / _PATCH, CJ_MAKE_VERSION and CJ_VERSION_NUMBER
 * come from libver.h, which takes the numbers from the generated libver_gen.h.
 *
 * They used to be written out here as 0.1.0 with a packing of its own
 * (M<<24 | m<<16 | p<<8, which left the bottom byte permanently empty). That
 * disagreed with the Makefile's 0.0.0 in every other place the version
 * appears - the soname, the .pc Version:, the install directory - and with
 * what this library reports to Vulkan. The suite now uses one packing
 * everywhere, libcurl's one-byte-per-component layout.
 */

/** Current CJelly header version. Kept as the documented spelling. */
#define CJ_HEADER_VERSION CJ_VERSION_NUMBER

/** Return the runtime version (implementation). */
CJ_API uint32_t cj_version_runtime(void);
