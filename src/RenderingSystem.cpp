#include "RenderingSystem.h"
#include <array>
#include <iostream>
#include <cstring>

struct GeomPC {
    int isUnlit;
    float dispScale;
    float time;
    int isCurtain;
};

struct ShadowPC {
    glm::mat4 lightSpace;
};

void RenderingSystem::init(Engine& engine) {
    auto ext = engine.getSwapExtent();
    gbuffer.init(engine, ext.width, ext.height);
    createInstanceBuffers_(engine);
    createShadowResources_(engine);
    createShadowPipeline_(engine);
    createGeomPipeline_(engine);
    createDebugPipeline_(engine);
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
    vkDestroyPipeline(dev, debugPipeline, nullptr);
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
        vkDestroyBuffer(dev, instanceBufs[i], nullptr); vkFreeMemory(dev, instanceMems[i], nullptr);
    }
    gbuffer.cleanup(dev);
}

void RenderingSystem::onResize(Engine& engine) {
    VkDevice dev = engine.getDevice(); auto ext = engine.getSwapExtent();
    cleanupFramebuffers_(dev); gbuffer.recreate(engine, ext.width, ext.height);
    createFramebuffers_(engine); updateLightDescSets_(engine);
}

void RenderingSystem::createInstanceBuffers_(Engine& engine) {
    int frames = Engine::MAX_FRAMES;
    instanceBufs.resize(frames); instanceMems.resize(frames); instanceMapped.resize(frames);
    for(int i = 0; i < frames; ++i) {
        engine.createBuffer(sizeof(InstanceData) * MAX_INSTANCES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instanceBufs[i], instanceMems[i]);
        vkMapMemory(engine.getDevice(), instanceMems[i], 0, sizeof(InstanceData) * MAX_INSTANCES, 0, &instanceMapped[i]);
    }
}

