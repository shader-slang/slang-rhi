#pragma once

#include <slang-rhi.h>
#include "core/common.h"
#include "core/arena-allocator.h"

#include <utility>
#include <set>
#include <cstring>

// clang-format off
#define SLANG_RHI_COMMANDS(x) \
    x(CopyBuffer) \
    x(CopyTexture) \
    x(CopyTextureToBuffer) \
    x(ClearBuffer) \
    x(ClearTexture) \
    x(UploadTextureData) \
    x(UploadBufferData) \
    x(ResolveQuery) \
    x(BeginRenderPass) \
    x(EndRenderPass) \
    x(SetRenderState) \
    x(Draw) \
    x(DrawIndexed) \
    x(DrawIndirect) \
    x(DrawIndexedIndirect) \
    x(DrawMeshTasks) \
    x(BeginComputePass) \
    x(EndComputePass) \
    x(SetComputeState) \
    x(DispatchCompute) \
    x(DispatchComputeIndirect) \
    x(BeginRayTracingPass) \
    x(EndRayTracingPass) \
    x(SetRayTracingState) \
    x(DispatchRays) \
    x(BuildAccelerationStructure) \
    x(CopyAccelerationStructure) \
    x(QueryAccelerationStructureProperties) \
    x(SerializeAccelerationStructure) \
    x(DeserializeAccelerationStructure) \
    x(ConvertCooperativeVectorMatrix) \
    x(SetBufferState) \
    x(SetTextureState) \
    x(PushDebugGroup) \
    x(PopDebugGroup) \
    x(InsertDebugMarker) \
    x(WriteTimestamp) \
    x(ExecuteCallback)
// clang-format on


