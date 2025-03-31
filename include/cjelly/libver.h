#ifndef CJELLY_LIBVER_H
#define CJELLY_LIBVER_H

#define CJELLY_MAKE_VERSION(major, minor, patch)                               \
  ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) |                 \
      ((uint32_t)(patch)))

#define CJELLY_VERSION_STRING "0.0.0"
#define CJELLY_VERSION_UINT32 CJELLY_MAKE_VERSION(0, 0, 0)
#define CJELLY_ENGINE_NAME "Ghoti.io CJelly"

#endif // CJELLY_LIBVER_H
