#version 450

// Geometry pass vertex shader
// Outputs world-space data that the fragment shader writes into the GBuffer.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// set 0: per-frame view/proj UBO
layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
} ubo;

// push constant: per-object model matrix
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;

void main() {
    vec4 worldPos    = pc.model * vec4(inPosition, 1.0);
    outWorldPos      = worldPos.xyz;
    // Normal matrix: transpose(inverse(model)) — correct under non-uniform scale
    mat3 normalMat   = transpose(inverse(mat3(pc.model)));
    outNormal        = normalize(normalMat * inNormal);
    outTexCoord      = inTexCoord;
    gl_Position      = ubo.proj * ubo.view * worldPos;
}
