#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 lightColor;
    vec4 viewPos;
    vec4 ambientColor;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragPos      = worldPos.xyz;
    // Normal matrix: upper-left 3x3 of transpose(inverse(model))
    mat3 normalMat = transpose(inverse(mat3(pc.model)));
    fragNormal   = normalMat * inNormal;
    fragTexCoord = inTexCoord;
    gl_Position  = ubo.proj * ubo.view * worldPos;
}
