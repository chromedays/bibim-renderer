#version 450

#include "standard_sets.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
  int index = uNumLights;
  vec3 renderedBuffer =
      texture(sampler2D(uGbuffer[index], uSamplers[SMP_NEAREST]), vUV).rgb;

  outColor = vec4(renderedBuffer, 1);
}