#version 450

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D gNormal;
layout(set = 0, binding = 1) uniform sampler2D gAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gDepth;

struct LightData {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
    vec4 params2;
    mat4 lightSpace;
    mat4 cascadeMatrices[4];
    vec4 cascadeSplits;
};

layout(set = 0, binding = 3) uniform LightsUBO {
    vec4 viewPos;
    vec4 ambientColor;
    ivec4 countPad;
    mat4 invViewProj;
    mat4 view;
    LightData lights[64];
} ubo;

layout(set = 0, binding = 4) uniform sampler2DArray shadowMap;

layout(location = 0) out vec4 outColor;

vec3 toneMapACES(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    float depth = texture(gDepth, inUV).r;

    if (depth >= 1.0) {
        outColor = vec4(0.53, 0.75, 0.92, 1.0);
        return;
    }

    vec4 albedo = texture(gAlbedo, inUV);
    vec3 normal = texture(gNormal, inUV).xyz;

    if (length(normal) < 0.001) {
        outColor = vec4(albedo.rgb, 1.0);
        return;
    }

    vec4 clip = vec4(inUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPosW = ubo.invViewProj * clip;
    vec3 worldPos = worldPosW.xyz / worldPosW.w;

    vec3 result = albedo.rgb * ubo.ambientColor.rgb;

    vec3 V = normalize(ubo.viewPos.xyz - worldPos);
    float viewDepth = abs((ubo.view * vec4(worldPos, 1.0)).z);
    vec2 shadowTexel = 1.0 / vec2(textureSize(shadowMap, 0).xy);

    for (int i = 0; i < ubo.countPad.x; ++i) {
        int type = int(ubo.lights[i].params.x);
        vec4 lightColor = ubo.lights[i].color;

        vec3 L;
        float atten = 1.0;

        if (type == 0) {
            L = normalize(-ubo.lights[i].direction.xyz);
        } else if (type == 1) {
            vec3 toLight = ubo.lights[i].position.xyz - worldPos;
            float dist = length(toLight);
            float range = max(ubo.lights[i].params.w, 0.001);
            L = toLight / dist;
            atten = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
            if (atten <= 0.0) continue;
        } else {
            L = normalize(ubo.lights[i].position.xyz - worldPos);
        }

        float NdotL = dot(normal, L);
        float diff = max(NdotL, 0.0);

        float shadow = 1.0;
        if (ubo.lights[i].params2.x > 0.5) {
            vec4 splits = ubo.lights[i].cascadeSplits;

            int cascadeIndex = -1;
            if (viewDepth < splits.x) cascadeIndex = 0;
            else if (viewDepth < splits.y) cascadeIndex = 1;
            else if (viewDepth < splits.z) cascadeIndex = 2;
            else if (viewDepth < splits.w) cascadeIndex = 3;

            if (cascadeIndex >= 0) {
                vec4 lightSpacePos = ubo.lights[i].cascadeMatrices[cascadeIndex] * vec4(worldPos, 1.0);
                vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
                projCoords.xy = projCoords.xy * 0.5 + 0.5;

                if (projCoords.z > 0.0 && projCoords.z < 1.0 &&
                    projCoords.x > 0.0 && projCoords.x < 1.0 &&
                    projCoords.y > 0.0 && projCoords.y < 1.0) {
                    float bias = max(0.0015 * (1.0 - NdotL), 0.0002);
                    bias *= (1.0 + float(cascadeIndex) * 0.5);

                    shadow = 0.0;
                    for (int x = -1; x <= 1; ++x) {
                        for (int y = -1; y <= 1; ++y) {
                            float pcfDepth = texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * shadowTexel, float(cascadeIndex))).r;
                            shadow += (projCoords.z - bias > pcfDepth) ? 0.0 : 1.0;
                        }
                    }
                    shadow /= 9.0;
                }
            }
        }

        vec3 H = normalize(L + V);
        float spec = pow(max(dot(normal, H), 0.0), 32.0);
        vec3 specular = spec * lightColor.rgb * 0.25;

        vec3 diffuse = diff * albedo.rgb * lightColor.rgb;
        result += (diffuse + specular) * atten * shadow * lightColor.a;
    }

    outColor = vec4(toneMapACES(result), 1.0);
}
