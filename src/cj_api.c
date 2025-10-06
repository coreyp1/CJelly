#include <stdint.h>
#include <cjelly/cj_result.h>
#include <cjelly/cj_version.h>

CJ_API const char* cj_result_str(cj_result_t r) {
  switch (r) {
    case CJ_SUCCESS: return "CJ_SUCCESS";
    case CJ_E_UNKNOWN: return "CJ_E_UNKNOWN";
    case CJ_E_INVALID_ARGUMENT: return "CJ_E_INVALID_ARGUMENT";
    case CJ_E_OUT_OF_MEMORY: return "CJ_E_OUT_OF_MEMORY";
    case CJ_E_NOT_READY: return "CJ_E_NOT_READY";
    case CJ_E_TIMEOUT: return "CJ_E_TIMEOUT";
    case CJ_E_DEVICE_LOST: return "CJ_E_DEVICE_LOST";
    case CJ_E_SURFACE_LOST: return "CJ_E_SURFACE_LOST";
    case CJ_E_OUT_OF_DATE: return "CJ_E_OUT_OF_DATE";
    case CJ_E_UNSUPPORTED: return "CJ_E_UNSUPPORTED";
    case CJ_E_ALREADY_EXISTS: return "CJ_E_ALREADY_EXISTS";
    case CJ_E_NOT_FOUND: return "CJ_E_NOT_FOUND";
    case CJ_E_BUSY: return "CJ_E_BUSY";
    default: return "CJ_E_???";
  }
}

CJ_API uint32_t cj_version_runtime(void) {
  return CJ_HEADER_VERSION;
}
