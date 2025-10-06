#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec4 uv;
    vec4 colorMul;
} pc;

void main() {
    // vPos spans [-0.5,0.5]. Map to [0,1] for gradients
    float uvx = clamp(vPos.x + 0.5, 0.0, 1.0);
    float uvy = clamp(1.0 - (vPos.y + 0.5), 0.0, 1.0); // top=0, bottom=1

    // Compute two gradients for the whole quad
    float gradRed = uvx;                                   // horizontal left->right
    float gradGreen = clamp((uvx + uvy) * 0.5, 0.0, 1.0);  // diagonal TL->BR

    // Select by push-constant colorMul (r vs g dominance)
    float useGreen = step(pc.colorMul.r, pc.colorMul.g);
    float factor = mix(gradRed, gradGreen, useGreen);

    // Output pure red/green modulated by factor
    vec3 color = mix(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), useGreen);
    vec3 shaded = color * factor;
    outColor = vec4(shaded, 1.0);
}


