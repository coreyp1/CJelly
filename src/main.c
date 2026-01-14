#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <cjelly/application.h>
#include <cjelly/macros.h>

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
#include <cjelly/cj_engine.h>
#include <cjelly/cj_window.h>
#include <cjelly/cj_rgraph.h>
#include <cjelly/engine_internal.h>
#include <cjelly/bindless_internal.h>

/* Demo uses window API now; per-window rendering is managed internally */

/* ---------------------------------------------------------------------- */
/* Callback-based event loop demo helpers                                  */
/* ---------------------------------------------------------------------- */

typedef struct Window1Context {
  CJellyBindlessResources* colorOnly;
  uint64_t last_tick_ms;
} Window1Context;

typedef struct Window2Context {
  cj_rgraph_t* graph2;
} Window2Context;

typedef struct Window3Context {
  cj_rgraph_t* graph3;
  uint64_t last_tick_ms;
} Window3Context;

static cj_frame_result_t window1_on_frame(GCJ_MAYBE_UNUSED(cj_window_t* window),
                                          GCJ_MAYBE_UNUSED(const cj_frame_info_t* frame),
                                          void* user_data) {
  Window1Context* ctx = (Window1Context*)user_data;
  if (!ctx || !ctx->colorOnly) return CJ_FRAME_CONTINUE;

  uint64_t now = getCurrentTimeInMilliseconds();
  if (now - ctx->last_tick_ms >= 50) {
    int colorIndex = (int)((now / 1000) % 2);
    float r = (colorIndex == 0) ? 1.0f : 0.0f;
    float g = (colorIndex == 0) ? 0.0f : 1.0f;
    cj_bindless_set_color(ctx->colorOnly, r, g, 0.0f, 1.0f);
    cj_bindless_update_split_from_colorMul(ctx->colorOnly);
    ctx->last_tick_ms = now;
  }
  return CJ_FRAME_CONTINUE;
}

static cj_frame_result_t window3_on_frame(GCJ_MAYBE_UNUSED(cj_window_t* window),
                                          GCJ_MAYBE_UNUSED(const cj_frame_info_t* frame),
                                          void* user_data) {
  Window3Context* ctx = (Window3Context*)user_data;
  if (!ctx || !ctx->graph3) return CJ_FRAME_CONTINUE;

  uint64_t now = getCurrentTimeInMilliseconds();
  if (now - ctx->last_tick_ms >= 50) {
    cj_str_t param_time = {"time_ms", 7};
    cj_str_t param_blur_intensity = {"blur_intensity", 12};
    cj_rgraph_set_i32(ctx->graph3, param_time, (int32_t)(now % 10000));

    float blur_intensity = 0.5f + 0.5f * sin(now * 0.001f);
    cj_rgraph_set_i32(ctx->graph3, param_blur_intensity,
                      (int32_t)(blur_intensity * 1000.0f));
    ctx->last_tick_ms = now;
  }
  return CJ_FRAME_CONTINUE;
}

/* Resize callbacks for all three windows */
static void window1_on_resize(cj_window_t* window, uint32_t new_width, uint32_t new_height, GCJ_MAYBE_UNUSED(void* user_data)) {
  printf("Window 1 resized to %ux%u\n", new_width, new_height);
  /* Note: Swapchain recreation would happen here in a full implementation.
   * For now, we just notify. The viewport will update automatically when
   * the swapchain is recreated (which would be triggered by VK_ERROR_OUT_OF_DATE
   * during present, or explicitly here). */
  (void)window;  /* Suppress unused warning */
}

static void window2_on_resize(cj_window_t* window, uint32_t new_width, uint32_t new_height, GCJ_MAYBE_UNUSED(void* user_data)) {
  printf("Window 2 resized to %ux%u\n", new_width, new_height);
  (void)window;  /* Suppress unused warning */
}

