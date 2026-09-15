#version 450

// Vertex layout must match CJellyModelVertex in cjelly/format/3d/mesh.h.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// 128 bytes exactly, which is all Vulkan guarantees for push constants. The
// three rows carry the model matrix's rotation, used to move normals into
// world space; a fourth would not fit alongside the colour.
layout(push_constant) uniform ModelPush {
  mat4 mvp;
  vec4 normalRow0;
  vec4 normalRow1;
  vec4 normalRow2;
  vec4 baseColor;
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;

void main() {
  gl_Position = push.mvp * vec4(inPosition, 1.0);
  fragNormal = vec3(dot(push.normalRow0.xyz, inNormal),
                    dot(push.normalRow1.xyz, inNormal),
                    dot(push.normalRow2.xyz, inNormal));
  fragTexCoord = inTexCoord;
}
