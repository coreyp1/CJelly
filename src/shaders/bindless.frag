#version 450

// Enable descriptor indexing extension
#extension GL_EXT_nonuniform_qualifier : enable

// Input from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in flat uint fragTextureID;

// Bindless texture array - this is the key to bindless rendering
layout(set = 0, binding = 0) uniform sampler2D textures[];

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // If texture ID is 0, use solid color (no texture)
    if (fragTextureID == 0u) {
        outColor = vec4(fragColor, 1.0);
    } else {
        // Use the texture ID to index into the bindless texture array
        // The nonuniformEXT qualifier is required for dynamic indexing
        vec4 texColor = texture(nonuniformEXT(textures[fragTextureID]), fragTexCoord);
        
        // Blend texture with vertex color
        outColor = vec4(fragColor * texColor.rgb, texColor.a);
    }
}
