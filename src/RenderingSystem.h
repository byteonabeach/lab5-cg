#pragma once
#include "Engine.h"
#include "GBuffer.h"
#include "Light.h"
#include "Camera.h"
#include "Terrain.h"
#include "MarchingCubes.h"
#include <vector>
#include <unordered_map>

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

struct BatchKey {
    int mesh;
    VkDescriptorSet matSet;
    bool isUnlit;
    bool isCurtain;
    bool isTransparent;
    glm::vec4 unlitColor;

    bool operator==(const BatchKey& o) const {
        return mesh == o.mesh && matSet == o.matSet && isUnlit == o.isUnlit && isCurtain == o.isCurtain && isTransparent == o.isTransparent && unlitColor == o.unlitColor;
    }
};

struct BatchKeyHash {
    size_t operator()(const BatchKey& k) const {
        size_t h = std::hash<int>()(k.mesh);
        auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
        mix(std::hash<uint64_t>()((uint64_t)k.matSet));
        mix((size_t)k.isUnlit | ((size_t)k.isCurtain << 1) | ((size_t)k.isTransparent << 2));
        mix(std::hash<float>()(k.unlitColor.x));
        mix(std::hash<float>()(k.unlitColor.y));
        mix(std::hash<float>()(k.unlitColor.z));
        mix(std::hash<float>()(k.unlitColor.w));
        return h;
    }
};

class RenderingSystem {
public:
    void init(Engine& engine);
    void cleanup(Engine& engine);
    void onResize(Engine& engine);
    void setLights(const std::vector<LightData>& lights) { pendingLights = lights; }
    void setTAAEnabled(bool enabled) { taaEnabled = enabled; }
    void setMarchingCubesEnabled(bool enabled) { marchingEnabled = enabled; }
    void setDistortionEnabled(bool enabled) { distortionEnabled = enabled; }
    void setDistortionVelocityEnabled(bool enabled) { distortionVelocity = enabled; }
    void setMotionVectorsVisible(bool visible) { showMotionVectors = visible; }
    void recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, int frameIndex, const Camera& camera, const std::vector<const SceneObject*>& objects, Engine& engine, float time, MeshHandle debugCubeMesh = {}, const std::vector<AABB>& staticNodes = {}, const std::vector<AABB>& dynamicNodes = {}, bool enableParticles = true, const Terrain* terrain = nullptr);

private:
    GBuffer gbuffer;
    struct GeomUBO {
        glm::mat4 view, proj;
        glm::vec4 cameraPos;
        glm::mat4 viewProj, prevViewProj;
        glm::vec4 params;
    };
    static constexpr int MAX_INSTANCES = 40000;
    std::vector<VkBuffer> instanceBufs;
    std::vector<VkDeviceMemory> instanceMems;
    std::vector<void*> instanceMapped;
    void createInstanceBuffers_(Engine& engine);
    std::vector<RenderBatch> activeBatches;
    std::unordered_map<BatchKey, size_t, BatchKeyHash> batchLookup;

    VkDescriptorSetLayout geomUBOLayout = VK_NULL_HANDLE;
    VkPipelineLayout geomPipelineLayout = VK_NULL_HANDLE;
    VkPipeline geomPipeline = VK_NULL_HANDLE;
    VkPipeline debugPipeline = VK_NULL_HANDLE;
    VkDescriptorPool geomDescPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> geomDescSets;
    std::vector<VkBuffer> geomUBOBufs;
    std::vector<VkDeviceMemory> geomUBOMems;
    std::vector<void*> geomUBOMapped;

    static constexpr VkFormat SCENE_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkRenderPass lightRenderPass = VK_NULL_HANDLE;
    VkFramebuffer lightFramebuffer = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout lightPipelineLayout = VK_NULL_HANDLE;
    VkPipeline lightPipeline = VK_NULL_HANDLE;
    VkDescriptorPool lightDescPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> lightDescSets;
    std::vector<VkBuffer> lightUBOBufs;
    std::vector<VkDeviceMemory> lightUBOMems;
    std::vector<void*> lightUBOMapped;

    VkImage sceneImage = VK_NULL_HANDLE;
    VkDeviceMemory sceneMemory = VK_NULL_HANDLE;
    VkImageView sceneView = VK_NULL_HANDLE;
    VkImage historyImages[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory historyMemories[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView historyViews[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler taaLinearSampler = VK_NULL_HANDLE;
    VkRenderPass taaRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> taaFramebuffers;
    VkDescriptorSetLayout taaDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout taaPipelineLayout = VK_NULL_HANDLE;
    VkPipeline taaPipeline = VK_NULL_HANDLE;
    VkDescriptorPool taaDescPool = VK_NULL_HANDLE;
    VkDescriptorSet taaDescSets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    bool taaEnabled = true;
    bool taaReset = true;
    uint32_t taaFrame = 0;
    glm::mat4 prevViewProj = glm::mat4(1.0f);
    bool hasPrevViewProj = false;
    bool showMotionVectors = false;

    VkImage distortedSceneImage = VK_NULL_HANDLE;
    VkDeviceMemory distortedSceneMemory = VK_NULL_HANDLE;
    VkImageView distortedSceneView = VK_NULL_HANDLE;
    VkImage distortedVelocityImage = VK_NULL_HANDLE;
    VkDeviceMemory distortedVelocityMemory = VK_NULL_HANDLE;
    VkImageView distortedVelocityView = VK_NULL_HANDLE;
    VkRenderPass distortionRenderPass = VK_NULL_HANDLE;
    VkFramebuffer distortionFramebuffer = VK_NULL_HANDLE;
    VkDescriptorSetLayout distortionDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout distortionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline distortionPipeline = VK_NULL_HANDLE;
    VkDescriptorPool distortionDescPool = VK_NULL_HANDLE;
    VkDescriptorSet distortionDescSet = VK_NULL_HANDLE;
    bool distortionEnabled = false;
    bool distortionVelocity = true;
    float distortionPrevTime = 0.0f;
    float distortionPrevStrength = 0.0f;

    MarchingCubes marching;
    bool marchingEnabled = true;

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

    void createTAAImages_(Engine& engine);
    void destroyTAAImages_(VkDevice device);
    void createTAAPass_(Engine& engine);
    void createTAAPipeline_(Engine& engine);
    void updateTAADescSets_(Engine& engine);

    void createDistortionImages_(Engine& engine);
    void destroyDistortionImages_(VkDevice device);
    void createDistortionPass_(Engine& engine);
    void createDistortionPipeline_(Engine& engine);
    void updateDistortionDescSet_(Engine& engine);

    void createParticleResources_(Engine& engine);
    void createParticlePipelines_(Engine& engine);

    void createTerrainPipeline_(Engine& engine);

    VkPipelineShaderStageCreateInfo loadShader_(Engine& engine, const std::string& path, VkShaderStageFlagBits stage);
};
