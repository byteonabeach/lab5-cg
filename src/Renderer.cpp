#include "Renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>
#include <cstring>
#include <limits>

static const std::vector<const char*> kDevExts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
static const std::vector<const char*> kLayers  = { "VK_LAYER_KHRONOS_validation" };

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCb(
    VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* d, void*)
{
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[vk] " << d->pMessage << "\n";
    return VK_FALSE;
}

Renderer::Renderer(Window& window) : m_win(window) {
#ifndef NDEBUG
    m_validation = true;
#endif
    initInstance();
    initSurface();
    initDevice();
    initSwapchain();
    initImageViews();
    initRenderPass();
    initDepth();
    initFramebuffers();
    initCmdPool();
    initDescLayout();
    initPipeline();
    initWhiteTex();
    initUBOs();
    initDescPool();
    initDescSets();
    initCmdBuffers();
    initSync();

    m_ubo.lightPos   = glm::vec4(3, 5, 3, 0);
    m_ubo.lightColor = glm::vec4(1, 1, 1, 64);
    m_ubo.uvOffset   = glm::vec2(0, 0);
    m_ubo.uvScale    = glm::vec2(1, 1);
    setCamera({0, 1, 3}, {0, 0, 0}, {0, 1, 0});
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(m_dev);
    destroySwapchain();

    auto destroyTex = [&](VkImageView& v, VkImage& i, VkDeviceMemory& m, VkSampler& s) {
        if (s) { vkDestroySampler(m_dev, s, nullptr); s = VK_NULL_HANDLE; }
        if (v) { vkDestroyImageView(m_dev, v, nullptr); v = VK_NULL_HANDLE; }
        if (i) { vkDestroyImage(m_dev, i, nullptr); i = VK_NULL_HANDLE; }
        if (m) { vkFreeMemory(m_dev, m, nullptr); m = VK_NULL_HANDLE; }
    };
    destroyTex(m_texV, m_texImg, m_texMem, m_texS);
    destroyTex(m_wV,   m_wImg,   m_wMem,   m_wS);

    clearMeshes();

    for (int i = 0; i < FRAMES; i++) {
        vkDestroyBuffer(m_dev, m_ub[i], nullptr);
        vkFreeMemory(m_dev, m_ubm[i], nullptr);
    }

    vkDestroyDescriptorPool(m_dev, m_dp, nullptr);
    vkDestroyDescriptorSetLayout(m_dev, m_dsl, nullptr);
    vkDestroyPipeline(m_dev, m_pipe, nullptr);
    vkDestroyPipelineLayout(m_dev, m_pl, nullptr);
    vkDestroyRenderPass(m_dev, m_rp, nullptr);
    vkDestroyCommandPool(m_dev, m_cp, nullptr);

    for (int i = 0; i < FRAMES; i++) {
        vkDestroySemaphore(m_dev, m_imgReady[i], nullptr);
        vkDestroySemaphore(m_dev, m_renDone[i], nullptr);
        vkDestroyFence(m_dev, m_fence[i], nullptr);
    }

    vkDestroyDevice(m_dev, nullptr);

    if (m_dbg) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_inst, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(m_inst, m_dbg, nullptr);
    }

    vkDestroySurfaceKHR(m_inst, m_surf, nullptr);
    vkDestroyInstance(m_inst, nullptr);
}

