#include "Terrain.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

void Terrain::init(Engine& engine, const std::string& heightmapPath, float worldSize, float heightScale) {
    totalWorldSize = worldSize;
    maxHeightScale = heightScale;

    heightmapTexture = loadOrGenerateHeightmap_(engine, heightmapPath);
    createPatchMesh_(engine);
    createDescriptorSet_(engine);

    float halfSize = totalWorldSize * 0.5f;
    rootNode = std::make_unique<TerrainNode>(
        glm::vec2(-halfSize, -halfSize),
        totalWorldSize,
        glm::vec2(0.0f, 0.0f),
        glm::vec2(1.0f, 1.0f),
        0,
        -5.0f,
        maxHeightScale + 5.0f
    );

    initialized = true;
}

void Terrain::cleanup(VkDevice device) {
    if (!initialized) return;
    vkDestroyDescriptorPool(device, terrainDescPool, nullptr);
    vkDestroyDescriptorSetLayout(device, terrainDescLayout, nullptr);
    initialized = false;
}

void Terrain::update(const Camera& camera, const Frustum& frustum) {
    if (!initialized || !rootNode) return;
    activePatches.clear();
    traverseNode_(rootNode.get(), camera.position, frustum);
}

void Terrain::traverseNode_(TerrainNode* node, const glm::vec3& camPos, const Frustum& frustum) {
    if (!frustum.intersects(node->bounds)) {
        return;
    }

    glm::vec2 centerXZ = node->minXZ + glm::vec2(node->size * 0.5f);
    float dist = glm::length(glm::vec2(camPos.x, camPos.z) - centerXZ);

    float lodThreshold = node->size * 1.75f;

    if (dist < lodThreshold && node->depth < MAX_LOD) {
        if (node->isLeaf) {
            node->subdivide(-5.0f, maxHeightScale + 5.0f);
        }
        for (int i = 0; i < 4; ++i) {
            traverseNode_(node->children[i].get(), camPos, frustum);
        }
    } else {
        TerrainPatchPush push{};
        push.patchOffsetSize = glm::vec4(node->minXZ.x, node->minXZ.y, node->size, 0.0f);
        push.uvOffsetScale = glm::vec4(node->uvMin.x, node->uvMin.y, node->uvScale.x, node->uvScale.y);
        push.heightScale = maxHeightScale;
        push.lodLevel = node->depth;
        activePatches.push_back(push);
    }
}

void Terrain::draw(VkCommandBuffer cmd, VkPipelineLayout layout, const Engine& engine) const {
    if (!initialized || activePatches.empty()) return;

    engine.bindMeshOnly(cmd, patchMeshHandle);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &terrainDescriptorSet, 0, nullptr);

    for (const auto& patch : activePatches) {
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(TerrainPatchPush), &patch);
        vkCmdDrawIndexed(cmd, GRID_RESOLUTION * GRID_RESOLUTION * 6, 1, 0, 0, 0);
    }
}

void Terrain::createPatchMesh_(Engine& engine) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (int z = 0; z <= GRID_RESOLUTION; ++z) {
        for (int x = 0; x <= GRID_RESOLUTION; ++x) {
            Vertex v{};
            float u = (float)x / (float)GRID_RESOLUTION;
            float w = (float)z / (float)GRID_RESOLUTION;
            v.pos = glm::vec3(u, 0.0f, w);
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.texCoord = glm::vec2(u, w);
            v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            vertices.push_back(v);
        }
    }

    for (int z = 0; z < GRID_RESOLUTION; ++z) {
        for (int x = 0; x < GRID_RESOLUTION; ++x) {
            uint32_t row1 = z * (GRID_RESOLUTION + 1);
            uint32_t row2 = (z + 1) * (GRID_RESOLUTION + 1);

            indices.push_back(row1 + x);
            indices.push_back(row2 + x);
            indices.push_back(row1 + x + 1);

            indices.push_back(row1 + x + 1);
            indices.push_back(row2 + x);
            indices.push_back(row2 + x + 1);
        }
    }

    patchMeshHandle = engine.createMesh(vertices, indices);
}

void Terrain::createDescriptorSet_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1;
    lci.pBindings = &b;
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &terrainDescLayout);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &ps;
    vkCreateDescriptorPool(dev, &pci, nullptr, &terrainDescPool);

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = terrainDescPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &terrainDescLayout;
    vkAllocateDescriptorSets(dev, &ai, &terrainDescriptorSet);

    VkDescriptorImageInfo ii = engine.getTextureDescriptorInfo(heightmapTexture);

    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = terrainDescriptorSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
}

TextureHandle Terrain::loadOrGenerateHeightmap_(Engine& engine, const std::string& path) {
    if (!path.empty() && fs::exists(path)) {
        return engine.loadTexture(path);
    }

    constexpr int W = 512, H = 512;
    std::vector<uint8_t> pixels(W * H * 4);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float fx = (float)x / (float)W;
            float fy = (float)y / (float)H;

            float cx = (fx - 0.5f) * 2.0f;
            float cy = (fy - 0.5f) * 2.0f;
            float dist = std::sqrt(cx * cx + cy * cy);
            float centerValley = std::clamp((dist - 0.12f) / 0.28f, 0.0f, 1.0f);

            float hills = 0.5f + 0.35f * std::sin(fx * 10.0f) * std::cos(fy * 10.0f)
                               + 0.15f * std::sin(fx * 24.0f + fy * 20.0f);
            float h = hills * centerValley;
            h = std::clamp(h, 0.0f, 1.0f);

            uint8_t val = (uint8_t)(h * 255.0f);
            int idx = (y * W + x) * 4;
            pixels[idx + 0] = val;
            pixels[idx + 1] = val;
            pixels[idx + 2] = val;
            pixels[idx + 3] = 255;
        }
    }

    return engine.registerRawTexture(W, H, pixels.data(), pixels.size());
}
