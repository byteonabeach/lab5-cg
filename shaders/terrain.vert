#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D heightMap;

layout(push_constant) uniform PushConstants {
    vec4 patchOffsetSize;
    vec4 uvOffsetScale;
    float heightScale;
    int lodLevel;
    float pad0;
    float pad1;
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec4 outColor;

void main() {
    vec2 localUV = inPos.xz;
    vec2 globalUV = pc.uvOffsetScale.xy + localUV * pc.uvOffsetScale.zw;
    outUV = globalUV;

    float h = textureLod(heightMap, globalUV, 0.0).r;
    float worldY = h * pc.heightScale;

    vec2 worldXZ = pc.patchOffsetSize.xy + localUV * pc.patchOffsetSize.z;
    vec3 worldPos = vec3(worldXZ.x, worldY, worldXZ.y);
    outWorldPos = worldPos;

    vec2 texelSize = 1.0 / vec2(textureSize(heightMap, 0));
    float hL = textureLod(heightMap, globalUV - vec2(texelSize.x, 0.0), 0.0).r * pc.heightScale;
    float hR = textureLod(heightMap, globalUV + vec2(texelSize.x, 0.0), 0.0).r * pc.heightScale;
    float hD = textureLod(heightMap, globalUV - vec2(0.0, texelSize.y), 0.0).r * pc.heightScale;
    float hU = textureLod(heightMap, globalUV + vec2(0.0, texelSize.y), 0.0).r * pc.heightScale;

    float worldTexelSize = (pc.patchOffsetSize.z / 32.0);
    vec3 normal = normalize(vec3(hL - hR, 2.0 * worldTexelSize, hD - hU));
    outNormal = normal;

    vec3 tangent = normalize(vec3(1.0, (hR - hL) / (2.0 * worldTexelSize), 0.0));
    outTangent = tangent;

    outColor = vec4(1.0);

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
