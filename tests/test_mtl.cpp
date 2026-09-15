/**
 * @file test_mtl.cpp
 *
 * Unit tests for the Wavefront MTL material parser.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"
#include <cjelly/format/3d/mtl.h>
#include <gtest/gtest.h>
#include <string>

using cjtest::asset;
using cjtest::TempFile;

namespace {

/** RAII wrapper so a failing assertion cannot leak the material array. */
class Mtl {
public:
  ~Mtl() { cjelly_format_3d_mtl_free(&lib_); }
  CJellyFormat3dMtl * operator->() { return &lib_; }
  CJellyFormat3dMtl * get() { return &lib_; }

private:
  CJellyFormat3dMtl lib_{};
};

} // namespace

//
// Error handling
//

TEST(MtlLoad, MissingFileReportsNotFound) {
  Mtl lib;
  EXPECT_EQ(cjelly_format_3d_mtl_load(cjtest::missing_path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_ERR_FILE_NOT_FOUND);
  EXPECT_EQ(lib->material_count, 0);
}

TEST(MtlLoad, StrerrorCoversEveryCode) {
  const CJellyFormat3dMtlError codes[] = {CJELLY_FORMAT_3D_MTL_SUCCESS,
      CJELLY_FORMAT_3D_MTL_ERR_FILE_NOT_FOUND,
      CJELLY_FORMAT_3D_MTL_ERR_OUT_OF_MEMORY,
      CJELLY_FORMAT_3D_MTL_ERR_INVALID_FORMAT,
      CJELLY_FORMAT_3D_MTL_ERR_IO};
  for (CJellyFormat3dMtlError c : codes) {
    const char * msg = cjelly_format_3d_mtl_strerror(c);
    ASSERT_NE(msg, nullptr);
    EXPECT_GT(strlen(msg), 0u);
  }
}

TEST(MtlLoad, EmptyFileYieldsNoMaterials) {
  TempFile f("", ".mtl");
  Mtl lib;
  EXPECT_EQ(cjelly_format_3d_mtl_load(f.path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  EXPECT_EQ(lib->material_count, 0);
}

// Properties before any newmtl have nowhere to go and must not be written
// through a null current-material pointer.
TEST(MtlLoad, PropertiesBeforeFirstNewmtlIgnored) {
  TempFile f("Ka 1 1 1\nKd 1 1 1\nillum 2\nnewmtl real\nNs 5\n", ".mtl");
  Mtl lib;
  ASSERT_EQ(cjelly_format_3d_mtl_load(f.path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  ASSERT_EQ(lib->material_count, 1);
  EXPECT_STREQ(lib->materials[0].name, "real");
  EXPECT_FLOAT_EQ(lib->materials[0].Ns, 5.0f);
}

TEST(MtlLoad, MalformedColourReportsInvalidFormat) {
  TempFile f("newmtl broken\nKa 1 1\n", ".mtl");
  Mtl lib;
  EXPECT_EQ(cjelly_format_3d_mtl_load(f.path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_ERR_INVALID_FORMAT);
  EXPECT_EQ(lib->material_count, 0);
}

TEST(MtlFree, NullAndEmptyAreSafe) {
  cjelly_format_3d_mtl_free(nullptr);
  CJellyFormat3dMtl empty{};
  cjelly_format_3d_mtl_free(&empty);
}

//
// Parsing
//

TEST(MtlParse, ReadsEveryProperty) {
  TempFile f(
      "newmtl shiny\n"
      "Ka 0.1 0.2 0.3\n"
      "Kd 0.4 0.5 0.6\n"
      "Ks 0.7 0.8 0.9\n"
      "Ns 42.5\n"
      "d 0.25\n"
      "illum 2\n",
      ".mtl");
  Mtl lib;
  ASSERT_EQ(cjelly_format_3d_mtl_load(f.path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  ASSERT_EQ(lib->material_count, 1);
  const CJellyFormat3dMtlMaterial & m = lib->materials[0];
  EXPECT_STREQ(m.name, "shiny");
  EXPECT_FLOAT_EQ(m.Ka[0], 0.1f);
  EXPECT_FLOAT_EQ(m.Ka[2], 0.3f);
  EXPECT_FLOAT_EQ(m.Kd[1], 0.5f);
  EXPECT_FLOAT_EQ(m.Ks[2], 0.9f);
  EXPECT_FLOAT_EQ(m.Ns, 42.5f);
  EXPECT_FLOAT_EQ(m.d, 0.25f);
  EXPECT_EQ(m.illum, 2);
}

// Each newmtl starts from a clean slate, so a property set on one material
// must not leak into the next.
TEST(MtlParse, MaterialsAreIndependent) {
  TempFile f(
      "newmtl first\n"
      "Ka 1 1 1\n"
      "Ns 99\n"
      "newmtl second\n"
      "Kd 0.5 0.5 0.5\n",
      ".mtl");
  Mtl lib;
  ASSERT_EQ(cjelly_format_3d_mtl_load(f.path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  ASSERT_EQ(lib->material_count, 2);
  EXPECT_STREQ(lib->materials[1].name, "second");
  EXPECT_FLOAT_EQ(lib->materials[1].Ns, 0.0f) << "Ns belonged to 'first'";
  EXPECT_FLOAT_EQ(lib->materials[1].Ka[0], 0.0f) << "Ka belonged to 'first'";
  EXPECT_FLOAT_EQ(lib->materials[1].Kd[0], 0.5f);
}

TEST(MtlParse, CommentsIgnored) {
  TempFile f("# a comment\nnewmtl m\n# another\nNs 1\n", ".mtl");
  Mtl lib;
  ASSERT_EQ(cjelly_format_3d_mtl_load(f.path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  ASSERT_EQ(lib->material_count, 1);
  EXPECT_FLOAT_EQ(lib->materials[0].Ns, 1.0f);
}

// The array starts small and doubles; push well past the initial capacity.
TEST(MtlParse, GrowsBeyondInitialCapacity) {
  std::string text;
  const int kCount = 200;
  for (int i = 0; i < kCount; i++) {
    text += "newmtl mat" + std::to_string(i) + "\n";
    text += "Ns " + std::to_string(i) + "\n";
  }
  TempFile f(text, ".mtl");
  Mtl lib;
  ASSERT_EQ(cjelly_format_3d_mtl_load(f.path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  ASSERT_EQ(lib->material_count, kCount);
  EXPECT_STREQ(lib->materials[kCount - 1].name,
      ("mat" + std::to_string(kCount - 1)).c_str());
  EXPECT_FLOAT_EQ(lib->materials[kCount - 1].Ns, (float)(kCount - 1));
}

//
// Checked-in fixture
//

TEST(MtlFixture, LoadsViolinCaseMaterials) {
  Mtl lib;
  ASSERT_EQ(cjelly_format_3d_mtl_load(
                asset("models/violin_case/vp.mtl").c_str(), lib.get()),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  EXPECT_EQ(lib->material_count, 72) << "the fixture has 72 newmtl lines";
  EXPECT_STREQ(lib->materials[0].name, "white");
  EXPECT_FLOAT_EQ(lib->materials[0].Kd[0], 1.0f);
  EXPECT_EQ(lib->materials[0].illum, 2);
  EXPECT_FLOAT_EQ(lib->materials[0].Ns, 60.0f);
  for (int i = 0; i < lib->material_count; i++) {
    EXPECT_GT(strlen(lib->materials[i].name), 0u) << "material " << i;
  }
}

TEST(MtlDump, WritesSomethingForLoadedMaterials) {
  TempFile f("newmtl m\nKa 1 1 1\nKd 1 1 1\nKs 1 1 1\nNs 1\nillum 2\n", ".mtl");
  Mtl lib;
  ASSERT_EQ(cjelly_format_3d_mtl_load(f.path(), lib.get()),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  FILE * sink = fopen("/dev/null", "w");
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(
      cjelly_format_3d_mtl_dump(lib->materials, lib->material_count, sink),
      CJELLY_FORMAT_3D_MTL_SUCCESS);
  fclose(sink);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
