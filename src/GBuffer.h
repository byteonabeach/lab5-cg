#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>

// Forward declaration
class Engine;

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer
//
// Manages 3 off-screen color attachments + shared depth:
//   [0] gPosition  — VK_FORMAT_R32G32B32A32_SFLOAT  (world-space XYZ, w unused)
//   [1] gNormal    — VK_FORMAT_R16G16B16A16_SFLOAT  (world-space normal)
//   [2] gAlbedo    — VK_FORMAT_R8G8B8A8_UNORM        (diffuse color)
//   [3] depth      — findDepthFormat()
//
// Also owns the geometry render pass and framebuffer so that RenderingSystem
// can begin/end the geometry pass without knowing the raw Vulkan objects.
// ─────────────────────────────────────────────────────────────────────────────

class GBuffer {
public:
    static constexpr int NUM_ATTACHMENTS = 3; // color only; depth is separate

    // Formats for each attachment
    static constexpr VkFormat FORMAT_POSITION = VK_FORMAT_R32G32B32A32_SFLOAT;
    static constexpr VkFormat FORMAT_NORMAL   = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat FORMAT_ALBEDO   = VK_FORMAT_R8G8B8A8_UNORM;

    void init   (Engine& engine, uint32_t width, uint32_t height);
    void cleanup(VkDevice device);
    void recreate(Engine& engine, uint32_t width, uint32_t height);

    // ── Geometry render pass ──────────────────────────────────────────────
    VkRenderPass  getRenderPass()  const { return renderPass; }
    VkFramebuffer getFramebuffer() const { return framebuffer; }

    // ── Per-attachment views (used by RenderingSystem for descriptor sets) ─
    VkImageView getPositionView() const { return views[0]; }
    VkImageView getNormalView()   const { return views[1]; }
    VkImageView getAlbedoView()   const { return views[2]; }

    // One shared sampler (NEAREST filter — GBuffer values are exact per-pixel)
    VkSampler getSampler() const { return sampler; }

    VkExtent2D getExtent() const { return extent; }

private:
    VkExtent2D extent{};

    // Images & memory for 3 color attachments
    std::array<VkImage,        NUM_ATTACHMENTS> images{};
    std::array<VkDeviceMemory, NUM_ATTACHMENTS> memories{};
    std::array<VkImageView,    NUM_ATTACHMENTS> views{};

    // Depth
    VkImage        depthImage  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView    depthView   = VK_NULL_HANDLE;

    VkSampler     sampler     = VK_NULL_HANDLE;
    VkRenderPass  renderPass  = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    void createAttachments_(Engine& engine);
    void createRenderPass_ (Engine& engine);
    void createFramebuffer_(Engine& engine);
    void createSampler_    (VkDevice device);
    void destroyAttachments_(VkDevice device);
};