void RenderingSystem::recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, int frameIndex, const Camera& camera, const std::vector<const SceneObject*>& objects, Engine& engine, float time, MeshHandle debugCubeMesh, const std::vector<AABB>& staticNodes, const std::vector<AABB>& dynamicNodes) {
    auto ext = engine.getSwapExtent();
    GeomUBO gubo{ camera.view(), camera.projection((float)ext.width/(float)ext.height), glm::vec4(camera.position, 1.0f) };
    memcpy(geomUBOMapped[frameIndex], &gubo, sizeof(GeomUBO));
    LightsUBO lubo{ glm::vec4(camera.position, 1.0f), glm::vec4(0.08f, 0.08f, 0.10f, 1.0f), { (int)std::min((int)pendingLights.size(), MAX_LIGHTS), 0,0,0 }, glm::inverse(gubo.proj * gubo.view) };
    for (int i = 0; i < lubo.countPad.x; ++i) lubo.lights[i] = pendingLights[i];
    memcpy(lightUBOMapped[frameIndex], &lubo, sizeof(LightsUBO));

    activeBatches.clear();
    for (const auto* obj : objects) {
        for (const auto& sm : obj->submeshes) {
            RenderBatch* target = nullptr;
            for (auto& b : activeBatches) if (b.mesh.id == sm.mesh.id && b.matSet == sm.matSet && b.isUnlit == obj->unlit && b.isCurtain == sm.isCurtain && b.unlitColor == obj->unlitColor) { target = &b; break; }
            if (!target) { activeBatches.push_back({sm.mesh, sm.matSet, obj->unlit, obj->unlitColor, sm.dispScale, sm.isCurtain}); target = &activeBatches.back(); }
            target->instances.push_back({obj->transform, obj->unlit ? obj->unlitColor : glm::vec4(1.0f)});
        }
    }

    InstanceData* mapped = (InstanceData*)instanceMapped[frameIndex];
    uint32_t currentOffset = 0;
    for (auto& b : activeBatches) {
        b.instanceOffset = currentOffset; b.instanceCount = (uint32_t)b.instances.size();
        memcpy(mapped + currentOffset, b.instances.data(), b.instanceCount * sizeof(InstanceData));
        currentOffset += b.instanceCount;
    }

    uint32_t sOff = currentOffset, sCnt = (uint32_t)staticNodes.size();
    for (const auto& b : staticNodes) {
        if (currentOffset >= MAX_INSTANCES) break;
        glm::vec3 center = (b.minVal + b.maxVal) * 0.5f;
        glm::vec3 size = (b.maxVal - b.minVal) * 0.5f;
        mapped[currentOffset++] = { glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), size), {0, 1, 0, 1} };
    }
    uint32_t dOff = currentOffset, dCnt = (uint32_t)dynamicNodes.size();
    for (const auto& b : dynamicNodes) {
        if (currentOffset >= MAX_INSTANCES) break;
        glm::vec3 center = (b.minVal + b.maxVal) * 0.5f;
        glm::vec3 size = (b.maxVal - b.minVal) * 0.5f;
        mapped[currentOffset++] = { glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), size), {1, 0, 0, 1} };
    }

    for (int i = 0; i < lubo.countPad.x; ++i) {
        if (pendingLights[i].params2.x > 0.5f) {
            VkRenderPassBeginInfo rpi{}; rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpi.renderPass = shadowRenderPass; rpi.framebuffer = shadowFramebuffers[(int)pendingLights[i].params2.y];
            rpi.renderArea = {{0,0}, {2048,2048}};
            VkClearValue cv; cv.depthStencil = {1.0f, 0}; rpi.clearValueCount = 1; rpi.pClearValues = &cv;
            vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
            VkViewport vp{0, 0, 2048, 2048, 0, 1}; vkCmdSetViewport(cmd, 0, 1, &vp);
            VkRect2D sc{{0, 0}, {2048, 2048}}; vkCmdSetScissor(cmd, 0, 1, &sc);
            for (const auto& b : activeBatches) if (!b.isUnlit) {
                ShadowPC spc{ pendingLights[i].lightSpace }; vkCmdPushConstants(cmd, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPC), &spc);
                engine.bindAndDrawInstanced(cmd, b.mesh, instanceBufs[frameIndex], b.instanceOffset, b.instanceCount);
            }
            vkCmdEndRenderPass(cmd);
        }
    }

    std::array<VkClearValue, 3> clears{}; clears[2].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rpi{}; rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = gbuffer.getRenderPass(); rpi.framebuffer = gbuffer.getFramebuffer();
    rpi.renderArea = {{0,0}, ext}; rpi.clearValueCount = 3; rpi.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0,0,(float)ext.width,(float)ext.height, 0, 1}; vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0,0}, ext}; vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 0, 1, &geomDescSets[frameIndex], 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipeline);
    for (const auto& b : activeBatches) {
        GeomPC gpc{b.isUnlit?1:0, b.dispScale, time, b.isCurtain?1:0};
        vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT|VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GeomPC), &gpc);
        if (!b.isUnlit && b.matSet != VK_NULL_HANDLE) vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 1, 1, &b.matSet, 0, nullptr);
        engine.bindAndDrawInstanced(cmd, b.mesh, instanceBufs[frameIndex], b.instanceOffset, b.instanceCount);
    }

    if (debugCubeMesh.valid() && (sCnt > 0 || dCnt > 0)) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline);
        GeomPC gpc{1, 0, time, 0};
        vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT|VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GeomPC), &gpc);
        if (sCnt > 0) engine.bindAndDrawInstanced(cmd, debugCubeMesh, instanceBufs[frameIndex], sOff, sCnt);
        if (dCnt > 0) engine.bindAndDrawInstanced(cmd, debugCubeMesh, instanceBufs[frameIndex], dOff, dCnt);
    }

    vkCmdEndRenderPass(cmd);

    VkClearValue lc; lc.color = {{0.02f, 0.02f, 0.05f, 1.0f}};
    VkRenderPassBeginInfo lrpi{}; lrpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    lrpi.renderPass = lightRenderPass; lrpi.framebuffer = lightFramebuffers[imageIndex];
    lrpi.renderArea = {{0,0}, ext}; lrpi.clearValueCount = 1; lrpi.pClearValues = &lc;
    vkCmdBeginRenderPass(cmd, &lrpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipelineLayout, 0, 1, &lightDescSets[frameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void RenderingSystem::createShadowResources_(Engine& engine) {
    VkFormat depthFmt = engine.findDepthFormat();
    engine.createImage(2048, 2048, 4, depthFmt, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, shadowImage, shadowMemory);
    engine.transitionLayout(shadowImage, 4, depthFmt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    shadowArrayView = engine.createImageView(shadowImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 4, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    shadowLayerViews.resize(4); for(int i=0; i<4; ++i) shadowLayerViews[i] = engine.createImageView(shadowImage, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT, i, 1, VK_IMAGE_VIEW_TYPE_2D);
    VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(engine.getDevice(), &si, nullptr, &shadowSampler);
    VkAttachmentDescription att{}; att.format = depthFmt; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.pDepthStencilAttachment = &ref;
    VkSubpassDependency deps[2] = { {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0}, {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0} };
    VkRenderPassCreateInfo rpci{}; rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpci.attachmentCount = 1; rpci.pAttachments = &att; rpci.subpassCount = 1; rpci.pSubpasses = &sub; rpci.dependencyCount = 2; rpci.pDependencies = deps;
    vkCreateRenderPass(engine.getDevice(), &rpci, nullptr, &shadowRenderPass);
    shadowFramebuffers.resize(4); for(int i=0; i<4; ++i) { VkFramebufferCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fci.renderPass = shadowRenderPass; fci.attachmentCount = 1; fci.pAttachments = &shadowLayerViews[i]; fci.width = 2048; fci.height = 2048; fci.layers = 1; vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &shadowFramebuffers[i]); }
}

void RenderingSystem::createShadowPipeline_(Engine& engine) {
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPC)};
    VkPipelineLayoutCreateInfo plci{}; plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(engine.getDevice(), &plci, nullptr, &shadowPipelineLayout);
    auto vs = loadShader_(engine, "shaders/shadows.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto b0 = Vertex::getBindingDesc(); auto a0 = Vertex::getAttrDescs(); auto b1 = InstanceData::getBindingDesc(); auto a1 = InstanceData::getAttrDescs();
    std::vector<VkVertexInputBindingDescription> binds = {b0, b1}; std::vector<VkVertexInputAttributeDescription> attrs(a0.begin(), a0.end()); attrs.insert(attrs.end(), a1.begin(), a1.end());
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds.data(); vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f; rs.depthBiasEnable = VK_TRUE; rs.depthBiasConstantFactor = 1.25f; rs.depthBiasSlopeFactor = 1.75f;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = 1; ds.depthWriteEnable = 1; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gci{}; gci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gci.stageCount = 1; gci.pStages = &vs; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pDepthStencilState = &ds; gci.pDynamicState = &dy; gci.layout = shadowPipelineLayout; gci.renderPass = shadowRenderPass;
    vkCreateGraphicsPipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &gci, nullptr, &shadowPipeline); vkDestroyShaderModule(engine.getDevice(), vs.module, nullptr);
}

void RenderingSystem::createGeomPipeline_(Engine& engine) {
    VkDescriptorSetLayoutBinding ub{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT|VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{}; lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; lci.bindingCount = 1; lci.pBindings = &ub;
    vkCreateDescriptorSetLayout(engine.getDevice(), &lci, nullptr, &geomUBOLayout);
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT|VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GeomPC)};
    VkDescriptorSetLayout sets[] = {geomUBOLayout, engine.getMaterialLayout()};
    VkPipelineLayoutCreateInfo plci{}; plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; plci.setLayoutCount = 2; plci.pSetLayouts = sets; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(engine.getDevice(), &plci, nullptr, &geomPipelineLayout);
    auto vs = loadShader_(engine, "shaders/gbuffer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT); auto tcs = loadShader_(engine, "shaders/gbuffer.tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    auto tes = loadShader_(engine, "shaders/gbuffer.tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT); auto fs = loadShader_(engine, "shaders/gbuffer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vs, tcs, tes, fs};
    auto b0 = Vertex::getBindingDesc(); auto a0 = Vertex::getAttrDescs(); auto b1 = InstanceData::getBindingDesc(); auto a1 = InstanceData::getAttrDescs();
    std::vector<VkVertexInputBindingDescription> binds = {b0, b1}; std::vector<VkVertexInputAttributeDescription> attrs(a0.begin(), a0.end()); attrs.insert(attrs.end(), a1.begin(), a1.end());
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds.data(); vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    VkPipelineTessellationStateCreateInfo ts{}; ts.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO; ts.patchControlPoints = 3;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = 1; ds.depthWriteEnable = 1; ds.depthCompareOp = VK_COMPARE_OP_LESS;
    std::array<VkPipelineColorBlendAttachmentState, 2> cba{}; for(auto& a:cba) a.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 2; cb.pAttachments = cba.data();
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gci{}; gci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gci.stageCount = 4; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pTessellationState = &ts; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pDepthStencilState = &ds; gci.pColorBlendState = &cb; gci.pDynamicState = &dy; gci.layout = geomPipelineLayout; gci.renderPass = gbuffer.getRenderPass();
    vkCreateGraphicsPipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &gci, nullptr, &geomPipeline);
    vkDestroyShaderModule(engine.getDevice(), vs.module, nullptr); vkDestroyShaderModule(engine.getDevice(), tcs.module, nullptr); vkDestroyShaderModule(engine.getDevice(), tes.module, nullptr); vkDestroyShaderModule(engine.getDevice(), fs.module, nullptr);
}

