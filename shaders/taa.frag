#version 450

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D historyColor;
layout(set = 0, binding = 2) uniform sampler2D velocityTex;
layout(set = 0, binding = 3) uniform sampler2D depthTex;

layout(push_constant) uniform PushConstants {
    float feedbackMin;
    float feedbackMax;
    int enabled;
    int reset;
    int showMotion;
} pc;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outHistory;

vec3 toYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
                0.5 * c.r - 0.5 * c.b,
                -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 fromYCoCg(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

vec3 sampleHistory(vec2 uv, vec2 texSize) {
    vec2 samplePos = uv * texSize;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
    vec2 f = samplePos - texPos1;

    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;

    vec2 texPos0 = (texPos1 - 1.0) / texSize;
    vec2 texPos3 = (texPos1 + 2.0) / texSize;
    vec2 texPos12 = (texPos1 + offset12) / texSize;

    float wTop = w12.x * w0.y;
    float wLeft = w0.x * w12.y;
    float wCenter = w12.x * w12.y;
    float wRight = w3.x * w12.y;
    float wBottom = w12.x * w3.y;

    vec3 result = textureLod(historyColor, vec2(texPos12.x, texPos0.y), 0.0).rgb * wTop
                + textureLod(historyColor, vec2(texPos0.x, texPos12.y), 0.0).rgb * wLeft
                + textureLod(historyColor, texPos12, 0.0).rgb * wCenter
                + textureLod(historyColor, vec2(texPos3.x, texPos12.y), 0.0).rgb * wRight
                + textureLod(historyColor, vec2(texPos12.x, texPos3.y), 0.0).rgb * wBottom;

    return max(result / (wTop + wLeft + wCenter + wRight + wBottom), vec3(0.0));
}

vec3 directionColor(vec2 v) {
    float h = atan(v.y, v.x) / 6.2831853 + 0.5;
    return clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
}

float segmentDistance(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

vec3 drawMotionVectors(vec3 color, ivec2 px, ivec2 res) {
    vec2 velocity = texelFetch(velocityTex, px, 0).rg * vec2(res);
    float speed = length(velocity);
    if (speed > 1e-3) color = mix(color, directionColor(velocity), clamp(speed * 0.25, 0.2, 0.6));

    const float cell = 32.0;
    vec2 p = vec2(px) + 0.5;
    vec2 center = (floor(p / cell) + 0.5) * cell;
    vec2 v = texelFetch(velocityTex, clamp(ivec2(center), ivec2(0), res - 1), 0).rg * vec2(res);
    float len = length(v);
    if (len < 1e-3) return color;

    vec2 dir = v / len;
    vec2 side = vec2(-dir.y, dir.x);
    float halfLen = clamp(len * 6.0, 4.0, cell * 0.42);
    vec2 tip = center + dir * halfLen;
    float d = segmentDistance(p, center - dir * halfLen, tip);
    d = min(d, segmentDistance(p, tip, tip - dir * 5.0 + side * 3.5));
    d = min(d, segmentDistance(p, tip, tip - dir * 5.0 - side * 3.5));

    color = mix(color, vec3(0.0), 1.0 - smoothstep(1.2, 2.2, d));
    return mix(color, vec3(1.0), 1.0 - smoothstep(0.4, 1.1, d));
}

void main() {
    ivec2 res = textureSize(sceneColor, 0);
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec3 current = texelFetch(sceneColor, px, 0).rgb;

    if (pc.enabled == 0 || pc.reset != 0) {
        outColor = vec4(pc.showMotion != 0 ? drawMotionVectors(current, px, res) : current, 1.0);
        outHistory = vec4(current, 1.0);
        return;
    }

    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    vec3 cMin = vec3(1e9);
    vec3 cMax = vec3(-1e9);
    float closestDepth = 2.0;
    ivec2 closestPx = px;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 q = clamp(px + ivec2(x, y), ivec2(0), res - 1);
            vec3 c = toYCoCg(texelFetch(sceneColor, q, 0).rgb);
            m1 += c;
            m2 += c * c;
            cMin = min(cMin, c);
            cMax = max(cMax, c);
            float d = texelFetch(depthTex, q, 0).r;
            if (d < closestDepth) {
                closestDepth = d;
                closestPx = q;
            }
        }
    }

    m1 /= 9.0;
    m2 /= 9.0;
    vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));
    vec3 boxMin = max(m1 - 1.25 * sigma, cMin);
    vec3 boxMax = min(m1 + 1.25 * sigma, cMax);

    vec2 velocity = texelFetch(velocityTex, closestPx, 0).rg;
    vec2 uv = (vec2(px) + 0.5) / vec2(res);
    vec2 prevUV = uv - velocity;

    vec3 history = toYCoCg(sampleHistory(prevUV, vec2(res)));

    vec3 center = 0.5 * (boxMin + boxMax);
    vec3 extent = 0.5 * (boxMax - boxMin) + 1e-5;
    vec3 offset = history - center;
    vec3 scaled = abs(offset / extent);
    float maxScaled = max(scaled.x, max(scaled.y, scaled.z));
    if (maxScaled > 1.0) history = center + offset / maxScaled;

    float speed = length(velocity * vec2(res));
    float feedback = mix(pc.feedbackMax, pc.feedbackMin, clamp(speed * 0.5, 0.0, 1.0));
    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) feedback = 0.0;

    vec3 resolved = max(fromYCoCg(mix(toYCoCg(current), history, feedback)), vec3(0.0));
    outColor = vec4(pc.showMotion != 0 ? drawMotionVectors(resolved, px, res) : resolved, 1.0);
    outHistory = vec4(resolved, 1.0);
}
