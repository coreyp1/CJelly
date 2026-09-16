/**
 * @file mat4.h
 * @brief 4x4 matrix helpers for placing a model in front of a camera.
 *
 * Column-major, the layout GLSL expects, so a `CJellyMat4` can be handed
 * straight to a `mat4` push constant with no transpose.
 *
 * The projection is built for Vulkan's clip space rather than OpenGL's: depth
 * runs 0 to 1 instead of -1 to 1, and Y points down. Getting either wrong
 * produces a picture rather than an error - an upside-down model, or one
 * clipped away entirely - so both are tested.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_CJ_MAT4_H
#define GHOTI_IO_CJ_MAT4_H

#include <ghoti.io/cjelly/macros.h>


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @brief A 4x4 matrix in column-major order.
 *
 * `m[column * 4 + row]`, matching GLSL.
 */
typedef struct CJellyMat4 {
  float m[16]; /**< The elements, column-major */
} CJellyMat4;

/**
 * @brief The identity matrix.
 * @return The identity.
 */
CJellyMat4 cjelly_mat4_identity(void);

/**
 * @brief Matrix product, applying @p b first and then @p a.
 * @param a The second transform to apply.
 * @param b The first transform to apply.
 * @return The product a * b.
 */
CJellyMat4 cjelly_mat4_multiply(CJellyMat4 a, CJellyMat4 b);

/**
 * @brief A translation.
 * @param x Translation along X.
 * @param y Translation along Y.
 * @param z Translation along Z.
 * @return The transform.
 */
CJellyMat4 cjelly_mat4_translation(float x, float y, float z);

/**
 * @brief A uniform scale.
 * @param s The factor.
 * @return The transform.
 */
CJellyMat4 cjelly_mat4_scale(float s);

/**
 * @brief A rotation about the X axis.
 * @param radians The angle.
 * @return The transform.
 */
CJellyMat4 cjelly_mat4_rotation_x(float radians);

/**
 * @brief A rotation about the Y axis.
 * @param radians The angle.
 * @return The transform.
 */
CJellyMat4 cjelly_mat4_rotation_y(float radians);

/**
 * @brief A right-handed view matrix.
 * @param eye Camera position.
 * @param center Point the camera looks at.
 * @param up Approximate up direction.
 * @return The view transform.
 */
CJellyMat4 cjelly_mat4_look_at(
    const float eye[3], const float center[3], const float up[3]);

/**
 * @brief A perspective projection for Vulkan clip space.
 *
 * Y is flipped and depth runs 0 to 1, so the result can be used directly
 * without the `gl_Position.y = -gl_Position.y` fixup.
 *
 * @param fovy_radians Vertical field of view.
 * @param aspect Width divided by height.
 * @param near_plane Near plane distance, greater than zero.
 * @param far_plane Far plane distance, greater than @p near_plane.
 * @return The projection.
 */
CJellyMat4 cjelly_mat4_perspective(
    float fovy_radians, float aspect, float near_plane, float far_plane);

/**
 * @brief How far a camera has to sit to fit a bounding sphere on screen.
 *
 * The tighter of the two field-of-view angles governs: a viewport wider than
 * it is tall is limited by its height, and the reverse when it is taller than
 * it is wide. Framing on the vertical angle alone puts a tall window's model
 * partly off screen.
 *
 * @param radius Radius of the sphere to fit, about its centre.
 * @param fovy_radians Vertical field of view.
 * @param aspect Width divided by height.
 * @return The distance from the sphere's centre to the camera.
 */
float cjelly_camera_distance_for_sphere(
    float radius, float fovy_radians, float aspect);

/**
 * @brief Transform a point, keeping the w component.
 * @param matrix The transform.
 * @param point The point.
 * @param out_result Receives (x, y, z, w).
 */
void cjelly_mat4_transform_point(
    const CJellyMat4 * matrix, const float point[3], float out_result[4]);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // GHOTI_IO_CJ_MAT4_H
