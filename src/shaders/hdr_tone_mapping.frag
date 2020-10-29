#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec2 vUV;

layout (location = 0) out vec4 outColor;

void main() {
    vec3 hdrColor = texture(sampler2D(uHDRBuffer, uSamplers[SMP_NEAREST]), vUV).rgb;
    vec3 mapped;
    if (uEnableToneMapping != 0) {
        mapped = vec3(1.0) - exp(-hdrColor * uExposure);
    } else {
        mapped = hdrColor;
    }
    outColor = vec4(mapped, 1.0);
}