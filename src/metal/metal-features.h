#pragma once

#include <slang-rhi.h>
#include "core/common.h"
#include <array>

namespace rhi {

// Enumeration of Metal GPU families
enum class MetalGPUFamily
{
    GPUFamily1,
    Apple2,
    Apple3,
    Apple4,
    Apple5,
    Apple6,
    Apple7,
    Apple8,
    Apple9,
    Mac2,
    Count
};

// Structure to hold Metal GPU limits
struct MetalGPULimits
{
    // Function Arguments
    uint32_t maxVertexAttributes;
    uint32_t maxBufferArgumentTableEntries;
    uint32_t maxTextureArgumentTableEntries;
    uint32_t maxSamplerStateArgumentTableEntries;
    uint32_t maxThreadgroupMemoryArgumentTableEntries;
    uint32_t maxConstantBufferArguments;
    uint32_t maxConstantBufferArgumentLength;
    uint32_t maxThreadsPerThreadgroup;
    uint32_t maxTotalThreadgroupMemoryAllocation;
    uint32_t maxTotalTileMemoryAllocation;
    uint32_t threadgroupMemoryLengthAlignment;
    uint32_t maxFragmentFunctionInputs;
    uint32_t maxFragmentFunctionInputComponents;
    uint32_t maxFunctionConstants;
    uint32_t maxTessellationFactor;
    uint32_t maxViewportsAndScissorRectangles;
    uint32_t maxRasterOrderGroups;

    // Argument Buffers
    uint32_t maxBuffersPerStage;
    uint32_t maxTexturesPerStage;
    uint32_t maxSamplersPerStage;

    // Resources
    uint32_t minConstantBufferOffsetAlignment;
    uint32_t max1DTextureWidth;
    uint32_t max2DTextureDimensions;
    uint32_t maxCubeMapDimensions;
    uint32_t max3DTextureDimensions;
    uint32_t maxTextureBufferWidth;
    uint32_t maxTextureArrayLayers;
    uint32_t bufferAlignmentForTextureCopy;
    uint32_t maxCounterSampleBufferLength;
    uint32_t maxNumberOfSampleBuffers;

    // Render Targets
    uint32_t maxColorRenderTargets;
    uint32_t maxPointPrimitiveSize;
    uint32_t maxTotalRenderTargetSizePerPixel;
    uint32_t maxVisibilityQueryOffset;
    uint32_t maxTileSizeNoMSAA;
    uint32_t maxTileSize2xMSAA;
    uint32_t maxTileSize4xMSAA;

    // Feature Limits
    uint32_t maxNumberOfFences;
    uint32_t maxIOCommandsPerBuffer;
    uint32_t maxVertexAmplificationCount;
    uint32_t maxThreadgroupsPerObjectShaderGrid;
    uint32_t maxThreadgroupsPerMeshShaderGrid;
    uint32_t maxPayloadInMeshShaderPipeline;
    uint32_t maxRayTracingIntersectorLevels;
    uint32_t maxRayTracingIntersectionQueryLevels;
};

// Function to get GPU limits for a specific Metal GPU family
Result getMetalGPULimits(MetalGPUFamily family, MetalGPULimits& outLimits);

} // namespace rhi
