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
SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(Heap)

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

std::string createShaderProgramLabel(const ShaderProgramDesc& desc)
{
    std::string result = "Unnamed shader program";
    if (!desc.slangGlobalScope)
    {
        return result;
    }
    SlangUInt entryPointCount = desc.slangGlobalScope->getLayout()->getEntryPointCount();
    if (entryPointCount == 1)
    {
        result += " (entryPoint=";
    }
    else if (entryPointCount > 1)
    {
        result += " (entryPoints=[";
    }
    for (SlangUInt i = 0; i < entryPointCount; ++i)
    {
        auto entryPointLayout = desc.slangGlobalScope->getLayout()->getEntryPointByIndex(i);
        const char* entryPointName =
            entryPointLayout->getNameOverride() ? entryPointLayout->getNameOverride() : entryPointLayout->getName();
        if (i > 0)
        {
            result += ", ";
        }
        result += entryPointName;
    }
    if (entryPointCount == 1)
    {
        result += ")";
    }
    else if (entryPointCount > 1)
    {
        result += "])";
    }
    return result;
}

std::string createRenderPipelineLabel(const RenderPipelineDesc& desc)
{
    return string::format("Unnamed render pipeline (program=\"%s\")", desc.program->getDesc().label);
}

std::string createComputePipelineLabel(const ComputePipelineDesc& desc)
{
    return string::format("Unnamed compute pipeline (program=\"%s\")", desc.program->getDesc().label);
}

std::string createRayTracingPipelineLabel(const RayTracingPipelineDesc& desc)
{
    return string::format("Unnamed ray tracing pipeline (program=\"%s\")", desc.program->getDesc().label);
}

std::string createHeapLabel(const HeapDesc& desc)
{
    return string::format("Unnamed heap (memoryType=%s)", enumToString(desc.memoryType));
}

