#version 450

layout(triangles, invocations = 4) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec3 inWorldPos[];

layout(set = 0, binding = 0) uniform ShadowUBO {
    mat4 cascadeMatrices[4];
} ubo;

void main() {
    gl_Layer = gl_InvocationID;
    for (int j = 0; j < 3; ++j) {
        gl_Position = ubo.cascadeMatrices[gl_InvocationID] * vec4(inWorldPos[j], 1.0);
        EmitVertex();
    }
    EndPrimitive();
}
