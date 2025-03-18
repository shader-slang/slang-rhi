#include "wgpu-clear-engine.h"
#include "wgpu-texture.h"
#include "wgpu-helper-functions.h"
#include "wgpu-util.h"
#include "wgpu-api.h"
#include "wgpu-device.h"

#include "format-conversion.h"

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(resources);

namespace rhi::wgpu {

Result ClearEngine::initialize(Context* ctx)
{
    m_ctx = ctx;

    SLANG_RETURN_ON_FAIL(createBindGroupLayout());
    SLANG_RETURN_ON_FAIL(createPipelineLayout());
    SLANG_RETURN_ON_FAIL(createShaderModule());
    SLANG_RETURN_ON_FAIL(createPipelines());

    // Set workgroup sizes for different texture types
    m_workgroupSizes[size_t(TextureType::Texture1D)] = {256, 1, 1};
    m_workgroupSizes[size_t(TextureType::Texture1DArray)] = {256, 1, 1};
    m_workgroupSizes[size_t(TextureType::Texture2D)] = {32, 32, 1};
    m_workgroupSizes[size_t(TextureType::Texture2DArray)] = {32, 32, 1};
    m_workgroupSizes[size_t(TextureType::Texture3D)] = {8, 8, 8};
    m_workgroupSizes[size_t(TextureType::TextureCube)] = {32, 32, 1};
    m_workgroupSizes[size_t(TextureType::TextureCubeArray)] = {32, 32, 1};

    return SLANG_OK;
}

void ClearEngine::release()
{
    for (size_t textureType = 0; textureType < kTextureTypeCount; ++textureType)
    {
        for (size_t type = 0; type < kTypeCount; ++type)
        {
            if (m_clearPipelines[textureType][type])
            {
                m_ctx->api.wgpuComputePipelineRelease(m_clearPipelines[textureType][type]);
                m_clearPipelines[textureType][type] = nullptr;
            }
        }
    }

    if (m_bindGroupLayout)
    {
        m_ctx->api.wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }

    if (m_pipelineLayout)
    {
        m_ctx->api.wgpuPipelineLayoutRelease(m_pipelineLayout);
        m_pipelineLayout = nullptr;
    }

    if (m_shaderModule)
    {
        m_ctx->api.wgpuShaderModuleRelease(m_shaderModule);
        m_shaderModule = nullptr;
    }

    m_ctx = nullptr;
}

Result ClearEngine::createBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[2] = {};

    // Storage texture binding
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[0].storageTexture.format = WGPUTextureFormat_Undefined; // Will be set at runtime

    // Uniform buffer for parameters and clear value
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].buffer.type = WGPUBufferBindingType_Uniform;
    entries[1].buffer.minBindingSize = 32; // Size of Params + clear value

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.entryCount = 2;
    bindGroupLayoutDesc.entries = entries;

    m_bindGroupLayout = m_ctx->api.wgpuDeviceCreateBindGroupLayout(m_ctx->device, &bindGroupLayoutDesc);
    return m_bindGroupLayout ? SLANG_OK : SLANG_FAIL;
}

Result ClearEngine::createPipelineLayout()
{
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;

    m_pipelineLayout = m_ctx->api.wgpuDeviceCreatePipelineLayout(m_ctx->device, &pipelineLayoutDesc);
    return m_pipelineLayout ? SLANG_OK : SLANG_FAIL;
}

Result ClearEngine::createShaderModule()
{
    auto fs = cmrc::resources::get_filesystem();
    auto shader = fs.open("src/wgpu/shaders/clear-texture.wgsl");
    std::string shaderSource(shader.begin(), shader.end());

    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.chain.next = nullptr;
    WGPUStringView code = {};
    code.data = shaderSource.c_str();
    code.length = shaderSource.length();
    wgslDesc.code = code;

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    shaderDesc.label = "Clear Texture Shader";

    m_shaderModule = m_ctx->api.wgpuDeviceCreateShaderModule(m_ctx->device, &shaderDesc);
    return m_shaderModule ? SLANG_OK : SLANG_FAIL;
}