Result validateAccelerationStructureBuildDesc(DebugContext* ctx, const AccelerationStructureBuildDesc& buildDesc)
{
    if (buildDesc.inputCount < 1)
    {
        RHI_VALIDATION_WARNING("AccelerationStructureBuildDesc::inputCount must be >= 1.");
        return SLANG_E_INVALID_ARG;
    }

    AccelerationStructureBuildInputType type = buildDesc.inputs[0].type;
    for (uint32_t i = 1; i < buildDesc.inputCount; ++i)
    {
        if (type != buildDesc.inputs[i].type)
        {
            RHI_VALIDATION_WARNING("AccelerationStructureBuildDesc::inputs must have the same type.");
            return SLANG_E_INVALID_ARG;
        }
    }

    bool valid = true;

    for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
    {
        switch (buildDesc.inputs[i].type)
        {
        case AccelerationStructureBuildInputType::Instances:
        {
            const AccelerationStructureBuildInputInstances& instances = buildDesc.inputs[i].instances;
            if (instances.instanceCount < 1)
            {
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].instances.instanceCount must be >= 1.",
                    i
                );
                valid = false;
            }
            if (!instances.instanceBuffer.buffer)
            {
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].instances.instanceBuffer cannot be null.",
                    i
                );
                valid = false;
            }
            if (instances.instanceStride == 0)
            {
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].instances.instanceStride cannot be 0.",
                    i
                );
                valid = false;
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
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].triangles.vertexFormat is unsupported. "
                    "Valid values are RGB32Float, RG32Float, RGBA16Float, RG16Float, RGBA16Snorm or RG16Snorm.",
                    i
                );
                valid = false;
            }
            if (triangles.indexCount)
            {
                switch (triangles.indexFormat)
                {
                case IndexFormat::Uint16:
                case IndexFormat::Uint32:
                    break;
                default:
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].triangles.indexFormat is unsupported. "
                        "Valid values are Uint16 and Uint32.",
                        i
                    );
                    valid = false;
                }
                if (!triangles.indexBuffer.buffer)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].triangles.indexBuffer.buffer cannot be null if "
                        "indexCount is not 0.",
                        i
                    );
                    valid = false;
                }
            }
            if (triangles.vertexBufferCount < 1)
            {
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].triangles.vertexBufferCount cannot be <= 1.",
                    i
                );
                valid = false;
            }
            for (uint32_t j = 0; j < triangles.vertexBufferCount; ++j)
            {
                if (!triangles.vertexBuffers[j].buffer)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].triangles.vertexBuffers.buffer cannot be null.",
                        i
                    );
                    valid = false;
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
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].spheres.vertexPositionFormat is unsupported. "
                    "Valid values are RGB32Float, RG32Float, RGBA16Float, RG16Float, RGBA16Snorm or RG16Snorm.",
                    i
                );
                valid = false;
            }

            switch (spheres.vertexRadiusFormat)
            {
            case Format::R32Float:
            case Format::R16Float:
                break;
            default:
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].spheres.vertexRadiusFormat is unsupported. "
                    "Valid values are R32Float or R16Float.",
                    i
                );
                valid = false;
            }

            if (ctx->deviceType == DeviceType::CUDA)
            {
                if (spheres.vertexPositionFormat != Format::RGB32Float)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].spheres.vertexPositionFormat must be RGB32Float "
                        "(OptiX limitation).",
                        i
                    );
                    valid = false;
                }
                if (spheres.vertexRadiusFormat != Format::R32Float)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].spheres.vertexRadiusFormat must be R32Float "
                        "(OptiX limitation).",
                        i
                    );
                    valid = false;
                }
                if (spheres.indexBuffer)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].spheres.indexBuffer must not be used "
                        "(OptiX limitation).",
                        i
                    );
                    valid = false;
                }
            }
            break;
        }
        case AccelerationStructureBuildInputType::LinearSweptSpheres:
        {
            const AccelerationStructureBuildInputLinearSweptSpheres& linearSweptSpheres =
                buildDesc.inputs[i].linearSweptSpheres;

            if (ctx->deviceType == DeviceType::CUDA)
            {
                if (linearSweptSpheres.endCapsMode != LinearSweptSpheresEndCapsMode::Chained)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.endCapsMode must be Chained "
                        "(OptiX limitation).",
                        i
                    );
                    valid = false;
                }

                if (linearSweptSpheres.indexingMode != LinearSweptSpheresIndexingMode::Successive)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.indexingMode must be Successive "
                        "(OptiX limitation).",
                        i
                    );
                    valid = false;
                }

                if (linearSweptSpheres.indexFormat != IndexFormat::Uint32)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.indexFormat must be Uint32 "
                        "(OptiX limitation).",
                        i
                    );
                    valid = false;
                }

                if (!linearSweptSpheres.indexBuffer)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.indexBuffer must be set "
                        "(OptiX limitation).",
                        i
                    );
                    valid = false;
                }
            }

            switch (linearSweptSpheres.vertexPositionFormat)
            {
            case Format::RGB32Float:
                break;
            default:
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.vertexPositionFormat is not "
                    "supported. Valid values are RGB32Float.",
                    i
                );
                valid = false;
            }

            switch (linearSweptSpheres.vertexRadiusFormat)
            {
            case Format::R32Float:
                break;
            default:
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.vertexRadiusFormat is not "
                    "supported. Valid values are R32Float.",
                    i
                );
                valid = false;
            }

            if (linearSweptSpheres.indexBuffer)
            {
                if (linearSweptSpheres.indexingMode == LinearSweptSpheresIndexingMode::List)
                {
                    if (linearSweptSpheres.indexCount < linearSweptSpheres.primitiveCount * 2)
                    {
                        RHI_VALIDATION_ERROR_FORMAT(
                            "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.indexCount must be >= "
                            "primitiveCount * 2 when indexingMode is List.",
                            i
                        );
                        valid = false;
                    }
                }
                else if (linearSweptSpheres.indexingMode == LinearSweptSpheresIndexingMode::Successive)
                {
                    if (linearSweptSpheres.indexCount < linearSweptSpheres.primitiveCount)
                    {
                        RHI_VALIDATION_ERROR_FORMAT(
                            "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.indexCount must be >= "
                            "primitiveCount when indexingMode is Successive.",
                            i
                        );
                        valid = false;
                    }
                }
                else
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.indexingMode is not supported. "
                        "Valid values are List and Successive.",
                        i
                    );
                    valid = false;
                }
            }

            if (linearSweptSpheres.vertexBufferCount < 1)
            {
                RHI_VALIDATION_ERROR_FORMAT(
                    "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.vertexBufferCount cannot be <= 1.",
                    i
                );
                valid = false;
            }
            for (uint32_t j = 0; j < linearSweptSpheres.vertexBufferCount; ++j)
            {
                if (!linearSweptSpheres.vertexPositionBuffers[j].buffer)
                {
                    RHI_VALIDATION_ERROR_FORMAT(
                        "AccelerationStructureBuildDesc::inputs[%d].linearSweptSpheres.vertexBuffers[%d].buffer cannot "
                        "be null.",
                        i,
                        j
                    );
                    valid = false;
                }
            }

            break;
        }
        default:
            RHI_VALIDATION_ERROR_FORMAT(
                "AccelerationStructureBuildDesc::inputs[%d].type is not supported. ",
                "Valid values are Instances, Triangles, ProceduralPrimitives, Spheres and LinearSweptSpheres.",
                i
            );
            valid = false;
        }
    }

    return valid ? SLANG_OK : SLANG_E_INVALID_ARG;
}

