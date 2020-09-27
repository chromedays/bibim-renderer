#version 450
#include "debug_common.glsl"

layout (binding = 0) uniform UniformBlock {
    mat4 viewMat;
    mat4 projMat;
    vec3 viewPos;
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
layout (location = 15) in int aMaterialIndex;

layout (location = 0) out vec3 vNormal;
layout (location = 1) out vec3 vColor;
layout (location = 2) out flat mat4 vModel;

void main() {
    gl_Position = vec4(aPosition, 1);
    
    mat3 normalMat = transpose(mat3(aInvModel));
    vNormal = normalize(normalMat * aNormal);
    vAlbedo = aAlbedo;
    vMRA = vec3(aMetallic, aRoughness, aAO);
    vMaterialIndex = aMaterialIndex;
}