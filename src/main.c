#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
uint64_t getCurrentTimeInMilliseconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}
#endif

#include <cjelly/cjelly.h>


void renderSquare(CJellyWindow * win) {
  drawFrameForWindow(win);
}

int main(void) {
#ifdef _WIN32
  // Windows: hInstance is set in createPlatformWindow.
#else
  // Linux: Open X display.
  display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "Failed to open X display\n");
    exit(EXIT_FAILURE);
  }
#endif

  CJellyApplication * app = NULL;
  CJellyApplicationError err = cjelly_application_create(
      &app, "Vulkan Square", VK_MAKE_VERSION(1, 0, 0));
  if (err != CJELLY_APPLICATION_ERROR_NONE) {
    fprintf(stderr, "Failed to create CJelly application: %d\n", err);
    return EXIT_FAILURE;
  }
  // Set up the application options.
  // cjelly_application_set_required_vulkan_version(app, VK_API_VERSION_1_0);
  // cjelly_application_set_required_gpu_memory(app, 2048);
  // cjelly_application_set_device_type(app, CJELLY_DEVICE_TYPE_DISCRETE, true);


  // Initialize the application.
  err = cjelly_application_init(app);
  if (err != CJELLY_APPLICATION_ERROR_NONE) {
    fprintf(stderr, "Failed to initialize CJelly application: %d\n", err);
    return EXIT_FAILURE;
  }

  // Destroy the application.
  cjelly_application_destroy(app);
  app = NULL;

  //   // Create two windows.
  //   CJellyWindow win1 = {0}, win2 = {0};

  //   win1.renderCallback = renderSquare;
  //   win1.updateMode = CJELLY_UPDATE_MODE_FIXED;
  //   win1.fixedFramerate = 60;

  //   win2.renderCallback = renderSquare;
  //   win2.updateMode = CJELLY_UPDATE_MODE_EVENT_DRIVEN;

  //   createPlatformWindow(&win1, "Vulkan Square - Window 1", WIDTH, HEIGHT);
  //   createPlatformWindow(&win2, "Vulkan Square - Window 2", WIDTH, HEIGHT);

  //   // Global Vulkan initialization.
  //   initVulkanGlobal();

  //   // For each window, create the per-window Vulkan objects.
  //   createSurfaceForWindow(&win1);
  //   createSwapChainForWindow(&win1);
  //   createImageViewsForWindow(&win1);
  //   createFramebuffersForWindow(&win1);
  //   createCommandBuffersForWindow(&win1);
  //   createSyncObjectsForWindow(&win1);

  //   createSurfaceForWindow(&win2);
  //   createSwapChainForWindow(&win2);
  //   createImageViewsForWindow(&win2);
  //   createFramebuffersForWindow(&win2);
  //   createTexturedCommandBuffersForWindow(&win2);
  //   createSyncObjectsForWindow(&win2);

  //   // Main render loop.
  //   CJellyWindow * windows[] = {&win1, &win2};
  //   while (!shouldClose) {
  //     processWindowEvents();
  //     uint64_t currentTime = getCurrentTimeInMilliseconds();

  //     for (int i = 0; i < 2; ++i) {
  //       CJellyWindow * win = windows[i];
  //       // Update window 1 independently:
  //       switch (win->updateMode) {
  //       case CJELLY_UPDATE_MODE_VSYNC:
  //         // For VSync mode, the present call (with FIFO) will throttle
  //         rendering. if (win->renderCallback) {
  //           win->renderCallback(win);
  //         }
  //         break;
  //       case CJELLY_UPDATE_MODE_FIXED:
  //         // In fixed mode, only render if it’s time for the next frame.
  //         if (currentTime >= win->nextFrameTime) {
  //           if (win->renderCallback) {
  //             win->renderCallback(win);
  //           }
  //           win->nextFrameTime = currentTime + (1000 / win->fixedFramerate);
  //         }
  //         break;
  //       case CJELLY_UPDATE_MODE_EVENT_DRIVEN:
  //         // In event-driven mode, only render when needed.
  //         if (win->needsRedraw) {
  //           if (win->renderCallback) {
  //             win->renderCallback(win);
  //           }
  //           win->needsRedraw = 0;
  //         }
  //         break;
  //       }
  //     }

  //     // Sleep for a short duration to avoid busy waiting.
  // #ifdef _WIN32
  //     Sleep(1);
  // #else
  //     struct timespec req = {0, 1000000}; // 1 millisecond
  //     nanosleep(&req, NULL);
  // #endif
  //   }
  //   vkDeviceWaitIdle(device);

  //   // Clean up per-window resources.
  //   cleanupWindow(&win1);
  //   cleanupWindow(&win2);

  //   // Clean up global Vulkan resources.
  //   cleanupVulkanGlobal();

#ifndef _WIN32
  XCloseDisplay(display);
#endif
  return 0;
}
