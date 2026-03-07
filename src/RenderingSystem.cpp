#include "RenderingSystem.h"
#include <array>
#include <cstring>

struct GeomPC {
    glm::mat4 model;
    glm::vec4 color;
    int isUnlit;
    int pad[3];
};

struct ShadowPC {
    glm::mat4 model;
    glm::mat4 lightSpace;
};

void RenderingSystem::init(Engine& engine) {
    auto ext = engine.getSwapExtent();
    gbuffer.init(engine, ext.width, ext.height);
    createShadowResources_(engine);
    createShadowPipeline_(engine);
    createGeomPipeline_(engine);
    createLightRenderPass_(engine);
    createLightPipeline_(engine);
    createFramebuffers_(engine);
    createDescriptors_(engine);
    updateLightDescSets_(engine);
}

void RenderingSystem::cleanup(Engine& engine) {
    VkDevice dev = engine.getDevice();
    vkDeviceWaitIdle(dev);
    cleanupFramebuffers_(dev);
    vkDestroyRenderPass(dev, lightRenderPass, nullptr);
    vkDestroyPipeline(dev, lightPipeline, nullptr);
    vkDestroyPipelineLayout(dev, lightPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, lightDescLayout, nullptr);
    vkDestroyDescriptorPool(dev, lightDescPool, nullptr);
    vkDestroyPipeline(dev, geomPipeline, nullptr);
    vkDestroyPipelineLayout(dev, geomPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, geomUBOLayout, nullptr);
    vkDestroyDescriptorPool(dev, geomDescPool, nullptr);
    vkDestroyPipeline(dev, shadowPipeline, nullptr);
    vkDestroyPipelineLayout(dev, shadowPipelineLayout, nullptr);
    vkDestroyRenderPass(dev, shadowRenderPass, nullptr);
    vkDestroyImageView(dev, shadowArrayView, nullptr);
    for(auto v : shadowLayerViews) vkDestroyImageView(dev, v, nullptr);
    for(auto f : shadowFramebuffers) vkDestroyFramebuffer(dev, f, nullptr);
    vkDestroyImage(dev, shadowImage, nullptr);
    vkFreeMemory(dev, shadowMemory, nullptr);
    vkDestroySampler(dev, shadowSampler, nullptr);
    for (int i = 0; i < Engine::MAX_FRAMES; ++i) {
        vkDestroyBuffer(dev, geomUBOBufs[i], nullptr); vkFreeMemory(dev, geomUBOMems[i], nullptr);
        vkDestroyBuffer(dev, lightUBOBufs[i], nullptr); vkFreeMemory(dev, lightUBOMems[i], nullptr);
    }
    gbuffer.cleanup(dev);
}

void RenderingSystem::onResize(Engine& engine) {
    VkDevice dev = engine.getDevice();
    auto ext = engine.getSwapExtent();
    cleanupFramebuffers_(dev);
    gbuffer.recreate(engine, ext.width, ext.height);
    createFramebuffers_(engine);
    updateLightDescSets_(engine);
}

