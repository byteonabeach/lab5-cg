//filename: ./src/GBuffer.h
#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <array>

class Engine;

class GBuffer {
public:
    static constexpr int NUM_ATTACHMENTS = 2; // Уменьшили с 3 до 2
    static constexpr VkFormat FORMAT_NORMAL = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat FORMAT_ALBEDO = VK_FORMAT_R8G8B8A8_UNORM;

    void init(Engine& engine, uint32_t width, uint32_t height);
    void cleanup(VkDevice device);
    void recreate(Engine& engine, uint32_t width, uint32_t height);

    VkRenderPass getRenderPass() const { return renderPass; }
    VkFramebuffer getFramebuffer() const { return framebuffer; }

    // PositionView удален, вместо него отдаем DepthView
    VkImageView getNormalView() const { return views[0]; }
    VkImageView getAlbedoView() const { return views[1]; }
    VkImageView getDepthView()  const { return depthView; }

    VkSampler getSampler() const { return sampler; }
    VkExtent2D getExtent() const { return extent; }

private:
    VkExtent2D extent{};
    std::array<VkImage, NUM_ATTACHMENTS> images{};
    std::array<VkDeviceMemory, NUM_ATTACHMENTS> memories{};
    std::array<VkImageView, NUM_ATTACHMENTS> views{};
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    void createAttachments_(Engine& engine);
    void createRenderPass_(Engine& engine);
    void createFramebuffer_(Engine& engine);
    void createSampler_(VkDevice device);
    void destroyAttachments_(VkDevice device);
};
