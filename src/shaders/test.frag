#version 450

layout (location = 0) in vec3 vColor;
layout (location = 1) in vec2 vUV;
layout (location = 2) in vec3 vPosWorld;
layout (location = 3) in vec3 vNormalWorld;

layout (location = 0) out vec4 outColor;

layout (binding = 1) uniform sampler2D uSampler;

void main() {
    vec3 lightDir = vec3(1, 1, -1);
    vec3 viewPos = vec3(1, 1.5, -1);

    vec3 L = -normalize(lightDir);
    vec3 V = -normalize(viewPos);
    vec3 N = -normalize(vNormalWorld);
    outColor = texture(uSampler, vUV) * vec4(vec3(max(dot(N, L), 0)), 1.0);

    // outColor = vec4((N + vec3(1)) * 0.5, 1.0);
    //outColor = texture(uSampler, vUV);//vec4(vUV, 0, 1.0);
}