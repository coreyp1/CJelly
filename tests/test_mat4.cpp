/**
 * @file test_mat4.cpp
 *
 * Unit tests for the camera and projection maths.
 *
 * These matter more than they look: a sign error in the projection does not
 * fail, it renders an upside-down or invisible model, and there is no way to
 * tell which from the code alone. Testing the clip-space conventions directly
 * is the only cheap check available without a GPU.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/cjelly/mat4.h>
#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979f;

float element(const CJellyMat4 & m, int row, int col) {
  return m.m[col * 4 + row];
}

} // namespace

TEST(Mat4, IdentityIsIdentity) {
  CJellyMat4 i = cjelly_mat4_identity();
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      EXPECT_FLOAT_EQ(element(i, row, col), row == col ? 1.0f : 0.0f)
          << "row " << row << " col " << col;
    }
  }
}

TEST(Mat4, MultiplyingByIdentityChangesNothing) {
  CJellyMat4 t = cjelly_mat4_translation(1.0f, 2.0f, 3.0f);
  CJellyMat4 left = cjelly_mat4_multiply(cjelly_mat4_identity(), t);
  CJellyMat4 right = cjelly_mat4_multiply(t, cjelly_mat4_identity());
  for (int i = 0; i < 16; i++) {
    EXPECT_FLOAT_EQ(left.m[i], t.m[i]) << "element " << i;
    EXPECT_FLOAT_EQ(right.m[i], t.m[i]) << "element " << i;
  }
}

TEST(Mat4, TranslationMovesAPoint) {
  CJellyMat4 t = cjelly_mat4_translation(1.0f, 2.0f, 3.0f);
  const float p[3] = {10.0f, 20.0f, 30.0f};
  float out[4];
  cjelly_mat4_transform_point(&t, p, out);
  EXPECT_FLOAT_EQ(out[0], 11.0f);
  EXPECT_FLOAT_EQ(out[1], 22.0f);
  EXPECT_FLOAT_EQ(out[2], 33.0f);
  EXPECT_FLOAT_EQ(out[3], 1.0f);
}

TEST(Mat4, ScaleScales) {
  CJellyMat4 s = cjelly_mat4_scale(2.5f);
  const float p[3] = {1.0f, -2.0f, 4.0f};
  float out[4];
  cjelly_mat4_transform_point(&s, p, out);
  EXPECT_FLOAT_EQ(out[0], 2.5f);
  EXPECT_FLOAT_EQ(out[1], -5.0f);
  EXPECT_FLOAT_EQ(out[2], 10.0f);
}

// The order matters, and getting it backwards is the classic bug: this is
// "translate, then scale", which moves the model further out.
TEST(Mat4, MultiplicationAppliesTheRightOperandFirst) {
  CJellyMat4 scale = cjelly_mat4_scale(2.0f);
  CJellyMat4 translate = cjelly_mat4_translation(1.0f, 0.0f, 0.0f);
  CJellyMat4 scale_then_translate = cjelly_mat4_multiply(scale, translate);

  const float p[3] = {0.0f, 0.0f, 0.0f};
  float out[4];
  cjelly_mat4_transform_point(&scale_then_translate, p, out);
  EXPECT_FLOAT_EQ(out[0], 2.0f) << "the translation is scaled too";

  CJellyMat4 translate_then_scale = cjelly_mat4_multiply(translate, scale);
  cjelly_mat4_transform_point(&translate_then_scale, p, out);
  EXPECT_FLOAT_EQ(out[0], 1.0f) << "the translation happens after the scale";
}

TEST(Mat4, RotationYByNinetyDegreesMapsXToMinusZ) {
  CJellyMat4 r = cjelly_mat4_rotation_y(kPi * 0.5f);
  const float p[3] = {1.0f, 0.0f, 0.0f};
  float out[4];
  cjelly_mat4_transform_point(&r, p, out);
  EXPECT_NEAR(out[0], 0.0f, 1e-6f);
  EXPECT_NEAR(out[1], 0.0f, 1e-6f);
  EXPECT_NEAR(out[2], -1.0f, 1e-6f);
}

TEST(Mat4, RotationXByNinetyDegreesMapsYToZ) {
  CJellyMat4 r = cjelly_mat4_rotation_x(kPi * 0.5f);
  const float p[3] = {0.0f, 1.0f, 0.0f};
  float out[4];
  cjelly_mat4_transform_point(&r, p, out);
  EXPECT_NEAR(out[0], 0.0f, 1e-6f);
  EXPECT_NEAR(out[1], 0.0f, 1e-6f);
  EXPECT_NEAR(out[2], 1.0f, 1e-6f);
}

TEST(Mat4, AFullTurnIsTheIdentity) {
  CJellyMat4 r = cjelly_mat4_rotation_y(2.0f * kPi);
  CJellyMat4 i = cjelly_mat4_identity();
  for (int e = 0; e < 16; e++) {
    EXPECT_NEAR(r.m[e], i.m[e], 1e-5f) << "element " << e;
  }
}

//
// The view transform
//

TEST(Mat4, LookAtPutsTheTargetOnTheNegativeZAxis) {
  // A right-handed view looks down -Z.
  const float eye[3] = {0.0f, 0.0f, 5.0f};
  const float center[3] = {0.0f, 0.0f, 0.0f};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  CJellyMat4 view = cjelly_mat4_look_at(eye, center, up);

  float out[4];
  cjelly_mat4_transform_point(&view, center, out);
  EXPECT_NEAR(out[0], 0.0f, 1e-6f);
  EXPECT_NEAR(out[1], 0.0f, 1e-6f);
  EXPECT_NEAR(out[2], -5.0f, 1e-5f) << "five units in front of the camera";
}

TEST(Mat4, LookAtPutsTheEyeAtTheOrigin) {
  const float eye[3] = {3.0f, 4.0f, 5.0f};
  const float center[3] = {0.0f, 0.0f, 0.0f};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  CJellyMat4 view = cjelly_mat4_look_at(eye, center, up);

  float out[4];
  cjelly_mat4_transform_point(&view, eye, out);
  EXPECT_NEAR(out[0], 0.0f, 1e-5f);
  EXPECT_NEAR(out[1], 0.0f, 1e-5f);
  EXPECT_NEAR(out[2], 0.0f, 1e-5f);
}

TEST(Mat4, LookAtKeepsUpPointingUp) {
  const float eye[3] = {0.0f, 0.0f, 5.0f};
  const float center[3] = {0.0f, 0.0f, 0.0f};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  CJellyMat4 view = cjelly_mat4_look_at(eye, center, up);

  const float above[3] = {0.0f, 1.0f, 0.0f};
  float out[4];
  cjelly_mat4_transform_point(&view, above, out);
  EXPECT_GT(out[1], 0.0f) << "a point above the target stays above it";
}

//
// The projection, and Vulkan's clip-space conventions
//

TEST(Mat4, PerspectiveMapsTheNearPlaneToZeroAndTheFarPlaneToOne) {
  // Vulkan's depth range is 0..1, unlike OpenGL's -1..1. A projection built
  // for OpenGL puts half the scene behind the near plane.
  const float near_plane = 0.1f;
  const float far_plane = 100.0f;
  CJellyMat4 p = cjelly_mat4_perspective(kPi / 4.0f, 1.0f, near_plane,
      far_plane);

  const float at_near[3] = {0.0f, 0.0f, -near_plane};
  float out[4];
  cjelly_mat4_transform_point(&p, at_near, out);
  ASSERT_GT(out[3], 0.0f) << "w must be positive in front of the camera";
  EXPECT_NEAR(out[2] / out[3], 0.0f, 1e-5f);

  const float at_far[3] = {0.0f, 0.0f, -far_plane};
  cjelly_mat4_transform_point(&p, at_far, out);
  ASSERT_GT(out[3], 0.0f);
  EXPECT_NEAR(out[2] / out[3], 1.0f, 1e-5f);
}

TEST(Mat4, PerspectiveFlipsYForVulkan) {
  // Vulkan's Y axis points down in clip space. Without the flip the model
  // renders upside down, which reads as a bad model rather than a bad matrix.
  CJellyMat4 p = cjelly_mat4_perspective(kPi / 4.0f, 1.0f, 0.1f, 100.0f);
  const float above[3] = {0.0f, 1.0f, -5.0f};
  float out[4];
  cjelly_mat4_transform_point(&p, above, out);
  ASSERT_GT(out[3], 0.0f);
  EXPECT_LT(out[1] / out[3], 0.0f)
      << "a point above the centre lands in the upper half, which is negative "
         "Y in Vulkan";
}

TEST(Mat4, PerspectiveRespectsAspectRatio) {
  CJellyMat4 wide = cjelly_mat4_perspective(kPi / 4.0f, 2.0f, 0.1f, 100.0f);
  CJellyMat4 square = cjelly_mat4_perspective(kPi / 4.0f, 1.0f, 0.1f, 100.0f);
  const float off_axis[3] = {1.0f, 0.0f, -5.0f};
  float wide_out[4];
  float square_out[4];
  cjelly_mat4_transform_point(&wide, off_axis, wide_out);
  cjelly_mat4_transform_point(&square, off_axis, square_out);
  EXPECT_LT(std::fabs(wide_out[0] / wide_out[3]),
      std::fabs(square_out[0] / square_out[3]))
      << "a wider viewport spreads the same point over less of X";
}

TEST(Mat4, PointsBehindTheCameraGetANegativeW) {
  CJellyMat4 p = cjelly_mat4_perspective(kPi / 4.0f, 1.0f, 0.1f, 100.0f);
  const float behind[3] = {0.0f, 0.0f, 5.0f};
  float out[4];
  cjelly_mat4_transform_point(&p, behind, out);
  EXPECT_LT(out[3], 0.0f) << "which is what clips it away";
}

TEST(Mat4, TransformPointToleratesNulls) {
  CJellyMat4 p = cjelly_mat4_identity();
  const float point[3] = {0.0f, 0.0f, 0.0f};
  float out[4] = {9.0f, 9.0f, 9.0f, 9.0f};
  cjelly_mat4_transform_point(nullptr, point, out);
  cjelly_mat4_transform_point(&p, nullptr, out);
  cjelly_mat4_transform_point(&p, point, nullptr);
  EXPECT_FLOAT_EQ(out[0], 9.0f) << "nothing was written";
}

// The whole chain, the way the render node assembles it: a model centred on
// the origin and framed by the camera has to land inside the unit cube of
// normalized device coordinates.
TEST(Mat4, AFramedModelLandsInsideClipSpace) {
  const float half_extent = 2.0f;
  // The sphere that actually contains the box reaches its corners, not its
  // faces. Framing on the half-extent leaves the corners off screen.
  const float radius = half_extent * std::sqrt(3.0f);
  const float fovy = kPi / 4.0f;
  const float aspect = 16.0f / 9.0f;
  const float distance =
      cjelly_camera_distance_for_sphere(radius, fovy, aspect);
  const float eye[3] = {0.0f, 0.0f, distance};
  const float center[3] = {0.0f, 0.0f, 0.0f};
  const float up[3] = {0.0f, 1.0f, 0.0f};

  CJellyMat4 view = cjelly_mat4_look_at(eye, center, up);
  CJellyMat4 proj = cjelly_mat4_perspective(
      fovy, aspect, 0.1f, distance + radius * 2.0f);
  CJellyMat4 mvp = cjelly_mat4_multiply(proj, view);

  // The eight corners of the model's bounding box.
  for (int i = 0; i < 8; i++) {
    const float corner[3] = {(i & 1) ? half_extent : -half_extent,
        (i & 2) ? half_extent : -half_extent,
        (i & 4) ? half_extent : -half_extent};
    float out[4];
    cjelly_mat4_transform_point(&mvp, corner, out);
    ASSERT_GT(out[3], 0.0f) << "corner " << i << " is behind the camera";
    float ndc_x = out[0] / out[3];
    float ndc_y = out[1] / out[3];
    float ndc_z = out[2] / out[3];
    EXPECT_GE(ndc_z, 0.0f) << "corner " << i << " is in front of the near plane";
    EXPECT_LE(ndc_z, 1.0f) << "corner " << i << " is inside the far plane";
    EXPECT_LE(std::fabs(ndc_x), 1.0f) << "corner " << i << " is off screen in X";
    EXPECT_LE(std::fabs(ndc_y), 1.0f) << "corner " << i << " is off screen in Y";
  }
}

TEST(CameraFraming, ATallerViewportNeedsMoreDistance) {
  const float radius = 1.0f;
  const float fovy = kPi / 4.0f;
  float wide = cjelly_camera_distance_for_sphere(radius, fovy, 2.0f);
  float square = cjelly_camera_distance_for_sphere(radius, fovy, 1.0f);
  float tall = cjelly_camera_distance_for_sphere(radius, fovy, 0.5f);
  EXPECT_FLOAT_EQ(wide, square)
      << "past square the vertical angle is the tighter one, so it governs";
  EXPECT_GT(tall, square) << "a tall window is limited by its width";
}

TEST(CameraFraming, DistanceScalesWithRadius) {
  const float fovy = kPi / 4.0f;
  float one = cjelly_camera_distance_for_sphere(1.0f, fovy, 1.0f);
  float ten = cjelly_camera_distance_for_sphere(10.0f, fovy, 1.0f);
  EXPECT_NEAR(ten, one * 10.0f, 1e-4f);
}

TEST(CameraFraming, AWiderFieldOfViewNeedsLessDistance) {
  float narrow = cjelly_camera_distance_for_sphere(1.0f, kPi / 8.0f, 1.0f);
  float wide = cjelly_camera_distance_for_sphere(1.0f, kPi / 2.0f, 1.0f);
  EXPECT_GT(narrow, wide);
}

TEST(CameraFraming, NonsenseInputsDoNotDivideByZero) {
  EXPECT_GT(cjelly_camera_distance_for_sphere(0.0f, kPi / 4.0f, 1.0f), 0.0f);
  EXPECT_GT(cjelly_camera_distance_for_sphere(-1.0f, kPi / 4.0f, 1.0f), 0.0f);
  EXPECT_GT(cjelly_camera_distance_for_sphere(1.0f, 0.0f, 1.0f), 0.0f);
  float zero_aspect = cjelly_camera_distance_for_sphere(1.0f, kPi / 4.0f, 0.0f);
  EXPECT_GT(zero_aspect, 0.0f);
  EXPECT_FALSE(std::isnan(zero_aspect));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
