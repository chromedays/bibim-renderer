#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 vPosWorld;
layout (location = 1) in vec2 vUV;
layout (location = 2) in mat3 vTBN;

layout (location = 0) out vec3 outPosWorld;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outAlbedo;
layout (location = 3) out vec4 outMRAH; // Metallic, Roughness, AO, Height
layout (location = 4) out vec3 outMaterialIndex;


void main() 
{
    float metallic = texture(sampler2D(uMaterialTextures[TEX_METALLIC], uSamplers[SMP_LINEAR]), vUV).r;
    float roughness = texture(sampler2D(uMaterialTextures[TEX_ROUGHNESS], uSamplers[SMP_LINEAR]), vUV).r;
    float ao = texture(sampler2D(uMaterialTextures[TEX_AO], uSamplers[SMP_LINEAR]), vUV).r;
    float height = texture(sampler2D(uMaterialTextures[TEX_HEIGHT], uSamplers[SMP_LINEAR]), vUV).r;

    outPosWorld = vPosWorld;
    outNormal = vTBN * (texture(sampler2D(uMaterialTextures[TEX_NORMAL], uSamplers[SMP_NEAREST]), vUV).xyz * 2 - 1);
    outAlbedo = texture(sampler2D(uMaterialTextures[TEX_ALBEDO], uSamplers[SMP_LINEAR]), vUV).rgb;
    outMRAH = vec4(metallic, roughness, ao, height);
    outMaterialIndex = vec3(1,0,0); // Not in use?
}