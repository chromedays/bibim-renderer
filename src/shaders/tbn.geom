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

    vec4 tri_centeroid = (gl_in[0].gl_Position 
    + gl_in[1].gl_Position 
    + gl_in[2].gl_Position) / 3;

    vec3 face_tangent = (vT[0] + vT[1] + vT[2]) / 3;
    face_tangent = normalize(face_tangent) * LENGTH;

    vec3 face_binormal = (vB[0] + vB[1] + vB[2]) / 3;
    face_binormal = normalize(face_binormal) * LENGTH;

    vec3 face_normal = (vN[0] + vN[1] + vN[2]) / 3;
    face_normal = normalize(face_normal) * LENGTH;

    vec4 tangent = vCombined[0] * (tri_centeroid + vec4(face_tangent, 0));
    vec4 binormal = vCombined[0] * (tri_centeroid + vec4(face_binormal, 0));
    vec4 normal = vCombined[0] * (tri_centeroid + vec4(face_normal, 0));
    
    tri_centeroid = vCombined[0] * tri_centeroid;



    gColor = vec3(1,0,0); // T
    gl_Position = tri_centeroid;
    EmitVertex();
    gColor = vec3(1,0,0); // T
    gl_Position = tangent;
    EmitVertex();
    gColor = vec3(1,0,0); // T
    gl_Position = tri_centeroid;
    EmitVertex();



    gColor = vec3(0,1,0); // B
    gl_Position = tri_centeroid;
    EmitVertex();
    gColor = vec3(0,1,0); // B
    gl_Position = binormal;
    EmitVertex();
    gColor = vec3(0,1,0); // B
    gl_Position = tri_centeroid;
    EmitVertex();



    gColor = vec3(0,0,1); // N
    gl_Position = tri_centeroid;
    EmitVertex();
    gColor = vec3(0,0,1); // N
    gl_Position = normal;
    EmitVertex();
    gColor = vec3(0,0,1); // N
    gl_Position = tri_centeroid;
    EmitVertex();
    
    
    EndPrimitive();
}  