static void window3_on_resize(cj_window_t* window, uint32_t new_width, uint32_t new_height, GCJ_MAYBE_UNUSED(void* user_data)) {
  printf("Window 3 resized to %ux%u\n", new_width, new_height);
  (void)window;  /* Suppress unused warning */
}

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

  // Initialize Vulkan via Engine
  fprintf(stderr, "Initializing Vulkan...\n");
  const char* vkenv = getenv("CJELLY_VALIDATION");
  int use_validation = (vkenv && vkenv[0] == '1') ? 1 : 0;
  if (!cj_engine_init(engine, use_validation)) {
    fprintf(stderr, "Failed to initialize Vulkan via engine\n");
    return EXIT_FAILURE;
  }
  fprintf(stderr, "Vulkan initialized.\n");

  // Set current engine
  cj_engine_set_current(engine);

  // Create application object for window tracking
  CJellyApplication* app = NULL;
  cjelly_application_create(&app, "CJelly Test", 1);
  if (app) {
    cjelly_application_set_current(app);
  }

  // Create three windows via new API (now that Vulkan is ready)
  cj_window_desc_t wdesc1 = {0};
  wdesc1.title.ptr = "CJelly Window 1 (Color Graph)";
  wdesc1.title.len = strlen(wdesc1.title.ptr);
  wdesc1.width = 800;
  wdesc1.height = 600;

  cj_window_desc_t wdesc2 = wdesc1;
  wdesc2.title.ptr = "CJelly Window 2 (Textured Graph)";
  wdesc2.title.len = strlen(wdesc2.title.ptr);

  cj_window_desc_t wdesc3 = wdesc1;
  wdesc3.title.ptr = "CJelly Window 3 (Multi-Pass Graph)";
  wdesc3.title.len = strlen(wdesc3.title.ptr);
  wdesc3.width = 600;
  wdesc3.height = 400;

  printf("Creating windows...\n");
  cj_window_t* win1 = cj_window_create(engine, &wdesc1);
  printf("Created window 1\n");
  cj_window_t* win2 = cj_window_create(engine, &wdesc2);
  printf("Created window 2\n");
  cj_window_t* win3 = cj_window_create(engine, &wdesc3);
  printf("Created window 3\n");

  // Create different render graphs for each window
  printf("Creating render graphs...\n");
  cj_rgraph_desc_t rgraph_desc = {0};
  printf("About to create graph1...\n");
  cj_rgraph_t* graph1 = cj_rgraph_create(engine, &rgraph_desc);  // Color-only graph
  printf("Created graph1\n");
  printf("About to create graph2...\n");
  cj_rgraph_t* graph2 = cj_rgraph_create(engine, &rgraph_desc);  // Textured graph
  printf("Created graph2\n");
  printf("About to create graph3...\n");
  cj_rgraph_t* graph3 = cj_rgraph_create(engine, &rgraph_desc);  // Multi-pass graph
  printf("Created graph3\n");

  // Attach render graphs to windows
  cj_window_set_render_graph(win1, graph1);
  cj_window_set_render_graph(win2, graph2);
  cj_window_set_render_graph(win3, graph3);

  // Configure different parameters for each window's render graph
  cj_str_t param_color = {"render_mode", 11};
  cj_str_t param_textured = {"render_mode", 11};
  cj_str_t param_multipass = {"render_mode", 11};
  cj_str_t param_passes = {"pass_count", 10};
  cj_str_t param_effects = {"post_effects", 12};

  cj_rgraph_set_i32(graph1, param_color, 1);        // Color-only mode
  cj_rgraph_set_i32(graph2, param_textured, 2);     // Textured mode
  cj_rgraph_set_i32(graph3, param_multipass, 3);    // Multi-pass mode
  cj_rgraph_set_i32(graph3, param_passes, 2);       // 2 passes
  cj_rgraph_set_i32(graph3, param_effects, 1);      // Enable post-effects

  // Add nodes to all render graphs to make them functional
  printf("About to add color node to Window 1...\n");
  cj_result_t color_result = cj_rgraph_add_color_node(graph1, "color_effect");
  if (color_result == CJ_SUCCESS) {
    printf("Added color effect to Window 1\n");
  } else {
    printf("Failed to add color effect to Window 1\n");
  }

  printf("About to add textured node to Window 2...\n");
  cj_result_t textured_result = cj_rgraph_add_textured_node(graph2, "textured_effect");
  if (textured_result == CJ_SUCCESS) {
    printf("Added textured effect to Window 2\n");
  } else {
    printf("Failed to add textured effect to Window 2\n");
  }

  // Add a blur node to window 3's render graph to demonstrate post-processing
  printf("About to add blur node to Window 3...\n");
  cj_result_t blur_result = cj_rgraph_add_blur_node(graph3, "blur_effect");
  if (blur_result == CJ_SUCCESS) {
    printf("Added blur effect to Window 3 (Multi-Pass Graph)\n");
  } else {
    printf("Failed to add blur effect to Window 3\n");
  }

  // Legacy fallback: still set up color pipeline for window 1
  CJellyBindlessResources* colorOnly = cj_engine_color_pipeline(engine);
  CJellyVulkanContext ctx_local = {0};
  cj_engine_export_context(engine, &ctx_local);
  if (colorOnly) {
    cj_window_rerecord_bindless_color(win1, (const void*)colorOnly, &ctx_local);
  }

  uint64_t start_ms = getCurrentTimeInMilliseconds();
  Window1Context w1ctx = { colorOnly, start_ms };
  Window3Context w3ctx = { graph3, start_ms };

  cj_window_on_frame(win1, window1_on_frame, &w1ctx);
  cj_window_on_frame(win3, window3_on_frame, &w3ctx);

  // Register resize callbacks for all windows
  cj_window_on_resize(win1, window1_on_resize, NULL);
  cj_window_on_resize(win2, window2_on_resize, NULL);
  cj_window_on_resize(win3, window3_on_resize, NULL);

  // Register signal handlers automatically (handlers only set shutdown flag)
  cjelly_application_register_signal_handlers(app);

  printf("Starting callback-based event loop...\n");
  cj_run_config_t run_cfg = {0};
  run_cfg.target_fps = 30;
  run_cfg.enable_fps_profiling = true;  /* Enable FPS statistics output */
  cj_run_with_config(engine, &run_cfg);

  printf("Event loop exited.\n");

  // Destroy any remaining windows (some may have been closed via X button)
  // Check which windows still exist before destroying
  {
    uint32_t final_count = cjelly_application_window_count(app);
    void* final_windows[10];
    uint32_t final_actual = cjelly_application_get_windows(app, final_windows, final_count < 10 ? final_count : 10);

    bool win1_exists = false, win2_exists = false, win3_exists = false;
    for (uint32_t j = 0; j < final_actual; j++) {
      if (final_windows[j] == win1) win1_exists = true;
      if (final_windows[j] == win2) win2_exists = true;
      if (final_windows[j] == win3) win3_exists = true;
    }

    if (win1_exists) cj_window_destroy(win1);
    if (win2_exists) cj_window_destroy(win2);
    if (win3_exists) cj_window_destroy(win3);
  }

  // Destroy render graphs
  cj_rgraph_destroy(graph1);
  cj_rgraph_destroy(graph2);
  cj_rgraph_destroy(graph3);

  // Cleanup - colorOnly is now engine-owned, no manual cleanup needed

  // Wait for GPU idle and clean up global/context resources last
  cj_engine_wait_idle(engine);
  cj_engine_shutdown_device(engine);

#ifndef _WIN32
  XCloseDisplay(display);
#endif
  // Free engine last (owns no Vulkan handles)
  cj_engine_shutdown(engine);
  return 0;
}