void Renderer::initInstance() {
    if (m_validation) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> layers(n);
        vkEnumerateInstanceLayerProperties(&n, layers.data());
        bool found = false;
        for (auto& l : layers)
            if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) { found = true; break; }
        if (!found) m_validation = false;
    }

    VkApplicationInfo ai{};
    ai.sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.apiVersion = VK_API_VERSION_1_2;

    uint32_t n = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&n);
    std::vector<const char*> extensions(exts, exts + n);
    if (m_validation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    if (m_validation) {
        ci.enabledLayerCount   = kLayers.size();
        ci.ppEnabledLayerNames = kLayers.data();
    }

    if (vkCreateInstance(&ci, nullptr, &m_inst) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");

    if (m_validation) {
        VkDebugUtilsMessengerCreateInfoEXT dci{};
        dci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        dci.pfnUserCallback = debugCb;
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_inst, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(m_inst, &dci, nullptr, &m_dbg);
    }
}

void Renderer::initSurface() {
    if (glfwCreateWindowSurface(m_inst, m_win.handle(), nullptr, &m_surf) != VK_SUCCESS)
        throw std::runtime_error("glfwCreateWindowSurface failed");
}

void Renderer::initDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(m_inst, &n, nullptr);
    if (!n) throw std::runtime_error("no Vulkan GPU found");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(m_inst, &n, devs.data());
    for (auto d : devs) {
        if (deviceOk(d)) { m_gpu = d; break; }
    }
    if (!m_gpu) throw std::runtime_error("no suitable GPU");

    auto qfi = findQFI(m_gpu);
    m_gfxFam  = qfi.gfx.value();
    m_presFam = qfi.present.value();

    std::set<uint32_t> fams = {m_gfxFam, m_presFam};
    float prio = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    for (uint32_t f : fams) {
        VkDeviceQueueCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = f;
        q.queueCount = 1;
        q.pQueuePriorities = &prio;
        qcis.push_back(q);
    }

    VkPhysicalDeviceFeatures feat{};
    feat.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = qcis.size();
    ci.pQueueCreateInfos       = qcis.data();
    ci.enabledExtensionCount   = kDevExts.size();
    ci.ppEnabledExtensionNames = kDevExts.data();
    ci.pEnabledFeatures        = &feat;
    if (m_validation) {
        ci.enabledLayerCount   = kLayers.size();
        ci.ppEnabledLayerNames = kLayers.data();
    }

    if (vkCreateDevice(m_gpu, &ci, nullptr, &m_dev) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");

    vkGetDeviceQueue(m_dev, m_gfxFam, 0, &m_gfxQ);
    vkGetDeviceQueue(m_dev, m_presFam, 0, &m_presQ);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_gpu, &props);
    std::cout << "gpu: " << props.deviceName << "\n";
}

