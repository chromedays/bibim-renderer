#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in mat4 aModel;
layout (location = 8) in mat4 aInvModel;


layout (location = 0) out vec4 vPosWorld;
layout (location = 1) out vec2 vUV;
layout (location = 2) out vec3 vNormalWorld;
layout (location = 3) out mat3 vTBN;

void main() {
    vec4 posWorld = aModel * vec4(aPosition, 1.0);
    vec4 posView = uViewMat * posWorld;
    
    gl_Position = uProjMat * posView;


    mat3 normalMat = transpose(mat3(aInvModel));
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(normalMat * aTangent);
    vec3 B = cross(N, T);

    vNormalWorld = N;
    vPosWorld = posWorld;
    

    vTBN = mat3(T, B, N);
    vUV = aUV;
}