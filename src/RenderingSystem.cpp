#include "RenderingSystem.h"
#include <array>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>

struct GeomPC {
    int isUnlit;
    float dispScale;
    float time;
    int isCurtain;
    int isTransparent;
};

struct ParticlePush {
    float dt;
    float time;
    int spawnCount;
    int maxParticles;
    glm::vec4 emitterPos;
    glm::vec4 spherePos;
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

    createParticleResources_(engine);
    createParticlePipelines_(engine);
    createTerrainPipeline_(engine);
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
    vkDestroyDescriptorSetLayout(dev, shadowDescLayout, nullptr);
    vkDestroyDescriptorPool(dev, shadowDescPool, nullptr);
    vkDestroyRenderPass(dev, shadowRenderPass, nullptr);
    vkDestroySampler(dev, shadowSampler, nullptr);

    vkDestroyPipeline(dev, terrainPipeline, nullptr);
    vkDestroyPipelineLayout(dev, terrainPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, terrainMaterialLayout, nullptr);

    vkDestroyPipeline(dev, particleComputePipeline, nullptr);
    vkDestroyPipeline(dev, particleResetPipeline, nullptr);
    vkDestroyPipeline(dev, particleGraphicsPipeline, nullptr);
    vkDestroyPipelineLayout(dev, particleComputePipelineLayout, nullptr);
    vkDestroyPipelineLayout(dev, particleResetPipelineLayout, nullptr);
    vkDestroyPipelineLayout(dev, particleGraphicsPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, particleComputeLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, particleResetLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, particleGraphicsLayout, nullptr);
    vkDestroyDescriptorPool(dev, particleDescPool, nullptr);

    for (int i = 0; i < 2; ++i) {
        vkDestroyBuffer(dev, particleBuffers[i], nullptr);
        vkFreeMemory(dev, particleMemories[i], nullptr);
    }

    for (int i = 0; i < Engine::MAX_FRAMES; ++i) {
        vkDestroyImageView(dev, shadowArrayViews[i], nullptr);
        vkDestroyFramebuffer(dev, shadowFramebuffers[i], nullptr);
        vkDestroyImage(dev, shadowImages[i], nullptr);
        vkFreeMemory(dev, shadowMemories[i], nullptr);

        vkDestroyBuffer(dev, geomUBOBufs[i], nullptr); vkFreeMemory(dev, geomUBOMems[i], nullptr);
        vkDestroyBuffer(dev, lightUBOBufs[i], nullptr); vkFreeMemory(dev, lightUBOMems[i], nullptr);
        vkDestroyBuffer(dev, instanceBufs[i], nullptr); vkFreeMemory(dev, instanceMems[i], nullptr);
        vkDestroyBuffer(dev, shadowUBOBufs[i], nullptr); vkFreeMemory(dev, shadowUBOMems[i], nullptr);
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

void RenderingSystem::createTerrainPipeline_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1;
    lci.pBindings = &b;
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &terrainMaterialLayout);

    VkPushConstantRange pcr{VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(TerrainPatchPush)};
    VkDescriptorSetLayout layouts[] = {geomUBOLayout, terrainMaterialLayout};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 2;
    plci.pSetLayouts = layouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &terrainPipelineLayout);

    auto vs = loadShader_(engine, "shaders/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fs = loadShader_(engine, "shaders/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

    auto bd = Vertex::getBindingDesc();
    auto ad = Vertex::getAttrDescs();
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bd;
    vi.vertexAttributeDescriptionCount = (uint32_t)ad.size();
    vi.pVertexAttributeDescriptions = ad.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = 1;
    ds.depthWriteEnable = 1;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    std::array<VkPipelineColorBlendAttachmentState, 2> cba{};
    for (auto& a : cba) a.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 2;
    cb.pAttachments = cba.data();

    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount = 2;
    gci.pStages = stages;
    gci.pVertexInputState = &vi;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState = &vp;
    gci.pRasterizationState = &rs;
    gci.pMultisampleState = &ms;
    gci.pDepthStencilState = &ds;
    gci.pColorBlendState = &cb;
    gci.pDynamicState = &dy;
    gci.layout = terrainPipelineLayout;
    gci.renderPass = gbuffer.getRenderPass();

    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &terrainPipeline);
    vkDestroyShaderModule(dev, vs.module, nullptr);
    vkDestroyShaderModule(dev, fs.module, nullptr);
}

