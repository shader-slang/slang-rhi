#include "vk-pipeline.h"
#include "vk-device.h"
#include "vk-shader-object-layout.h"
#include "vk-shader-program.h"
#include "vk-input-layout.h"
#include "vk-utils.h"

#include "core/static_vector.h"
#include "core/sha1.h"

#include <map>
#include <string>
#include <vector>

namespace rhi::vk {

// For pipeline caching, we use the VK_KHR_pipeline_binary extension.
// We serialize the pipeline binaries into a custom format that stores a number of pipeline binaries,
// each with a key and data size, along with the binary data itself.
// The format is laid out as follows:
// Header [PipelineCacheHeader] (12 bytes):
// - Magic number (4 bytes)
// - Version (4 bytes)
// - Number of binaries (4 bytes)
// Binary headers [PipelineCacheBinaryHeader] (44 bytes each):
// - Key size (4 bytes)
// - Key (32 bytes (VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR))
// - Data size (4 bytes)
// - Data offset (4 bytes, relative to the start of the blob)
// Binary data (variable size)

struct PipelineCacheHeader
{
    static constexpr uint32_t kMagic = 0x12345678;
    static constexpr uint32_t kVersion = 1;

    uint32_t magic;
    uint32_t version;
    uint32_t binaryCount;
};

struct PipelineCacheBinaryHeader
{
    uint32_t keySize;
    uint8_t key[VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR];
    uint32_t dataSize;
    uint32_t dataOffset;
};

// Create a pipeline cache key based on the device and pipeline create info.
// The key is a SHA1 hash that includes the adapter LUID, global pipeline key, and the pipeline create info key.
Result getPipelineCacheKey(DeviceImpl* device, void* createInfo, ISlangBlob** outBlob)
{
    auto& api = device->m_api;

    SHA1 sha1;
    // Hash adapter LUID.
    {
        const AdapterLUID& luid = device->getInfo().adapterLUID;
        sha1.update(luid.luid, sizeof(luid.luid));
    }
    // Hash global key.
    {
        VkPipelineBinaryKeyKHR pipelineKey = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR};
        SLANG_VK_RETURN_ON_FAIL(api.vkGetPipelineKeyKHR(device->m_device, nullptr, &pipelineKey));
        sha1.update(pipelineKey.key, pipelineKey.keySize);
    }
    // Hash pipeline key.
    {
        VkPipelineCreateInfoKHR pipelineCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_CREATE_INFO_KHR};
        pipelineCreateInfo.pNext = createInfo;
        VkPipelineBinaryKeyKHR pipelineKey = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR};
        SLANG_VK_RETURN_ON_FAIL(api.vkGetPipelineKeyKHR(device->m_device, &pipelineCreateInfo, &pipelineKey));
        sha1.update(pipelineKey.key, pipelineKey.keySize);
    }
    SHA1::Digest digest = sha1.getDigest();
    ComPtr<ISlangBlob> blob = OwnedBlob::create(digest.data(), digest.size());
    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