void RenderingSystem::recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, int frameIndex, const Camera& camera, const std::vector<SceneObject>& objects, Engine& engine) {
    auto ext = engine.getSwapExtent();

    GeomUBO gubo{};
    gubo.view = camera.view();
    gubo.proj = camera.projection((float)ext.width / (float)ext.height);
    memcpy(geomUBOMapped[frameIndex], &gubo, sizeof(GeomUBO));

    LightsUBO lubo{};
    lubo.viewPos = glm::vec4(camera.position, 1.0f);
    lubo.ambientColor = glm::vec4(0.08f, 0.08f, 0.10f, 1.0f);
    // Считаем обратную видово-проекционную матрицу
    lubo.invViewProj = glm::inverse(gubo.proj * gubo.view);

    int cnt = std::min((int)pendingLights.size(), MAX_LIGHTS);
    lubo.countPad.x = cnt;
    for (int i = 0; i < cnt; ++i) lubo.lights[i] = pendingLights[i];
    memcpy(lightUBOMapped[frameIndex], &lubo, sizeof(LightsUBO));

    for (int i = 0; i < cnt; ++i) {
        if (pendingLights[i].params2.x > 0.5f) {
            int layer = (int)pendingLights[i].params2.y;
            VkRenderPassBeginInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rpi.renderPass = shadowRenderPass;
            rpi.framebuffer = shadowFramebuffers[layer];
            rpi.renderArea.extent = {2048, 2048};
            VkClearValue cv; cv.depthStencil = {1.0f, 0};
            rpi.clearValueCount = 1; rpi.pClearValues = &cv;
            vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
            VkViewport vp{0, 0, 2048.0f, 2048.0f, 0.0f, 1.0f}; VkRect2D sc{{0, 0}, {2048, 2048}};
            vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
            for (const auto& obj : objects) {
                if (obj.unlit) continue;
                for (const auto& sm : obj.submeshes) {
                    if (!sm.mesh.valid()) continue;
                    ShadowPC spc{};
                    spc.model = obj.transform; spc.lightSpace = pendingLights[i].lightSpace;
                    vkCmdPushConstants(cmd, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPC), &spc);
                    engine.bindAndDrawMesh_(cmd, sm.mesh);
                }
            }
            vkCmdEndRenderPass(cmd);
        }
    }

    // ТЕПЕРЬ ОЧИЩАЕМ ТОЛЬКО 3 ЭЛЕМЕНТА (2 Цвета + 1 Глубина)
    std::array<VkClearValue, 3> clears{};
    clears[0].color = {0,0,0,0};
    clears[1].color = {0,0,0,0};
    clears[2].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpi.renderPass = gbuffer.getRenderPass(); rpi.framebuffer = gbuffer.getFramebuffer();
    rpi.renderArea.extent = ext; rpi.clearValueCount = (uint32_t)clears.size(); rpi.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipeline);
    VkViewport vp{0,0,(float)ext.width,(float)ext.height, 0.0f, 1.0f}; VkRect2D sc{{0,0}, ext};
    vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 0, 1, &geomDescSets[frameIndex], 0, nullptr);
    for (const auto& obj : objects) {
        for (const auto& sm : obj.submeshes) {
            if (!sm.mesh.valid()) continue;
            GeomPC gpc{};
            gpc.model = obj.transform; gpc.color = obj.unlitColor; gpc.isUnlit = obj.unlit ? 1 : 0;
            vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeomPC), &gpc);
            if (!obj.unlit && sm.texture.valid()) {
                VkDescriptorSet matSet = engine.getTextureSet(sm.texture);
                if (matSet != VK_NULL_HANDLE) vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 1, 1, &matSet, 0, nullptr);
            }
            engine.bindAndDrawMesh_(cmd, sm.mesh);
        }
    }
    vkCmdEndRenderPass(cmd);

    std::array<VkClearValue, 1> lightClears{};
    lightClears[0].color = {0.02f, 0.02f, 0.05f, 1.0f};
    VkRenderPassBeginInfo lrpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    lrpi.renderPass = lightRenderPass; lrpi.framebuffer = lightFramebuffers[imageIndex];
    lrpi.renderArea.extent = ext; lrpi.clearValueCount = 1; lrpi.pClearValues = lightClears.data();
    vkCmdBeginRenderPass(cmd, &lrpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipeline);
    vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipelineLayout, 0, 1, &lightDescSets[frameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void RenderingSystem::createShadowResources_(Engine& engine) {
    VkDevice dev = engine.getDevice();
    VkFormat depthFmt = engine.findDepthFormat();
    engine.createImage(2048, 2048, 4, depthFmt, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, shadowImage, shadowMemory);
    engine.transitionLayout(shadowImage, 4, depthFmt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    shadowArrayView = engine.createImageView(shadowImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 4, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    shadowLayerViews.resize(4);
    for(int i=0; i<4; ++i) shadowLayerViews[i] = engine.createImageView(shadowImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT, i, 1, VK_IMAGE_VIEW_TYPE_2D);
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(dev, &si, nullptr, &shadowSampler);
    VkAttachmentDescription att{};
    att.format = depthFmt; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.pDepthStencilAttachment = &ref;
    VkSubpassDependency dep1{}; dep1.srcSubpass = VK_SUBPASS_EXTERNAL; dep1.dstSubpass = 0; dep1.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; dep1.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; dep1.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; dep1.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkSubpassDependency dep2{}; dep2.srcSubpass = 0; dep2.dstSubpass = VK_SUBPASS_EXTERNAL; dep2.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; dep2.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; dep2.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; dep2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkSubpassDependency deps[] = {dep1, dep2};
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1; rpci.pAttachments = &att; rpci.subpassCount = 1; rpci.pSubpasses = &subpass; rpci.dependencyCount = 2; rpci.pDependencies = deps;
    vkCreateRenderPass(dev, &rpci, nullptr, &shadowRenderPass);
    shadowFramebuffers.resize(4);
    for(int i=0; i<4; ++i) {
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = shadowRenderPass; fci.attachmentCount = 1; fci.pAttachments = &shadowLayerViews[i]; fci.width = 2048; fci.height = 2048; fci.layers = 1;
        vkCreateFramebuffer(dev, &fci, nullptr, &shadowFramebuffers[i]);
    }
}

void RenderingSystem::createShadowPipeline_(Engine& engine) {
    VkDevice dev = engine.getDevice();
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPC)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &shadowPipelineLayout);
    auto vsStage = loadShader_(engine, "shaders/shadows.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto bindDesc = Vertex::getBindingDesc();
    auto attrDescs = Vertex::getAttrDescs();
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bindDesc;
    vi.vertexAttributeDescriptionCount = 1; vi.pVertexAttributeDescriptions = &attrDescs[0];
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = 1; vpState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rast{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rast.polygonMode = VK_POLYGON_MODE_FILL; rast.cullMode = VK_CULL_MODE_NONE; rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rast.lineWidth = 1.0f; rast.depthBiasEnable = VK_TRUE; rast.depthBiasConstantFactor = 1.25f; rast.depthBiasSlopeFactor = 1.75f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount = 1; gci.pStages = &vsStage; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vpState; gci.pRasterizationState = &rast; gci.pMultisampleState = &ms; gci.pDepthStencilState = &ds; gci.pDynamicState = &dynState; gci.layout = shadowPipelineLayout; gci.renderPass = shadowRenderPass;
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &shadowPipeline);
    vkDestroyShaderModule(dev, vsStage.module, nullptr);
}

void RenderingSystem::createGeomPipeline_(Engine& engine) {
    VkDevice dev = engine.getDevice();
    VkDescriptorSetLayoutBinding uboBinding{}; uboBinding.binding = 0; uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; uboBinding.descriptorCount = 1; uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1; lci.pBindings = &uboBinding;
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &geomUBOLayout);
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeomPC)};
    VkDescriptorSetLayout setLayouts[] = {geomUBOLayout, engine.getMaterialLayout()};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 2; plci.pSetLayouts = setLayouts; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &geomPipelineLayout);
    auto vsStage = loadShader_(engine, "shaders/gbuffer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fsStage = loadShader_(engine, "shaders/gbuffer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vsStage, fsStage};
    auto bindDesc = Vertex::getBindingDesc(); auto attrDesc = Vertex::getAttrDescs();
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bindDesc; vi.vertexAttributeDescriptionCount = (uint32_t)attrDesc.size(); vi.pVertexAttributeDescriptions = attrDesc.data();
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = vpState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rast{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rast.polygonMode = VK_POLYGON_MODE_FILL; rast.cullMode = VK_CULL_MODE_NONE; rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rast.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;
    std::array<VkPipelineColorBlendAttachmentState, GBuffer::NUM_ATTACHMENTS> cbAtts{};
    for (auto& att : cbAtts) { att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; att.blendEnable = VK_FALSE; }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = (uint32_t)cbAtts.size(); cb.pAttachments = cbAtts.data();
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount = 2; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vpState; gci.pRasterizationState = &rast; gci.pMultisampleState = &ms; gci.pDepthStencilState = &ds; gci.pColorBlendState = &cb; gci.pDynamicState = &dynState; gci.layout = geomPipelineLayout; gci.renderPass = gbuffer.getRenderPass();
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &geomPipeline);
    vkDestroyShaderModule(dev, vsStage.module, nullptr); vkDestroyShaderModule(dev, fsStage.module, nullptr);
}