void Renderer::initSwapchain() {
    auto sc  = querySC(m_gpu);
    auto fmt = pickFmt(sc.fmts);
    auto mode= pickMode(sc.modes);
    auto ext = pickExtent(sc.caps);

    uint32_t count = sc.caps.minImageCount + 1;
    if (sc.caps.maxImageCount > 0)
        count = std::min(count, sc.caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface         = m_surf;
    ci.minImageCount   = count;
    ci.imageFormat     = fmt.format;
    ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent     = ext;
    ci.imageArrayLayers= 1;
    ci.imageUsage      = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t fams[] = {m_gfxFam, m_presFam};
    if (m_gfxFam != m_presFam) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = fams;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = sc.caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = mode;
    ci.clipped        = VK_TRUE;

    if (vkCreateSwapchainKHR(m_dev, &ci, nullptr, &m_sc) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR failed");

    vkGetSwapchainImagesKHR(m_dev, m_sc, &count, nullptr);
    m_scImgs.resize(count);
    vkGetSwapchainImagesKHR(m_dev, m_sc, &count, m_scImgs.data());
    m_scFmt = fmt.format;
    m_scExt = ext;
}

void Renderer::initImageViews() {
    m_scViews.resize(m_scImgs.size());
    for (size_t i = 0; i < m_scImgs.size(); i++)
        m_scViews[i] = mkView(m_scImgs[i], m_scFmt, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Renderer::initRenderPass() {
    VkAttachmentDescription col{};
    col.format         = m_scFmt;
    col.samples        = VK_SAMPLE_COUNT_1_BIT;
    col.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    col.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    col.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    col.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    col.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    col.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription dep{};
    dep.format         = depthFmt();
    dep.samples        = VK_SAMPLE_COUNT_1_BIT;
    dep.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    dep.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    dep.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    dep.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    dep.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    dep.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &colRef;
    sub.pDepthStencilAttachment = &depRef;

    VkSubpassDependency d{};
    d.srcSubpass    = VK_SUBPASS_EXTERNAL;
    d.dstSubpass    = 0;
    d.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    d.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    d.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> atts = {col, dep};
    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = atts.size();
    ci.pAttachments    = atts.data();
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies   = &d;

    if (vkCreateRenderPass(m_dev, &ci, nullptr, &m_rp) != VK_SUCCESS)
        throw std::runtime_error("vkCreateRenderPass failed");
}

void Renderer::initDepth() {
    auto fmt = depthFmt();
    mkImg(m_scExt.width, m_scExt.height, fmt, VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_di, m_dm);
    m_dv = mkView(m_di, fmt, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Renderer::initFramebuffers() {
    m_fbs.resize(m_scViews.size());
    for (size_t i = 0; i < m_scViews.size(); i++) {
        std::array<VkImageView, 2> atts = {m_scViews[i], m_dv};
        VkFramebufferCreateInfo fi{};
        fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass      = m_rp;
        fi.attachmentCount = atts.size();
        fi.pAttachments    = atts.data();
        fi.width           = m_scExt.width;
        fi.height          = m_scExt.height;
        fi.layers          = 1;
        vkCreateFramebuffer(m_dev, &fi, nullptr, &m_fbs[i]);
    }
}

void Renderer::initCmdPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = m_gfxFam;
    vkCreateCommandPool(m_dev, &ci, nullptr, &m_cp);
}

void Renderer::initDescLayout() {
    VkDescriptorSetLayoutBinding ubo{};
    ubo.binding      = 0;
    ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo.descriptorCount = 1;
    ubo.stageFlags   = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding tex{};
    tex.binding      = 1;
    tex.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    tex.descriptorCount = 1;
    tex.stageFlags   = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {ubo, tex};
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = bindings.size();
    ci.pBindings    = bindings.data();
    vkCreateDescriptorSetLayout(m_dev, &ci, nullptr, &m_dsl);
}

void Renderer::initPipeline() {
    auto vert = makeShader(readFile(SHADER_DIR "phong_vert.spv"));
    auto frag = makeShader(readFile(SHADER_DIR "phong_frag.spv"));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    auto bd = Vertex::binding();
    auto ad = Vertex::attrs();
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bd;
    vi.vertexAttributeDescriptionCount = ad.size();
    vi.pVertexAttributeDescriptions    = ad.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    std::vector<VkDynamicState> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = dynStates.size();
    dyn.pDynamicStates    = dynStates.data();

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_BACK_BIT;
    rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                         VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts    = &m_dsl;
    vkCreatePipelineLayout(m_dev, &pli, nullptr, &m_pl);

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &cb;
    pci.pDynamicState       = &dyn;
    pci.layout              = m_pl;
    pci.renderPass          = m_rp;

    if (vkCreateGraphicsPipelines(m_dev, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipe) != VK_SUCCESS)
        throw std::runtime_error("vkCreateGraphicsPipelines failed");

    vkDestroyShaderModule(m_dev, vert, nullptr);
    vkDestroyShaderModule(m_dev, frag, nullptr);
}

void Renderer::initWhiteTex() {
    uint8_t px[4] = {255, 255, 255, 255};
    VkBuffer sb; VkDeviceMemory sm;
    mkBuf(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sm);
    void* p; vkMapMemory(m_dev, sm, 0, 4, 0, &p);
    memcpy(p, px, 4);
    vkUnmapMemory(m_dev, sm);

    mkImg(1, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_wImg, m_wMem);
    transitionImg(m_wImg, VK_FORMAT_R8G8B8A8_SRGB,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    cpBufToImg(sb, m_wImg, 1, 1);
    transitionImg(m_wImg, VK_FORMAT_R8G8B8A8_SRGB,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(m_dev, sb, nullptr);
    vkFreeMemory(m_dev, sm, nullptr);

    m_wV = mkView(m_wImg, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = VK_TRUE;
    si.maxAnisotropy    = 16.f;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(m_dev, &si, nullptr, &m_wS);
}

void Renderer::initUBOs() {
    for (int i = 0; i < FRAMES; i++) {
        mkBuf(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
              m_ub[i], m_ubm[i]);
        vkMapMemory(m_dev, m_ubm[i], 0, sizeof(UBO), 0, &m_ubp[i]);
    }
}

void Renderer::initDescPool() {
    std::array<VkDescriptorPoolSize, 2> sz = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         FRAMES},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FRAMES},
    }};
    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = sz.size();
    ci.pPoolSizes    = sz.data();
    ci.maxSets       = FRAMES;
    vkCreateDescriptorPool(m_dev, &ci, nullptr, &m_dp);
}

void Renderer::initDescSets() {
    std::vector<VkDescriptorSetLayout> layouts(FRAMES, m_dsl);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_dp;
    ai.descriptorSetCount = FRAMES;
    ai.pSetLayouts        = layouts.data();
    vkAllocateDescriptorSets(m_dev, &ai, m_ds.data());
    refreshDescSets();
}

void Renderer::initCmdBuffers() {
    m_cmds.resize(FRAMES);
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_cp;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = FRAMES;
    vkAllocateCommandBuffers(m_dev, &ai, m_cmds.data());
}

void Renderer::initSync() {
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < FRAMES; i++) {
        vkCreateSemaphore(m_dev, &si, nullptr, &m_imgReady[i]);
        vkCreateSemaphore(m_dev, &si, nullptr, &m_renDone[i]);
        vkCreateFence(m_dev, &fi, nullptr, &m_fence[i]);
    }
}

bool Renderer::beginFrame() {
    vkWaitForFences(m_dev, 1, &m_fence[m_frame], VK_TRUE, UINT64_MAX);

    VkResult r = vkAcquireNextImageKHR(m_dev, m_sc, UINT64_MAX,
                                        m_imgReady[m_frame], VK_NULL_HANDLE, &m_imgIdx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return false; }

    vkResetFences(m_dev, 1, &m_fence[m_frame]);

    auto cmd = m_cmds[m_frame];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);

    std::array<VkClearValue, 2> clear{};
    clear[0].color        = {{0.05f, 0.05f, 0.05f, 1.f}};
    clear[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rpi{};
    rpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass        = m_rp;
    rpi.framebuffer       = m_fbs[m_imgIdx];
    rpi.renderArea.extent = m_scExt;
    rpi.clearValueCount   = clear.size();
    rpi.pClearValues      = clear.data();
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe);

    VkViewport vp{0, 0, (float)m_scExt.width, (float)m_scExt.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0,0}, m_scExt};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    m_recording = true;
    return true;
}

void Renderer::draw(int idx, const glm::mat4& model) {
    if (!m_recording || idx < 0 || idx >= (int)m_meshes.size()) return;
    updateUBO(m_frame, model);

    auto cmd = m_cmds[m_frame];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             m_pl, 0, 1, &m_ds[m_frame], 0, nullptr);

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_meshes[idx].vb, &off);
    vkCmdBindIndexBuffer(cmd, m_meshes[idx].ib, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m_meshes[idx].count, 1, 0, 0, 0);
}

