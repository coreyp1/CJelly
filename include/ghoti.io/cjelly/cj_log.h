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

/** @file cj_log.h
 *  @brief Where the library's diagnostics go, and how much of them there is.
 *
 * A library that prints is a library that has decided something on its
 * embedder's behalf. CJelly used to make that decision 218 times, in
 * `printf` and `fprintf` calls scattered through nine files, so an
 * application had no way to quiet it, redirect it, or capture it - and the
 * two streams interleaved wrongly the moment output went to a file, because
 * stdout is block-buffered there and stderr is not.
 *
 * This is the single place that decision is now made.
 *
 * ### Two entry points, one rule
 *
 * The level answers *how important is this message*; it filters what the
 * library says **on its own initiative**. `cj_log_write` and the `CJ_LOG_*`
 * macros go through that filter.
 *
 * `cj_log_emit` does not. It is for a report the caller **explicitly asked
 * for** - the frame profiler, say, which runs only because
 * `enable_fps_profiling` was set. The caller has already decided they want
 * it, and a level check would let a second, unrelated setting silently
 * cancel the first.
 *
 * ### Default
 *
 * `CJ_LOG_ERROR`: the library is quiet unless something failed. Raise it
 * with `cj_log_set_level`, or from the environment before the first log
 * call:
 *
 * - `CJELLY_LOG=off|error|warn|info|debug|trace`, or a number 0-5.
 * - `CJELLY_DEBUG` set to anything non-empty means `debug`, which is what
 *   that variable meant before this header existed.
 *
 * An explicit `cj_log_set_level` always wins; the environment is read once,
 * on the first call that needs it.
 *
 * ### Threads
 *
 * The level and the sink are plain statics. Set them before the threads
 * start. Writing a message from two threads at once interleaves like any
 * other `fwrite`.
 */

#ifndef GHOTI_IO_CJ_LOG_H
#define GHOTI_IO_CJ_LOG_H

#include <ghoti.io/cjelly/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How much the library says. Ordered, so a level admits every level below
 *  it: at CJ_LOG_INFO a warning still prints, a debug message does not. */
typedef enum cj_log_level_t {
  CJ_LOG_OFF   = 0, /**< Nothing at all, not even errors. */
  CJ_LOG_ERROR = 1, /**< Something failed. The default. */
  CJ_LOG_WARN  = 2, /**< Something is off, but the call carried on. */
  CJ_LOG_INFO  = 3, /**< What the library decided: a device, a format, a fallback. */
  CJ_LOG_DEBUG = 4, /**< Step-by-step progress through a setup path. */
  CJ_LOG_TRACE = 5, /**< Per-frame and per-object detail. */
} cj_log_level_t;

/** Receives one finished message.
 *
 * @param level    The level it was written at.
 * @param message  The formatted text, NUL-terminated, with no trailing
 *                 newline and no level prefix - add whatever framing the
 *                 destination wants.
 * @param user_data The pointer handed to cj_log_set_sink.
 */
typedef void (*cj_log_fn)(cj_log_level_t level, const char* message,
                          void* user_data);

/** Set the level. Anything above it is dropped before formatting. */
CJ_API void cj_log_set_level(cj_log_level_t level);

/** The level in force, resolving the environment if nothing has set one. */
CJ_API cj_log_level_t cj_log_get_level(void);

/** Route messages somewhere other than stderr.
 *
 * @param fn        The sink, or NULL to go back to the built-in one, which
 *                  writes "cjelly: LEVEL: message\n" to stderr and flushes.
 * @param user_data Passed to every call of @p fn. Not copied or freed.
 */
CJ_API void cj_log_set_sink(cj_log_fn fn, void* user_data);

/** Write a message if @p level passes the filter. */
CJ_API CJ_PRINTF_FORMAT(2, 3) void cj_log_write(cj_log_level_t level,
                                                const char* fmt, ...);

/** Write a message whatever the level is.
 *
 * For output the caller turned on by name. See the file comment. */
CJ_API CJ_PRINTF_FORMAT(2, 3) void cj_log_emit(cj_log_level_t level,
                                               const char* fmt, ...);

/** "ERROR", "WARN", ... for a known level; "?" for anything else. */
CJ_API const char* cj_log_level_str(cj_log_level_t level);

/** @name Level shorthands
 *
 * Each tests the level before evaluating its arguments, so a filtered-out
 * message costs a comparison and does not format its operands.
 * @{
 */
#define CJ_LOG_AT(level, ...)                                                  \
  do {                                                                         \
    if ((level) <= cj_log_get_level()) cj_log_write((level), __VA_ARGS__);      \
  } while (0)

#define CJ_ERRORF(...) CJ_LOG_AT(CJ_LOG_ERROR, __VA_ARGS__)
#define CJ_WARNF(...)  CJ_LOG_AT(CJ_LOG_WARN, __VA_ARGS__)
#define CJ_INFOF(...)  CJ_LOG_AT(CJ_LOG_INFO, __VA_ARGS__)
#define CJ_DEBUGF(...) CJ_LOG_AT(CJ_LOG_DEBUG, __VA_ARGS__)
#define CJ_TRACEF(...) CJ_LOG_AT(CJ_LOG_TRACE, __VA_ARGS__)
/** @} */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // GHOTI_IO_CJ_LOG_H