// Serialize a vulkan pipeline into a blob containing the pipeline binaries.
Result serializePipelineBinaries(DeviceImpl* device, VkPipeline pipeline, ISlangBlob** outBlob)
{
    auto& api = device->m_api;

    VkPipelineBinaryCreateInfoKHR binaryCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR};
    binaryCreateInfo.pipeline = pipeline;

    VkPipelineBinaryHandlesInfoKHR binaryHandlesInfo = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR};

    SLANG_VK_RETURN_ON_FAIL(
        api.vkCreatePipelineBinariesKHR(device->m_device, &binaryCreateInfo, nullptr, &binaryHandlesInfo)
    );

    short_vector<VkPipelineBinaryKHR> pipelineBinaries(binaryHandlesInfo.pipelineBinaryCount, VK_NULL_HANDLE);
    binaryHandlesInfo.pPipelineBinaries = pipelineBinaries.data();
    SLANG_VK_RETURN_ON_FAIL(
        api.vkCreatePipelineBinariesKHR(device->m_device, &binaryCreateInfo, nullptr, &binaryHandlesInfo)
    );

    // Compute total size of the cache data blob.
    size_t dataSize = sizeof(PipelineCacheHeader);
    dataSize += binaryHandlesInfo.pipelineBinaryCount * sizeof(PipelineCacheBinaryHeader);
    for (uint32_t i = 0; i < binaryHandlesInfo.pipelineBinaryCount; ++i)
    {
        VkPipelineBinaryDataInfoKHR binaryInfo = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_DATA_INFO_KHR};
        binaryInfo.pipelineBinary = pipelineBinaries[i];
        VkPipelineBinaryKeyKHR binaryKey = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR};
        size_t binaryDataSize = 0;
        SLANG_VK_RETURN_ON_FAIL(
            api.vkGetPipelineBinaryDataKHR(device->m_device, &binaryInfo, &binaryKey, &binaryDataSize, nullptr)
        );
        dataSize += binaryDataSize;
    }

    ComPtr<ISlangBlob> blob = OwnedBlob::create(dataSize);
    uint8_t* data = (uint8_t*)blob->getBufferPointer();
    uint8_t* dataPtr = data;

    // Write cache data header.
    PipelineCacheHeader* header = (PipelineCacheHeader*)dataPtr;
    header->magic = PipelineCacheHeader::kMagic;
    header->version = PipelineCacheHeader::kVersion;
    header->binaryCount = binaryHandlesInfo.pipelineBinaryCount;
    dataPtr += sizeof(PipelineCacheHeader);

    // Write binary data.
    uint32_t binaryDataOffset =
        sizeof(PipelineCacheHeader) + binaryHandlesInfo.pipelineBinaryCount * sizeof(PipelineCacheBinaryHeader);
    for (uint32_t i = 0; i < binaryHandlesInfo.pipelineBinaryCount; ++i)
    {
        VkPipelineBinaryDataInfoKHR binaryInfo = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_DATA_INFO_KHR};
        binaryInfo.pipelineBinary = pipelineBinaries[i];

        VkPipelineBinaryKeyKHR binaryKey = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR};
        size_t binaryDataSize = 0;
        SLANG_VK_RETURN_ON_FAIL(
            api.vkGetPipelineBinaryDataKHR(device->m_device, &binaryInfo, &binaryKey, &binaryDataSize, nullptr)
        );

        SLANG_VK_RETURN_ON_FAIL(api.vkGetPipelineBinaryDataKHR(
            device->m_device,
            &binaryInfo,
            &binaryKey,
            &binaryDataSize,
            data + binaryDataOffset
        ));

        PipelineCacheBinaryHeader* binaryHeader = (PipelineCacheBinaryHeader*)dataPtr;
        std::memset(binaryHeader->key, 0, sizeof(PipelineCacheBinaryHeader::key));
        std::memcpy(binaryHeader->key, binaryKey.key, binaryKey.keySize);
        binaryHeader->keySize = binaryKey.keySize;
        binaryHeader->dataSize = (uint32_t)binaryDataSize;
        binaryHeader->dataOffset = binaryDataOffset;
        dataPtr += sizeof(PipelineCacheBinaryHeader);

        binaryDataOffset += binaryDataSize;

        api.vkDestroyPipelineBinaryKHR(device->m_device, pipelineBinaries[i], nullptr);
    }

    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

