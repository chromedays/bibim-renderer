#version 450

#define LENGTH 0.05f

layout (triangles) in;
layout (line_strip, max_vertices = 9) out;


layout (location = 0) in vec3 vT[];
layout (location = 1) in vec3 vB[];
layout (location = 2) in vec3 vN[];
layout (location = 3) in mat4 vCombined[];

layout (location = 0) flat out vec3 gColor;

void main() {

    vec4 triCenteroid = (gl_in[0].gl_Position 
    + gl_in[1].gl_Position 
    + gl_in[2].gl_Position) / 3;

    vec3 faceTangent = (vT[0] + vT[1] + vT[2]) / 3;
    faceTangent = normalize(faceTangent) * LENGTH;

    vec3 faceBinormal = (vB[0] + vB[1] + vB[2]) / 3;
    faceBinormal = normalize(faceBinormal) * LENGTH;

    vec3 faceNormal = (vN[0] + vN[1] + vN[2]) / 3;
    faceNormal = normalize(faceNormal) * LENGTH;

    vec4 tangent = vCombined[0] * (triCenteroid + vec4(faceTangent, 0));
    vec4 binormal = vCombined[0] * (triCenteroid + vec4(faceBinormal, 0));
    vec4 normal = vCombined[0] * (triCenteroid + vec4(faceNormal, 0));
    
    triCenteroid = vCombined[0] * triCenteroid;



    gColor = vec3(1,0,0); // T
    gl_Position = triCenteroid;
    EmitVertex();
    gColor = vec3(1,0,0); // T
    gl_Position = tangent;
    EmitVertex();
    gColor = vec3(1,0,0); // T
    gl_Position = triCenteroid;
    EmitVertex();



    gColor = vec3(0,1,0); // B
    gl_Position = triCenteroid;
    EmitVertex();
    gColor = vec3(0,1,0); // B
    gl_Position = binormal;
    EmitVertex();
    gColor = vec3(0,1,0); // B
    gl_Position = triCenteroid;
    EmitVertex();



    gColor = vec3(0,0,1); // N
    gl_Position = triCenteroid;
    EmitVertex();
    gColor = vec3(0,0,1); // N
    gl_Position = normal;
    EmitVertex();
    gColor = vec3(0,0,1); // N
    gl_Position = triCenteroid;
    EmitVertex();
    
    
    EndPrimitive();
}  