namespace rhi {

struct BindingData;
class ExtendedShaderObjectTypeListObject;

#define SLANG_RHI_COMMAND_ENUM_X(x) x,

enum class CommandID : uint32_t
{
    SLANG_RHI_COMMANDS(SLANG_RHI_COMMAND_ENUM_X)
};

#undef SLANG_RHI_COMMAND_ENUM_X

namespace commands {

struct CopyBuffer
{
    IBuffer* dst;
    uint64_t dstOffset;
    IBuffer* src;
    uint64_t srcOffset;
    Size size;
};

struct CopyTexture
{
    ITexture* dst;
    SubresourceRange dstSubresource;
    Offset3D dstOffset;
    ITexture* src;
    SubresourceRange srcSubresource;
    Offset3D srcOffset;
    Extents extent;
};

struct CopyTextureToBuffer
{
    IBuffer* dst;
    uint64_t dstOffset;
    Size dstSize;
    Size dstRowStride;
    ITexture* src;
    SubresourceRange srcSubresource;
    Offset3D srcOffset;
    Extents extent;
};

struct ClearBuffer
{
    IBuffer* buffer;
    BufferRange range;
};

struct ClearTexture
{
    ITexture* texture;
    ClearValue clearValue;
    SubresourceRange subresourceRange;
    bool clearDepth;
    bool clearStencil;
};

struct UploadTextureData
{
    ITexture* dst;
    SubresourceRange subresourceRange;
    Offset3D offset;
    Extents extent;
    // TODO: we could use some owned memory blob to avoid copying data
    // also, SubresourceData needs a size field to know how much to copy
    SubresourceData* subresourceData;
    uint32_t subresourceDataCount;
};

struct UploadBufferData
{
    IBuffer* dst;
    uint64_t offset;
    // TODO: we could use some owned memory blob to avoid copying data
    const void* data;
    Size size;
};

struct ResolveQuery
{
    IQueryPool* queryPool;
    uint32_t index;
    uint32_t count;
    IBuffer* buffer;
    uint64_t offset;
};

struct BeginRenderPass
{
    RenderPassDesc desc;
};

struct EndRenderPass
{};

struct SetRenderState
{
    RenderState state;
    IRenderPipeline* pipeline;
    ExtendedShaderObjectTypeListObject* specializationArgs;
    BindingData* bindingData;
};

struct Draw
{
    DrawArguments args;
};

struct DrawIndexed
{
    DrawArguments args;
};

struct DrawIndirect
{
    uint32_t maxDrawCount;
    IBuffer* argBuffer;
    uint64_t argOffset;
    IBuffer* countBuffer;
    uint64_t countOffset;
};

struct DrawIndexedIndirect
{
    uint32_t maxDrawCount;
    IBuffer* argBuffer;
    uint64_t argOffset;
    IBuffer* countBuffer;
    uint64_t countOffset;
};

struct DrawMeshTasks
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

struct BeginComputePass
{};

struct EndComputePass
{};

struct SetComputeState
{
    IComputePipeline* pipeline;
    ExtendedShaderObjectTypeListObject* specializationArgs;
    BindingData* bindingData;
};

struct DispatchCompute
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

struct DispatchComputeIndirect
{
    IBuffer* argBuffer;
    uint64_t offset;
};

struct BeginRayTracingPass
{};

struct EndRayTracingPass
{};

struct SetRayTracingState
{
    IRayTracingPipeline* pipeline;
    ExtendedShaderObjectTypeListObject* specializationArgs;
    IShaderTable* shaderTable;
    BindingData* bindingData;
};

struct DispatchRays
{
    uint32_t rayGenShaderIndex;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
};

struct BuildAccelerationStructure
{
    AccelerationStructureBuildDesc desc;
    IAccelerationStructure* dst;
    IAccelerationStructure* src;
    BufferWithOffset scratchBuffer;
    uint32_t propertyQueryCount;
    AccelerationStructureQueryDesc* queryDescs;
};

struct CopyAccelerationStructure
{
    IAccelerationStructure* dst;
    IAccelerationStructure* src;
    AccelerationStructureCopyMode mode;
};

struct QueryAccelerationStructureProperties
{
    uint32_t accelerationStructureCount;
    IAccelerationStructure** accelerationStructures;
    uint32_t queryCount;
    AccelerationStructureQueryDesc* queryDescs;
};

struct SerializeAccelerationStructure
{
    BufferWithOffset dst;
    IAccelerationStructure* src;
};

struct DeserializeAccelerationStructure
{
    IAccelerationStructure* dst;
    BufferWithOffset src;
};

struct ConvertCooperativeVectorMatrix
{
    const ConvertCooperativeVectorMatrixDesc* descs;
    uint32_t descCount;
};

struct SetBufferState
{
    IBuffer* buffer;
    ResourceState state;
};

struct SetTextureState
{
    ITexture* texture;
    SubresourceRange subresourceRange;
    ResourceState state;
};

struct PushDebugGroup
{
    const char* name;
    float rgbColor[3];
};

struct PopDebugGroup
{};

struct InsertDebugMarker
{
    const char* name;
    float rgbColor[3];
};

struct WriteTimestamp
{
    IQueryPool* queryPool;
    uint32_t queryIndex;
};

struct ExecuteCallback
{
    using Callback = void (*)(const void* userData);
    Callback callback;
    const void* userData;
    Size userDataSize;
};

#define SLANG_RHI_COMMAND_CHECK_X(x)                                                                                   \
    static_assert(                                                                                                     \
        std::is_default_constructible_v<x> && std::is_trivially_copyable_v<x>,                                         \
        #x " must be a default constructible and trivially copyable"                                                   \
    );
SLANG_RHI_COMMANDS(SLANG_RHI_COMMAND_CHECK_X)
#undef SLANG_RHI_COMMAND_CHECK_X

template<typename T>
struct Traits
{};

#define SLANG_RHI_COMMAND_TRAITS_X(x)                                                                                  \
    template<>                                                                                                         \
    struct Traits<x>                                                                                                   \
    {                                                                                                                  \
        static const CommandID id = CommandID::x;                                                                      \
        static inline const char* name = #x;                                                                           \
    };

SLANG_RHI_COMMANDS(SLANG_RHI_COMMAND_TRAITS_X)
#undef SLANG_RHI_COMMAND_TRAITS_X

} // namespace commands


/// A list of commands recorded by the command encoder.
///
/// Depending on the backend, this list can either be executed immediately on submit
/// (CPU, CUDA, D3D11) or recorded to a backend specific command buffer when finishing
/// encoding (D3D12, Vulkan, Metal, WGPU).
///
/// There are multiple reasons for encoding commands into a host side command list:
/// - Allow to encode commands in parallel, even if backend doesn't support multi-threading.
/// - Allow parallel compilation of specialized programs and pipeline creation.
/// - Allow use of unspecialized programs during command encoding, for which pipelines
///   are not yet created.
///
/// Commands are written into consecutive memory pages and put into a linked list.
/// All resources referenced by the commands are retained until the command list is reset.
///
class CommandList : public RefObject
{
public:
    struct CommandSlot
    {
        CommandID id;
        CommandSlot* next;
        void* data;
    };

