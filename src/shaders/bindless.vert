#version 450

// Input from the vertex buffer: position, color, and texture ID
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in uint inTextureID;

// Pass data to the fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out flat uint fragTextureID;

void main() {
    // Convert the 2D position to a 4D clip-space coordinate
    gl_Position = vec4(inPosition, 0.0, 1.0);

    // Pass through the color and texture ID
    fragColor = inColor;
    fragTextureID = inTextureID;

    // Generate texture coordinates based on position
    // This creates a simple UV mapping for the quad
    fragTexCoord = (inPosition + vec2(0.5, 0.5));
}