void Renderer::endFrame() {
    if (!m_recording) return;
    m_recording = false;

    auto cmd = m_cmds[m_frame];
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &m_imgReady[m_frame];
    si.pWaitDstStageMask    = &wait;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &m_renDone[m_frame];
    vkQueueSubmit(m_gfxQ, 1, &si, m_fence[m_frame]);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_renDone[m_frame];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_sc;
    pi.pImageIndices      = &m_imgIdx;

    VkResult r = vkQueuePresentKHR(m_presQ, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || m_win.resized()) {
        m_win.clearResize();
        recreateSwapchain();
    }

    m_frame = (m_frame + 1) % FRAMES;
}

void Renderer::uploadMesh(const MeshData& mesh) {
    GpuMesh gm;
    gm.count = mesh.indices.size();

    auto upload = [&](const void* data, VkDeviceSize sz, VkBufferUsageFlags usage,
                      VkBuffer& buf, VkDeviceMemory& mem) {
        VkBuffer sb; VkDeviceMemory sm;
        mkBuf(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sm);
        void* p; vkMapMemory(m_dev, sm, 0, sz, 0, &p);
        memcpy(p, data, sz);
        vkUnmapMemory(m_dev, sm);
        mkBuf(sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT|usage,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf, mem);
        cpBuf(sb, buf, sz);
        vkDestroyBuffer(m_dev, sb, nullptr);
        vkFreeMemory(m_dev, sm, nullptr);
    };

    upload(mesh.vertices.data(), sizeof(Vertex)*mesh.vertices.size(),
           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gm.vb, gm.vm);
    upload(mesh.indices.data(), sizeof(uint32_t)*mesh.indices.size(),
           VK_BUFFER_USAGE_INDEX_BUFFER_BIT, gm.ib, gm.im);

    m_meshes.push_back(gm);
}

