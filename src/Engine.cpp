#include "Engine.h"
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <set>
#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

void Engine::init(GLFWwindow* w) {
    window = w;
    createInstance_();
    createSurface_(w);
    pickPhysDevice_();
    createDevice_();
    createSwapchain_();
    createCommandPool_();
    createCommandBuffers_();
    createSyncObjects_();
    createMaterialLayout_();
    createMaterialPool_();
}

void Engine::cleanup() {
    vkDeviceWaitIdle(device);
    for (auto& t : textures) {
        vkDestroySampler(device, t.sampler, nullptr);
        vkDestroyImageView(device, t.view, nullptr);
        vkDestroyImage(device, t.image, nullptr);
        vkFreeMemory(device, t.memory, nullptr);
    }
    for (auto& m : meshes) {
        vkDestroyBuffer(device, m.vb, nullptr); vkFreeMemory(device, m.vm, nullptr);
        vkDestroyBuffer(device, m.ib, nullptr); vkFreeMemory(device, m.im, nullptr);
    }
    vkDestroyDescriptorPool(device, materialPool, nullptr);
    vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
    for (int i = 0; i < MAX_FRAMES; ++i) {
        vkDestroySemaphore(device, imageAvailable[i], nullptr);
        vkDestroySemaphore(device, renderFinished[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    cleanupSwapchain_();
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}

FrameContext Engine::beginFrame() {
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    uint32_t imageIndex;
    VkResult res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        return {VK_NULL_HANDLE, 0, currentFrame, false};
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) throw std::runtime_error("vkAcquireNextImageKHR failed");
    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    imagesInFlight[imageIndex] = inFlightFences[currentFrame];
    VkCommandBuffer cmd = commandBuffers[currentFrame];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);
    return {cmd, imageIndex, currentFrame, true};
}

void Engine::endFrame(const FrameContext& ctx) {
    if (!ctx.valid) return;
    vkEndCommandBuffer(ctx.cmd);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &imageAvailable[ctx.frameIndex];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &ctx.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinished[ctx.frameIndex];
    vkResetFences(device, 1, &inFlightFences[ctx.frameIndex]);
    vkQueueSubmit(graphicsQueue, 1, &si, inFlightFences[ctx.frameIndex]);
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished[ctx.frameIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &ctx.imageIndex;
    vkQueuePresentKHR(presentQueue, &pi);
    currentFrame = (currentFrame + 1) % MAX_FRAMES;
}

TextureHandle Engine::registerTexture_(uint32_t w, uint32_t h, const unsigned char* pixels, VkDeviceSize byteSize) {
    VkBuffer stagingBuf; VkDeviceMemory stagingMem;
    createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuf, stagingMem);
    void* data; vkMapMemory(device, stagingMem, 0, byteSize, 0, &data);
    memcpy(data, pixels, byteSize);
    vkUnmapMemory(device, stagingMem);
    TextureRes t;
    createImage(w, h, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, t.image, t.memory);
    transitionLayout(t.image, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufToImage(stagingBuf, t.image, w, h);
    transitionLayout(t.image, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(device, stagingBuf, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);
    t.view = createImageView(t.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = VK_TRUE;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physDevice, &props);
    si.maxAnisotropy = props.limits.maxSamplerAnisotropy;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(device, &si, nullptr, &t.sampler);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = materialPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &materialLayout;
    vkAllocateDescriptorSets(device, &ai, &t.set);
    VkDescriptorImageInfo imgInfo{t.sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = t.set;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    int id = (int)textures.size();
    textures.push_back(std::move(t));
    return TextureHandle{id};
}

TextureHandle Engine::loadTexture(const std::string& path) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!pixels) return createWhiteTexture();
    auto handle = registerTexture_((uint32_t)w, (uint32_t)h, pixels, (VkDeviceSize)w*h*4);
    stbi_image_free(pixels);
    return handle;
}

TextureHandle Engine::createWhiteTexture() {
    if (cachedWhiteTex.valid()) return cachedWhiteTex;
    uint8_t white[4] = {255,255,255,255};
    cachedWhiteTex = registerTexture_(1, 1, white, 4);
    return cachedWhiteTex;
}

MeshHandle Engine::createMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    MeshRes m;
    m.indexCount = (uint32_t)indices.size();
    auto upload = [&](VkBufferUsageFlags usage, const void* src, VkDeviceSize sz, VkBuffer& buf, VkDeviceMemory& mem) {
        VkBuffer sb; VkDeviceMemory sm;
        createBuffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sm);
        void* p; vkMapMemory(device, sm, 0, sz, 0, &p);
        memcpy(p, src, sz); vkUnmapMemory(device, sm);
        createBuffer(sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT|usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf, mem);
        copyBuffer(sb, buf, sz);
        vkDestroyBuffer(device, sb, nullptr); vkFreeMemory(device, sm, nullptr);
    };
    upload(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts.data(), sizeof(Vertex)*verts.size(), m.vb, m.vm);
    upload(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data(), sizeof(uint32_t)*indices.size(), m.ib, m.im);
    int id = (int)meshes.size();
    meshes.push_back(std::move(m));
    return MeshHandle{id};
}

