#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 aPos;
layout (location = 1) in int aLightIndex;

layout (location = 0) out vec3 vColor; 

void main() {
    mat4 modelMat = mat4(1);
    modelMat[3] = vec4(uLights[gl_InstanceIndex].pos, 1);

    gl_Position = uProjMat * uViewMat * modelMat * vec4(aPos, 1);
    vColor = uLights[gl_InstanceIndex].color;
}