// Deserialize a blob containing pipeline binaries into a vector of VkPipelineBinaryKHR handles.
// The caller is responsible for destroying the VkPipelineBinaryKHR handles after use.
Result deserializePipelineBinaries(DeviceImpl* device, ISlangBlob* blob, short_vector<VkPipelineBinaryKHR>& outBinaries)
{
    auto& api = device->m_api;

    size_t dataSize = blob->getBufferSize();
    const uint8_t* data = (const uint8_t*)blob->getBufferPointer();
    const uint8_t* dataPtr = data;
    if (dataSize < sizeof(PipelineCacheHeader))
    {
        return SLANG_FAIL;
    }

    const PipelineCacheHeader* header = (const PipelineCacheHeader*)dataPtr;
    if (header->magic != PipelineCacheHeader::kMagic || header->version != PipelineCacheHeader::kVersion ||
        header->binaryCount == 0)
    {
        return SLANG_FAIL;
    }
    dataPtr += sizeof(PipelineCacheHeader);

    short_vector<VkPipelineBinaryKeyKHR> binaryKeys(header->binaryCount, {VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR});
    short_vector<VkPipelineBinaryDataKHR> pipelineData(header->binaryCount, {});

    for (uint32_t i = 0; i < header->binaryCount; ++i)
    {
        const PipelineCacheBinaryHeader* binaryHeader = (const PipelineCacheBinaryHeader*)dataPtr;
        dataPtr += sizeof(PipelineCacheBinaryHeader);

        binaryKeys[i].keySize = binaryHeader->keySize;
        std::memcpy(binaryKeys[i].key, binaryHeader->key, binaryHeader->keySize);

        pipelineData[i].dataSize = binaryHeader->dataSize;
        pipelineData[i].pData = (void*)(data + binaryHeader->dataOffset);
    }

    VkPipelineBinaryKeysAndDataKHR binaryKeysAndData;
    binaryKeysAndData.binaryCount = header->binaryCount;
    binaryKeysAndData.pPipelineBinaryKeys = binaryKeys.data();
    binaryKeysAndData.pPipelineBinaryData = pipelineData.data();

    VkPipelineBinaryCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR};
    createInfo.pKeysAndDataInfo = &binaryKeysAndData;

    short_vector<VkPipelineBinaryKHR> binaries(header->binaryCount, VK_NULL_HANDLE);

    VkPipelineBinaryHandlesInfoKHR handlesInfo = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR};
    handlesInfo.pipelineBinaryCount = binaries.size();
    handlesInfo.pPipelineBinaries = binaries.data();

    SLANG_VK_RETURN_ON_FAIL(api.vkCreatePipelineBinariesKHR(device->m_device, &createInfo, nullptr, &handlesInfo));

    outBinaries = binaries;
    return SLANG_OK;
}