void Renderer::uploadTexture(const std::string& path) {
    int w, h, ch;
    stbi_uc* px = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!px) { std::cerr << "failed to load texture: " << path << "\n"; return; }

    VkDeviceSize sz = w * h * 4;
    VkBuffer sb; VkDeviceMemory sm;
    mkBuf(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sm);
    void* p; vkMapMemory(m_dev, sm, 0, sz, 0, &p);
    memcpy(p, px, sz);
    vkUnmapMemory(m_dev, sm);
    stbi_image_free(px);

    if (m_hasTex) {
        vkDeviceWaitIdle(m_dev);
        vkDestroySampler(m_dev, m_texS, nullptr);
        vkDestroyImageView(m_dev, m_texV, nullptr);
        vkDestroyImage(m_dev, m_texImg, nullptr);
        vkFreeMemory(m_dev, m_texMem, nullptr);
    }

    mkImg(w, h, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_texImg, m_texMem);
    transitionImg(m_texImg, VK_FORMAT_R8G8B8A8_SRGB,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    cpBufToImg(sb, m_texImg, w, h);
    transitionImg(m_texImg, VK_FORMAT_R8G8B8A8_SRGB,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(m_dev, sb, nullptr);
    vkFreeMemory(m_dev, sm, nullptr);

    m_texV = mkView(m_texImg, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo si{};
    si.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter        = VK_FILTER_LINEAR;
    si.minFilter        = VK_FILTER_LINEAR;
    si.addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = VK_TRUE;
    si.maxAnisotropy    = 16.f;
    si.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(m_dev, &si, nullptr, &m_texS);

    m_hasTex = true;
    vkDeviceWaitIdle(m_dev);
    refreshDescSets();
}

void Renderer::clearMeshes() {
    vkDeviceWaitIdle(m_dev);
    for (auto& m : m_meshes) {
        vkDestroyBuffer(m_dev, m.vb, nullptr); vkFreeMemory(m_dev, m.vm, nullptr);
        vkDestroyBuffer(m_dev, m.ib, nullptr); vkFreeMemory(m_dev, m.im, nullptr);
    }
    m_meshes.clear();
}

void Renderer::setCamera(glm::vec3 eye, glm::vec3 target, glm::vec3 up) {
    m_ubo.view    = glm::lookAt(eye, target, up);
    m_ubo.viewPos = glm::vec4(eye, 0);
}

void Renderer::setLight(glm::vec3 pos, glm::vec3 color, float specPow) {
    m_ubo.lightPos   = glm::vec4(pos, 0);
    m_ubo.lightColor = glm::vec4(color, specPow);
}

void Renderer::setUV(glm::vec2 offset, glm::vec2 scale) {
    m_ubo.uvOffset = offset;
    m_ubo.uvScale  = scale;
}

void Renderer::updateUBO(uint32_t frame, const glm::mat4& model) {
    m_ubo.model = model;
    m_ubo.proj  = glm::perspective(glm::radians(90.f), m_win.aspect(), 0.01f, 1000.f);
    m_ubo.proj[1][1] *= -1;
    memcpy(m_ubp[frame], &m_ubo, sizeof(m_ubo));
}

void Renderer::refreshDescSets() {
    VkImageView  iv = m_hasTex ? m_texV : m_wV;
    VkSampler    is = m_hasTex ? m_texS : m_wS;
    for (int i = 0; i < FRAMES; i++) {
        VkDescriptorBufferInfo bi{m_ub[i], 0, sizeof(UBO)};
        VkDescriptorImageInfo  ii{is, iv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::array<VkWriteDescriptorSet, 2> w{};
        w[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet          = m_ds[i];
        w[0].dstBinding      = 0;
        w[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].descriptorCount = 1;
        w[0].pBufferInfo     = &bi;

        w[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet          = m_ds[i];
        w[1].dstBinding      = 1;
        w[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].descriptorCount = 1;
        w[1].pImageInfo      = &ii;

        vkUpdateDescriptorSets(m_dev, w.size(), w.data(), 0, nullptr);
    }
}

void Renderer::destroySwapchain() {
    vkDestroyImageView(m_dev, m_dv, nullptr);
    vkDestroyImage(m_dev, m_di, nullptr);
    vkFreeMemory(m_dev, m_dm, nullptr);
    for (auto fb : m_fbs) vkDestroyFramebuffer(m_dev, fb, nullptr);
    for (auto iv : m_scViews) vkDestroyImageView(m_dev, iv, nullptr);
    vkDestroySwapchainKHR(m_dev, m_sc, nullptr);
    m_fbs.clear(); m_scViews.clear(); m_scImgs.clear();
}

void Renderer::recreateSwapchain() {
    int w = 0, h = 0;
    while (!w || !h) { glfwGetFramebufferSize(m_win.handle(), &w, &h); glfwWaitEvents(); }
    vkDeviceWaitIdle(m_dev);
    destroySwapchain();
    initSwapchain(); initImageViews(); initDepth(); initFramebuffers();
}

Renderer::QFI Renderer::findQFI(VkPhysicalDevice dev) {
    QFI qi;
    uint32_t n; vkGetPhysicalDeviceQueueFamilyProperties(dev, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fams(n);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &n, fams.data());
    for (uint32_t i = 0; i < n; i++) {
        if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) qi.gfx = i;
        VkBool32 ps = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surf, &ps);
        if (ps) qi.present = i;
        if (qi.ok()) break;
    }
    return qi;
}

Renderer::SCSupport Renderer::querySC(VkPhysicalDevice dev) {
    SCSupport s;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, m_surf, &s.caps);
    uint32_t n;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_surf, &n, nullptr);
    s.fmts.resize(n); vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_surf, &n, s.fmts.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, m_surf, &n, nullptr);
    s.modes.resize(n); vkGetPhysicalDeviceSurfacePresentModesKHR(dev, m_surf, &n, s.modes.data());
    return s;
}

