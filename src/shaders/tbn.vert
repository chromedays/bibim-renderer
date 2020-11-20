#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in mat4 aModel;
layout (location = 8) in mat4 aInvModel;

layout (location = 0) out vec3 vT;
layout (location = 1) out vec3 vB;
layout (location = 2) out vec3 vN;
layout (location = 3) out mat4 vCombined;

void main() {
    mat3 normalMat = transpose(mat3(aInvModel));
    
    gl_Position = aModel * vec4(aPosition, 1.0);
    vCombined = uProjMat * uViewMat;

    vN = normalize(normalMat * aNormal);
    vT = normalize(normalMat * aTangent);
    vB = cross(vN, vT);

    if (uEnableNormalMap != 0) 
    {
        mat3 TBN = mat3(vT, vB, vN);
        vec3 normal = TBN * (texture(sampler2D(uMaterialTextures[TEX_NORMAL], uSamplers[SMP_LINEAR]), aUV).xyz * 2 - 1);

        vec3 binormal = vec3(1,0,0);
        if(binormal == normal)
            binormal = vec3(0,0,1);

        vec3 tangent = cross(normal, binormal);
        binormal = cross(normal, tangent);

        vN = normal;
        vT = tangent;
        vB = binormal;
    }
}