#pragma once
#include "Engine.h"
#include <vector>

class MarchingCubes {
public:
    static constexpr int GRID = 64;
    static constexpr uint32_t MAX_VERTICES = 262144;
    static constexpr float ISO = 0.35f;

    void init(Engine& engine, VkDescriptorSetLayout geomUBOLayout, VkRenderPass gbufferPass, VkRenderPass shadowPass, VkDescriptorSetLayout shadowUBOLayout);
    void cleanup(VkDevice device);

    void setVolume(const glm::vec3& c, float cellSize) { center = c; cell = cellSize; }
    void generate(VkCommandBuffer cmd, int frameIndex, float time, float prevTime);
    void drawGBuffer(VkCommandBuffer cmd, int frameIndex, VkDescriptorSet geomSet) const;
    void drawShadow(VkCommandBuffer cmd, int frameIndex, VkDescriptorSet shadowSet, VkDescriptorSet materialSet) const;

private:
    struct Push {
        glm::vec4 originCell;
        float time;
        float prevTime;
        float iso;
        int grid;
        uint32_t maxVertices;
    };

    glm::vec3 center{0.0f};
    float cell = 0.4f;

    VkBuffer triTableBuf = VK_NULL_HANDLE;
    VkDeviceMemory triTableMem = VK_NULL_HANDLE;
    std::vector<VkBuffer> vertexBufs, fieldBufs;
    std::vector<VkDeviceMemory> vertexMems, fieldMems;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descSets;

    VkPipelineLayout computeLayout = VK_NULL_HANDLE;
    VkPipeline fieldPipeline = VK_NULL_HANDLE;
    VkPipeline polygonizePipeline = VK_NULL_HANDLE;

    VkPipelineLayout gbufferLayout = VK_NULL_HANDLE;
    VkPipeline gbufferPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    void createBuffers_(Engine& engine);
    void createDescriptors_(Engine& engine);
    void createComputePipelines_(Engine& engine);
    void createGBufferPipeline_(Engine& engine, VkDescriptorSetLayout geomUBOLayout, VkRenderPass pass);
    void createShadowPipeline_(Engine& engine, VkDescriptorSetLayout shadowUBOLayout, VkRenderPass pass);
};
