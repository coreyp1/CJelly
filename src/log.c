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

/** @file log.c
 *  @brief The one place the library writes to a stream.
 *
 * Every other translation unit reaches stdout and stderr through here, which
 * `make check-quiet` enforces. See cj_log.h for what the levels mean.
 */

#include <ghoti.io/cjelly/macros.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cjelly/cj_log.h>
#include <ghoti.io/cjelly/log_internal.h>

/* One past CJ_LOG_TRACE, so it cannot collide with a real level: "nobody has
 * chosen a level, and the environment has not been consulted yet". The
 * environment is read on the first call that needs an answer rather than in a
 * constructor, so that a cj_log_set_level before the first message is not
 * racing anything. */
#define CJ_LOG_UNRESOLVED ((cj_log_level_t)(CJ_LOG_TRACE + 1))

static cj_log_level_t cj_log__level = CJ_LOG_UNRESOLVED;
static cj_log_fn cj_log__sink = NULL;
static void * cj_log__sink_user = NULL;

/* Longer than almost every message, so the heap path below is rare. Vulkan
 * validation messages are the exception and they are the ones worth not
 * truncating, which is why there is a heap path at all. */
#define CJ_LOG_STACK_BUFFER 1024

/**
 * @brief Where messages go when nobody has set a sink.
 *
 * stderr rather than stdout, for all levels. The two streams buffer
 * differently once output is redirected - stdout by block, stderr not at all
 * - so a library that used both put its own messages out of order in every
 * log file. One stream cannot do that to itself.
 */
static void cj_log__default_sink(cj_log_level_t level, const char * message,
    CJ_MAYBE_UNUSED(void * user_data)) {
  fprintf(stderr, "cjelly: %s: %s\n", cj_log_level_str(level), message);
  fflush(stderr);
}

bool cj_log__parse_level(const char * text, cj_log_level_t * out) {
  static const struct {
    const char * name;
    cj_log_level_t level;
  } names[] = {
    {"off", CJ_LOG_OFF},
    {"error", CJ_LOG_ERROR},
    {"warn", CJ_LOG_WARN},
    {"warning", CJ_LOG_WARN},
    {"info", CJ_LOG_INFO},
    {"debug", CJ_LOG_DEBUG},
    {"trace", CJ_LOG_TRACE},
  };

  if (!text || !*text) {
    return false;
  }
  for (size_t i = 0; i < CJ_ARRAY_SIZE(names); ++i) {
    if (strcmp(text, names[i].name) == 0) {
      *out = names[i].level;
      return true;
    }
  }
  /* A bare digit, so that a script can step the level without knowing the
   * spelling. Anything else is refused rather than diagnosed: complaining
   * about the logging configuration would need the logger. */
  if (text[0] >= '0' && text[0] <= '5' && text[1] == '\0') {
    *out = (cj_log_level_t)(text[0] - '0');
    return true;
  }
  return false;
}

/**
 * @brief Read CJELLY_LOG, then CJELLY_DEBUG, into a level.
 *
 * @param out Set only when the environment actually names a level.
 * @return true if it did.
 */
static bool cj_log__level_from_env(cj_log_level_t * out) {
  if (cj_log__parse_level(getenv("CJELLY_LOG"), out)) {
    return true;
  }

  /* What CJELLY_DEBUG meant before this file existed: 21 sites in cjelly.c
   * called getenv on it, once per message, and printed at what is now
   * DEBUG. Any non-empty value counts, because that is what those sites
   * tested - they never looked at what it was set to. */
  const char * debug = getenv("CJELLY_DEBUG");
  if (debug && *debug) {
    *out = CJ_LOG_DEBUG;
    return true;
  }

  return false;
}

void cj_log__reset_for_test(void) {
  cj_log__level = CJ_LOG_UNRESOLVED;
}

CJ_API cj_log_level_t cj_log_get_level(void) {
  if (cj_log__level == CJ_LOG_UNRESOLVED) {
    cj_log_level_t from_env;
    cj_log__level = cj_log__level_from_env(&from_env) ? from_env : CJ_LOG_ERROR;
  }
  return cj_log__level;
}

CJ_API void cj_log_set_level(cj_log_level_t level) {
  cj_log__level = level;
}

CJ_API void cj_log_set_sink(cj_log_fn fn, void * user_data) {
  cj_log__sink = fn;
  cj_log__sink_user = user_data;
}

CJ_API const char * cj_log_level_str(cj_log_level_t level) {
  switch (level) {
    case CJ_LOG_OFF: return "OFF";
    case CJ_LOG_ERROR: return "ERROR";
    case CJ_LOG_WARN: return "WARN";
    case CJ_LOG_INFO: return "INFO";
    case CJ_LOG_DEBUG: return "DEBUG";
    case CJ_LOG_TRACE: return "TRACE";
    default: return "?";
  }
}

/**
 * @brief Format the message and hand it to the sink.
 *
 * Takes the va_list so that the two public entry points differ only in
 * whether they consulted the level first.
 */
static void cj_log__dispatch(cj_log_level_t level, const char * fmt,
    va_list args) {
  char stack_buffer[CJ_LOG_STACK_BUFFER];
  char * message = stack_buffer;
  char * heap = NULL;

  /* vsnprintf consumes args, and a second pass needs a fresh copy. */
  va_list retry;
  va_copy(retry, args);
  int needed = vsnprintf(stack_buffer, sizeof(stack_buffer), fmt, args);

  if (needed >= (int)sizeof(stack_buffer)) {
    /* Not the library allocator on purpose: that one can fail loudly, and a
     * logger whose failure path logs does not have a failure path. A failed
     * malloc here just means the stack buffer's truncated text is what gets
     * reported, which is better than nothing and better than recursing. */
    heap = (char *)malloc((size_t)needed + 1);
    if (heap) {
      vsnprintf(heap, (size_t)needed + 1, fmt, retry);
      message = heap;
    }
  }
  va_end(retry);

  if (needed < 0) {
    /* The format itself was rejected. Say so rather than printing whatever
     * the buffer happened to hold. */
    message = (char *)"<message could not be formatted>";
  }

  if (cj_log__sink) {
    cj_log__sink(level, message, cj_log__sink_user);
  }
  else {
    cj_log__default_sink(level, message, NULL);
  }

  free(heap);
}

CJ_API void cj_log_write(cj_log_level_t level, const char * fmt, ...) {
  /* Checked again here, not only in the CJ_LOG_AT macro: a caller may reach
   * this function directly, and the macro's check exists to skip formatting
   * rather than to be the only gate. */
  if (level > cj_log_get_level()) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  cj_log__dispatch(level, fmt, args);
  va_end(args);
}

CJ_API void cj_log_emit(cj_log_level_t level, const char * fmt, ...) {
  va_list args;
  va_start(args, fmt);
  cj_log__dispatch(level, fmt, args);
  va_end(args);
}
