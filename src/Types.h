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

struct InstanceData {
    glm::mat4 model;
    glm::vec4 color;

    static VkVertexInputBindingDescription getBindingDesc() {
        VkVertexInputBindingDescription d{};
        d.binding = 1;
        d.stride = sizeof(InstanceData);
        d.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return d;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttrDescs() {
        std::array<VkVertexInputAttributeDescription, 5> a{};
        a[0] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, model) + 0 * sizeof(glm::vec4)};
        a[1] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, model) + 1 * sizeof(glm::vec4)};
        a[2] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, model) + 2 * sizeof(glm::vec4)};
        a[3] = {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, model) + 3 * sizeof(glm::vec4)};
        a[4] = {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, color)};
        return a;
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
    float     time;
    float     animMode;
    float     _pad[2];
};

struct MeshData {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    std::string           texturePath;
};
