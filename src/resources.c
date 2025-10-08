#include <string.h>
#include <cjelly/cj_resources.h>
#include <cjelly/cj_types.h>
#include <cjelly/cj_engine.h>
#include <cjelly/engine_internal.h>
#include <cjelly/resource_helpers_internal.h>

static inline cj_handle_t make_handle_from_pair(uint32_t idx, uint32_t gen) { cj_handle_t out; out.idx = idx; out.gen = gen; return out; }

CJ_API cj_handle_t cj_texture_create(cj_engine_t* e, const cj_texture_desc_t* d) {
  if (!e || !d) {
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  uint32_t slot = 0; 
  uint64_t h = cj_engine_res_alloc(e, CJ_RES_TEX, &slot);
  if (h == 0) {
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  // Create the actual Vulkan texture
  if (!cj_engine_create_texture(e, slot, d)) {
    cj_engine_res_release(e, CJ_RES_TEX, h);
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  cj_handle_t out = { (uint32_t)(h >> 32), (uint32_t)(h & 0xffffffffu) };
  return out;
}
CJ_API void        cj_texture_retain(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_retain(e, CJ_RES_TEX, v); }
CJ_API void        cj_texture_release(cj_engine_t* e, cj_handle_t h) { 
  uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; 
  uint32_t slot = cj_engine_res_slot(e, CJ_RES_TEX, v);
  if (slot > 0) {
    cj_engine_destroy_texture(e, slot - 1); // slot is 1-based, array is 0-based
  }
  cj_engine_res_release(e, CJ_RES_TEX, v); 
}
CJ_API uint32_t    cj_texture_descriptor_slot(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; return cj_engine_res_slot(e, CJ_RES_TEX, v); }

CJ_API cj_handle_t cj_buffer_create(cj_engine_t* e, const cj_buffer_desc_t* d) {
  if (!e || !d) {
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  uint32_t slot = 0; 
  uint64_t h = cj_engine_res_alloc(e, CJ_RES_BUF, &slot);
  if (h == 0) {
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  // Create the actual Vulkan buffer
  if (!cj_engine_create_buffer(e, slot, d)) {
    cj_engine_res_release(e, CJ_RES_BUF, h);
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  cj_handle_t out = { (uint32_t)(h >> 32), (uint32_t)(h & 0xffffffffu) };
  return out;
}
CJ_API void        cj_buffer_retain(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_retain(e, CJ_RES_BUF, v); }
CJ_API void        cj_buffer_release(cj_engine_t* e, cj_handle_t h) { 
  uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; 
  uint32_t slot = cj_engine_res_slot(e, CJ_RES_BUF, v);
  if (slot > 0) {
    cj_engine_destroy_buffer(e, slot - 1); // slot is 1-based, array is 0-based
  }
  cj_engine_res_release(e, CJ_RES_BUF, v); 
}
CJ_API uint32_t    cj_buffer_descriptor_slot(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; return cj_engine_res_slot(e, CJ_RES_BUF, v); }

CJ_API cj_handle_t cj_sampler_create(cj_engine_t* e, const cj_sampler_desc_t* d) {
  if (!e || !d) {
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  uint32_t slot = 0; 
  uint64_t h = cj_engine_res_alloc(e, CJ_RES_SMP, &slot);
  if (h == 0) {
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  // Create the actual Vulkan sampler
  if (!cj_engine_create_sampler(e, slot, d)) {
    cj_engine_res_release(e, CJ_RES_SMP, h);
    cj_handle_t null_handle = {0};
    return null_handle;
  }
  
  cj_handle_t out = { (uint32_t)(h >> 32), (uint32_t)(h & 0xffffffffu) };
  return out;
}
CJ_API void        cj_sampler_retain(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_retain(e, CJ_RES_SMP, v); }
CJ_API void        cj_sampler_release(cj_engine_t* e, cj_handle_t h) { 
  uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; 
  uint32_t slot = cj_engine_res_slot(e, CJ_RES_SMP, v);
  if (slot > 0) {
    cj_engine_destroy_sampler(e, slot - 1); // slot is 1-based, array is 0-based
  }
  cj_engine_res_release(e, CJ_RES_SMP, v); 
}
CJ_API uint32_t    cj_sampler_descriptor_slot(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; return cj_engine_res_slot(e, CJ_RES_SMP, v); }
