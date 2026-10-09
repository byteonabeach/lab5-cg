#include "MarchingCubes.h"
#include "MarchingCubesTable.h"
#include "GBuffer.h"
#include <array>
#include <cstring>

namespace {

constexpr VkDeviceSize kVertexStride = 48;
constexpr VkDeviceSize kHeaderSize = 16;

VkShaderModule loadModule(Engine& engine, const std::string& path) {
    return engine.createShaderModule(engine.readFile(path));
}

VkPipelineShaderStageCreateInfo stageInfo(VkShaderStageFlagBits stage, VkShaderModule module) {
    VkPipelineShaderStageCreateInfo si{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    si.stage = stage;
    si.module = module;
    si.pName = "main";
    return si;
}

}

void MarchingCubes::init(Engine& engine, VkDescriptorSetLayout geomUBOLayout, VkRenderPass gbufferPass, VkRenderPass shadowPass, VkDescriptorSetLayout shadowUBOLayout) {
    createBuffers_(engine);
    createDescriptors_(engine);
    createComputePipelines_(engine);
    createGBufferPipeline_(engine, geomUBOLayout, gbufferPass);
    createShadowPipeline_(engine, shadowUBOLayout, shadowPass);
}

void MarchingCubes::cleanup(VkDevice dev) {
    vkDestroyPipeline(dev, shadowPipeline, nullptr);
    vkDestroyPipelineLayout(dev, shadowLayout, nullptr);
    vkDestroyPipeline(dev, gbufferPipeline, nullptr);
    vkDestroyPipelineLayout(dev, gbufferLayout, nullptr);
    vkDestroyPipeline(dev, polygonizePipeline, nullptr);
    vkDestroyPipeline(dev, fieldPipeline, nullptr);
    vkDestroyPipelineLayout(dev, computeLayout, nullptr);
    vkDestroyDescriptorPool(dev, descPool, nullptr);
    vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
    for (size_t i = 0; i < vertexBufs.size(); ++i) {
        vkDestroyBuffer(dev, vertexBufs[i], nullptr);
        vkFreeMemory(dev, vertexMems[i], nullptr);
        vkDestroyBuffer(dev, fieldBufs[i], nullptr);
        vkFreeMemory(dev, fieldMems[i], nullptr);
    }
    vkDestroyBuffer(dev, triTableBuf, nullptr);
    vkFreeMemory(dev, triTableMem, nullptr);
}

void MarchingCubes::createBuffers_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    const auto table = mc::buildTriTable();
    const VkDeviceSize tableSize = sizeof(table);
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    engine.createBuffer(tableSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem);
    void* mapped;
    vkMapMemory(dev, stagingMem, 0, tableSize, 0, &mapped);
    memcpy(mapped, table.data(), tableSize);
    vkUnmapMemory(dev, stagingMem);
    engine.createBuffer(tableSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, triTableBuf, triTableMem);
    engine.copyBuffer(staging, triTableBuf, tableSize);
    vkDestroyBuffer(dev, staging, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);

    const int frames = Engine::MAX_FRAMES;
    vertexBufs.resize(frames); vertexMems.resize(frames);
    fieldBufs.resize(frames); fieldMems.resize(frames);
    const VkDeviceSize fieldSize = sizeof(float) * (GRID + 1) * (GRID + 1) * (GRID + 1);
    for (int i = 0; i < frames; ++i) {
        engine.createBuffer(kHeaderSize + kVertexStride * MAX_VERTICES,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBufs[i], vertexMems[i]);
        engine.createBuffer(fieldSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, fieldBufs[i], fieldMems[i]);
    }
}

void MarchingCubes::createDescriptors_(Engine& engine) {
    VkDevice dev = engine.getDevice();
    const int frames = Engine::MAX_FRAMES;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < 3; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;
    }
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = (uint32_t)bindings.size();
    lci.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &setLayout);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)(3 * frames)};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = (uint32_t)frames;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &ps;
    vkCreateDescriptorPool(dev, &pci, nullptr, &descPool);

    std::vector<VkDescriptorSetLayout> layouts(frames, setLayout);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descPool;
    ai.descriptorSetCount = (uint32_t)frames;
    ai.pSetLayouts = layouts.data();
    descSets.resize(frames);
    vkAllocateDescriptorSets(dev, &ai, descSets.data());

    for (int i = 0; i < frames; ++i) {
        std::array<VkDescriptorBufferInfo, 3> infos = {{
            {vertexBufs[i], 0, VK_WHOLE_SIZE},
            {fieldBufs[i], 0, VK_WHOLE_SIZE},
            {triTableBuf, 0, VK_WHOLE_SIZE}
        }};
        std::array<VkWriteDescriptorSet, 3> writes{};
        for (uint32_t b = 0; b < 3; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = descSets[i];
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].pBufferInfo = &infos[b];
        }
        vkUpdateDescriptorSets(dev, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }
}

