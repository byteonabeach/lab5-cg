#pragma once
#include <glm/glm.hpp>

// ─── Light types ────────────────────────────────────────────────────────────

enum class LightType : int {
    Directional = 0,
    Point       = 1,
    Spot        = 2
};

// GPU-side light descriptor (must match lighting.frag layout)
// Total: 4 × vec4 = 64 bytes — naturally aligned
struct LightData {
    glm::vec4 position;   // xyz = world position (Point / Spot)
    glm::vec4 direction;  // xyz = normalized direction (Directional / Spot)
    glm::vec4 color;      // xyz = RGB color, w = intensity
    glm::vec4 params;     // x = LightType, y = innerCutoffCos (Spot),
                          // z = outerCutoffCos (Spot), w = range (Point/Spot)
};

// CPU helper: factory functions
namespace Light {

    inline LightData makeDirectional(glm::vec3 dir,
                                     glm::vec3 color = {1,1,1},
                                     float intensity  = 1.0f)
    {
        LightData d{};
        d.direction = glm::vec4(glm::normalize(dir), 0.0f);
        d.color     = glm::vec4(color, intensity);
        d.params    = glm::vec4((float)LightType::Directional, 0, 0, 0);
        return d;
    }

    inline LightData makePoint(glm::vec3 pos,
                               glm::vec3 color = {1,1,1},
                               float intensity = 1.0f,
                               float range     = 10.0f)
    {
        LightData d{};
        d.position = glm::vec4(pos, 1.0f);
        d.color    = glm::vec4(color, intensity);
        d.params   = glm::vec4((float)LightType::Point, 0, 0, range);
        return d;
    }

    inline LightData makeSpot(glm::vec3 pos,
                              glm::vec3 dir,
                              float innerDeg  = 12.5f,
                              float outerDeg  = 17.5f,
                              glm::vec3 color = {1,1,1},
                              float intensity = 1.0f,
                              float range     = 20.0f)
    {
        LightData d{};
        d.position  = glm::vec4(pos, 1.0f);
        d.direction = glm::vec4(glm::normalize(dir), 0.0f);
        d.color     = glm::vec4(color, intensity);
        d.params    = glm::vec4((float)LightType::Spot,
                                 glm::cos(glm::radians(innerDeg)),
                                 glm::cos(glm::radians(outerDeg)),
                                 range);
        return d;
    }
}

// UBO sent to lighting pass shader
// Std140 layout: vec4 fields + ivec4 padding for count
static constexpr int MAX_LIGHTS = 64;

struct LightsUBO {
    glm::vec4  viewPos;
    glm::vec4  ambientColor;
    glm::ivec4 countPad;   // x = lightCount, yzw = padding
    LightData  lights[MAX_LIGHTS];
};
