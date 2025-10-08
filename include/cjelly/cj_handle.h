#pragma once
#include <cjelly/cj_types.h>

typedef enum cj_handle_kind_t {
  CJ_HANDLE_TEX = 0,
  CJ_HANDLE_BUF = 1,
  CJ_HANDLE_SMP = 2
} cj_handle_kind_t;

/* Allocate/release/retain/query resource handles via the engine */
CJ_API cj_handle_t cj_handle_alloc(cj_engine_t* e, cj_handle_kind_t kind, uint32_t* out_slot);
CJ_API void        cj_handle_retain(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h);
CJ_API void        cj_handle_release(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h);
CJ_API uint32_t    cj_handle_slot(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h);


