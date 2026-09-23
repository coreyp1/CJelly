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

/** @file log_internal.h
 *  @brief The parts of the logger the test suite needs and callers do not.
 *
 * Not installed-facing API: these exist so that the two things hardest to
 * test through cj_log.h alone - how a level name is parsed, and whether the
 * environment is consulted at all - can be tested directly rather than
 * inferred from a message appearing.
 */

#ifndef GHOTI_IO_CJ_LOG_INTERNAL_H
#define GHOTI_IO_CJ_LOG_INTERNAL_H

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_log.h>

#include <stdbool.h>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Turn a level name or a digit into a level.
 *
 * Pure: it reads no environment and no global state, which is what makes it
 * worth testing on its own.
 *
 * @param text A level name ("warn", "debug", ...) or a single digit 0-5.
 *             NULL and "" are not levels.
 * @param out  Written only when @p text names a level.
 * @return true if it did.
 */
bool cj_log__parse_level(const char * text, cj_log_level_t * out);

/** The level a Vulkan validation message of this severity is worth.
 *
 * Separate from the callback so that it can be tested: the severity is a
 * BITMASK, a message may carry more than one bit, and the order the bits are
 * tested in decides the answer. Reaching this through a real validation
 * failure would mean provoking one of each severity from a live driver.
 */
cj_log_level_t cj_log__level_for_vk_severity(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity);

/** Put the level back to "nobody has chosen one, and the environment has not
 *  been read yet", so that a test can set an environment variable and see
 *  whether it is honoured.
 *
 * Nothing but the test suite has a reason to call this: in a real program
 * the environment is read once and the answer does not change. */
void cj_log__reset_for_test(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // GHOTI_IO_CJ_LOG_INTERNAL_H