Result ClearEngine::createPipelines()
{
    static const char* textureTypeEntryPoints[] = {
        "clear_1d",
        "clear_1d_array",
        "clear_2d",
        "clear_2d_array",
        "clear_2d",
        "clear_2d_array",
        "clear_3d",
        "clear_cube",
        "clear_cube_array",
    };

    static const char* typeEntryPointSuffixes[] = {
        "_float",
        "_uint"
    };

    // Create a pipeline for each supported format
    static const WGPUTextureFormat kSupportedFormats[] = {
        WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureFormat_RGBA8Snorm,
        WGPUTextureFormat_RGBA8Uint,
        WGPUTextureFormat_RGBA8Sint,
        WGPUTextureFormat_RGBA16Float,
        WGPUTextureFormat_R32Uint,
        WGPUTextureFormat_R32Sint,
        WGPUTextureFormat_RG32Uint,
        WGPUTextureFormat_RG32Sint,
        WGPUTextureFormat_RGBA32Uint,
        WGPUTextureFormat_RGBA32Sint,
        WGPUTextureFormat_RGBA32Float,
    };

    for (size_t textureType = 0; textureType < kTextureTypeCount; ++textureType)
    {
        if (textureType == size_t(TextureType::Texture2DMS) ||
            textureType == size_t(TextureType::Texture2DMSArray))
        {
            continue;
        }

        for (size_t type = 0; type < kTypeCount; ++type)
        {
            for (auto format : kSupportedFormats)
            {
                // Create bind group layout for this format
                WGPUBindGroupLayoutEntry entries[2] = {};

                // Storage texture binding
                entries[0].binding = 0;
                entries[0].visibility = WGPUShaderStage_Compute;
                entries[0].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                entries[0].storageTexture.format = format;

                // Uniform buffer for parameters and clear value
                entries[1].binding = 1;
                entries[1].visibility = WGPUShaderStage_Compute;
                entries[1].buffer.type = WGPUBufferBindingType_Uniform;
                entries[1].buffer.minBindingSize = 32; // Size of Params + clear value

                WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
                bindGroupLayoutDesc.entryCount = 2;
                bindGroupLayoutDesc.entries = entries;

                WGPUBindGroupLayout bindGroupLayout = m_ctx->api.wgpuDeviceCreateBindGroupLayout(m_ctx->device, &bindGroupLayoutDesc);
                if (!bindGroupLayout)
                {
                    continue; // Skip unsupported format
                }

                // Create pipeline layout
                WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
                pipelineLayoutDesc.bindGroupLayoutCount = 1;
                pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;

                WGPUPipelineLayout pipelineLayout = m_ctx->api.wgpuDeviceCreatePipelineLayout(m_ctx->device, &pipelineLayoutDesc);
                if (!pipelineLayout)
                {
                    m_ctx->api.wgpuBindGroupLayoutRelease(bindGroupLayout);
                    continue;
                }

                // Create pipeline
                WGPUComputePipelineDescriptor pipelineDesc = {};
                pipelineDesc.layout = pipelineLayout;

                char entryPoint[128];
                snprintf(
                    entryPoint,
                    sizeof(entryPoint),
                    "%s%s",
                    textureTypeEntryPoints[textureType],
                    typeEntryPointSuffixes[type]
                );

                WGPUProgrammableStageDescriptor computeStage = {};
                computeStage.module = m_shaderModule;
                computeStage.entryPoint = entryPoint;
                pipelineDesc.compute = computeStage;

                m_clearPipelines[textureType][type] =
                    m_ctx->api.wgpuDeviceCreateComputePipeline(m_ctx->device, &pipelineDesc);

                m_ctx->api.wgpuPipelineLayoutRelease(pipelineLayout);
                m_ctx->api.wgpuBindGroupLayoutRelease(bindGroupLayout);

                if (!m_clearPipelines[textureType][type])
                {
                    return SLANG_FAIL;
                }
            }
        }
    }

    return SLANG_OK;
}

