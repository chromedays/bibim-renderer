#version 450

#define MAX_NUM_LIGHTS 100
#define NUM_MATERIALS 1

struct Light
{
    vec3 pos;
    int type; // 0 = point light, 1 = spot light, 2 = directional light
    vec3 dir;
    float intensity;
    vec3 color;
};

layout (binding = 0) uniform UniformBlock {
    mat4 viewMat;
    mat4 projMat;
    vec3 viewPos;
    int numLights;
    Light lights[MAX_NUM_LIGHTS];
} ub;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in mat4 aModel;
layout (location = 8) in mat4 aInvModel;
layout (location = 12) in vec3 aAlbedo;
layout (location = 13) in float aMetallic;
layout (location = 14) in float aRoughness;
layout (location = 15) in float aAO;
layout (location = 16) in int aMaterialIndex;

layout (location = 0) out vec2 vUV;
layout (location = 1) out vec3 vPosWorld;
// (Tangent/Bitangent/Normal) matrix. Used to transform normal from model space to tangent space
layout (location = 2) out flat mat3 vTBN; 
layout (location = 5) out flat vec3 vAlbedo;
layout (location = 6) out flat vec3 vMRA; // Metallic, Roughness, AO
layout (location = 7) out flat int vMaterialIndex;

void main() {
    vec4 posWorld = aModel * vec4(aPosition, 1.0);
    vPosWorld = posWorld.xyz;
    gl_Position = ub.projMat * ub.viewMat * posWorld;
    vUV = aUV;

    // TODO(ilgwon): Pass normal matrix through instance data
    mat3 normalMat = transpose(mat3(aInvModel));
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(normalMat * aTangent);
    vec3 B = cross(N, T);
    vTBN = mat3(T, B, N);

    vAlbedo = aAlbedo;
    vMRA = vec3(aMetallic, aRoughness, aAO);
    vMaterialIndex = aMaterialIndex;
}