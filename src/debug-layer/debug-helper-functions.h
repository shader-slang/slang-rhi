#pragma once
#include "debug-base.h"
#include "debug-command-buffer.h"
#include "debug-command-queue.h"
#include "debug-command-encoder.h"
#include "debug-device.h"
#include "debug-fence.h"
#include "debug-heap.h"
#include "debug-query.h"
#include "debug-shader-object.h"
#include "debug-surface.h"

#include "strings.h"

#include <vector>

namespace rhi::debug {

extern thread_local const char* _currentFunctionName;

struct ScopedAPIName
{
    ScopedAPIName(const char* name) { _currentFunctionName = name; }
    ~ScopedAPIName() { _currentFunctionName = nullptr; }
};

#define SLANG_RHI_DEBUG_API(interface, method) ScopedAPIName scopedAPIName(#interface "::" #method)

inline const char* getAPIName()
{
    return _currentFunctionName ? _currentFunctionName : "<unknown function>";
}

template<typename... TArgs>
char* _rhiDiagnoseFormat(
    char* buffer,                   // Initial buffer to output formatted string.
    size_t shortBufferSize,         // Size of the initial buffer.
    std::vector<char>& bufferArray, // A list for allocating a large buffer if needed.
    const char* format,             // The format string.
    TArgs... args
)
{
    int length = snprintf(buffer, shortBufferSize, format, args...);
    if (length < 0)
        return buffer;
    if (length > 255)
    {
        bufferArray.resize(length + 1);
        buffer = bufferArray.data();
        snprintf(buffer, bufferArray.size(), format, args...);
    }
    return buffer;
}

template<typename... TArgs>
void _rhiDiagnoseImpl(DebugContext* ctx, DebugMessageType type, const char* format, TArgs... args)
{
    char shortBuffer[256];
    std::vector<char> bufferArray;
    auto buffer = _rhiDiagnoseFormat(shortBuffer, sizeof(shortBuffer), bufferArray, format, args...);
    ctx->debugCallback->handleMessage(type, DebugMessageSource::Layer, buffer);
}

#define RHI_VALIDATION_ERROR(message) _rhiDiagnoseImpl(ctx, DebugMessageType::Error, "%s: %s", getAPIName(), message)

#define RHI_VALIDATION_WARNING(message)                                                                                \
    _rhiDiagnoseImpl(ctx, DebugMessageType::Warning, "%s: %s", getAPIName(), message)

#define RHI_VALIDATION_INFO(message) _rhiDiagnoseImpl(ctx, DebugMessageType::Info, "%s: %s", getAPIName(), message)

#define RHI_VALIDATION_FORMAT(type, format, ...)                                                                       \
    {                                                                                                                  \
        char shortBuffer[256];                                                                                         \
        std::vector<char> bufferArray;                                                                                 \
        auto message = _rhiDiagnoseFormat(shortBuffer, sizeof(shortBuffer), bufferArray, format, __VA_ARGS__);         \
        _rhiDiagnoseImpl(ctx, type, "%s: %s", getAPIName(), message);                                                  \
    }

#define RHI_VALIDATION_ERROR_FORMAT(...) RHI_VALIDATION_FORMAT(DebugMessageType::Error, __VA_ARGS__)

#define SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(typeName)                                                                   \
    I##typeName* Debug##typeName::getInterface(const Guid& guid)                                                       \
    {                                                                                                                  \
        if (guid == ISlangUnknown::getTypeGuid() || guid == I##typeName::getTypeGuid())                                \
            return static_cast<I##typeName*>(this);                                                                    \
        return nullptr;                                                                                                \
    }

// Utility conversion functions to get Debug* object or the inner object from a user provided
// pointer.
#define SLANG_RHI_DEBUG_GET_OBJ_IMPL(type)                                                                             \
    inline Debug##type* getDebugObj(I##type* ptr)                                                                      \
    {                                                                                                                  \
        return checked_cast<Debug##type*>(checked_cast<DebugObject<I##type>*>(ptr));                                   \
    }                                                                                                                  \
    inline I##type* getInnerObj(I##type* ptr)                                                                          \
    {                                                                                                                  \
        if (!ptr)                                                                                                      \
            return nullptr;                                                                                            \
        auto debugObj = getDebugObj(ptr);                                                                              \
        return debugObj->baseObject;                                                                                   \
    }

#define SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(type)                                                                     \
    inline Debug##type* getDebugObj(I##type* ptr)                                                                      \
    {                                                                                                                  \
        return checked_cast<Debug##type*>(checked_cast<UnownedDebugObject<I##type>*>(ptr));                            \
    }                                                                                                                  \
    inline I##type* getInnerObj(I##type* ptr)                                                                          \
    {                                                                                                                  \
        if (!ptr)                                                                                                      \
            return nullptr;                                                                                            \
        auto debugObj = getDebugObj(ptr);                                                                              \
        return debugObj->baseObject;                                                                                   \
    }

SLANG_RHI_DEBUG_GET_OBJ_IMPL(Device)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(CommandBuffer)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(CommandQueue)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(CommandEncoder)
SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(RenderPassEncoder)
SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(ComputePassEncoder)
SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(RayTracingPassEncoder)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(ShaderObject)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Surface)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(QueryPool)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Fence)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Heap)

