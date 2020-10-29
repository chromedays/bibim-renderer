#version 450

#include "standard_sets.glsl"

layout (location = 0) out vec2 vUV;
void main() {
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0f + -1.0f, 0.0f, 1.0f);
}