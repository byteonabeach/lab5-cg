#version 450

// ─── Атрибуты вершин ──────────────────────────────────────────────────────
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

// ─── Uniform buffer (аналог ConstantBuffer в D3D12) ──────────────────────
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightPos;      // w не используется
    vec4 lightColor;    // w = specular power
    vec4 viewPos;       // w не используется
    vec2 uvOffset;      // текстурная анимация: смещение
    vec2 uvScale;       // текстурная анимация: тайлинг
} ubo;

// ─── Передача во фрагментный шейдер ─────────────────────────────────────
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

void main() {
    vec4 worldPos   = ubo.model * vec4(inPos, 1.0);
    fragWorldPos    = worldPos.xyz;

    // Нормаль в world space (учитываем non-uniform scale)
    mat3 normalMat  = transpose(inverse(mat3(ubo.model)));
    fragNormal      = normalize(normalMat * inNormal);

    // UV с тайлингом и анимацией (домашнее задание)
    fragUV = inUV * ubo.uvScale + ubo.uvOffset;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
