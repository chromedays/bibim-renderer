#version 450

#include "brdf.glsl"
#include "debug.glsl"

layout (binding = 0) uniform UniformBlock {
    mat4 modelMat;
    mat4 invModelMat;
    mat4 viewMat;
    mat4 projMat;
    vec3 viewPos;
    float roughness;
    int visualizeOption;
} ub;

layout (location = 0) in vec3 vColor;
layout (location = 1) in vec2 vUV;
layout (location = 2) in vec3 vPosWorld;
layout (location = 3) in vec3 vNormalWorld;

layout (location = 0) out vec4 outColor;

layout (binding = 1) uniform sampler2D uSampler;

void main() {
    vec3 lightDir = vec3(-1, -1, 0);

    vec3 L = -normalize(lightDir);
    vec3 V = normalize(ub.viewPos - vPosWorld);
    vec3 N = normalize(vNormalWorld);
    vec3 H = normalize(L + V);
    float a = ub.roughness;
    float k = a + 1;
    k = (k * k) / 8;

    float D = distributionGGX(N, H, a);
    float F = fresnelSchlick(N, V, 0.04);
    float G = geometrySmith(N, V, L, k);

    vec3 color;
    if (ub.visualizeOption == 0) {
        color = colorizeNormal(N);
    } else if (ub.visualizeOption == 1) {
        color = colorizeNormal(H);
    } else if (ub.visualizeOption == 2) {
        color = vec3(D);
    } else if (ub.visualizeOption == 3) {
        color = vec3(F);
    } else if (ub.visualizeOption == 4) {
        color = vec3(G);
    }

    outColor = vec4(color, 1);

    // outColor = texture(uSampler, vUV) * vec4(vec3(max(dot(N, L), 0)), 1.0);
}