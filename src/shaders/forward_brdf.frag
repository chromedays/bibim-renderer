#version 450

#include "brdf.glsl"
#include "standard_sets.glsl"

layout (location = 0) in vec2 vUV;
layout (location = 1) in vec3 vPosWorld;
layout (location = 2) in vec3 vNormalWorld;
layout (location = 3) in mat3 vTBN;
//layout (location = 5) in flat vec3 vAlbedo;
//layout (location = 6) in flat vec3 vMRA; // Metallic, Roughness, AO

layout (location = 0) out vec4 outColor;

void main() {
    vec3 albedo = texture(sampler2D(uMaterialTextures[TEX_ALBEDO], uSamplers[SMP_LINEAR]), vUV).rgb;
    float metallic = texture(sampler2D(uMaterialTextures[TEX_METALLIC], uSamplers[SMP_LINEAR]), vUV).r;
    float roughness = texture(sampler2D(uMaterialTextures[TEX_ROUGHNESS], uSamplers[SMP_LINEAR]), vUV).r;
    float ao = texture(sampler2D(uMaterialTextures[TEX_AO], uSamplers[SMP_LINEAR]), vUV).r;
    vec3 normal;
    if (uEnableNormalMap != 0) {
        normal = vTBN * (texture(sampler2D(uMaterialTextures[TEX_NORMAL], uSamplers[SMP_LINEAR]), vUV).xyz * 2 - 1);
    } else {
        normal = normalize(vNormalWorld);
    }

    vec3 Lo = vec3(0);

    for (int i = 0; i < uNumLights; ++i) {
        Light light = uLights[i];
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
            
        vec3 V = normalize(uViewPos - vPosWorld);
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