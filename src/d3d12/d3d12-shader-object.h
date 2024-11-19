#pragma once

#include "d3d12-base.h"
#include "d3d12-helper-functions.h"
#include "d3d12-submitter.h"

#include "../state-tracking.h"
#include "../buffer-pool.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::d3d12 {

struct PendingDescriptorTableBinding
{
    uint32_t rootIndex;
    D3D12_GPU_DESCRIPTOR_HANDLE handle;
};

#if 0
/// Contextual data and operations required when binding shader objects to the pipeline state
struct BindingContext
{
    // CommandEncoderImpl* encoder;
    Submitter* submitter;
    DeviceImpl* device;

    D3D12DescriptorHeap* viewHeap;
    D3D12DescriptorHeap* samplerHeap;

    /// The type of descriptor heap that is OOM during binding.
    D3D12_DESCRIPTOR_HEAP_TYPE outOfMemoryHeap;
    short_vector<PendingDescriptorTableBinding>* pendingTableBindings;

    virtual Result allocateConstantBuffer(size_t size, BufferImpl*& outBufferWeakPtr, size_t& outOffset) = 0;
    virtual Result writeBuffer(BufferImpl* buffer, size_t offset, size_t size, void const* data) = 0;
};
#endif

struct DescriptorTable
{
    DescriptorHeapReference m_heap;
    uint32_t m_offset = 0;
    uint32_t m_count = 0;

    SLANG_FORCE_INLINE uint32_t getDescriptorCount() const { return m_count; }

    /// Get the GPU handle at the specified index
    SLANG_FORCE_INLINE D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(uint32_t index = 0) const
    {
        SLANG_RHI_ASSERT(index < getDescriptorCount());
        return m_heap.getGpuHandle(m_offset + index);
    }

    /// Get the CPU handle at the specified index
    SLANG_FORCE_INLINE D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(uint32_t index = 0) const
    {
        SLANG_RHI_ASSERT(index < getDescriptorCount());
        return m_heap.getCpuHandle(m_offset + index);
    }

    void freeIfSupported()
    {
        if (m_count)
        {
            m_heap.freeIfSupported(m_offset, m_count);
            m_offset = 0;
            m_count = 0;
        }
    }

    bool allocate(uint32_t count)
    {
        auto allocatedOffset = m_heap.allocate(count);
        if (allocatedOffset == -1)
            return false;
        m_offset = allocatedOffset;
        m_count = count;
        return true;
    }

    bool allocate(DescriptorHeapReference heap, uint32_t count)
    {
        auto allocatedOffset = heap.allocate(count);
        if (allocatedOffset == -1)
            return false;
        m_heap = heap;
        m_offset = allocatedOffset;
        m_count = count;
        return true;
    }
};

/// A reprsentation of an allocated descriptor set, consisting of an option resource table and
/// an optional sampler table
struct DescriptorSet
{
    DescriptorTable resourceTable;
    DescriptorTable samplerTable;

    void freeIfSupported()
    {
        resourceTable.freeIfSupported();
        samplerTable.freeIfSupported();
    }
};

enum class BoundResourceType
{
    None,
    Buffer,
    TextureView,
    AccelerationStructure,
};

struct BoundResource
{
    BoundResourceType type = BoundResourceType::None;
    RefPtr<Resource> resource;
    RefPtr<Resource> counterResource;
    ResourceState requiredState = ResourceState::Undefined;
};

class ShaderObjectImpl : public ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, SimpleShaderObjectData>
{
    typedef ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, SimpleShaderObjectData> Super;

public:
    static Result create(DeviceImpl* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject);

    ~ShaderObjectImpl();

    Device* getDevice() { return m_device.get(); }

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;

    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() override;

    // TODO: What to do with size_t?
    virtual SLANG_NO_THROW Result SLANG_MCALL
    setData(ShaderOffset const& inOffset, void const* data, size_t inSize) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(ShaderOffset const& offset, IShaderObject* object) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) override;

