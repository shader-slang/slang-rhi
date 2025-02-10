#include "vk-pipeline.h"
#include "vk-device.h"
#include "vk-helper-functions.h"
#include "vk-shader-object-layout.h"
#include "vk-shader-program.h"
#include "vk-input-layout.h"

#include "core/static_vector.h"

#include <map>
#include <string>
#include <vector>

namespace rhi::vk {

RenderPipelineImpl::~RenderPipelineImpl()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        m_device->m_api.vkDestroyPipeline(m_device->m_api.m_device, m_pipeline, nullptr);
    }
}

Result RenderPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkPipeline;
    outHandle->value = (uint64_t)m_pipeline;
    return SLANG_OK;
}

Result DeviceImpl::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_stageCreateInfos.empty());
    InputLayoutImpl* inputLayout = checked_cast<InputLayoutImpl*>(desc.inputLayout);

    // VertexBuffer/s
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    if (inputLayout)
    {
        const auto& srcAttributeDescs = inputLayout->m_attributeDescs;
        const auto& srcStreamDescs = inputLayout->m_streamDescs;

        vertexInputInfo.vertexBindingDescriptionCount = (uint32_t)srcStreamDescs.size();
        vertexInputInfo.pVertexBindingDescriptions = srcStreamDescs.data();

        vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)srcAttributeDescs.size();
        vertexInputInfo.pVertexAttributeDescriptions = srcAttributeDescs.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    // All other forms of primitive toplogies are specified via dynamic state.
    inputAssembly.topology = VulkanUtil::translatePrimitiveListTopology(desc.primitiveTopology);
    inputAssembly.primitiveRestartEnable = VK_FALSE; // TODO: Currently unsupported

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    // We are using dynamic viewport and scissor state.
    // Here we specify an arbitrary size, actual viewport will be set at `beginRenderPass`
    // time.
    viewport.width = 16.0f;
    viewport.height = 16.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {uint32_t(16), uint32_t(16)};

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    auto rasterizerDesc = desc.rasterizer;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_TRUE; // TODO: Depth clipping and clamping are different between Vk and D3D12
    rasterizer.rasterizerDiscardEnable = VK_FALSE; // TODO: Currently unsupported
    rasterizer.polygonMode = VulkanUtil::translateFillMode(rasterizerDesc.fillMode);
    rasterizer.cullMode = VulkanUtil::translateCullMode(rasterizerDesc.cullMode);
    rasterizer.frontFace = VulkanUtil::translateFrontFaceMode(rasterizerDesc.frontFace);
    rasterizer.depthBiasEnable = (rasterizerDesc.depthBias == 0) ? VK_FALSE : VK_TRUE;
    rasterizer.depthBiasConstantFactor = (float)rasterizerDesc.depthBias;
    rasterizer.depthBiasClamp = rasterizerDesc.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = rasterizerDesc.slopeScaledDepthBias;
    rasterizer.lineWidth = 1.0f; // TODO: Currently unsupported

    VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterInfo = {};
    conservativeRasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
    conservativeRasterInfo.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
    if (desc.rasterizer.enableConservativeRasterization)
    {
        rasterizer.pNext = &conservativeRasterInfo;
    }

    auto forcedSampleCount = rasterizerDesc.forcedSampleCount;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = (forcedSampleCount == 0) ? VkSampleCountFlagBits(desc.multisample.sampleCount)
                                                                  : VulkanUtil::translateSampleCount(forcedSampleCount);
    multisampling.sampleShadingEnable = VK_FALSE; // TODO: Should check if fragment shader needs this
    // TODO: Sample mask is dynamic in D3D12 but PSO state in Vulkan
    multisampling.alphaToCoverageEnable = desc.multisample.alphaToCoverageEnable;
    multisampling.alphaToOneEnable = desc.multisample.alphaToOneEnable;

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendTargets;

    // Regardless of whether blending is enabled, Vulkan always applies the color write mask
    // operation, so if there is no blending then we need to add an attachment that defines
    // the color write mask to ensure colors are actually written.
    if (desc.targetCount == 0)
    {
        colorBlendTargets.resize(1);
        auto& vkBlendDesc = colorBlendTargets[0];
        memset(&vkBlendDesc, 0, sizeof(vkBlendDesc));
        vkBlendDesc.blendEnable = VK_FALSE;
        vkBlendDesc.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        vkBlendDesc.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        vkBlendDesc.colorBlendOp = VK_BLEND_OP_ADD;
        vkBlendDesc.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        vkBlendDesc.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        vkBlendDesc.alphaBlendOp = VK_BLEND_OP_ADD;
        vkBlendDesc.colorWriteMask = (VkColorComponentFlags)RenderTargetWriteMask::EnableAll;
    }
    else
    {
        colorBlendTargets.resize(desc.targetCount);
        for (uint32_t i = 0; i < desc.targetCount; ++i)
        {
            auto& target = desc.targets[i];
            auto& vkBlendDesc = colorBlendTargets[i];

            vkBlendDesc.blendEnable = target.enableBlend;
            vkBlendDesc.srcColorBlendFactor = VulkanUtil::translateBlendFactor(target.color.srcFactor);
            vkBlendDesc.dstColorBlendFactor = VulkanUtil::translateBlendFactor(target.color.dstFactor);
            vkBlendDesc.colorBlendOp = VulkanUtil::translateBlendOp(target.color.op);
            vkBlendDesc.srcAlphaBlendFactor = VulkanUtil::translateBlendFactor(target.alpha.srcFactor);
            vkBlendDesc.dstAlphaBlendFactor = VulkanUtil::translateBlendFactor(target.alpha.dstFactor);
            vkBlendDesc.alphaBlendOp = VulkanUtil::translateBlendOp(target.alpha.op);
            vkBlendDesc.colorWriteMask = (VkColorComponentFlags)target.writeMask;
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE; // TODO: D3D12 has per attachment logic op (and
                                            // both have way more than one op)
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = (uint32_t)colorBlendTargets.size();
    colorBlending.pAttachments = colorBlendTargets.data();
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    static_vector<VkDynamicState, 8> dynamicStates;
    dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
    dynamicStates.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
    dynamicStates.push_back(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
    VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo = {};
    depthStencilStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilStateInfo.depthTestEnable = desc.depthStencil.depthTestEnable ? 1 : 0;
    depthStencilStateInfo.back = VulkanUtil::translateStencilState(desc.depthStencil.backFace);
    depthStencilStateInfo.front = VulkanUtil::translateStencilState(desc.depthStencil.frontFace);
    depthStencilStateInfo.back.compareMask = desc.depthStencil.stencilReadMask;
    depthStencilStateInfo.back.writeMask = desc.depthStencil.stencilWriteMask;
    depthStencilStateInfo.front.compareMask = desc.depthStencil.stencilReadMask;
    depthStencilStateInfo.front.writeMask = desc.depthStencil.stencilWriteMask;
    depthStencilStateInfo.depthBoundsTestEnable = 0; // TODO: Currently unsupported
    depthStencilStateInfo.depthCompareOp = VulkanUtil::translateComparisonFunc(desc.depthStencil.depthFunc);
    depthStencilStateInfo.depthWriteEnable = desc.depthStencil.depthWriteEnable ? 1 : 0;
    depthStencilStateInfo.stencilTestEnable = desc.depthStencil.stencilEnable ? 1 : 0;

    VkPipelineRenderingCreateInfoKHR renderingInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    short_vector<VkFormat> colorAttachmentFormats;
    for (uint32_t i = 0; i < desc.targetCount; ++i)
    {
        colorAttachmentFormats.push_back(VulkanUtil::getVkFormat(desc.targets[i].format));
    }
    renderingInfo.colorAttachmentCount = colorAttachmentFormats.size();
    renderingInfo.pColorAttachmentFormats = colorAttachmentFormats.data();
    renderingInfo.depthAttachmentFormat = VulkanUtil::getVkFormat(desc.depthStencil.format);
    if (VulkanUtil::isStencilFormat(renderingInfo.depthAttachmentFormat))
    {
        renderingInfo.stencilAttachmentFormat = renderingInfo.depthAttachmentFormat;
    }

    VkGraphicsPipelineCreateInfo createInfo = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    createInfo.pNext = &renderingInfo;
    createInfo.stageCount = (uint32_t)program->m_stageCreateInfos.size();
    createInfo.pStages = program->m_stageCreateInfos.data();
    createInfo.pVertexInputState = &vertexInputInfo;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pViewportState = &viewportState;
    createInfo.pRasterizationState = &rasterizer;
    createInfo.pMultisampleState = &multisampling;
    createInfo.pColorBlendState = &colorBlending;
    createInfo.pDepthStencilState = &depthStencilStateInfo;
    createInfo.layout = program->m_rootShaderObjectLayout->m_pipelineLayout;
    createInfo.subpass = 0;
    createInfo.basePipelineHandle = VK_NULL_HANDLE;
    createInfo.pDynamicState = &dynamicStateInfo;

    VkPipeline vkPipeline = VK_NULL_HANDLE;

    if (m_pipelineCreationAPIDispatcher)
    {
        SLANG_RETURN_ON_FAIL(
            m_pipelineCreationAPIDispatcher
                ->createRenderPipeline(this, program->linkedProgram.get(), &createInfo, (void**)&vkPipeline)
        );
    }
    else
    {
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkCreateGraphicsPipelines(m_device, pipelineCache, 1, &createInfo, nullptr, &vkPipeline)
        );
    }

    RefPtr<RenderPipelineImpl> pipeline = new RenderPipelineImpl();
    pipeline->m_device = this;
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootShaderObjectLayout;
    pipeline->m_pipeline = vkPipeline;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

ComputePipelineImpl::~ComputePipelineImpl()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        m_device->m_api.vkDestroyPipeline(m_device->m_api.m_device, m_pipeline, nullptr);
    }
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkPipeline;
    outHandle->value = (uint64_t)m_pipeline;
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_stageCreateInfos.empty());
    VkPipeline vkPipeline = VK_NULL_HANDLE;

    VkComputePipelineCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    createInfo.stage = program->m_stageCreateInfos[0];
    createInfo.layout = program->m_rootShaderObjectLayout->m_pipelineLayout;

    if (m_pipelineCreationAPIDispatcher)
    {
        SLANG_RETURN_ON_FAIL(
            m_pipelineCreationAPIDispatcher
                ->createComputePipeline(this, program->linkedProgram.get(), &createInfo, (void**)&vkPipeline)
        );
    }
    else
    {
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkCreateComputePipelines(m_device, pipelineCache, 1, &createInfo, nullptr, &vkPipeline)
        );
    }

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl();
    pipeline->m_device = this;
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootShaderObjectLayout;
    pipeline->m_pipeline = vkPipeline;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

