/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io CJelly.
 *
 * Ghoti.io CJelly is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io CJelly is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * What a successfully built mesh has to be true of, whatever produced it.
 *
 * The sanitizers catch the reads and writes. These catch the answers: a mesh
 * can be built without touching memory it should not and still hand the
 * renderer an index past the end of its own vertex array, which becomes a
 * GPU fault a long way from here with nothing pointing back.
 *
 * Every check aborts rather than returning, because libFuzzer records a
 * crash and keeps the input. A check that printed and continued would be
 * discovered by nobody.
 */

#ifndef CJELLY_TESTS_FUZZ_MESH_INVARIANTS_H
#define CJELLY_TESTS_FUZZ_MESH_INVARIANTS_H

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <ghoti.io/cjelly/format/3d/mesh.h>

#define FUZZ_CHECK(cond, ...)                                                 \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::fprintf(stderr, "invariant failed: %s\n  ", #cond);                \
      std::fprintf(stderr, __VA_ARGS__);                                      \
      std::fprintf(stderr, "\n");                                             \
      std::abort();                                                           \
    }                                                                         \
  } while (0)

/**
 * @param mesh   The mesh returned with CJELLY_FORMAT_MESH_SUCCESS.
 * @param faces  The face count of the document it came from, so that
 *   dropped_faces can be checked against something real.
 */
static inline void fuzz_check_mesh(
    const CJellyModelMesh * mesh, size_t faces) {
  FUZZ_CHECK(mesh != nullptr, "success returned no mesh");

  // The bounds code reads vertices[0] unconditionally, so a success with no
  // vertices would already have been a read of nothing.
  FUZZ_CHECK(mesh->vertex_count > 0, "success with no vertices");
  FUZZ_CHECK(mesh->vertices != nullptr, "vertex_count %u with a null array",
      mesh->vertex_count);

  FUZZ_CHECK(mesh->index_count > 0, "success with no indices");
  FUZZ_CHECK(mesh->indices != nullptr, "index_count %u with a null array",
      mesh->index_count);
  FUZZ_CHECK(mesh->index_count % 3 == 0, "index_count %u is not a multiple of 3",
      mesh->index_count);

  // The one that matters most. Every index is a subscript the renderer will
  // apply to the vertex array without checking it again.
  for (uint32_t i = 0; i < mesh->index_count; i++) {
    FUZZ_CHECK(mesh->indices[i] < mesh->vertex_count,
        "indices[%u] = %u, vertex_count = %u", i, mesh->indices[i],
        mesh->vertex_count);
  }

  FUZZ_CHECK((size_t)mesh->dropped_faces <= faces,
      "dropped %u of %zu faces", mesh->dropped_faces, faces);

  // Bounds are only ordered when they are numbers at all. A document may
  // carry a non-finite position - `v nan nan nan` parses - and every
  // comparison against a NaN is false, so the bounds stay at whatever
  // vertices[0] held and the radius follows. That is propagation, not
  // corruption, and it is the caller's to reject; asserting an order here
  // would be asserting a policy this library has not chosen.
  for (int axis = 0; axis < 3; axis++) {
    if (std::isfinite(mesh->bounds_min[axis])
        && std::isfinite(mesh->bounds_max[axis])) {
      FUZZ_CHECK(mesh->bounds_min[axis] <= mesh->bounds_max[axis],
          "axis %d: min %g > max %g", axis, (double)mesh->bounds_min[axis],
          (double)mesh->bounds_max[axis]);
      FUZZ_CHECK(mesh->center[axis] >= mesh->bounds_min[axis]
              && mesh->center[axis] <= mesh->bounds_max[axis],
          "axis %d: centre %g outside [%g, %g]", axis,
          (double)mesh->center[axis], (double)mesh->bounds_min[axis],
          (double)mesh->bounds_max[axis]);
    }
  }
  if (std::isfinite(mesh->radius)) {
    FUZZ_CHECK(mesh->radius >= 0.0f, "negative radius %g",
        (double)mesh->radius);
  }
}

#endif // CJELLY_TESTS_FUZZ_MESH_INVARIANTS_H
