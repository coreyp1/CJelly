/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2025-2026 Corey Pennycuff
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
 * @file mesh.c
 *
 * Turning a parsed OBJ into a triangulated, interleaved mesh.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/array.h>

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/format/3d/mesh.h>

/** One corner of a face, as the OBJ describes it. */
typedef struct {
  int32_t vertex;
  int32_t texcoord;
  int32_t normal;
} mesh_corner_t;

/**
 * Read corner `j` of a face.
 *
 * The first four corners live in the face itself and the rest in its overflow
 * array, which is the one place this translation has to know about the OBJ
 * structure's shape.
 */
static mesh_corner_t mesh_face_corner(const GMDL_Obj_Face * face, size_t j) {
  mesh_corner_t corner = {-1, -1, -1};
  if (j < 4) {
    corner.vertex = face->vertex[j];
    corner.texcoord = face->texcoord[j];
    corner.normal = face->normal[j];
  }
  else if (face->overflow) {
    const GMDL_Obj_Face_Overflow * extra = &face->overflow[j - 4];
    corner.vertex = extra->vertex;
    corner.texcoord = extra->texcoord;
    corner.normal = extra->normal;
  }
  return corner;
}

/** Whether every corner of a face names a position that exists. */
static bool mesh_face_is_in_range(
    const GMDL_Obj_Face * face, size_t vertex_count) {
  if (face->count < 3) {
    return false;
  }
  if (face->count > 4 && !face->overflow) {
    // The count claims corners that were never stored.
    return false;
  }
  for (size_t j = 0; j < face->count; j++) {
    mesh_corner_t corner = mesh_face_corner(face, j);
    if (corner.vertex < 0 || (size_t)corner.vertex >= vertex_count) {
      return false;
    }
  }
  return true;
}

static void mesh_subtract(const float * a, const float * b, float * out) {
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
  out[2] = a[2] - b[2];
}

static void mesh_cross(const float * a, const float * b, float * out) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

/** Normalize in place, leaving a zero-length vector alone. */
static void mesh_normalize(float * v) {
  float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (length > 1e-20f) {
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
  }
}

/**
 * Accumulate a face's geometric normal onto each position it touches.
 *
 * The cross product is left unnormalized on purpose, so a large triangle
 * contributes more than a small one - the usual area weighting, which gives a
 * better result on meshes with uneven tessellation than averaging unit
 * normals does.
 */
static void mesh_accumulate_normal(const GMDL_Obj * obj,
    const GMDL_Obj_Face * face, float * accumulated) {
  mesh_corner_t a = mesh_face_corner(face, 0);
  mesh_corner_t b = mesh_face_corner(face, 1);
  mesh_corner_t c = mesh_face_corner(face, 2);

  const float * pa = &obj->vertices[a.vertex].x;
  const float * pb = &obj->vertices[b.vertex].x;
  const float * pc = &obj->vertices[c.vertex].x;

  float ab[3];
  float ac[3];
  float n[3];
  mesh_subtract(pb, pa, ab);
  mesh_subtract(pc, pa, ac);
  mesh_cross(ab, ac, n);

  for (size_t j = 0; j < face->count; j++) {
    mesh_corner_t corner = mesh_face_corner(face, j);
    float * slot = &accumulated[(size_t)corner.vertex * 3];
    slot[0] += n[0];
    slot[1] += n[1];
    slot[2] += n[2];
  }
}

