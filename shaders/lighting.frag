#version 450

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D gNormal;
layout(set = 0, binding = 1) uniform sampler2D gAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gDepth;
layout(set = 0, binding = 4) uniform sampler2DArray shadowMap;

struct LightData {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
    vec4 params2;
    mat4 lightSpace;
};

layout(set = 0, binding = 3) uniform LightsUBO {
    vec4 viewPos;
    vec4 ambientColor;
    ivec4 countPad;
    mat4 invViewProj;
    LightData lights[64];
} lightsUBO;

layout(location = 0) out vec4 outColor;

float calcAttenuation(float dist, float range) {
    float x = clamp(dist / range, 0.0, 1.0);
    return (1.0 - x) * (1.0 - x);
}

vec3 evaluateLight(LightData light, vec3 fragPos, vec3 N, vec3 albedo, vec3 viewDir) {
    int type = int(light.params.x);
    vec3 lightDir;
    float atten = 1.0;

    if (type == 0) {
        lightDir = normalize(-light.direction.xyz);
    } else if (type == 1) {
        vec3 toLight = light.position.xyz - fragPos;
        lightDir = normalize(toLight);
        atten = calcAttenuation(length(toLight), light.params.w);
    } else {
        vec3 toLight = light.position.xyz - fragPos;
        lightDir = normalize(toLight);
        atten = calcAttenuation(length(toLight), light.params.w);
        float cosAngle = dot(lightDir, normalize(-light.direction.xyz));
        float spotFactor = clamp((cosAngle - light.params.z) / max(light.params.y - light.params.z, 1e-5), 0.0, 1.0);
        atten *= spotFactor * spotFactor * (3.0 - 2.0 * spotFactor);
    }

    if (atten < 1e-5) return vec3(0.0);

    float shadow = 0.0;
    if (light.params2.x > 0.5) {
        vec4 fragPosLS = light.lightSpace * vec4(fragPos, 1.0);
        vec3 projCoords = fragPosLS.xyz / fragPosLS.w;
        projCoords.xy = projCoords.xy * 0.5 + 0.5;

        if (projCoords.z > 0.0 && projCoords.z < 1.0 &&
                projCoords.x > 0.0 && projCoords.x < 1.0 &&
                projCoords.y > 0.0 && projCoords.y < 1.0) {
            float currentDepth = projCoords.z;
            float bias = max(0.005 * (1.0 - dot(N, lightDir)), 0.001);
            vec2 texelSize = 1.0 / vec2(2048.0);
            float pcf = 0.0;

            for (int x = -1; x <= 1; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    float pcfDepth = texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, light.params2.y)).r;
                    pcf += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
                }
            }

            shadow = pcf / 9.0;
        }
    }

    atten *= (1.0 - shadow);

    float NdotL = max(dot(N, lightDir), 0.0);
    vec3 halfV = normalize(lightDir + viewDir);
    float NdotH = max(dot(N, halfV), 0.0);
    float spec = pow(NdotH, 64.0);

    vec3 lightRGB = light.color.rgb * light.color.w;
    vec3 diffuse = NdotL * albedo * lightRGB;
    vec3 specular = spec * 0.25 * lightRGB;

    return (diffuse + specular) * atten;
}

void main() {
    vec3 N = texture(gNormal, inUV).xyz;
    vec3 albedo = texture(gAlbedo, inUV).rgb;

    if (dot(N, N) < 0.5) {
        outColor = vec4(albedo, 1.0);
        return;
    }

    float depth = texture(gDepth, inUV).r;

    vec4 ndc = vec4(inUV.x * 2.0 - 1.0, inUV.y * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = lightsUBO.invViewProj * ndc;

    vec3 fragPos = worldPos.xyz / worldPos.w;

    N = normalize(N);
    vec3 viewDir = normalize(lightsUBO.viewPos.xyz - fragPos);
    vec3 result = lightsUBO.ambientColor.rgb * albedo;

    int cnt = lightsUBO.countPad.x;
    for (int i = 0; i < cnt; ++i) {
        result += evaluateLight(lightsUBO.lights[i], fragPos, N, albedo, viewDir);
    }

    result = result / (result + vec3(1.0));
    outColor = vec4(result, 1.0);
}
