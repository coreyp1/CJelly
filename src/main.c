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
uint64_t getCurrentTimeInMilliseconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}
#endif

#include <cjelly/cjelly.h>

// Forward declarations for global variables used by bindless rendering
extern VkBuffer vertexBufferBindless;
extern VkDeviceMemory vertexBufferBindlessMemory;
extern VkDevice device;
extern VkRenderPass renderPass;
extern VkCommandPool commandPool;

// Vertex structure for bindless rendering (must match cjelly.c)
typedef struct {
    float pos[2];      // Position (x, y)
    float color[3];    // Color (r, g, b)
    uint32_t textureID; // Texture ID for bindless rendering
} VertexBindless;

// Update bindless color for square based on current time
void updateBindlessColor(uint64_t currentTime) {
  // Get current second and determine color (red or green)
  time_t currentSeconds = currentTime / 1000; // Convert ms to seconds
  int colorIndex = (currentSeconds % 2); // 0 = red, 1 = green

  // Define colors
  float redColor[3] = {1.0f, 0.0f, 0.0f};   // Red
  float greenColor[3] = {0.0f, 1.0f, 0.0f}; // Green

  // Choose color based on time
  float *selectedColor = (colorIndex == 0) ? redColor : greenColor;

  // Update the vertex buffer data with the new color
  if (vertexBufferBindless != VK_NULL_HANDLE && vertexBufferBindlessMemory != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
    void * data;
    VkResult result = vkMapMemory(device, vertexBufferBindlessMemory, 0, sizeof(VertexBindless) * 6, 0, &data);
    if (result == VK_SUCCESS) {
      // Update color for all vertices
      VertexBindless *vertices = (VertexBindless *)data;
      for (int i = 0; i < 6; i++) {
        // Update the color component of each vertex
        vertices[i].color[0] = selectedColor[0]; // R
        vertices[i].color[1] = selectedColor[1]; // G
        vertices[i].color[2] = selectedColor[2]; // B
      }

      vkUnmapMemory(device, vertexBufferBindlessMemory);
    }
  }
}

