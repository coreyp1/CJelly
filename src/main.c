#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <stdint.h>
uint64_t getCurrentTimeInMilliseconds(void) {
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return (uint64_t)((counter.QuadPart * 1000LL) / frequency.QuadPart);
}
#else
#include <time.h>
#include <stdint.h>
uint64_t getCurrentTimeInMilliseconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}
#endif

#include <cjelly/cjelly.h>

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

  // Create two windows.
  CJellyWindow win1 = {0}, win2 = {0};
  win1.updateMode = CJELLY_UPDATE_MODE_FIXED;
  win1.fixedFramerate = 60;
  win2.updateMode = CJELLY_UPDATE_MODE_EVENT_DRIVEN;
  createPlatformWindow(&win1, "Vulkan Square - Window 1", WIDTH, HEIGHT);
  createPlatformWindow(&win2, "Vulkan Square - Window 2", WIDTH, HEIGHT);

  // Global Vulkan initialization.
  initVulkanGlobal();

  // For each window, create the per-window Vulkan objects.
  createSurfaceForWindow(&win1);
  createSwapChainForWindow(&win1);
  createImageViewsForWindow(&win1);
  createFramebuffersForWindow(&win1);
  createCommandBuffersForWindow(&win1);
  createSyncObjectsForWindow(&win1);

  createSurfaceForWindow(&win2);
  createSwapChainForWindow(&win2);
  createImageViewsForWindow(&win2);
  createFramebuffersForWindow(&win2);
  createCommandBuffersForWindow(&win2);
  createSyncObjectsForWindow(&win2);

  // Main render loop.
  CJellyWindow * windows[] = {&win1, &win2};
  while (!shouldClose) {
    processWindowEvents();
    uint64_t currentTime = getCurrentTimeInMilliseconds();

    for (int i = 0; i < 2; ++i) {
      CJellyWindow * win = windows[i];
      // Update window 1 independently:
      switch (win->updateMode) {
        case CJELLY_UPDATE_MODE_VSYNC:
          // For VSync mode, the present call (with FIFO) will throttle rendering.
          drawFrameForWindow(win);
          break;
        case CJELLY_UPDATE_MODE_FIXED:
          // In fixed mode, only render if it’s time for the next frame.
          if (currentTime >= win->nextFrameTime) {
            drawFrameForWindow(win);
            win->nextFrameTime = currentTime + (1000 / win->fixedFramerate);
          }
          break;
        case CJELLY_UPDATE_MODE_EVENT_DRIVEN:
          // In event-driven mode, only render when needed.
          if (win->needsRedraw) {
            drawFrameForWindow(win);
            win->needsRedraw = 0;
          }
          break;
      }
    }

    // Sleep for a short duration to avoid busy waiting.
  #ifdef _WIN32
    Sleep(1);
  #else
    struct timespec req = {0, 1000000}; // 1 millisecond
    nanosleep(&req, NULL);
  #endif
  }
  vkDeviceWaitIdle(device);

  // Clean up per-window resources.
  cleanupWindow(&win1);
  cleanupWindow(&win2);

  // Clean up global Vulkan resources.
  cleanupVulkanGlobal();

#ifndef _WIN32
  XCloseDisplay(display);
#endif
  return 0;
}
