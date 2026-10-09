#version 450

struct McVertex {
    vec4 pos;
    vec4 normal;
    vec4 prevPos;
};

layout(set = 2, binding = 0) readonly buffer VertexBuf {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
    McVertex vertices[];
};

layout(push_constant) uniform Push {
    uint maxVertices;
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outUV;

void main() {
    if (uint(gl_VertexIndex) >= pc.maxVertices) {
        outWorldPos = vec3(0.0);
        outUV = vec2(0.0);
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    outWorldPos = vertices[gl_VertexIndex].pos.xyz;
    outUV = vec2(0.0);
    gl_Position = vec4(outWorldPos, 1.0);
}
