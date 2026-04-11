#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 4) in vec4 inModel0;
layout(location = 5) in vec4 inModel1;
layout(location = 6) in vec4 inModel2;
layout(location = 7) in vec4 inModel3;
layout(location = 8) in vec4 inInstanceColor;

layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} ubo;

layout(push_constant) uniform PushConstants {
    int isUnlit;
    float dispScale;
    float time;
    int isCurtain;
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec4 outColor;

void main() {
    mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
    vec4 worldPos = model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
    mat3 normalMat = transpose(inverse(mat3(model)));
    outNormal = normalize(normalMat * inNormal);
    outTangent = normalize(normalMat * inTangent);
    outTexCoord = inTexCoord;
    outColor = inInstanceColor;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
