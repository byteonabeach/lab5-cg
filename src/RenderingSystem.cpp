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
    float prevTime;
};

struct TAAPush {
    float feedbackMin;
    float feedbackMax;
    int enabled;
    int reset;
    int showMotion;
};

struct DistortionPush {
    float time;
    float prevTime;
    float strength;
    float prevStrength;
    int writeVelocity;
};

static float halton(uint32_t index, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
    }
    return r;
}

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
    defaultMatSet = engine.createMaterialSet(engine.createWhiteTexture(), engine.createDefaultNormalTexture(), engine.createBlackTexture());

    createInstanceBuffers_(engine);
    createShadowResources_(engine);
    createShadowPipeline_(engine);
    createGeomPipeline_(engine);
    createDebugPipeline_(engine);
    createLightRenderPass_(engine);
    createLightPipeline_(engine);
    createTAAImages_(engine);
    createTAAPass_(engine);
    createTAAPipeline_(engine);
    createDistortionImages_(engine);
    createDistortionPass_(engine);
    createDistortionPipeline_(engine);
    createFramebuffers_(engine);
    createDescriptors_(engine);
    updateLightDescSets_(engine);
    updateTAADescSets_(engine);
    updateDistortionDescSet_(engine);

    createParticleResources_(engine);
    createParticlePipelines_(engine);
    createTerrainPipeline_(engine);

    marching.setVolume(glm::vec3(-32.0f, 24.0f, 6.0f), 0.5f);
    marching.init(engine, geomUBOLayout, gbuffer.getRenderPass(), shadowRenderPass, shadowDescLayout);
}

