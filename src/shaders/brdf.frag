#version 450

#include "brdf.glsl"

#define MAX_NUM_LIGHTS 100
#define NUM_MATERIALS 1

struct Light {
    vec3 pos;
    int type; // 0 = point light, 1 = spot light, 2 = directional light
    vec3 dir;
    float intensity;
    vec3 color;
    float innerCutOff;
    float outerCutOff;
};

layout (binding = 0) uniform UniformBlock {
    mat4 viewMat;
    mat4 projMat;
    vec3 viewPos;
    int numLights;
    Light lights[MAX_NUM_LIGHTS];
} ub;

layout (binding = 1) uniform sampler uSamplers[2]; // 0 - Nearest, 1 - Bilinear
layout (binding = 2) uniform texture2D uAlbedoMap[NUM_MATERIALS];
layout (binding = 3) uniform texture2D uMetallicMap[NUM_MATERIALS];
layout (binding = 4) uniform texture2D uRoughnessMap[NUM_MATERIALS];
layout (binding = 5) uniform texture2D uAOMap[NUM_MATERIALS];
layout (binding = 6) uniform texture2D uNormalMap[NUM_MATERIALS];
layout (binding = 7) uniform texture2D uHeightMap[NUM_MATERIALS];

layout (location = 0) in vec2 vUV;
layout (location = 1) in vec3 vPosWorld;
layout (location = 2) in mat3 vTBN;
layout (location = 5) in flat vec3 vAlbedo;
layout (location = 6) in flat vec3 vMRA; // Metallic, Roughness, AO
layout (location = 7) in flat int vMaterialIndex;

layout (location = 0) out vec4 outColor;

void main() {
    vec3 albedo = texture(sampler2D(uAlbedoMap[vMaterialIndex], uSamplers[1]), vUV).rgb;
    float metallic = texture(sampler2D(uMetallicMap[vMaterialIndex], uSamplers[1]), vUV).r;
    float roughness = texture(sampler2D(uRoughnessMap[vMaterialIndex], uSamplers[1]), vUV).r;
    float ao = texture(sampler2D(uAOMap[vMaterialIndex], uSamplers[1]), vUV).r;
    vec3 normal = vTBN * (texture(sampler2D(uNormalMap[vMaterialIndex], uSamplers[0]), vUV).xyz * 2 - 1);
    //normal = vTBN * vec3(0, 0, 1);
    //normal = vTBN[2];

    vec3 Lo = vec3(0);

    for (int i = 0; i < ub.numLights; ++i) {
        Light light = ub.lights[i];
        vec3 L;
        float att;
        if (light.type == 0) {
            L = light.pos - vPosWorld;
            float d = length(L);
            att = 1 / (d * d);
            L = normalize(L);
        } else if (light.type == 1) {
            L = light.pos - vPosWorld;
            float d = length(L);
            att = 1 / (d * d);
            L = normalize(L);
            float theta = dot(L, normalize(-light.dir));
            float epsilon = light.innerCutOff - light.outerCutOff;
            att *= clamp((theta - light.outerCutOff) / epsilon, 0, 1);
        } else if (light.type == 2) {
            L = -normalize(light.dir);
            att = 1;
        }
            
        vec3 V = normalize(ub.viewPos - vPosWorld);
        vec3 N = normalize(normal);
        vec3 H = normalize(L + V);

        float D = distributionGGX(N, H, roughness);

        vec3 F0 = vec3(0.04);
        F0 = mix(F0, albedo, metallic);
        vec3 F = fresnelSchlick(H, V, F0);
        float G = geometrySmith(N, V, L, roughness);

        vec3 radiance = att * light.color * light.intensity;

        vec3 specular = (D * F * G) / max(4 * max(dot(V, N), 0) * max(dot(L, N), 0), 0.001);
        vec3 kS = F;
        vec3 kD = vec3(1) - kS;
        kD *= (1 - metallic);

        Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0);
    }

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lo;

    outColor = vec4(color, 1);
}