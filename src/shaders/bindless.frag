#version 450

// Enable descriptor indexing extension
#extension GL_EXT_nonuniform_qualifier : enable

// Input from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in flat uint fragTextureID;

// Single texture sampler (interim to isolate crash)
layout(set = 0, binding = 0) uniform sampler2D textures;

// Push constants: uv mapping for atlas, and a color multiplier
layout(push_constant) uniform Push {
    vec4 uv;        // xy = scale, zw = offset
    vec4 colorMul;  // rgba multiplier
} pc;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // If texture ID is 0, use solid color (no texture)
    if (fragTextureID == 0u) {
        // Color-only path: apply color multiplier from push constants
        outColor = vec4(fragColor * pc.colorMul.rgb, 1.0);
    } else {
        // Map the quad UV into the atlas sub-rectangle
        vec2 uvCoord = pc.uv.xy * fragTexCoord + pc.uv.zw;
        vec4 texColor = texture(textures, uvCoord);

        // Blend texture with vertex color and color multiplier
        outColor = vec4(fragColor * texColor.rgb * pc.colorMul.rgb, texColor.a * pc.colorMul.a);
    }
}
