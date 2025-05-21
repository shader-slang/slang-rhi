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
        "(layer=%u, layerCount=%u, mip=%u, mipCount=%u)",
        range.layer,
        range.layerCount,
        range.mip,
        range.mipCount
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
        "Unnamed texture (type=%s, size=%dx%dx%d, arrayLength=%d, mipCount=%d, sampleCount=%d, sampleQuality=%d, "
        "format=%s, memoryType=%s, usage=%s, defaultState=%s)",
        enumToString(desc.type),
        desc.size.width,
        desc.size.height,
        desc.size.depth,
        desc.arrayLength,
        desc.mipCount,
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

std::string createAccelerationStructureLabel(const AccelerationStructureDesc& desc)
{
    return string::format("Unnamed acceleration structure (size=%llu)", desc.size);
}

std::string createFenceLabel(const FenceDesc& desc)
{
    return string::format(
        "Unnamed fence (initialValue=%llu, isShared=%s)",
        desc.initialValue,
        desc.isShared ? "true" : "false"
    );
}

std::string createQueryPoolLabel(const QueryPoolDesc& desc)
{
    return string::format("Unnamed query pool (type=%s, count=%u)", enumToString(desc.type), desc.count);
}


void validateAccelerationStructureBuildDesc(DebugContext* ctx, const AccelerationStructureBuildDesc& buildDesc)
{
    if (buildDesc.inputCount < 1)
    {
        RHI_VALIDATION_WARNING("AccelerationStructureBuildDesc::inputCount must be >= 1.");
    }

    AccelerationStructureBuildInputType type = buildDesc.inputs[0].type;
    for (uint32_t i = 1; i < buildDesc.inputCount; ++i)
    {
        if (type != buildDesc.inputs[i].type)
        {
            RHI_VALIDATION_WARNING("AccelerationStructureBuildDesc::inputs must have the same type.");
        }
    }

    for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
    {
        switch (buildDesc.inputs[i].type)
        {
        case AccelerationStructureBuildInputType::Instances:
        {
            const AccelerationStructureBuildInputInstances& instances = buildDesc.inputs[i].instances;
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
            const AccelerationStructureBuildInputTriangles& triangles = buildDesc.inputs[i].triangles;

            switch (triangles.vertexFormat)
            {
            case Format::RGB32Float:
            case Format::RG32Float:
            case Format::RGBA16Float:
            case Format::RG16Float:
            case Format::RGBA16Snorm:
            case Format::RG16Snorm:
                break;
            default:
                RHI_VALIDATION_ERROR(
                    "Unsupported vertexFormat. Valid values are RGB32Float, RG32Float, RGBA16Float, "
                    "RG16Float, RGBA16Snorm or RG16Snorm."
                );
            }
            if (triangles.indexCount)
            {
                switch (triangles.indexFormat)
                {
                case IndexFormat::Uint16:
                case IndexFormat::Uint32:
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
                buildDesc.inputs[i].proceduralPrimitives;
            SLANG_UNUSED(proceduralPrimitives);
            break;
        }
        case AccelerationStructureBuildInputType::Spheres:
        {
            const AccelerationStructureBuildInputSpheres& spheres = buildDesc.inputs[i].spheres;

            switch (spheres.vertexPositionFormat)
            {
            case Format::RGB32Float:
            case Format::RG32Float:
            case Format::RGBA16Float:
            case Format::RG16Float:
            case Format::RGBA16Snorm:
            case Format::RG16Snorm:
                break;
            default:
                RHI_VALIDATION_ERROR(
                    "Unsupported vertexPositionFormat. Valid values are RGB32Float, RG32Float, "
                    "RGBA16Float, "
                    "RG16Float, RGBA16Snorm or RG16Snorm."
                );
            }

            switch (spheres.vertexRadiusFormat)
            {
            case Format::R32Float:
            case Format::R16Float:
                break;
            default:
                RHI_VALIDATION_ERROR("Unsupported vertexRadiusFormat. Valid values are R32Float or R16Float.");
            }
            break;

            if (ctx->deviceType == DeviceType::CUDA)
            {
                if (spheres.vertexPositionFormat != Format::RGB32Float)
                {
                    RHI_VALIDATION_ERROR("OptiX requires vertexPositionFormat to be RGB32Float.");
                }
                if (spheres.vertexRadiusFormat != Format::R32Float)
                {
                    RHI_VALIDATION_ERROR("OptiX requires vertexRadiusFormat to be R32Float.");
                }
                if (spheres.indexBuffer)
                {
                    RHI_VALIDATION_ERROR("OptiX does not support indexBuffer.");
                }
            }
            break;
        }
        case AccelerationStructureBuildInputType::LinearSweptSpheres:
        {
            const AccelerationStructureBuildInputLinearSweptSpheres& linearSweptSpheres =
                buildDesc.inputs[i].linearSweptSpheres;
            SLANG_UNUSED(linearSweptSpheres);
            break;
        }
        default:
            RHI_VALIDATION_ERROR("Invalid AccelerationStructureBuildInputType.");
            break;
        }
    }
}

} // namespace rhi::debug