void ClearEngine::clearTextureUint(
    WGPUComputePassEncoder encoder,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    const uint32_t clearValue[4]
)
{
    clearTexture(encoder, texture, subresourceRange, Type::Uint, clearValue, sizeof(uint32_t[4]));
}

void ClearEngine::clearTextureFloat(
    WGPUComputePassEncoder encoder,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    const float clearValue[4]
)
{
    clearTexture(encoder, texture, subresourceRange, Type::Float, clearValue, sizeof(float[4]));
}

void ClearEngine::clearTexture(
    WGPUComputePassEncoder encoder,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    Type type,
    const void* clearValue,
    size_t clearValueSize
)
{
    TextureType textureType = texture->m_desc.type;
    if (textureType == TextureType::Texture2DMS || textureType == TextureType::Texture2DMSArray)
    {
        return;
    }

    // Create a uniform buffer for parameters and clear value
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(Params) + clearValueSize;
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer uniformBuffer = m_ctx->api.wgpuDeviceCreateBuffer(m_ctx->device, &bufferDesc);

    // Get the pipeline for this format
    WGPUTextureFormat format = translateTextureFormat(texture->m_desc.format);
    if (format == WGPUTextureFormat_Undefined)
    {
        m_ctx->api.wgpuBufferRelease(uniformBuffer);
        return;
    }

    // Check if the format is supported for storage textures
    bool isSupported =
        format == WGPUTextureFormat_RGBA8Unorm ||
        format == WGPUTextureFormat_RGBA8Snorm ||
        format == WGPUTextureFormat_RGBA8Uint ||
        format == WGPUTextureFormat_RGBA8Sint ||
        format == WGPUTextureFormat_RGBA16Float ||
        format == WGPUTextureFormat_R32Uint ||
        format == WGPUTextureFormat_R32Sint ||
        format == WGPUTextureFormat_RG32Uint ||
        format == WGPUTextureFormat_RG32Sint ||
        format == WGPUTextureFormat_RGBA32Uint ||
        format == WGPUTextureFormat_RGBA32Sint ||
        format == WGPUTextureFormat_RGBA32Float;

    if (!isSupported)
    {
        m_ctx->api.wgpuBufferRelease(uniformBuffer);
        return;
    }

    // Convert format to shader format index
    uint32_t shaderFormat;
    if (format == WGPUTextureFormat_RGBA8Unorm)
        shaderFormat = 0;
    else if (format == WGPUTextureFormat_RGBA8Snorm)
        shaderFormat = 1;
    else if (format == WGPUTextureFormat_RGBA8Uint)
        shaderFormat = 2;
    else if (format == WGPUTextureFormat_RGBA8Sint)
        shaderFormat = 3;
    else if (format == WGPUTextureFormat_RGBA16Float)
        shaderFormat = 4;
    else if (format == WGPUTextureFormat_R32Uint)
        shaderFormat = 5;
    else if (format == WGPUTextureFormat_R32Sint)
        shaderFormat = 6;
    else if (format == WGPUTextureFormat_RG32Uint)
        shaderFormat = 7;
    else if (format == WGPUTextureFormat_RG32Sint)
        shaderFormat = 8;
    else if (format == WGPUTextureFormat_RGBA32Uint)
        shaderFormat = 9;
    else if (format == WGPUTextureFormat_RGBA32Sint)
        shaderFormat = 10;
    else
        shaderFormat = 11; // RGBA32Float

    m_ctx->api.wgpuComputePassEncoderSetPipeline(encoder, m_clearPipelines[size_t(textureType)][size_t(type)]);

    auto& workgroupSize = m_workgroupSizes[size_t(textureType)];

    for (uint32_t mipOffset = 0; mipOffset < subresourceRange.mipLevelCount; ++mipOffset)
    {
        for (uint32_t layerOffset = 0; layerOffset < subresourceRange.layerCount; ++layerOffset)
        {
            uint32_t mipLevel = subresourceRange.mipLevel + mipOffset;
            uint32_t layer = subresourceRange.baseArrayLayer + layerOffset;
            Extents mipSize = calcMipSize(texture->m_desc.size, mipLevel);

            // Update uniform buffer with parameters and clear value
            Params params = {};
            params.width = mipSize.width;
            params.height = mipSize.height;
            params.depth = mipSize.depth;
            params.layer = layer;
            params.mipLevel = mipLevel;
            params.format = shaderFormat;

            m_ctx->api.wgpuQueueWriteBuffer(
                m_ctx->api.wgpuDeviceGetQueue(m_ctx->device),
                uniformBuffer,
                0,
                &params,
                sizeof(params)
            );

            m_ctx->api.wgpuQueueWriteBuffer(
                m_ctx->api.wgpuDeviceGetQueue(m_ctx->device),
                uniformBuffer,
                sizeof(params),
                clearValue,
                clearValueSize
            );

            // Create bind group for this dispatch
            WGPUBindGroupEntry entries[2] = {};

            // Storage texture view
            WGPUTextureViewDescriptor viewDesc = {};
            viewDesc.format = format;
            // For cube textures, treat them as 2D array textures
            viewDesc.dimension = WGPUTextureViewDimension_2DArray;
            viewDesc.baseMipLevel = mipLevel;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = layer;
            viewDesc.arrayLayerCount = 1;

            WGPUTextureView textureView = m_ctx->api.wgpuTextureCreateView(texture->m_texture, &viewDesc);
            entries[0].binding = 0;
            entries[0].textureView = textureView;

            // Uniform buffer
            entries[1].binding = 1;
            entries[1].buffer = uniformBuffer;
            entries[1].size = bufferDesc.size;

            // Create bind group layout for this format
            WGPUBindGroupLayoutEntry layoutEntries[2] = {};

            // Storage texture binding
            layoutEntries[0].binding = 0;
            layoutEntries[0].visibility = WGPUShaderStage_Compute;
            layoutEntries[0].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
            layoutEntries[0].storageTexture.format = format;

            // Uniform buffer for parameters and clear value
            layoutEntries[1].binding = 1;
            layoutEntries[1].visibility = WGPUShaderStage_Compute;
            layoutEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
            layoutEntries[1].buffer.minBindingSize = 32; // Size of Params + clear value

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
            bindGroupLayoutDesc.entryCount = 2;
            bindGroupLayoutDesc.entries = layoutEntries;

            WGPUBindGroupLayout bindGroupLayout = m_ctx->api.wgpuDeviceCreateBindGroupLayout(m_ctx->device, &bindGroupLayoutDesc);

            WGPUBindGroupDescriptor bindGroupDesc = {};
            bindGroupDesc.layout = bindGroupLayout;
            bindGroupDesc.entryCount = 2;
            bindGroupDesc.entries = entries;

            WGPUBindGroup bindGroup = m_ctx->api.wgpuDeviceCreateBindGroup(m_ctx->device, &bindGroupDesc);
            m_ctx->api.wgpuComputePassEncoderSetBindGroup(encoder, 0, bindGroup, 0, nullptr);

            // Dispatch
            uint32_t workgroupsX = (params.width + workgroupSize.x - 1) / workgroupSize.x;
            uint32_t workgroupsY = (params.height + workgroupSize.y - 1) / workgroupSize.y;
            uint32_t workgroupsZ = (params.depth + workgroupSize.z - 1) / workgroupSize.z;

            m_ctx->api.wgpuComputePassEncoderDispatchWorkgroups(
                encoder,
                workgroupsX,
                workgroupsY,
                workgroupsZ
            );

            // Cleanup
            m_ctx->api.wgpuTextureViewRelease(textureView);
            m_ctx->api.wgpuBindGroupRelease(bindGroup);
            m_ctx->api.wgpuBindGroupLayoutRelease(bindGroupLayout);
        }
    }

    m_ctx->api.wgpuBufferRelease(uniformBuffer);
}

} // namespace rhi::wgpu
