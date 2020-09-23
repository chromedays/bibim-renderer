#version 450

layout (binding = 0) uniform UniformBlock {
    mat4 viewMat;
    mat4 projMat;
    vec3 viewPos;
    vec3 albedo;
    float metallic;
    float roughness;
    float ao;
    int visualizeOption;
} ub;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in mat4 aModel;
layout (location = 7) in mat4 aInvModel;
layout (location = 11) in vec3 aAlbedo;
layout (location = 12) in float aMetallic;
layout (location = 13) in float aRoughness;
layout (location = 14) in float aAO;

layout (location = 0) out vec2 vUV;
layout (location = 1) out vec3 vPosWorld;
layout (location = 2) out vec3 vNormalWorld;
layout (location = 3) out flat vec3 vAlbedo;
layout (location = 4) out flat vec3 vMRA; // Metallic, Roughness, AO

void main() {
    vec4 posWorld = aModel * vec4(aPosition, 1.0);
    vPosWorld = posWorld.xyz;
    gl_Position = ub.projMat * ub.viewMat * posWorld;
    vUV = aUV;
    mat3 normalMat = transpose(mat3(aInvModel));
    vNormalWorld = normalize(normalMat * aNormal);
    vAlbedo = aAlbedo;
    vMRA = vec3(aMetallic, aRoughness, aAO);
}