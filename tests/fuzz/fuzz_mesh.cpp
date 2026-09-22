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
 * LibFuzzer harness for the OBJ-document path: bytes in, mesh out.
 *
 * This is what cjelly_model_mesh_load() does with the file I/O removed - the
 * parse belongs to Ghoti.io Model and is fuzzed there, so what this adds is
 * everything after it. That post-processing is cjelly's own: range checking
 * the face indices, triangulating as a fan, generating normals when the file
 * has none, flipping V, and computing the bounds.
 *
 * Feeding it text rather than a struct keeps it honest about reachability -
 * every input here is a file somebody could hand the library. The companion
 * harness, fuzz_mesh_struct, gives up that guarantee on purpose to reach the
 * states the parser will not produce.
 *
 * Build: make fuzz-mesh      Run: make fuzz-run-mesh FUZZ_TIME=300
 */

#include <cstddef>
#include <cstdint>

#include <ghoti.io/cjelly/format/3d/mesh.h>
#include <ghoti.io/model/obj.h>
#include <ghoti.io/model/stream.h>

#include "fuzz_mesh_invariants.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (data == nullptr || size == 0) {
    return 0;
  }

  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(data, size, &stream) != GMDL_OK
      || stream == nullptr) {
    return 0;
  }

  GMDL_Obj * obj = nullptr;
  GMDL_Result r = gmdl_obj_load(stream, nullptr, nullptr, &obj);
  gmdl_stream_destroy(stream);

  if (r != GMDL_OK || obj == nullptr) {
    // Expected for most inputs: malformed, truncated, or over a limit.
    return 0;
  }

  CJellyModelMesh * mesh = nullptr;
  CJellyModelMeshError err = cjelly_model_mesh_from_obj(obj, nullptr, &mesh);

  if (err == CJELLY_MODEL_MESH_SUCCESS) {
    fuzz_check_mesh(mesh, obj->face_count);
  }
  else {
    // A failure has to leave nothing behind for the caller to free, because
    // the caller has no way to tell a partly built mesh from no mesh.
    FUZZ_CHECK(mesh == nullptr, "error %d returned a mesh", (int)err);
  }

  cjelly_model_mesh_free(mesh);
  gmdl_obj_free(obj);
  return 0;
}