Result validateClusterOperationParams(DebugContext* ctx, const ClusterOperationParams& params)
{
    bool valid = true;
    bool validateClasParams = false;
    bool validateBlasParams = false;

    switch (params.type)
    {
    case ClusterOperationType::MoveObjects:
        if (ctx->deviceType == DeviceType::CUDA)
        {
            RHI_VALIDATION_ERROR("ClusterOperationType::MoveObjects is not supported on CUDA (OptiX limitation)");
            valid = false;
        }
        break;
    case ClusterOperationType::CLASFromTriangles:
        validateClasParams = true;
        break;
    case ClusterOperationType::BLASFromCLAS:
        validateBlasParams = true;
        break;
    case ClusterOperationType::TemplatesFromTriangles:
        validateClasParams = true;
        break;
    case ClusterOperationType::CLASFromTemplates:
        validateClasParams = true;
        break;
    default:
        RHI_VALIDATION_ERROR("ClusterOperationParams::type is invalid");
        valid = false;
        break;
    }

    switch (params.mode)
    {
    case ClusterOperationMode::ImplicitDestinations:
        break;
    case ClusterOperationMode::ExplicitDestinations:
        break;
    case ClusterOperationMode::GetSizes:
        break;
    default:
        RHI_VALIDATION_ERROR("ClusterOperationParams::mode is invalid");
        valid = false;
        break;
    }

    if (validateClasParams)
    {
        if (params.clas.maxGeometryIndex > kClusterMaxGeometryIndex)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "ClusterOperationClasBuildParams::maxGeometryIndex (%d) cannot be greater than %d.",
                params.clas.maxGeometryIndex,
                kClusterMaxGeometryIndex
            );
            valid = false;
        }
        if (params.clas.maxUniqueGeometryCount > params.clas.maxTriangleCount)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "ClusterOperationClasBuildParams::maxUniqueGeometryCount (%d) cannot be greater than "
                "ClusterOperationClasBuildParams::maxTriangleCount (%d). Maximum 1 geometry per triangle.",
                params.clas.maxUniqueGeometryCount,
                params.clas.maxTriangleCount
            );
            valid = false;
        }
        if (params.clas.maxTriangleCount > kClusterMaxTriangleCount)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "ClusterOperationClasBuildParams::maxTriangleCount (%d) cannot be greater than %d.",
                params.clas.maxTriangleCount,
                kClusterMaxTriangleCount
            );
            valid = false;
        }
        if (params.clas.maxTriangleCount > params.clas.maxTotalTriangleCount)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "ClusterOperationClasBuildParams::maxTriangleCount (%d) cannot be greater than "
                "ClusterOperationClasBuildParams::maxTotalTriangleCount (%d).",
                params.clas.maxTriangleCount,
                params.clas.maxTotalTriangleCount
            );
            valid = false;
        }
        if (params.clas.maxVertexCount > kClusterMaxVertexCount)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "ClusterOperationClasBuildParams::maxVertexCount (%d) cannot be greater than %d.",
                params.clas.maxVertexCount,
                kClusterMaxVertexCount
            );
            valid = false;
        }
        if (params.clas.maxVertexCount > params.clas.maxTotalVertexCount)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "ClusterOperationClasBuildParams::maxVertexCount (%d) cannot be greater than "
                "ClusterOperationClasBuildParams::maxTotalVertexCount (%d).",
                params.clas.maxVertexCount,
                params.clas.maxTotalVertexCount
            );
            valid = false;
        }
    }

    if (validateBlasParams)
    {
        if (params.blas.maxClasCount > params.blas.maxTotalClasCount)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "ClusterOperationBlasBuildParams::maxClasCount (%d) cannot be greater than "
                "ClusterOperationBlasBuildParams::maxTotalClasCount (%d).",
                params.blas.maxClasCount,
                params.blas.maxTotalClasCount
            );
            valid = false;
        }
    }

    return valid ? SLANG_OK : SLANG_E_INVALID_ARG;
}

