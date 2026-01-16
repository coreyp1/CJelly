/** @file runtime.h
 *  @brief Runtime utilities: event loop, bindless helpers, and context management.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "cj_macros.h"
#include "cj_types.h"
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Minimal Vulkan context used by public helpers. */
typedef struct CJellyVulkanContext {
  VkInstance instance;          /**< Vulkan instance. */
  VkPhysicalDevice physicalDevice; /**< Physical device. */
  VkDevice device;             /**< Logical device. */
  VkQueue graphicsQueue;       /**< Graphics queue. */
  VkQueue presentQueue;        /**< Present queue. */
  VkRenderPass renderPass;     /**< Render pass. */
  VkCommandPool commandPool;   /**< Command pool. */
} CJellyVulkanContext;

/** Forward-declare bindless resources struct; layout is internal. */
typedef struct CJellyBindlessResources CJellyBindlessResources;

/** Initialize a Vulkan context (retained for source compatibility; no-op).
 *  @param ctx Context to initialize (unused).
 *  @param enableValidation Whether to enable validation layers (unused).
 *  @return Always returns 0.
 */
static inline int cjelly_init_context(CJellyVulkanContext* ctx, int enableValidation) { (void)ctx; (void)enableValidation; return 0; }

/** Destroy a Vulkan context (retained for source compatibility; no-op).
 *  @param ctx Context to destroy (unused).
 */
static inline void cjelly_destroy_context(CJellyVulkanContext* ctx) { (void)ctx; }

/** Create bindless resources using a Vulkan context.
 *  @param ctx Vulkan context to use for resource creation.
 *  @return Pointer to created resources, or NULL on failure.
 */
CJ_API CJellyBindlessResources* cjelly_create_bindless_resources_ctx(const CJellyVulkanContext* ctx);

/** Create bindless color square resources using a Vulkan context.
 *  @param ctx Vulkan context to use for resource creation.
 *  @return Pointer to created resources, or NULL on failure.
 */
CJ_API CJellyBindlessResources* cjelly_create_bindless_color_square_resources_ctx(const CJellyVulkanContext* ctx);

/** Destroy bindless resources.
 *  @param resources Resources to destroy. NULL is safe and will be ignored.
 */
CJ_API void cjelly_destroy_bindless_resources(CJellyBindlessResources* resources);

/** Set the color multiplier for bindless resources.
 *  @param resources Resources to update.
 *  @param r Red component (0.0 to 1.0).
 *  @param g Green component (0.0 to 1.0).
 *  @param b Blue component (0.0 to 1.0).
 *  @param a Alpha component (0.0 to 1.0).
 */
CJ_API void cj_bindless_set_color(CJellyBindlessResources* resources, float r, float g, float b, float a);

/** Update vertex buffer for split rendering based on current color multiplier.
 *  This updates the vertex buffer to reflect the current colorMul values.
 *  @param resources Resources to update.
 */
CJ_API void cj_bindless_update_split_from_colorMul(CJellyBindlessResources* resources);

/** Poll window events (alias for processWindowEvents).
 *  Processes pending window events such as resize, close, etc.
 */
CJ_API void cj_poll_events(void);

/** @defgroup event_loop Event Loop
 *  @{
 */

/** Event loop configuration structure. */
typedef struct cj_run_config_t {
  uint32_t target_fps;        /**< Target FPS (0 = unlimited). */
  bool     vsync;             /**< Use VSync for timing (skip sleep when VSync active). */
  bool     run_when_minimized;/**< Continue running when all windows are minimized. */
  bool     enable_fps_profiling; /**< Print FPS statistics to stdout every second. */
} cj_run_config_t;

/** Run the event loop until all windows are closed or shutdown requested.
 *  Uses default configuration (30 FPS, no profiling).
 *  @param engine The engine to run the event loop for.
 */
CJ_API void cj_run(cj_engine_t* engine);

/** Run the event loop with configuration.
 *  @param engine The engine to run the event loop for.
 *  @param config Configuration for the event loop. NULL uses defaults.
 */
CJ_API void cj_run_with_config(cj_engine_t* engine, const cj_run_config_t* config);

/** Run a single iteration of the event loop.
 *  @param engine The engine to run the event loop iteration for.
 *  @return true if the loop should continue, false if it should stop.
 */
CJ_API bool cj_run_once(cj_engine_t* engine);

/** Request the event loop to stop.
 *  This sets a global flag that causes the event loop to exit on the next iteration.
 *  @param engine The engine whose event loop should stop.
 */
CJ_API void cj_request_stop(cj_engine_t* engine);

/** @} */

#ifdef __cplusplus
}
#endif
