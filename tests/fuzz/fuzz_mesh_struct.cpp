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
 * LibFuzzer harness for cjelly_model_mesh_from_obj() called with a document
 * built by hand rather than parsed.
 *
 * cjelly_model_mesh_from_obj() is public API. A consumer of a generic
 * framework can build a GMDL_Obj from a format of its own - a glTF import, a
 * procedural generator, a network message - and hand it over, so the OBJ
 * parser is not a gate this function sits behind. Ghoti.io Model says as much
 * about its own output: "A file may still name an element that does not
 * exist... a consumer that indexes the arrays directly must range check
 * first." CJelly is that consumer, and this is the harness for the check.
 *
 * The parser will not produce most of what is generated here. It resolves
 * relative indices, applies limits, and never emits a face whose `count`
 * claims corners it did not store. Each of those is a state this reaches
 * directly, which is the point: the guard has to hold for callers that are
 * not the parser.
 *
 * What this harness will NOT do is lie about its own arrays. Every count
 * matches an allocation of exactly that many elements, because a GMDL_Obj
 * whose `vertex_count` exceeds its `vertices` array is a broken caller, and
 * a crash from one would be this file's bug rather than the library's.
 *
 * Build: make fuzz-mesh-struct   Run: make fuzz-run-mesh-struct FUZZ_TIME=300
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <ghoti.io/cjelly/format/3d/mesh.h>
#include <ghoti.io/model/obj.h>

#include "fuzz_mesh_invariants.h"

namespace {

/** A cursor over the fuzzer's bytes that reads zeroes once they run out. */
class Reader {
public:
  Reader(const uint8_t * d, size_t n) : data_(d), size_(n) {}

  uint8_t u8() { return pos_ < size_ ? data_[pos_++] : 0; }

  uint32_t u32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
      v = (v << 8) | u8();
    }
    return v;
  }

  /**
   * A float, with the awkward ones reachable without having to be guessed.
   *
   * Drawing 32 bits uniformly makes an infinity or a denormal vanishingly
   * rare, and those are where the normalisation, the bounds and the radius
   * each behave differently.
   */
  float f32() {
    switch (u8() & 0x0f) {
      case 0: return std::numeric_limits<float>::quiet_NaN();
      case 1: return std::numeric_limits<float>::infinity();
      case 2: return -std::numeric_limits<float>::infinity();
      case 3: return 0.0f;
      case 4: return -0.0f;
      case 5: return std::numeric_limits<float>::denorm_min();
      case 6: return std::numeric_limits<float>::max();
      case 7: return -std::numeric_limits<float>::max();
      default: {
        uint32_t bits = u32();
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
      }
    }
  }

private:
  const uint8_t * data_;
  size_t size_;
  size_t pos_ = 0;
};

/**
 * An index into an array of @p count elements - usually valid, sometimes not.
 *
 * -1 is the documented spelling for "this corner named no such element", so
 * it has to be common. The rest are the ways a hostile or buggy producer gets
 * it wrong, with one-past-the-end and the two extremes called out because a
 * bound written `<=` rather than `<`, or a signed comparison that wraps, fails
 * on exactly those and on nothing else.
 */
int32_t fuzz_index(Reader & r, size_t count) {
  switch (r.u8() & 7) {
    case 0: return -1;
    case 1: return (int32_t)r.u32();
    case 2: return -(int32_t)(r.u8()) - 1;
    case 3: return (int32_t)count;
    case 4: return std::numeric_limits<int32_t>::max();
    case 5: return std::numeric_limits<int32_t>::min();
    default: return count > 0 ? (int32_t)(r.u32() % count) : 0;
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (data == nullptr || size < 4) {
    return 0;
  }
  Reader r(data, size);

  const size_t vertex_count = 1 + (r.u8() % 64);
  const size_t texcoord_count = r.u8() % 64;
  const size_t normal_count = r.u8() % 64;
  const size_t face_count = 1 + (r.u8() % 64);

  std::vector<GMDL_Obj_Vertex> vertices(vertex_count);
  for (auto & v : vertices) {
    v.x = r.f32();
    v.y = r.f32();
    v.z = r.f32();
  }

  std::vector<GMDL_Obj_TexCoord> texcoords(texcoord_count);
  for (auto & t : texcoords) {
    t.u = r.f32();
    t.v = r.f32();
  }

  std::vector<GMDL_Obj_Normal> normals(normal_count);
  for (auto & n : normals) {
    n.x = r.f32();
    n.y = r.f32();
    n.z = r.f32();
  }

  // Held alongside the faces so the overflow arrays outlive the call.
  std::vector<std::vector<GMDL_Obj_Face_Overflow>> overflow(face_count);
  std::vector<GMDL_Obj_Face> faces(face_count);

  for (size_t i = 0; i < face_count; i++) {
    GMDL_Obj_Face & f = faces[i];
    std::memset(&f, 0, sizeof(f));

    // 0 to 7, so that a degenerate face of fewer than three corners and the
    // overflow path beyond four are both reached.
    f.count = r.u8() % 8;
    f.material_index = -1;
    f.smoothing_group = 0;

    for (int k = 0; k < 4; k++) {
      f.vertex[k] = fuzz_index(r, vertex_count);
      f.texcoord[k] = fuzz_index(r, texcoord_count);
      f.normal[k] = fuzz_index(r, normal_count);
    }

    if (f.count > 4) {
      // Half the time the overflow array is absent while the count says it is
      // there. That is not a state the parser reaches, and it is exactly what
      // mesh_face_is_in_range() has a branch for; leaving it out would leave
      // that branch fuzzed by nothing.
      if (r.u8() & 1) {
        overflow[i].resize(f.count - 4);
        for (auto & o : overflow[i]) {
          o.vertex = fuzz_index(r, vertex_count);
          o.texcoord = fuzz_index(r, texcoord_count);
          o.normal = fuzz_index(r, normal_count);
        }
        f.overflow = overflow[i].data();
      }
      else {
        f.overflow = nullptr;
      }
    }
  }

  GMDL_Obj obj;
  std::memset(&obj, 0, sizeof(obj));
  obj.vertices = vertices.data();
  obj.vertex_count = vertex_count;
  obj.texcoords = texcoord_count ? texcoords.data() : nullptr;
  obj.texcoord_count = texcoord_count;
  obj.normals = normal_count ? normals.data() : nullptr;
  obj.normal_count = normal_count;
  obj.faces = faces.data();
  obj.face_count = face_count;

  CJellyModelMesh * mesh = nullptr;
  CJellyModelMeshError err = cjelly_model_mesh_from_obj(&obj, nullptr, &mesh);

  if (err == CJELLY_MODEL_MESH_SUCCESS) {
    fuzz_check_mesh(mesh, face_count);
  }
  else {
    FUZZ_CHECK(mesh == nullptr, "error %d returned a mesh", (int)err);
  }

  cjelly_model_mesh_free(mesh);
  // Deliberately no gmdl_obj_free(): every array above is this harness's, and
  // obj.allocator is NULL. Freeing it would hand stack-owned memory to an
  // allocator.
  return 0;
}
