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

//-----------------------------------------------------------------------------
// The Vulkan boundary
//-----------------------------------------------------------------------------
//
// These three exist solely to fill in VkApplicationInfo. Everything else uses
// CJ_VERSION_NUMBER below. They carry VK in the name because the difference
// matters: this packing is not the suite's.

/**
 * Pack a version the way Vulkan does.
 *
 * VkApplicationInfo::engineVersion is decoded by the loader and by tools such
 * as vulkaninfo and RenderDoc with VK_VERSION_MAJOR and friends, so it has to
 * be in their layout to display correctly. Use CJ_MAKE_VERSION for anything
 * that is not handed to Vulkan.
 *
 * The numbers come from libver_gen.h, not from here.
 */
#define CJ_VK_MAKE_VERSION(major, minor, patch)                                \
  ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) |                 \
      ((uint32_t)(patch)))

/** This build's version, for VkApplicationInfo::engineVersion. */
#define CJ_VK_ENGINE_VERSION                                                   \
  CJ_VK_MAKE_VERSION(GHOTIIO_CJELLY_VERSION_MAJOR,                             \
      GHOTIIO_CJELLY_VERSION_MINOR, GHOTIIO_CJELLY_VERSION_PATCH)

/** This engine's name, for VkApplicationInfo::pEngineName. */
#define CJ_VK_ENGINE_NAME "Ghoti.io CJelly"


//-----------------------------------------------------------------------------
// Version
//-----------------------------------------------------------------------------
//
// The numbers come from libver_gen.h, which the Makefile writes from
// MAJOR_VERSION and MINOR_VERSION. Writing them out here instead is correct
// only until someone bumps the Makefile, at which point the soname, the .pc
// Version: and the install directory all move and these do not.

/** This build's major version. */
#define CJ_VERSION_MAJOR GHOTIIO_CJELLY_VERSION_MAJOR
/** This build's minor version. */
#define CJ_VERSION_MINOR GHOTIIO_CJELLY_VERSION_MINOR
/** This build's patch version. */
#define CJ_VERSION_PATCH GHOTIIO_CJELLY_VERSION_PATCH
/** This build's version as a string, e.g. "1.2.3" or "1.2.3-dev". */
#define CJ_VERSION_STRING GHOTIIO_CJELLY_VERSION

/**
 * Pack a version into one comparable integer, one byte per component.
 *
 * This is libcurl's LIBCURL_VERSION_NUM layout, which is the common spelling
 * across C libraries: 1.2.3 becomes 0x010203, and a plain `<` compares two
 * versions correctly. Every library in the suite uses it, so a consumer
 * checking one checks them all the same way.
 */
#define CJ_MAKE_VERSION(major, minor, patch)                                  \
  ((((unsigned)(major)) << 16) | (((unsigned)(minor)) << 8) |                  \
      ((unsigned)(patch)))

/** This build's version, packed. Compare against CJ_MAKE_VERSION(1, 2, 3). */
#define CJ_VERSION_NUMBER                                                     \
  CJ_MAKE_VERSION(CJ_VERSION_MAJOR, CJ_VERSION_MINOR, CJ_VERSION_PATCH)

#endif // GHOTI_IO_CJ_LIBVER_H
