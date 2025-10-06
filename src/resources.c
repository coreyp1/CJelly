#include <string.h>
#include <cjelly/cj_resources.h>
#include <cjelly/cj_types.h>
#include <cjelly/cj_engine.h>
#include <cjelly/engine_internal.h>

static inline cj_handle_t make_handle_from_pair(uint32_t idx, uint32_t gen) { cj_handle_t out; out.idx = idx; out.gen = gen; return out; }

CJ_API cj_handle_t cj_texture_create(cj_engine_t* e, const cj_texture_desc_t* d) {
  (void)d;
  uint32_t slot = 0; uint64_t h = cj_engine_res_alloc(e, CJ_RES_TEX, &slot);
  cj_handle_t out = { (uint32_t)(h >> 32), (uint32_t)(h & 0xffffffffu) };
  return out;
}
CJ_API void        cj_texture_retain(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_retain(e, CJ_RES_TEX, v); }
CJ_API void        cj_texture_release(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_release(e, CJ_RES_TEX, v); }
CJ_API uint32_t    cj_texture_descriptor_slot(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; return cj_engine_res_slot(e, CJ_RES_TEX, v); }

CJ_API cj_handle_t cj_buffer_create(cj_engine_t* e, const cj_buffer_desc_t* d) {
  (void)d;
  uint32_t slot = 0; uint64_t h = cj_engine_res_alloc(e, CJ_RES_BUF, &slot);
  cj_handle_t out = { (uint32_t)(h >> 32), (uint32_t)(h & 0xffffffffu) };
  return out;
}
CJ_API void        cj_buffer_retain(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_retain(e, CJ_RES_BUF, v); }
CJ_API void        cj_buffer_release(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_release(e, CJ_RES_BUF, v); }
CJ_API uint32_t    cj_buffer_descriptor_slot(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; return cj_engine_res_slot(e, CJ_RES_BUF, v); }

CJ_API cj_handle_t cj_sampler_create(cj_engine_t* e, const cj_sampler_desc_t* d) {
  (void)d;
  uint32_t slot = 0; uint64_t h = cj_engine_res_alloc(e, CJ_RES_SMP, &slot);
  cj_handle_t out = { (uint32_t)(h >> 32), (uint32_t)(h & 0xffffffffu) };
  return out;
}
CJ_API void        cj_sampler_retain(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_retain(e, CJ_RES_SMP, v); }
CJ_API void        cj_sampler_release(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; cj_engine_res_release(e, CJ_RES_SMP, v); }
CJ_API uint32_t    cj_sampler_descriptor_slot(cj_engine_t* e, cj_handle_t h) { uint64_t v = ((uint64_t)h.idx << 32) | (uint64_t)h.gen; return cj_engine_res_slot(e, CJ_RES_SMP, v); }