void RenderingSystem::createLightRenderPass_(Engine& engine) {
    VkAttachmentDescription colorAtt{};
    colorAtt.format = engine.getSwapFormat(); colorAtt.samples = VK_SAMPLE_COUNT_1_BIT; colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE; colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1; rpci.pAttachments = &colorAtt; rpci.subpassCount = 1; rpci.pSubpasses = &subpass; rpci.dependencyCount = (uint32_t)deps.size(); rpci.pDependencies = deps.data();
    vkCreateRenderPass(engine.getDevice(), &rpci, nullptr, &lightRenderPass);
}

void RenderingSystem::createLightPipeline_(Engine& engine) {
    VkDevice dev = engine.getDevice();
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (int i = 0; i < 3; ++i) { bindings[i].binding = i; bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; }
    bindings[3].binding = 3; bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; bindings[3].descriptorCount = 1; bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[4].binding = 4; bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; bindings[4].descriptorCount = 1; bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = (uint32_t)bindings.size(); lci.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &lightDescLayout);
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1; plci.pSetLayouts = &lightDescLayout;
    vkCreatePipelineLayout(dev, &plci, nullptr, &lightPipelineLayout);
    auto vsStage = loadShader_(engine, "shaders/lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fsStage = loadShader_(engine, "shaders/lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vsStage, fsStage};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = vpState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rast{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rast.polygonMode = VK_POLYGON_MODE_FILL; rast.cullMode = VK_CULL_MODE_NONE; rast.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; cba.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount = 2; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vpState; gci.pRasterizationState = &rast; gci.pMultisampleState = &ms; gci.pColorBlendState = &cb; gci.pDynamicState = &dynState; gci.layout = lightPipelineLayout; gci.renderPass = lightRenderPass;
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &lightPipeline);
    vkDestroyShaderModule(dev, vsStage.module, nullptr); vkDestroyShaderModule(dev, fsStage.module, nullptr);
}