template<typename VkPipelineCreateInfo>
Result createPipelineWithCache(
    DeviceImpl* device,
    VkPipelineCreateInfo* createInfo,
    VkResult (*createPipelineFunc)(DeviceImpl* device, VkPipelineCreateInfo* createInfo, VkPipeline* outPipeline),
    VkPipeline* outPipeline,
    bool& outCached,
    size_t& outCacheSize
)
{
    auto& api = device->m_api;

    outCached = false;
    outCacheSize = 0;

    // Early out if cache is not enabled or the feature is not supported.
    if (!device->m_persistentPipelineCache || !api.m_extendedFeatures.pipelineBinaryFeatures.pipelineBinaries)
    {
        return createPipelineFunc(device, createInfo, outPipeline);
    }

    bool writeCache = true;
    ComPtr<ISlangBlob> pipelineCacheKey;
    ComPtr<ISlangBlob> pipelineCacheData;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // Create pipeline cache key.
    if (SLANG_FAILED(getPipelineCacheKey(device, createInfo, pipelineCacheKey.writeRef())))
    {
        device->printWarning("Failed to get pipeline cache key, disabling pipeline cache.");
        return createPipelineFunc(device, createInfo, outPipeline);
    }

    // Query pipeline cache.
    if (SLANG_FAILED(device->m_persistentPipelineCache->queryCache(pipelineCacheKey, pipelineCacheData.writeRef())))
    {
        pipelineCacheData = nullptr;
    }

    // Try create pipeline from cache.
    if (pipelineCacheData)
    {
        short_vector<VkPipelineBinaryKHR> pipelineBinaries;
        if (SLANG_SUCCEEDED(deserializePipelineBinaries(device, pipelineCacheData, pipelineBinaries)))
        {
            VkPipelineBinaryInfoKHR binaryInfo = {VK_STRUCTURE_TYPE_PIPELINE_BINARY_INFO_KHR};
            binaryInfo.binaryCount = (uint32_t)pipelineBinaries.size();
            binaryInfo.pPipelineBinaries = pipelineBinaries.data();
            binaryInfo.pNext = createInfo->pNext;
            createInfo->pNext = &binaryInfo;
            if (createPipelineFunc(device, createInfo, &pipeline) == VK_SUCCESS)
            {
                writeCache = false;
                outCached = true;
                outCacheSize = pipelineCacheData->getBufferSize();
            }
            else
            {
                createInfo->pNext = binaryInfo.pNext;
                pipeline = VK_NULL_HANDLE;
            }
            for (auto& binary : pipelineBinaries)
            {
                api.vkDestroyPipelineBinaryKHR(device->m_device, binary, nullptr);
            }
        }
        else
        {
            device->printWarning("Failed to deserialize pipeline binaries from cache, creating new pipeline.");
        }
    }

    // Create pipeline if not found in cache.
    if (!pipeline)
    {
        // To capture the pipeline data, we need to set the VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR flag
        // in VkPipelineCreateFlags2CreateInfoKHR. In some cases, the passed in createInfo already has a
        // VkPipelineCreateFlags2CreateInfoKHR in the chain, so we use that, otherwise create a new one on the stack.
        VkPipelineCreateFlags2CreateInfoKHR createFlags = {VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR};
        if (writeCache)
        {
            // Check createInfo chain for existing VkPipelineCreateFlags2CreateInfoKHR
            bool foundExistingCreateFlags = false;
            VkBaseInStructure* inStruct = (VkBaseInStructure*)createInfo->pNext;
            while (inStruct)
            {
                if (inStruct->sType == VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR)
                {
                    ((VkPipelineCreateFlags2CreateInfoKHR*)inStruct)->flags |=
                        VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR;
                    foundExistingCreateFlags = true;
                    break;
                }
                inStruct = (VkBaseInStructure*)inStruct->pNext;
            }
            // If not found, append VkPipelineCreateFlags2CreateInfoKHR on stack
            if (!foundExistingCreateFlags)
            {
                createFlags.flags = VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR;
                createFlags.pNext = createInfo->pNext;
                createInfo->pNext = &createFlags;
            }
        }
        SLANG_VK_RETURN_ON_FAIL(createPipelineFunc(device, createInfo, &pipeline));
    }

    // Write to the cache.
    if (writeCache)
    {
        if (SLANG_SUCCEEDED(serializePipelineBinaries(device, pipeline, pipelineCacheData.writeRef())))
        {
            device->m_persistentPipelineCache->writeCache(pipelineCacheKey, pipelineCacheData);
            outCacheSize = pipelineCacheData->getBufferSize();
        }
        else
        {
            device->printWarning("Failed to serialize pipeline binaries, cache write skipped.");
        }
    }

    // Release captured pipeline data.
    if (writeCache)
    {
        VkReleaseCapturedPipelineDataInfoKHR releaseInfo = {VK_STRUCTURE_TYPE_RELEASE_CAPTURED_PIPELINE_DATA_INFO_KHR};
        releaseInfo.pipeline = pipeline;
        SLANG_VK_RETURN_ON_FAIL(api.vkReleaseCapturedPipelineDataKHR(device->m_device, &releaseInfo, nullptr));
    }

    *outPipeline = pipeline;
    return SLANG_OK;
}

RenderPipelineImpl::RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc)
    : RenderPipeline(device, desc)
{
}

