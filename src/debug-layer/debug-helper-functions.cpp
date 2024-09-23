#include "debug-helper-functions.h"
#include "../enum-strings.h"
#include "core/string.h"

#include <string>

namespace rhi::debug {

thread_local const char* _currentFunctionName = nullptr;

SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(Device)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL_PARENT(Buffer, Resource)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL_PARENT(Texture, Resource)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL_PARENT(TextureView, Resource)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(CommandQueue)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(InputLayout)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(Pipeline)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL_PARENT(Sampler, Resource)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(ShaderObject)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(ShaderProgram)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(Swapchain)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(QueryPool)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL_PARENT(AccelerationStructure, Resource)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(Fence)
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(ShaderTable)

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

std::string createBufferLabel(const BufferDesc& desc)
{
    return string::printf(
        "Unnamed buffer (size=%zu, elementSize=%zu, format=%s, memoryType=%s, usage=%s, defaultState=%s)",
        desc.size,
        desc.elementSize,
        enumToString(desc.format),
        enumToString(desc.memoryType),
        flagsToString(desc.usage),
        enumToString(desc.defaultState)
    );
}

std::string createTextureLabel(const TextureDesc& desc)
{
    return string::printf(
        "Unnamed texture (type=%s, size=%dx%dx%d, arrayLength=%d, numMipLevels=%d, sampleCount=%d, sampleQuality=%d, "
        "format=%s, memoryType=%s, usage=%s, defaultState=%s)",
        enumToString(desc.type),
        desc.size.width,
        desc.size.height,
        desc.size.depth,
        desc.arrayLength,
        desc.numMipLevels,
        desc.sampleCount,
        desc.sampleQuality,
        enumToString(desc.format),
        enumToString(desc.memoryType),
        flagsToString(desc.usage),
        enumToString(desc.defaultState)
    );
}

std::string createSamplerLabel(const SamplerDesc& desc)
{
    return string::printf(
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

void validateAccelerationStructureBuildInputs(const IAccelerationStructure::BuildInputs& buildInputs)
{
    switch (buildInputs.kind)
    {
    case IAccelerationStructure::Kind::TopLevel:
        if (!buildInputs.instanceDescs)
        {
            RHI_VALIDATION_WARNING(
                "IAccelerationStructure::BuildInputs::instanceDescs is null "
                "when creating a top-level acceleration structure."
            );
        }
        break;
    case IAccelerationStructure::Kind::BottomLevel:
        if (!buildInputs.geometryDescs)
        {
            RHI_VALIDATION_WARNING(
                "IAccelerationStructure::BuildInputs::geometryDescs is null "
                "when creating a bottom-level acceleration structure."
            );
        }
        for (int i = 0; i < buildInputs.descCount; i++)
        {
            switch (buildInputs.geometryDescs[i].type)
            {
            case IAccelerationStructure::GeometryType::Triangles:
                switch (buildInputs.geometryDescs[i].content.triangles.vertexFormat)
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
                        "Unsupported IAccelerationStructure::TriangleDesc::vertexFormat. Valid "
                        "values are R32G32B32_FLOAT, R32G32_FLOAT, R16G16B16A16_FLOAT, R16G16_FLOAT, "
                        "R16G16B16A16_SNORM or R16G16_SNORM."
                    );
                }
                if (buildInputs.geometryDescs[i].content.triangles.indexCount)
                {
                    switch (buildInputs.geometryDescs[i].content.triangles.indexFormat)
                    {
                    case Format::R32_UINT:
                    case Format::R16_UINT:
                        break;
                    default:
                        RHI_VALIDATION_ERROR(
                            "Unsupported IAccelerationStructure::TriangleDesc::indexFormat. Valid "
                            "values are Unknown, R32_UINT or R16_UINT."
                        );
                    }
                    if (!buildInputs.geometryDescs[i].content.triangles.indexData)
                    {
                        RHI_VALIDATION_ERROR(
                            "IAccelerationStructure::TriangleDesc::indexData cannot be null if "
                            "IAccelerationStructure::TriangleDesc::indexCount is not 0"
                        );
                    }
                }
                if (buildInputs.geometryDescs[i].content.triangles.indexFormat != Format::Unknown)
                {
                    if (buildInputs.geometryDescs[i].content.triangles.indexCount == 0)
                    {
                        RHI_VALIDATION_ERROR(
                            "IAccelerationStructure::TriangleDesc::indexCount cannot be 0 if "
                            "IAccelerationStructure::TriangleDesc::indexFormat is not Format::Unknown"
                        );
                    }
                    if (buildInputs.geometryDescs[i].content.triangles.indexData == 0)
                    {
                        RHI_VALIDATION_ERROR(
                            "IAccelerationStructure::TriangleDesc::indexData cannot be null if "
                            "IAccelerationStructure::TriangleDesc::indexFormat is not "
                            "Format::Unknown"
                        );
                    }
                }
                else
                {
                    if (buildInputs.geometryDescs[i].content.triangles.indexCount != 0)
                    {
                        RHI_VALIDATION_ERROR(
                            "IAccelerationStructure::TriangleDesc::indexCount must be 0 if "
                            "IAccelerationStructure::TriangleDesc::indexFormat is "
                            "Format::Unknown"
                        );
                    }
                    if (buildInputs.geometryDescs[i].content.triangles.indexData != 0)
                    {
                        RHI_VALIDATION_ERROR(
                            "IAccelerationStructure::TriangleDesc::indexData must be null if "
                            "IAccelerationStructure::TriangleDesc::indexFormat is "
                            "Format::Unknown"
                        );
                    }
                }
                if (!buildInputs.geometryDescs[i].content.triangles.vertexData)
                {
                    RHI_VALIDATION_ERROR("IAccelerationStructure::TriangleDesc::vertexData cannot be null.");
                }
                break;
            }
        }
        break;
    default:
        RHI_VALIDATION_ERROR("Invalid value of IAccelerationStructure::Kind.");
        break;
    }
}

} // namespace rhi::debug
