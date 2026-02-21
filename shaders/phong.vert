#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 lightColor;
    vec4 viewPos;
    vec2 uvOffset;
    vec2 uvScale;
    float time;
    float animMode;
    float _pad0;
    float _pad1;
} ubo;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

vec3 applyWave(vec3 p, vec3 n) {
    float wave = sin(p.x * 4.0 + ubo.time * 2.5)
            * cos(p.z * 4.0 + ubo.time * 1.8) * 0.12;
    return vec3(p.x, p.y + wave, p.z);
}

vec3 applyPulse(vec3 p, vec3 n) {
    float amp = 0.08 * sin(ubo.time * 3.0);
    return p + n * amp;
}

vec3 applyTwist(vec3 p, vec3 n) {
    float angle = p.y * 1.8 * sin(ubo.time * 0.9);
    float c = cos(angle), s = sin(angle);
    return vec3(p.x * c - p.z * s,
        p.y,
        p.x * s + p.z * c);
}

void main() {
    int mode = int(ubo.animMode + 0.5);

    vec3 pos = inPos;
    vec3 normal = inNormal;

    if (mode == 1) pos = applyWave(pos, normal);
    else if (mode == 2) pos = applyPulse(pos, normal);
    else if (mode == 3) pos = applyTwist(pos, normal);

    vec4 worldPos = ubo.model * vec4(pos, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 normalMat = transpose(inverse(mat3(ubo.model)));
    fragNormal = normalize(normalMat * normal);

    fragUV = inUV * ubo.uvScale + ubo.uvOffset;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