std::string subresourceRangeToString(const SubresourceRange& range);

std::string createBufferLabel(const BufferDesc& desc);
std::string createTextureLabel(const TextureDesc& desc);
std::string createTextureViewLabel(const TextureViewDesc& desc);
std::string createSamplerLabel(const SamplerDesc& desc);
std::string createAccelerationStructureLabel(const AccelerationStructureDesc& desc);
std::string createFenceLabel(const FenceDesc& desc);
std::string createQueryPoolLabel(const QueryPoolDesc& desc);
std::string createShaderProgramLabel(const ShaderProgramDesc& desc);
std::string createRenderPipelineLabel(const RenderPipelineDesc& desc);
std::string createComputePipelineLabel(const ComputePipelineDesc& desc);
std::string createRayTracingPipelineLabel(const RayTracingPipelineDesc& desc);
std::string createHeapLabel(const HeapDesc& desc);

Result validateAccelerationStructureBuildDesc(DebugContext* ctx, const AccelerationStructureBuildDesc& buildDesc);
Result validateClusterOperationParams(DebugContext* ctx, const ClusterOperationParams& params);
Result validateConvertCooperativeVectorMatrix(
    DebugContext* ctx,
    size_t dstBufferSize,
    const CooperativeVectorMatrixDesc* dstDescs,
    size_t srcBufferSize,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
);

// ----------------------------------------------------------------------------
// Validation helpers
// ----------------------------------------------------------------------------

/// Check that offset and size are within the total size while avoiding overlow.
/// Returns true if the offset and size are valid.
template<typename T>
bool checkSizePlusOffsetInRange(T offset, T size, T totalSize)
{
    if (offset > totalSize)
        return false;
    if (size > totalSize - offset)
        return false;
    return true;
}

// ----------------------------------------------------------------------------
// Enum validation helpers
// ----------------------------------------------------------------------------

/// Check that a sequential enum value is in [0, LastValue].
template<typename E, E LastValue>
inline bool isValidEnum(E value)
{
    return static_cast<int>(value) >= 0 && static_cast<int>(value) <= static_cast<int>(LastValue);
}

inline bool isValidFormat(Format value)
{
    return isValidEnum<Format, static_cast<Format>(static_cast<int>(Format::_Count) - 1)>(value);
}

inline bool isValidIndexFormat(IndexFormat value)
{
    return isValidEnum<IndexFormat, IndexFormat::Uint32>(value);
}

inline bool isValidMemoryType(MemoryType value)
{
    return isValidEnum<MemoryType, MemoryType::ReadBack>(value);
}

inline bool isValidCpuAccessMode(CpuAccessMode value)
{
    return isValidEnum<CpuAccessMode, CpuAccessMode::Write>(value);
}

inline bool isValidTextureType(TextureType value)
{
    return isValidEnum<TextureType, TextureType::TextureCubeArray>(value);
}

inline bool isValidTextureAspect(TextureAspect value)
{
    return isValidEnum<TextureAspect, TextureAspect::StencilOnly>(value);
}

inline bool isValidResourceState(ResourceState value)
{
    return isValidEnum<ResourceState, ResourceState::AccelerationStructureBuildInput>(value);
}

inline bool isValidLoadOp(LoadOp value)
{
    return isValidEnum<LoadOp, LoadOp::DontCare>(value);
}

inline bool isValidStoreOp(StoreOp value)
{
    return isValidEnum<StoreOp, StoreOp::DontCare>(value);
}

inline bool isValidQueryType(QueryType value)
{
    return isValidEnum<QueryType, QueryType::AccelerationStructureCurrentSize>(value);
}

