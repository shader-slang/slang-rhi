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

#include <map>
#include <mutex>
#include <vector>

namespace rhi::debug {

extern thread_local const char* tls_currentFunctionName;

struct ScopedAPIName
{
    ScopedAPIName(const char* name) { tls_currentFunctionName = name; }
    ~ScopedAPIName() { tls_currentFunctionName = nullptr; }
};

#define SLANG_RHI_DEBUG_API(interface, method) ScopedAPIName scopedAPIName(#interface "::" #method)

inline const char* getAPIName()
{
    return tls_currentFunctionName ? tls_currentFunctionName : "<unknown function>";
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

// Tracks which queue currently owns each `Shared` resource across the explicit handOffShared /
// takeOverShared transitions, so the validation layer can flag using a resource on a queue that does
// not own it. The state is process-global because a shared resource is exported from one device and
// imported into another, each with its own DebugContext; the two sides are tied only by the
// resource's shared NativeHandle, the sole identifier common to both.
//
// A producer resource resolves its key via getSharedHandle (cached after the first export). An
// imported resource that cannot re-export its handle (e.g. a CUDA buffer created from a shared
// handle) is tied to its key at import time.
//
// Misuse of the handOffShared/takeOverShared calls themselves - e.g. handing off a resource that is
// already handed off, or handing off a resource owned by another queue - is an error on every
// backend. Using a resource that was explicitly handed off is likewise an error. Using a resource on
// a queue that does not own it, without an intervening hand-off, is an error on Vulkan (the
// queue-family transfer is real, so it is genuine misuse) and a warning on the no-op backends (where
// it cannot be distinguished from legitimate use).
//
// This is a best-effort validation aid, not a source of truth. The debug layer does not wrap
// buffers/textures and so cannot observe their destruction, so entries are never evicted; in a
// long-running process that frees and recreates many shared resources the table grows unbounded.
// Stale state from a destroyed resource does not, however, mislead a newly created one: creating a
// producer shared resource resets any entry for its freshly minted handle (an existing entry for it
// can only be a recycled, stale one) - see resetForNewSharedResource and the debug device's create
// paths. Ownership is updated at command-recording time rather than at submission, so validation
// assumes encoders are submitted in the order recorded (the intended hand-off/take-over usage).
class SharedResourceOwnershipTracker
{
public:
    static SharedResourceOwnershipTracker& get()
    {
        // Intentionally never destroyed: the table is process-global and may be referenced during
        // static teardown, and clang builds forbid exit-time destructors (-Wexit-time-destructors
        // -Werror). A leaked function-local static has no exit-time destructor, and being
        // constructed on first use it also avoids -Wglobal-constructors.
        static SharedResourceOwnershipTracker* instance = new SharedResourceOwnershipTracker();
        return *instance;
    }

    // Tie an imported resource to its shared handle so later uses on the consumer side resolve to the
    // same entry as the producer. Import does not change ownership.
    void tieImportedResource(IResource* resource, const NativeHandle& handle)
    {
        if (!resource || !handle)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_resourceKeys[resource] = Key{handle.type, handle.value};
    }

    // Reset any tracked state for a newly created producer shared resource. A producer mints a fresh
    // shared handle at creation, so an existing entry for that handle can only be stale - left by a
    // destroyed resource whose handle value the driver has since recycled - and inheriting it would
    // misreport ownership; the entry is dropped so the new resource starts untracked. The pointer->key
    // cache entry is dropped too, in case the resource's address was itself recycled. Import paths do
    // not call this: an imported handle is the producer's live handle, whose ownership state must be
    // preserved for the consumer's takeOverShared to validate against.
    void resetForNewSharedResource(IResource* resource)
    {
        NativeHandle handle;
        if (!getSharedHandleOf(resource, handle) || !handle)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_resourceKeys.erase(resource);
        m_entries.erase(Key{handle.type, handle.value});
    }

    // Whether `resource` has a shared handle, i.e. is a valid handOffShared/takeOverShared argument.
    // Used by the encoder to reject an unshared resource before recording a transfer. This is not a
    // pure query: resolveKey caches the resolved key in m_resourceKeys and may call the backend's
    // getSharedHandle. It does not touch the ownership state (Owned/HandedOff).
    bool isShared(IResource* resource)
    {
        Key key;
        return resolveKey(resource, key);
    }

    // Hand off ownership from `srcQueue` (this encoder's queue) to `destQueue`. Validates the
    // transition (Unowned/Owned(srcQueue) -> HandedOff); an invalid transition is an error on every
    // backend, since the application is misusing the API. The recorded transition is applied
    // regardless of validity so the tracker follows the recorded command stream (the debug layer
    // tracks, it does not gate - the real barrier is recorded either way); rejecting it would desync
    // the tracker and spuriously flag every later legitimate op.
    void handOff(DebugContext* ctx, IResource* resource, ICommandQueue* srcQueue, ICommandQueue* destQueue)
    {
        Key key;
        if (!resolveKey(resource, key))
            return;
        const char* problem = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Entry& entry = m_entries[key];
            entry.registered = true;
            if (entry.state == State::HandedOff)
                problem =
                    "a shared resource that is already handed off is being handed off again; the "
                    "current owner must takeOverShared before handing it off.";
            else if (entry.state == State::Owned && entry.owner != srcQueue)
                problem = "a shared resource is being handed off by a queue that does not own it.";
            entry.state = State::HandedOff;
            entry.handoffSrc = srcQueue;
            entry.handoffDst = destQueue;
        }
        if (problem)
            report(ctx, Severity::Error, problem);
    }

    // Take ownership to `takerQueue` (this encoder's queue) from `srcQueue`, reversing a hand-off.
    // Validates that the resource was handed off and that this queue is the one it was handed off to.
    void takeOver(DebugContext* ctx, IResource* resource, ICommandQueue* takerQueue, ICommandQueue* srcQueue)
    {
        Key key;
        if (!resolveKey(resource, key))
            return;
        const char* problem = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Entry& entry = m_entries[key];
            entry.registered = true;
            if (entry.state != State::HandedOff)
                problem = "a shared resource that was not handed off is being taken over.";
            else if (entry.handoffDst && entry.handoffDst != takerQueue)
                problem =
                    "a shared resource is being taken over by a queue other than the one it was "
                    "handed off to.";
            else if (srcQueue && entry.handoffSrc && entry.handoffSrc != srcQueue)
                problem = "the srcQueue does not match the queue that handed off the resource.";
            entry.state = State::Owned;
            entry.owner = takerQueue;
            entry.handoffSrc = nullptr;
            entry.handoffDst = nullptr;
        }
        if (problem)
            report(ctx, Severity::Error, problem);
    }