RenderPipelineImpl::~RenderPipelineImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (m_pipeline != VK_NULL_HANDLE)
    {
        device->m_api.vkDestroyPipeline(device->m_api.m_device, m_pipeline, nullptr);
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
    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());
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
    inputAssembly.topology = translatePrimitiveListTopology(desc.primitiveTopology);
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
    rasterizer.polygonMode = translateFillMode(rasterizerDesc.fillMode);
    rasterizer.cullMode = translateCullMode(rasterizerDesc.cullMode);
    rasterizer.frontFace = translateFrontFaceMode(rasterizerDesc.frontFace);
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
                                                                  : translateSampleCount(forcedSampleCount);
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
        vkBlendDesc.colorWriteMask = (VkColorComponentFlags)RenderTargetWriteMask::All;
    }
    else
    {
        colorBlendTargets.resize(desc.targetCount);
        for (uint32_t i = 0; i < desc.targetCount; ++i)
        {
            auto& target = desc.targets[i];
            auto& vkBlendDesc = colorBlendTargets[i];

            vkBlendDesc.blendEnable = target.enableBlend;
            vkBlendDesc.srcColorBlendFactor = translateBlendFactor(target.color.srcFactor);
            vkBlendDesc.dstColorBlendFactor = translateBlendFactor(target.color.dstFactor);
            vkBlendDesc.colorBlendOp = translateBlendOp(target.color.op);
            vkBlendDesc.srcAlphaBlendFactor = translateBlendFactor(target.alpha.srcFactor);
            vkBlendDesc.dstAlphaBlendFactor = translateBlendFactor(target.alpha.dstFactor);
            vkBlendDesc.alphaBlendOp = translateBlendOp(target.alpha.op);
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
    depthStencilStateInfo.back = translateStencilState(desc.depthStencil.backFace);
    depthStencilStateInfo.front = translateStencilState(desc.depthStencil.frontFace);
    depthStencilStateInfo.back.compareMask = desc.depthStencil.stencilReadMask;
    depthStencilStateInfo.back.writeMask = desc.depthStencil.stencilWriteMask;
    depthStencilStateInfo.front.compareMask = desc.depthStencil.stencilReadMask;
    depthStencilStateInfo.front.writeMask = desc.depthStencil.stencilWriteMask;
    depthStencilStateInfo.depthBoundsTestEnable = 0; // TODO: Currently unsupported
    depthStencilStateInfo.depthCompareOp = translateComparisonFunc(desc.depthStencil.depthFunc);
    depthStencilStateInfo.depthWriteEnable = desc.depthStencil.depthWriteEnable ? 1 : 0;
    depthStencilStateInfo.stencilTestEnable = desc.depthStencil.stencilEnable ? 1 : 0;

    VkPipelineRenderingCreateInfoKHR renderingInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    short_vector<VkFormat> colorAttachmentFormats;
    for (uint32_t i = 0; i < desc.targetCount; ++i)
    {
        colorAttachmentFormats.push_back(getVkFormat(desc.targets[i].format));
    }
    renderingInfo.colorAttachmentCount = colorAttachmentFormats.size();
    renderingInfo.pColorAttachmentFormats = colorAttachmentFormats.data();
    renderingInfo.depthAttachmentFormat = getVkFormat(desc.depthStencil.format);
    if (isStencilFormat(renderingInfo.depthAttachmentFormat))
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
    bool cached = false;
    size_t cacheSize = 0;
    SLANG_RETURN_ON_FAIL(
        createPipelineWithCache<VkGraphicsPipelineCreateInfo>(
            this,
            &createInfo,
            [](DeviceImpl* device, VkGraphicsPipelineCreateInfo* createInfo2, VkPipeline* pipeline) -> VkResult
            {
                return device->m_api
                    .vkCreateGraphicsPipelines(device->m_device, VK_NULL_HANDLE, 1, createInfo2, nullptr, pipeline);
            },
            &vkPipeline,
            cached,
            cacheSize
        )
    );

    _labelObject((uint64_t)vkPipeline, VK_OBJECT_TYPE_PIPELINE, desc.label);

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::Render,
            startTime,
            Timer::now(),
            cached,
            cacheSize
        );
    }

    RefPtr<RenderPipelineImpl> pipeline = new RenderPipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootShaderObjectLayout;
    pipeline->m_pipeline = vkPipeline;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

ComputePipelineImpl::ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc)
    : ComputePipeline(device, desc)
{
}