void RenderingSystem::createDebugPipeline_(Engine& engine) {
    auto vs = loadShader_(engine, "shaders/gbuffer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fs = loadShader_(engine, "shaders/gbuffer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vs, fs};
    auto b0 = Vertex::getBindingDesc(); auto a0 = Vertex::getAttrDescs(); auto b1 = InstanceData::getBindingDesc(); auto a1 = InstanceData::getAttrDescs();
    std::vector<VkVertexInputBindingDescription> binds = {b0, b1}; std::vector<VkVertexInputAttributeDescription> attrs(a0.begin(), a0.end()); attrs.insert(attrs.end(), a1.begin(), a1.end());
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds.data(); vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_LINE; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = 1; ds.depthWriteEnable = 0; ds.depthCompareOp = VK_COMPARE_OP_LESS;
    std::array<VkPipelineColorBlendAttachmentState, 2> cba{}; for(auto& a:cba) a.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 2; cb.pAttachments = cba.data();
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gci{}; gci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gci.stageCount = 2; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pDepthStencilState = &ds; gci.pColorBlendState = &cb; gci.pDynamicState = &dy; gci.layout = geomPipelineLayout; gci.renderPass = gbuffer.getRenderPass();
    vkCreateGraphicsPipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &gci, nullptr, &debugPipeline);
    vkDestroyShaderModule(engine.getDevice(), vs.module, nullptr); vkDestroyShaderModule(engine.getDevice(), fs.module, nullptr);
}

