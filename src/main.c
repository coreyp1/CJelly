#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

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
#include <cjelly/cj_engine.h>
#include <cjelly/cj_window.h>
#include <cjelly/cj_rgraph.h>
#include <cjelly/engine_internal.h>
#include <cjelly/bindless_internal.h>

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

  // Create three windows via new API (now that Vulkan is ready)
  cj_window_desc_t wdesc1 = {0};
  wdesc1.title.ptr = "CJelly Window 1 (Color Graph)";
  wdesc1.title.len = 25;
  wdesc1.width = 800;
  wdesc1.height = 600;
  
  cj_window_desc_t wdesc2 = wdesc1;
  wdesc2.title.ptr = "CJelly Window 2 (Textured Graph)";
  wdesc2.title.len = 28;
  
  cj_window_desc_t wdesc3 = wdesc1;
  wdesc3.title.ptr = "CJelly Window 3 (Multi-Pass Graph)";
  wdesc3.title.len = 30;
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

  // Main render loop with FPS limiting
  cj_window_t* windows[] = {win1, win2, win3};
  
  // FPS configuration
  const int target_fps = 30;  // Target FPS (reduced for better performance)
  const uint64_t frame_time_ms = 1000 / target_fps;  // Target frame time in milliseconds
  uint64_t last_frame_time = getCurrentTimeInMilliseconds();
  uint64_t last_update_time = last_frame_time;
  uint64_t last_poll_time = last_frame_time;
  const uint64_t update_interval_ms = 50; // Update parameters only every 50ms (20 times per second)
  const uint64_t poll_interval_ms = 16;   // Poll events every 16ms (60 times per second)
  
  printf("Starting main render loop...\n");
  int frame_count = 0;
  
  while (!cj_should_close()) {
    uint64_t currentTime = getCurrentTimeInMilliseconds();
    frame_count++;
    
    
    // Only poll events periodically to reduce CPU usage
    if (currentTime - last_poll_time >= poll_interval_ms) {
      cj_poll_events();
      last_poll_time = currentTime;
    }
    
    // Only update parameters periodically to reduce CPU usage
    if (currentTime - last_update_time >= update_interval_ms) {
      // Toggle color each second for window 1 by updating push constants in recorded commands
      if (colorOnly) {
        int colorIndex = ((currentTime / 1000) % 2);
        float r = (colorIndex == 0) ? 1.0f : 0.0f;
        float g = (colorIndex == 0) ? 0.0f : 1.0f;
        cj_bindless_set_color(colorOnly, r, g, 0.0f, 1.0f);
        cj_bindless_update_split_from_colorMul(colorOnly);
        // REMOVED: cj_window_rerecord_bindless_color(win1, (const void*)colorOnly, &ctx_local);
        // Command buffers should NOT be recreated every frame - this was causing major performance issues
      }
      
      // Update window 3's render graph parameters dynamically
      cj_str_t param_time = {"time_ms", 7};
      cj_str_t param_blur_intensity = {"blur_intensity", 12};
      cj_rgraph_set_i32(graph3, param_time, (int32_t)(currentTime % 10000));
      
      // Animate blur intensity over time (0.0 to 1.0) - slower for better performance
      float blur_intensity = 0.5f + 0.5f * sin(currentTime * 0.001f); // Slower animation
      cj_rgraph_set_i32(graph3, param_blur_intensity, (int32_t)(blur_intensity * 1000.0f)); // Store as integer * 1000
      
      last_update_time = currentTime;
    }
    
    // Render all three windows
    for (int i = 0; i < 3; ++i) {
      cj_frame_info_t frame = {0};
      if (cj_window_begin_frame(windows[i], &frame) == CJ_SUCCESS) {
        cj_window_execute(windows[i]);
        cj_window_present(windows[i]);
      }
    }
    
    // FPS limiting - sleep for the remaining frame time
    uint64_t frame_end_time = getCurrentTimeInMilliseconds();
    uint64_t frame_duration = frame_end_time - last_frame_time;
    
    if (frame_duration < frame_time_ms) {
      uint64_t sleep_time = frame_time_ms - frame_duration;
#ifdef _WIN32
      Sleep((DWORD)sleep_time);
#else
      struct timespec req = {0, (long)(sleep_time * 1000000)}; // Convert ms to nanoseconds
      nanosleep(&req, NULL);
#endif
    }
    last_frame_time = getCurrentTimeInMilliseconds();
  }
  // Destroy windows via new API (frees per-window resources and command buffers)
  cj_window_destroy(win1);
  cj_window_destroy(win2);
  cj_window_destroy(win3);
  
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
