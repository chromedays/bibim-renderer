#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 vColor;
layout (location = 1) in vec3 vNormal;

layout (location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0, 0, 1));
    vec3 L = -lightDir;
    vec3 N = normalize(vNormal);
    float diff = max(dot(L, N), 0.0);

    outColor = vec4(vColor * diff, 1);
}