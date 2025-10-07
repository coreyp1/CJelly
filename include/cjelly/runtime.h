/* CJelly runtime utilities: context, bindless helpers, event loop */
#pragma once
#include <stdint.h>
#include "cj_macros.h"
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

/* Context-based initialization helpers (deprecated: engine owns init) */
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
CJ_API int  cj_should_close(void);
CJ_API void cj_set_should_close(int v);

#ifdef __cplusplus
}
#endif


