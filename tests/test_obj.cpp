/**
 * @file test_obj.cpp
 *
 * Unit tests for the Wavefront OBJ parser.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"
#include <cjelly/format/3d/obj.h>
#include <gtest/gtest.h>
#include <string>

using cjtest::asset;
using cjtest::TempFile;

namespace {

/** Load from an inline OBJ document, or fail the test. */
CJellyFormat3dObjModel * load_text(const std::string & text) {
  TempFile f(text, ".obj");
  EXPECT_TRUE(f.valid());
  CJellyFormat3dObjModel * model = nullptr;
  CJellyFormat3dObjError err = cjelly_format_3d_obj_load(f.path(), &model);
  EXPECT_EQ(err, CJELLY_FORMAT_3D_OBJ_SUCCESS)
      << cjelly_format_3d_obj_strerror(err);
  return model;
}

} // namespace

//
// Error handling
//

TEST(ObjLoad, MissingFileReportsNotFound) {
  CJellyFormat3dObjModel * model = nullptr;
  EXPECT_EQ(cjelly_format_3d_obj_load(cjtest::missing_path(), &model),
      CJELLY_FORMAT_3D_OBJ_ERR_FILE_NOT_FOUND);
  EXPECT_EQ(model, nullptr);
}

TEST(ObjLoad, NullArgumentsRejected) {
  CJellyFormat3dObjModel * model = nullptr;
  EXPECT_EQ(cjelly_format_3d_obj_load(nullptr, &model),
      CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT);
  EXPECT_EQ(cjelly_format_3d_obj_load("whatever.obj", nullptr),
      CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT);
}

TEST(ObjLoad, StrerrorCoversEveryCode) {
  const CJellyFormat3dObjError codes[] = {CJELLY_FORMAT_3D_OBJ_SUCCESS,
      CJELLY_FORMAT_3D_OBJ_ERR_FILE_NOT_FOUND,
      CJELLY_FORMAT_3D_OBJ_ERR_OUT_OF_MEMORY,
      CJELLY_FORMAT_3D_OBJ_ERR_INVALID_FORMAT,
      CJELLY_FORMAT_3D_OBJ_ERR_IO};
  for (CJellyFormat3dObjError c : codes) {
    const char * msg = cjelly_format_3d_obj_strerror(c);
    ASSERT_NE(msg, nullptr);
    EXPECT_GT(strlen(msg), 0u);
  }
}

TEST(ObjLoad, EmptyFileYieldsEmptyModel) {
  CJellyFormat3dObjModel * model = load_text("");
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->vertex_count, 0);
  EXPECT_EQ(model->face_count, 0);
  cjelly_format_3d_obj_free(model);
}

TEST(ObjFree, NullIsSafe) {
  cjelly_format_3d_obj_free(nullptr);
}

//
// Geometry
//

TEST(ObjParse, VerticesTexcoordsAndNormals) {
  CJellyFormat3dObjModel * model = load_text(
      "v 1.0 2.0 3.0\n"
      "v -4.5 0.0 6.25\n"
      "vt 0.25 0.75\n"
      "vn 0.0 1.0 0.0\n");
  ASSERT_NE(model, nullptr);

  ASSERT_EQ(model->vertex_count, 2);
  EXPECT_FLOAT_EQ(model->vertices[0].x, 1.0f);
  EXPECT_FLOAT_EQ(model->vertices[0].y, 2.0f);
  EXPECT_FLOAT_EQ(model->vertices[0].z, 3.0f);
  EXPECT_FLOAT_EQ(model->vertices[1].x, -4.5f);
  EXPECT_FLOAT_EQ(model->vertices[1].z, 6.25f);

  ASSERT_EQ(model->texcoord_count, 1);
  EXPECT_FLOAT_EQ(model->texcoords[0].u, 0.25f);
  EXPECT_FLOAT_EQ(model->texcoords[0].v, 0.75f);

  ASSERT_EQ(model->normal_count, 1);
  EXPECT_FLOAT_EQ(model->normals[0].y, 1.0f);

  cjelly_format_3d_obj_free(model);
}

// OBJ indices are 1-based in the file and stored 0-based.
TEST(ObjParse, FaceIndicesAreZeroBased) {
  CJellyFormat3dObjModel * model = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n");
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->face_count, 1);
  EXPECT_EQ(model->faces[0].count, 3);
  EXPECT_EQ(model->faces[0].vertex[0], 0);
  EXPECT_EQ(model->faces[0].vertex[1], 1);
  EXPECT_EQ(model->faces[0].vertex[2], 2);
  // No texture or normal given: both absent.
  EXPECT_EQ(model->faces[0].texcoord[0], -1);
  EXPECT_EQ(model->faces[0].normal[0], -1);
  cjelly_format_3d_obj_free(model);
}

