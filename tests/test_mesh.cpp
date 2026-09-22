/**
 * @file test_mesh.cpp
 *
 * Unit tests for turning a parsed OBJ into a GPU-ready mesh.
 *
 * This is the half of "load and display a model" that can be tested without a
 * GPU or a display, which is why it is a separate translation unit from the
 * render node that consumes it.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"
#include <ghoti.io/cjelly/format/3d/mesh.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string>

using cjtest::asset;
using cjtest::TempFile;
using cjtest::CountingAllocator;

namespace {

/** Build a mesh from an inline OBJ document, or fail the test. */
CJellyModelMesh * build(const std::string & text) {
  TempFile f(text);
  EXPECT_TRUE(f.valid());
  CJellyModelMesh * mesh = nullptr;
  CJellyModelMeshError err = cjelly_model_mesh_load(f.path(), nullptr, &mesh);
  EXPECT_EQ(err, CJELLY_MODEL_MESH_SUCCESS)
      << cjelly_model_mesh_strerror(err);
  return mesh;
}

/** Build from an inline document that is expected to fail. */
CJellyModelMeshError build_expecting_failure(const std::string & text) {
  TempFile f(text);
  EXPECT_TRUE(f.valid());
  CJellyModelMesh * mesh = nullptr;
  CJellyModelMeshError err = cjelly_model_mesh_load(f.path(), nullptr, &mesh);
  EXPECT_EQ(mesh, nullptr);
  cjelly_model_mesh_free(mesh);
  return err;
}

float length(const float * v) {
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

} // namespace

//
// Argument and error handling
//

TEST(MeshLoad, RejectsNullArguments) {
  CJellyModelMesh * mesh = nullptr;
  EXPECT_EQ(cjelly_model_mesh_load(nullptr, nullptr, &mesh),
      CJELLY_MODEL_MESH_ERR_INVALID);
  EXPECT_EQ(cjelly_model_mesh_load("x.obj", nullptr, nullptr),
      CJELLY_MODEL_MESH_ERR_INVALID);
  EXPECT_EQ(cjelly_model_mesh_from_obj(nullptr, nullptr, &mesh),
      CJELLY_MODEL_MESH_ERR_INVALID);
}

TEST(MeshLoad, MissingFileReportsLoadError) {
  CJellyModelMesh * mesh = nullptr;
  EXPECT_EQ(cjelly_model_mesh_load(cjtest::missing_path(), nullptr, &mesh),
      CJELLY_MODEL_MESH_ERR_LOAD);
  EXPECT_EQ(mesh, nullptr);
}

TEST(MeshFree, NullIsSafe) {
  cjelly_model_mesh_free(nullptr);
}

TEST(MeshLoad, StrerrorCoversEveryCode) {
  const CJellyModelMeshError codes[] = {CJELLY_MODEL_MESH_SUCCESS,
      CJELLY_MODEL_MESH_ERR_INVALID, CJELLY_MODEL_MESH_ERR_OUT_OF_MEMORY,
      CJELLY_MODEL_MESH_ERR_LOAD, CJELLY_MODEL_MESH_ERR_EMPTY};
  for (CJellyModelMeshError c : codes) {
    ASSERT_NE(cjelly_model_mesh_strerror(c), nullptr);
    EXPECT_GT(strlen(cjelly_model_mesh_strerror(c)), 0u);
  }
}

TEST(MeshBuild, AFileWithNoGeometryIsEmptyNotAnError) {
  EXPECT_EQ(build_expecting_failure("# nothing here\n"),
      CJELLY_MODEL_MESH_ERR_EMPTY);
  EXPECT_EQ(build_expecting_failure("v 0 0 0\nv 1 0 0\nv 0 1 0\n"),
      CJELLY_MODEL_MESH_ERR_EMPTY) << "vertices but no faces";
}

//
// Triangulation
//

