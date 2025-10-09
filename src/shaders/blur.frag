#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform BlurParams {
    vec2 texelSize;     // 1.0 / texture_size
    vec2 direction;     // blur direction (1,0) for horizontal, (0,1) for vertical
    float intensity;    // blur intensity (0.0 = no blur, 1.0 = max blur)
    float time;         // time parameter for animation
} blurParams;

void main() {
    vec2 uv = inTexCoord;
    
    // Sample the input texture
    vec4 color = texture(inputTexture, uv);
    
    // Apply simple blur effect
    float blurRadius = blurParams.intensity * 5.0; // Scale intensity to reasonable blur radius
    
    // Simple blur by sampling nearby pixels
    vec4 blurredColor = color;
    
    // Sample neighboring pixels for blur effect
    for (int i = -2; i <= 2; i++) {
        for (int j = -2; j <= 2; j++) {
            vec2 offset = vec2(float(i), float(j)) * blurParams.texelSize * blurRadius;
            blurredColor += texture(inputTexture, uv + offset);
        }
    }
    
    // Average the samples
    blurredColor /= 25.0; // 5x5 sampling = 25 samples
    
    // Add some animated color variation based on time
    float timeFactor = sin(blurParams.time * 0.5) * 0.1;
    blurredColor.r += timeFactor;
    blurredColor.g += timeFactor * 0.5;
    blurredColor.b += timeFactor * 0.3;
    
    outColor = blurredColor;
}
