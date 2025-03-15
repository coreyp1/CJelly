#version 450

// Receive the interpolated color from the vertex shader.
layout(location = 0) in vec3 fragColor;

// Output the final pixel color.
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