void RenderingSystem::cleanup(Engine& engine) {
    VkDevice dev = engine.getDevice();
    vkDeviceWaitIdle(dev);
    marching.cleanup(dev);
    cleanupFramebuffers_(dev);
    destroyTAAImages_(dev);
    vkDestroyPipeline(dev, taaPipeline, nullptr);
    vkDestroyPipelineLayout(dev, taaPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, taaDescLayout, nullptr);
    vkDestroyDescriptorPool(dev, taaDescPool, nullptr);
    vkDestroyRenderPass(dev, taaRenderPass, nullptr);
    vkDestroySampler(dev, taaLinearSampler, nullptr);
    destroyDistortionImages_(dev);
    vkDestroyPipeline(dev, distortionPipeline, nullptr);
    vkDestroyPipelineLayout(dev, distortionPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, distortionDescLayout, nullptr);
    vkDestroyDescriptorPool(dev, distortionDescPool, nullptr);
    vkDestroyRenderPass(dev, distortionRenderPass, nullptr);
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
    cleanupFramebuffers_(dev); destroyTAAImages_(dev); destroyDistortionImages_(dev); gbuffer.recreate(engine, ext.width, ext.height);
    createTAAImages_(engine); createDistortionImages_(engine); createFramebuffers_(engine); updateLightDescSets_(engine); updateTAADescSets_(engine); updateDistortionDescSet_(engine);
    taaReset = true; hasPrevViewProj = false;
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

    std::array<VkPipelineColorBlendAttachmentState, GBuffer::NUM_ATTACHMENTS> cba{};
    for (auto& a : cba) a.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = (uint32_t)cba.size();
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

    float frameDt = time - lastTime;
    if (frameDt <= 0.0f) frameDt = 0.016f;
    if (frameDt > 0.05f) frameDt = 0.05f;
    lastTime = time;
    const float prevTime = time - frameDt;

    glm::vec2 jitter(0.0f);
    if (taaEnabled) {
        uint32_t index = (taaFrame % 8) + 1;
        jitter = (glm::vec2(halton(index, 2), halton(index, 3)) - 0.5f) * 2.0f / glm::vec2((float)ext.width, (float)ext.height);
    }

    const glm::mat4 viewMat = camera.view();
    const glm::mat4 projMat = camera.projection((float)ext.width/(float)ext.height);
    glm::mat4 jitteredProj = projMat;
    jitteredProj[2][0] += jitter.x;
    jitteredProj[2][1] += jitter.y;
    const glm::mat4 viewProj = projMat * viewMat;
    if (!hasPrevViewProj) prevViewProj = viewProj;

    GeomUBO gubo{ viewMat, jitteredProj, glm::vec4(camera.position, 1.0f), viewProj, prevViewProj, glm::vec4(frameDt, 0.0f, 0.0f, 0.0f) };
    memcpy(geomUBOMapped[frameIndex], &gubo, sizeof(GeomUBO));
    LightsUBO lubo{ glm::vec4(camera.position, 1.0f), glm::vec4(0.08f, 0.08f, 0.10f, 1.0f), { (int)std::min((size_t)MAX_LIGHTS, pendingLights.size()), 0,0,0 }, glm::inverse(jitteredProj * viewMat), viewMat };

    bool shadowCaster = false;
    glm::mat4 invCam(1.0f);
    for (int i = 0; i < lubo.countPad.x; ++i) {
        lubo.lights[i] = pendingLights[i];
        if (lubo.lights[i].params2.x > 0.5f) {
            if (!shadowCaster) invCam = glm::inverse(viewProj);
            shadowCaster = true;
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
    batchLookup.clear();
    for (const auto* obj : objects) {
        const glm::vec4 instanceColor = obj->unlit ? obj->unlitColor : glm::vec4(1.0f);
        for (const auto& sm : obj->submeshes) {
            VkDescriptorSet effectiveMat = sm.matSet != VK_NULL_HANDLE ? sm.matSet : defaultMatSet;
            BatchKey key{sm.mesh.id, effectiveMat, obj->unlit, sm.isCurtain, obj->isTransparent, obj->unlitColor};
            auto it = batchLookup.find(key);
            if (it == batchLookup.end()) {
                it = batchLookup.emplace(key, activeBatches.size()).first;
                activeBatches.push_back({sm.mesh, effectiveMat, obj->unlit, obj->unlitColor, sm.dispScale, sm.isCurtain, obj->isTransparent, 0, 0, {}});
            }
            activeBatches[it->second].instances.push_back({obj->transform, instanceColor, obj->previousTransform()});
        }
    }

    InstanceData* mapped = (InstanceData*)instanceMapped[frameIndex];
    uint32_t currentOffset = 0;
    for (auto& b : activeBatches) {
        b.instanceOffset = currentOffset;
        b.instanceCount = std::min((uint32_t)b.instances.size(), (uint32_t)MAX_INSTANCES - currentOffset);
        memcpy(mapped + currentOffset, b.instances.data(), b.instanceCount * sizeof(InstanceData));
        currentOffset += b.instanceCount;
    }

    auto writeDebugBoxes = [&](const std::vector<AABB>& boxes, const glm::vec4& color) {
        uint32_t offset = currentOffset;
        for (const auto& b : boxes) {
            if (currentOffset >= MAX_INSTANCES) break;
            glm::mat4 m = glm::translate(glm::mat4(1.0f), (b.minVal + b.maxVal) * 0.5f) * glm::scale(glm::mat4(1.0f), (b.maxVal - b.minVal) * 0.5f);
            mapped[currentOffset++] = { m, color, m };
        }
        return offset;
    };
    uint32_t sOff = writeDebugBoxes(staticNodes, {0, 1, 0, 1}), sCnt = currentOffset - sOff;
    uint32_t dOff = writeDebugBoxes(dynamicNodes, {1, 0, 0, 1}), dCnt = currentOffset - dOff;

    int bufIn = particleFrameIndex % 2;
    int bufOut = (particleFrameIndex + 1) % 2;

    ParticlePush pPush{};
    pPush.dt = frameDt;
    pPush.time = time;
    pPush.spawnCount = 10;
    pPush.maxParticles = 50000;
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

    if (marchingEnabled) marching.generate(cmd, frameIndex, time, prevTime);

    if (shadowCaster) {
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
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 1, 1, &b.matSet, 0, nullptr);
            engine.bindAndDrawInstanced(cmd, b.mesh, instanceBufs[frameIndex], b.instanceOffset, b.instanceCount);
        }
        if (marchingEnabled) marching.drawShadow(cmd, frameIndex, shadowDescSets[frameIndex], defaultMatSet);
        vkCmdEndRenderPass(cmd);
    }

    std::array<VkClearValue, GBuffer::NUM_ATTACHMENTS + 1> clears{};
    for (int i = 0; i < GBuffer::NUM_ATTACHMENTS; ++i) clears[i].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clears[GBuffer::NUM_ATTACHMENTS].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpi{}; rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = gbuffer.getRenderPass(); rpi.framebuffer = gbuffer.getFramebuffer();
    rpi.renderArea = {{0,0}, ext}; rpi.clearValueCount = (uint32_t)clears.size(); rpi.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0,0,(float)ext.width,(float)ext.height, 0, 1}; vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0,0}, ext}; vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 0, 1, &geomDescSets[frameIndex], 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipeline);

    for (const auto& b : activeBatches) {
        GeomPC gpc{b.isUnlit?1:0, b.dispScale, time, b.isCurtain?1:0, b.isTransparent?1:0, prevTime};
        vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(GeomPC), &gpc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 1, 1, &b.matSet, 0, nullptr);
        engine.bindAndDrawInstanced(cmd, b.mesh, instanceBufs[frameIndex], b.instanceOffset, b.instanceCount);
    }

    if (terrain && terrain->isInitialized()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 0, 1, &geomDescSets[frameIndex], 0, nullptr);
        terrain->draw(cmd, terrainPipelineLayout, engine);
    }

    if (marchingEnabled) marching.drawGBuffer(cmd, frameIndex, geomDescSets[frameIndex]);

    if (debugCubeMesh.valid() && (sCnt > 0 || dCnt > 0)) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline);
        GeomPC gpc{1, 0, time, 0, 0, prevTime};
        vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(GeomPC), &gpc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 0, 1, &geomDescSets[frameIndex], 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 1, 1, &defaultMatSet, 0, nullptr);
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
    lrpi.renderPass = lightRenderPass; lrpi.framebuffer = lightFramebuffer;
    lrpi.renderArea = {{0,0}, ext}; lrpi.clearValueCount = 1; lrpi.pClearValues = &lc;
    vkCmdBeginRenderPass(cmd, &lrpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipelineLayout, 0, 1, &lightDescSets[frameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    const float distortionStrength = distortionEnabled ? 8.0f : 0.0f;
    VkRenderPassBeginInfo drpi{}; drpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    drpi.renderPass = distortionRenderPass; drpi.framebuffer = distortionFramebuffer;
    drpi.renderArea = {{0,0}, ext};
    vkCmdBeginRenderPass(cmd, &drpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, distortionPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, distortionPipelineLayout, 0, 1, &distortionDescSet, 0, nullptr);
    DistortionPush dpc{time, distortionPrevTime, distortionStrength, distortionPrevStrength, distortionVelocity ? 1 : 0};
    vkCmdPushConstants(cmd, distortionPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DistortionPush), &dpc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    const uint32_t historyIn = taaFrame & 1;
    VkRenderPassBeginInfo trpi{}; trpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    trpi.renderPass = taaRenderPass; trpi.framebuffer = taaFramebuffers[historyIn * engine.getSwapImageCount() + imageIndex];
    trpi.renderArea = {{0,0}, ext};
    vkCmdBeginRenderPass(cmd, &trpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taaPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taaPipelineLayout, 0, 1, &taaDescSets[historyIn], 0, nullptr);
    TAAPush tpc{0.80f, 0.95f, taaEnabled ? 1 : 0, taaReset ? 1 : 0, showMotionVectors ? 1 : 0};
    vkCmdPushConstants(cmd, taaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TAAPush), &tpc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    ++taaFrame;
    taaReset = false;
    prevViewProj = viewProj;
    hasPrevViewProj = true;
    distortionPrevTime = time;
    distortionPrevStrength = distortionStrength;

    if (enableParticles) {
        particleFrameIndex++;
    }
}

void RenderingSystem::createParticleResources_(Engine& engine) {
    uint32_t maxParticles = 50000;
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
    std::array<VkPipelineColorBlendAttachmentState, GBuffer::NUM_ATTACHMENTS> cba{}; for(auto& a:cba) a.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = (uint32_t)cba.size(); cb.pAttachments = cba.data();
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
    std::array<VkPipelineColorBlendAttachmentState, GBuffer::NUM_ATTACHMENTS> cba{}; for(auto& a:cba) a.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = (uint32_t)cba.size(); cb.pAttachments = cba.data();
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
    std::array<VkPipelineColorBlendAttachmentState, GBuffer::NUM_ATTACHMENTS> cba{}; for(auto& a:cba) a.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = (uint32_t)cba.size(); cb.pAttachments = cba.data();
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gci{}; gci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gci.stageCount = 2; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pDepthStencilState = &ds; gci.pColorBlendState = &cb; gci.pDynamicState = &dy; gci.layout = geomPipelineLayout; gci.renderPass = gbuffer.getRenderPass();
    vkCreateGraphicsPipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &gci, nullptr, &debugPipeline);
    vkDestroyShaderModule(engine.getDevice(), vs.module, nullptr); vkDestroyShaderModule(engine.getDevice(), fs.module, nullptr);
}