    // Validate a use of `resource` on `usingQueue`. A first use of an untracked resource acquires it
    // for `usingQueue`; a use while the resource is handed off, or owned by a different queue, is
    // reported through `ctx`.
    void checkUse(DebugContext* ctx, IResource* resource, ICommandQueue* usingQueue)
    {
        Key key;
        if (!resolveKey(resource, key))
            return;
        Severity severity = Severity::Warning;
        const char* message = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Entry& entry = m_entries[key];
            if (!entry.registered)
            {
                // First use of an untracked shared resource acquires it for the using queue.
                entry.registered = true;
                entry.state = State::Owned;
                entry.owner = usingQueue;
                return;
            }
            if (entry.state == State::Owned && entry.owner == usingQueue)
                return;
            if (entry.state == State::HandedOff)
            {
                // Explicitly handed off then used: misuse of the API.
                severity = Severity::Error;
                message =
                    "a shared resource that has been handed off to another queue or external API is "
                    "used before being reacquired; call takeOverShared first.";
            }
            else
            {
                // Owned by a different queue without an intervening hand-off. On a backend where the
                // transfer calls are no-ops this cannot be distinguished from legitimate use, so it is
                // a warning; on Vulkan the queue-family ownership transfer is real, so using the
                // resource on a non-owning queue without a takeOverShared is genuine misuse and an
                // error.
                severity = (ctx && ctx->deviceType == DeviceType::Vulkan) ? Severity::Error : Severity::Warning;
                message = "a shared resource is used on a queue that does not currently own it.";
            }
        }
        report(ctx, severity, message);
    }

