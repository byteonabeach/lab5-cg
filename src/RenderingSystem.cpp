#include "RenderingSystem.h"

#include <stdexcept>
#include <array>
#include <iostream>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────

void RenderingSystem::init(Engine& engine) {
    auto ext = engine.getSwapExtent();
    gbuffer.init(engine, ext.width, ext.height);

    createGeomPipeline_   (engine);
    createLightRenderPass_(engine);
    createLightPipeline_  (engine);
    createFramebuffers_   (engine);
    createDescriptors_    (engine);
    updateLightDescSets_  (engine);
}

void RenderingSystem::cleanup(Engine& engine) {
    VkDevice dev = engine.getDevice();
    vkDeviceWaitIdle(dev);

    cleanupFramebuffers_(dev);
    vkDestroyRenderPass(dev, lightRenderPass, nullptr);

    // Light pipeline
    vkDestroyPipeline            (dev, lightPipeline,       nullptr);
    vkDestroyPipelineLayout      (dev, lightPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout (dev, lightDescLayout,     nullptr);
    vkDestroyDescriptorPool      (dev, lightDescPool,       nullptr);

    // Geom pipeline
    vkDestroyPipeline            (dev, geomPipeline,        nullptr);
    vkDestroyPipelineLayout      (dev, geomPipelineLayout,  nullptr);
    vkDestroyDescriptorSetLayout (dev, geomUBOLayout,       nullptr);
    vkDestroyDescriptorPool      (dev, geomDescPool,        nullptr);

    int frames = Engine::MAX_FRAMES;
    for (int i = 0; i < frames; ++i) {
        vkDestroyBuffer(dev, geomUBOBufs[i],  nullptr);
        vkFreeMemory   (dev, geomUBOMems[i],  nullptr);
        vkDestroyBuffer(dev, lightUBOBufs[i], nullptr);
        vkFreeMemory   (dev, lightUBOMems[i], nullptr);
    }

    gbuffer.cleanup(dev);
}

void RenderingSystem::onResize(Engine& engine) {
    VkDevice dev = engine.getDevice();
    auto ext = engine.getSwapExtent();

    cleanupFramebuffers_(dev);
    gbuffer.recreate(engine, ext.width, ext.height);
    createFramebuffers_(engine);
    // Re-write descriptor sets since GBuffer image views changed
    updateLightDescSets_(engine);
}

void RenderingSystem::recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, int frameIndex,
                                   const Camera& camera,
                                   const std::vector<SceneObject>& objects,
                                   Engine& engine) {
    auto ext = engine.getSwapExtent();

    // ── Update geometry UBO ──────────────────────────────────────────────
    GeomUBO gubo{};
    gubo.view = camera.view();
    gubo.proj = camera.projection((float)ext.width / (float)ext.height);
    memcpy(geomUBOMapped[frameIndex], &gubo, sizeof(GeomUBO));

    // ── Update lights UBO ────────────────────────────────────────────────
    LightsUBO lubo{};
    lubo.viewPos     = glm::vec4(camera.position, 1.0f);
    lubo.ambientColor= glm::vec4(0.08f, 0.08f, 0.10f, 1.0f);
    int cnt = std::min((int)pendingLights.size(), MAX_LIGHTS);
    lubo.countPad.x = cnt;
    for (int i = 0; i < cnt; ++i) lubo.lights[i] = pendingLights[i];
    memcpy(lightUBOMapped[frameIndex], &lubo, sizeof(LightsUBO));

    // ─────────────────────────────────────────────────────────────────────
    // PASS 1 — Geometry pass → GBuffer
    // ─────────────────────────────────────────────────────────────────────
    {
        std::array<VkClearValue, 4> clears{};
        clears[0].color = {0,0,0,0}; // position
        clears[1].color = {0,0,0,0}; // normal
        clears[2].color = {0,0,0,0}; // albedo
        clears[3].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpi.renderPass        = gbuffer.getRenderPass();
        rpi.framebuffer       = gbuffer.getFramebuffer();
        rpi.renderArea.extent = ext;
        rpi.clearValueCount   = (uint32_t)clears.size();
        rpi.pClearValues      = clears.data();

        vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipeline);

        VkViewport vp{0,0,(float)ext.width,(float)ext.height, 0.0f, 1.0f};
        VkRect2D   sc{{0,0}, ext};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor (cmd, 0, 1, &sc);

        // Set 0: geometry UBO
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            geomPipelineLayout, 0, 1, &geomDescSets[frameIndex], 0, nullptr);

        for (const auto& obj : objects) {
            for (const auto& sm : obj.submeshes) {
                if (!sm.mesh.valid() || !sm.texture.valid()) continue;

                // Push model matrix
                vkCmdPushConstants(cmd, geomPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
                    &obj.transform);

                // Set 1: material texture
                VkDescriptorSet matSet = engine.getTextureSet(sm.texture);
                if (matSet != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        geomPipelineLayout, 1, 1, &matSet, 0, nullptr);

                // Bind mesh buffers and draw
                engine.bindAndDrawMesh_(cmd, sm.mesh);
            }
        }

        vkCmdEndRenderPass(cmd);
    }

    // ─────────────────────────────────────────────────────────────────────
    // PASS 2 — Lighting pass → swapchain
    // ─────────────────────────────────────────────────────────────────────
    {
        std::array<VkClearValue, 1> clears{};
        clears[0].color = {0.02f, 0.02f, 0.05f, 1.0f};

        VkRenderPassBeginInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpi.renderPass        = lightRenderPass;
        rpi.framebuffer       = lightFramebuffers[imageIndex];
        rpi.renderArea.extent = ext;
        rpi.clearValueCount   = (uint32_t)clears.size();
        rpi.pClearValues      = clears.data();

        vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipeline);

        VkViewport vp{0,0,(float)ext.width,(float)ext.height, 0.0f, 1.0f};
        VkRect2D   sc{{0,0}, ext};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor (cmd, 0, 1, &sc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            lightPipelineLayout, 0, 1, &lightDescSets[frameIndex], 0, nullptr);

        // Draw fullscreen triangle — no vertex buffer needed
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Geometry pipeline
// ─────────────────────────────────────────────────────────────────────────────