void RenderingSystem::createLightRenderPass_(Engine& engine) {
    VkAttachmentDescription att{}; att.format = engine.getSwapFormat(); att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    VkSubpassDependency deps[2] = { {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0}, {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, 0} };
    VkRenderPassCreateInfo rpci{}; rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpci.attachmentCount = 1; rpci.pAttachments = &att; rpci.subpassCount = 1; rpci.pSubpasses = &sub; rpci.dependencyCount = 2; rpci.pDependencies = deps;
    vkCreateRenderPass(engine.getDevice(), &rpci, nullptr, &lightRenderPass);
}

void RenderingSystem::createLightPipeline_(Engine& engine) {
    std::array<VkDescriptorSetLayoutBinding, 5> b{}; for(int i=0; i<3; ++i) b[i]={ (uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[3]={3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}; b[4]={4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{}; lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; lci.bindingCount = 5; lci.pBindings = b.data();
    vkCreateDescriptorSetLayout(engine.getDevice(), &lci, nullptr, &lightDescLayout);
    VkPipelineLayoutCreateInfo plci{}; plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; plci.setLayoutCount = 1; plci.pSetLayouts = &lightDescLayout;
    vkCreatePipelineLayout(engine.getDevice(), &plci, nullptr, &lightPipelineLayout);
    auto vs = loadShader_(engine, "shaders/lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT); auto fs = loadShader_(engine, "shaders/lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vs, fs};
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gci{}; gci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gci.stageCount = 2; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pColorBlendState = &cb; gci.pDynamicState = &dy; gci.layout = lightPipelineLayout; gci.renderPass = lightRenderPass;
    vkCreateGraphicsPipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &gci, nullptr, &lightPipeline); vkDestroyShaderModule(engine.getDevice(), vs.module, nullptr); vkDestroyShaderModule(engine.getDevice(), fs.module, nullptr);
}

void RenderingSystem::createFramebuffers_(Engine& engine) {
    auto views = engine.getSwapImageViews(); auto ext = engine.getSwapExtent();
    lightFramebuffers.resize(views.size()); for(size_t i=0; i<views.size(); ++i) { VkFramebufferCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fci.renderPass = lightRenderPass; fci.attachmentCount = 1; fci.pAttachments = &views[i]; fci.width = ext.width; fci.height = ext.height; fci.layers = 1; vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &lightFramebuffers[i]); }
}

void RenderingSystem::cleanupFramebuffers_(VkDevice device) { for (auto fb : lightFramebuffers) vkDestroyFramebuffer(device, fb, nullptr); lightFramebuffers.clear(); }

void RenderingSystem::createDescriptors_(Engine& engine) {
    int f = Engine::MAX_FRAMES; VkDevice d = engine.getDevice();
    { VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)f}; VkDescriptorPoolCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; ci.maxSets = (uint32_t)f; ci.poolSizeCount = 1; ci.pPoolSizes = &ps; vkCreateDescriptorPool(d, &ci, nullptr, &geomDescPool); std::vector<VkDescriptorSetLayout> l(f, geomUBOLayout); VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; ai.descriptorPool = geomDescPool; ai.descriptorSetCount = (uint32_t)f; ai.pSetLayouts = l.data(); geomDescSets.resize(f); vkAllocateDescriptorSets(d, &ai, geomDescSets.data()); geomUBOBufs.resize(f); geomUBOMems.resize(f); geomUBOMapped.resize(f); for(int i=0; i<f; ++i) { engine.createBuffer(sizeof(GeomUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, geomUBOBufs[i], geomUBOMems[i]); vkMapMemory(d, geomUBOMems[i], 0, sizeof(GeomUBO), 0, &geomUBOMapped[i]); VkDescriptorBufferInfo bi{geomUBOBufs[i], 0, sizeof(GeomUBO)}; VkWriteDescriptorSet w{}; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = geomDescSets[i]; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.pBufferInfo = &bi; vkUpdateDescriptorSets(d, 1, &w, 0, nullptr); } }
    { std::array<VkDescriptorPoolSize, 2> ps{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)4*f}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)f}}}; VkDescriptorPoolCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; ci.maxSets = (uint32_t)f; ci.poolSizeCount = 2; ci.pPoolSizes = ps.data(); vkCreateDescriptorPool(d, &ci, nullptr, &lightDescPool); std::vector<VkDescriptorSetLayout> l(f, lightDescLayout); VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; ai.descriptorPool = lightDescPool; ai.descriptorSetCount = (uint32_t)f; ai.pSetLayouts = l.data(); lightDescSets.resize(f); vkAllocateDescriptorSets(d, &ai, lightDescSets.data()); lightUBOBufs.resize(f); lightUBOMems.resize(f); lightUBOMapped.resize(f); for(int i=0; i<f; ++i) { engine.createBuffer(sizeof(LightsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, lightUBOBufs[i], lightUBOMems[i]); vkMapMemory(d, lightUBOMems[i], 0, sizeof(LightsUBO), 0, &lightUBOMapped[i]); } }
}

