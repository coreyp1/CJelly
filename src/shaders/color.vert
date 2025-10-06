#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vPos;

// Keep the push constant layout consistent (uv, colorMul)
layout(push_constant) uniform PC {
    vec4 uv;        // unused here
    vec4 colorMul;  // used in frag
} pc;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    vColor = inColor;
    vPos = inPos; // in [-0.5, 0.5]
}


