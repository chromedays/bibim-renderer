#version 450


layout (location = 0) in vec2 vUV;
layout (location = 1) in vec3 vPosWorld;
layout (location = 2) in vec3 vNormalWorld;

layout (location = 0) out vec3 outPosWorld;
layout (location = 1) out vec3 outNormalWorld;
layout (location = 2) out vec3 outAlbedo;
layout (location = 3) out vec4 outMRAH; // Metallic, Roughness, AO, Height
layout (location = 4) out vec3 outMaterialIndex;

void main() 
{
    outPosWorld = vPosWorld;
    outNormalWorld = vNormalWorld;
    outAlbedo = vec3(1,0,0);
    outMRAH = vec4(0, 1, 0, 1);
}