TEST(ObjParse, FaceWithVertexTexcoordNormal) {
  CJellyFormat3dObjModel * model = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vt 0 0\nvt 1 0\nvt 0 1\n"
      "vn 0 0 1\n"
      "f 1/1/1 2/2/1 3/3/1\n");
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->face_count, 1);
  const CJellyFormat3dObjFace & f = model->faces[0];
  ASSERT_EQ(f.count, 3);
  EXPECT_EQ(f.vertex[1], 1);
  EXPECT_EQ(f.texcoord[1], 1);
  EXPECT_EQ(f.normal[1], 0);
  cjelly_format_3d_obj_free(model);
}

TEST(ObjParse, FaceWithVertexAndTexcoordOnly) {
  CJellyFormat3dObjModel * model = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vt 0 0\nvt 1 0\nvt 0 1\n"
      "f 1/1 2/2 3/3\n");
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->face_count, 1);
  const CJellyFormat3dObjFace & f = model->faces[0];
  ASSERT_EQ(f.count, 3);
  EXPECT_EQ(f.vertex[2], 2);
  EXPECT_EQ(f.texcoord[2], 2);
  EXPECT_EQ(f.normal[2], -1);
  cjelly_format_3d_obj_free(model);
}

// "v//vn" omits the texture coordinate. The normal still has to survive.
TEST(ObjParse, FaceWithVertexAndNormalOnly) {
  CJellyFormat3dObjModel * model = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vn 0 0 1\nvn 0 1 0\n"
      "f 1//2 2//2 3//2\n");
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->face_count, 1);
  const CJellyFormat3dObjFace & f = model->faces[0];
  ASSERT_EQ(f.count, 3);
  EXPECT_EQ(f.vertex[0], 0);
  EXPECT_EQ(f.texcoord[0], -1) << "no texture coordinate was given";
  EXPECT_EQ(f.normal[0], 1) << "vn index 2 should map to normal 1";
  cjelly_format_3d_obj_free(model);
}

TEST(ObjParse, QuadFaceKeepsFourVertices) {
  CJellyFormat3dObjModel * model = load_text(
      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
      "f 1 2 3 4\n");
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->face_count, 1);
  ASSERT_EQ(model->faces[0].count, 4);
  EXPECT_EQ(model->faces[0].vertex[3], 3);
  EXPECT_EQ(model->faces[0].overflow, nullptr);
  cjelly_format_3d_obj_free(model);
}

// Faces beyond four vertices spill into the overflow array.
TEST(ObjParse, NgonFaceUsesOverflow) {
  CJellyFormat3dObjModel * model = load_text(
      "v 0 0 0\nv 1 0 0\nv 2 0 0\nv 3 0 0\nv 4 0 0\nv 5 0 0\nv 6 0 0\n"
      "f 1 2 3 4 5 6 7\n");
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->face_count, 1);
  const CJellyFormat3dObjFace & f = model->faces[0];
  ASSERT_EQ(f.count, 7);
  ASSERT_NE(f.overflow, nullptr);
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(f.vertex[i], i);
  }
  // Vertices five onward live in overflow, still 0-based.
  EXPECT_EQ(f.overflow[0].vertex, 4);
  EXPECT_EQ(f.overflow[1].vertex, 5);
  EXPECT_EQ(f.overflow[2].vertex, 6);
  cjelly_format_3d_obj_free(model);
}

// Growing past the initial 4-entry overflow allocation.
TEST(ObjParse, LargeNgonGrowsOverflow) {
  std::string text;
  for (int i = 0; i < 12; i++) {
    text += "v " + std::to_string(i) + " 0 0\n";
  }
  text += "f";
  for (int i = 1; i <= 12; i++) {
    text += " " + std::to_string(i);
  }
  text += "\n";

  CJellyFormat3dObjModel * model = load_text(text);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->face_count, 1);
  const CJellyFormat3dObjFace & f = model->faces[0];
  ASSERT_EQ(f.count, 12);
  ASSERT_NE(f.overflow, nullptr);
  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(f.overflow[i].vertex, i + 4) << "overflow entry " << i;
  }
  cjelly_format_3d_obj_free(model);
}

//
// Growth past the preallocated capacities
//

TEST(ObjParse, GrowsBeyondInitialCapacity) {
  // The parser preallocates 128 vertices and faces; push well past that.
  const int kCount = 500;
  std::string text;
  for (int i = 0; i < kCount; i++) {
    text += "v " + std::to_string(i) + " 0 0\n";
  }
  for (int i = 1; i + 2 <= kCount; i += 3) {
    text += "f " + std::to_string(i) + " " + std::to_string(i + 1) + " " +
        std::to_string(i + 2) + "\n";
  }

  CJellyFormat3dObjModel * model = load_text(text);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->vertex_count, kCount);
  EXPECT_FLOAT_EQ(model->vertices[kCount - 1].x, (float)(kCount - 1));
  EXPECT_GT(model->face_count, 128);
  cjelly_format_3d_obj_free(model);
}