void RenderingSystem::createLightRenderPass_(Engine& engine) {
    VkAttachmentDescription att{}; att.format = SCENE_FORMAT; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    VkSubpassDependency deps[2] = {
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0}
    };
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
    VkFramebufferCreateInfo lfci{}; lfci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; lfci.renderPass = lightRenderPass; lfci.attachmentCount = 1; lfci.pAttachments = &sceneView; lfci.width = ext.width; lfci.height = ext.height; lfci.layers = 1;
    vkCreateFramebuffer(engine.getDevice(), &lfci, nullptr, &lightFramebuffer);

    VkImageView datts[2] = {distortedSceneView, distortedVelocityView};
    VkFramebufferCreateInfo dfci{}; dfci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; dfci.renderPass = distortionRenderPass; dfci.attachmentCount = 2; dfci.pAttachments = datts; dfci.width = ext.width; dfci.height = ext.height; dfci.layers = 1;
    vkCreateFramebuffer(engine.getDevice(), &dfci, nullptr, &distortionFramebuffer);

    taaFramebuffers.resize(2 * views.size());
    for (uint32_t parity = 0; parity < 2; ++parity) {
        for (size_t i = 0; i < views.size(); ++i) {
            VkImageView atts[2] = {views[i], historyViews[1 - parity]};
            VkFramebufferCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fci.renderPass = taaRenderPass; fci.attachmentCount = 2; fci.pAttachments = atts; fci.width = ext.width; fci.height = ext.height; fci.layers = 1;
            vkCreateFramebuffer(engine.getDevice(), &fci, nullptr, &taaFramebuffers[parity * views.size() + i]);
        }
    }
}

