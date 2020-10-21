#version 450
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;

layout(location = 0) out vec2 vUV;
void main() {
  vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  gl_Position = vec4(vUV * 2.0f + -1.0f, 0.0f, 1.0f);
}