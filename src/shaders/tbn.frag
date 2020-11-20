#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 gColor;
layout (location = 0) out vec4 outColor;

void main() {
    outColor = vec4(gColor, 1);
}