void Engine::createInstance_() {
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "VulkanDeferred";
    ai.apiVersion = VK_API_VERSION_1_2;
    uint32_t glfwCount;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwCount);
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    vkCreateInstance(&ci, nullptr, &instance);
}

void Engine::createSurface_(GLFWwindow* w) {
    glfwCreateWindowSurface(instance, w, nullptr, &surface);
}

void Engine::pickPhysDevice_() {
    uint32_t cnt;
    vkEnumeratePhysicalDevices(instance, &cnt, nullptr);
    std::vector<VkPhysicalDevice> devs(cnt);
    vkEnumeratePhysicalDevices(instance, &cnt, devs.data());
    physDevice = devs[0];
    for (auto d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { physDevice = d; break; }
    }
}

void Engine::createDevice_() {
    uint32_t qfCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &qfCount, qf.data());
    graphicsFamily = presentFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphicsFamily = i;
        VkBool32 pres = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physDevice, i, surface, &pres);
        if (pres) presentFamily = i;
        if (graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX) break;
    }
    std::set<uint32_t> uq = {graphicsFamily, presentFamily};
    std::vector<VkDeviceQueueCreateInfo> qcis;
    float prio = 1.0f;
    for (uint32_t f : uq) {
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = f; qi.queueCount = 1; qi.pQueuePriorities = &prio;
        qcis.push_back(qi);
    }
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.queueCreateInfoCount = (uint32_t)qcis.size();
    ci.pQueueCreateInfos = qcis.data();
    ci.enabledExtensionCount = (uint32_t)kDeviceExtensions.size();
    ci.ppEnabledExtensionNames = kDeviceExtensions.data();
    ci.pEnabledFeatures = &features;
    vkCreateDevice(physDevice, &ci, nullptr, &device);
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}

void Engine::createSwapchain_() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, surface, &caps);
    uint32_t fmtCnt;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &fmtCnt, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCnt);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &fmtCnt, fmts.data());
    uint32_t pmCnt;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &pmCnt, nullptr);
    std::vector<VkPresentModeKHR> pms(pmCnt);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &pmCnt, pms.data());
    VkSurfaceFormatKHR fmt = fmts[0];
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { fmt = f; break; }
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : pms) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { pm = m; break; }

    if (caps.currentExtent.width != UINT32_MAX) {
        swapExtent = caps.currentExtent;
    } else {
        int w, h; glfwGetFramebufferSize(window, &w, &h);
        swapExtent.width = std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width);
        swapExtent.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imgCnt = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imgCnt = std::min(imgCnt, caps.maxImageCount);
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface;
    ci.minImageCount = imgCnt;
    ci.imageFormat = fmt.format;
    ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent = swapExtent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    uint32_t families[] = {graphicsFamily, presentFamily};
    if (graphicsFamily != presentFamily) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = pm;
    ci.clipped = VK_TRUE;
    vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);
    swapFormat = fmt.format;
    vkGetSwapchainImagesKHR(device, swapchain, &imgCnt, nullptr);
    swapImages.resize(imgCnt);
    vkGetSwapchainImagesKHR(device, swapchain, &imgCnt, swapImages.data());
    swapImageViews.resize(imgCnt);
    for (size_t i = 0; i < imgCnt; ++i)
        swapImageViews[i] = createImageView(swapImages[i], swapFormat, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
}

void Engine::createCommandPool_() {
    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.queueFamilyIndex = graphicsFamily;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device, &ci, nullptr, &commandPool);
}

void Engine::createCommandBuffers_() {
    commandBuffers.resize(MAX_FRAMES);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = MAX_FRAMES;
    vkAllocateCommandBuffers(device, &ai, commandBuffers.data());
}

