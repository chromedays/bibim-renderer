#version 450

layout (binding = 0) uniform UniformBlock {
    mat4 modelMat;
    mat4 viewMat;
    mat4 projMat;
} ub;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec2 aUV;

layout (location = 0) out vec3 vColor;
layout (location = 1) out vec2 vUV;

void main() {
    gl_Position = ub.projMat * ub.viewMat * ub.modelMat * vec4(aPosition, 1.0);
    vColor = aColor;
    vUV = aUV;
}