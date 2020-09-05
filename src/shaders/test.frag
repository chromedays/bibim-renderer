#version 450

layout (location = 0) in vec3 vColor;
layout (location = 1) in vec2 vUV;

layout (location = 0) out vec4 outColor;

layout (binding = 1) uniform sampler2D uSampler;

void main() {
    outColor = texture(uSampler, vUV);//vec4(vUV, 0, 1.0);
}