void MarchingCubes::createComputePipelines_(Engine& engine) {
    VkDevice dev = engine.getDevice();

    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &computeLayout);

    VkShaderModule fieldModule = loadModule(engine, "shaders/mc_field.comp.spv");
    VkShaderModule polyModule = loadModule(engine, "shaders/mc_polygonize.comp.spv");

    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.layout = computeLayout;
    cpi.stage = stageInfo(VK_SHADER_STAGE_COMPUTE_BIT, fieldModule);
    vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &fieldPipeline);
    cpi.stage = stageInfo(VK_SHADER_STAGE_COMPUTE_BIT, polyModule);
    vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &polygonizePipeline);

    vkDestroyShaderModule(dev, fieldModule, nullptr);
    vkDestroyShaderModule(dev, polyModule, nullptr);
}

void MarchingCubes::createGBufferPipeline_(Engine& engine, VkDescriptorSetLayout geomUBOLayout, VkRenderPass pass) {
    VkDevice dev = engine.getDevice();

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t)};
    VkDescriptorSetLayout layouts[] = {geomUBOLayout, setLayout};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 2;
    plci.pSetLayouts = layouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &gbufferLayout);

    VkShaderModule vs = loadModule(engine, "shaders/marching.vert.spv");
    VkShaderModule fs = loadModule(engine, "shaders/marching.frag.spv");
    VkPipelineShaderStageCreateInfo stages[] = {stageInfo(VK_SHADER_STAGE_VERTEX_BIT, vs), stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fs)};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
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
    gci.layout = gbufferLayout;
    gci.renderPass = pass;
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &gbufferPipeline);

    vkDestroyShaderModule(dev, vs, nullptr);
    vkDestroyShaderModule(dev, fs, nullptr);
}

void MarchingCubes::createShadowPipeline_(Engine& engine, VkDescriptorSetLayout shadowUBOLayout, VkRenderPass pass) {
    VkDevice dev = engine.getDevice();

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t)};
    VkDescriptorSetLayout layouts[] = {shadowUBOLayout, engine.getMaterialLayout(), setLayout};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 3;
    plci.pSetLayouts = layouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &plci, nullptr, &shadowLayout);

    VkShaderModule vs = loadModule(engine, "shaders/marching_shadow.vert.spv");
    VkShaderModule gs = loadModule(engine, "shaders/shadows.geom.spv");
    VkShaderModule fs = loadModule(engine, "shaders/shadows.frag.spv");
    VkPipelineShaderStageCreateInfo stages[] = {
        stageInfo(VK_SHADER_STAGE_VERTEX_BIT, vs),
        stageInfo(VK_SHADER_STAGE_GEOMETRY_BIT, gs),
        stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fs)
    };

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 1.25f;
    rs.depthBiasSlopeFactor = 1.75f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.stageCount = 3;
    gci.pStages = stages;
    gci.pVertexInputState = &vi;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState = &vp;
    gci.pRasterizationState = &rs;
    gci.pMultisampleState = &ms;
    gci.pDepthStencilState = &ds;
    gci.pDynamicState = &dy;
    gci.layout = shadowLayout;
    gci.renderPass = pass;
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, nullptr, &shadowPipeline);

    vkDestroyShaderModule(dev, vs, nullptr);
    vkDestroyShaderModule(dev, gs, nullptr);
    vkDestroyShaderModule(dev, fs, nullptr);
}

void MarchingCubes::generate(VkCommandBuffer cmd, int frameIndex, float time, float prevTime) {
    const uint32_t header[4] = {0, 1, 0, 0};
    vkCmdUpdateBuffer(cmd, vertexBufs[frameIndex], 0, sizeof(header), header);

    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

    Push push{glm::vec4(center, cell), time, prevTime, ISO, GRID, MAX_VERTICES};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout, 0, 1, &descSets[frameIndex], 0, nullptr);
    vkCmdPushConstants(cmd, computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fieldPipeline);
    uint32_t fieldGroups = (GRID + 1 + 7) / 8;
    vkCmdDispatch(cmd, fieldGroups, fieldGroups, fieldGroups);

    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, polygonizePipeline);
    uint32_t cellGroups = GRID / 4;
    vkCmdDispatch(cmd, cellGroups, cellGroups, cellGroups);

    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void MarchingCubes::drawGBuffer(VkCommandBuffer cmd, int frameIndex, VkDescriptorSet geomSet) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline);
    VkDescriptorSet sets[] = {geomSet, descSets[frameIndex]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferLayout, 0, 2, sets, 0, nullptr);
    uint32_t maxVertices = MAX_VERTICES;
    vkCmdPushConstants(cmd, gbufferLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(maxVertices), &maxVertices);
    vkCmdDrawIndirect(cmd, vertexBufs[frameIndex], 0, 1, 16);
}

void MarchingCubes::drawShadow(VkCommandBuffer cmd, int frameIndex, VkDescriptorSet shadowSet, VkDescriptorSet materialSet) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    VkDescriptorSet sets[] = {shadowSet, materialSet, descSets[frameIndex]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowLayout, 0, 3, sets, 0, nullptr);
    uint32_t maxVertices = MAX_VERTICES;
    vkCmdPushConstants(cmd, shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(maxVertices), &maxVertices);
    vkCmdDrawIndirect(cmd, vertexBufs[frameIndex], 0, 1, 16);
}
