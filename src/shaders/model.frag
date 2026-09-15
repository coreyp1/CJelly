#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ModelPush {
  mat4 mvp;
  vec4 normalRow0;
  vec4 normalRow1;
  vec4 normalRow2;
  vec4 baseColor;
} push;

void main() {
  vec3 normal = normalize(fragNormal);
  vec3 lightDir = normalize(vec3(0.35, 0.75, 0.55));

  float facing = dot(normal, lightDir);

  // Lit from both sides. OBJ files in the wild are not reliably wound
  // consistently, and a one-sided term turns every reversed triangle into a
  // black hole in the surface - which reads as a broken mesh rather than a
  // lighting choice. The back side is dimmer so the shape still reads.
  float diffuse = max(facing, 0.0) + max(-facing, 0.0) * 0.35;

  float ambient = 0.18;
  vec3 lit = push.baseColor.rgb * (ambient + 0.82 * diffuse);

  outColor = vec4(lit, push.baseColor.a);
}
