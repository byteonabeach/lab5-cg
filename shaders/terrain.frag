#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec4 inColor;

layout(set = 1, binding = 0) uniform sampler2D heightMap;

layout(location = 0) out vec4 gNormal;
layout(location = 1) out vec4 gAlbedo;

void main() {
    vec3 N = normalize(inNormal);

    float slope = clamp(dot(N, vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
    float height = inWorldPos.y;

    vec3 valleyGrass = vec3(0.14, 0.25, 0.09);
    vec3 hillGrass = vec3(0.20, 0.32, 0.12);
    vec3 rockColor = vec3(0.25, 0.23, 0.21);
    vec3 snowColor = vec3(0.85, 0.88, 0.92);

    vec3 diffuse = mix(valleyGrass, hillGrass, clamp(height / 15.0, 0.0, 1.0));

    if (height > 22.0) {
        float t = clamp((height - 22.0) / 12.0, 0.0, 1.0);
        diffuse = mix(diffuse, snowColor, t);
    }

    if (slope < 0.75) {
        float rockFactor = clamp((0.75 - slope) / 0.2, 0.0, 1.0);
        diffuse = mix(diffuse, rockColor, rockFactor);
    }

    gNormal = vec4(N, 0.0);
    gAlbedo = vec4(diffuse, 1.0);
}
