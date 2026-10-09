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
layout(location = 9) in vec4 inPrevModel0;
layout(location = 10) in vec4 inPrevModel1;
layout(location = 11) in vec4 inPrevModel2;
layout(location = 12) in vec4 inPrevModel3;

layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    mat4 viewProj;
    mat4 prevViewProj;
    vec4 params;
} ubo;

layout(push_constant) uniform PushConstants {
    int isUnlit;
    float dispScale;
    float time;
    int isCurtain;
    int isTransparent;
    float prevTime;
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec4 outColor;
layout(location = 5) out vec3 outPrevWorldPos;

void main() {
    mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
    mat4 prevModel = mat4(inPrevModel0, inPrevModel1, inPrevModel2, inPrevModel3);
    vec4 worldPos = model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
    outPrevWorldPos = (prevModel * vec4(inPosition, 1.0)).xyz;

    mat3 m = mat3(model);
    mat3 cofactor = mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    float handedness = dot(m[0], cofactor[0]) < 0.0 ? -1.0 : 1.0;
    mat3 normalMat = cofactor * handedness;
    outNormal = normalize(normalMat * inNormal);
    outTangent = normalize(normalMat * inTangent);
    outTexCoord = inTexCoord;
    outColor = inInstanceColor;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
