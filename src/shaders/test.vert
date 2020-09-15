#version 450


layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec3 aNormal;
layout (location = 4) in mat4 aModel;
layout (location = 8) in mat4 aView;
layout (location = 12) in mat4 aProj;

layout (location = 0) out vec3 vColor;
layout (location = 1) out vec2 vUV;
layout (location = 2) out vec3 vPosWorld;
layout (location = 3) out vec3 vNormalWorld;


void main() {
    vec4 posWorld = aModel * vec4(aPosition, 1.0);
    vPosWorld = posWorld.xyz;
    gl_Position = aProj * aView * posWorld;
    vColor = aColor;
    vUV = aUV;
    vNormalWorld = normalize((mat3(aModel) * aNormal)); // Assuming an uniform transformation
}