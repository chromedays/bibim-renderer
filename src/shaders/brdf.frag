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

layout (location = 0) in vec2 vUV;
layout (location = 1) in vec3 vPosWorld;
layout (location = 2) in vec3 vNormalWorld;

layout (location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = vec3(-1, -1, 0);

    vec3 L = -normalize(lightDir);
    vec3 V = normalize(ub.viewPos - vPosWorld);
    vec3 N = normalize(vNormalWorld);
    vec3 H = normalize(L + V);
    float roughness = ub.roughness;

    float D = distributionGGX(N, H, roughness);
    vec3 F = fresnelSchlick(N, V, vec3(0.04));
    float G = geometrySmith(N, V, L, roughness);

    vec3 color;

    vec3 lightColor = vec3(23.47, 21.31, 20.79) * 0.1;
    vec3 radiance = lightColor;

    vec3 specular = (D * F * G) / max(4 * max(dot(V, N), 0) * max(dot(L, N), 0), 0.001);
    vec3 kS = F;
    vec3 kD = vec3(1) - kS;

    vec3 Lo = vec3(0);
    vec3 albedo = vec3(1);
    Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0);
    color = Lo;

    outColor = vec4(color, 1);
}