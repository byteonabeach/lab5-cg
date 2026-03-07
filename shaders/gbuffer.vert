#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    int isUnlit;
} pc;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec4 outColor;
layout(location = 3) out flat int outIsUnlit;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    outNormal = normalize(transpose(inverse(mat3(pc.model))) * inNormal);
    outTexCoord = inTexCoord;
    outColor = pc.color;
    outIsUnlit = pc.isUnlit;
    gl_Position = ubo.proj * ubo.view * worldPos;
}
