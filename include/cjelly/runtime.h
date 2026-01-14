/* CJelly runtime utilities: context, bindless helpers, event loop */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "cj_macros.h"
#include "cj_types.h"
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal Vulkan context used by public helpers */
typedef struct CJellyVulkanContext {
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkQueue graphicsQueue;
  VkQueue presentQueue;
  VkRenderPass renderPass;
  VkCommandPool commandPool;
} CJellyVulkanContext;

/* Forward-declare bindless resources struct; layout is internal */
typedef struct CJellyBindlessResources CJellyBindlessResources;

/* Retained for source compatibility; no-ops or engine-backed where needed */
static inline int cjelly_init_context(CJellyVulkanContext* ctx, int enableValidation) { (void)ctx; (void)enableValidation; return 0; }
static inline void cjelly_destroy_context(CJellyVulkanContext* ctx) { (void)ctx; }

/* Bindless resource helpers */
CJ_API CJellyBindlessResources* cjelly_create_bindless_resources_ctx(const CJellyVulkanContext* ctx);
CJ_API CJellyBindlessResources* cjelly_create_bindless_color_square_resources_ctx(const CJellyVulkanContext* ctx);
CJ_API void cjelly_destroy_bindless_resources(CJellyBindlessResources* resources);
/* Convenience setter for demo */
CJ_API void cj_bindless_set_color(CJellyBindlessResources* resources, float r, float g, float b, float a);
CJ_API void cj_bindless_update_split_from_colorMul(CJellyBindlessResources* resources);

/* Event loop helpers */
CJ_API void cj_poll_events(void);      /* alias for processWindowEvents */

/* ============================================================================
 * Callback-based main loop helpers
 * ============================================================================
 */

typedef struct cj_run_config_t {
  uint32_t target_fps;        /* 0 = unlimited */
  bool     vsync;             /* Use VSync for timing (skip sleep when VSync active) */
  bool     run_when_minimized;/* Continue running when all windows minimized */
  bool     enable_fps_profiling; /* Print FPS statistics to stdout every second */
} cj_run_config_t;

/* Run the event loop until all windows are closed or shutdown requested. */
CJ_API void cj_run(cj_engine_t* engine);

/* Run the event loop with configuration. */
CJ_API void cj_run_with_config(cj_engine_t* engine, const cj_run_config_t* config);

/* Run a single iteration. Returns false when loop should stop. */
CJ_API bool cj_run_once(cj_engine_t* engine);

/* Request the event loop to stop (implemented as a global flag for now). */
CJ_API void cj_request_stop(cj_engine_t* engine);

#ifdef __cplusplus
}
#endif