RayTracingPipelineImpl::~RayTracingPipelineImpl()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        m_device->m_api.vkDestroyPipeline(m_device->m_api.m_device, m_pipeline, nullptr);
    }
}

Result RayTracingPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkPipeline;
    outHandle->value = (uint64_t)m_pipeline;
    return SLANG_OK;
}

inline uint32_t findEntryPointIndexByName(
    const std::map<std::string, uint32_t>& entryPointNameToIndex,
    const char* name
)
{
    if (!name)
        return VK_SHADER_UNUSED_KHR;

    auto it = entryPointNameToIndex.find(name);
    if (it != entryPointNameToIndex.end())
        return it->second;
    // TODO: Error reporting?
    return VK_SHADER_UNUSED_KHR;
}


Result DeviceImpl::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_stageCreateInfos.empty());

    VkRayTracingPipelineCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    createInfo.pNext = nullptr;
    createInfo.flags = translateRayTracingPipelineFlags(desc.flags);

    createInfo.stageCount = (uint32_t)program->m_stageCreateInfos.size();
    createInfo.pStages = program->m_stageCreateInfos.data();

    // Build Dictionary from entry point name to entry point index (stageCreateInfos index)
    // for all hit shaders - findShaderIndexByName
    std::map<std::string, uint32_t> entryPointNameToIndex;

    std::map<std::string, uint32_t> shaderGroupNameToIndex;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroupInfos;
    for (uint32_t i = 0; i < createInfo.stageCount; ++i)
    {
        auto stageCreateInfo = program->m_stageCreateInfos[i];
        auto entryPointName = program->m_entryPointNames[i];
        entryPointNameToIndex.emplace(entryPointName, i);
        if (stageCreateInfo.stage & (VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                     VK_SHADER_STAGE_INTERSECTION_BIT_KHR))
            continue;

        VkRayTracingShaderGroupCreateInfoKHR shaderGroupInfo = {
            VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
        };
        shaderGroupInfo.pNext = nullptr;
        shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroupInfo.generalShader = i;
        shaderGroupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroupInfo.pShaderGroupCaptureReplayHandle = nullptr;

        // For groups with a single entry point, the group name is the entry point name.
        auto shaderGroupName = entryPointName;
        uint32_t shaderGroupIndex = (uint32_t)shaderGroupInfos.size();
        shaderGroupInfos.push_back(shaderGroupInfo);
        shaderGroupNameToIndex.emplace(shaderGroupName, shaderGroupIndex);
    }

    for (uint32_t i = 0; i < desc.hitGroupCount; ++i)
    {
        VkRayTracingShaderGroupCreateInfoKHR shaderGroupInfo = {
            VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
        };
        auto& groupDesc = desc.hitGroups[i];

        shaderGroupInfo.pNext = nullptr;
        shaderGroupInfo.type = groupDesc.intersectionEntryPoint
                                   ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR
                                   : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        shaderGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
        shaderGroupInfo.closestHitShader =
            findEntryPointIndexByName(entryPointNameToIndex, groupDesc.closestHitEntryPoint);
        shaderGroupInfo.anyHitShader = findEntryPointIndexByName(entryPointNameToIndex, groupDesc.anyHitEntryPoint);
        shaderGroupInfo.intersectionShader =
            findEntryPointIndexByName(entryPointNameToIndex, groupDesc.intersectionEntryPoint);
        shaderGroupInfo.pShaderGroupCaptureReplayHandle = nullptr;

        uint32_t shaderGroupIndex = (uint32_t)shaderGroupInfos.size();
        shaderGroupInfos.push_back(shaderGroupInfo);
        shaderGroupNameToIndex.emplace(groupDesc.hitGroupName, shaderGroupIndex);
    }

    createInfo.groupCount = (uint32_t)shaderGroupInfos.size();
    createInfo.pGroups = shaderGroupInfos.data();

    createInfo.maxPipelineRayRecursionDepth = desc.maxRecursion;

    createInfo.pLibraryInfo = nullptr;
    createInfo.pLibraryInterface = nullptr;

    createInfo.pDynamicState = nullptr;

    createInfo.layout = program->m_rootShaderObjectLayout->m_pipelineLayout;
    createInfo.basePipelineHandle = VK_NULL_HANDLE;
    createInfo.basePipelineIndex = 0;

    if (m_pipelineCreationAPIDispatcher)
    {
        m_pipelineCreationAPIDispatcher->beforeCreateRayTracingState(this, program->linkedProgram.get());
    }

    VkPipeline vkPipeline = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateRayTracingPipelinesKHR(
        m_device,
        VK_NULL_HANDLE,
        pipelineCache,
        1,
        &createInfo,
        nullptr,
        &vkPipeline
    ));

    if (m_pipelineCreationAPIDispatcher)
    {
        m_pipelineCreationAPIDispatcher->afterCreateRayTracingState(this, program->linkedProgram.get());
    }

    RefPtr<RayTracingPipelineImpl> pipeline = new RayTracingPipelineImpl();
    pipeline->m_device = this;
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootShaderObjectLayout;
    pipeline->m_pipeline = vkPipeline;
    pipeline->m_shaderGroupNameToIndex = std::move(shaderGroupNameToIndex);
    pipeline->m_shaderGroupCount = shaderGroupInfos.size();
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

} // namespace rhi::vk