void RenderingSystem::createFramebuffers_(Engine& engine) {
    const auto& views = engine.getSwapImageViews();
    auto ext = engine.getSwapExtent();
    lightFramebuffers.resize(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = lightRenderPass; fci.attachmentCount = 1; fci.pAttachments = &views[i]; fci.width = ext.width; fci.height = ext.height; fci.layers = 1;
        vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &lightFramebuffers[i]);
    }
}

void RenderingSystem::cleanupFramebuffers_(VkDevice device) {
    for (auto fb : lightFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    lightFramebuffers.clear();
}

void RenderingSystem::createDescriptors_(Engine& engine) {
    VkDevice dev = engine.getDevice();
    int frames = Engine::MAX_FRAMES;
    {
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)frames};
        VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.poolSizeCount = 1; ci.pPoolSizes = &ps; ci.maxSets = (uint32_t)frames;
        vkCreateDescriptorPool(dev, &ci, nullptr, &geomDescPool);
        std::vector<VkDescriptorSetLayout> layouts(frames, geomUBOLayout);
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = geomDescPool; ai.descriptorSetCount = (uint32_t)frames; ai.pSetLayouts = layouts.data();
        geomDescSets.resize(frames); vkAllocateDescriptorSets(dev, &ai, geomDescSets.data());
        geomUBOBufs.resize(frames); geomUBOMems.resize(frames); geomUBOMapped.resize(frames);
        for (int i = 0; i < frames; ++i) {
            engine.createBuffer(sizeof(GeomUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, geomUBOBufs[i], geomUBOMems[i]);
            vkMapMemory(dev, geomUBOMems[i], 0, sizeof(GeomUBO), 0, &geomUBOMapped[i]);
            VkDescriptorBufferInfo bi{geomUBOBufs[i], 0, sizeof(GeomUBO)};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = geomDescSets[i]; w.dstBinding = 0; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.descriptorCount = 1; w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
        }
    }
    {
        std::array<VkDescriptorPoolSize, 2> ps{};
        ps[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)(4 * frames)};
        ps[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)frames};
        VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.poolSizeCount = (uint32_t)ps.size(); ci.pPoolSizes = ps.data(); ci.maxSets = (uint32_t)frames;
        vkCreateDescriptorPool(dev, &ci, nullptr, &lightDescPool);
        std::vector<VkDescriptorSetLayout> layouts(frames, lightDescLayout);
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = lightDescPool; ai.descriptorSetCount = (uint32_t)frames; ai.pSetLayouts = layouts.data();
        lightDescSets.resize(frames); vkAllocateDescriptorSets(dev, &ai, lightDescSets.data());
        lightUBOBufs.resize(frames); lightUBOMems.resize(frames); lightUBOMapped.resize(frames);
        for (int i = 0; i < frames; ++i) {
            engine.createBuffer(sizeof(LightsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, lightUBOBufs[i], lightUBOMems[i]);
            vkMapMemory(dev, lightUBOMems[i], 0, sizeof(LightsUBO), 0, &lightUBOMapped[i]);
        }
    }
}

void RenderingSystem::updateLightDescSets_(Engine& engine) {
    VkDevice dev = engine.getDevice();
    VkSampler gbSampler = gbuffer.getSampler();

    VkImageView gbViews[3] = {gbuffer.getNormalView(), gbuffer.getAlbedoView(), gbuffer.getDepthView()};

    for (int i = 0; i < Engine::MAX_FRAMES; ++i) {
        std::array<VkWriteDescriptorSet, 5> writes{};
        std::array<VkDescriptorImageInfo, 3> imgInfos{};
        for (int b = 0; b < 3; ++b) {
            imgInfos[b] = {gbSampler, gbViews[b], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[b].dstSet = lightDescSets[i];
            writes[b].dstBinding = (uint32_t)b; writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1; writes[b].pImageInfo = &imgInfos[b];
        }
        VkDescriptorBufferInfo uboInfo{lightUBOBufs[i], 0, sizeof(LightsUBO)};
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[3].dstSet = lightDescSets[i];
        writes[3].dstBinding = 3; writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1; writes[3].pBufferInfo = &uboInfo;

        VkDescriptorImageInfo shadowInfo{shadowSampler, shadowArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[4].dstSet = lightDescSets[i];
        writes[4].dstBinding = 4; writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1; writes[4].pImageInfo = &shadowInfo;

        vkUpdateDescriptorSets(dev, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }
}

VkPipelineShaderStageCreateInfo RenderingSystem::loadShader_(Engine& engine, const std::string& path, VkShaderStageFlagBits stage) {
    auto code = engine.readFile(path);
    VkShaderModule sm = engine.createShaderModule(code);
    VkPipelineShaderStageCreateInfo si{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    si.stage = stage; si.module = sm; si.pName = "main";
    return si;
}
