#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cutil/array.h>

#include <cjelly/format/3d/obj.h>


// Reference documents:
// https://en.wikipedia.org/wiki/Wavefront_.obj_file
// https://paulbourke.net/dataformats/obj/
// https://paulbourke.net/dataformats/obj/obj_spec.pdf


#define LINE_SIZE 256


/**
 * The arrays an OBJ file builds up while it is being parsed.
 *
 * The parser used to grow six arrays by hand, each with its own doubling
 * step and its own out-of-memory branch. They are GCU_Arrays now, and the
 * finished contents are handed to the model with gcu_array_steal(), so the
 * public CJellyFormat3dObjModel still exposes plain pointers and counts.
 */
typedef struct {
  GCU_Array vertices;
  GCU_Array texcoords;
  GCU_Array normals;
  GCU_Array faces;
  GCU_Array groups;
  GCU_Array material_mappings;
} obj_builder_t;

/** Initialize every array, with the capacities the parser used to preallocate. */
static bool obj_builder_init(obj_builder_t * b) {
  memset(b, 0, sizeof(*b));
  return gcu_array_create_in_place(
             &b->vertices, sizeof(CJellyFormat3dObjVertex), 128, NULL) &&
      gcu_array_create_in_place(
          &b->texcoords, sizeof(CJellyFormat3dObjTexCoord), 128, NULL) &&
      gcu_array_create_in_place(
          &b->normals, sizeof(CJellyFormat3dObjNormal), 128, NULL) &&
      gcu_array_create_in_place(
          &b->faces, sizeof(CJellyFormat3dObjFace), 128, NULL) &&
      gcu_array_create_in_place(
          &b->groups, sizeof(CJellyFormat3dObjGroup), 16, NULL) &&
      gcu_array_create_in_place(&b->material_mappings,
          sizeof(CJellyFormat3dObjMaterialMapping), 4, NULL);
}

/**
 * Release everything the builder holds, including the per-face overflow
 * arrays, which the model's own free function would otherwise be responsible
 * for once the faces reached it.
 */
static void obj_builder_destroy(obj_builder_t * b) {
  for (size_t i = 0; i < gcu_array_count(&b->faces); i++) {
    CJellyFormat3dObjFace * face =
        (CJellyFormat3dObjFace *)gcu_array_at(&b->faces, i);
    free(face->overflow);
  }
  gcu_array_destroy_in_place(&b->vertices);
  gcu_array_destroy_in_place(&b->texcoords);
  gcu_array_destroy_in_place(&b->normals);
  gcu_array_destroy_in_place(&b->faces);
  gcu_array_destroy_in_place(&b->groups);
  gcu_array_destroy_in_place(&b->material_mappings);
}

/** Move one array into a model's pointer/count/capacity triple. */
static void obj_steal_into(
    GCU_Array * array, void ** out_data, int * out_count, int * out_capacity) {
  // Trim first, so the capacity the model reports is the one it actually has.
  (void)gcu_array_shrink_to_fit(array);
  size_t count = 0;
  *out_data = gcu_array_steal(array, &count);
  *out_count = (int)count;
  *out_capacity = (int)count;
}

