#version 450

// The model node's own copy of the full-screen quad shader.
//
// It could have reused textured.vert, but the generated SPIR-V headers define
// their arrays with external linkage, so including one in a second translation
// unit is a duplicate-symbol link error. Its own pair keeps the node
// self-contained instead of depending on which other file happens to include
// what.
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
  gl_Position = vec4(inPos, 0.0, 1.0);
  fragTexCoord = inTexCoord;
}
