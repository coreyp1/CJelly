#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cjelly/format/3d/mtl.h"


// Reference documents:
// http://www.paulbourke.net/dataformats/mtl/


#define LINE_SIZE 256


CJellyFormat3dMtlError cjelly_format_3d_mtl_load(const char * filename, CJellyFormat3dMtl * materials) {
  CJellyFormat3dMtlError err = CJELLY_FORMAT_3D_MTL_SUCCESS;

  // Check for invalid input.
  if (!filename || !materials) {
    return CJELLY_FORMAT_3D_MTL_ERR_INVALID_FORMAT;
  }

  // Open the file for reading.
  FILE * fp = fopen(filename, "r");
  if (!fp) {
    materials->material_count = 0;
    materials->materials = NULL;
    return CJELLY_FORMAT_3D_MTL_ERR_FILE_NOT_FOUND;
  }

  // Allocate memory for the materials.
  size_t capacity = 10;
  size_t count = 0;
  materials->materials = (CJellyFormat3dMtlMaterial *)malloc(capacity * sizeof(CJellyFormat3dMtlMaterial));
  if (!materials) {
    err = CJELLY_FORMAT_3D_MTL_ERR_OUT_OF_MEMORY;
    goto ERROR_CLOSE_FILE;
  }

  char line[LINE_SIZE];
  CJellyFormat3dMtlMaterial * current = NULL;
  while (fgets(line, LINE_SIZE, fp)) {
    // Remove newline characters.
    line[strcspn(line, "\r\n")] = 0;
    // Skip empty lines and comments.
    if (line[0] == '#' || line[0] == '\0')
      continue;

    if (strncmp(line, "newmtl", 6) == 0) {
      // Found a new material definition.
      // Reallocate memory if needed.
      if (count >= capacity) {
        capacity *= 2;
        CJellyFormat3dMtlMaterial * temp = realloc(materials->materials, capacity * sizeof(CJellyFormat3dMtlMaterial));
        if (!temp) {
          err = CJELLY_FORMAT_3D_MTL_ERR_OUT_OF_MEMORY;
          goto ERROR_CLOSE_FILE;
        }
        materials->materials = temp;
      }
      // Read a new material name.
      current = &materials->materials[count];
      memset(current, 0, sizeof(CJellyFormat3dMtlMaterial));
      assert(sizeof(current->name) >= 128);
      if (sscanf(line + 7, "%127s", current->name) != 1)
        goto INVALID_FILE_FORMAT;
      count++;
    }
    else if (!current) {
      /* No material has been defined yet */
      continue;
    }
    else if (strncmp(line, "Ka", 2) == 0) {
      if (sscanf(line + 2, "%f %f %f", &current->Ka[0], &current->Ka[1], &current->Ka[2]) != 3)
        goto INVALID_FILE_FORMAT;
    }
    else if (strncmp(line, "Kd", 2) == 0) {
      if (sscanf(line + 2, "%f %f %f", &current->Kd[0], &current->Kd[1], &current->Kd[2]) != 3)
        goto INVALID_FILE_FORMAT;
    }
    else if (strncmp(line, "Ks", 2) == 0) {
      if (sscanf(line + 2, "%f %f %f", &current->Ks[0], &current->Ks[1], &current->Ks[2]) != 3)
        goto INVALID_FILE_FORMAT;
    }
    else if (strncmp(line, "Ns", 2) == 0) {
      if (sscanf(line + 2, "%f", &current->Ns) != 1)
        goto INVALID_FILE_FORMAT;
    }
    else if (line[0] == 'd' && (line[1] == ' ' || line[1] == '\t')) {
      if (sscanf(line + 1, "%f", &current->d) != 1)
        goto INVALID_FILE_FORMAT;
    }
    else if (strncmp(line, "illum", 5) == 0) {
      if (sscanf(line + 5, "%d", &current->illum) != 1)
        goto INVALID_FILE_FORMAT;
    }
  }

  // Close the file and return the materials.
  fclose(fp);
  materials->material_count = count;
  return CJELLY_FORMAT_3D_MTL_SUCCESS;

  // Error handling.
INVALID_FILE_FORMAT:
  err = CJELLY_FORMAT_3D_MTL_ERR_INVALID_FORMAT;

ERROR_CLOSE_FILE:
  fclose(fp);
  materials->material_count = 0;
  if (materials->materials)
    free(materials->materials);
  materials->materials = NULL;
  return err;
}


void cjelly_format_3d_mtl_free(CJellyFormat3dMtl * materials) {
  free(materials->materials);
  materials->materials = NULL;
  materials->material_count = 0;
}


CJellyFormat3dMtlError cjelly_format_3d_mtl_dump(const CJellyFormat3dMtlMaterial *materials, int material_count, FILE *fd) {
  if (!materials || material_count <= 0 || !fd)
  return CJELLY_FORMAT_3D_MTL_ERR_IO;

  int ret;
  for (int i = 0; i < material_count; ++i) {
    ret = fprintf(fd, "newmtl %s\n", materials[i].name);
    if (ret < 0) return CJELLY_FORMAT_3D_MTL_ERR_IO;
    ret = fprintf(fd, "Ka %f %f %f\n", materials[i].Ka[0], materials[i].Ka[1], materials[i].Ka[2]);
    if (ret < 0) return CJELLY_FORMAT_3D_MTL_ERR_IO;
    ret = fprintf(fd, "Kd %f %f %f\n", materials[i].Kd[0], materials[i].Kd[1], materials[i].Kd[2]);
    if (ret < 0) return CJELLY_FORMAT_3D_MTL_ERR_IO;
    ret = fprintf(fd, "Ks %f %f %f\n", materials[i].Ks[0], materials[i].Ks[1], materials[i].Ks[2]);
    if (ret < 0) return CJELLY_FORMAT_3D_MTL_ERR_IO;
    ret = fprintf(fd, "Ns %f\n", materials[i].Ns);
    if (ret < 0) return CJELLY_FORMAT_3D_MTL_ERR_IO;
    ret = fprintf(fd, "d %f\n", materials[i].d);
    if (ret < 0) return CJELLY_FORMAT_3D_MTL_ERR_IO;
    ret = fprintf(fd, "illum %d\n\n", materials[i].illum);
    if (ret < 0) return CJELLY_FORMAT_3D_MTL_ERR_IO;
  }
  return CJELLY_FORMAT_3D_MTL_SUCCESS;
}

const char * cjelly_format_3d_mtl_strerror(CJellyFormat3dMtlError err) {
  switch (err) {
    case CJELLY_FORMAT_3D_MTL_SUCCESS:
      return "No error";
    case CJELLY_FORMAT_3D_MTL_ERR_FILE_NOT_FOUND:
      return "Material file not found";
    case CJELLY_FORMAT_3D_MTL_ERR_OUT_OF_MEMORY:
      return "Out of memory";
    case CJELLY_FORMAT_3D_MTL_ERR_INVALID_FORMAT:
      return "Invalid material file format";
    case CJELLY_FORMAT_3D_MTL_ERR_IO:
      return "I/O error";
    default:
      return "Unknown error";
  }
}
