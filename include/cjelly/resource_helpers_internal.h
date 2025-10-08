#pragma once
#include <cjelly/cj_resources.h>

/* Forward declaration to avoid circular dependency */
typedef struct cj_engine_t cj_engine_t;

/* Vulkan resource creation helpers */
CJ_API int cj_engine_create_texture(cj_engine_t* e, uint32_t slot, const cj_texture_desc_t* desc);
CJ_API int cj_engine_create_buffer(cj_engine_t* e, uint32_t slot, const cj_buffer_desc_t* desc);
CJ_API int cj_engine_create_sampler(cj_engine_t* e, uint32_t slot, const cj_sampler_desc_t* desc);
CJ_API void cj_engine_destroy_texture(cj_engine_t* e, uint32_t slot);
CJ_API void cj_engine_destroy_buffer(cj_engine_t* e, uint32_t slot);
CJ_API void cj_engine_destroy_sampler(cj_engine_t* e, uint32_t slot);