void RenderingSystem::createGeomPipeline_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    // Descriptor set layout: set 0 = GeomUBO
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding        = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount= 1;
    uboBinding.stageFlags     = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1;
    lci.pBindings    = &uboBinding;
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &geomUBOLayout);

    // Pipeline layout: set 0 = geomUBOLayout, set 1 = materialLayout (from Engine)
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)};
    VkDescriptorSetLayout setLayouts[] = {geomUBOLayout, engine.getMaterialLayout()};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = 2;
    plci.pSetLayouts            = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &geomPipelineLayout);

    // Shaders
    auto vsStage = loadShader_(engine, "shaders/gbuffer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fsStage = loadShader_(engine, "shaders/gbuffer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vsStage, fsStage};

    auto bindDesc = Vertex::getBindingDesc();
    auto attrDesc = Vertex::getAttrDescs();
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bindDesc;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrDesc.size();
    vi.pVertexAttributeDescriptions    = attrDesc.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = vpState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode    = VK_CULL_MODE_NONE;
    rast.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    // 3 color blend attachments (one per GBuffer target)
    std::array<VkPipelineColorBlendAttachmentState, GBuffer::NUM_ATTACHMENTS> cbAtts{};
    for (auto& att : cbAtts) {
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        att.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = (uint32_t)cbAtts.size();
    cb.pAttachments    = cbAtts.data();

    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dyn;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount          = 2;
    gci.pStages             = stages;
    gci.pVertexInputState   = &vi;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState      = &vpState;
    gci.pRasterizationState = &rast;
    gci.pMultisampleState   = &ms;
    gci.pDepthStencilState  = &ds;
    gci.pColorBlendState    = &cb;
    gci.pDynamicState       = &dynState;
    gci.layout              = geomPipelineLayout;
    gci.renderPass          = gbuffer.getRenderPass();

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &geomPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create geometry pipeline");

    vkDestroyShaderModule(dev, vsStage.module, nullptr);
    vkDestroyShaderModule(dev, fsStage.module, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lighting render pass (writes to swapchain)
// ─────────────────────────────────────────────────────────────────────────────

void RenderingSystem::createLightRenderPass_(Engine& engine) {
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = engine.getSwapFormat();
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &colorAtt;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = (uint32_t)deps.size();
    rpci.pDependencies   = deps.data();

    if (vkCreateRenderPass(engine.getDevice(), &rpci, nullptr, &lightRenderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create lighting render pass");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lighting pipeline
// ─────────────────────────────────────────────────────────────────────────────

void RenderingSystem::createLightPipeline_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    // set 0: bindings 0,1,2 = GBuffer samplers; binding 3 = LightsUBO
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (int i = 0; i < 3; ++i) {
        bindings[i].binding        = (uint32_t)i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount= 1;
        bindings[i].stageFlags     = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[3].binding        = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount= 1;
    bindings[3].stageFlags     = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = (uint32_t)bindings.size();
    lci.pBindings    = bindings.data();
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &lightDescLayout);

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &lightDescLayout;
    vkCreatePipelineLayout(dev, &plci, nullptr, &lightPipelineLayout);

    auto vsStage = loadShader_(engine, "shaders/lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fsStage = loadShader_(engine, "shaders/lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vsStage, fsStage};

    // No vertex input — fullscreen triangle generated in vertex shader
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = vpState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode    = VK_CULL_MODE_NONE;
    rast.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable    = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount          = 2;
    gci.pStages             = stages;
    gci.pVertexInputState   = &vi;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState      = &vpState;
    gci.pRasterizationState = &rast;
    gci.pMultisampleState   = &ms;
    gci.pColorBlendState    = &cb;
    gci.pDynamicState       = &dynState;
    gci.layout              = lightPipelineLayout;
    gci.renderPass          = lightRenderPass;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &lightPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create lighting pipeline");

    vkDestroyShaderModule(dev, vsStage.module, nullptr);
    vkDestroyShaderModule(dev, fsStage.module, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Framebuffers (lighting pass — one per ` image)
// ─────────────────────────────────────────────────────────────────────────────

void RenderingSystem::createFramebuffers_(Engine& engine) {
    const auto& views = engine.getSwapImageViews();
    auto ext = engine.getSwapExtent();
    lightFramebuffers.resize(views.size());

    for (size_t i = 0; i < views.size(); ++i) {
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass      = lightRenderPass;
        fci.attachmentCount = 1;
        fci.pAttachments    = &views[i];
        fci.width           = ext.width;
        fci.height          = ext.height;
        fci.layers          = 1;
        vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &lightFramebuffers[i]);
    }
}

void RenderingSystem::cleanupFramebuffers_(VkDevice device) {
    for (auto fb : lightFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    lightFramebuffers.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Descriptors
// ─────────────────────────────────────────────────────────────────────────────

void RenderingSystem::createDescriptors_(Engine& engine) {
    VkDevice dev  = engine.getDevice();
    int frames    = Engine::MAX_FRAMES;

    // ── Geometry descriptor pool + sets ──────────────────────────────────
    {
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)frames};
        VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.poolSizeCount = 1; ci.pPoolSizes = &ps; ci.maxSets = (uint32_t)frames;
        vkCreateDescriptorPool(dev, &ci, nullptr, &geomDescPool);

        std::vector<VkDescriptorSetLayout> layouts(frames, geomUBOLayout);
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = geomDescPool;
        ai.descriptorSetCount = (uint32_t)frames;
        ai.pSetLayouts    = layouts.data();
        geomDescSets.resize(frames);
        vkAllocateDescriptorSets(dev, &ai, geomDescSets.data());

        // Create UBOs
        geomUBOBufs.resize(frames); geomUBOMems.resize(frames); geomUBOMapped.resize(frames);
        for (int i = 0; i < frames; ++i) {
            engine.createBuffer(sizeof(GeomUBO),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                geomUBOBufs[i], geomUBOMems[i]);
            vkMapMemory(dev, geomUBOMems[i], 0, sizeof(GeomUBO), 0, &geomUBOMapped[i]);

            VkDescriptorBufferInfo bi{geomUBOBufs[i], 0, sizeof(GeomUBO)};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet          = geomDescSets[i];
            w.dstBinding      = 0;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.descriptorCount = 1;
            w.pBufferInfo     = &bi;
            vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
        }
    }

    // ── Lighting descriptor pool + sets ──────────────────────────────────
    {
        std::array<VkDescriptorPoolSize, 2> ps{};
        ps[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)(3 * frames)};
        ps[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          (uint32_t)frames};
        VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.poolSizeCount = (uint32_t)ps.size(); ci.pPoolSizes = ps.data();
        ci.maxSets = (uint32_t)frames;
        vkCreateDescriptorPool(dev, &ci, nullptr, &lightDescPool);

        std::vector<VkDescriptorSetLayout> layouts(frames, lightDescLayout);
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = lightDescPool;
        ai.descriptorSetCount = (uint32_t)frames;
        ai.pSetLayouts    = layouts.data();
        lightDescSets.resize(frames);
        vkAllocateDescriptorSets(dev, &ai, lightDescSets.data());

        // Create LightsUBOs
        lightUBOBufs.resize(frames); lightUBOMems.resize(frames); lightUBOMapped.resize(frames);
        for (int i = 0; i < frames; ++i) {
            engine.createBuffer(sizeof(LightsUBO),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                lightUBOBufs[i], lightUBOMems[i]);
            vkMapMemory(dev, lightUBOMems[i], 0, sizeof(LightsUBO), 0, &lightUBOMapped[i]);
        }
    }
}

void RenderingSystem::updateLightDescSets_(Engine& engine) {
    VkDevice dev = engine.getDevice();
    VkSampler gbSampler = gbuffer.getSampler();

    VkImageView gbViews[3] = {
        gbuffer.getPositionView(),
        gbuffer.getNormalView(),
        gbuffer.getAlbedoView()
    };

    for (int i = 0; i < Engine::MAX_FRAMES; ++i) {
        std::array<VkWriteDescriptorSet, 4> writes{};

        // Bindings 0,1,2: GBuffer samplers
        std::array<VkDescriptorImageInfo, 3> imgInfos{};
        for (int b = 0; b < 3; ++b) {
            imgInfos[b] = {gbSampler, gbViews[b], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = lightDescSets[i];
            writes[b].dstBinding      = (uint32_t)b;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1;
            writes[b].pImageInfo      = &imgInfos[b];
        }

        // Binding 3: LightsUBO
        VkDescriptorBufferInfo uboInfo{lightUBOBufs[i], 0, sizeof(LightsUBO)};
        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = lightDescSets[i];
        writes[3].dstBinding      = 3;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo     = &uboInfo;

        vkUpdateDescriptorSets(dev, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shader loading
// ─────────────────────────────────────────────────────────────────────────────

VkPipelineShaderStageCreateInfo RenderingSystem::loadShader_(
        Engine& engine, const std::string& path, VkShaderStageFlagBits stage) {
    auto code = engine.readFile(path);
    VkShaderModule sm = engine.createShaderModule(code);
    VkPipelineShaderStageCreateInfo si{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    si.stage  = stage;
    si.module = sm;
    si.pName  = "main";
    return si;
}
