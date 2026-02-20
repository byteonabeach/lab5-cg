#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

// ─── UBO (тот же binding, что в вертексном шейдере) ──────────────────────
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 lightColor;    // w = specular power
    vec4 viewPos;
    vec2 uvOffset;
    vec2 uvScale;
} ubo;

// ─── Текстура (аналог SRV в D3D12) ───────────────────────────────────────
layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 texColor   = texture(texSampler, fragUV).rgb;
    vec3 lightColor = ubo.lightColor.rgb;
    float specPow   = max(ubo.lightColor.w, 1.0);

    vec3 N = normalize(fragNormal);
    vec3 L = normalize(ubo.lightPos.xyz - fragWorldPos);
    vec3 V = normalize(ubo.viewPos.xyz  - fragWorldPos);
    vec3 R = reflect(-L, N);

    // Phong: ambient + diffuse + specular
    vec3 ambient  = 0.15 * lightColor * texColor;
    vec3 diffuse  = max(dot(N, L), 0.0) * lightColor * texColor;
    float spec    = pow(max(dot(V, R), 0.0), specPow);
    vec3 specular = 0.5 * spec * lightColor;

    outColor = vec4(ambient + diffuse + specular, 1.0);
}
