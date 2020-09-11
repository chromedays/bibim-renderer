#version 450

layout (binding = 0) uniform UniformBlock {
    mat4 modelMat;
    mat4 invModelMat;
    mat4 viewMat;
    mat4 projMat;
    vec3 viewPos;
    float roughness;
    int visualizeOption;
} ub;

layout (location = 0) in vec3 vColor;
layout (location = 1) in vec2 vUV;
layout (location = 2) in vec3 vPosWorld;
layout (location = 3) in vec3 vNormalWorld;

layout (location = 0) out vec4 outColor;

layout (binding = 1) uniform sampler2D uSampler;

const float PI = 3.1415926535897932384626433832795;

// Trowbridge-Reitz GGX
float distributionGGX(vec3 N, vec3 H, float a) {
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = NdotH2 * (a2 - 1) + 1;
    denom = PI * denom * denom;

    return num / denom;
}

float geometrySchlickGGX(float NdotV, float k) {
    float num = NdotV;
    float denom = NdotV * (1 - k) + k;
    return num / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float k) {
    float NdotV = max(dot(N, V), 0);
    float NdotL = max(dot(N, L), 0);
    float ggx1 = geometrySchlickGGX(NdotV, k);
    float ggx2 = geometrySchlickGGX(NdotL, k);
    return ggx1 * ggx2;
}

float fresnelSchlick(vec3 H, vec3 V, float F0) {
    return F0 + (1 - F0) * pow(1 - max(dot(H, V), 0), 5);
}

vec3 colorizeNormal(vec3 n)
{
    return (n + vec3(1)) * 0.5;
}

void main() {
    vec3 lightDir = vec3(-1, -1, 0);

    vec3 L = -normalize(lightDir);
    vec3 V = normalize(ub.viewPos - vPosWorld);
    vec3 N = normalize(vNormalWorld);
    vec3 H = normalize(L + V);
    float a = ub.roughness;
    float k = a + 1;
    k = (k * k) / 8;

    float D = distributionGGX(N, H, a);
    float F = fresnelSchlick(N, V, 0.04);
    float G = geometrySmith(N, V, L, k);

    vec3 color;
    if (ub.visualizeOption == 0) {
        color = colorizeNormal(N);
    } else if (ub.visualizeOption == 1) {
        color = colorizeNormal(H);
    } else if (ub.visualizeOption == 2) {
        color = vec3(D);
    } else if (ub.visualizeOption == 3) {
        color = vec3(F);
    } else if (ub.visualizeOption == 4) {
        color = vec3(G);
    }

    outColor = vec4(color, 1);

    // outColor = texture(uSampler, vUV) * vec4(vec3(max(dot(N, L), 0)), 1.0);
}