#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cjelly/format/3d/obj.h>


// Reference documents:
// https://en.wikipedia.org/wiki/Wavefront_.obj_file
// https://paulbourke.net/dataformats/obj/
// https://paulbourke.net/dataformats/obj/obj_spec.pdf


#define LINE_SIZE 256


CJellyFormat3dObjError cjelly_format_3d_obj_load(const char * filename, CJellyFormat3dObjModel * * outModel) {
  CJellyFormat3dObjError err = CJELLY_FORMAT_3D_OBJ_SUCCESS;

  // Check for invalid input.
  if (!filename || !outModel) {
    return CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
  }

  // Open the file for reading.
  FILE* fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "Cannot open file %s\n", filename);
    return CJELLY_FORMAT_3D_OBJ_ERR_FILE_NOT_FOUND;
  }

  // Allocate memory for the model structure.
  CJellyFormat3dObjModel * model = (CJellyFormat3dObjModel *)malloc(sizeof(CJellyFormat3dObjModel));
  if (!model) { goto ERROR_CLEANUP; }

  // Initialize all counts and capacities, and preallocate memory.
  model->vertex_count = 0;
  model->vertex_capacity = 128;
  model->vertices = (CJellyFormat3dObjVertex *)malloc(model->vertex_capacity * sizeof(CJellyFormat3dObjVertex));
  if (!model->vertices) { goto ERROR_CLEANUP; }

  model->texcoord_count = 0;
  model->texcoord_capacity = 128;
  model->texcoords = (CJellyFormat3dObjTexCoord *)malloc(model->texcoord_capacity * sizeof(CJellyFormat3dObjTexCoord));
  if (!model->texcoords) { goto ERROR_CLEANUP; }

  model->normal_count = 0;
  model->normal_capacity = 128;
  model->normals = (CJellyFormat3dObjNormal *)malloc(model->normal_capacity * sizeof(CJellyFormat3dObjNormal));
  if (!model->normals) { goto ERROR_CLEANUP; }

  model->face_count = 0;
  model->face_capacity = 128;
  model->faces = (CJellyFormat3dObjFace *)malloc(model->face_capacity * sizeof(CJellyFormat3dObjFace));
  if (!model->faces) { goto ERROR_CLEANUP; }

  model->group_count = 0;
  model->group_capacity = 16;
  model->groups = (CJellyFormat3dObjGroup *)malloc(model->group_capacity * sizeof(CJellyFormat3dObjGroup));
  if (!model->groups) { goto ERROR_CLEANUP; }

  model->material_mapping_count = 0;
  model->material_mapping_capacity = 4;
  model->material_mappings = (CJellyFormat3dObjMaterialMapping *)malloc(model->material_mapping_capacity * sizeof(CJellyFormat3dObjMaterialMapping));
  if (!model->material_mappings) { goto ERROR_CLEANUP; }

  // Initialize the material library name.
  model->mtllib[0] = '\0';

  int current_group = -1;            // Index of the current active group
  int current_material_index = -1;   // Current material index (updated by "usemtl" directive)

  // Begin parsing the file line by line.
  char line[LINE_SIZE];
  while (fgets(line, LINE_SIZE, fp)) {
    // Remove newline characters.
    line[strcspn(line, "\r\n")] = 0;

    if (strncmp(line, "v ", 2) == 0) {
      // Read a vertex line.
      CJellyFormat3dObjVertex v;
      if (sscanf(line + 2, "%f %f %f", &v.x, &v.y, &v.z) == 3) {
        if (model->vertex_count >= model->vertex_capacity) {
          model->vertex_capacity *= 2;
          CJellyFormat3dObjVertex* temp = realloc(model->vertices, model->vertex_capacity * sizeof(CJellyFormat3dObjVertex));
          if (!temp) { goto ERROR_CLEANUP; }
          model->vertices = temp;
        }
        model->vertices[model->vertex_count++] = v;
      }
      else {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
    }
    else if (strncmp(line, "vt ", 3) == 0) {
      // Read a texture coordinate line.
      CJellyFormat3dObjTexCoord vt;
      if (sscanf(line + 3, "%f %f", &vt.u, &vt.v) == 2) {
        if (model->texcoord_count >= model->texcoord_capacity) {
          model->texcoord_capacity *= 2;
          CJellyFormat3dObjTexCoord* temp = realloc(model->texcoords, model->texcoord_capacity * sizeof(CJellyFormat3dObjTexCoord));
          if (!temp) { goto ERROR_CLEANUP; }
          model->texcoords = temp;
        }
        model->texcoords[model->texcoord_count++] = vt;
      }
      else {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
    }
    else if (strncmp(line, "vn ", 3) == 0) {
      // Read a normal line.
      CJellyFormat3dObjNormal vn;
      if (sscanf(line + 3, "%f %f %f", &vn.x, &vn.y, &vn.z) == 3) {
        if (model->normal_count >= model->normal_capacity) {
          model->normal_capacity *= 2;
          CJellyFormat3dObjNormal* temp = realloc(model->normals, model->normal_capacity * sizeof(CJellyFormat3dObjNormal));
          if (!temp) { goto ERROR_CLEANUP; }
          model->normals = temp;
        }
        model->normals[model->normal_count++] = vn;
      }
      else {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
    }
    else if (strncmp(line, "f ", 2) == 0) {
      // Read a face line.
      CJellyFormat3dObjFace face;
      face.count = 0;
      face.material_index = current_material_index;
      face.overflow = NULL; // Initialize overflow to NULL.
      int extra_count = 0;  // Number of vertices stored in overflow.
      int extra_capacity = 0; // Capacity for overflow array.

      // Tokenize the line after "f ".
      char * token = strtok(line + 2, " ");
      while (token != NULL) {
        int vIndex = 0, vtIndex = 0, vnIndex = 0;
        if (strchr(token, '/')) {
          /* Replace '/' with space for easier parsing */
          char temp[64];
          strncpy(temp, token, 63);
          temp[63] = '\0';
          for (int i = 0; i < (int)strlen(temp); i++) {
            if (temp[i] == '/')
              temp[i] = ' ';
          }
          // Parse the indices (some may be missing).
          int parsed = sscanf(temp, "%d %d %d", &vIndex, &vtIndex, &vnIndex);
          if (parsed < 3)
            vtIndex = 0; // If texture coordinate is missing, set to 0 (or use -1).
        }
        else {
          vIndex = atoi(token);
        }

        // For the first four vertices, store in fixed arrays.
        if (face.count < 4) {
          face.vertex[face.count] = vIndex - 1;
          face.texcoord[face.count] = vtIndex ? (vtIndex - 1) : -1;
          face.normal[face.count] = vnIndex ? (vnIndex - 1) : -1;
          face.count++;
        }
        else {
          // For extra vertices, allocate or grow the overflow array.
          if (face.overflow == NULL) {
            extra_capacity = 4;
            face.overflow = (CJellyFormat3dObjFaceOverflow *)malloc(extra_capacity * sizeof(CJellyFormat3dObjFaceOverflow));
            if (!face.overflow) { goto ERROR_CLEANUP; }
            extra_count = 0;
          }
          if (extra_count >= extra_capacity) {
            extra_capacity *= 2;
            CJellyFormat3dObjFaceOverflow * tmp = realloc(face.overflow, extra_capacity * sizeof(CJellyFormat3dObjFaceOverflow));
            if (!tmp) { goto ERROR_CLEANUP; }
            face.overflow = tmp;
          }
          face.overflow[extra_count].vertex = vIndex - 1;
          face.overflow[extra_count].texcoord = vtIndex ? (vtIndex - 1) : -1;
          face.overflow[extra_count].normal = vnIndex ? (vnIndex - 1) : -1;
          extra_count++;
          face.count++; // Increase total vertex count.
        }
        token = strtok(NULL, " ");
      }
      // Append the face to the model's face array.
      if (model->face_count >= model->face_capacity) {
        model->face_capacity *= 2;
        CJellyFormat3dObjFace * temp = realloc(model->faces, model->face_capacity * sizeof(CJellyFormat3dObjFace));
        if (!temp) {
          // Free the overflow array if it was allocated.
          // The code pattern is different for this case because the operation
          // to add the face to the faces array failed.  Therefore, it will
          // not be automatically freed when the model itself is freed.
          if (face.overflow) free(face.overflow);
          goto ERROR_CLEANUP;
        }
        model->faces = temp;
      }
      model->faces[model->face_count++] = face;
      if (current_group >= 0) {
        model->groups[current_group].face_count++;
      }
    }
    else if (strncmp(line, "g ", 2) == 0 || strncmp(line, "o ", 2) == 0) {
      // Read a group or object name line.
      char name[CJELLY_FORMAT_3D_OBJ_MAX_NAME_LENGTH];
      // 128 is hard-coded because "%127s" is used in the sscanf call.
      // The assert is used to ensure that the buffer cannot be smaller
      // than this hard-coded value.
      assert(sizeof(name) >= 128);
      if (sscanf(line + 2, "%127s", name) == 1) {
        if (model->group_count >= model->group_capacity) {
          model->group_capacity *= 2;
          CJellyFormat3dObjGroup* temp = realloc(model->groups, model->group_capacity * sizeof(CJellyFormat3dObjGroup));
          if (!temp) { goto ERROR_CLEANUP; }
          model->groups = temp;
        }
        strcpy(model->groups[model->group_count].name, name);
        model->groups[model->group_count].start_face = model->face_count;
        model->groups[model->group_count].face_count = 0;
        current_group = model->group_count;
        model->group_count++;
      }
      else {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
    }
    else if (strncmp(line, "usemtl", 6) == 0) {
      // Read a material usage directive.
      char mtl_name[CJELLY_FORMAT_3D_OBJ_MAX_NAME_LENGTH];
      // 128 is hard-coded because "%127s" is used in the sscanf call.
      // The assert is used to ensure that the buffer cannot be smaller
      // than this hard-coded value.
      assert(sizeof(mtl_name) >= 128);
      if (sscanf(line + 6, "%127s", mtl_name) == 1) {
        int found = 0;
        int mapped_index = -1;
        // Search for an existing mapping.
        for (int i = 0; i < model->material_mapping_count; i++) {
          if (strcmp(model->material_mappings[i].name, mtl_name) == 0) {
            found = 1;
            mapped_index = model->material_mappings[i].index;
            break;
          }
        }
        if (!found) {
          // Add a new mapping.
          if (model->material_mapping_count >= model->material_mapping_capacity) {
            model->material_mapping_capacity *= 2;
            CJellyFormat3dObjMaterialMapping* temp = realloc(model->material_mappings, model->material_mapping_capacity * sizeof(CJellyFormat3dObjMaterialMapping));
            if (!temp) { goto ERROR_CLEANUP; }
            model->material_mappings = temp;
          }
          strcpy(model->material_mappings[model->material_mapping_count].name, mtl_name);
          model->material_mappings[model->material_mapping_count].index = model->material_mapping_count;
          mapped_index = model->material_mapping_count;
          model->material_mapping_count++;
        }
        current_material_index = mapped_index;
      }
      else {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
    }
    else if (strncmp(line, "mtllib", 6) == 0) {
      // Read the material library name.
      sscanf(line + 6, "%255s", model->mtllib);
    }
  }

  // Close the file and return the model.
  fclose(fp);
  *outModel = model;
  return CJELLY_FORMAT_3D_OBJ_SUCCESS;

  // Error handling.
ERROR_CLEANUP:
  // If an error is not set, then default to out-of-memory.
  // This approach is used in this function because out-of-memory is the most
  // common reason to error out, so it was chosen to be the default.
  if (err == CJELLY_FORMAT_3D_OBJ_SUCCESS) {
    err = CJELLY_FORMAT_3D_OBJ_ERR_OUT_OF_MEMORY;
  }
  cjelly_format_3d_obj_free(model);
  fclose(fp);
  return err;
}


void cjelly_format_3d_obj_free(CJellyFormat3dObjModel* model) {
  if (!model) return;
  if (model->vertices) free(model->vertices);
  if (model->texcoords) free(model->texcoords);
  if (model->normals) free(model->normals);
  if (model->faces) {
    // Free per-face overflow arrays.
    for (int i = 0; i < model->face_count; ++i) {
      if (model->faces[i].overflow) {
        free(model->faces[i].overflow);
      }
    }
    free(model->faces);
  }
  if (model->groups) free(model->groups);
  if (model->material_mappings) free(model->material_mappings);
  free(model);
}


CJellyFormat3dObjError cjelly_format_3d_obj_dump(const CJellyFormat3dObjModel *model, FILE *fd) {
  if (!model || !fd)
    return CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;

  int ret;
  // Print material library if specified.
  if (model->mtllib[0] != '\0') {
    ret = fprintf(fd, "mtllib %s\n", model->mtllib);
    if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
  }

  // Dump vertices.
  for (int i = 0; i < model->vertex_count; ++i) {
    ret = fprintf(fd, "v %f %f %f\n", model->vertices[i].x, model->vertices[i].y, model->vertices[i].z);
    if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
  }

  // Dump texture coordinates.
  for (int i = 0; i < model->texcoord_count; ++i) {
    ret = fprintf(fd, "vt %f %f\n", model->texcoords[i].u, model->texcoords[i].v);
    if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
  }

  // Dump normals.
  for (int i = 0; i < model->normal_count; ++i) {
    ret = fprintf(fd, "vn %f %f %f\n", model->normals[i].x, model->normals[i].y, model->normals[i].z);
    if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
  }

  // Dump groups and faces.
  if (model->group_count > 0) {
    for (int g = 0; g < model->group_count; ++g) {
      ret = fprintf(fd, "g %s\n", model->groups[g].name);
      if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
      int start = model->groups[g].start_face;
      int count = model->groups[g].face_count;
      // Initialize last material index to a value that cannot be valid.
      int last_material_index = -2;
      for (int i = start; i < start + count; ++i) {
        // Only print "usemtl" if material has changed.
        if (model->faces[i].material_index != last_material_index) {
          if (model->faces[i].material_index != -1) {
            const char * mtl_name = NULL;
            for (int j = 0; j < model->material_mapping_count; ++j) {
              if (model->material_mappings[j].index == model->faces[i].material_index) {
                mtl_name = model->material_mappings[j].name;
                break;
              }
            }
            if (mtl_name) {
              ret = fprintf(fd, "usemtl %s\n", mtl_name);
              if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            }
            else {
              // Per https://paulbourke.net/dataformats/obj/
              // A material cannot be "turned off", it can only be changed.
              // If a material name is not specified, a white material is used.
              ret = fprintf(fd, "usemtl white\n");
              if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            }
          }
          last_material_index = model->faces[i].material_index;
        }
        // Print the face line.
        ret = fprintf(fd, "f");
        if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
        // Print the first four vertices.
        for (int j = 0; j < (model->faces[i].count > 4 ? 4 : model->faces[i].count); ++j) {
          int v = model->faces[i].vertex[j] + 1;
          int vt = model->faces[i].texcoord[j];
          int vn = model->faces[i].normal[j];
          ret = fprintf(fd, " %d", v);
          if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
          if (vt != -1 || vn != -1) {
            ret = fprintf(fd, "/");
            if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            if (vt != -1) {
              ret = fprintf(fd, "%d", vt + 1);
              if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            }
            if (vn != -1) {
              ret = fprintf(fd, "/%d", vn + 1);
              if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            }
          }
        }
        // Print overflow vertices, if any.
        if (model->faces[i].count > 4 && model->faces[i].overflow != NULL) {
          int overflow_count = model->faces[i].count - 4;
          for (int j = 0; j < overflow_count; j++) {
            int v = model->faces[i].overflow[j].vertex + 1;
            int vt = model->faces[i].overflow[j].texcoord;
            int vn = model->faces[i].overflow[j].normal;
            ret = fprintf(fd, " %d", v);
            if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            if (vt != -1 || vn != -1) {
              ret = fprintf(fd, "/");
              if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
              if (vt != -1) {
                ret = fprintf(fd, "%d", vt + 1);
                if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
              }
              if (vn != -1) {
                ret = fprintf(fd, "/%d", vn + 1);
                if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
              }
            }
          }
        }
        ret = fprintf(fd, "\n");
        if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
    }
  }
}
else {
    // No groups; dump all faces.
    int last_material_index = -2;
    for (int i = 0; i < model->face_count; ++i) {
      if (model->faces[i].material_index != last_material_index) {
        if (model->faces[i].material_index != -1) {
          const char * mtl_name = NULL;
          for (int j = 0; j < model->material_mapping_count; ++j) {
            if (model->material_mappings[j].index == model->faces[i].material_index) {
              mtl_name = model->material_mappings[j].name;
              break;
            }
          }
          if (mtl_name) {
            ret = fprintf(fd, "usemtl %s\n", mtl_name);
            if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
          }
        }
        last_material_index = model->faces[i].material_index;
      }
      ret = fprintf(fd, "f");
      if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
      for (int j = 0; j < (model->faces[i].count > 4 ? 4 : model->faces[i].count); ++j) {
        int v = model->faces[i].vertex[j] + 1;
        int vt = model->faces[i].texcoord[j];
        int vn = model->faces[i].normal[j];
        ret = fprintf(fd, " %d", v);
        if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
        if (vt != -1 || vn != -1) {
          ret = fprintf(fd, "/");
          if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
          if (vt != -1) {
            ret = fprintf(fd, "%d", vt + 1);
            if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
          }
          if (vn != -1) {
            ret = fprintf(fd, "/%d", vn + 1);
            if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
          }
        }
      }
      if (model->faces[i].count > 4 && model->faces[i].overflow != NULL) {
        int overflow_count = model->faces[i].count - 4;
        for (int j = 0; j < overflow_count; j++) {
          int v = model->faces[i].overflow[j].vertex + 1;
          int vt = model->faces[i].overflow[j].texcoord;
          int vn = model->faces[i].overflow[j].normal;
          ret = fprintf(fd, " %d", v);
          if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
          if (vt != -1 || vn != -1) {
            ret = fprintf(fd, "/");
            if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            if (vt != -1) {
              ret = fprintf(fd, "%d", vt + 1);
              if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            }
            if (vn != -1) {
              ret = fprintf(fd, "/%d", vn + 1);
              if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
            }
          }
        }
      }
      ret = fprintf(fd, "\n");
      if (ret < 0) return CJELLY_FORMAT_3D_OBJ_ERR_IO;
    }
  }
  return CJELLY_FORMAT_3D_OBJ_SUCCESS;
}


const char* cjelly_format_3d_obj_strerror(CJellyFormat3dObjError err) {
  switch (err) {
    case CJELLY_FORMAT_3D_OBJ_SUCCESS:
      return "No error";
    case CJELLY_FORMAT_3D_OBJ_ERR_FILE_NOT_FOUND:
      return "OBJ file not found";
    case CJELLY_FORMAT_3D_OBJ_ERR_OUT_OF_MEMORY:
      return "Out of memory";
    case CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT:
      return "Invalid OBJ file format";
    case CJELLY_FORMAT_3D_OBJ_ERR_IO:
      return "I/O error when reading/writing the OBJ file";
    default:
      return "Unknown error";
  }
}
