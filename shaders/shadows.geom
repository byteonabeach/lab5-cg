#version 450

layout(triangles, invocations = 4) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec3 inWorldPos[];
layout(location = 1) in vec2 inUV[];

layout(set = 0, binding = 0) uniform ShadowUBO {
    mat4 cascadeMatrices[4];
} ubo;

layout(location = 0) out vec2 outUV;

void main() {
    gl_Layer = gl_InvocationID;

    vec4 p0 = ubo.cascadeMatrices[gl_InvocationID] * vec4(inWorldPos[0], 1.0);
    vec4 p1 = ubo.cascadeMatrices[gl_InvocationID] * vec4(inWorldPos[1], 1.0);
    vec4 p2 = ubo.cascadeMatrices[gl_InvocationID] * vec4(inWorldPos[2], 1.0);

    if ((p0.x < -p0.w && p1.x < -p1.w && p2.x < -p2.w) ||
        (p0.x >  p0.w && p1.x >  p1.w && p2.x >  p2.w) ||
        (p0.y < -p0.w && p1.y < -p1.w && p2.y < -p2.w) ||
        (p0.y >  p0.w && p1.y >  p1.w && p2.y >  p2.w) ||
        (p0.z >  p0.w && p1.z >  p1.w && p2.z >  p2.w) ||
        (p0.z <  0.0  && p1.z <  0.0  && p2.z <  0.0)) {
        return;
    }

    gl_Position = p0;
    outUV = inUV[0];
    EmitVertex();

    gl_Position = p1;
    outUV = inUV[1];
    EmitVertex();

    gl_Position = p2;
    outUV = inUV[2];
    EmitVertex();

    EndPrimitive();
}
