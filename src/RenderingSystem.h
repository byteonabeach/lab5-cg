#pragma once
#include "Engine.h"
#include "GBuffer.h"
#include "Light.h"
#include "Camera.h"
#include "Terrain.h"
#include <vector>

struct RenderBatch {
    MeshHandle mesh;
    VkDescriptorSet matSet;
    bool isUnlit;
    glm::vec4 unlitColor;
    float dispScale;
    bool isCurtain;
    bool isTransparent;
    uint32_t instanceOffset;
    uint32_t instanceCount;
    std::vector<InstanceData> instances;
};

class RenderingSystem {
public:
    void init(Engine& engine);
    void cleanup(Engine& engine);
    void onResize(Engine& engine);
    void setLights(const std::vector<LightData>& lights) { pendingLights = lights; }
    void recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, int frameIndex, const Camera& camera, const std::vector<const SceneObject*>& objects, Engine& engine, float time, MeshHandle debugCubeMesh = {}, const std::vector<AABB>& staticNodes = {}, const std::vector<AABB>& dynamicNodes = {}, bool enableParticles = true, const Terrain* terrain = nullptr);

private:
    GBuffer gbuffer;
    struct GeomUBO { glm::mat4 view, proj; glm::vec4 cameraPos; };
    static constexpr int MAX_INSTANCES = 40000;
    std::vector<VkBuffer> instanceBufs;
    std::vector<VkDeviceMemory> instanceMems;
    std::vector<void*> instanceMapped;
    void createInstanceBuffers_(Engine& engine);
    std::vector<RenderBatch> activeBatches;

    VkDescriptorSetLayout geomUBOLayout = VK_NULL_HANDLE;
    VkPipelineLayout geomPipelineLayout = VK_NULL_HANDLE;
    VkPipeline geomPipeline = VK_NULL_HANDLE;
    VkPipeline debugPipeline = VK_NULL_HANDLE;
    VkDescriptorPool geomDescPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> geomDescSets;
    std::vector<VkBuffer> geomUBOBufs;
    std::vector<VkDeviceMemory> geomUBOMems;
    std::vector<void*> geomUBOMapped;

    VkRenderPass lightRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> lightFramebuffers;
    VkDescriptorSetLayout lightDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout lightPipelineLayout = VK_NULL_HANDLE;
    VkPipeline lightPipeline = VK_NULL_HANDLE;
    VkDescriptorPool lightDescPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> lightDescSets;
    std::vector<VkBuffer> lightUBOBufs;
    std::vector<VkDeviceMemory> lightUBOMems;
    std::vector<void*> lightUBOMapped;

    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    std::vector<LightData> pendingLights;
    VkDescriptorSetLayout shadowDescLayout = VK_NULL_HANDLE;
    VkDescriptorPool shadowDescPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> shadowDescSets;
    std::vector<VkBuffer> shadowUBOBufs;
    std::vector<VkDeviceMemory> shadowUBOMems;
    std::vector<void*> shadowUBOMapped;

    std::vector<VkImage> shadowImages;
    std::vector<VkDeviceMemory> shadowMemories;
    std::vector<VkImageView> shadowArrayViews;
    std::vector<VkFramebuffer> shadowFramebuffers;

    VkDescriptorSetLayout particleComputeLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout particleResetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout particleGraphicsLayout = VK_NULL_HANDLE;

    VkPipelineLayout particleComputePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout particleResetPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout particleGraphicsPipelineLayout = VK_NULL_HANDLE;

    VkPipeline particleComputePipeline = VK_NULL_HANDLE;
    VkPipeline particleResetPipeline = VK_NULL_HANDLE;
    VkPipeline particleGraphicsPipeline = VK_NULL_HANDLE;

    VkBuffer particleBuffers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory particleMemories[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    VkDescriptorPool particleDescPool = VK_NULL_HANDLE;
    VkDescriptorSet particleComputeSets[2];
    VkDescriptorSet particleResetSets[2];
    VkDescriptorSet particleGraphicsSets[2];

    int particleFrameIndex = 0;
    float lastTime = 0.0f;

    VkDescriptorSetLayout terrainMaterialLayout = VK_NULL_HANDLE;
    VkPipelineLayout terrainPipelineLayout = VK_NULL_HANDLE;
    VkPipeline terrainPipeline = VK_NULL_HANDLE;

    VkDescriptorSet defaultMatSet = VK_NULL_HANDLE;

    void createShadowResources_(Engine& engine);
    void createShadowPipeline_(Engine& engine);
    void createGeomPipeline_(Engine& engine);
    void createDebugPipeline_(Engine& engine);
    void createLightRenderPass_(Engine& engine);
    void createLightPipeline_(Engine& engine);
    void createFramebuffers_(Engine& engine);
    void createDescriptors_(Engine& engine);
    void updateLightDescSets_(Engine& engine);
    void cleanupFramebuffers_(VkDevice device);

    void createParticleResources_(Engine& engine);
    void createParticlePipelines_(Engine& engine);

    void createTerrainPipeline_(Engine& engine);

    VkPipelineShaderStageCreateInfo loadShader_(Engine& engine, const std::string& path, VkShaderStageFlagBits stage);
};
