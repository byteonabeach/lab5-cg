#include "GBuffer.h"
#include "Engine.h"

void GBuffer::init(Engine& engine, uint32_t width, uint32_t height) {
    extent = {width, height};
    createAttachments_(engine);
    createSampler_(engine.getDevice());
    createRenderPass_(engine);
    createFramebuffer_(engine);
}

void GBuffer::cleanup(VkDevice device) {
    vkDestroyFramebuffer(device, framebuffer, nullptr); framebuffer = VK_NULL_HANDLE;
    vkDestroyRenderPass(device, renderPass, nullptr); renderPass = VK_NULL_HANDLE;
    vkDestroySampler(device, sampler, nullptr); sampler = VK_NULL_HANDLE;
    destroyAttachments_(device);
}

void GBuffer::recreate(Engine& engine, uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(engine.getDevice());
    VkDevice dev = engine.getDevice();
    vkDestroyFramebuffer(dev, framebuffer, nullptr);
    destroyAttachments_(dev);
    extent = {width, height};
    createAttachments_(engine);
    createFramebuffer_(engine);
}

void GBuffer::createAttachments_(Engine& engine) {
    constexpr VkFormat fmts[NUM_ATTACHMENTS] = { FORMAT_NORMAL, FORMAT_ALBEDO };
    for (int i = 0; i < NUM_ATTACHMENTS; ++i) {
        engine.createImage(extent.width, extent.height, 1, fmts[i], VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, images[i], memories[i]);
        views[i] = engine.createImageView(images[i], fmts[i], VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
        engine.transitionLayout(images[i], 1, fmts[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    VkFormat depthFmt = engine.findDepthFormat();

    // ВАЖНО: Добавляем VK_IMAGE_USAGE_SAMPLED_BIT к Depth Buffer, чтобы читать его в шейдере освещения
    engine.createImage(extent.width, extent.height, 1, depthFmt, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthMemory);
    depthView = engine.createImageView(depthImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
    engine.transitionLayout(depthImage, 1, depthFmt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void GBuffer::createSampler_(VkDevice device) {
    if (sampler != VK_NULL_HANDLE) return;
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    vkCreateSampler(device, &si, nullptr, &sampler);
}

void GBuffer::createRenderPass_(Engine& engine) {
    if (renderPass != VK_NULL_HANDLE) return;
    constexpr VkFormat fmts[NUM_ATTACHMENTS] = { FORMAT_NORMAL, FORMAT_ALBEDO };
    VkFormat depthFmt = engine.findDepthFormat();
    std::array<VkAttachmentDescription, 3> atts{}; // Теперь 3 (Normal, Albedo, Depth)

    for (int i = 0; i < NUM_ATTACHMENTS; ++i) {
        atts[i].format = fmts[i]; atts[i].samples = VK_SAMPLE_COUNT_1_BIT; atts[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        atts[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; atts[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // Настраиваем RenderPass так, чтобы по окончании он переводил Depth Buffer в режим ЧТЕНИЯ (SHADER_READ_ONLY)
    atts[2].format = depthFmt; atts[2].samples = VK_SAMPLE_COUNT_1_BIT; atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentReference, NUM_ATTACHMENTS> colorRefs{};
    for (int i = 0; i < NUM_ATTACHMENTS; ++i) colorRefs[i] = {(uint32_t)i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = NUM_ATTACHMENTS;
    subpass.pColorAttachments = colorRefs.data(); subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = (uint32_t)atts.size(); rpci.pAttachments = atts.data(); rpci.subpassCount = 1; rpci.pSubpasses = &subpass;
    rpci.dependencyCount = (uint32_t)deps.size(); rpci.pDependencies = deps.data();
    vkCreateRenderPass(engine.getDevice(), &rpci, nullptr, &renderPass);
}

void GBuffer::createFramebuffer_(Engine& engine) {
    std::array<VkImageView, 3> attachments = {views[0], views[1], depthView};
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = renderPass; fci.attachmentCount = (uint32_t)attachments.size(); fci.pAttachments = attachments.data();
    fci.width = extent.width; fci.height = extent.height; fci.layers = 1;
    vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &framebuffer);
}

void GBuffer::destroyAttachments_(VkDevice device) {
    for (int i = 0; i < NUM_ATTACHMENTS; ++i) {
        vkDestroyImageView(device, views[i], nullptr); vkDestroyImage(device, images[i], nullptr); vkFreeMemory(device, memories[i], nullptr);
        views[i] = VK_NULL_HANDLE; images[i] = VK_NULL_HANDLE; memories[i] = VK_NULL_HANDLE;
    }
    vkDestroyImageView(device, depthView, nullptr); vkDestroyImage(device, depthImage, nullptr); vkFreeMemory(device, depthMemory, nullptr);
    depthView = VK_NULL_HANDLE; depthImage = VK_NULL_HANDLE; depthMemory = VK_NULL_HANDLE;
}