CJellyFormat3dObjError cjelly_format_3d_obj_load(const char * filename, CJellyFormat3dObjModel * * outModel) {
  CJellyFormat3dObjError err = CJELLY_FORMAT_3D_OBJ_SUCCESS;

  // Check for invalid input.
  if (!filename || !outModel) {
    return CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
  }
  *outModel = NULL;

  // Open the file for reading.
  FILE* fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "Cannot open file %s\n", filename);
    return CJELLY_FORMAT_3D_OBJ_ERR_FILE_NOT_FOUND;
  }

  obj_builder_t builder;
  if (!obj_builder_init(&builder)) {
    obj_builder_destroy(&builder);
    fclose(fp);
    return CJELLY_FORMAT_3D_OBJ_ERR_OUT_OF_MEMORY;
  }

  char mtllib[256];
  mtllib[0] = '\0';

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
      if (sscanf(line + 2, "%f %f %f", &v.x, &v.y, &v.z) != 3) {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
      if (!gcu_array_append(&builder.vertices, &v)) { goto ERROR_CLEANUP; }
    }
    else if (strncmp(line, "vt ", 3) == 0) {
      // Read a texture coordinate line.
      CJellyFormat3dObjTexCoord vt;
      if (sscanf(line + 3, "%f %f", &vt.u, &vt.v) != 2) {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
      if (!gcu_array_append(&builder.texcoords, &vt)) { goto ERROR_CLEANUP; }
    }
    else if (strncmp(line, "vn ", 3) == 0) {
      // Read a normal line.
      CJellyFormat3dObjNormal vn;
      if (sscanf(line + 3, "%f %f %f", &vn.x, &vn.y, &vn.z) != 3) {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
      if (!gcu_array_append(&builder.normals, &vn)) { goto ERROR_CLEANUP; }
    }
    else if (strncmp(line, "f ", 2) == 0) {
      // Read a face line.
      CJellyFormat3dObjFace face;
      face.count = 0;
      face.material_index = current_material_index;
      face.overflow = NULL; // Initialize overflow to NULL.

      // Vertices past the fourth go into an overflow array, which is handed
      // to the face at the end of the line.
      GCU_Array overflow;
      if (!gcu_array_create_in_place(
              &overflow, sizeof(CJellyFormat3dObjFaceOverflow), 0, NULL)) {
        goto ERROR_CLEANUP;
      }

      // Tokenize the line after "f ".
      char * token = strtok(line + 2, " ");
      while (token != NULL) {
        int vIndex = 0, vtIndex = 0, vnIndex = 0;
        // A face token is one of "v", "v/vt", "v//vn" or "v/vt/vn". Read the
        // fields positionally so an omitted one stays distinct from a present
        // one.
        //
        // Rewriting every '/' as a space and handing the result to sscanf
        // cannot do that: "1//2" and "1 2" become the same string, so the
        // normal was read as the texture coordinate and then discarded, and
        // "1/2" was treated as a missing texture coordinate for the same
        // reason. Only the fully specified "v/vt/vn" form survived.
        {
          const char * cursor = token;
          char * end = NULL;
          long value = strtol(cursor, &end, 10);
          if (end != cursor) {
            vIndex = (int)value;
          }
          if (end && *end == '/') {
            cursor = end + 1;
            value = strtol(cursor, &end, 10);
            if (end != cursor) {
              vtIndex = (int)value; // Left empty in "v//vn"; stays 0.
            }
            if (end && *end == '/') {
              cursor = end + 1;
              value = strtol(cursor, &end, 10);
              if (end != cursor) {
                vnIndex = (int)value;
              }
            }
          }
        }

        // For the first four vertices, store in fixed arrays.
        if (face.count < 4) {
          face.vertex[face.count] = vIndex - 1;
          face.texcoord[face.count] = vtIndex ? (vtIndex - 1) : -1;
          face.normal[face.count] = vnIndex ? (vnIndex - 1) : -1;
          face.count++;
        }
        else {
          CJellyFormat3dObjFaceOverflow * extra =
              (CJellyFormat3dObjFaceOverflow *)gcu_array_emplace(&overflow);
          if (!extra) {
            gcu_array_destroy_in_place(&overflow);
            goto ERROR_CLEANUP;
          }
          extra->vertex = vIndex - 1;
          extra->texcoord = vtIndex ? (vtIndex - 1) : -1;
          extra->normal = vnIndex ? (vnIndex - 1) : -1;
          face.count++; // Increase total vertex count.
        }
        token = strtok(NULL, " ");
      }

      // Trim before handing it over: this block outlives the parse and there
      // may be one per face.
      (void)gcu_array_shrink_to_fit(&overflow);
      face.overflow =
          (CJellyFormat3dObjFaceOverflow *)gcu_array_steal(&overflow, NULL);
      gcu_array_destroy_in_place(&overflow);

      // Append the face to the model's face array.
      if (!gcu_array_append(&builder.faces, &face)) {
        // The face never reached the array, so its overflow will not be freed
        // along with the rest.
        free(face.overflow);
        goto ERROR_CLEANUP;
      }
      if (current_group >= 0) {
        CJellyFormat3dObjGroup * group = (CJellyFormat3dObjGroup *)gcu_array_at(
            &builder.groups, (size_t)current_group);
        group->face_count++;
      }
    }
    else if (strncmp(line, "g ", 2) == 0 || strncmp(line, "o ", 2) == 0) {
      // Read a group or object name line.
      char name[CJELLY_FORMAT_3D_OBJ_MAX_NAME_LENGTH];
      // 128 is hard-coded because "%127s" is used in the sscanf call.
      // The assert is used to ensure that the buffer cannot be smaller
      // than this hard-coded value.
      assert(sizeof(name) >= 128);
      if (sscanf(line + 2, "%127s", name) != 1) {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }
      CJellyFormat3dObjGroup * group =
          (CJellyFormat3dObjGroup *)gcu_array_emplace(&builder.groups);
      if (!group) { goto ERROR_CLEANUP; }
      strcpy(group->name, name);
      group->start_face = (int)gcu_array_count(&builder.faces);
      group->face_count = 0;
      current_group = (int)gcu_array_count(&builder.groups) - 1;
    }
    else if (strncmp(line, "usemtl", 6) == 0) {
      // Read a material usage directive.
      char mtl_name[CJELLY_FORMAT_3D_OBJ_MAX_NAME_LENGTH];
      // 128 is hard-coded because "%127s" is used in the sscanf call.
      // The assert is used to ensure that the buffer cannot be smaller
      // than this hard-coded value.
      assert(sizeof(mtl_name) >= 128);
      if (sscanf(line + 6, "%127s", mtl_name) != 1) {
        err = CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT;
        goto ERROR_CLEANUP;
      }

      // Search for an existing mapping.
      int mapped_index = -1;
      for (size_t i = 0; i < gcu_array_count(&builder.material_mappings); i++) {
        CJellyFormat3dObjMaterialMapping * mapping =
            (CJellyFormat3dObjMaterialMapping *)gcu_array_at(
                &builder.material_mappings, i);
        if (strcmp(mapping->name, mtl_name) == 0) {
          mapped_index = mapping->index;
          break;
        }
      }
      if (mapped_index < 0) {
        // Add a new mapping.
        CJellyFormat3dObjMaterialMapping * mapping =
            (CJellyFormat3dObjMaterialMapping *)gcu_array_emplace(
                &builder.material_mappings);
        if (!mapping) { goto ERROR_CLEANUP; }
        strcpy(mapping->name, mtl_name);
        mapping->index =
            (int)gcu_array_count(&builder.material_mappings) - 1;
        mapped_index = mapping->index;
      }
      current_material_index = mapped_index;
    }
    else if (strncmp(line, "mtllib", 6) == 0) {
      // Read the material library name.
      sscanf(line + 6, "%255s", mtllib);
    }
  }

  // Parsing succeeded: build the model and move the arrays into it. Doing
  // this last means there is no half-built model to unwind on the error path.
  {
    CJellyFormat3dObjModel * model = (CJellyFormat3dObjModel *)calloc(
        1, sizeof(CJellyFormat3dObjModel));
    if (!model) { goto ERROR_CLEANUP; }

    obj_steal_into(&builder.vertices, (void **)&model->vertices,
        &model->vertex_count, &model->vertex_capacity);
    obj_steal_into(&builder.texcoords, (void **)&model->texcoords,
        &model->texcoord_count, &model->texcoord_capacity);
    obj_steal_into(&builder.normals, (void **)&model->normals,
        &model->normal_count, &model->normal_capacity);
    obj_steal_into(&builder.faces, (void **)&model->faces, &model->face_count,
        &model->face_capacity);
    obj_steal_into(&builder.groups, (void **)&model->groups,
        &model->group_count, &model->group_capacity);
    obj_steal_into(&builder.material_mappings,
        (void **)&model->material_mappings, &model->material_mapping_count,
        &model->material_mapping_capacity);

    memcpy(model->mtllib, mtllib, sizeof(model->mtllib));

    obj_builder_destroy(&builder);
    fclose(fp);
    *outModel = model;
    return CJELLY_FORMAT_3D_OBJ_SUCCESS;
  }

  // Error handling.
ERROR_CLEANUP:
  // If an error is not set, then default to out-of-memory.
  // This approach is used in this function because out-of-memory is the most
  // common reason to error out, so it was chosen to be the default.
  if (err == CJELLY_FORMAT_3D_OBJ_SUCCESS) {
    err = CJELLY_FORMAT_3D_OBJ_ERR_OUT_OF_MEMORY;
  }
  obj_builder_destroy(&builder);
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
