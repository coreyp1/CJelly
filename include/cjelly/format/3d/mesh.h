/**
 * @file mesh.h
 * @brief Turning a parsed OBJ into something a GPU can draw.
 *
 * The model library reports an OBJ the way the file describes it: separate
 * arrays for positions, texture coordinates and normals, and faces that index
 * all three independently, with up to four corners inline and the rest in an
 * overflow array. A graphics pipeline wants none of that. It wants one
 * interleaved vertex buffer, one index buffer, and triangles.
 *
 * This is the translation, and it is deliberately free of Vulkan: it can be
 * tested without a GPU or a display, which is most of the reason it is a
 * separate file.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef CJELLY_FORMAT_3D_MESH_H
#define CJELLY_FORMAT_3D_MESH_H

#include <stdint.h>

#include <cjelly/macros.h>

#include <ghoti.io/model/model.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @brief Error codes for mesh building.
 */
typedef enum {
  CJELLY_MODEL_MESH_SUCCESS = 0,       /**< No error */
  CJELLY_MODEL_MESH_ERR_INVALID,       /**< NULL or otherwise unusable input */
  CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY, /**< Allocation failure */
  CJELLY_MODEL_MESH_ERR_LOAD,          /**< The OBJ file could not be read */
  CJELLY_MODEL_MESH_ERR_EMPTY          /**< Nothing drawable in the file */
} CJellyModelMeshError;

/**
 * @brief One vertex, laid out the way the pipeline consumes it.
 *
 * The order and offsets here are what `model.vert` declares; changing one
 * without the other is a silent rendering fault rather than a compile error.
 */
typedef struct CJellyModelVertex {
  float position[3]; /**< Object-space position */
  float normal[3];   /**< Unit normal, generated when the file had none */
  float texcoord[2]; /**< Texture coordinate, v flipped for Vulkan */
} CJellyModelVertex;

/**
 * @brief A triangulated, GPU-ready mesh.
 */
typedef struct CJellyModelMesh {
  CJellyModelVertex * vertices; /**< Interleaved vertex data */
  uint32_t vertex_count;        /**< Number of vertices */

  uint32_t * indices;    /**< Triangle indices, three per triangle */
  uint32_t index_count;  /**< Number of indices; always a multiple of three */

  float bounds_min[3]; /**< Minimum corner of the bounding box */
  float bounds_max[3]; /**< Maximum corner of the bounding box */
  float center[3];     /**< Centre of the bounding box */
  float radius;        /**< Distance from the centre to the furthest vertex */

  /**
   * Whether the normals were computed rather than read.
   *
   * Worth knowing: a file with no `vn` lines - the Stanford bunny, for one -
   * renders unlit and therefore black without this step, which looks like a
   * pipeline fault rather than missing data.
   */
  int generated_normals;

  /**
   * Faces skipped because they named a vertex that does not exist.
   *
   * The OBJ parser does not reject an out-of-range index, because readers
   * differ on how to treat one, so the check has to happen here. Skipping the
   * face is the conservative choice: the alternative is reading past the end
   * of the position array.
   */
  uint32_t dropped_faces;
} CJellyModelMesh;

/**
 * @brief Build a mesh from an already-parsed OBJ.
 *
 * @param obj The parsed model.
 * @param out_mesh Receives the mesh on success.
 * @return CJELLY_MODEL_MESH_SUCCESS, or an error code.
 */
CJellyModelMeshError cjelly_model_mesh_from_obj(
    const GMDL_Obj * obj, CJellyModelMesh ** out_mesh);

/**
 * @brief Read an OBJ file and build a mesh from it.
 *
 * @param path Path to the OBJ file.
 * @param out_mesh Receives the mesh on success.
 * @return CJELLY_MODEL_MESH_SUCCESS, or an error code.
 */
CJellyModelMeshError cjelly_model_mesh_load(
    const char * path, CJellyModelMesh ** out_mesh);

/**
 * @brief Free a mesh. NULL is ignored.
 *
 * @param mesh The mesh.
 */
void cjelly_model_mesh_free(CJellyModelMesh * mesh);

/**
 * @brief A human-readable description of an error code.
 *
 * @param err The error code.
 * @return A constant string, never NULL.
 */
const char * cjelly_model_mesh_strerror(CJellyModelMeshError err);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // CJELLY_FORMAT_3D_MESH_H