ComputePipelineImpl::~ComputePipelineImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (m_pipeline != VK_NULL_HANDLE)
    {
        device->m_api.vkDestroyPipeline(device->m_api.m_device, m_pipeline, nullptr);
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
    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());

    VkComputePipelineCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    createInfo.stage = program->m_stageCreateInfos[0];
    createInfo.layout = program->m_rootShaderObjectLayout->m_pipelineLayout;

    VkPipeline vkPipeline = VK_NULL_HANDLE;
    bool cached = false;
    size_t cacheSize = 0;
    SLANG_RETURN_ON_FAIL(
        createPipelineWithCache<VkComputePipelineCreateInfo>(
            this,
            &createInfo,
            [](DeviceImpl* device, VkComputePipelineCreateInfo* createInfo2, VkPipeline* pipeline) -> VkResult
            {
                return device->m_api
                    .vkCreateComputePipelines(device->m_device, VK_NULL_HANDLE, 1, createInfo2, nullptr, pipeline);
            },
            &vkPipeline,
            cached,
            cacheSize
        )
    );

    _labelObject((uint64_t)vkPipeline, VK_OBJECT_TYPE_PIPELINE, desc.label);

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::Compute,
            startTime,
            Timer::now(),
            cached,
            cacheSize
        );
    }

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootShaderObjectLayout;
    pipeline->m_pipeline = vkPipeline;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

RayTracingPipelineImpl::RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc)
    : RayTracingPipeline(device, desc)
{
}

RayTracingPipelineImpl::~RayTracingPipelineImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (m_pipeline != VK_NULL_HANDLE)
    {
        device->m_api.vkDestroyPipeline(device->m_api.m_device, m_pipeline, nullptr);
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
    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());

    VkRayTracingPipelineCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    createInfo.flags = translateRayTracingPipelineFlags(desc.flags);

    VkPipelineCreateFlags2CreateInfoKHR createFlags2Info = {VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR};
    createFlags2Info.flags = translateRayTracingPipelineFlags2(desc.flags);
    if (createFlags2Info.flags != createInfo.flags)
    {
        createInfo.flags = 0; // Unused
        createInfo.pNext = &createFlags2Info;
    }

    VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV clusterCreateInfo = {
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CLUSTER_ACCELERATION_STRUCTURE_CREATE_INFO_NV
    };
    if (is_set(desc.flags, RayTracingPipelineFlags::EnableClusters))
    {
        clusterCreateInfo.allowClusterAccelerationStructure = VK_TRUE;
        clusterCreateInfo.pNext = (void*)createInfo.pNext;
        createInfo.pNext = &clusterCreateInfo;
    }

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
        auto entryPointName = program->m_modules[i].entryPointName;
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

    VkPipeline vkPipeline = VK_NULL_HANDLE;
    bool cached = false;
    size_t cacheSize = 0;
    SLANG_RETURN_ON_FAIL(
        createPipelineWithCache<VkRayTracingPipelineCreateInfoKHR>(
            this,
            &createInfo,
            [](DeviceImpl* device, VkRayTracingPipelineCreateInfoKHR* createInfo2, VkPipeline* pipeline) -> VkResult
            {
                return device->m_api.vkCreateRayTracingPipelinesKHR(
                    device->m_device,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    1,
                    createInfo2,
                    nullptr,
                    pipeline
                );
            },
            &vkPipeline,
            cached,
            cacheSize
        )
    );

    _labelObject((uint64_t)vkPipeline, VK_OBJECT_TYPE_PIPELINE, desc.label);

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::RayTracing,
            startTime,
            Timer::now(),
            cached,
            cacheSize
        );
    }

    RefPtr<RayTracingPipelineImpl> pipeline = new RayTracingPipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootShaderObjectLayout;
    pipeline->m_pipeline = vkPipeline;
    pipeline->m_shaderGroupNameToIndex = std::move(shaderGroupNameToIndex);
    pipeline->m_shaderGroupCount = shaderGroupInfos.size();
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

} // namespace rhi::vk
