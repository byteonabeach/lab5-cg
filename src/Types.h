#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;

    static VkVertexInputBindingDescription binding() {
        return {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::array<VkVertexInputAttributeDescription, 3> attrs() {
        return {{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)},
        }};
    }
};

struct alignas(16) UBO {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightPos;
    glm::vec4 lightColor;
    glm::vec4 viewPos;
    glm::vec2 uvOffset;
    glm::vec2 uvScale;
};

struct MeshData {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    std::string           texturePath;
};
