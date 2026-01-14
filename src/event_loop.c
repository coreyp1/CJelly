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

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

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

/* Run a single iteration. Returns false when loop should stop. */
CJ_API bool cj_run_once(cj_engine_t* engine) {
  (void)engine;
  CJellyApplication* app = cjelly_application_get_current();
  if (!app) return false;
  if (g_cj_run_stop_requested) return false;
  if (cjelly_application_should_shutdown(app)) return false;

  /* Process events first (can close windows). */
  cj_poll_events();

  if (g_cj_run_stop_requested) return false;
  if (cjelly_application_should_shutdown(app)) return false;

  uint32_t count = cjelly_application_window_count(app);
  if (count == 0) return false;

  void** windows = (void**)malloc(sizeof(void*) * count);
  if (!windows) return false;
  uint32_t actual = cjelly_application_get_windows(app, windows, count);

  for (uint32_t i = 0; i < actual; i++) {
    cj_window_t* win = (cj_window_t*)windows[i];
    if (!win) continue;

    cj_frame_info_t frame = (cj_frame_info_t){0};
    if (cj_window_begin_frame(win, &frame) != CJ_SUCCESS) {
      continue;
    }

    cj_frame_result_t result = cj_window__dispatch_frame_callback(win, &frame);

    switch (result) {
      case CJ_FRAME_CONTINUE:
        cj_window_execute(win);
        cj_window_present(win);
        break;
      case CJ_FRAME_SKIP:
        break;
      case CJ_FRAME_CLOSE_WINDOW:
        cj_window_destroy(win);
        break;
      case CJ_FRAME_STOP_LOOP:
        g_cj_run_stop_requested = 1;
        break;
      default:
        /* Unknown: default to continue */
        cj_window_execute(win);
        cj_window_present(win);
        break;
    }

    if (g_cj_run_stop_requested) break;
  }

  free(windows);

  /* Continue if windows still exist and we haven't been asked to stop. */
  return (cjelly_application_window_count(app) > 0) &&
         !cjelly_application_should_shutdown(app) &&
         !g_cj_run_stop_requested;
}

CJ_API void cj_run(cj_engine_t* engine) {
  cj_run_with_config(engine, NULL);
}

CJ_API void cj_run_with_config(cj_engine_t* engine, const cj_run_config_t* config) {
  (void)engine;

  g_cj_run_stop_requested = 0;

  uint32_t target_fps = 0;
  if (config) {
    target_fps = config->target_fps;
  }

  uint32_t frame_ms = 0;
  if (target_fps > 0) {
    frame_ms = 1000u / target_fps;
  }

  /* Simple loop: run until cj_run_once says stop. */
  while (cj_run_once(engine)) {
    if (frame_ms > 0) {
      cj_sleep_ms(frame_ms);
    }
  }
}

