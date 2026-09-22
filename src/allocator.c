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
 * @file
 * The default allocator, which is cutil's.
 *
 * A seam rather than an implementation: it exists so that callers of this
 * library have a name in this library's own namespace to ask for, and so
 * that a NULL allocator has something to resolve to.
 */

#include <ghoti.io/cjelly/cj_allocator.h>

CJ_API const cj_allocator_t * cj_allocator_default(void) {
  return gcu_allocator_default();
}
