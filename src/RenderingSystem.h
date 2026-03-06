#pragma once
#include "Engine.h"
#include "GBuffer.h"
#include "Light.h"
#include "Camera.h"

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// RenderingSystem — deferred rendering orchestrator
//
// Geometry pass:
//   • Renders all SceneObjects to the GBuffer (position / normal / albedo)
//
// Lighting pass:
//   • Full-screen triangle reads GBuffer, evaluates Phong shading
//     for each LightData (Directional, Point, Spot)
//   • Writes to swapchain image
// ─────────────────────────────────────────────────────────────────────────────

class RenderingSystem {
public:
    void init   (Engine& engine);
    void cleanup(Engine& engine);

    // Call after Engine swapchain recreated (window resize)
    void onResize(Engine& engine);

    // Set lights for the current frame (call before recordFrame)
    void setLights(const std::vector<LightData>& lights) { pendingLights = lights; }

    // Record all deferred rendering commands into cmd
    void recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, int frameIndex,
                     const Camera& camera, const std::vector<SceneObject>& objects,
                     Engine& engine);

private:
    GBuffer gbuffer;

    // ── Geometry pass ─────────────────────────────────────────────────────
    // set 0: GeomUBO (view + proj), set 1: material sampler (from Engine)
    struct GeomUBO {
        glm::mat4 view;
        glm::mat4 proj;
    };

    VkDescriptorSetLayout  geomUBOLayout      = VK_NULL_HANDLE;
    VkPipelineLayout       geomPipelineLayout = VK_NULL_HANDLE;
    VkPipeline             geomPipeline       = VK_NULL_HANDLE;
    VkDescriptorPool       geomDescPool       = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet>  geomDescSets;  // [MAX_FRAMES] — UBO
    std::vector<VkBuffer>         geomUBOBufs;
    std::vector<VkDeviceMemory>   geomUBOMems;
    std::vector<void*>            geomUBOMapped;

    // ── Lighting pass ─────────────────────────────────────────────────────
    // set 0: 3 gbuffer samplers + LightsUBO
    VkRenderPass           lightRenderPass     = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> lightFramebuffers; // one per swapchain image
    VkDescriptorSetLayout  lightDescLayout     = VK_NULL_HANDLE;
    VkPipelineLayout       lightPipelineLayout = VK_NULL_HANDLE;
    VkPipeline             lightPipeline       = VK_NULL_HANDLE;
    VkDescriptorPool       lightDescPool       = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet>  lightDescSets; // [MAX_FRAMES]
    std::vector<VkBuffer>         lightUBOBufs;
    std::vector<VkDeviceMemory>   lightUBOMems;
    std::vector<void*>            lightUBOMapped;

    std::vector<LightData> pendingLights;

    // ── Helpers ───────────────────────────────────────────────────────────
    void createGeomPipeline_   (Engine& engine);
    void createLightRenderPass_(Engine& engine);
    void createLightPipeline_  (Engine& engine);
    void createFramebuffers_   (Engine& engine);
    void createDescriptors_    (Engine& engine);
    void updateLightDescSets_  (Engine& engine);
    void cleanupFramebuffers_  (VkDevice device);

    VkPipelineShaderStageCreateInfo loadShader_(
        Engine& engine, const std::string& path, VkShaderStageFlagBits stage);
};
