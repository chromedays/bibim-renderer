#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 vModelPos;
layout (location = 0) out vec4 outColor;

vec2 sampleEquirectangularMap(vec3 normal) {
    const vec2 invAtan = vec2(0.1591, 0.3183);
    vec2 uv = vec2(atan(normal.z, normal.x), asin(normal.y));
    uv *= invAtan;
    uv += 0.5;
    uv.x = 1.0 - uv.x;
    uv.y = 1.0 - uv.y;

    return uv;
}

void main() {
    vec3 normal = normalize(vModelPos);

    // vec3 irradiance = vec3(0.0);  

    // vec3 up    = vec3(0.0, 1.0, 0.0);
    // vec3 right = cross(up, normal);
    // up         = cross(normal, right);

    // const float PI = 3.141592;

    // float sampleDelta = 0.025;
    // float nrSamples = 0.0; 
    // for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    // {
    //     for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
    //     {
    //         // spherical to cartesian (in tangent space)
    //         vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
    //         // tangent space to world
    //         vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal; 
            
    //         vec2 uv = sampleEquirectangularMap(sampleVec);

    //         irradiance += texture(sampler2D(uEnvMap, uSamplers[SMP_LINEAR]), uv).rgb * cos(theta) * sin(theta);
    //         nrSamples++;
    //     }
    // }
    // irradiance = PI * irradiance * (1.0 / float(nrSamples));

    vec2 uv = sampleEquirectangularMap(normal);
    outColor = vec4(texture(sampler2D(uEnvMap, uSamplers[SMP_LINEAR]), uv).rgb, 1);
    // outColor = vec4(irradiance, 1);
}