protected:
    Result init(
        DeviceImpl* device,
        ShaderObjectLayoutImpl* layout,
        DescriptorHeapReference viewHeap,
        DescriptorHeapReference samplerHeap
    );

    /// Write the uniform/ordinary data of this object into the given `dest` buffer at the given
    /// `offset`
    Result _writeOrdinaryData(
        BindingContext& context,
        BufferImpl* buffer,
        Offset offset,
        Size destSize,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Ensure that the `m_ordinaryDataBuffer` has been created, if it is needed
    Result _ensureOrdinaryDataBufferCreatedIfNeeded(BindingContext& context, ShaderObjectLayoutImpl* specializedLayout);

public:
    /// Prepare to bind this object as a parameter block.
    ///
    /// This involves allocating and binding any descriptor tables necessary
    /// to to store the state of the object. The function returns a descriptor
    /// set formed from any table(s) allocated. In addition, the `ioOffset`
    /// parameter will be adjusted to be correct for binding values into
    /// the resulting descriptor set.
    ///
    /// Returns:
    ///   SLANG_OK when successful,
    ///   SLANG_E_OUT_OF_MEMORY when descriptor heap is full.
    ///
    Result prepareToBindAsParameterBlock(
        BindingContext& context,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout,
        DescriptorSet& outDescriptorSet
    );

    /// Bind this object as a `ParameterBlock<X>`
    Result bindAsParameterBlock(
        BindingContext& context,
        BindingOffset const& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a `ConstantBuffer<X>`
    Result bindAsConstantBuffer(
        BindingContext& context,
        DescriptorSet const& descriptorSet,
        BindingOffset const& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a value (for an interface-type parameter)
    Result bindAsValue(
        BindingContext& context,
        DescriptorSet const& descriptorSet,
        BindingOffset const& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Shared logic for `bindAsConstantBuffer()` and `bindAsValue()`
    Result _bindImpl(
        BindingContext& context,
        DescriptorSet const& descriptorSet,
        BindingOffset const& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    Result bindRootArguments(BindingContext& context, uint32_t& index);

    void setResourceStates(StateTracking& stateTracking);

    /// A CPU-memory descriptor set holding any descriptors used to represent the
    /// resources/samplers in this object's state
    DescriptorSet m_descriptorSet;
    /// A cached descriptor set on GPU heap.
    DescriptorSet m_cachedGPUDescriptorSet;

    short_vector<BoundResource, 16> m_boundResources;
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> m_rootArguments;
    /// A constant buffer used to stored ordinary data for this object
    /// and existential-type sub-objects.
    ///
    /// Allocated from transient heap on demand with `_createOrdinaryDataBufferIfNeeded()`
    BufferImpl* m_constantBufferWeakPtr = nullptr;
    Offset m_constantBufferOffset = 0;
    Size m_constantBufferSize = 0;

    /// Dirty bit tracking whether the constant buffer needs to be updated.
    bool m_isConstantBufferDirty = true;

    /// Get the layout of this shader object with specialization arguments considered
    ///
    /// This operation should only be called after the shader object has been
    /// fully filled in and finalized.
    ///
    Result getSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

    /// Create the layout for this shader object with specialization arguments considered
    ///
    /// This operation is virtual so that it can be customized by `RootShaderObject`.
    ///
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

    RefPtr<ShaderObjectLayoutImpl> m_specializedLayout;
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
    typedef ShaderObjectImpl Super;

public:
    RootShaderObjectLayoutImpl* getLayout();

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;
    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

public:
    void setResourceStates(StateTracking& stateTracking);

    Result bindAsRoot(BindingContext& context, RootShaderObjectLayoutImpl* specializedLayout);

public:
    Result init(DeviceImpl* device, RootShaderObjectLayoutImpl* layout);

protected:
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout) override;

    std::vector<RefPtr<ShaderObjectImpl>> m_entryPoints;
};

class BindingDataImpl : public BindingData
{
public:
    struct RootCBV
    {
        UINT index;
        D3D12_GPU_VIRTUAL_ADDRESS bufferLocation;
    };
    struct RootUAV
    {
        UINT index;
        D3D12_GPU_VIRTUAL_ADDRESS bufferLocation;
    };
    struct RootSRV
    {
        UINT index;
        D3D12_GPU_VIRTUAL_ADDRESS bufferLocation;
    };
    struct RootDescriptorTable
    {
        UINT index;
        D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor;
    };

    short_vector<RootCBV> rootCbvs;
    short_vector<RootUAV> rootUavs;
    short_vector<RootSRV> rootSrvs;
    short_vector<RootDescriptorTable> rootDescriptorTables;
};

class BindingCache : public RefObject
{};

struct BindingContext
{
    DeviceImpl* device;

    BufferPool<DeviceImpl, BufferImpl>* constantBufferPool;
    BufferPool<DeviceImpl, BufferImpl>* uploadBufferPool;

    D3D12DescriptorHeap* viewHeap;
    D3D12DescriptorHeap* samplerHeap;
    D3D12_DESCRIPTOR_HEAP_TYPE outOfMemoryHeap;

    short_vector<PendingDescriptorTableBinding>* pendingTableBindings;

    BindingDataImpl* currentBindingData;

    Result allocateConstantBuffer(size_t size, BufferImpl*& outBufferWeakPtr, size_t& outOffset)
    {
        auto allocation = constantBufferPool->allocate(size);
        outBufferWeakPtr = allocation.resource;
        outOffset = allocation.offset;
        return SLANG_OK;
    }

    Result writeBuffer(BufferImpl* buffer, size_t offset, size_t size, void const* data)
    {
        auto allocation = uploadBufferPool->allocate(size);
        ID3D12Resource* stagingBuffer = allocation.resource->m_resource.getResource();
        D3D12_RANGE readRange = {};
        readRange.Begin = 0;
        readRange.End = 0;
        void* uploadData;
        SLANG_RETURN_ON_FAIL(stagingBuffer->Map(0, &readRange, reinterpret_cast<void**>(&uploadData)));
        memcpy((uint8_t*)uploadData + allocation.offset, data, size);
        D3D12_RANGE writtenRange = {};
        writtenRange.Begin = allocation.offset;
        writtenRange.End = allocation.offset + size;
        stagingBuffer->Unmap(0, &writtenRange);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.pResource = buffer->m_resource.getResource();
        recorder->m_cmdList->ResourceBarrier(1, &barrier);

        recorder->m_cmdList
            ->CopyBufferRegion(buffer->m_resource.getResource(), offset, stagingBuffer, allocation.offset, size);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        recorder->m_cmdList->ResourceBarrier(1, &barrier);

        return SLANG_OK;
    }

    void copyDescriptors(
        UINT count,
        D3D12_CPU_DESCRIPTOR_HANDLE dst,
        D3D12_CPU_DESCRIPTOR_HANDLE src,
        D3D12_DESCRIPTOR_HEAP_TYPE type
    )
    {
        // device->m_device->CopyDescriptorsSimple(count, dst, src, type);
    }

    void createConstantBufferView(
        D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation,
        UINT size,
        D3D12_CPU_DESCRIPTOR_HANDLE dst
    )
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc = {};
        viewDesc.BufferLocation = gpuBufferLocation;
        viewDesc.SizeInBytes = size;
        // device->m_device->CreateConstantBufferView(&viewDesc, dst);
    }

    void setRootConstantBufferView(UINT index, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
    {
        auto& rootCbvs = currentBindingData->rootCbvs;
        if (index >= rootCbvs.size())
            rootCbvs.resize(index + 1);
        rootCbvs[index] = {index, bufferLocation};
    }

    void setRootUAV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
    {
        auto& rootUavs = currentBindingData->rootUavs;
        if (index >= rootUavs.size())
            rootUavs.resize(index + 1);
        rootUavs[index] = {index, bufferLocation};
    }

    void setRootSRV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
    {
        auto& rootSrvs = currentBindingData->rootSrvs;
        if (index >= rootSrvs.size())
            rootSrvs.resize(index + 1);
        rootSrvs[index] = {index, bufferLocation};
    }

    void setRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE descriptor)
    {
        auto& rootDescriptorTables = currentBindingData->rootDescriptorTables;
        if (index >= rootDescriptorTables.size())
            rootDescriptorTables.resize(index + 1);
        rootDescriptorTables[index] = {index, descriptor};
    }
};

} // namespace rhi::d3d12