//
// Groups, materials and comments
//

TEST(ObjParse, CommentsAndBlankLinesIgnored) {
  CJellyFormat3dObjModel * model = load_text(
      "# a comment\n"
      "\n"
      "v 1 2 3\n"
      "   \n"
      "# another\n");
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->vertex_count, 1);
  EXPECT_EQ(model->face_count, 0);
  cjelly_format_3d_obj_free(model);
}

TEST(ObjParse, MtllibRecorded) {
  CJellyFormat3dObjModel * model = load_text("mtllib materials.mtl\nv 0 0 0\n");
  ASSERT_NE(model, nullptr);
  EXPECT_STREQ(model->mtllib, "materials.mtl");
  cjelly_format_3d_obj_free(model);
}

TEST(ObjParse, GroupsRecorded) {
  CJellyFormat3dObjModel * model = load_text(
      "g first\n"
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n"
      "g second\n"
      "f 1 2 3\n");
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->group_count, 2);
  EXPECT_STREQ(model->groups[0].name, "first");
  EXPECT_STREQ(model->groups[1].name, "second");
  cjelly_format_3d_obj_free(model);
}

TEST(ObjParse, UsemtlAssignsMaterialToFollowingFaces) {
  CJellyFormat3dObjModel * model = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n"
      "usemtl red\n"
      "f 1 2 3\n"
      "usemtl blue\n"
      "f 1 2 3\n");
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->face_count, 3);
  EXPECT_EQ(model->faces[0].material_index, -1) << "before any usemtl";
  EXPECT_NE(model->faces[1].material_index, model->faces[0].material_index);
  EXPECT_NE(model->faces[2].material_index, model->faces[1].material_index);
  EXPECT_GE(model->material_mapping_count, 2);
  cjelly_format_3d_obj_free(model);
}

//
// Checked-in fixtures
//

TEST(ObjFixture, LoadsViolinCase) {
  CJellyFormat3dObjModel * model = nullptr;
  ASSERT_EQ(cjelly_format_3d_obj_load(
                asset("models/violin_case/violin_case.obj").c_str(), &model),
      CJELLY_FORMAT_3D_OBJ_SUCCESS);
  ASSERT_NE(model, nullptr);
  EXPECT_GT(model->vertex_count, 0);
  EXPECT_EQ(model->face_count, 944) << "the fixture has 944 'f' lines";
  EXPECT_STREQ(model->mtllib, "./vp.mtl") << "as written in the fixture";

  // Every index the parser produced must be in range; an off-by-one here
  // would read out of bounds downstream.
  for (int i = 0; i < model->face_count; i++) {
    const CJellyFormat3dObjFace & f = model->faces[i];
    ASSERT_GE(f.count, 3) << "face " << i;
    int inline_count = f.count < 4 ? f.count : 4;
    for (int j = 0; j < inline_count; j++) {
      EXPECT_GE(f.vertex[j], 0) << "face " << i << " vertex " << j;
      EXPECT_LT(f.vertex[j], model->vertex_count) << "face " << i;
    }
  }
  cjelly_format_3d_obj_free(model);
}

TEST(ObjFixture, LoadsStanfordBunny) {
  CJellyFormat3dObjModel * model = nullptr;
  ASSERT_EQ(
      cjelly_format_3d_obj_load(
          asset("models/stanford-bunny/stanford-bunny.obj").c_str(), &model),
      CJELLY_FORMAT_3D_OBJ_SUCCESS);
  ASSERT_NE(model, nullptr);
  EXPECT_GT(model->vertex_count, 30000);
  EXPECT_GT(model->face_count, 60000);
  for (int i = 0; i < model->face_count; i++) {
    const CJellyFormat3dObjFace & f = model->faces[i];
    for (int j = 0; j < (f.count < 4 ? f.count : 4); j++) {
      ASSERT_GE(f.vertex[j], 0) << "face " << i;
      ASSERT_LT(f.vertex[j], model->vertex_count) << "face " << i;
    }
  }
  cjelly_format_3d_obj_free(model);
}

TEST(ObjDump, WritesSomethingForALoadedModel) {
  CJellyFormat3dObjModel * model = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  ASSERT_NE(model, nullptr);
  FILE * sink = fopen("/dev/null", "w");
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(cjelly_format_3d_obj_dump(model, sink),
      CJELLY_FORMAT_3D_OBJ_SUCCESS);
  fclose(sink);
  cjelly_format_3d_obj_free(model);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
