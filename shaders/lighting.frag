#version 450

// Lighting pass fragment shader.
// Reads the GBuffer and evaluates Blinn-Phong lighting for every light
// (Directional, Point, Spot) in one fullscreen pass.

layout(location = 0) in vec2 inUV;

// GBuffer inputs (set 0, bindings 0-2)
layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;

// ─── Light data ────────────────────────────────────────────────────────────

struct LightData {
    vec4 position;    // xyz = world position      (Point / Spot)
    vec4 direction;   // xyz = normalized direction (Directional / Spot)
    vec4 color;       // xyz = RGB color, w = intensity
    vec4 params;      // x = type (0=Dir, 1=Point, 2=Spot)
                      // y = innerCutoffCos (Spot)
                      // z = outerCutoffCos (Spot)
                      // w = range (Point / Spot)
};

layout(set = 0, binding = 3) uniform LightsUBO {
    vec4       viewPos;
    vec4       ambientColor;
    ivec4      countPad;     // x = light count
    LightData  lights[64];
} lightsUBO;

layout(location = 0) out vec4 outColor;

// ─── Attenuation ────────────────────────────────────────────────────────────

float calcAttenuation(float dist, float range) {
    // Smooth quadratic falloff that reaches 0 at range
    float x = clamp(dist / range, 0.0, 1.0);
    return (1.0 - x) * (1.0 - x);
}

// ─── Per-light Blinn-Phong ──────────────────────────────────────────────────

vec3 evaluateLight(LightData light, vec3 fragPos, vec3 N, vec3 albedo, vec3 viewDir) {
    int  type      = int(light.params.x);
    vec3 lightDir;
    float atten    = 1.0;

    if (type == 0) {
        // ── Directional ──────────────────────────────────────────────────
        lightDir = normalize(-light.direction.xyz);
        atten    = 1.0;

    } else if (type == 1) {
        // ── Point ────────────────────────────────────────────────────────
        vec3  toLight = light.position.xyz - fragPos;
        float dist    = length(toLight);
        lightDir = normalize(toLight);
        atten    = calcAttenuation(dist, light.params.w);

    } else {
        // ── Spot ─────────────────────────────────────────────────────────
        vec3  toLight   = light.position.xyz - fragPos;
        float dist      = length(toLight);
        lightDir        = normalize(toLight);
        atten           = calcAttenuation(dist, light.params.w);

        // Spot cone attenuation: smooth edge between inner and outer cutoff
        float cosAngle  = dot(lightDir, normalize(-light.direction.xyz));
        float innerCos  = light.params.y;
        float outerCos  = light.params.z;
        float spotFactor= clamp((cosAngle - outerCos) / max(innerCos - outerCos, 1e-5), 0.0, 1.0);
        spotFactor      = spotFactor * spotFactor * (3.0 - 2.0 * spotFactor); // smoothstep
        atten          *= spotFactor;
    }

    if (atten < 1e-5) return vec3(0.0);

    // Diffuse (Lambertian)
    float NdotL  = max(dot(N, lightDir), 0.0);

    // Specular (Blinn-Phong half-vector)
    vec3  halfV  = normalize(lightDir + viewDir);
    float NdotH  = max(dot(N, halfV), 0.0);
    float spec   = pow(NdotH, 64.0);

    vec3 lightRGB   = light.color.rgb * light.color.w; // RGB × intensity
    vec3 diffuse    = NdotL * albedo * lightRGB;
    vec3 specular   = spec  * 0.25   * lightRGB;

    return (diffuse + specular) * atten;
}

// ─── Main ───────────────────────────────────────────────────────────────────

void main() {
    // Read GBuffer
    vec3 fragPos = texture(gPosition, inUV).xyz;
    vec3 N       = normalize(texture(gNormal, inUV).xyz);
    vec3 albedo  = texture(gAlbedo,   inUV).rgb;

    // Skip sky / unrendered pixels (position == 0 means geometry pass wrote nothing)
    if (dot(N, N) < 0.5) {
        outColor = vec4(lightsUBO.ambientColor.rgb * 0.5, 1.0);
        return;
    }

    vec3 viewDir = normalize(lightsUBO.viewPos.xyz - fragPos);

    // Ambient contribution
    vec3 result = lightsUBO.ambientColor.rgb * albedo;

    // Accumulate all lights
    int cnt = lightsUBO.countPad.x;
    for (int i = 0; i < cnt; ++i) {
        result += evaluateLight(lightsUBO.lights[i], fragPos, N, albedo, viewDir);
    }

    // Tone-mapping: Reinhard (prevents blown-out highlights)
    result = result / (result + vec3(1.0));

    outColor = vec4(result, 1.0);
}
