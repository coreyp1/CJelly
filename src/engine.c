#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <cjelly/cj_engine.h>
#include <cjelly/engine_internal.h>
#include <cjelly/runtime.h>
#include <vulkan/vulkan.h>
#include <cjelly/textured_internal.h>
#include <cjelly/bindless_state_internal.h>
#include <cjelly/basic_state_internal.h>

/* Internal definition of the opaque engine type */
struct cj_engine_t {
  uint32_t selected_device_index;
  uint32_t flags;

  /* Vulkan globals during migration */
  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkQueue graphics_queue;
  VkQueue present_queue;
  VkRenderPass render_pass;
  VkCommandPool command_pool;
  VkFormat color_format;

  /* Phase 3: simple resource tables */
  cj_res_entry_t textures[CJ_ENGINE_MAX_TEXTURES];
  cj_res_entry_t buffers[CJ_ENGINE_MAX_BUFFERS];
  cj_res_entry_t samplers[CJ_ENGINE_MAX_SAMPLERS];

  /* Internal-only textured resources (migration) */
  CJellyTexturedResources textured;
  /* Internal-only bindless state (migration) */
  CJellyBindlessState bindless;
  /* Internal-only basic pipeline state (migration) */
  CJellyBasicState basic;
};

static cj_engine_t* g_current_engine = NULL;

/* --- Engine-owned Vulkan bootstrap (migration of legacy init) --- */
static int eng_create_instance(cj_engine_t* e, int use_validation) {
  VkApplicationInfo appInfo = {0};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "CJelly";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "CJellyEngine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  const char* extensions[8];
  uint32_t extCount = 0;
  extensions[extCount++] = "VK_KHR_surface";
#ifdef _WIN32
  extensions[extCount++] = "VK_KHR_win32_surface";
#else
  extensions[extCount++] = "VK_KHR_xlib_surface";
#endif

  VkInstanceCreateInfo ci = {0};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &appInfo;
  ci.enabledExtensionCount = extCount;
  ci.ppEnabledExtensionNames = extensions;
  const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
  if (use_validation) {
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = layers;
  }
  if (vkCreateInstance(&ci, NULL, &e->instance) != VK_SUCCESS) return 0;
  return 1;
}

static int eng_pick_physical_device(cj_engine_t* e) {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(e->instance, &count, NULL);
  if (count == 0) return 0;
  VkPhysicalDevice devices[16];
  if (count > 16) count = 16;
  vkEnumeratePhysicalDevices(e->instance, &count, devices);

  /* Simple selection: prefer discrete, else first */
  VkPhysicalDevice best = devices[0];
  int bestScore = -1;
  for (uint32_t i = 0; i < count; ++i) {
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(devices[i], &props);
    int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1000 : 0;
    score += (int)props.limits.maxImageDimension2D;
    if (score > bestScore) { bestScore = score; best = devices[i]; }
  }
  e->physical_device = best;
  return 1;
}

