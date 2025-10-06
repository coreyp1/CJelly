#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <cjelly/application.h>

// #include <cjelly/format/3d/obj.h>
// #include <cjelly/format/3d/mtl.h>
#include <cjelly/format/image.h>
#include <cjelly/format/image/bmp.h>


#ifdef _WIN32
#include <stdint.h>
#include <windows.h>
uint64_t getCurrentTimeInMilliseconds(void) {
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return (uint64_t)((counter.QuadPart * 1000LL) / frequency.QuadPart);
}
#else
#include <stdint.h>
#include <time.h>
// Local extern for legacy X11 display pointer used by cjelly internals
extern Display * display;
uint64_t getCurrentTimeInMilliseconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}
#endif

#include <cjelly/cjelly.h>
#include <cjelly/runtime.h>
#include <cjelly/engine_internal.h>
#include <cjelly/cj_window.h>

/* Demo uses window API now; per-window rendering is managed internally */

int main(void) {
#ifndef _WIN32
  fprintf(stderr, "Starting CJelly demo...\n");
#endif
#ifdef _WIN32
  // Windows: hInstance is set in createPlatformWindow.
#else
  // Linux: Open X display.
  fprintf(stderr, "Opening X display...\n");
  display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "Failed to open X display\n");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, "X display opened successfully\n");
#endif

  // Create engine (will be bound to legacy globals after init)
  cj_engine_desc_t eng_desc = {0};
  cj_engine_t* engine = cj_engine_create(&eng_desc);

  // Initialize Vulkan via context API
  fprintf(stderr, "Initializing Vulkan...\n");
  CJellyVulkanContext ctx = {0};
  if (!cjelly_init_context(&ctx, 1)) {
    fprintf(stderr, "Failed to initialize CJelly Vulkan context\n");
    return EXIT_FAILURE;
  }
  fprintf(stderr, "Vulkan initialized.\n");

  // Import context into engine and set current
  cj_engine_import_context(engine, &ctx);
  cj_engine_set_current(engine);

  // Create two windows via new API (now that Vulkan is ready)
  cj_window_desc_t wdesc1 = {0};
  wdesc1.width = 800;
  wdesc1.height = 600;
  cj_window_desc_t wdesc2 = wdesc1;
  cj_window_t* win1 = cj_window_create(engine, &wdesc1);
  cj_window_t* win2 = cj_window_create(engine, &wdesc2);

  // Re-record window 1 with bindless color-only (square) so it doesn't render the fish
  // Build minimal color-only bindless resources (no descriptor set required)
  static CJellyBindlessResources* colorOnly = NULL;
  CJellyVulkanContext ctx_local = ctx;
  colorOnly = cjelly_create_bindless_color_square_resources_ctx(&ctx_local);
  if (colorOnly) {
    cj_window_rerecord_bindless_color(win1, (const void*)colorOnly, &ctx_local);
  }

  // Main render loop.
  cj_window_t* windows[] = {win1, win2};
  while (!cj_should_close()) {
    cj_poll_events();
    uint64_t currentTime = getCurrentTimeInMilliseconds();
    // Toggle color each second for window 1 by updating push constants in recorded commands
    if (colorOnly) {
      int colorIndex = ((currentTime / 1000) % 2);
      float r = (colorIndex == 0) ? 1.0f : 0.0f;
      float g = (colorIndex == 0) ? 0.0f : 1.0f;
      cj_bindless_set_color(colorOnly, r, g, 0.0f, 1.0f);
      cj_bindless_update_split_from_colorMul(colorOnly);
      cj_window_rerecord_bindless_color(win1, (const void*)colorOnly, &ctx_local);
    }
    for (int i = 0; i < 2; ++i) {
      cj_frame_info_t frame = {0};
      if (cj_window_begin_frame(windows[i], &frame) == CJ_SUCCESS) {
        cj_window_execute(windows[i]);
        cj_window_present(windows[i]);
      }
    }

#ifdef _WIN32
    Sleep(1);
#else
    struct timespec req = {0, 1000000}; // 1 ms
    nanosleep(&req, NULL);
#endif
  }
  // Destroy windows via new API (frees per-window resources and command buffers)
  cj_window_destroy(win1);
  cj_window_destroy(win2);

  // Cleanup colorOnly resources if allocated (must be before device/context destroy)
  if (colorOnly) {
    cjelly_destroy_bindless_resources(colorOnly);
    colorOnly = NULL;
  }

  // Wait for GPU idle and clean up global/context resources last
  vkDeviceWaitIdle(ctx.device);

  // Clean up global Vulkan resources.
  cjelly_destroy_context(&ctx);

#ifndef _WIN32
  XCloseDisplay(display);
#endif
  return 0;
}
