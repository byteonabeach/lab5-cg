#version 450

struct McVertex {
    vec4 pos;
    vec4 normal;
    vec4 prevPos;
};

layout(set = 1, binding = 0) readonly buffer VertexBuf {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
    McVertex vertices[];
};

layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    mat4 viewProj;
    mat4 prevViewProj;
    vec4 params;
} ubo;

layout(push_constant) uniform Push {
    uint maxVertices;
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outPrevWorldPos;

void main() {
    if (uint(gl_VertexIndex) >= pc.maxVertices) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        outWorldPos = vec3(0.0);
        outNormal = vec3(0.0, 1.0, 0.0);
        outPrevWorldPos = vec3(0.0);
        return;
    }
    McVertex v = vertices[gl_VertexIndex];
    outWorldPos = v.pos.xyz;
    outNormal = v.normal.xyz;
    outPrevWorldPos = v.prevPos.xyz;
    gl_Position = ubo.proj * ubo.view * vec4(v.pos.xyz, 1.0);
}
