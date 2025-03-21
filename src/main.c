#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

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
  CJellyWindow win1, win2;
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
  while (!shouldClose) {
    processWindowEvents();
    drawFrameForWindow(&win1);
    drawFrameForWindow(&win2);
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