static int eng_create_logical_device(cj_engine_t* e) {
  uint32_t qCount = 0; VkQueueFamilyProperties qProps[16];
  vkGetPhysicalDeviceQueueFamilyProperties(e->physical_device, &qCount, NULL);
  if (qCount > 16) qCount = 16;
  vkGetPhysicalDeviceQueueFamilyProperties(e->physical_device, &qCount, qProps);
  uint32_t gfxIndex = 0; int found = 0;
  for (uint32_t i = 0; i < qCount; ++i) {
    if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { gfxIndex = i; found = 1; break; }
  }
  if (!found) return 0;
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = {0};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = gfxIndex;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  const char* devExt[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  VkDeviceCreateInfo dci = {0};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = devExt;
  if (vkCreateDevice(e->physical_device, &dci, NULL, &e->device) != VK_SUCCESS) return 0;
  vkGetDeviceQueue(e->device, gfxIndex, 0, &e->graphics_queue);
  e->present_queue = e->graphics_queue;
  return 1;
}

static int eng_create_render_pass(cj_engine_t* e) {
  VkAttachmentDescription color = {0};
  VkFormat fmt = (e->color_format != 0) ? e->color_format : VK_FORMAT_B8G8R8A8_SRGB;
  color.format = fmt;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference colorRef = {0}; colorRef.attachment = 0; colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkSubpassDescription sub = {0}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &colorRef;
  VkRenderPassCreateInfo rp = {0}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rp.attachmentCount = 1; rp.pAttachments = &color; rp.subpassCount = 1; rp.pSubpasses = &sub;
  if (vkCreateRenderPass(e->device, &rp, NULL, &e->render_pass) != VK_SUCCESS) return 0;
  return 1;
}

/* Recreate engine render pass if format changed or not created */
CJ_API int cj_engine_ensure_render_pass(cj_engine_t* e, VkFormat fmt) {
  if (!e) return 0;
  if (e->render_pass != VK_NULL_HANDLE && e->color_format == fmt) return 1;
  if (e->render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(e->device, e->render_pass, NULL);
    e->render_pass = VK_NULL_HANDLE;
  }
  e->color_format = fmt;
  return eng_create_render_pass(e);
}

static int eng_create_command_pool(cj_engine_t* e) {
  VkCommandPoolCreateInfo pci = {0}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pci.queueFamilyIndex = 0;
  if (vkCreateCommandPool(e->device, &pci, NULL, &e->command_pool) != VK_SUCCESS) return 0;
  return 1;
}

CJ_API cj_engine_t* cj_engine_create(const cj_engine_desc_t* desc) {
  cj_engine_t* engine = (cj_engine_t*)malloc(sizeof(*engine));
  if (!engine) return NULL;
  memset(engine, 0, sizeof(*engine));
  engine->flags = desc ? desc->flags : 0u;
  if (desc && desc->device_select == CJ_DEVICE_SELECT_INDEX) {
    engine->selected_device_index = desc->requested_device_index;
  } else {
    engine->selected_device_index = 0u; /* default device index for now */
  }
  return engine;
}

CJ_API void cj_engine_shutdown(cj_engine_t* engine) {
  if (!engine) return;
  if (g_current_engine == engine) g_current_engine = NULL;
  free(engine);
}

CJ_API void cj_engine_wait_idle(cj_engine_t* engine) {
  (void)engine; /* no-op in stub */
}

/* Initialize Vulkan into the engine using the existing context bootstrap */
CJ_API int cj_engine_init_vulkan(cj_engine_t* engine, int use_validation) {
  if (!engine) return 0;
  if (!eng_create_instance(engine, use_validation)) return 0;
  if (!eng_pick_physical_device(engine)) return 0;
  if (!eng_create_logical_device(engine)) return 0;
  if (!eng_create_render_pass(engine)) return 0;
  if (!eng_create_command_pool(engine)) return 0;
  /* All code paths now use engine getters; legacy bind removed */
  return 1;
}

/* Shutdown Vulkan from the engine */
CJ_API void cj_engine_shutdown_vulkan(cj_engine_t* engine) {
  (void)engine;
  CJellyVulkanContext ctx = {0};
  cjelly_destroy_context(&ctx);
}

CJ_API uint32_t cj_engine_device_index(const cj_engine_t* engine) {
  return engine ? engine->selected_device_index : 0u;
}

CJ_API void cj_engine_get_bindless_info(const cj_engine_t* engine, cj_bindless_info_t* out_info) {
  (void)engine;
  if (!out_info) return;
  out_info->images_capacity = 0u;
  out_info->buffers_capacity = 0u;
  out_info->samplers_capacity = 0u;
}

/* === Migration helpers === */
CJ_API void cj_engine_bind_legacy_globals(cj_engine_t* engine) { (void)engine; }

CJ_API void cj_engine_set_current(cj_engine_t* engine) { g_current_engine = engine; }
CJ_API cj_engine_t* cj_engine_get_current(void) { return g_current_engine; }

CJ_API VkInstance cj_engine_instance(const cj_engine_t* e) { return e ? e->instance : VK_NULL_HANDLE; }
CJ_API VkPhysicalDevice cj_engine_physical_device(const cj_engine_t* e) { return e ? e->physical_device : VK_NULL_HANDLE; }
CJ_API VkDevice cj_engine_device(const cj_engine_t* e) { return e ? e->device : VK_NULL_HANDLE; }
CJ_API VkQueue cj_engine_graphics_queue(const cj_engine_t* e) { return e ? e->graphics_queue : VK_NULL_HANDLE; }
CJ_API VkQueue cj_engine_present_queue(const cj_engine_t* e) { return e ? e->present_queue : VK_NULL_HANDLE; }
CJ_API VkRenderPass cj_engine_render_pass(const cj_engine_t* e) { return e ? e->render_pass : VK_NULL_HANDLE; }
CJ_API VkCommandPool cj_engine_command_pool(const cj_engine_t* e) { return e ? e->command_pool : VK_NULL_HANDLE; }

/* Internal access to textured resources */
CJ_API CJellyTexturedResources* cj_engine_textured(const cj_engine_t* e) { return (CJellyTexturedResources*)(e ? &e->textured : NULL); }
CJ_API CJellyBindlessState* cj_engine_bindless(const cj_engine_t* e) { return (CJellyBindlessState*)(e ? &e->bindless : NULL); }
CJ_API CJellyBasicState* cj_engine_basic(const cj_engine_t* e) { return (CJellyBasicState*)(e ? &e->basic : NULL); }

CJ_API void cj_engine_import_context(cj_engine_t* engine, const CJellyVulkanContext* ctx) {
  if (!engine || !ctx) return;
  engine->instance = ctx->instance;
  engine->physical_device = ctx->physicalDevice;
  engine->device = ctx->device;
  engine->graphics_queue = ctx->graphicsQueue;
  engine->present_queue = ctx->presentQueue;
  engine->render_pass = ctx->renderPass;
  engine->command_pool = ctx->commandPool;

  /* Initialize resource tables */
  memset(engine->textures, 0, sizeof(engine->textures));
  memset(engine->buffers, 0, sizeof(engine->buffers));
  memset(engine->samplers, 0, sizeof(engine->samplers));

  /* Initialize internal textured container */
  memset(&engine->textured, 0, sizeof(engine->textured));
  /* Initialize internal bindless container */
  memset(&engine->bindless, 0, sizeof(engine->bindless));
  /* Initialize internal basic container */
  memset(&engine->basic, 0, sizeof(engine->basic));
}

static inline cj_res_entry_t* table_for(cj_engine_t* e, cj_res_kind_t kind, size_t* out_cap) {
  switch (kind) {
    case CJ_RES_TEX: *out_cap = CJ_ENGINE_MAX_TEXTURES; return e->textures;
    case CJ_RES_BUF: *out_cap = CJ_ENGINE_MAX_BUFFERS;  return e->buffers;
    case CJ_RES_SMP: *out_cap = CJ_ENGINE_MAX_SAMPLERS; return e->samplers;
  }
  *out_cap = 0; return NULL;
}

static inline uint64_t make_handle(uint32_t index, uint32_t gen) {
  return ((uint64_t)index << 32) | (uint64_t)gen;
}

static inline void split_handle(uint64_t h, uint32_t* out_index, uint32_t* out_gen) {
  *out_index = (uint32_t)(h >> 32);
  *out_gen = (uint32_t)(h & 0xffffffffu);
}

CJ_API uint64_t cj_engine_res_alloc(cj_engine_t* e, cj_res_kind_t kind, uint32_t* out_slot) {
  if (!e) return 0;
  size_t cap = 0; cj_res_entry_t* table = table_for(e, kind, &cap);
  if (!table || cap == 0) return 0;
  for (uint32_t i = 1; i < cap; ++i) { /* start at 1 so 0 stays null */
    if (!table[i].in_use) {
      table[i].in_use = 1;
      table[i].refcount = 1;
      uint32_t newgen = table[i].generation + 1u;
      if (newgen == 0u) newgen = 1u;
      table[i].generation = newgen;
      table[i].slot = i; /* trivial mapping in stub */
      if (out_slot) *out_slot = table[i].slot;
      return make_handle(i, table[i].generation);
    }
  }
  return 0;
}

CJ_API void cj_engine_res_retain(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle) {
  if (!e || handle == 0) return;
  size_t cap = 0; cj_res_entry_t* table = table_for(e, kind, &cap);
  uint32_t idx, gen; split_handle(handle, &idx, &gen);
  if (idx >= cap) return;
  cj_res_entry_t* ent = &table[idx];
  if (!ent->in_use || ent->generation != gen) return;
  if (ent->refcount < 0xfffffff0u) ++ent->refcount;
}

CJ_API void cj_engine_res_release(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle) {
  if (!e || handle == 0) return;
  size_t cap = 0; cj_res_entry_t* table = table_for(e, kind, &cap);
  uint32_t idx, gen; split_handle(handle, &idx, &gen);
  if (idx >= cap) return;
  cj_res_entry_t* ent = &table[idx];
  if (!ent->in_use || ent->generation != gen) return;
  if (ent->refcount > 0) --ent->refcount;
  if (ent->refcount == 0) {
    ent->in_use = 0;
    ent->slot = 0;
  }
}

CJ_API uint32_t cj_engine_res_slot(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle) {
  if (!e || handle == 0) return 0;
  size_t cap = 0; cj_res_entry_t* table = table_for(e, kind, &cap);
  uint32_t idx, gen; split_handle(handle, &idx, &gen);
  if (idx >= cap) return 0;
  cj_res_entry_t* ent = &table[idx];
  if (!ent->in_use || ent->generation != gen) return 0;
  return ent->slot;
}
