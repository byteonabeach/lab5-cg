#include "GBuffer.h"
#include "Engine.h"

#include <stdexcept>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────

void GBuffer::init(Engine& engine, uint32_t width, uint32_t height) {
    extent = {width, height};
    createAttachments_(engine);
    createSampler_    (engine.getDevice());
    createRenderPass_ (engine);
    createFramebuffer_(engine);
}

void GBuffer::cleanup(VkDevice device) {
    vkDestroyFramebuffer(device, framebuffer, nullptr); framebuffer = VK_NULL_HANDLE;
    vkDestroyRenderPass (device, renderPass,  nullptr); renderPass  = VK_NULL_HANDLE;
    vkDestroySampler    (device, sampler,      nullptr); sampler     = VK_NULL_HANDLE;
    destroyAttachments_ (device);
}

void GBuffer::recreate(Engine& engine, uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(engine.getDevice());
    VkDevice dev = engine.getDevice();
    vkDestroyFramebuffer(dev, framebuffer, nullptr);
    destroyAttachments_ (dev);
    // sampler + renderPass are extent-independent, keep them
    extent = {width, height};
    createAttachments_(engine);
    createFramebuffer_(engine);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void GBuffer::createAttachments_(Engine& engine) {
    constexpr VkFormat fmts[NUM_ATTACHMENTS] = {
        FORMAT_POSITION,   // R32G32B32A32_SFLOAT
        FORMAT_NORMAL,     // R16G16B16A16_SFLOAT
        FORMAT_ALBEDO      // R8G8B8A8_UNORM
    };

    for (int i = 0; i < NUM_ATTACHMENTS; ++i) {
        engine.createImage(
            extent.width, extent.height, fmts[i],
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            images[i], memories[i]);
        views[i] = engine.createImageView(images[i], fmts[i], VK_IMAGE_ASPECT_COLOR_BIT);
        // Transition to color attachment layout (render pass will handle it from here)
        engine.transitionLayout(images[i], fmts[i],
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    // Depth
    VkFormat depthFmt = engine.findDepthFormat();
    engine.createImage(
        extent.width, extent.height, depthFmt,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage, depthMemory);
    depthView = engine.createImageView(depthImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT);
    engine.transitionLayout(depthImage, depthFmt,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void GBuffer::createSampler_(VkDevice device) {
    if (sampler != VK_NULL_HANDLE) return; // already created, reuse across resizes
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter    = VK_FILTER_NEAREST; // GBuffer values are per-pixel exact
    si.minFilter    = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    vkCreateSampler(device, &si, nullptr, &sampler);
}

void GBuffer::createRenderPass_(Engine& engine) {
    if (renderPass != VK_NULL_HANDLE) return; // reuse across resizes

    constexpr VkFormat fmts[NUM_ATTACHMENTS] = {
        FORMAT_POSITION, FORMAT_NORMAL, FORMAT_ALBEDO
    };
    VkFormat depthFmt = engine.findDepthFormat();

    // Attachment descriptions: 3 color + 1 depth
    std::array<VkAttachmentDescription, 4> atts{};
    for (int i = 0; i < NUM_ATTACHMENTS; ++i) {
        atts[i].format         = fmts[i];
        atts[i].samples        = VK_SAMPLE_COUNT_1_BIT;
        atts[i].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[i].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        atts[i].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[i].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // After geometry pass: transition so lighting pass can sample it
        atts[i].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    atts[3].format         = depthFmt;
    atts[3].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[3].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[3].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[3].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[3].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    atts[3].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, NUM_ATTACHMENTS> colorRefs{};
    for (int i = 0; i < NUM_ATTACHMENTS; ++i)
        colorRefs[i] = {(uint32_t)i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentReference depthRef{3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = NUM_ATTACHMENTS;
    subpass.pColorAttachments       = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;

    // Dependency: ensure geometry pass writes are visible to lighting pass fragment reads
    std::array<VkSubpassDependency, 2> deps{};
    // External → subpass 0
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    // subpass 0 → external (lighting pass)
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = (uint32_t)atts.size();
    rpci.pAttachments    = atts.data();
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = (uint32_t)deps.size();
    rpci.pDependencies   = deps.data();

    if (vkCreateRenderPass(engine.getDevice(), &rpci, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("GBuffer: vkCreateRenderPass failed");
}

void GBuffer::createFramebuffer_(Engine& engine) {
    std::array<VkImageView, 4> attachments = {
        views[0], views[1], views[2], depthView
    };
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass      = renderPass;
    fci.attachmentCount = (uint32_t)attachments.size();
    fci.pAttachments    = attachments.data();
    fci.width           = extent.width;
    fci.height          = extent.height;
    fci.layers          = 1;
    if (vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &framebuffer) != VK_SUCCESS)
        throw std::runtime_error("GBuffer: vkCreateFramebuffer failed");
}

void GBuffer::destroyAttachments_(VkDevice device) {
    for (int i = 0; i < NUM_ATTACHMENTS; ++i) {
        vkDestroyImageView(device, views[i],    nullptr);
        vkDestroyImage    (device, images[i],   nullptr);
        vkFreeMemory      (device, memories[i], nullptr);
        views[i]    = VK_NULL_HANDLE;
        images[i]   = VK_NULL_HANDLE;
        memories[i] = VK_NULL_HANDLE;
    }
    vkDestroyImageView(device, depthView,   nullptr);
    vkDestroyImage    (device, depthImage,  nullptr);
    vkFreeMemory      (device, depthMemory, nullptr);
    depthView   = VK_NULL_HANDLE;
    depthImage  = VK_NULL_HANDLE;
    depthMemory = VK_NULL_HANDLE;
}
