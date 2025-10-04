#ifndef CJELLY_FORMAT_3D_OBJ_H
#define CJELLY_FORMAT_3D_OBJ_H

#include <cjelly/macros.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#define CJELLY_FORMAT_3D_OBJ_MAX_NAME_LENGTH 128

/**
 * @file obj.h
 * @brief Header file for loading and parsing OBJ files.
 *
 * This file defines the structures and functions needed to parse and represent
 * 3D models in the OBJ file format with standardized error handling.
 */

/**
 * @brief Enumeration of error codes for the OBJ parser.
 */
typedef enum {
  CJELLY_FORMAT_3D_OBJ_SUCCESS = 0,         /**< No error */
  CJELLY_FORMAT_3D_OBJ_ERR_FILE_NOT_FOUND,  /**< Unable to open the file */
  CJELLY_FORMAT_3D_OBJ_ERR_OUT_OF_MEMORY,   /**< Memory allocation failure */
  CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT,  /**< File contains an invalid format */
  CJELLY_FORMAT_3D_OBJ_ERR_IO               /**< I/O error while reading/writing the file */
} CJellyFormat3dObjError;

/**
 * @brief Structure representing a 3D vertex.
 */
struct CJellyFormat3dObjVertex {
  float x; /**< X coordinate */
  float y; /**< Y coordinate */
  float z; /**< Z coordinate */
};

/**
 * @brief Structure representing a 2D texture coordinate.
 */
struct CJellyFormat3dObjTexCoord {
  float u; /**< U coordinate */
  float v; /**< V coordinate */
};

/**
 * @brief Structure representing a vertex normal.
 */
struct CJellyFormat3dObjNormal {
  float x; /**< X component of the normal */
  float y; /**< Y component of the normal */
  float z; /**< Z component of the normal */
};

/**
 * @brief Structure for holding overflow information for a face.
 *
 * The overwhelming majority of faces in OBJ files are triangles or quads, but
 * some faces may have more than four vertices. This structure is used to store
 * overflow information for faces with more than four vertices.
 */
struct CJellyFormat3dObjFaceOverflow {
  int vertex;    /**< Vertex index (0-based) */
  int texcoord;  /**< Texture coordinate index (0-based or -1 if missing) */
  int normal;    /**< Normal index (0-based or -1 if missing) */
};

/**
 * @brief Structure representing a face in the OBJ model.
 *
 * This structure storesvertex indices along with corresponding texture and
 * normal indices. The count field indicates the number of vertices that form
 * the face.  The material_index field indicates the index of the material used
 * for this face, or -1 if no material is assigned.
 */
struct CJellyFormat3dObjFace {
  int vertex[4];      /**< Vertex indices (0-based) */
  int texcoord[4];    /**< Texture coordinate indices (0-based or -1 if missing) */
  int normal[4];      /**< Normal indices (0-based or -1 if missing) */
  int count;          /**< Number of vertices in the face */
  int material_index; /**< Index into the material array, or -1 if no material assigned */
  CJellyFormat3dObjFaceOverflow * overflow; /**< Overflow information for faces with more than four vertices */
};

/**
 * @brief Structure representing a group or object in the OBJ model.
 *
 * Groups help organize subsets of faces within the model.
 */
struct CJellyFormat3dObjGroup {
  char name[CJELLY_FORMAT_3D_OBJ_MAX_NAME_LENGTH]; /**< Group or object name */
  int start_face;     /**< Index of the first face in this group */
  int face_count;     /**< Number of faces in this group */
};

/**
 * @brief Structure representing a material mapping.
 *
 * This structure associates a material name from the "usemtl" directive with an integer index.
 * The index can later be used to reference a material definition from an MTL file.
 */
struct CJellyFormat3dObjMaterialMapping {
  char name[CJELLY_FORMAT_3D_OBJ_MAX_NAME_LENGTH]; /**< Material name */
  int index;      /**< Assigned index for the material */
};

/**
 * @brief Main structure for storing an OBJ model.
 *
 * This structure contains dynamically allocated arrays for vertices, texture coordinates,
 * normals, faces, and groups. It also includes a reference to an external material library if present.
 */
struct CJellyFormat3dObjModel {
  CJellyFormat3dObjVertex * vertices;    /**< Array of vertices */
  int vertex_count;       /**< Number of vertices */
  int vertex_capacity;    /**< Allocated capacity for vertices */

  CJellyFormat3dObjTexCoord * texcoords;   /**< Array of texture coordinates */
  int texcoord_count;     /**< Number of texture coordinates */
  int texcoord_capacity;  /**< Allocated capacity for texture coordinates */

  CJellyFormat3dObjNormal * normals;       /**< Array of normals */
  int normal_count;       /**< Number of normals */
  int normal_capacity;    /**< Allocated capacity for normals */

  CJellyFormat3dObjFace * faces;           /**< Array of faces */
  int face_count;         /**< Number of faces */
  int face_capacity;      /**< Allocated capacity for faces */

  CJellyFormat3dObjGroup* groups;          /**< Array of groups/objects */
  int group_count;        /**< Number of groups */
  int group_capacity;     /**< Allocated capacity for groups */

  char mtllib[256];       /**< CJellyFormat3dMtlMaterial library filename, if any */

  CJellyFormat3dObjMaterialMapping* material_mappings; /**< Array of material mappings */
  int material_mapping_count;    /**< Number of material mappings */
  int material_mapping_capacity; /**< Allocated capacity for material mappings */
};

/**
 * @brief Loads an OBJ file and parses its contents.
 *
 * This function reads an OBJ file from disk, parses its contents, and allocates a
 * CJellyFormat3dObjModel structure with vertices, texture coordinates, normals, faces,
 * and groups. The parsed model is returned via the outModel output parameter.
 *
 * @param filename Path to the OBJ file.
 * @param outModel Output pointer that will point to the allocated CJellyFormat3dObjModel on success.
 * @return CJellyFormat3dObjError Error code indicating success or the type of failure.
 */
CJellyFormat3dObjError cjelly_format_3d_obj_load(const char* filename,
                                                 CJellyFormat3dObjModel** outModel);

/**
 * @brief Frees the memory allocated for an OBJ model.
 *
 * @param model Pointer to the CJellyFormat3dObjModel to free.
 */
void cjelly_format_3d_obj_free(CJellyFormat3dObjModel* model);

/**
 * @brief Dumps the contents of a CJellyFormat3dObjModel to the specified FILE pointer in valid OBJ format.
 *
 * This function prints the mtllib declaration (if any), all vertices, texture coordinates,
 * normals, and then iterates through groups and faces. For each face, if a material is assigned,
 * it prints a "usemtl" directive based on the material mapping.
 *
 * @param model Pointer to the CJellyFormat3dObjModel to dump.
 * @param fd FILE pointer to write the output (e.g., stdout).
 * @return CJellyFormat3dObjError Error code indicating success or an I/O/format error.
 */
CJellyFormat3dObjError cjelly_format_3d_obj_dump(const CJellyFormat3dObjModel * model, FILE * fd);

/**
 * @brief Converts an OBJ error code to a human-readable error message.
 *
 * @param err The CJellyFormat3dObjError code.
 * @return A constant string describing the error.
 */
const char * cjelly_format_3d_obj_strerror(CJellyFormat3dObjError err);


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CJELLY_FORMAT_3D_OBJ_H