inline bool isValidAccelerationStructureCopyMode(AccelerationStructureCopyMode value)
{
    return isValidEnum<AccelerationStructureCopyMode, AccelerationStructureCopyMode::Compact>(value);
}

inline bool isValidComparisonFunc(ComparisonFunc value)
{
    return isValidEnum<ComparisonFunc, ComparisonFunc::Always>(value);
}

inline bool isValidTextureFilteringMode(TextureFilteringMode value)
{
    return isValidEnum<TextureFilteringMode, TextureFilteringMode::Linear>(value);
}

inline bool isValidTextureAddressingMode(TextureAddressingMode value)
{
    return isValidEnum<TextureAddressingMode, TextureAddressingMode::MirrorOnce>(value);
}

inline bool isValidTextureReductionOp(TextureReductionOp value)
{
    return isValidEnum<TextureReductionOp, TextureReductionOp::Maximum>(value);
}

inline bool isValidPrimitiveTopology(PrimitiveTopology value)
{
    return isValidEnum<PrimitiveTopology, PrimitiveTopology::PatchList>(value);
}

// ----------------------------------------------------------------------------
// Flags validation helpers
// ----------------------------------------------------------------------------

/// Check that a bitmask enum value has no bits set outside allValidBits.
template<typename E>
inline bool isValidFlags(E value, E allValidBits)
{
    using U = std::underlying_type_t<E>;
    return (static_cast<U>(value) & ~static_cast<U>(allValidBits)) == 0;
}

inline bool isValidBufferUsage(BufferUsage value)
{
    const BufferUsage allValidBits =
        BufferUsage::VertexBuffer | BufferUsage::IndexBuffer | BufferUsage::ConstantBuffer |
        BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::IndirectArgument |
        BufferUsage::CopySource | BufferUsage::CopyDestination | BufferUsage::AccelerationStructure |
        BufferUsage::AccelerationStructureBuildInput | BufferUsage::ShaderTable | BufferUsage::Shared;
    return isValidFlags(value, allValidBits);
}

inline bool isValidTextureUsage(TextureUsage value)
{
    const TextureUsage allValidBits =
        TextureUsage::ShaderResource | TextureUsage::UnorderedAccess | TextureUsage::RenderTarget |
        TextureUsage::DepthStencil | TextureUsage::Present | TextureUsage::CopySource | TextureUsage::CopyDestination |
        TextureUsage::ResolveSource | TextureUsage::ResolveDestination | TextureUsage::Typeless | TextureUsage::Shared;
    return isValidFlags(value, allValidBits);
}

inline bool isValidHeapUsage(HeapUsage value)
{
    const HeapUsage allValidBits = HeapUsage::Shared;
    return isValidFlags(value, allValidBits);
}

inline bool isValidAccelerationStructureBuildFlags(AccelerationStructureBuildFlags value)
{
    const AccelerationStructureBuildFlags allValidBits =
        AccelerationStructureBuildFlags::AllowUpdate | AccelerationStructureBuildFlags::AllowCompaction |
        AccelerationStructureBuildFlags::PreferFastTrace | AccelerationStructureBuildFlags::PreferFastBuild |
        AccelerationStructureBuildFlags::MinimizeMemory | AccelerationStructureBuildFlags::CreateMotion;
    return isValidFlags(value, allValidBits);
}

// ----------------------------------------------------------------------------
// Subresource range validation
// ----------------------------------------------------------------------------

/// Validate a SubresourceRange against a TextureDesc.
/// Returns true if the range is valid. Resolves sentinel values (kAllMips, kAllLayers)
/// and checks that the range is within the texture bounds.
inline bool validateSubresourceRange(const SubresourceRange& range, const TextureDesc& desc)
{
    uint32_t totalMips = desc.mipCount;
    uint32_t totalLayers = desc.getLayerCount();

    // Resolve sentinel values.
    uint32_t mipCount = (range.mipCount == kAllMips) ? (totalMips - min(range.mip, totalMips)) : range.mipCount;
    uint32_t layerCount =
        (range.layerCount == kAllLayers) ? (totalLayers - min(range.layer, totalLayers)) : range.layerCount;

    // Check mip bounds.
    if (range.mip >= totalMips)
        return false;
    if (mipCount == 0 || range.mip + mipCount > totalMips)
        return false;

    // Check layer bounds.
    if (range.layer >= totalLayers)
        return false;
    if (layerCount == 0 || range.layer + layerCount > totalLayers)
        return false;

    return true;
}

} // namespace rhi::debug
