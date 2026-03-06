#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 lightColor;
    vec4 viewPos;
    vec4 ambientColor;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D diffuseTex;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texSample = texture(diffuseTex, fragTexCoord);
    if (texSample.a < 0.1)
        discard;

    vec3 texColor = texSample.rgb;

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb * texColor;

    // Diffuse
    vec3 norm     = normalize(fragNormal);
    vec3 lightDir = normalize(ubo.lightPos.xyz - fragPos);
    float diff    = max(dot(norm, lightDir), 0.0);
    vec3 diffuse  = diff * ubo.lightColor.rgb * texColor;

    // Specular (Blinn-Phong half-vector)
    vec3 viewDir  = normalize(ubo.viewPos.xyz - fragPos);
    vec3 halfDir  = normalize(lightDir + viewDir);
    float spec    = pow(max(dot(norm, halfDir), 0.0), 64.0);
    vec3 specular = spec * ubo.lightColor.rgb * 0.3;

    vec3 result = ambient + diffuse + specular;
    outColor = vec4(result, 1.0);
}