TEST(MeshBuild, ATriangleStaysOneTriangle) {
  CJellyModelMesh * mesh = build("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->vertex_count, 3u);
  EXPECT_EQ(mesh->index_count, 3u);
  cjelly_model_mesh_free(mesh);
}

TEST(MeshBuild, AQuadBecomesTwoTriangles) {
  CJellyModelMesh * mesh =
      build("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->vertex_count, 4u);
  ASSERT_EQ(mesh->index_count, 6u);

  // A fan from the first corner: (0,1,2) and (0,2,3).
  EXPECT_EQ(mesh->indices[0], 0u);
  EXPECT_EQ(mesh->indices[1], 1u);
  EXPECT_EQ(mesh->indices[2], 2u);
  EXPECT_EQ(mesh->indices[3], 0u);
  EXPECT_EQ(mesh->indices[4], 2u);
  EXPECT_EQ(mesh->indices[5], 3u);
  cjelly_model_mesh_free(mesh);
}

// An n-gon of N corners fans into N-2 triangles, including the corners the
// OBJ parser keeps in its overflow array.
TEST(MeshBuild, AnNgonFansIntoTriangles) {
  std::string text;
  const int kCorners = 9;
  for (int i = 0; i < kCorners; i++) {
    double angle = 2.0 * 3.14159265358979 * i / kCorners;
    text += "v " + std::to_string(std::cos(angle)) + " " +
        std::to_string(std::sin(angle)) + " 0\n";
  }
  text += "f";
  for (int i = 1; i <= kCorners; i++) {
    text += " " + std::to_string(i);
  }
  text += "\n";

  CJellyModelMesh * mesh = build(text);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->vertex_count, (uint32_t)kCorners);
  EXPECT_EQ(mesh->index_count, (uint32_t)(kCorners - 2) * 3)
      << "nine corners should make seven triangles";

  // Every index has to address a vertex that exists.
  for (uint32_t i = 0; i < mesh->index_count; i++) {
    EXPECT_LT(mesh->indices[i], mesh->vertex_count) << "index " << i;
  }
  cjelly_model_mesh_free(mesh);
}

TEST(MeshBuild, IndexCountIsAlwaysAMultipleOfThree) {
  CJellyModelMesh * mesh = build(
      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\n"
      "f 1 2 3\n"
      "f 1 2 3 4\n"
      "f 1 2 3 4 5\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->index_count % 3, 0u);
  EXPECT_EQ(mesh->index_count, 3u + 6u + 9u);
  cjelly_model_mesh_free(mesh);
}

//
// Out-of-range indices
//

// The OBJ parser does not reject an index that names a vertex which does not
// exist, so the mesh builder has to. Reading it would be a read past the end
// of the position array.
TEST(MeshBuild, FacesNamingAMissingVertexAreDropped) {
  CJellyModelMesh * mesh = build(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n"
      "f 1 2 99\n"
      "f 1 2 3\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->dropped_faces, 1u);
  EXPECT_EQ(mesh->index_count, 6u) << "the two good faces survive";
  cjelly_model_mesh_free(mesh);
}

TEST(MeshBuild, AFaceWithFewerThanThreeCornersIsDropped) {
  CJellyModelMesh * mesh = build(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2\n"
      "f 1 2 3\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->dropped_faces, 1u);
  EXPECT_EQ(mesh->index_count, 3u);
  cjelly_model_mesh_free(mesh);
}

TEST(MeshBuild, AFileOfNothingButBadFacesIsEmpty) {
  EXPECT_EQ(build_expecting_failure("v 0 0 0\nf 7 8 9\n"),
      CJELLY_MODEL_MESH_ERR_EMPTY);
}

//
// Normals
//

TEST(MeshBuild, NormalsFromTheFileAreUsed) {
  CJellyModelMesh * mesh = build(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vn 0 0 1\n"
      "f 1//1 2//1 3//1\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->generated_normals, 0);
  for (uint32_t i = 0; i < mesh->vertex_count; i++) {
    EXPECT_FLOAT_EQ(mesh->vertices[i].normal[2], 1.0f) << "vertex " << i;
  }
  cjelly_model_mesh_free(mesh);
}

// A file with no vn lines renders unlit, and therefore black, unless the
// normals are computed. The Stanford bunny is such a file.
TEST(MeshBuild, NormalsAreGeneratedWhenTheFileHasNone) {
  CJellyModelMesh * mesh = build("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->generated_normals, 1);
  for (uint32_t i = 0; i < mesh->vertex_count; i++) {
    EXPECT_NEAR(length(mesh->vertices[i].normal), 1.0f, 1e-5f)
        << "vertex " << i << " has a non-unit normal";
    // Counter-clockwise in the XY plane faces +Z.
    EXPECT_NEAR(mesh->vertices[i].normal[2], 1.0f, 1e-5f);
  }
  cjelly_model_mesh_free(mesh);
}

TEST(MeshBuild, GeneratedNormalsAreSharedBetweenAdjacentFaces) {
  CJellyModelMesh * mesh = nullptr;
  ASSERT_EQ(cjelly_model_mesh_load(asset("models/triangle_no_normals.obj").c_str(), nullptr, &mesh),
      CJELLY_MODEL_MESH_SUCCESS);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->generated_normals, 1);
  EXPECT_EQ(mesh->index_count, 6u);
  for (uint32_t i = 0; i < mesh->vertex_count; i++) {
    EXPECT_NEAR(length(mesh->vertices[i].normal), 1.0f, 1e-5f);
  }
  cjelly_model_mesh_free(mesh);
}

// A degenerate triangle has no direction to contribute. It must not produce a
// NaN normal, which would take the lighting with it.
TEST(MeshBuild, DegenerateTrianglesDoNotProduceNaNs) {
  CJellyModelMesh * mesh = build("v 0 0 0\nv 0 0 0\nv 0 0 0\nf 1 2 3\n");
  ASSERT_NE(mesh, nullptr);
  for (uint32_t i = 0; i < mesh->vertex_count; i++) {
    for (int axis = 0; axis < 3; axis++) {
      EXPECT_FALSE(std::isnan(mesh->vertices[i].normal[axis]))
          << "vertex " << i << " axis " << axis;
    }
  }
  cjelly_model_mesh_free(mesh);
}

//
// Texture coordinates
//

// OBJ measures v from the bottom of the image and Vulkan samples from the
// top, so the coordinate is flipped on the way in.
TEST(MeshBuild, TextureCoordinateVIsFlippedForVulkan) {
  CJellyModelMesh * mesh = build(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vt 0.25 0.75\n"
      "f 1/1 2/1 3/1\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_FLOAT_EQ(mesh->vertices[0].texcoord[0], 0.25f);
  EXPECT_FLOAT_EQ(mesh->vertices[0].texcoord[1], 0.25f) << "1 - 0.75";
  cjelly_model_mesh_free(mesh);
}

TEST(MeshBuild, MissingTextureCoordinatesAreZero) {
  CJellyModelMesh * mesh = build("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_FLOAT_EQ(mesh->vertices[0].texcoord[0], 0.0f);
  EXPECT_FLOAT_EQ(mesh->vertices[0].texcoord[1], 0.0f);
  cjelly_model_mesh_free(mesh);
}

//
// Bounds
//

TEST(MeshBuild, BoundsCentreAndRadiusDescribeTheModel) {
  CJellyModelMesh * mesh = build(
      "v -1 -2 -3\nv 1 2 3\nv 0 0 0\n"
      "f 1 2 3\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_FLOAT_EQ(mesh->bounds_min[0], -1.0f);
  EXPECT_FLOAT_EQ(mesh->bounds_min[2], -3.0f);
  EXPECT_FLOAT_EQ(mesh->bounds_max[1], 2.0f);
  EXPECT_FLOAT_EQ(mesh->center[0], 0.0f);
  EXPECT_FLOAT_EQ(mesh->center[1], 0.0f);
  EXPECT_FLOAT_EQ(mesh->center[2], 0.0f);
  EXPECT_NEAR(mesh->radius, std::sqrt(1.0f + 4.0f + 9.0f), 1e-5f);
  cjelly_model_mesh_free(mesh);
}

TEST(MeshBuild, AnOffCentreModelReportsAnOffCentreCentre) {
  CJellyModelMesh * mesh =
      build("v 10 10 10\nv 12 10 10\nv 10 12 10\nf 1 2 3\n");
  ASSERT_NE(mesh, nullptr);
  EXPECT_FLOAT_EQ(mesh->center[0], 11.0f);
  EXPECT_FLOAT_EQ(mesh->center[1], 11.0f);
  EXPECT_FLOAT_EQ(mesh->center[2], 10.0f);
  EXPECT_GT(mesh->radius, 0.0f);
  cjelly_model_mesh_free(mesh);
}

//
// The checked-in cube
//

TEST(MeshFixture, LoadsTheCube) {
  CJellyModelMesh * mesh = nullptr;
  ASSERT_EQ(cjelly_model_mesh_load(asset("models/cube.obj").c_str(), nullptr, &mesh),
      CJELLY_MODEL_MESH_SUCCESS);
  ASSERT_NE(mesh, nullptr);

  // Six quads, each two triangles, each triangle three indices.
  EXPECT_EQ(mesh->index_count, 36u);
  EXPECT_EQ(mesh->vertex_count, 24u) << "four corners per face, six faces";
  EXPECT_EQ(mesh->dropped_faces, 0u);
  EXPECT_EQ(mesh->generated_normals, 0) << "the fixture supplies normals";

  EXPECT_FLOAT_EQ(mesh->bounds_min[0], -0.5f);
  EXPECT_FLOAT_EQ(mesh->bounds_max[0], 0.5f);
  EXPECT_NEAR(mesh->center[0], 0.0f, 1e-6f);
  EXPECT_NEAR(mesh->radius, std::sqrt(0.75f), 1e-5f) << "half the diagonal";

  for (uint32_t i = 0; i < mesh->vertex_count; i++) {
    EXPECT_NEAR(length(mesh->vertices[i].normal), 1.0f, 1e-5f)
        << "vertex " << i;
  }
  for (uint32_t i = 0; i < mesh->index_count; i++) {
    ASSERT_LT(mesh->indices[i], mesh->vertex_count) << "index " << i;
  }
  cjelly_model_mesh_free(mesh);
}

TEST(MeshFixture, TheCubeNamesAMaterialLibraryThatParses) {
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(asset("models/cube.obj").c_str(), nullptr,
                nullptr, &obj),
      GMDL_OK);
  ASSERT_NE(obj, nullptr);
  EXPECT_STREQ(obj->mtllib, "cube.mtl");

  GMDL_Mtl * mtl = nullptr;
  ASSERT_EQ(gmdl_mtl_load_file(asset("models/cube.mtl").c_str(), nullptr,
                nullptr, &mtl),
      GMDL_OK);
  ASSERT_NE(mtl, nullptr);
  for (size_t i = 0; i < obj->material_mapping_count; i++) {
    EXPECT_NE(gmdl_mtl_find(mtl, obj->material_mappings[i].name), nullptr)
        << "usemtl " << obj->material_mappings[i].name;
  }
  gmdl_mtl_free(mtl);
  gmdl_obj_free(obj);
}

//
// The allocator
//

TEST(MeshAllocator, LoadAndFreeGoThroughTheCallersAllocator) {
  CountingAllocator alloc;
  CJellyModelMesh * mesh = nullptr;
  ASSERT_EQ(cjelly_model_mesh_load(
                cjtest::asset("models/cube.obj").c_str(), alloc.get(), &mesh),
      CJELLY_MODEL_MESH_SUCCESS);
  ASSERT_NE(mesh, nullptr);

  EXPECT_GT(alloc.allocations(), 0u)
      << "the mesh was built without asking the allocator for anything";
  EXPECT_GT(alloc.live(), 0);
  EXPECT_EQ(mesh->allocator, alloc.get());

  // The vertex and index arrays are stolen from a GCU_Array, so they only
  // come back if that array was created with this allocator too.
  const size_t after_load = alloc.allocations();
  cjelly_model_mesh_free(mesh);
  EXPECT_EQ(alloc.live(), 0)
      << "a stolen array buffer was allocated somewhere else";
  EXPECT_EQ(alloc.allocations(), after_load) << "free must not allocate";
}

// The OBJ parse happens inside the model library, which takes the same
// allocator - so this covers the handoff between the two libraries.
TEST(MeshAllocator, TheObjParseUsesItToo) {
  CountingAllocator alloc;
  CJellyModelMesh * mesh = nullptr;
  TempFile f("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  ASSERT_EQ(cjelly_model_mesh_load(f.path(), alloc.get(), &mesh),
      CJELLY_MODEL_MESH_SUCCESS);
  const size_t with_parse = alloc.allocations();
  cjelly_model_mesh_free(mesh);
  EXPECT_EQ(alloc.live(), 0);
  EXPECT_GT(with_parse, 1u)
      << "one allocation would mean only the mesh struct came from here";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