void RenderingSystem::cleanupFramebuffers_(VkDevice device) {
    vkDestroyFramebuffer(device, lightFramebuffer, nullptr);
    lightFramebuffer = VK_NULL_HANDLE;
    vkDestroyFramebuffer(device, distortionFramebuffer, nullptr);
    distortionFramebuffer = VK_NULL_HANDLE;
    for (auto fb : taaFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    taaFramebuffers.clear();
}

void RenderingSystem::createTAAImages_(Engine& engine) {
    auto ext = engine.getSwapExtent();
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    engine.createImage(ext.width, ext.height, 1, SCENE_FORMAT, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sceneImage, sceneMemory);
    sceneView = engine.createImageView(sceneImage, SCENE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_IMAGE_VIEW_TYPE_2D);

    for (int i = 0; i < 2; ++i) {
        engine.createImage(ext.width, ext.height, 1, SCENE_FORMAT, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, historyImages[i], historyMemories[i]);
        historyViews[i] = engine.createImageView(historyImages[i], SCENE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
        engine.transitionLayout(historyImages[i], 1, SCENE_FORMAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        engine.transitionLayout(historyImages[i], 1, SCENE_FORMAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void RenderingSystem::destroyTAAImages_(VkDevice device) {
    vkDestroyImageView(device, sceneView, nullptr);
    vkDestroyImage(device, sceneImage, nullptr);
    vkFreeMemory(device, sceneMemory, nullptr);
    sceneView = VK_NULL_HANDLE; sceneImage = VK_NULL_HANDLE; sceneMemory = VK_NULL_HANDLE;
    for (int i = 0; i < 2; ++i) {
        vkDestroyImageView(device, historyViews[i], nullptr);
        vkDestroyImage(device, historyImages[i], nullptr);
        vkFreeMemory(device, historyMemories[i], nullptr);
        historyViews[i] = VK_NULL_HANDLE; historyImages[i] = VK_NULL_HANDLE; historyMemories[i] = VK_NULL_HANDLE;
    }
}

void RenderingSystem::createTAAPass_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(dev, &si, nullptr, &taaLinearSampler);

    VkAttachmentDescription atts[2]{};
    atts[0].format = engine.getSwapFormat(); atts[0].samples = VK_SAMPLE_COUNT_1_BIT; atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    atts[1] = atts[0];
    atts[1].format = SCENE_FORMAT; atts[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference refs[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 2; sub.pColorAttachments = refs;
    VkSubpassDependency deps[2] = {
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0}
    };
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 2; rpci.pAttachments = atts; rpci.subpassCount = 1; rpci.pSubpasses = &sub; rpci.dependencyCount = 2; rpci.pDependencies = deps;
    vkCreateRenderPass(dev, &rpci, nullptr, &taaRenderPass);
}

void RenderingSystem::createTAAPipeline_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    std::array<VkDescriptorSetLayoutBinding, 4> b{};
    for (uint32_t i = 0; i < 4; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = (uint32_t)b.size(); lci.pBindings = b.data();
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &taaDescLayout);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 2; pci.poolSizeCount = 1; pci.pPoolSizes = &ps;
    vkCreateDescriptorPool(dev, &pci, nullptr, &taaDescPool);

    VkDescriptorSetLayout layouts[2] = {taaDescLayout, taaDescLayout};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = taaDescPool; ai.descriptorSetCount = 2; ai.pSetLayouts = layouts;
    vkAllocateDescriptorSets(dev, &ai, taaDescSets);

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TAAPush)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1; plci.pSetLayouts = &taaDescLayout; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &taaPipelineLayout);

    auto vs = loadShader_(engine, "shaders/lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fs = loadShader_(engine, "shaders/taa.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba[2]{}; cba[0].colorWriteMask = 0xF; cba[1].colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 2; cb.pAttachments = cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount = 2; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pColorBlendState = &cb; gci.pDynamicState = &dy; gci.layout = taaPipelineLayout; gci.renderPass = taaRenderPass;
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &taaPipeline);
    vkDestroyShaderModule(dev, vs.module, nullptr);
    vkDestroyShaderModule(dev, fs.module, nullptr);
}

void RenderingSystem::updateTAADescSets_(Engine& engine) {
    for (uint32_t p = 0; p < 2; ++p) {
        VkDescriptorImageInfo ii[4] = {
            {gbuffer.getSampler(), distortedSceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {taaLinearSampler, historyViews[p], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gbuffer.getSampler(), distortedVelocityView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gbuffer.getSampler(), gbuffer.getDepthView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
        };
        std::array<VkWriteDescriptorSet, 4> w{};
        for (uint32_t i = 0; i < 4; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = taaDescSets[p]; w[i].dstBinding = i; w[i].descriptorCount = 1; w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &ii[i];
        }
        vkUpdateDescriptorSets(engine.getDevice(), (uint32_t)w.size(), w.data(), 0, nullptr);
    }
}

void RenderingSystem::createDistortionImages_(Engine& engine) {
    auto ext = engine.getSwapExtent();
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    engine.createImage(ext.width, ext.height, 1, SCENE_FORMAT, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, distortedSceneImage, distortedSceneMemory);
    distortedSceneView = engine.createImageView(distortedSceneImage, SCENE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_IMAGE_VIEW_TYPE_2D);

    engine.createImage(ext.width, ext.height, 1, GBuffer::FORMAT_VELOCITY, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, distortedVelocityImage, distortedVelocityMemory);
    distortedVelocityView = engine.createImageView(distortedVelocityImage, GBuffer::FORMAT_VELOCITY, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
}

void RenderingSystem::destroyDistortionImages_(VkDevice device) {
    vkDestroyImageView(device, distortedSceneView, nullptr);
    vkDestroyImage(device, distortedSceneImage, nullptr);
    vkFreeMemory(device, distortedSceneMemory, nullptr);
    distortedSceneView = VK_NULL_HANDLE; distortedSceneImage = VK_NULL_HANDLE; distortedSceneMemory = VK_NULL_HANDLE;
    vkDestroyImageView(device, distortedVelocityView, nullptr);
    vkDestroyImage(device, distortedVelocityImage, nullptr);
    vkFreeMemory(device, distortedVelocityMemory, nullptr);
    distortedVelocityView = VK_NULL_HANDLE; distortedVelocityImage = VK_NULL_HANDLE; distortedVelocityMemory = VK_NULL_HANDLE;
}

void RenderingSystem::createDistortionPass_(Engine& engine) {
    VkAttachmentDescription atts[2]{};
    atts[0].format = SCENE_FORMAT; atts[0].samples = VK_SAMPLE_COUNT_1_BIT; atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[1] = atts[0];
    atts[1].format = GBuffer::FORMAT_VELOCITY;

    VkAttachmentReference refs[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 2; sub.pColorAttachments = refs;
    VkSubpassDependency deps[2] = {
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0}
    };
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 2; rpci.pAttachments = atts; rpci.subpassCount = 1; rpci.pSubpasses = &sub; rpci.dependencyCount = 2; rpci.pDependencies = deps;
    vkCreateRenderPass(engine.getDevice(), &rpci, nullptr, &distortionRenderPass);
}

void RenderingSystem::createDistortionPipeline_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    std::array<VkDescriptorSetLayoutBinding, 2> b{};
    for (uint32_t i = 0; i < 2; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = (uint32_t)b.size(); lci.pBindings = b.data();
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &distortionDescLayout);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1; pci.poolSizeCount = 1; pci.pPoolSizes = &ps;
    vkCreateDescriptorPool(dev, &pci, nullptr, &distortionDescPool);

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = distortionDescPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &distortionDescLayout;
    vkAllocateDescriptorSets(dev, &ai, &distortionDescSet);

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DistortionPush)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1; plci.pSetLayouts = &distortionDescLayout; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &distortionPipelineLayout);

    auto vs = loadShader_(engine, "shaders/lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fs = loadShader_(engine, "shaders/distortion.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba[2]{}; cba[0].colorWriteMask = 0xF; cba[1].colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 2; cb.pAttachments = cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount = 2; gci.pStages = stages; gci.pVertexInputState = &vi; gci.pInputAssemblyState = &ia; gci.pViewportState = &vp; gci.pRasterizationState = &rs; gci.pMultisampleState = &ms; gci.pColorBlendState = &cb; gci.pDynamicState = &dy; gci.layout = distortionPipelineLayout; gci.renderPass = distortionRenderPass;
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &distortionPipeline);
    vkDestroyShaderModule(dev, vs.module, nullptr);
    vkDestroyShaderModule(dev, fs.module, nullptr);
}

void RenderingSystem::updateDistortionDescSet_(Engine& engine) {
    VkDescriptorImageInfo ii[2] = {
        {taaLinearSampler, sceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {gbuffer.getSampler(), gbuffer.getVelocityView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    };
    std::array<VkWriteDescriptorSet, 2> w{};
    for (uint32_t i = 0; i < 2; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = distortionDescSet; w[i].dstBinding = i; w[i].descriptorCount = 1; w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(engine.getDevice(), (uint32_t)w.size(), w.data(), 0, nullptr);
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
