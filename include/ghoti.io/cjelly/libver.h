#ifndef GHOTI_IO_CJ_LIBVER_H
#define GHOTI_IO_CJ_LIBVER_H

/**
 * The symbol namespace.
 *
 * Every exported symbol carries a per-version token so that two versions of
 * this library can be loaded into one process without the dynamic linker
 * binding one caller to the other version's implementation.  The token is
 * generated at build time from the Makefile's BRANCH.  See CONVENTIONS.md
 * section 4.
 */
#include <ghoti.io/cjelly/libver_gen.h>

/** Produce the namespaced form of an identifier. */
#define GHOTIIO_CJELLY(NAME) GHOTIIO_CJELLY_RENAME(GHOTIIO_CJELLY_NAME, _##NAME)

/** Helper.  Concatenation needs two levels of expansion. */
#define GHOTIIO_CJELLY_RENAME_INNER(a, b) a##b

/** Helper.  Concatenation needs two levels of expansion. */
#define GHOTIIO_CJELLY_RENAME(a, b) GHOTIIO_CJELLY_RENAME_INNER(a, b)

/**
 * Pack a version into the single integer Vulkan expects.
 *
 * This is the encoder only; the numbers come from libver_gen.h, which the
 * Makefile writes from MAJOR_VERSION and MINOR_VERSION.  They used to be
 * written out here as CJELLY_MAKE_VERSION(0, 0, 0), which was correct only for
 * as long as nobody bumped the Makefile: the soname, the .pc Version and the
 * install directory would all have moved while Vulkan kept being told 0.0.0.
 */
#define CJELLY_MAKE_VERSION(major, minor, patch)                               \
  ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) |                 \
      ((uint32_t)(patch)))

/** This build's version, packed for VkApplicationInfo::engineVersion. */
#define CJELLY_VERSION_UINT32                                                  \
  CJELLY_MAKE_VERSION(GHOTIIO_CJELLY_VERSION_MAJOR,                            \
      GHOTIIO_CJELLY_VERSION_MINOR, GHOTIIO_CJELLY_VERSION_PATCH)

#define CJELLY_ENGINE_NAME "Ghoti.io CJelly"

#endif // GHOTI_IO_CJ_LIBVER_H