void RenderingSystem::recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, int frameIndex, const Camera& camera, const std::vector<const SceneObject*>& objects, Engine& engine, float time, MeshHandle debugCubeMesh, const std::vector<AABB>& staticNodes, const std::vector<AABB>& dynamicNodes, bool enableParticles, const Terrain* terrain) {
    auto ext = engine.getSwapExtent();
    GeomUBO gubo{ camera.view(), camera.projection((float)ext.width/(float)ext.height), glm::vec4(camera.position, 1.0f) };
    memcpy(geomUBOMapped[frameIndex], &gubo, sizeof(GeomUBO));
    LightsUBO lubo{ glm::vec4(camera.position, 1.0f), glm::vec4(0.08f, 0.08f, 0.10f, 1.0f), { (int)std::min((size_t)MAX_LIGHTS, pendingLights.size()), 0,0,0 }, glm::inverse(gubo.proj * gubo.view), camera.view() };

    for (int i = 0; i < lubo.countPad.x; ++i) {
        lubo.lights[i] = pendingLights[i];
        if (lubo.lights[i].params2.x > 0.5f) {
            float cameraNear = 0.1f;
            float cameraFar = 1000.0f;
            float shadowFar = 150.0f;
            float cascadeSplits[4];
            float ratio = shadowFar / cameraNear;
            for (int c = 0; c < 4; c++) {
                float p = (c + 1) / 4.0f;
                float logScale = cameraNear * std::pow(ratio, p);
                float linScale = cameraNear + (shadowFar - cameraNear) * p;
                cascadeSplits[c] = 0.75f * logScale + 0.25f * linScale;
            }

            glm::vec3 originalCorners[8] = {
                glm::vec3(-1.0f,  1.0f, 0.0f), glm::vec3( 1.0f,  1.0f, 0.0f),
                glm::vec3( 1.0f, -1.0f, 0.0f), glm::vec3(-1.0f, -1.0f, 0.0f),
                glm::vec3(-1.0f,  1.0f, 1.0f), glm::vec3( 1.0f,  1.0f, 1.0f),
                glm::vec3( 1.0f, -1.0f, 1.0f), glm::vec3(-1.0f, -1.0f, 1.0f),
            };
            glm::mat4 invCam = glm::inverse(camera.projection((float)ext.width/(float)ext.height) * camera.view());
            for (int j = 0; j < 8; j++) {
                glm::vec4 invCorner = invCam * glm::vec4(originalCorners[j], 1.0f);
                originalCorners[j] = glm::vec3(invCorner) / invCorner.w;
            }

            float lastSplitDist = cameraNear;
            for (int c = 0; c < 4; c++) {
                float splitDist = cascadeSplits[c];
                glm::vec3 frustumCorners[8];
                for (int j = 0; j < 4; j++) {
                    glm::vec3 dist = originalCorners[j + 4] - originalCorners[j];
                    frustumCorners[j + 4] = originalCorners[j] + dist * ((splitDist - cameraNear) / (cameraFar - cameraNear));
                    frustumCorners[j] = originalCorners[j] + dist * ((lastSplitDist - cameraNear) / (cameraFar - cameraNear));
                }

                glm::vec3 frustumCenter = glm::vec3(0.0f);
                for (int j = 0; j < 8; j++) {
                    frustumCenter += frustumCorners[j];
                }
                frustumCenter /= 8.0f;

                float radius = 0.0f;
                for (int j = 0; j < 8; j++) {
                    float distance = glm::length(frustumCorners[j] - frustumCenter);
                    radius = std::max(radius, distance);
                }
                radius = std::ceil(radius * 16.0f) / 16.0f;

                glm::vec3 lightDir = glm::normalize(glm::vec3(lubo.lights[i].direction));
                glm::vec3 up = (std::abs(lightDir.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);

                float zMultiplier = 10.0f;
                glm::mat4 lightViewMatrix = glm::lookAt(frustumCenter - lightDir * (radius * zMultiplier), frustumCenter, up);
                glm::mat4 lightOrthoMatrix = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * (1.0f + zMultiplier));
                lightOrthoMatrix[1][1] *= -1.0f;

                glm::mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
                glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                shadowOrigin *= (2048.0f / 2.0f);
                glm::vec4 roundedOrigin = glm::round(shadowOrigin);
                glm::vec4 roundOffset = (roundedOrigin - shadowOrigin) * (2.0f / 2048.0f);
                roundOffset.z = 0.0f;
                roundOffset.w = 0.0f;
                lightOrthoMatrix[3] += roundOffset;

                lubo.lights[i].cascadeMatrices[c] = lightOrthoMatrix * lightViewMatrix;
                lubo.lights[i].cascadeSplits[c] = splitDist;
                ((glm::mat4*)shadowUBOMapped[frameIndex])[c] = lubo.lights[i].cascadeMatrices[c];

                lastSplitDist = splitDist;
            }
        }
    }
    memcpy(lightUBOMapped[frameIndex], &lubo, sizeof(LightsUBO));

    activeBatches.clear();
    for (const auto* obj : objects) {
        for (const auto& sm : obj->submeshes) {
            RenderBatch* target = nullptr;
            for (auto& b : activeBatches) if (b.mesh.id == sm.mesh.id && b.matSet == sm.matSet && b.isUnlit == obj->unlit && b.isCurtain == sm.isCurtain && b.unlitColor == obj->unlitColor && b.isTransparent == obj->isTransparent) { target = &b; break; }
            if (!target) { activeBatches.push_back({sm.mesh, sm.matSet, obj->unlit, obj->unlitColor, sm.dispScale, sm.isCurtain, obj->isTransparent, 0, 0, {}}); target = &activeBatches.back(); }
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

    int bufIn = particleFrameIndex % 2;
    int bufOut = (particleFrameIndex + 1) % 2;

    float dt = time - lastTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;
    lastTime = time;

    ParticlePush pPush{};
    pPush.dt = dt;
    pPush.time = time;
    pPush.spawnCount = 20;
    pPush.maxParticles = 100000;
    pPush.emitterPos = glm::vec4(0.0f, 15.0f, 0.0f, 1.0f);
    pPush.spherePos = glm::vec4(0.0f, 7.0f, 0.0f, 2.0f);

    if (enableParticles) {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, particleResetPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, particleResetPipelineLayout, 0, 1, &particleResetSets[bufIn], 0, nullptr);
        vkCmdDispatch(cmd, 1, 1, 1);

        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, particleComputePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, particleComputePipelineLayout, 0, 1, &particleComputeSets[bufIn], 0, nullptr);
        vkCmdPushConstants(cmd, particleComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ParticlePush), &pPush);
        vkCmdDispatch(cmd, (pPush.maxParticles / 64) + 1, 1, 1);

        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    for (int i = 0; i < lubo.countPad.x; ++i) {
        if (pendingLights[i].params2.x > 0.5f) {
            VkRenderPassBeginInfo rpi{}; rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpi.renderPass = shadowRenderPass; rpi.framebuffer = shadowFramebuffers[frameIndex];
            rpi.renderArea = {{0,0}, {2048,2048}};
            VkClearValue cv; cv.depthStencil = {1.0f, 0}; rpi.clearValueCount = 1; rpi.pClearValues = &cv;
            vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1, &shadowDescSets[frameIndex], 0, nullptr);
            VkViewport vp{0, 0, 2048, 2048, 0, 1}; vkCmdSetViewport(cmd, 0, 1, &vp);
            VkRect2D sc{{0, 0}, {2048, 2048}}; vkCmdSetScissor(cmd, 0, 1, &sc);
            for (const auto& b : activeBatches) if (!b.isUnlit) {
                if (b.matSet != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 1, 1, &b.matSet, 0, nullptr);
                }
                engine.bindAndDrawInstanced(cmd, b.mesh, instanceBufs[frameIndex], b.instanceOffset, b.instanceCount);
            }
            vkCmdEndRenderPass(cmd);
            break;
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
        GeomPC gpc{b.isUnlit?1:0, b.dispScale, time, b.isCurtain?1:0, b.isTransparent?1:0};
        vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(GeomPC), &gpc);
        if (!b.isUnlit && b.matSet != VK_NULL_HANDLE) vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 1, 1, &b.matSet, 0, nullptr);
        engine.bindAndDrawInstanced(cmd, b.mesh, instanceBufs[frameIndex], b.instanceOffset, b.instanceCount);
    }

    if (terrain && terrain->isInitialized()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 0, 1, &geomDescSets[frameIndex], 0, nullptr);
        terrain->draw(cmd, terrainPipelineLayout, engine);
    }

    if (debugCubeMesh.valid() && (sCnt > 0 || dCnt > 0)) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline);
        GeomPC gpc{1, 0, time, 0, 0};
        vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(GeomPC), &gpc);
        if (sCnt > 0) engine.bindAndDrawInstanced(cmd, debugCubeMesh, instanceBufs[frameIndex], sOff, sCnt);
        if (dCnt > 0) engine.bindAndDrawInstanced(cmd, debugCubeMesh, instanceBufs[frameIndex], dOff, dCnt);
    }

    if (enableParticles) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particleGraphicsPipeline);
        VkDescriptorSet pSets[] = {geomDescSets[frameIndex], particleGraphicsSets[bufOut]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particleGraphicsPipelineLayout, 0, 2, pSets, 0, nullptr);
        vkCmdDrawIndirect(cmd, particleBuffers[bufOut], 0, 1, sizeof(uint32_t)*4);
    }

    vkCmdEndRenderPass(cmd);

    VkClearValue lc; lc.color = {{0.53f, 0.75f, 0.92f, 1.0f}};
    VkRenderPassBeginInfo lrpi{}; lrpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    lrpi.renderPass = lightRenderPass; lrpi.framebuffer = lightFramebuffers[imageIndex];
    lrpi.renderArea = {{0,0}, ext}; lrpi.clearValueCount = 1; lrpi.pClearValues = &lc;
    vkCmdBeginRenderPass(cmd, &lrpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipelineLayout, 0, 1, &lightDescSets[frameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    if (enableParticles) {
        particleFrameIndex++;
    }
}

void RenderingSystem::createParticleResources_(Engine& engine) {
    uint32_t maxParticles = 100000;
    VkDeviceSize bufferSize = 16 + maxParticles * 48;

    for (int i = 0; i < 2; ++i) {
        engine.createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            particleBuffers[i], particleMemories[i]);

        void* mapped;
        vkMapMemory(engine.getDevice(), particleMemories[i], 0, bufferSize, 0, &mapped);
        memset(mapped, 0, bufferSize);
        vkUnmapMemory(engine.getDevice(), particleMemories[i]);
    }

    std::array<VkDescriptorPoolSize, 1> ps = {{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}}};
    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.maxSets = 8;
    ci.poolSizeCount = 1;
    ci.pPoolSizes = ps.data();
    vkCreateDescriptorPool(engine.getDevice(), &ci, nullptr, &particleDescPool);

    VkDescriptorSetLayoutBinding lb0{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutBinding lb1{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutBinding lbs[] = {lb0, lb1};
    VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

    lc.bindingCount = 2; lc.pBindings = lbs;
    vkCreateDescriptorSetLayout(engine.getDevice(), &lc, nullptr, &particleComputeLayout);

    lc.bindingCount = 1; lc.pBindings = &lb0;
    vkCreateDescriptorSetLayout(engine.getDevice(), &lc, nullptr, &particleResetLayout);

    VkDescriptorSetLayoutBinding gBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr};
    lc.bindingCount = 1; lc.pBindings = &gBinding;
    vkCreateDescriptorSetLayout(engine.getDevice(), &lc, nullptr, &particleGraphicsLayout);

    std::vector<VkDescriptorSetLayout> cLayouts(2, particleComputeLayout);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = particleDescPool; ai.descriptorSetCount = 2; ai.pSetLayouts = cLayouts.data();
    vkAllocateDescriptorSets(engine.getDevice(), &ai, particleComputeSets);

    std::vector<VkDescriptorSetLayout> rLayouts(2, particleResetLayout);
    ai.pSetLayouts = rLayouts.data();
    vkAllocateDescriptorSets(engine.getDevice(), &ai, particleResetSets);

    std::vector<VkDescriptorSetLayout> gLayouts(2, particleGraphicsLayout);
    ai.pSetLayouts = gLayouts.data();
    vkAllocateDescriptorSets(engine.getDevice(), &ai, particleGraphicsSets);

    VkDescriptorBufferInfo bA{particleBuffers[0], 0, bufferSize};
    VkDescriptorBufferInfo bB{particleBuffers[1], 0, bufferSize};

    auto writeSet = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
        vkUpdateDescriptorSets(engine.getDevice(), 1, &w, 0, nullptr);
    };

    writeSet(particleComputeSets[0], 0, &bA); writeSet(particleComputeSets[0], 1, &bB);
    writeSet(particleComputeSets[1], 0, &bB); writeSet(particleComputeSets[1], 1, &bA);

    writeSet(particleResetSets[0], 0, &bB);
    writeSet(particleResetSets[1], 0, &bA);

    writeSet(particleGraphicsSets[0], 0, &bA);
    writeSet(particleGraphicsSets[1], 0, &bB);
}

