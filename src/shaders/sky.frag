#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 vModelPos;
layout (location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(vModelPos);

    const vec2 invAtan = vec2(0.1591, 0.3183);
    vec2 uv = vec2(atan(normal.z, normal.x), asin(normal.y));
    uv *= invAtan;
    uv += 0.5;
    uv.x = 1.0 - uv.x;
    uv.y = 1.0 - uv.y;

    outColor = vec4(texture(sampler2D(uEnvMap, uSamplers[SMP_LINEAR]), uv).rgb, 1);
}