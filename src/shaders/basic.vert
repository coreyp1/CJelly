#version 450

// Input from the vertex buffer: position and color.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

// Pass color to the fragment shader.
layout(location = 0) out vec3 fragColor;

void main() {
    // Convert the 2D position to a 4D clip-space coordinate.
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}
