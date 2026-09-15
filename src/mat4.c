/**
 * @file mat4.c
 *
 * 4x4 matrix helpers. Column-major throughout.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <math.h>
#include <string.h>

#include <cjelly/mat4.h>

/** Index of the element at (row, column) in column-major order. */
#define AT(col, row) ((col) * 4 + (row))

CJellyMat4 cjelly_mat4_identity(void) {
  CJellyMat4 result;
  memset(result.m, 0, sizeof(result.m));
  result.m[AT(0, 0)] = 1.0f;
  result.m[AT(1, 1)] = 1.0f;
  result.m[AT(2, 2)] = 1.0f;
  result.m[AT(3, 3)] = 1.0f;
  return result;
}

CJellyMat4 cjelly_mat4_multiply(CJellyMat4 a, CJellyMat4 b) {
  CJellyMat4 result;
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        sum += a.m[AT(k, row)] * b.m[AT(col, k)];
      }
      result.m[AT(col, row)] = sum;
    }
  }
  return result;
}

CJellyMat4 cjelly_mat4_translation(float x, float y, float z) {
  CJellyMat4 result = cjelly_mat4_identity();
  result.m[AT(3, 0)] = x;
  result.m[AT(3, 1)] = y;
  result.m[AT(3, 2)] = z;
  return result;
}

CJellyMat4 cjelly_mat4_scale(float s) {
  CJellyMat4 result = cjelly_mat4_identity();
  result.m[AT(0, 0)] = s;
  result.m[AT(1, 1)] = s;
  result.m[AT(2, 2)] = s;
  return result;
}

CJellyMat4 cjelly_mat4_rotation_x(float radians) {
  CJellyMat4 result = cjelly_mat4_identity();
  float c = cosf(radians);
  float s = sinf(radians);
  result.m[AT(1, 1)] = c;
  result.m[AT(1, 2)] = s;
  result.m[AT(2, 1)] = -s;
  result.m[AT(2, 2)] = c;
  return result;
}

CJellyMat4 cjelly_mat4_rotation_y(float radians) {
  CJellyMat4 result = cjelly_mat4_identity();
  float c = cosf(radians);
  float s = sinf(radians);
  result.m[AT(0, 0)] = c;
  result.m[AT(0, 2)] = -s;
  result.m[AT(2, 0)] = s;
  result.m[AT(2, 2)] = c;
  return result;
}

static void subtract3(const float * a, const float * b, float * out) {
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
  out[2] = a[2] - b[2];
}

static void cross3(const float * a, const float * b, float * out) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static float dot3(const float * a, const float * b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void normalize3(float * v) {
  float length = sqrtf(dot3(v, v));
  if (length > 1e-20f) {
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
  }
}

CJellyMat4 cjelly_mat4_look_at(
    const float eye[3], const float center[3], const float up[3]) {
  float forward[3];
  subtract3(center, eye, forward);
  normalize3(forward);

  float side[3];
  cross3(forward, up, side);
  normalize3(side);

  float true_up[3];
  cross3(side, forward, true_up);

  CJellyMat4 result = cjelly_mat4_identity();
  result.m[AT(0, 0)] = side[0];
  result.m[AT(1, 0)] = side[1];
  result.m[AT(2, 0)] = side[2];
  result.m[AT(0, 1)] = true_up[0];
  result.m[AT(1, 1)] = true_up[1];
  result.m[AT(2, 1)] = true_up[2];
  result.m[AT(0, 2)] = -forward[0];
  result.m[AT(1, 2)] = -forward[1];
  result.m[AT(2, 2)] = -forward[2];
  result.m[AT(3, 0)] = -dot3(side, eye);
  result.m[AT(3, 1)] = -dot3(true_up, eye);
  result.m[AT(3, 2)] = dot3(forward, eye);
  return result;
}

CJellyMat4 cjelly_mat4_perspective(
    float fovy_radians, float aspect, float near_plane, float far_plane) {
  CJellyMat4 result;
  memset(result.m, 0, sizeof(result.m));

  float f = 1.0f / tanf(fovy_radians * 0.5f);
  result.m[AT(0, 0)] = f / aspect;
  // Negative: Vulkan's Y axis points down in clip space, where OpenGL's points
  // up. Leaving this positive renders the model upside down, which looks like
  // a model problem rather than a projection one.
  result.m[AT(1, 1)] = -f;
  // Depth maps to 0..1 rather than -1..1, again unlike OpenGL.
  result.m[AT(2, 2)] = far_plane / (near_plane - far_plane);
  result.m[AT(2, 3)] = -1.0f;
  result.m[AT(3, 2)] = (near_plane * far_plane) / (near_plane - far_plane);
  return result;
}

float cjelly_camera_distance_for_sphere(
    float radius, float fovy_radians, float aspect) {
  if (radius <= 0.0f || fovy_radians <= 0.0f) {
    return 1.0f;
  }
  if (aspect <= 0.0f) {
    aspect = 1.0f;
  }

  // The horizontal angle follows from the vertical one and the aspect ratio.
  float half_fovy = fovy_radians * 0.5f;
  float half_fovx = atanf(tanf(half_fovy) * aspect);
  float half_angle = half_fovy < half_fovx ? half_fovy : half_fovx;

  float sine = sinf(half_angle);
  if (sine < 1e-4f) {
    return radius * 1000.0f;
  }
  return radius / sine;
}

void cjelly_mat4_transform_point(
    const CJellyMat4 * matrix, const float point[3], float out_result[4]) {
  if (!matrix || !point || !out_result) {
    return;
  }
  for (int row = 0; row < 4; row++) {
    out_result[row] = matrix->m[AT(0, row)] * point[0]
        + matrix->m[AT(1, row)] * point[1] + matrix->m[AT(2, row)] * point[2]
        + matrix->m[AT(3, row)];
  }
}
