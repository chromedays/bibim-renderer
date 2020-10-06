#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 vColor;

layout (location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vColor, 1);
}