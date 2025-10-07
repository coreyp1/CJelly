#pragma once
#include <vulkan/vulkan.h>
#include <cjelly/cj_engine.h>
#include <cjelly/runtime.h>

/* Internal engine API during migration */

/* Bind current legacy globals into the engine instance (temporary) */
CJ_API void cj_engine_bind_legacy_globals(cj_engine_t* engine);

/* Import a CJellyVulkanContext into the engine (preferred) */
CJ_API void cj_engine_import_context(cj_engine_t* engine, const CJellyVulkanContext* ctx);

/* Manage a process-wide current engine pointer during transition */
CJ_API void cj_engine_set_current(cj_engine_t* engine);
CJ_API cj_engine_t* cj_engine_get_current(void);

/* Getters to read Vulkan objects from the engine (will replace globals) */
CJ_API VkInstance cj_engine_instance(const cj_engine_t*);
CJ_API VkPhysicalDevice cj_engine_physical_device(const cj_engine_t*);
CJ_API VkDevice cj_engine_device(const cj_engine_t*);
CJ_API VkQueue cj_engine_graphics_queue(const cj_engine_t*);
CJ_API VkQueue cj_engine_present_queue(const cj_engine_t*);
CJ_API VkRenderPass cj_engine_render_pass(const cj_engine_t*);
CJ_API VkCommandPool cj_engine_command_pool(const cj_engine_t*);

/* Vulkan lifecycle owned by the engine */
CJ_API int  cj_engine_init_vulkan(cj_engine_t* engine, int use_validation);
CJ_API void cj_engine_shutdown_vulkan(cj_engine_t* engine);

/* === Internal resource tables for Phase 3 === */
typedef struct cj_res_entry_t {
  uint32_t generation;
  uint32_t refcount;
  uint32_t slot;      /* descriptor slot when applicable */
  uint8_t  in_use;
} cj_res_entry_t;

#define CJ_ENGINE_MAX_TEXTURES 1024u
#define CJ_ENGINE_MAX_BUFFERS  1024u
#define CJ_ENGINE_MAX_SAMPLERS 256u

typedef enum cj_res_kind_t { CJ_RES_TEX = 0, CJ_RES_BUF = 1, CJ_RES_SMP = 2 } cj_res_kind_t;

/* Allocate a new entry, returning 64-bit handle (index|generation). Returns 0 on failure. */
CJ_API uint64_t cj_engine_res_alloc(cj_engine_t* e, cj_res_kind_t kind, uint32_t* out_slot);
/* Retain existing handle (no-op if invalid) */
CJ_API void     cj_engine_res_retain(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle);
/* Release existing handle (no-op if invalid) */
CJ_API void     cj_engine_res_release(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle);
/* Query descriptor slot for a handle; returns 0 if invalid. */
CJ_API uint32_t cj_engine_res_slot(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle);
