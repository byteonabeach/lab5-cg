#pragma once

#include "Engine.h"
#include "Culling.h"
#include "Camera.h"
#include <vector>
#include <memory>
#include <string>

struct TerrainPatchPush {
    glm::vec4 patchOffsetSize;
    glm::vec4 uvOffsetScale;
    float heightScale;
    int lodLevel;
    float pad0;
    float pad1;
};

class TerrainNode {
public:
    AABB bounds;
    glm::vec2 minXZ;
    float size;
    glm::vec2 uvMin;
    glm::vec2 uvScale;
    int depth;
    int lod;
    std::unique_ptr<TerrainNode> children[4];
    bool isLeaf = true;

    TerrainNode(glm::vec2 minCorner, float sideSize, glm::vec2 uvCorner, glm::vec2 uvSide, int d, float minHeight, float maxHeight)
        : minXZ(minCorner), size(sideSize), uvMin(uvCorner), uvScale(uvSide), depth(d) {
        bounds.minVal = glm::vec3(minXZ.x, minHeight, minXZ.y);
        bounds.maxVal = glm::vec3(minXZ.x + size, maxHeight, minXZ.y + size);
    }

    void subdivide(float minHeight, float maxHeight) {
        float halfSize = size * 0.5f;
        glm::vec2 halfUV = uvScale * 0.5f;

        children[0] = std::make_unique<TerrainNode>(minXZ, halfSize, uvMin, halfUV, depth + 1, minHeight, maxHeight);
        children[1] = std::make_unique<TerrainNode>(glm::vec2(minXZ.x + halfSize, minXZ.y), halfSize, glm::vec2(uvMin.x + halfUV.x, uvMin.y), halfUV, depth + 1, minHeight, maxHeight);
        children[2] = std::make_unique<TerrainNode>(glm::vec2(minXZ.x, minXZ.y + halfSize), halfSize, glm::vec2(uvMin.x, uvMin.y + halfUV.y), halfUV, depth + 1, minHeight, maxHeight);
        children[3] = std::make_unique<TerrainNode>(glm::vec2(minXZ.x + halfSize, minXZ.y + halfSize), halfSize, glm::vec2(uvMin.x + halfUV.x, uvMin.y + halfUV.y), halfUV, depth + 1, minHeight, maxHeight);
        isLeaf = false;
    }
};

class Terrain {
public:
    static constexpr int MAX_LOD = 5;
    static constexpr int GRID_RESOLUTION = 32;

    void init(Engine& engine, const std::string& heightmapPath, float worldSize = 600.0f, float heightScale = 45.0f);
    void cleanup(VkDevice device);
    void update(const Camera& camera, const Frustum& frustum);
    void draw(VkCommandBuffer cmd, VkPipelineLayout layout, const Engine& engine) const;

    bool isInitialized() const { return initialized; }

private:
    bool initialized = false;
    float totalWorldSize = 600.0f;
    float maxHeightScale = 45.0f;
    uint32_t patchIndexCount = 0;
    TextureHandle heightmapTexture;
    MeshHandle patchMeshHandle;
    VkDescriptorSet terrainDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool terrainDescPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout terrainDescLayout = VK_NULL_HANDLE;

    std::unique_ptr<TerrainNode> rootNode;
    std::vector<TerrainPatchPush> activePatches;

    void createPatchMesh_(Engine& engine);
    void createDescriptorSet_(Engine& engine);
    void traverseNode_(TerrainNode* node, const glm::vec3& camPos, const Frustum& frustum);
    TextureHandle loadOrGenerateHeightmap_(Engine& engine, const std::string& path);
};