bool Renderer::deviceOk(VkPhysicalDevice dev) {
    if (!findQFI(dev).ok()) return false;
    uint32_t n; vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, exts.data());
    for (auto& req : kDevExts) {
        bool found = false;
        for (auto& e : exts) if (!strcmp(e.extensionName, req)) { found = true; break; }
        if (!found) return false;
    }
    auto sc = querySC(dev);
    if (sc.fmts.empty() || sc.modes.empty()) return false;
    VkPhysicalDeviceFeatures f; vkGetPhysicalDeviceFeatures(dev, &f);
    return f.samplerAnisotropy;
}

VkSurfaceFormatKHR Renderer::pickFmt(const std::vector<VkSurfaceFormatKHR>& fmts) {
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return fmts[0];
}

VkPresentModeKHR Renderer::pickMode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Renderer::pickExtent(const VkSurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    int w, h; glfwGetFramebufferSize(m_win.handle(), &w, &h);
    return {
        std::clamp((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height),
    };
}

VkFormat Renderer::depthFmt() {
    return findFmt({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                   VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkFormat Renderer::findFmt(const std::vector<VkFormat>& cands, VkImageTiling tiling, VkFormatFeatureFlags feat) {
    for (auto f : cands) {
        VkFormatProperties p; vkGetPhysicalDeviceFormatProperties(m_gpu, f, &p);
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (p.optimalTilingFeatures & feat) == feat) return f;
    }
    throw std::runtime_error("no supported format");
}

uint32_t Renderer::memType(uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(m_gpu, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((filter & (1<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    throw std::runtime_error("no suitable memory type");
}

VkShaderModule Renderer::makeShader(const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    if (vkCreateShaderModule(m_dev, &ci, nullptr, &m) != VK_SUCCESS)
        throw std::runtime_error("vkCreateShaderModule failed");
    return m;
}

std::vector<char> Renderer::readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate|std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);
    size_t sz = f.tellg();
    std::vector<char> buf(sz);
    f.seekg(0); f.read(buf.data(), sz);
    return buf;
}

void Renderer::mkBuf(VkDeviceSize sz, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                     VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = sz; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_dev, &ci, nullptr, &buf);
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(m_dev, buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size; ai.memoryTypeIndex = memType(req.memoryTypeBits, props);
    vkAllocateMemory(m_dev, &ai, nullptr, &mem);
    vkBindBufferMemory(m_dev, buf, mem, 0);
}

void Renderer::cpBuf(VkBuffer src, VkBuffer dst, VkDeviceSize sz) {
    auto cmd = beginOnce();
    VkBufferCopy c{0, 0, sz};
    vkCmdCopyBuffer(cmd, src, dst, 1, &c);
    endOnce(cmd);
}

void Renderer::mkImg(uint32_t w, uint32_t h, VkFormat fmt, VkImageTiling tiling,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags props,
                     VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {w,h,1};
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = tiling; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(m_dev, &ci, nullptr, &img);
    VkMemoryRequirements req; vkGetImageMemoryRequirements(m_dev, img, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size; ai.memoryTypeIndex = memType(req.memoryTypeBits, props);
    vkAllocateMemory(m_dev, &ai, nullptr, &mem);
    vkBindImageMemory(m_dev, img, mem, 0);
}

VkImageView Renderer::mkView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ci.image = img; ci.viewType = VK_IMAGE_VIEW_TYPE_2D; ci.format = fmt;
    ci.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView v; vkCreateImageView(m_dev, &ci, nullptr, &v); return v;
}

void Renderer::transitionImg(VkImage img, VkFormat, VkImageLayout from, VkImageLayout to) {
    auto cmd = beginOnce();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkPipelineStageFlags src, dst;
    if (from == VK_IMAGE_LAYOUT_UNDEFINED) {
        b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
    endOnce(cmd);
}

void Renderer::cpBufToImg(VkBuffer buf, VkImage img, uint32_t w, uint32_t h) {
    auto cmd = beginOnce();
    VkBufferImageCopy r{};
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    endOnce(cmd);
}

VkCommandBuffer Renderer::beginOnce() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = m_cp; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(m_dev, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void Renderer::endOnce(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_gfxQ, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_gfxQ);
    vkFreeCommandBuffers(m_dev, m_cp, 1, &cmd);
}
