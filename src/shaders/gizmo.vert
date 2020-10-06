#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec3 aNormal;

layout (location = 0) out vec3 vColor;
layout (location = 1) out vec3 vNormal;

void main() {
    vec3 right = vec3(uViewMat[0][0], uViewMat[1][0], uViewMat[2][0]);
    vec3 up = vec3(uViewMat[0][1], uViewMat[1][1], uViewMat[2][1]);
    vec3 look = vec3(uViewMat[0][2], uViewMat[1][2], uViewMat[2][2]);
    vec3 viewPos = look * -27;
    mat4 viewMat = uViewMat;
    viewMat[3][0] = -dot(viewPos, right);
    viewMat[3][1] = -dot(viewPos, up);
    viewMat[3][2] = -dot(viewPos, look);
    mat4 projMat = uProjMat;
    float d = 1.f / tan(0.261799); // 30 degrees fov
    projMat[0][0] = d;
    projMat[1][1] = -d;
    gl_Position = projMat * viewMat * vec4(aPosition, 1);
    vColor = aColor;
    vNormal = mat3(viewMat) * aNormal;
}