#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPrevWorldPos;

layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    mat4 viewProj;
    mat4 prevViewProj;
    vec4 params;
} ubo;

layout(location = 0) out vec4 gNormal;
layout(location = 1) out vec4 gAlbedo;
layout(location = 2) out vec2 gVelocity;

void main() {
    vec3 N = normalize(inNormal);
    vec3 albedo = mix(vec3(0.85, 0.30, 0.12), vec3(0.15, 0.55, 0.90), 0.5 + 0.5 * N.y);

    vec4 cur = ubo.viewProj * vec4(inWorldPos, 1.0);
    vec4 prev = ubo.prevViewProj * vec4(inPrevWorldPos, 1.0);

    gNormal = vec4(N, 0.0);
    gAlbedo = vec4(albedo, 1.0);
    gVelocity = prev.w < 1e-4 ? vec2(0.0) : (cur.xy / cur.w - prev.xy / prev.w) * 0.5;
}
