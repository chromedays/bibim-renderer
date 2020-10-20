#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in mat4 aModel;
layout (location = 8) in mat4 aInvModel;
// layout (location = 12) in vec3 aAlbedo;
// layout (location = 13) in float aMetallic;
// layout (location = 14) in float aRoughness;
// layout (location = 15) in float aAO;

layout (location = 0) out vec2 vUV;
layout (location = 1) out vec3 vPosWorld;
// (Tangent/Bitangent/Normal) matrix. Used to transform normal from model space to tangent space
layout (location = 2) out flat mat3 vTBN; 
//layout (location = 5) out flat vec3 vAlbedo;
//layout (location = 6) out flat vec3 vMRA; // Metallic, Roughness, AO

void main() {
    vec4 posWorld = aModel * vec4(aPosition, 1.0);
    vPosWorld = posWorld.xyz;
    gl_Position = uProjMat * uViewMat * posWorld;
    vUV = aUV;

    // TODO(ilgwon): Pass normal matrix through instance data
    mat3 normalMat = transpose(mat3(aInvModel));
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(normalMat * aTangent);
    vec3 B = cross(N, T);
    vTBN = mat3(T, B, N);

    //vAlbedo = aAlbedo;
    //vMRA = vec3(aMetallic, aRoughness, aAO);
}