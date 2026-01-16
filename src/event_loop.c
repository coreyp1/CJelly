/* CJelly callback-based event loop implementation */

#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#endif

#include <cjelly/runtime.h>
#include <cjelly/cj_window.h>
#include <cjelly/application.h>
#include <cjelly/window_internal.h>
#include <cjelly/macros.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/* High-resolution timer helpers - returns microseconds for better precision */
static uint64_t cj_get_time_us(void) {
#ifdef _WIN32
  LARGE_INTEGER frequency, counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return (uint64_t)((counter.QuadPart * 1000000ULL) / frequency.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
#endif
}

/* Convenience wrapper for milliseconds (for compatibility) */
static uint64_t cj_get_time_ms(void) {
  return cj_get_time_us() / 1000ULL;
}

/* Global stop flag for now (process-wide). */
static volatile int g_cj_run_stop_requested = 0;

static void cj_sleep_ms(uint32_t ms) {
  if (ms == 0) return;
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  struct timespec req;
  req.tv_sec = (time_t)(ms / 1000u);
  req.tv_nsec = (long)((ms % 1000u) * 1000000u);
  nanosleep(&req, NULL);
#endif
}

CJ_API void cj_request_stop(cj_engine_t* engine) {
  (void)engine;
  g_cj_run_stop_requested = 1;
}

/* Profiling structure for detailed timing */
typedef struct {
  uint64_t event_poll_us;
  uint64_t window_list_us;
  uint64_t minimized_check_us;
  uint64_t begin_frame_us;
  uint64_t callback_us;
  uint64_t execute_us;
  uint64_t present_us;
  uint64_t vsync_check_us;
  uint64_t sleep_us;
  uint64_t other_us;  /* Unaccounted time (should be VSync wait) */
  uint32_t window_count;
} cj_frame_profile_t;

/* Internal version with flags. */
static bool cj_run_once_with_flags(cj_engine_t* engine, bool run_when_minimized, cj_frame_profile_t* profile) {
  (void)engine;
  uint64_t t0, t1;

  CJellyApplication* app = cjelly_application_get_current();
  if (!app) return false;
  if (g_cj_run_stop_requested) return false;
  if (cjelly_application_should_shutdown(app)) return false;

  /* Process events first (can close windows). */
  t0 = cj_get_time_us();
  cj_poll_events();
  if (profile) profile->event_poll_us = cj_get_time_us() - t0;

  if (g_cj_run_stop_requested) return false;
  if (cjelly_application_should_shutdown(app)) return false;

  t0 = cj_get_time_us();
  uint32_t count = cjelly_application_window_count(app);
  if (count == 0) return false;

  /* Use stack allocation for small window counts to avoid malloc overhead. */
  void* windows_stack[8];
  void** windows = (count <= 8) ? windows_stack : (void**)malloc(sizeof(void*) * count);
  if (!windows) return false;
  uint32_t actual = cjelly_application_get_windows(app, windows, count);
  if (profile) {
    profile->window_list_us = cj_get_time_us() - t0;
    profile->window_count = actual;
  }

  /* Check if all windows are minimized (if run_when_minimized is false).
   * We still process events, but skip rendering if all are minimized.
   */
  t0 = cj_get_time_us();
  if (!run_when_minimized && actual > 0) {
    bool all_minimized = true;
    for (uint32_t i = 0; i < actual; i++) {
      cj_window_t* win = (cj_window_t*)windows[i];
      if (win && !cj_window__is_minimized(win)) {
        all_minimized = false;
        break;
      }
    }
    /* If all windows are minimized and we shouldn't run when minimized, stop. */
    if (all_minimized) {
      if (profile) profile->minimized_check_us = cj_get_time_us() - t0;
      if (windows != windows_stack) free(windows);
      return false;
    }
  }
  if (profile) profile->minimized_check_us = cj_get_time_us() - t0;

  /* Render each window. */
  uint64_t total_begin_frame = 0, total_callback = 0, total_execute = 0, total_present = 0;
  for (uint32_t i = 0; i < actual; i++) {
    cj_window_t* win = (cj_window_t*)windows[i];
    if (!win) continue;

    /* Skip minimized windows if run_when_minimized is false. */
    if (!run_when_minimized && cj_window__is_minimized(win)) {
      continue;
    }

    /* Check if we should call the frame callback.
     * For CJ_REDRAW_ON_EVENTS, we always call the callback so it can check time
     * and mark dirty if needed, even if the window isn't currently dirty.
     */
    bool should_call_callback = cj_window__should_call_callback(win);

    /* Check if we actually need to render (begin_frame/execute/present).
     * For CJ_REDRAW_ON_EVENTS, we call the callback but only render if dirty.
     * For windows without callbacks, we still need to render if dirty.
     */
    bool needs_render = cj_window__needs_redraw(win);

    /* If window needs to render but isn't dirty, it's a timer-based render (CJ_REDRAW_ALWAYS).
     * For CJ_REDRAW_ALWAYS, needs_render is always true, but if not dirty, it's a timer render. */
    if (needs_render && !cj_window__needs_redraw(win)) {
      /* This shouldn't happen - needs_render should only be true if needs_redraw is true.
       * But if it does, it means it's a CJ_REDRAW_ALWAYS window that's not dirty, so set TIMER. */
      cj_window__set_pending_render_reason(win, CJ_RENDER_REASON_TIMER);
    } else if (needs_render) {
      /* Window is dirty - check if reason is still TIMER (shouldn't be, but handle it) */
      cj_render_reason_t reason = cj_window__get_pending_render_reason(win);
      if (reason == CJ_RENDER_REASON_TIMER) {
        /* Window is dirty but reason is TIMER - this shouldn't happen normally,
         * but if it does, keep it as TIMER (will respect FPS limit) */
      }
    }

    /* Check per-window FPS limit if window wants to render.
     * Bypass FPS limit for forced renders (resize, expose, etc.) */
    uint64_t current_time_us = cj_get_time_us();
    if (needs_render) {
      cj_render_reason_t reason = cj_window__get_pending_render_reason(win);
      bool should_bypass_fps = cj_window__should_bypass_fps_limit(reason);

      if (!should_bypass_fps && !cj_window__can_render_at_fps(win, current_time_us)) {
        /* Window wants to render but FPS limit hasn't been reached yet (timer-based render) */
        needs_render = false;
      }
    }

    /* If window has no callback and doesn't need render, skip it */
    if (!should_call_callback && !needs_render) {
      continue;
    }

    /* If window has no callback but needs render, render it */
    if (!should_call_callback && needs_render) {
      /* No callback to call, just render directly */
      t0 = cj_get_time_us();
      cj_frame_info_t frame = {0};
      if (cj_window_begin_frame(win, &frame) == CJ_SUCCESS) {
        t1 = cj_get_time_us();
        total_begin_frame += (t1 - t0);

        t0 = cj_get_time_us();
        cj_window_execute(win);
        t1 = cj_get_time_us();
        total_execute += (t1 - t0);

        t0 = cj_get_time_us();
        cj_window_present(win);
        t1 = cj_get_time_us();
        total_present += (t1 - t0);

        /* Update last render time for FPS limiting */
        cj_window__update_last_render_time(win, cj_get_time_us());

        /* Clear dirty flag after successful render */
        if (cj_window__should_clear_dirty_after_render(win)) {
          cj_window_clear_dirty(win);
        }
      }
      continue;
    }

    t0 = cj_get_time_us();
    cj_frame_info_t frame = (cj_frame_info_t){0};
    cj_result_t begin_result = CJ_SUCCESS;
    if (needs_render) {
      begin_result = cj_window_begin_frame(win, &frame);
      if (begin_result != CJ_SUCCESS) {
        /* If begin_frame fails, still call callback but skip rendering */
        needs_render = false;  /* Don't render if begin_frame failed */
      }
    }
    t1 = cj_get_time_us();
    if (needs_render) {
      total_begin_frame += (t1 - t0);
    }

    t0 = cj_get_time_us();
    cj_frame_result_t result = cj_window__dispatch_frame_callback(win, &frame);
    t1 = cj_get_time_us();
    total_callback += (t1 - t0);

    /* Check again if we need to render - callback may have marked window dirty */
    bool needs_render_after_callback = cj_window__needs_redraw(win);

    /* Check per-window FPS limit again (callback may have marked dirty) */
    if (needs_render_after_callback && !cj_window__can_render_at_fps(win, current_time_us)) {
      needs_render_after_callback = false;
    }

    /* If callback marked window dirty and we haven't begun frame yet, do it now */
    if (needs_render_after_callback && !needs_render) {
      t0 = cj_get_time_us();
      begin_result = cj_window_begin_frame(win, &frame);
      t1 = cj_get_time_us();
      if (begin_result == CJ_SUCCESS) {
        total_begin_frame += (t1 - t0);
        needs_render = true;
      } else {
        needs_render = false;
      }
    }

    /* Update needs_render to reflect current state */
    needs_render = needs_render_after_callback && (begin_result == CJ_SUCCESS);

    switch (result) {
      case CJ_FRAME_CONTINUE:
        if (needs_render) {
          t0 = cj_get_time_us();
          cj_window_execute(win);
          t1 = cj_get_time_us();
          total_execute += (t1 - t0);

          t0 = cj_get_time_us();
          cj_window_present(win);
          t1 = cj_get_time_us();
          total_present += (t1 - t0);

          /* Update last render time for FPS limiting */
          cj_window__update_last_render_time(win, cj_get_time_us());

          /* Clear dirty flag after successful frame render (if policy requires it) */
          if (cj_window__should_clear_dirty_after_render(win)) {
            cj_window_clear_dirty(win);
          }
        } else {
          /* Callback was called but window wasn't dirty - clear dirty flag if callback didn't mark it */
          if (cj_window__should_clear_dirty_after_render(win) && !cj_window__needs_redraw(win)) {
            /* Window is clean, nothing to do */
          }
        }
        break;
      case CJ_FRAME_SKIP:
        /* Clear dirty flag when frame is skipped (optional optimization) */
        if (cj_window__should_clear_dirty_after_render(win)) {
          cj_window_clear_dirty(win);
        }
        break;
      case CJ_FRAME_CLOSE_WINDOW:
        cj_window_destroy(win);
        break;
      case CJ_FRAME_STOP_LOOP:
        g_cj_run_stop_requested = 1;
        break;
      default:
        /* Unknown: default to continue */
        if (needs_render) {
          t0 = cj_get_time_us();
          cj_window_execute(win);
          t1 = cj_get_time_us();
          total_execute += (t1 - t0);

          t0 = cj_get_time_us();
          cj_window_present(win);
          t1 = cj_get_time_us();
          total_present += (t1 - t0);

          /* Update last render time for FPS limiting */
          cj_window__update_last_render_time(win, cj_get_time_us());

          /* Clear dirty flag after successful frame render (if policy requires it) */
          if (cj_window__should_clear_dirty_after_render(win)) {
            cj_window_clear_dirty(win);
          }
        }
        break;
    }

    if (g_cj_run_stop_requested) break;
  }

  if (profile) {
    profile->begin_frame_us = total_begin_frame;
    profile->callback_us = total_callback;
    profile->execute_us = total_execute;
    profile->present_us = total_present;
    profile->vsync_check_us = 0;  /* Set in main loop */
    profile->sleep_us = 0;  /* Set in main loop */
    profile->other_us = 0;  /* Will be calculated in main loop */
  }

  if (windows != windows_stack) free(windows);

  /* Continue if windows still exist and we haven't been asked to stop. */
  return (cjelly_application_window_count(app) > 0) &&
         !cjelly_application_should_shutdown(app) &&
         !g_cj_run_stop_requested;
}

/* Run a single iteration. Returns false when loop should stop. */
CJ_API bool cj_run_once(cj_engine_t* engine) {
  (void)engine;
  return cj_run_once_with_flags(engine, false, NULL);
}

CJ_API void cj_run(cj_engine_t* engine) {
  cj_run_with_config(engine, NULL);
}

CJ_API void cj_run_with_config(cj_engine_t* engine, const cj_run_config_t* config) {
  (void)engine;

  g_cj_run_stop_requested = 0;

  /* Extract config values with defaults. */
  uint32_t target_fps = 0;
  bool run_when_minimized = false;
  bool enable_fps_profiling = false;

  if (config) {
    target_fps = config->target_fps;
    /* Note: config->vsync is reserved for future use when we support different present modes */
    (void)config->vsync;  /* Suppress unused warning */
    run_when_minimized = config->run_when_minimized;
    enable_fps_profiling = config->enable_fps_profiling;
  }

  /* Calculate target frame time in milliseconds. */
  uint32_t target_frame_ms = 0;
  if (target_fps > 0) {
    target_frame_ms = 1000u / target_fps;
  }

  /* Track frame timing for accurate pacing (using microsecond precision). */

  /* FPS profiling state (if enabled). */
  uint64_t fps_start_time_us = cj_get_time_us();
  uint64_t fps_last_print_time = cj_get_time_ms();
  uint32_t fps_frame_count = 0;
  double fps_min_frame_time_us = 1e9;
  double fps_max_frame_time_us = 0;
  double fps_total_frame_time_us = 0;

  /* Detailed profiling accumulators */
  double total_event_poll_us = 0;
  double total_window_list_us = 0;
  double total_minimized_check_us = 0;
  double total_begin_frame_us = 0;
  double total_callback_us = 0;
  double total_execute_us = 0;
  double total_present_us = 0;
  double total_vsync_check_us = 0;
  double total_sleep_us = 0;
  double total_other_us = 0;

  /* VSync is always active (FIFO mode), but we still respect target_fps for lower limits. */

  /* Main loop: run until cj_run_once says stop. */
  cj_frame_profile_t profile = {0};
  uint64_t frame_start_us = cj_get_time_us();
  uint64_t vsync_check_start_us = 0;
  uint64_t sleep_start_us = 0;

  while (cj_run_once_with_flags(engine, run_when_minimized, enable_fps_profiling ? &profile : NULL)) {
    uint64_t loop_end_us = cj_get_time_us();
    double frame_duration_us = (double)(loop_end_us - frame_start_us);

    /* Frame timing: respect target FPS if set.
     * Note: VSync (FIFO mode) limits to 60 FPS max, but we can still limit to lower FPS.
     */
    uint64_t vsync_check_us = 0;
    uint64_t sleep_us = 0;
    if (target_frame_ms > 0) {
      /* VSync is always active (FIFO mode), but we still respect target_fps.
       * The config->vsync flag is for future use when we support different present modes.
       */
      if (enable_fps_profiling) {
        vsync_check_start_us = cj_get_time_us();
        vsync_check_us = cj_get_time_us() - vsync_check_start_us;
      }

      /* Sleep to respect target FPS if we finished early.
       * Use microsecond precision for accurate timing.
       * Even with VSync active, we can limit to lower FPS (e.g., 30 FPS).
       * VSync will prevent going above 60 FPS, but we can still sleep to hit lower targets.
       */
      uint64_t target_frame_us = (uint64_t)target_frame_ms * 1000ULL;
      if (frame_duration_us < (double)target_frame_us) {
        uint64_t sleep_us_needed = target_frame_us - (uint64_t)frame_duration_us;
        uint32_t sleep_ms = (uint32_t)(sleep_us_needed / 1000ULL);
        /* Sleep at least 1ms if we need any sleep (nanosleep has minimum granularity) */
        if (sleep_ms > 0 || (sleep_us_needed % 1000ULL) > 0) {
          if (sleep_ms == 0) sleep_ms = 1;  /* Minimum 1ms sleep */
          if (enable_fps_profiling) sleep_start_us = cj_get_time_us();
          cj_sleep_ms(sleep_ms);
          if (enable_fps_profiling) sleep_us = cj_get_time_us() - sleep_start_us;
        }
      }
    }

    /* Update frame start for next iteration (after all work is done) */
    frame_start_us = cj_get_time_us();

    /* FPS profiling (if enabled). */
    if (enable_fps_profiling) {
      /* Calculate unaccounted time (likely VSync wait in execute) */
      double accounted_us = (double)profile.event_poll_us +
                           (double)profile.window_list_us +
                           (double)profile.minimized_check_us +
                           (double)profile.begin_frame_us +
                           (double)profile.callback_us +
                           (double)profile.execute_us +
                           (double)profile.present_us +
                           vsync_check_us +
                           sleep_us;
      profile.other_us = (uint64_t)(frame_duration_us - accounted_us);
      if (profile.other_us > frame_duration_us) profile.other_us = 0;  /* Sanity check */

      fps_frame_count++;
      if (frame_duration_us < fps_min_frame_time_us) {
        fps_min_frame_time_us = frame_duration_us;
      }
      if (frame_duration_us > fps_max_frame_time_us) {
        fps_max_frame_time_us = frame_duration_us;
      }
      fps_total_frame_time_us += frame_duration_us;

      /* Accumulate detailed profiling data */
      total_event_poll_us += (double)profile.event_poll_us;
      total_window_list_us += (double)profile.window_list_us;
      total_minimized_check_us += (double)profile.minimized_check_us;
      total_begin_frame_us += (double)profile.begin_frame_us;
      total_callback_us += (double)profile.callback_us;
      total_execute_us += (double)profile.execute_us;
      total_present_us += (double)profile.present_us;
      total_vsync_check_us += (double)vsync_check_us;
      total_sleep_us += (double)sleep_us;
      total_other_us += (double)profile.other_us;

      /* Print statistics every second. */
      uint64_t current_time = cj_get_time_ms();
      if (current_time - fps_last_print_time >= 1000) {
        double elapsed_seconds = ((double)(cj_get_time_us() - fps_start_time_us)) / 1000000.0;
        double fps = (fps_frame_count > 0) ? (fps_frame_count / elapsed_seconds) : 0.0;
        double avg_frame_time_us = (fps_frame_count > 0) ?
          (fps_total_frame_time_us / fps_frame_count) : 0.0;

        printf("FPS: %.2f | Frame time: avg=%.3fms min=%.3fms max=%.3fms | Frames: %u\n",
               fps, avg_frame_time_us / 1000.0,
               fps_min_frame_time_us / 1000.0, fps_max_frame_time_us / 1000.0,
               fps_frame_count);

        /* Print detailed breakdown */
        if (fps_frame_count > 0) {
          printf("  Breakdown (avg per frame):\n");
          printf("    Event poll:     %.3fms\n", (total_event_poll_us / fps_frame_count) / 1000.0);
          printf("    Window list:    %.3fms\n", (total_window_list_us / fps_frame_count) / 1000.0);
          printf("    Minimized chk: %.3fms\n", (total_minimized_check_us / fps_frame_count) / 1000.0);
          printf("    Begin frame:   %.3fms\n", (total_begin_frame_us / fps_frame_count) / 1000.0);
          printf("    Callback:      %.3fms\n", (total_callback_us / fps_frame_count) / 1000.0);
          printf("    Execute:       %.3fms\n", (total_execute_us / fps_frame_count) / 1000.0);
          printf("    Present:       %.3fms\n", (total_present_us / fps_frame_count) / 1000.0);
          printf("    VSync check:   %.3fms\n", (total_vsync_check_us / fps_frame_count) / 1000.0);
          printf("    Sleep:         %.3fms\n", (total_sleep_us / fps_frame_count) / 1000.0);
          printf("    Other/VSync:   %.3fms (likely VSync wait in execute)\n", (total_other_us / fps_frame_count) / 1000.0);
          printf("    Windows:       %u\n", profile.window_count);
        }

        /* Reset profiling counters. */
        fps_last_print_time = current_time;
        fps_frame_count = 0;
        fps_start_time_us = cj_get_time_us();
        fps_min_frame_time_us = 1e9;
        fps_max_frame_time_us = 0;
        fps_total_frame_time_us = 0;
        total_event_poll_us = 0;
        total_window_list_us = 0;
        total_minimized_check_us = 0;
        total_begin_frame_us = 0;
        total_callback_us = 0;
        total_execute_us = 0;
        total_present_us = 0;
        total_vsync_check_us = 0;
        total_sleep_us = 0;
        total_other_us = 0;
      }
    }
  }
}

