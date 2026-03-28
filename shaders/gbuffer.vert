#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    int isUnlit;
    float dispScale;
    float time;
    int isCurtain;
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
<<<<<<< HEAD
layout(location = 3) out vec3 outTangent;
=======
layout(location = 3) out vec4 outColor;
layout(location = 4) out flat int outIsUnlit;
>>>>>>> parent of ad8e0d8 (SPONZA!!!)

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
<<<<<<< HEAD

    mat3 normalMat = transpose(inverse(mat3(pc.model)));
    outNormal = normalize(normalMat * inNormal);
    outTangent = normalize(normalMat * inTangent);

=======
    outNormal = normalize(transpose(inverse(mat3(pc.model))) * inNormal);
>>>>>>> parent of ad8e0d8 (SPONZA!!!)
    outTexCoord = inTexCoord;
}
