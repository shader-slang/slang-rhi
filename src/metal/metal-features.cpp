#include "metal-features.h"

namespace rhi {

// Helper function to convert KB to bytes
constexpr uint32_t KB(uint32_t kb) { return kb * 1024; }

// Helper function to convert MB to bytes
constexpr uint32_t MB(uint32_t mb) { return mb * 1024 * 1024; }

// Helper function to represent "No limit"
constexpr uint32_t NO_LIMIT = 0xFFFFFFFF;

// Helper function to represent "Varies"
constexpr uint32_t VARIES = 0xFFFFFFFF;

// Table entry representing a single limit across all GPU families
template<typename T>
struct LimitTableEntry
{
    const char* name;
    T values[MetalGPUFamily::Count];
};


// Table of all limits across GPU families
// clang-format off
constexpr std::array<LimitTableEntry<uint32_t>, 45> LIMIT_TABLE = {{
    // Function Arguments                           GPU1        Apple2      Apple3      Apple4      Apple5      Apple6      Apple7      Apple8      Apple9      Mac2
    {"maxVertexAttributes",                     {   31,         31,         31,         31,         31,         31,         31,         31,         31,         31          }},
    {"maxBufferArgumentTableEntries",           {   31,         31,         31,         31,         31,         31,         31,         31,         31,         31          }},
    {"maxTextureArgumentTableEntries",          {   31,         31,         96,         96,         128,        128,        128,        128,        128,        128         }},
    {"maxSamplerStateArgumentTableEntries",     {   2,          16,         16,         16,         16,         16,         16,         16,         16,         16          }},
    {"maxThreadgroupMemoryArgumentTableEntries",{   31,         31,         31,         31,         31,         31,         31,         31,         31,         31          }},
    {"maxConstantBufferArguments",              {   31,         31,         31,         31,         31,         31,         31,         31,         14,         14          }},
    {"maxConstantBufferArgumentLength",         {   KB(4),      KB(4),      KB(4),      KB(4),      KB(4),      KB(4),      KB(4),      KB(4),      KB(4),      KB(4)       }},
    {"maxThreadsPerThreadgroup",                {   512,        512,        1024,       1024,       1024,       1024,       1024,       1024,       1024,       1024        }},
    {"maxTotalThreadgroupMemoryAllocation",     {   16352,      KB(16),     KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     KB(32)      }},
    {"maxTotalTileMemoryAllocation",            {   0,          0,          0,          KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     0           }},
    {"threadgroupMemoryLengthAlignment",        {   16,         16,         16,         16,         16,         16,         16,         16,         16,         16          }},
    {"maxFragmentFunctionInputs",               {   4,          60,         60,         124,        124,        124,        124,        124,        124,        32          }},
    {"maxFragmentFunctionInputComponents",      {   60,         60,         124,        124,        124,        124,        124,        124,        124,        124         }},
    {"maxFunctionConstants",                    {   65536,      65536,      65536,      65536,      65536,      65536,      65536,      65536,      65536,      65536       }},
    {"maxTessellationFactor",                   {   0,          16,         16,         64,         64,         64,         64,         64,         64,         64          }},
    {"maxViewportsAndScissorRectangles",        {   1,          1,          1,          16,         16,         16,         16,         16,         16,         16          }},
    {"maxRasterOrderGroups",                    {   0,          0,          8,          8,          8,          8,          8,          8,          8,          8           }},
    // Argument Buffers                             GPU1        Apple2      Apple3      Apple4      Apple5      Apple6      Apple7      Apple8      Apple9      Mac2
    {"maxBuffersPerStage",                      {   31,         31,         96,         96,         NO_LIMIT,   NO_LIMIT,   NO_LIMIT,   NO_LIMIT,   NO_LIMIT,   NO_LIMIT    }},
    {"maxTexturesPerStage",                     {   31,         31,         96,         96,         MB(1),      MB(1),      MB(1),      MB(1),      MB(1),      MB(1)       }},
    {"maxSamplersPerStage",                     {   16,         16,         16,         16,         128,        1024,       1024,       500*1024,   1024,       1024        }},
    // Resources                                    GPU1        Apple2      Apple3      Apple4      Apple5      Apple6      Apple7      Apple8      Apple9      Mac2
    {"minConstantBufferOffsetAlignment",        {   4,          4,          4,          4,          4,          4,          4,          4,          4,          32          }},
    {"max1DTextureWidth",                       {   8192,       16384,      16384,      16384,      16384,      16384,      16384,      16384,      16384,      16384       }},
    {"max2DTextureDimensions",                  {   8192,       16384,      16384,      16384,      16384,      16384,      16384,      16384,      16384,      16384       }},
    {"maxCubeMapDimensions",                    {   8192,       16384,      16384,      16384,      16384,      16384,      16384,      16384,      16384,      16384       }},
    {"max3DTextureDimensions",                  {   2048,       2048,       2048,       2048,       2048,       2048,       2048,       2048,       2048,       2048        }},
    {"maxTextureBufferWidth",                   {   MB(64),     MB(256),    MB(256),    MB(256),    MB(256),    MB(256),    MB(256),    MB(256),    MB(256),    MB(256)     }},
    {"maxTextureArrayLayers",                   {   2048,       2048,       2048,       2048,       2048,       2048,       2048,       2048,       2048,       2048        }},
    {"bufferAlignmentForTextureCopy",           {   64,         16,         16,         16,         16,         16,         16,         16,         16,         256         }},
    {"maxCounterSampleBufferLength",            {   KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     KB(32),     NO_LIMIT,   NO_LIMIT    }},
    {"maxNumberOfSampleBuffers",                {   32,         32,         32,         32,         32,         32,         32,         32,         NO_LIMIT,   NO_LIMIT    }},
    // Render Targets                               GPU1        Apple2      Apple3      Apple4      Apple5      Apple6      Apple7      Apple8      Apple9      Mac2
    {"maxColorRenderTargets",                   {   8,          8,          8,          8,          8,          8,          8,          8,          8,          8           }},
    {"maxPointPrimitiveSize",                   {   511,        511,        511,        511,        511,        511,        511,        511,        511,        511         }},
    {"maxTotalRenderTargetSizePerPixel",        {   256,        256,        512,        512,        512,        512,        512,        512,        NO_LIMIT,   NO_LIMIT    }},
    {"maxVisibilityQueryOffset",                {   65528,      65528,      65528,      65528,      65528,      KB(256),    KB(256),    KB(256),    KB(256),    KB(256)     }},
    {"maxTileSizeNoMSAA",                       {   32,         32,         32,         32,         32,         32,         32,         32,         32,         0           }},
    {"maxTileSize2xMSAA",                       {   32,         32,         32,         32,         32,         32,         32,         32,         32,         0           }},
    {"maxTileSize4xMSAA",                       {   32,         32,         32,         32,         32,         32,         32,         32,         32,         0           }},
    // Feature Limits                               GPU1        Apple2      Apple3      Apple4      Apple5      Apple6      Apple7      Apple8      Apple9      Mac2
    {"maxNumberOfFences",                       {   32768,      32768,      32768,      32768,      32768,      32768,      32768,      32768,      32768,      32768       }},
    {"maxIOCommandsPerBuffer",                  {   8192,       8192,       8192,       8192,       8192,       8192,       8192,       8192,       8192,       8192        }},
    {"maxVertexAmplificationCount",             {   8,          0,          0,          0,          0,          2,          8,          8,          8,          VARIES      }},
    {"maxThreadgroupsPerObjectShaderGrid",      {   0,          0,          0,          0,          0,          NO_LIMIT,   NO_LIMIT,   NO_LIMIT,   1024,       1024        }},
    {"maxThreadgroupsPerMeshShaderGrid",        {   0,          0,          0,          0,          0,          1024,       1024,       1048575,    1024,       1024        }},
    {"maxPayloadInMeshShaderPipeline",          {   0,          0,          0,          0,          0,          KB(16),     KB(16),     KB(16),     KB(16),     KB(16)      }},
    {"maxRayTracingIntersectorLevels",          {   0,          0,          0,          0,          32,         32,         32,         32,         32,         32          }},
    {"maxRayTracingIntersectionQueryLevels",    {   0,          0,          0,          0,          16,         16,         16,         16,         16,         16          }},
}};
// clang-format on

// Helper function to get a limit value for a specific GPU family
constexpr uint32_t getLimitValue(const LimitTableEntry<uint32_t>& entry, MetalGPUFamily family)
{
    return entry.values[static_cast<size_t>(family)];
}

// Function to get GPU limits for a specific Metal GPU family
Result getMetalGPULimits(MetalGPUFamily family, MetalGPULimits& outLimits)
{
    if (family >= MetalGPUFamily::Count)
        return SLANG_FAIL;

    // Initialize all limits to 0
    memset(&outLimits, 0, sizeof(MetalGPULimits));

    // Function Arguments
    outLimits.maxVertexAttributes = getLimitValue(LIMIT_TABLE[0], family);
    outLimits.maxBufferArgumentTableEntries = getLimitValue(LIMIT_TABLE[1], family);
    outLimits.maxTextureArgumentTableEntries = getLimitValue(LIMIT_TABLE[2], family);
    outLimits.maxSamplerStateArgumentTableEntries = getLimitValue(LIMIT_TABLE[3], family);
    outLimits.maxThreadgroupMemoryArgumentTableEntries = getLimitValue(LIMIT_TABLE[4], family);
    outLimits.maxConstantBufferArguments = getLimitValue(LIMIT_TABLE[5], family);
    outLimits.maxConstantBufferArgumentLength = getLimitValue(LIMIT_TABLE[6], family);
    outLimits.maxThreadsPerThreadgroup = getLimitValue(LIMIT_TABLE[7], family);
    outLimits.maxTotalThreadgroupMemoryAllocation = getLimitValue(LIMIT_TABLE[8], family);
    outLimits.maxTotalTileMemoryAllocation = getLimitValue(LIMIT_TABLE[9], family);
    outLimits.threadgroupMemoryLengthAlignment = getLimitValue(LIMIT_TABLE[10], family);
    outLimits.maxFragmentFunctionInputs = getLimitValue(LIMIT_TABLE[11], family);
    outLimits.maxFragmentFunctionInputComponents = getLimitValue(LIMIT_TABLE[12], family);
    outLimits.maxFunctionConstants = getLimitValue(LIMIT_TABLE[13], family);
    outLimits.maxTessellationFactor = getLimitValue(LIMIT_TABLE[14], family);
    outLimits.maxViewportsAndScissorRectangles = getLimitValue(LIMIT_TABLE[15], family);
    outLimits.maxRasterOrderGroups = getLimitValue(LIMIT_TABLE[16], family);

    // Argument Buffers
    outLimits.maxBuffersPerStage = getLimitValue(LIMIT_TABLE[17], family);
    outLimits.maxTexturesPerStage = getLimitValue(LIMIT_TABLE[18], family);
    outLimits.maxSamplersPerStage = getLimitValue(LIMIT_TABLE[19], family);

    // Resources
    outLimits.minConstantBufferOffsetAlignment = getLimitValue(LIMIT_TABLE[20], family);
    outLimits.max1DTextureWidth = getLimitValue(LIMIT_TABLE[21], family);
    outLimits.max2DTextureDimensions = getLimitValue(LIMIT_TABLE[22], family);
    outLimits.maxCubeMapDimensions = getLimitValue(LIMIT_TABLE[23], family);
    outLimits.max3DTextureDimensions = getLimitValue(LIMIT_TABLE[24], family);
    outLimits.maxTextureBufferWidth = getLimitValue(LIMIT_TABLE[25], family);
    outLimits.maxTextureArrayLayers = getLimitValue(LIMIT_TABLE[26], family);
    outLimits.bufferAlignmentForTextureCopy = getLimitValue(LIMIT_TABLE[27], family);
    outLimits.maxCounterSampleBufferLength = getLimitValue(LIMIT_TABLE[28], family);
    outLimits.maxNumberOfSampleBuffers = getLimitValue(LIMIT_TABLE[29], family);

    // Render Targets
    outLimits.maxColorRenderTargets = getLimitValue(LIMIT_TABLE[30], family);
    outLimits.maxPointPrimitiveSize = getLimitValue(LIMIT_TABLE[31], family);
    outLimits.maxTotalRenderTargetSizePerPixel = getLimitValue(LIMIT_TABLE[32], family);
    outLimits.maxVisibilityQueryOffset = getLimitValue(LIMIT_TABLE[33], family);
    outLimits.maxTileSizeNoMSAA = getLimitValue(LIMIT_TABLE[34], family);
    outLimits.maxTileSize2xMSAA = getLimitValue(LIMIT_TABLE[35], family);
    outLimits.maxTileSize4xMSAA = getLimitValue(LIMIT_TABLE[36], family);

    // Feature Limits
    outLimits.maxNumberOfFences = getLimitValue(LIMIT_TABLE[37], family);
    outLimits.maxIOCommandsPerBuffer = getLimitValue(LIMIT_TABLE[38], family);
    outLimits.maxVertexAmplificationCount = getLimitValue(LIMIT_TABLE[39], family);
    outLimits.maxThreadgroupsPerObjectShaderGrid = getLimitValue(LIMIT_TABLE[40], family);
    outLimits.maxThreadgroupsPerMeshShaderGrid = getLimitValue(LIMIT_TABLE[41], family);
    outLimits.maxPayloadInMeshShaderPipeline = getLimitValue(LIMIT_TABLE[42], family);
    outLimits.maxRayTracingIntersectorLevels = getLimitValue(LIMIT_TABLE[43], family);
    outLimits.maxRayTracingIntersectionQueryLevels = getLimitValue(LIMIT_TABLE[44], family);

    return SLANG_OK;
}

} // namespace rhi
