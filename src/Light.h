#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class LightType : int { Directional = 0, Point = 1, Spot = 2 };

struct LightData {
    glm::vec4 position;
    glm::vec4 direction;
    glm::vec4 color;
    glm::vec4 params;
    glm::vec4 params2;
    glm::mat4 lightSpace;
};

namespace Light {
    inline LightData makeDirectional(glm::vec3 dir, glm::vec3 color = {1,1,1}, float intensity = 1.0f, bool castShadow = false, int shadowLayer = 0) {
        LightData d{};
        d.direction = glm::vec4(glm::normalize(dir), 0.0f); d.color = glm::vec4(color, intensity);
        d.params = glm::vec4((float)LightType::Directional, 0, 0, 0); d.params2 = glm::vec4(castShadow ? 1.0f : 0.0f, (float)shadowLayer, 0, 0);
        glm::mat4 proj = glm::ortho(-25.0f, 25.0f, -25.0f, 25.0f, -50.0f, 50.0f); proj[1][1] *= -1;
        glm::vec3 pos = glm::vec3(0.0f); glm::vec3 nDir = glm::normalize(dir);
        glm::vec3 up = (std::abs(nDir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        glm::mat4 view = glm::lookAt(pos - nDir * 20.0f, pos, up); d.lightSpace = proj * view;
        return d;
    }
    inline LightData makePoint(glm::vec3 pos, glm::vec3 color = {1,1,1}, float intensity = 1.0f, float range = 10.0f) {
        LightData d{};
        d.position = glm::vec4(pos, 1.0f); d.color = glm::vec4(color, intensity);
        d.params = glm::vec4((float)LightType::Point, 0, 0, range); d.params2 = glm::vec4(0); d.lightSpace = glm::mat4(1.0f);
        return d;
    }
    inline LightData makeSpot(glm::vec3 pos, glm::vec3 dir, float innerDeg = 12.5f, float outerDeg = 17.5f, glm::vec3 color = {1,1,1}, float intensity = 1.0f, float range = 20.0f, bool castShadow = false, int shadowLayer = 0) {
        LightData d{};
        d.position = glm::vec4(pos, 1.0f); d.direction = glm::vec4(glm::normalize(dir), 0.0f); d.color = glm::vec4(color, intensity);
        d.params = glm::vec4((float)LightType::Spot, glm::cos(glm::radians(innerDeg)), glm::cos(glm::radians(outerDeg)), range);
        d.params2 = glm::vec4(castShadow ? 1.0f : 0.0f, (float)shadowLayer, 0, 0);
        glm::mat4 proj = glm::perspective(glm::radians(outerDeg * 2.0f), 1.0f, 1.0f, range); proj[1][1] *= -1;
        glm::vec3 nDir = glm::normalize(dir); glm::vec3 up = (std::abs(nDir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        glm::mat4 view = glm::lookAt(pos, pos + nDir, up); d.lightSpace = proj * view;
        return d;
    }
}

static constexpr int MAX_LIGHTS = 64;

struct LightsUBO {
    glm::vec4 viewPos;
    glm::vec4 ambientColor;
    glm::ivec4 countPad;
    glm::mat4 invViewProj;
    LightData lights[MAX_LIGHTS];
};