CJellyModelMeshError cjelly_model_mesh_from_obj(
    const GMDL_Obj * obj, const cj_allocator_t * allocator,
    CJellyModelMesh ** out_mesh) {
  if (!out_mesh) {
    return CJELLY_MODEL_MESH_ERR_INVALID;
  }
  *out_mesh = NULL;
  if (!obj) {
    return CJELLY_MODEL_MESH_ERR_INVALID;
  }
  if (obj->vertex_count == 0 || obj->face_count == 0) {
    return CJELLY_MODEL_MESH_ERR_EMPTY;
  }

  CJellyModelMeshError err = CJELLY_MODEL_MESH_SUCCESS;
  float * accumulated = NULL;
  GCU_Array vertices;
  GCU_Array indices;
  bool vertices_ready = false;
  bool indices_ready = false;

  // Whether the file supplies normals at all. A file without them - the
  // Stanford bunny, for one - renders unlit and therefore black unless they
  // are computed here.
  bool have_normals = obj->normal_count > 0;
  if (!have_normals) {
    accumulated = gcu_allocator_calloc(
        allocator, obj->vertex_count * 3, sizeof(float));
    if (!accumulated) {
      return CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < obj->face_count; i++) {
      if (mesh_face_is_in_range(&obj->faces[i], obj->vertex_count)) {
        mesh_accumulate_normal(obj, &obj->faces[i], accumulated);
      }
    }
    for (size_t i = 0; i < obj->vertex_count; i++) {
      mesh_normalize(&accumulated[i * 3]);
    }
  }

  if (!gcu_array_create_in_place(
          &vertices, sizeof(CJellyModelVertex), obj->face_count * 3,
          allocator)) {
    err = CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  vertices_ready = true;
  if (!gcu_array_create_in_place(
          &indices, sizeof(uint32_t), obj->face_count * 3, allocator)) {
    err = CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  indices_ready = true;

  uint32_t dropped = 0;

  for (size_t i = 0; i < obj->face_count; i++) {
    const GMDL_Obj_Face * face = &obj->faces[i];
    if (!mesh_face_is_in_range(face, obj->vertex_count)) {
      dropped++;
      continue;
    }

    // Triangulate as a fan from the first corner. That is correct for convex
    // faces and is what OBJ readers conventionally do; a concave n-gon would
    // need a real triangulator, and in practice exporters emit triangles and
    // quads.
    uint32_t first_index = (uint32_t)gcu_array_count(&vertices);
    for (size_t j = 0; j < face->count; j++) {
      mesh_corner_t corner = mesh_face_corner(face, j);

      CJellyModelVertex * vertex =
          (CJellyModelVertex *)gcu_array_emplace(&vertices);
      if (!vertex) {
        err = CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY;
        goto cleanup;
      }
      memset(vertex, 0, sizeof(*vertex));

      const GMDL_Obj_Vertex * position = &obj->vertices[corner.vertex];
      vertex->position[0] = position->x;
      vertex->position[1] = position->y;
      vertex->position[2] = position->z;

      if (corner.normal >= 0 && (size_t)corner.normal < obj->normal_count) {
        const GMDL_Obj_Normal * normal = &obj->normals[corner.normal];
        vertex->normal[0] = normal->x;
        vertex->normal[1] = normal->y;
        vertex->normal[2] = normal->z;
        mesh_normalize(vertex->normal);
      }
      else if (accumulated) {
        const float * slot = &accumulated[(size_t)corner.vertex * 3];
        vertex->normal[0] = slot[0];
        vertex->normal[1] = slot[1];
        vertex->normal[2] = slot[2];
      }
      else {
        // The file has normals but this corner did not name one.
        vertex->normal[1] = 1.0f;
      }

      if (corner.texcoord >= 0
          && (size_t)corner.texcoord < obj->texcoord_count) {
        const GMDL_Obj_TexCoord * uv = &obj->texcoords[corner.texcoord];
        vertex->texcoord[0] = uv->u;
        // OBJ measures v from the bottom, Vulkan samples from the top.
        vertex->texcoord[1] = 1.0f - uv->v;
      }
    }

    for (size_t j = 1; j + 1 < face->count; j++) {
      uint32_t triangle[3] = {first_index, first_index + (uint32_t)j,
          first_index + (uint32_t)j + 1};
      if (!gcu_array_append(&indices, &triangle[0])
          || !gcu_array_append(&indices, &triangle[1])
          || !gcu_array_append(&indices, &triangle[2])) {
        err = CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY;
        goto cleanup;
      }
    }
  }

  if (gcu_array_count(&indices) == 0) {
    err = CJELLY_MODEL_MESH_ERR_EMPTY;
    goto cleanup;
  }

  {
    CJellyModelMesh * mesh =
        gcu_allocator_calloc(allocator, 1, sizeof(CJellyModelMesh));
    if (!mesh) {
      err = CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY;
      goto cleanup;
    }

    (void)gcu_array_shrink_to_fit(&vertices);
    (void)gcu_array_shrink_to_fit(&indices);
    size_t vertex_count = 0;
    size_t index_count = 0;
    mesh->vertices =
        (CJellyModelVertex *)gcu_array_steal(&vertices, &vertex_count);
    mesh->indices = (uint32_t *)gcu_array_steal(&indices, &index_count);
    mesh->vertex_count = (uint32_t)vertex_count;
    mesh->index_count = (uint32_t)index_count;
    mesh->allocator = allocator;
    mesh->generated_normals = have_normals ? 0 : 1;
    mesh->dropped_faces = dropped;

    // Bounds, and a bounding sphere the caller can frame a camera with.
    mesh->bounds_min[0] = mesh->bounds_max[0] = mesh->vertices[0].position[0];
    mesh->bounds_min[1] = mesh->bounds_max[1] = mesh->vertices[0].position[1];
    mesh->bounds_min[2] = mesh->bounds_max[2] = mesh->vertices[0].position[2];
    for (uint32_t i = 1; i < mesh->vertex_count; i++) {
      for (int axis = 0; axis < 3; axis++) {
        float value = mesh->vertices[i].position[axis];
        if (value < mesh->bounds_min[axis]) {
          mesh->bounds_min[axis] = value;
        }
        if (value > mesh->bounds_max[axis]) {
          mesh->bounds_max[axis] = value;
        }
      }
    }
    // The midpoint, by whichever of the two spellings cannot leave the box
    // on these inputs. Neither works alone, and both failures are reachable
    // from ordinary OBJ text:
    //
    //   (min + max) * 0.5f   overflows when the bounds are large and share a
    //                        sign - three vertices at -3.4e38 give an
    //                        infinite centre for a finite box.
    //   0.5f*min + 0.5f*max  underflows when they are denormal, halving
    //                        1.4e-45 to zero and putting the centre below a
    //                        box that is entirely above it.
    //
    // When the signs differ, |min + max| is at most the larger magnitude, so
    // the first form cannot overflow. When they agree, |max - min| is at
    // most the larger magnitude, so the second cannot, and adding back onto
    // min keeps the result between them.
    for (int axis = 0; axis < 3; axis++) {
      float lo = mesh->bounds_min[axis];
      float hi = mesh->bounds_max[axis];
      mesh->center[axis] = ((lo < 0.0f) == (hi < 0.0f))
          ? lo + (hi - lo) * 0.5f
          : (lo + hi) * 0.5f;
    }
    mesh->radius = 0.0f;
    for (uint32_t i = 0; i < mesh->vertex_count; i++) {
      float d[3];
      mesh_subtract(mesh->vertices[i].position, mesh->center, d);
      float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (distance > mesh->radius) {
        mesh->radius = distance;
      }
    }

    gcu_array_destroy_in_place(&vertices);
    gcu_array_destroy_in_place(&indices);
    gcu_allocator_free(allocator, accumulated);
    *out_mesh = mesh;
    return CJELLY_MODEL_MESH_SUCCESS;
  }

cleanup:
  if (vertices_ready) {
    gcu_array_destroy_in_place(&vertices);
  }
  if (indices_ready) {
    gcu_array_destroy_in_place(&indices);
  }
  gcu_allocator_free(allocator, accumulated);
  return err;
}

CJellyModelMeshError cjelly_model_mesh_load(const char * path,
    const cj_allocator_t * allocator, CJellyModelMesh ** out_mesh) {
  if (!out_mesh) {
    return CJELLY_MODEL_MESH_ERR_INVALID;
  }
  *out_mesh = NULL;
  if (!path) {
    return CJELLY_MODEL_MESH_ERR_INVALID;
  }

  GMDL_Obj * obj = NULL;
  /* The model library takes the same allocator: GMDL_Allocator and
   * cj_allocator_t are both cutil's, so one allocator covers the OBJ parse
   * as well as the mesh built from it. */
  GMDL_Result result = gmdl_obj_load_file(path, NULL, allocator, &obj);
  if (result != GMDL_OK) {
    return result == GMDL_ERR_OOM ? CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY
                                  : CJELLY_MODEL_MESH_ERR_LOAD;
  }

  CJellyModelMeshError err =
      cjelly_model_mesh_from_obj(obj, allocator, out_mesh);
  gmdl_obj_free(obj);
  return err;
}

void cjelly_model_mesh_free(CJellyModelMesh * mesh) {
  if (!mesh) {
    return;
  }
  const cj_allocator_t * allocator = mesh->allocator;
  gcu_allocator_free(allocator, mesh->vertices);
  gcu_allocator_free(allocator, mesh->indices);
  gcu_allocator_free(allocator, mesh);
}

const char * cjelly_model_mesh_strerror(CJellyModelMeshError err) {
  switch (err) {
    case CJELLY_MODEL_MESH_SUCCESS:
      return "No error";
    case CJELLY_MODEL_MESH_ERR_INVALID:
      return "Invalid argument";
    case CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY:
      return "Out of memory";
    case CJELLY_MODEL_MESH_ERR_LOAD:
      return "The OBJ file could not be read";
    case CJELLY_MODEL_MESH_ERR_EMPTY:
      return "The model has no drawable geometry";
    default:
      return "Unknown error";
  }
}
