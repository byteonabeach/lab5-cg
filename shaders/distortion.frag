#version 450

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D velocityTex;

layout(push_constant) uniform PushConstants {
    float time;
    float prevTime;
    float strength;
    float prevStrength;
    int writeVelocity;
} pc;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

vec2 waveOffset(vec2 uv, float t, float strength, vec2 res) {
    vec2 p = uv * vec2(res.x / res.y, 1.0);
    vec2 w = vec2(sin(p.y * 9.0 + t * 2.5) + 0.5 * sin((p.x + p.y) * 17.0 - t * 3.7),
                  cos(p.x * 8.0 - t * 2.2) + 0.5 * cos((p.x - p.y) * 15.0 + t * 3.1));
    return w * strength / res;
}

void main() {
    vec2 res = vec2(textureSize(sceneColor, 0));
    vec2 offset = waveOffset(inUV, pc.time, pc.strength, res);
    vec2 uv = inUV + offset;

    vec2 velocity = textureLod(velocityTex, uv, 0.0).rg;
    if (pc.writeVelocity != 0) velocity += waveOffset(inUV, pc.prevTime, pc.prevStrength, res) - offset;

    outColor = vec4(textureLod(sceneColor, uv, 0.0).rgb, 1.0);
    outVelocity = velocity;
}
