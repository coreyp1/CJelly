#ifndef CJELLY_FORMAT_3D_MTL_H
#define CJELLY_FORMAT_3D_MTL_H

#include <cjelly/macros.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus  

/**
 * @file mtl.h
 * @brief Header file for loading and parsing MTL files.
 *
 * This file defines the structures and functions needed to parse and represent
 * material definitions in the MTL file format.
 */

/**
 * @brief Enumeration of error codes for the MTL parser.
 */
typedef enum {
  CJELLY_FORMAT_3D_MTL_SUCCESS = 0,           /**< No error */
  CJELLY_FORMAT_3D_MTL_ERR_FILE_NOT_FOUND,    /**< Unable to open the file */
  CJELLY_FORMAT_3D_MTL_ERR_OUT_OF_MEMORY,     /**< Memory allocation failure */
  CJELLY_FORMAT_3D_MTL_ERR_INVALID_FORMAT,    /**< File contains an invalid format */
  CJELLY_FORMAT_3D_MTL_ERR_IO                 /**< I/O error while reading/writing the file */
} CJellyFormat3dMtlError;

/**
 * @brief Structure representing a single material definition.
 *
 * This structure contains common material properties such as ambient, diffuse,
 * and specular colors, as well as the specular exponent and illumination model.
 */
struct CJellyFormat3dMtlMaterial {
    char name[128];  /**< Name of the material */
    float Ka[3];     /**< Ambient color (RGB) */
    float Kd[3];     /**< Diffuse color (RGB) */
    float Ks[3];     /**< Specular color (RGB) */
    float Ns;        /**< Specular exponent */
    float d;         /**< Dissolve (transparency) */
    int illum;       /**< Illumination model */
};

/**
 * @brief Structure representing a material file.
 *
 * This structure contains an array of CJellyFormat3dMtlMaterial structures and
 * the number of materials in the library.
 */
struct CJellyFormat3dMtl {
    CJellyFormat3dMtlMaterial * materials;  /**< Array of materials */
    int material_count;                    /**< Number of materials */
};

/**
 * @brief Loads materials from an MTL file.
 *
 * This function reads an MTL file from disk, parses its contents, and allocates an array
 * of CJellyFormat3dMtlMaterial structures. The loaded materials are returned via the
 * materials output parameter.
 *
 * @param filename Path to the MTL file.
 * @param materials Output pointer that will point to the allocated array of materials on success.
 * @return CJellyFormat3dMtlError An error code indicating success or the type of failure.
 */
CJellyFormat3dMtlError cjelly_format_3d_mtl_load(const char * filename, CJellyFormat3dMtl * materials);

/**
 * @brief Frees the allocated memory of the materials struct.
 *
 * @param materials Pointer to the CJellyFormat3dMtl structure.
 * @param material_count The number of materials in the array.
 */
void cjelly_format_3d_mtl_free(CJellyFormat3dMtl * materials);

/**
 * @brief Dumps the contents of an array of CJellyFormat3dMtlMaterial to the specified FILE pointer in valid MTL format.
 *
 * This function iterates over the material array and prints each material using the standard
 * MTL file conventions.
 *
 * @param materials Pointer to the array of CJellyFormat3dMtlMaterial structures.
 * @param material_count Number of materials in the array.
 * @param fd FILE pointer to write the output (e.g., stdout).
 * @return CJellyFormat3dObjError Error code indicating success or an I/O/format error.
 */
CJellyFormat3dMtlError cjelly_format_3d_mtl_dump(const CJellyFormat3dMtlMaterial *materials, int material_count, FILE *fd);

/**
 * @brief Converts an MTL error code to a human-readable error message.
 *
 * @param err The CJellyFormat3dMtlError code.
 * @return A constant string describing the error.
 */
const char* cjelly_format_3d_mtl_strerror(CJellyFormat3dMtlError err);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CJELLY_FORMAT_3D_MTL_H