void RenderingSystem::updateLightDescSets_(Engine& engine) {
    VkSampler s = gbuffer.getSampler(); VkImageView v[3] = {gbuffer.getNormalView(), gbuffer.getAlbedoView(), gbuffer.getDepthView()};
    for(int i=0; i<Engine::MAX_FRAMES; ++i) {
        std::array<VkWriteDescriptorSet, 5> w{}; std::array<VkDescriptorImageInfo, 3> ii{};
        for(int b=0; b<3; ++b) {
            ii[b].sampler = s; ii[b].imageView = v[b]; ii[b].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[b].dstSet = lightDescSets[i]; w[b].dstBinding = (uint32_t)b; w[b].descriptorCount = 1; w[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[b].pImageInfo = &ii[b];
        }
        VkDescriptorBufferInfo bi{lightUBOBufs[i], 0, sizeof(LightsUBO)};
        w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[3].dstSet = lightDescSets[i]; w[3].dstBinding = 3; w[3].descriptorCount = 1; w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[3].pBufferInfo = &bi;
        VkDescriptorImageInfo si{shadowSampler, shadowArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        w[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[4].dstSet = lightDescSets[i]; w[4].dstBinding = 4; w[4].descriptorCount = 1; w[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[4].pImageInfo = &si;
        vkUpdateDescriptorSets(engine.getDevice(), 5, w.data(), 0, nullptr);
    }
}

VkPipelineShaderStageCreateInfo RenderingSystem::loadShader_(Engine& engine, const std::string& path, VkShaderStageFlagBits stage) {
    auto c = engine.readFile(path);
    VkPipelineShaderStageCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    si.stage = stage; si.module = engine.createShaderModule(c); si.pName = "main";
    return si;
}