    CommandList(ArenaAllocator& allocator, std::set<RefPtr<RefObject>>& trackedObjects);

    void reset();

    void write(commands::CopyBuffer&& cmd);
    void write(commands::CopyTexture&& cmd);
    void write(commands::CopyTextureToBuffer&& cmd);
    void write(commands::ClearBuffer&& cmd);
    void write(commands::ClearTexture&& cmd);
    void write(commands::UploadTextureData&& cmd);
    void write(commands::UploadBufferData&& cmd);
    void write(commands::ResolveQuery&& cmd);
    void write(commands::BeginRenderPass&& cmd);
    void write(commands::EndRenderPass&& cmd);
    void write(commands::SetRenderState&& cmd);
    void write(commands::Draw&& cmd);
    void write(commands::DrawIndexed&& cmd);
    void write(commands::DrawIndirect&& cmd);
    void write(commands::DrawIndexedIndirect&& cmd);
    void write(commands::DrawMeshTasks&& cmd);
    void write(commands::BeginComputePass&& cmd);
    void write(commands::EndComputePass&& cmd);
    void write(commands::SetComputeState&& cmd);
    void write(commands::DispatchCompute&& cmd);
    void write(commands::DispatchComputeIndirect&& cmd);
    void write(commands::BeginRayTracingPass&& cmd);
    void write(commands::EndRayTracingPass&& cmd);
    void write(commands::SetRayTracingState&& cmd);
    void write(commands::DispatchRays&& cmd);
    void write(commands::BuildAccelerationStructure&& cmd);
    void write(commands::CopyAccelerationStructure&& cmd);
    void write(commands::QueryAccelerationStructureProperties&& cmd);
    void write(commands::SerializeAccelerationStructure&& cmd);
    void write(commands::DeserializeAccelerationStructure&& cmd);
    void write(commands::ConvertCooperativeVectorMatrix&& cmd);
    void write(commands::SetBufferState&& cmd);
    void write(commands::SetTextureState&& cmd);
    void write(commands::PushDebugGroup&& cmd);
    void write(commands::PopDebugGroup&& cmd);
    void write(commands::InsertDebugMarker&& cmd);
    void write(commands::WriteTimestamp&& cmd);
    void write(commands::ExecuteCallback&& cmd);

    const CommandSlot* getCommands() const { return m_commandSlots; }

    template<typename T>
    T& getCommand(const CommandSlot* command)
    {
        return *reinterpret_cast<T*>(command->data);
    }

    template<typename T>
    const T& getCommand(const CommandSlot* command) const
    {
        return *reinterpret_cast<const T*>(command->data);
    }

private:
    ArenaAllocator& m_allocator;
    std::set<RefPtr<RefObject>>& m_trackedObjects;
    CommandSlot* m_commandSlots = nullptr;
    CommandSlot* m_lastCommandSlot = nullptr;

    void retainResource(RefObject* resource)
    {
        if (resource)
        {
            m_trackedObjects.insert(resource);
        }
    }

    template<typename To, typename From>
    void retainResource(From* resource)
    {
        if (resource)
        {
            To* obj = checked_cast<To*>(resource);
            retainResource(obj);
        }
    }

    const void* writeData(const void* data, size_t size)
    {
        void* dst = m_allocator.allocate(size);
        std::memcpy(dst, data, size);
        return dst;
    }

    template<typename T>
    void writeCommand(T&& cmd)
    {
        CommandSlot* slot = reinterpret_cast<CommandSlot*>(m_allocator.allocate(sizeof(CommandSlot)));
        slot->id = commands::Traits<T>::id;
        slot->next = nullptr;
        slot->data = nullptr;
        if (m_lastCommandSlot)
        {
            m_lastCommandSlot->next = slot;
        }
        else
        {
            m_commandSlots = slot;
        }
        m_lastCommandSlot = slot;

        if constexpr (sizeof(T) > 0)
        {
            slot->data = m_allocator.allocate(sizeof(T));
            new (slot->data) T(std::forward<T>(cmd));
        }
    }
};

} // namespace rhi