void Engine::createSyncObjects_() {
    imageAvailable.resize(MAX_FRAMES);
    renderFinished.resize(MAX_FRAMES);
    inFlightFences.resize(MAX_FRAMES);
    imagesInFlight.resize(swapImages.size(), VK_NULL_HANDLE);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < MAX_FRAMES; ++i) {
        vkCreateSemaphore(device, &si, nullptr, &imageAvailable[i]);
        vkCreateSemaphore(device, &si, nullptr, &renderFinished[i]);
        vkCreateFence(device, &fi, nullptr, &inFlightFences[i]);
    }
}

void Engine::createMaterialLayout_() {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = 1;
    ci.pBindings = &b;
    vkCreateDescriptorSetLayout(device, &ci, nullptr, &materialLayout);
}

void Engine::createMaterialPool_() {
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048};
    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.poolSizeCount = 1;
    ci.pPoolSizes = &ps;
    ci.maxSets = 2048;
    vkCreateDescriptorPool(device, &ci, nullptr, &materialPool);
}

void Engine::cleanupSwapchain_() {
    for (auto iv : swapImageViews) vkDestroyImageView(device, iv, nullptr);
    swapImageViews.clear();
    vkDestroySwapchainKHR(device, swapchain, nullptr);
}

void Engine::recreateSwapchain() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window, &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(device);
    cleanupSwapchain_();
    createSwapchain_();
    imagesInFlight.assign(swapImages.size(), VK_NULL_HANDLE);
}

uint32_t Engine::findMemoryType(uint32_t filter, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physDevice, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1 << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return 0;
}

VkFormat Engine::findDepthFormat() const {
    for (VkFormat fmt : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(physDevice, fmt, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return fmt;
    }
    return VK_FORMAT_UNDEFINED;
}

std::vector<char> Engine::readFile(const std::string& path) const {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        throw std::runtime_error("Failed to open file: " + path + " (Make sure you are running the program from the correct working directory!)");
    }
    size_t sz = (size_t)f.tellg();
    f.seekg(0);
    std::vector<char> buf(sz);
    f.read(buf.data(), sz);
    return buf;
}

void Engine::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &ci, nullptr, &buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
    vkAllocateMemory(device, &ai, nullptr, &mem);
    vkBindBufferMemory(device, buf, mem, 0);
}

void Engine::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBuffer cmd = beginSingleTime();
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    endSingleTime(cmd);
}

VkCommandBuffer Engine::beginSingleTime() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void Engine::endSingleTime(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

void Engine::createImage(uint32_t w, uint32_t h, uint32_t layers, VkFormat fmt, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props, VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.extent = {w, h, 1};
    ci.mipLevels = 1; ci.arrayLayers = layers; ci.format = fmt;
    ci.tiling = tiling; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = usage; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(device, &ci, nullptr, &img);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, img, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
    vkAllocateMemory(device, &ai, nullptr, &mem);
    vkBindImageMemory(device, img, mem, 0);
}

void Engine::transitionLayout(VkImage img, uint32_t layers, VkFormat fmt, VkImageLayout from, VkImageLayout to) {
    VkCommandBuffer cmd = beginSingleTime();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = from; barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img;
    bool isDepth = (fmt == VK_FORMAT_D32_SFLOAT || fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT);
    barrier.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT) barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = layers;
    VkPipelineStageFlags src, dst;
    if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; src = VK_PIPELINE_STAGE_TRANSFER_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (from == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        return;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endSingleTime(cmd);
}

void Engine::copyBufToImage(VkBuffer buf, VkImage img, uint32_t w, uint32_t h) {
    VkCommandBuffer cmd = beginSingleTime();
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endSingleTime(cmd);
}

VkImageView Engine::createImageView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect, uint32_t baseLayer, uint32_t layerCount, VkImageViewType viewType) const {
    VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ci.image = img;
    ci.viewType = viewType;
    ci.format = fmt;
    ci.subresourceRange.aspectMask = aspect;
    ci.subresourceRange.levelCount = 1;
    ci.subresourceRange.baseArrayLayer = baseLayer;
    ci.subresourceRange.layerCount = layerCount;
    VkImageView view;
    vkCreateImageView(device, &ci, nullptr, &view);
    return view;
}

VkShaderModule Engine::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule sm;
    vkCreateShaderModule(device, &ci, nullptr, &sm);
    return sm;
}
