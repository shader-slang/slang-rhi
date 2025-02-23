#include "debug-helper-functions.h"
#include "../enum-strings.h"
#include "core/string.h"

#include <string>

namespace rhi::debug {

thread_local const char* _currentFunctionName = nullptr;

SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(Device)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(CommandQueue)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(CommandEncoder)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(RenderPassEncoder)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(ComputePassEncoder)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(RayTracingPassEncoder)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(CommandBuffer)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(ShaderObject)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(Surface)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(QueryPool)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(Fence)

std::string _rhiGetFuncName(const char* input)
{
    std::string_view str(input);
    auto prefixIndex = str.find("Debug");
    if (prefixIndex == std::string::npos)
        return input;
    auto endIndex = str.find_last_of('(');
    if (endIndex == std::string::npos)
        endIndex = str.length();
    auto startIndex = prefixIndex + 5;
    return 'I' + std::string(str.substr(startIndex, endIndex - startIndex));
}

std::string subresourceRangeToString(const SubresourceRange& range)
{
    return string::format(
        "(mipLevel=%u, mipLevelCount=%u, baseArrayLayer=%u, layerCount=%u)",
        range.mipLevel,
        range.mipLevelCount,
        range.baseArrayLayer,
        range.layerCount
    );
}

std::string createBufferLabel(const BufferDesc& desc)
{
    return string::format(
        "Unnamed buffer (size=%zu, elementSize=%u, format=%s, memoryType=%s, usage=%s, defaultState=%s)",
        desc.size,
        desc.elementSize,
        enumToString(desc.format),
        enumToString(desc.memoryType),
        flagsToString(desc.usage).c_str(),
        enumToString(desc.defaultState)
    );
}

std::string createTextureLabel(const TextureDesc& desc)
{
    return string::format(
        "Unnamed texture (type=%s, size=%dx%dx%d, arrayLength=%d, mipLevelCount=%d, sampleCount=%d, sampleQuality=%d, "
        "format=%s, memoryType=%s, usage=%s, defaultState=%s)",
        enumToString(desc.type),
        desc.size.width,
        desc.size.height,
        desc.size.depth,
        desc.arrayLength,
        desc.mipLevelCount,
        desc.sampleCount,
        desc.sampleQuality,
        enumToString(desc.format),
        enumToString(desc.memoryType),
        flagsToString(desc.usage),
        enumToString(desc.defaultState)
    );
}

std::string createTextureViewLabel(const TextureViewDesc& desc)
{
    return string::format(
        "Unnamed texture view (format=%s, aspect=%s, subresourceRange=%s)",
        enumToString(desc.format),
        enumToString(desc.aspect),
        subresourceRangeToString(desc.subresourceRange).c_str()
    );
}

std::string createSamplerLabel(const SamplerDesc& desc)
{
    return string::format(
        "Unnamed sampler (minFilter=%s, magFilter=%s, mipFilter=%s, reductionOp=%s, addressU=%s, addressV=%s, "
        "addressW=%s, mipLODBias=%.1f, maxAnisotropy=%u, comparisonFunc=%s, borderColor=[%.1f, %.1f, %.1f, %.1f], "
        "minLOD=%.1f, maxLOD=%.1f)",
        enumToString(desc.minFilter),
        enumToString(desc.magFilter),
        enumToString(desc.mipFilter),
        enumToString(desc.reductionOp),
        enumToString(desc.addressU),
        enumToString(desc.addressV),
        enumToString(desc.addressW),
        desc.mipLODBias,
        desc.maxAnisotropy,
        enumToString(desc.comparisonFunc),
        desc.borderColor[0],
        desc.borderColor[1],
        desc.borderColor[2],
        desc.borderColor[3],
        desc.minLOD,
        desc.maxLOD
    );
}

void validateAccelerationStructureBuildDesc(DebugContext* ctx, const AccelerationStructureBuildDesc& buildDesc)
{
    if (buildDesc.inputCount < 1)
    {
        RHI_VALIDATION_WARNING("AccelerationStructureBuildDesc::inputCount must be >= 1.");
    }

    AccelerationStructureBuildInputType type = (AccelerationStructureBuildInputType&)buildDesc.inputs[0];
    for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
    {
        if (type != (AccelerationStructureBuildInputType&)buildDesc.inputs[i])
        {
            RHI_VALIDATION_WARNING("AccelerationStructureBuildDesc::inputs must have the same type.");
        }
    }

    for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
    {
        switch ((AccelerationStructureBuildInputType&)buildDesc.inputs[i])
        {
        case AccelerationStructureBuildInputType::Instances:
        {
            const AccelerationStructureBuildInputInstances& instances =
                (const AccelerationStructureBuildInputInstances&)buildDesc.inputs[i];
            if (instances.instanceCount < 1)
            {
                RHI_VALIDATION_ERROR("instanceCount must be >= 1.");
            }
            if (!instances.instanceBuffer.buffer)
            {
                RHI_VALIDATION_ERROR("instanceBuffer cannot be null.");
            }
            if (instances.instanceStride == 0)
            {
                RHI_VALIDATION_ERROR("instanceStride cannot be 0.");
            }
            break;
        }
        case AccelerationStructureBuildInputType::Triangles:
        {
            const AccelerationStructureBuildInputTriangles& triangles =
                (const AccelerationStructureBuildInputTriangles&)buildDesc.inputs[i];

            switch (triangles.vertexFormat)
            {
            case Format::R32G32B32_FLOAT:
            case Format::R32G32_FLOAT:
            case Format::R16G16B16A16_FLOAT:
            case Format::R16G16_FLOAT:
            case Format::R16G16B16A16_SNORM:
            case Format::R16G16_SNORM:
                break;
            default:
                RHI_VALIDATION_ERROR(
                    "Unsupported vertexFormat. Valid values are R32G32B32_FLOAT, R32G32_FLOAT, R16G16B16A16_FLOAT, "
                    "R16G16_FLOAT, R16G16B16A16_SNORM or R16G16_SNORM."
                );
            }
            if (triangles.indexCount)
            {
                switch (triangles.indexFormat)
                {
                case IndexFormat::UInt16:
                case IndexFormat::UInt32:
                    break;
                default:
                    RHI_VALIDATION_ERROR("Unsupported indexFormat. Valid values are Uint16 and Uint32.");
                }
                if (!triangles.indexBuffer.buffer)
                {
                    RHI_VALIDATION_ERROR("indexBuffer cannot be null if indexCount is not 0.");
                }
            }
            if (triangles.vertexBufferCount < 1)
            {
                RHI_VALIDATION_ERROR("vertexBufferCount cannot be <= 1.");
            }
            for (uint32_t j = 0; j < triangles.vertexBufferCount; ++j)
            {
                if (!triangles.vertexBuffers[j].buffer)
                {
                    RHI_VALIDATION_ERROR("vertexBuffers cannot be null.");
                }
            }
            break;
        }
        case AccelerationStructureBuildInputType::ProceduralPrimitives:
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                (const AccelerationStructureBuildInputProceduralPrimitives&)buildDesc.inputs[i];
            SLANG_UNUSED(proceduralPrimitives);
            break;
        }
        default:
            RHI_VALIDATION_ERROR("Invalid AccelerationStructureBuildInputType.");
            break;
        }
    }
}

} // namespace rhi::debug
