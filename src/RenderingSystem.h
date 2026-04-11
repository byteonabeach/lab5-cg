#pragma once

#include "Engine.h"
#include "GBuffer.h"
#include "Light.h"
#include "Camera.h"
#include <vector>

struct RenderBatch {
    MeshHandle mesh;
    VkDescriptorSet matSet;
    bool isUnlit;
    glm::vec4 unlitColor;
    float dispScale;
    bool isCurtain;
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
    void recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, int frameIndex, const Camera& camera, const std::vector<const SceneObject*>& objects, Engine& engine, float time, MeshHandle debugCubeMesh = {}, const std::vector<AABB>& debugBoxes = {});

private:
    GBuffer gbuffer;

    struct GeomUBO {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 cameraPos;
    };

    static constexpr int MAX_INSTANCES = 20000;
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
    VkImage shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory = VK_NULL_HANDLE;
    VkImageView shadowArrayView = VK_NULL_HANDLE;
    std::vector<VkImageView> shadowLayerViews;
    std::vector<VkFramebuffer> shadowFramebuffers;
    VkSampler shadowSampler = VK_NULL_HANDLE;

    std::vector<LightData> pendingLights;

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
    VkPipelineShaderStageCreateInfo loadShader_(Engine& engine, const std::string& path, VkShaderStageFlagBits stage);
};