private:
    struct Key
    {
        NativeHandleType type;
        uint64_t value;
        bool operator<(const Key& other) const { return type != other.type ? type < other.type : value < other.value; }
    };
    enum class State
    {
        Unowned,
        Owned,
        HandedOff,
    };
    enum class Severity
    {
        Error,
        Warning,
    };
    struct Entry
    {
        bool registered = false;
        State state = State::Unowned;
        ICommandQueue* owner = nullptr;      // valid when state == Owned
        ICommandQueue* handoffSrc = nullptr; // the queue that handed off, when state == HandedOff
        ICommandQueue* handoffDst = nullptr; // the queue expected to take over, when state == HandedOff
    };

    // Emit a validation message at the given severity. Called only outside m_mutex: the debug callback
    // may re-enter a tracked RHI operation, which would re-acquire the mutex and deadlock.
    void report(DebugContext* ctx, Severity severity, const char* message)
    {
        DebugMessageType type = (severity == Severity::Error) ? DebugMessageType::Error : DebugMessageType::Warning;
        _rhiDiagnoseImpl(ctx, type, "%s: %s", getAPIName(), message);
    }

    // Resolve a resource's key, preferring a previously tied key (needed for imported resources that
    // cannot re-export) and falling back to getSharedHandle for producer resources. Returns false for
    // a resource without a shared handle, which is then left untracked.
    // The Shared usage flag is checked before calling getSharedHandle because getSharedHandle on a
    // resource created without it fails at the backend (only Shared allocations are exportable), so
    // calling it on ordinary resources would emit spurious backend errors on every tracked command.
    // A resource that is neither an IBuffer nor an ITexture (a texture view, say) is not shareable
    // and is left untracked.
    static bool getSharedHandleOf(IResource* resource, NativeHandle& outHandle)
    {
        ComPtr<IBuffer> buffer;
        ComPtr<ITexture> texture;
        switch (classifySharedResource(resource, buffer, texture))
        {
        case SharedResourceKind::Buffer:
            return is_set(buffer->getDesc().usage, BufferUsage::Shared) &&
                   SLANG_SUCCEEDED(buffer->getSharedHandle(&outHandle));
        case SharedResourceKind::Texture:
            return is_set(texture->getDesc().usage, TextureUsage::Shared) &&
                   SLANG_SUCCEEDED(texture->getSharedHandle(&outHandle));
        default:
            return false;
        }
    }

    bool resolveKey(IResource* resource, Key& outKey)
    {
        if (!resource)
            return false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_resourceKeys.find(resource);
            if (it != m_resourceKeys.end())
            {
                outKey = it->second;
                return true;
            }
        }
        NativeHandle handle;
        if (!getSharedHandleOf(resource, handle) || !handle)
            return false;
        outKey = Key{handle.type, handle.value};
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_resourceKeys[resource] = outKey;
        }
        return true;
    }

    std::mutex m_mutex;
    std::map<IResource*, Key> m_resourceKeys;
    std::map<Key, Entry> m_entries;
};

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

/// Check that a subrange [offset, offset+size) is within the total size while avoiding overflow.
/// Returns true if the offset and size are valid.
template<typename T>
bool isValidSubrange(T offset, T size, T totalSize)
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
    return isValidEnum<ResourceState, ResourceState::MicromapWrite>(value);
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

inline bool isValidAccelerationStructureKind(AccelerationStructureKind value)
{
    return isValidEnum<AccelerationStructureKind, AccelerationStructureKind::TopLevel>(value);
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
        BufferUsage::AccelerationStructureBuildInput | BufferUsage::MicromapBuildInput | BufferUsage::MicromapStorage |
        BufferUsage::ShaderTable | BufferUsage::Shared;
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
        AccelerationStructureBuildFlags::MinimizeMemory | AccelerationStructureBuildFlags::CreateMotion |
        AccelerationStructureBuildFlags::AllowOpacityMicromapUpdate |
        AccelerationStructureBuildFlags::AllowDisableOpacityMicromaps;
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