void RenderingSystem::createParticlePipelines_(Engine& engine) {
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ParticlePush)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1; plci.pSetLayouts = &particleComputeLayout;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(engine.getDevice(), &plci, nullptr, &particleComputePipelineLayout);

    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.layout = particleComputePipelineLayout;
    cpi.stage = loadShader_(engine, "shaders/particles.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
    vkCreateComputePipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &cpi, nullptr, &particleComputePipeline);
    vkDestroyShaderModule(engine.getDevice(), cpi.stage.module, nullptr);

    plci.pSetLayouts = &particleResetLayout;
    plci.pushConstantRangeCount = 0;
    vkCreatePipelineLayout(engine.getDevice(), &plci, nullptr, &particleResetPipelineLayout);
    cpi.layout = particleResetPipelineLayout;
    cpi.stage = loadShader_(engine, "shaders/particles_reset.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
    vkCreateComputePipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &cpi, nullptr, &particleResetPipeline);
    vkDestroyShaderModule(engine.getDevice(), cpi.stage.module, nullptr);

    VkDescriptorSetLayout gLayouts[] = {geomUBOLayout, particleGraphicsLayout};
    plci.setLayoutCount = 2; plci.pSetLayouts = gLayouts;
    vkCreatePipelineLayout(engine.getDevice(), &plci, nullptr, &particleGraphicsPipelineLayout);

    auto vs = loadShader_(engine, "shaders/particles.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto gs = loadShader_(engine, "shaders/particles.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT);
    auto fs = loadShader_(engine, "shaders/particles.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vs, gs, fs};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable = 1; ds.depthWriteEnable = 1; ds.depthCompareOp = VK_COMPARE_OP_LESS;
    std::array<VkPipelineColorBlendAttachmentState, 2> cba{}; for(auto& a:cba) a.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 2; cb.pAttachments = cba.data();
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount = 3; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pDepthStencilState = &ds; gci.pColorBlendState = &cb; gci.pDynamicState = &dy;
    gci.layout = particleGraphicsPipelineLayout; gci.renderPass = gbuffer.getRenderPass();

    vkCreateGraphicsPipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &gci, nullptr, &particleGraphicsPipeline);
    vkDestroyShaderModule(engine.getDevice(), vs.module, nullptr);
    vkDestroyShaderModule(engine.getDevice(), gs.module, nullptr);
    vkDestroyShaderModule(engine.getDevice(), fs.module, nullptr);
}