Result validateConvertCooperativeVectorMatrix(
    DebugContext* ctx,
    size_t dstBufferSize,
    const CooperativeVectorMatrixDesc* dstDescs,
    size_t srcBufferSize,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
)
{
    if (!dstDescs)
    {
        RHI_VALIDATION_ERROR("Destination descriptions must be valid");
        return SLANG_E_INVALID_ARG;
    }
    if (!srcDescs)
    {
        RHI_VALIDATION_ERROR("Source descriptions must be valid");
        return SLANG_E_INVALID_ARG;
    }
    if (matrixCount == 0)
    {
        RHI_VALIDATION_ERROR("Matrix count must be non-zero");
        return SLANG_E_INVALID_ARG;
    }

    bool valid = true;
    size_t minDstBufferSize = 0;
    size_t minSrcBufferSize = 0;
    for (uint32_t i = 0; i < matrixCount; i++)
    {
        if (dstDescs[i].rowCount != srcDescs[i].rowCount)
        {
            RHI_VALIDATION_ERROR_FORMAT("dstDescs[%d].rowCount must match srcDescs[%d].rowCount", i, i);
            valid = false;
        }
        if (dstDescs[i].colCount != srcDescs[i].colCount)
        {
            RHI_VALIDATION_ERROR_FORMAT("dstDescs[%d].colCount must match srcDescs[%d].colCount", i, i);
            valid = false;
        }

        if (dstDescs[i].rowCount < 1 || dstDescs[i].rowCount > 128)
        {
            RHI_VALIDATION_ERROR_FORMAT("dstDescs[%d].rowCount must be in the range [1, 128]", i);
            valid = false;
        }
        if (dstDescs[i].colCount < 1 || dstDescs[i].colCount > 128)
        {
            RHI_VALIDATION_ERROR_FORMAT("dstDescs[%d].colCount must be in the range [1, 128]", i);
            valid = false;
        }
        if (dstDescs[i].size == 0)
        {
            RHI_VALIDATION_ERROR_FORMAT("dstDescs[%d].size must not be zero", i);
            valid = false;
        }
        if (dstDescs[i].offset % 64 != 0)
        {
            RHI_VALIDATION_ERROR_FORMAT("dstDescs[%d].offset must be a multiple of 64 bytes", i);
            valid = false;
        }
        switch (dstDescs[i].layout)
        {
        case CooperativeVectorMatrixLayout::RowMajor:
        case CooperativeVectorMatrixLayout::ColumnMajor:
            if (dstDescs[i].rowColumnStride == 0)
            {
                RHI_VALIDATION_ERROR_FORMAT(
                    "dstDescs[%d].rowColumnStride must must not be zero for row-major and column-major layouts"
                    "layouts",
                    i
                );
                valid = false;
            }
            break;
        case CooperativeVectorMatrixLayout::InferencingOptimal:
        case CooperativeVectorMatrixLayout::TrainingOptimal:
            if (dstDescs[i].rowColumnStride != 0)
            {
                RHI_VALIDATION_ERROR_FORMAT("dstDescs[%d].rowColumnStride must be zero for optimal layouts", i);
                valid = false;
            }
            break;
        default:
            RHI_VALIDATION_ERROR_FORMAT("dstDescs[%d].layout is invalid", i);
            valid = false;
            break;
        }

        if (srcDescs[i].rowCount < 1 || srcDescs[i].rowCount > 128)
        {
            RHI_VALIDATION_ERROR_FORMAT("srcDescs[%d].rowCount must be in the range [1, 128]", i);
            valid = false;
        }
        if (srcDescs[i].colCount < 1 || srcDescs[i].colCount > 128)
        {
            RHI_VALIDATION_ERROR_FORMAT("srcDescs[%d].colCount must be in the range [1, 128]", i);
            valid = false;
        }
        if (srcDescs[i].size == 0)
        {
            RHI_VALIDATION_ERROR_FORMAT("srcDescs[%d].size must not be zero", i);
            valid = false;
        }
        if (srcDescs[i].offset % 64 != 0)
        {
            RHI_VALIDATION_ERROR_FORMAT("srcDescs[%d].offset must be a multiple of 64 bytes", i);
            valid = false;
        }
        switch (srcDescs[i].layout)
        {
        case CooperativeVectorMatrixLayout::RowMajor:
        case CooperativeVectorMatrixLayout::ColumnMajor:
            if (srcDescs[i].rowColumnStride == 0)
            {
                RHI_VALIDATION_ERROR_FORMAT(
                    "srcDescs[%d].rowColumnStride must must not be zero for row-major and column-major layouts"
                    "layouts",
                    i
                );
                valid = false;
            }
            break;
        case CooperativeVectorMatrixLayout::InferencingOptimal:
        case CooperativeVectorMatrixLayout::TrainingOptimal:
            if (srcDescs[i].rowColumnStride != 0)
            {
                RHI_VALIDATION_ERROR_FORMAT("srcDescs[%d].rowColumnStride must be zero for optimal layouts", i);
                valid = false;
            }
            break;
        default:
            RHI_VALIDATION_ERROR_FORMAT("srcDescs[%d].layout is invalid", i);
            valid = false;
            break;
        }

        minDstBufferSize = std::max(minDstBufferSize, dstDescs[i].offset + dstDescs[i].size);
        minSrcBufferSize = std::max(minSrcBufferSize, srcDescs[i].offset + srcDescs[i].size);
    }

    if (dstBufferSize < minDstBufferSize)
    {
        RHI_VALIDATION_ERROR_FORMAT(
            "Destination buffer size (%zu) is smaller than the required minimum size (%zu)",
            dstBufferSize,
            minDstBufferSize
        );
        valid = false;
    }
    if (srcBufferSize < minSrcBufferSize)
    {
        RHI_VALIDATION_ERROR_FORMAT(
            "Source buffer size (%zu) is smaller than the required minimum size (%zu)",
            srcBufferSize,
            minSrcBufferSize
        );
        valid = false;
    }

    return valid ? SLANG_OK : SLANG_E_INVALID_ARG;
}

} // namespace rhi::debug
