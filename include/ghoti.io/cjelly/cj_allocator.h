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
 * @file cj_allocator.h
 * @brief Host memory allocation for CJelly.
 *
 * This is cutil's @ref GCU_Allocator under a local name, which is what every
 * other library in the suite does.  One definition across the suite means an
 * allocator written for any of them works with all of them.
 *
 * CJelly previously declared a vtable of its own here - two functions, an
 * alignment argument, and a `user` context - and never read it.  Nothing
 * implemented it and nothing called it, so the engine descriptor's
 * `allocator` field was decorative: a caller could supply one and every
 * allocation still went to malloc().  Replacing it costs nothing that was
 * working and gains the suite's shared definition.
 *
 * The alignment argument went with it.  Nothing host-side in CJelly needs
 * more than `max_align_t`, which every conforming malloc already provides;
 * device memory is Vulkan's to align and never passes through here.  An
 * argument no caller could act on and no implementation honoured is worse
 * than its absence, because it reads as a promise.
 */

#ifndef GHOTI_IO_CJ_ALLOCATOR_H
#define GHOTI_IO_CJ_ALLOCATOR_H

#include <ghoti.io/cutil/allocator.h>

#include <ghoti.io/cjelly/macros.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief The allocator interface CJelly allocates host memory through.
 *
 * All four function pointers must be supplied.  Each receives the struct's
 * `ctx` pointer as its first argument, so one implementation can serve
 * several independent pools.
 *
 * Two requirements beyond the C library equivalents: `calloc_fn` must treat
 * overflow of `nitems * size` as a failure and return NULL rather than
 * allocating a truncated block, and a zero-size request should return a
 * usable non-NULL pointer, so that NULL always means failure.
 *
 * A NULL `cj_allocator_t *` anywhere in this library means
 * ::cj_allocator_default().
 */
typedef GCU_Allocator cj_allocator_t;

/**
 * @brief The default, stdlib-backed allocator.
 *
 * @return A process-global instance.  It is never freed.
 */
CJ_API const cj_allocator_t * cj_allocator_default(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* GHOTI_IO_CJ_ALLOCATOR_H */
