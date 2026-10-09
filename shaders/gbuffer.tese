#version 450
layout(triangles, equal_spacing, ccw) in;

layout(location = 0) in vec3 inWorldPos[];
layout(location = 1) in vec3 inNormal[];
layout(location = 2) in vec2 inTexCoord[];
layout(location = 3) in vec3 inTangent[];
layout(location = 4) in vec4 inColor[];
layout(location = 5) in vec3 inPrevWorldPos[];

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec4 outColor;
layout(location = 5) out vec3 outPrevWorldPos;

layout(push_constant) uniform PushConstants {
    int isUnlit;
    float dispScale;
    float time;
    int isCurtain;
    int isTransparent;
    float prevTime;
} pc;

layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    mat4 viewProj;
    mat4 prevViewProj;
    vec4 params;
} ubo;
layout(set = 1, binding = 2) uniform sampler2D dispTex;

vec2 interpolate2D(vec2 v0, vec2 v1, vec2 v2) {
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}
vec3 interpolate3D(vec3 v0, vec3 v1, vec3 v2) {
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}

vec3 curtainOffset(vec3 p, vec3 n, float v, float t) {
    float stiff = (1.0 - v) * (1.0 - v);
    float waveX = sin(t * 1.5 + p.y * 3.0) * 0.12;
    float waveZ = cos(t * 1.2 + p.x * 2.0) * 0.08;
    float waveN = sin(t * 2.0 + p.x * 4.0 + p.z * 4.0) * 0.05;
    return (vec3(waveX, 0.0, waveZ) + n * waveN) * stiff;
}

void main() {
    outTexCoord = interpolate2D(inTexCoord[0], inTexCoord[1], inTexCoord[2]);
    outNormal = normalize(interpolate3D(inNormal[0], inNormal[1], inNormal[2]));
    outTangent = normalize(interpolate3D(inTangent[0], inTangent[1], inTangent[2]));
    vec3 worldPos = interpolate3D(inWorldPos[0], inWorldPos[1], inWorldPos[2]);
    vec3 prevWorldPos = interpolate3D(inPrevWorldPos[0], inPrevWorldPos[1], inPrevWorldPos[2]);

    if (pc.isCurtain != 0) {
        worldPos += curtainOffset(worldPos, outNormal, outTexCoord.y, pc.time);
        prevWorldPos += curtainOffset(prevWorldPos, outNormal, outTexCoord.y, pc.prevTime);
    } else if (pc.dispScale > 0.0) {
        float disp = textureLod(dispTex, outTexCoord, 0.0).r * pc.dispScale;
        worldPos += outNormal * disp;
        prevWorldPos += outNormal * disp;
    }

    outWorldPos = worldPos;
    outPrevWorldPos = prevWorldPos;
    outColor = inColor[0];

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
