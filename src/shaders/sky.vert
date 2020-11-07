#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 aPosition;
layout (location = 0) out vec3 vModelPos;

void main() {
    mat4 rotViewMat = mat4(mat3(uViewMat));
    vec4 clipPos = uProjMat * rotViewMat * vec4(aPosition, 1.0);
    clipPos.z = 0;
    gl_Position = clipPos;
    vModelPos = aPosition;
}