void renderSquare(CJellyWindow * win) {
  drawFrameForWindow(win);
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

  // Defer bindless resource creation until after global Vulkan init
  CJellyBindlessResources* bindlessResources = NULL;

  // No application object used in this sample

    // Create two windows.
    CJellyWindow win1 = {0}, win2 = {0};

    win1.renderCallback = renderSquare;
    win1.updateMode = CJELLY_UPDATE_MODE_FIXED;
    win1.fixedFramerate = 60;

    win2.renderCallback = renderSquare;
    win2.updateMode = CJELLY_UPDATE_MODE_EVENT_DRIVEN;

    createPlatformWindow(&win1, "Vulkan Square - Window 1", WIDTH, HEIGHT);
    createPlatformWindow(&win2, "Vulkan Square - Window 2", WIDTH, HEIGHT);

    // Initialize Vulkan via context API
    fprintf(stderr, "Initializing Vulkan...\n");
    CJellyVulkanContext ctx = {0};
    if (!cjelly_init_context(&ctx, 0)) {
      fprintf(stderr, "Failed to initialize CJelly Vulkan context\n");
      return EXIT_FAILURE;
    }
    fprintf(stderr, "Vulkan initialized.\n");

    // For each window, create the per-window Vulkan objects.
    fprintf(stderr, "Creating per-window resources for win1...\n");
    createSurfaceForWindow(&win1);
    createSwapChainForWindow(&win1);
    createImageViewsForWindow(&win1);
    createFramebuffersForWindow(&win1);
    // Use bindless-capable path for window 1 as well (color-only)
    CJellyBindlessResources* colorOnly = cjelly_create_bindless_color_square_resources_ctx(&ctx);
    if (colorOnly) {
      // Share the atlas descriptor from the fish path if available to ensure push constants are set
      if (bindlessResources && bindlessResources->textureAtlas) {
        colorOnly->textureAtlas = bindlessResources->textureAtlas;
      }
      createBindlessCommandBuffersForWindowCtx(&win1, colorOnly, &ctx);
    } else {
      createCommandBuffersForWindow(&win1);
    }
    createSyncObjectsForWindow(&win1);

    fprintf(stderr, "Creating per-window resources for win2...\n");
    createSurfaceForWindow(&win2);
    createSwapChainForWindow(&win2);
    createImageViewsForWindow(&win2);
    createFramebuffersForWindow(&win2);
    // Create bindless resources now that Vulkan globals are initialized
    fprintf(stderr, "Creating bindless resources...\n");
    bindlessResources = cjelly_create_bindless_resources_ctx(&ctx);
    fprintf(stderr, "DEBUG: bindlessResources returned %p\n", (void*)bindlessResources);
    if (bindlessResources) {
      printf("DEBUG: Calling bindless command buffer creation...\n");
      createBindlessCommandBuffersForWindowCtx(&win2, bindlessResources, &ctx);
      printf("DEBUG: Bindless command buffer creation completed\n");
    } else {
      createTexturedCommandBuffersForWindow(&win2);
    }
    createSyncObjectsForWindow(&win2);

    // Main render loop.
    CJellyWindow * windows[] = {&win1, &win2};
    // Track last color toggle state for window 1
    int lastColorIndex = -1;
    while (!shouldClose) {
      processWindowEvents();
      uint64_t currentTime = getCurrentTimeInMilliseconds();

      // Color switching is implemented for bindless rendering (when enabled)

      for (int i = 0; i < 2; ++i) {
        CJellyWindow * win = windows[i];
        // Update window 1 independently:
        switch (win->updateMode) {
        case CJELLY_UPDATE_MODE_VSYNC:
          // For VSync mode, the present call (with FIFO) will throttle rendering.
          if (win->renderCallback) {
            win->renderCallback(win);
          }
          break;
        case CJELLY_UPDATE_MODE_FIXED:
          // In fixed mode, only render if it’s time for the next frame.
          if (currentTime >= win->nextFrameTime) {
            if (win->renderCallback) {
              // For Window 1, toggle color via push constant by re-recording command buffers when the second changes
              if (win == &win1 && colorOnly) {
                int colorIndex = ((currentTime / 1000) % 2);
                if (colorIndex != lastColorIndex) {
                  // Update push constant color multiplier (red/green)
                  colorOnly->colorMul[0] = (colorIndex == 0) ? 1.0f : 0.0f; // R
                  colorOnly->colorMul[1] = (colorIndex == 0) ? 0.0f : 1.0f; // G
                  colorOnly->colorMul[2] = 0.0f;                              // B
                  colorOnly->colorMul[3] = 1.0f;                              // A
                  // Re-record command buffers for win1 with updated push constants
                  if (win1.commandBuffers) {
                    vkFreeCommandBuffers(device, commandPool, win1.swapChainImageCount, win1.commandBuffers);
                    free(win1.commandBuffers);
                    win1.commandBuffers = NULL;
                  }
                  createBindlessCommandBuffersForWindow(&win1, colorOnly, device, commandPool, renderPass);
                  lastColorIndex = colorIndex;
                }
              }
              win->renderCallback(win);
            }
            win->nextFrameTime = currentTime + (1000 / win->fixedFramerate);
          }
          break;
        case CJELLY_UPDATE_MODE_EVENT_DRIVEN:
          // In event-driven mode, only render when needed.
          if (win->needsRedraw) {
            if (win->renderCallback) {
              win->renderCallback(win);
            }
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
    vkDeviceWaitIdle(ctx.device);

    // Clean up per-window resources.
    cleanupWindow(&win1);
    cleanupWindow(&win2);

    // Destroy bindless resources before global cleanup
    if (bindlessResources) {
      cjelly_destroy_bindless_resources(bindlessResources);
      bindlessResources = NULL;
    }

    // Clean up global Vulkan resources.
    cjelly_destroy_context(&ctx);

#ifndef _WIN32
  XCloseDisplay(display);
#endif
  return 0;
}
