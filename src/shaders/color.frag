#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec4 uv;
    vec4 colorMul;
} pc;

void main() {
    // Map vPos from [-0.5, 0.5] to [0, 1] for UV coordinates
    vec2 uv = (vPos + 0.5);

    // Choose gradient based on colorMul
    if (pc.colorMul.r > pc.colorMul.g) {
        // Red horizontal gradient
        float redGradient = uv.x; // 0 to 1 from left to right
        outColor = vec4(redGradient, 0.0, 0.0, 1.0);
    } else {
        // Green diagonal gradient
        float greenGradient = (uv.x + uv.y) * 0.5; // 0 to 1 diagonal
        outColor = vec4(0.0, greenGradient, 0.0, 1.0);
    }
}