void RenderingSystem::createShadowResources_(Engine& engine) {
    VkFormat depthFmt = engine.findDepthFormat();

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    vkCreateSampler(engine.getDevice(), &si, nullptr, &shadowSampler);

    VkAttachmentDescription att{};
    att.format = depthFmt;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &ref;

    VkSubpassDependency deps[2] = {
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0}
    };

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    vkCreateRenderPass(engine.getDevice(), &rpci, nullptr, &shadowRenderPass);

    int f = Engine::MAX_FRAMES;
    shadowImages.resize(f);
    shadowMemories.resize(f);
    shadowArrayViews.resize(f);
    shadowFramebuffers.resize(f);

    for(int i=0; i<f; ++i) {
        engine.createImage(2048, 2048, 4, depthFmt, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, shadowImages[i], shadowMemories[i]);
        engine.transitionLayout(shadowImages[i], 4, depthFmt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        shadowArrayViews[i] = engine.createImageView(shadowImages[i], depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 4, VK_IMAGE_VIEW_TYPE_2D_ARRAY);

        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = shadowRenderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &shadowArrayViews[i];
        fci.width = 2048;
        fci.height = 2048;
        fci.layers = 4;
        vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &shadowFramebuffers[i]);
    }
}

void RenderingSystem::createShadowPipeline_(Engine& engine) {
    VkDescriptorSetLayoutBinding ub{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_GEOMETRY_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{}; lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; lci.bindingCount = 1; lci.pBindings = &ub;
    vkCreateDescriptorSetLayout(engine.getDevice(), &lci, nullptr, &shadowDescLayout);

    VkDescriptorSetLayout sets[] = {shadowDescLayout, engine.getMaterialLayout()};
    VkPipelineLayoutCreateInfo plci{}; plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; plci.setLayoutCount = 2; plci.pSetLayouts = sets;
    vkCreatePipelineLayout(engine.getDevice(), &plci, nullptr, &shadowPipelineLayout);

    auto vs = loadShader_(engine, "shaders/shadows.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto gs = loadShader_(engine, "shaders/shadows.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT);
    auto fs = loadShader_(engine, "shaders/shadows.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    auto b0 = Vertex::getBindingDesc(); auto a0 = Vertex::getAttrDescs(); auto b1 = InstanceData::getBindingDesc(); auto a1 = InstanceData::getAttrDescs();
    std::vector<VkVertexInputBindingDescription> binds = {b0, b1}; std::vector<VkVertexInputAttributeDescription> attrs(a0.begin(), a0.end()); attrs.insert(attrs.end(), a1.begin(), a1.end());

    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds.data(); vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f; rs.depthBiasEnable = VK_TRUE; rs.depthBiasConstantFactor = 1.25f; rs.depthBiasSlopeFactor = 1.75f;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = 1; ds.depthWriteEnable = 1; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkPipelineShaderStageCreateInfo stages[] = {vs, gs, fs};

    VkGraphicsPipelineCreateInfo gci{}; gci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gci.stageCount = 3; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pDepthStencilState = &ds; gci.pDynamicState = &dy; gci.layout = shadowPipelineLayout; gci.renderPass = shadowRenderPass;
    vkCreateGraphicsPipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &gci, nullptr, &shadowPipeline);
    vkDestroyShaderModule(engine.getDevice(), vs.module, nullptr); vkDestroyShaderModule(engine.getDevice(), gs.module, nullptr); vkDestroyShaderModule(engine.getDevice(), fs.module, nullptr);
}

void RenderingSystem::createGeomPipeline_(Engine& engine) {
    VkDescriptorSetLayoutBinding ub{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{}; lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; lci.bindingCount = 1; lci.pBindings = &ub;
    vkCreateDescriptorSetLayout(engine.getDevice(), &lci, nullptr, &geomUBOLayout);

    VkPushConstantRange pcr{VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(GeomPC)};
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
    lightFramebuffers.resize(views.size());
    for(size_t i=0; i<views.size(); ++i) {
        VkFramebufferCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fci.renderPass = lightRenderPass; fci.attachmentCount = 1; fci.pAttachments = &views[i]; fci.width = ext.width; fci.height = ext.height; fci.layers = 1; vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &lightFramebuffers[i]);
    }
}

void RenderingSystem::cleanupFramebuffers_(VkDevice device) {
    for (auto fb : lightFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    lightFramebuffers.clear();
}

void RenderingSystem::createDescriptors_(Engine& engine) {
    int f = Engine::MAX_FRAMES; VkDevice d = engine.getDevice();

    {
        std::array<VkDescriptorPoolSize, 2> ps{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)4*f}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)f}}};
        VkDescriptorPoolCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; ci.maxSets = (uint32_t)f; ci.poolSizeCount = 2; ci.pPoolSizes = ps.data();
        vkCreateDescriptorPool(d, &ci, nullptr, &lightDescPool);

        std::vector<VkDescriptorSetLayout> l(f, lightDescLayout);
        VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; ai.descriptorPool = lightDescPool; ai.descriptorSetCount = (uint32_t)f; ai.pSetLayouts = l.data();
        lightDescSets.resize(f);
        vkAllocateDescriptorSets(d, &ai, lightDescSets.data());

        lightUBOBufs.resize(f); lightUBOMems.resize(f); lightUBOMapped.resize(f);
        for(int i=0; i<f; ++i) {
            engine.createBuffer(sizeof(LightsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, lightUBOBufs[i], lightUBOMems[i]);
            vkMapMemory(d, lightUBOMems[i], 0, sizeof(LightsUBO), 0, &lightUBOMapped[i]);
        }
    }

    {
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)f};
        VkDescriptorPoolCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; ci.maxSets = (uint32_t)f; ci.poolSizeCount = 1; ci.pPoolSizes = &ps;
        vkCreateDescriptorPool(d, &ci, nullptr, &shadowDescPool);

        std::vector<VkDescriptorSetLayout> l(f, shadowDescLayout);
        VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; ai.descriptorPool = shadowDescPool; ai.descriptorSetCount = (uint32_t)f; ai.pSetLayouts = l.data();
        shadowDescSets.resize(f);
        vkAllocateDescriptorSets(d, &ai, shadowDescSets.data());

        shadowUBOBufs.resize(f); shadowUBOMems.resize(f); shadowUBOMapped.resize(f);
        for(int i=0; i<f; ++i) {
            engine.createBuffer(sizeof(glm::mat4) * 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, shadowUBOBufs[i], shadowUBOMems[i]);
            vkMapMemory(d, shadowUBOMems[i], 0, sizeof(glm::mat4) * 4, 0, &shadowUBOMapped[i]);
            VkDescriptorBufferInfo bi{shadowUBOBufs[i], 0, sizeof(glm::mat4) * 4};
            VkWriteDescriptorSet w{}; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = shadowDescSets[i]; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.dstBinding = 0; w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);
        }
    }

    {
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)f};
        VkDescriptorPoolCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; ci.maxSets = (uint32_t)f; ci.poolSizeCount = 1; ci.pPoolSizes = &ps;
        vkCreateDescriptorPool(d, &ci, nullptr, &geomDescPool);

        std::vector<VkDescriptorSetLayout> l(f, geomUBOLayout);
        VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; ai.descriptorPool = geomDescPool; ai.descriptorSetCount = (uint32_t)f; ai.pSetLayouts = l.data();
        geomDescSets.resize(f);
        vkAllocateDescriptorSets(d, &ai, geomDescSets.data());

        geomUBOBufs.resize(f); geomUBOMems.resize(f); geomUBOMapped.resize(f);
        for(int i=0; i<f; ++i) {
            engine.createBuffer(sizeof(GeomUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, geomUBOBufs[i], geomUBOMems[i]);
            vkMapMemory(d, geomUBOMems[i], 0, sizeof(GeomUBO), 0, &geomUBOMapped[i]);
            VkDescriptorBufferInfo bi{geomUBOBufs[i], 0, sizeof(GeomUBO)};
            VkWriteDescriptorSet w{}; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = geomDescSets[i]; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.dstBinding = 0; w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);
        }
    }
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

        VkDescriptorImageInfo si{shadowSampler, shadowArrayViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
