#pragma once

#include "d3d12-base.h"
#include "d3d12-helper-functions.h"
#include "d3d12-buffer.h"

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

struct ResourceSlot
{
    BindingType type = BindingType::Unknown;
    RefPtr<Resource> resource;
    RefPtr<Resource> resource2;
    Format format = Format::Unknown;
    union
    {
        BufferRange bufferRange = kEntireBuffer;
    };
    ResourceState requiredState = ResourceState::Undefined;
    operator bool() const { return type != BindingType::Unknown && resource; }
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
    setData(const ShaderOffset& inOffset, const void* data, size_t inSize) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(const ShaderOffset& offset, Binding binding) override;

protected:
    Result init(DeviceImpl* device, ShaderObjectLayoutImpl* layout);

    /// Write the uniform/ordinary data of this object into the given `dest` buffer at the given
    /// `offset`
    Result _writeOrdinaryData(
        BindingContext& context,
        BufferImpl* buffer,
        Offset offset,
        Size destSize,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

public:
    Result bindOrdinaryDataBufferIfNeeded(
        BindingContext& context,
        const DescriptorSet& descriptorSet,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    Result bindAsValue2(
        BindingContext& context,
        const DescriptorSet& descriptorSet,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

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
    Result allocateDescriptorSets(
        BindingContext& context,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout,
        DescriptorSet& outDescriptorSet
    ) const;

#if 0
    Result prepareToBindAsParameterBlock(
        BindingContext& context,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout,
        DescriptorSet& outDescriptorSet
    );
#endif

    /// Bind this object as a `ParameterBlock<X>`
    Result bindAsParameterBlock(
        BindingContext& context,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a `ConstantBuffer<X>`
    Result bindAsConstantBuffer(
        BindingContext& context,
        const DescriptorSet& descriptorSet,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a value (for an interface-type parameter)
    Result bindAsValue(
        BindingContext& context,
        const DescriptorSet& descriptorSet,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Shared logic for `bindAsConstantBuffer()` and `bindAsValue()`
    Result _bindImpl(
        BindingContext& context,
        const DescriptorSet& descriptorSet,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

#if 0
    Result bindRootArguments(BindingContext& context, uint32_t& index);
#endif

    void setResourceStates(BindingContext& context);

#if 0
    /// A CPU-memory descriptor set holding any descriptors used to represent the
    /// resources/samplers in this object's state
    DescriptorSet m_descriptorSet;
    /// A cached descriptor set on GPU heap.
    DescriptorSet m_cachedGPUDescriptorSet;
#endif

    std::vector<ResourceSlot> m_resources;
    std::vector<RefPtr<SamplerImpl>> m_samplers;

#if 0
    short_vector<BoundResource, 16> m_boundResources;
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> m_rootArguments;
#endif

#if 0
    /// A constant buffer used to stored ordinary data for this object
    /// and existential-type sub-objects.
    ///
    /// Allocated from transient heap on demand with `_createOrdinaryDataBufferIfNeeded()`
    BufferImpl* m_constantBufferWeakPtr = nullptr;
    Offset m_constantBufferOffset = 0;
    Size m_constantBufferSize = 0;

    /// Dirty bit tracking whether the constant buffer needs to be updated.
    bool m_isConstantBufferDirty = true;
#endif

    /// Get the layout of this shader object with specialization arguments considered
    ///
    /// This operation should only be called after the shader object has been
    /// fully filled in and finalized.
    ///
    Result _getSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

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

    RootShaderObjectLayoutImpl* getSpecializedLayout();

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;
    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

    void setResourceStates(BindingContext& context);

    Result bindAsRoot(
        BindingContext& context,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );

public:
    Result init(DeviceImpl* device, RootShaderObjectLayoutImpl* layout);

protected:
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout) override;

    std::vector<RefPtr<ShaderObjectImpl>> m_entryPoints;
};

class BindingDataImpl : public BindingData
{
public:
    struct BufferInfo
    {
        RefPtr<BufferImpl> buffer;
        ResourceState state;
    };
    struct TextureInfo
    {
        RefPtr<TextureViewImpl> textureView;
        ResourceState state;
    };

    struct RootDescriptor
    {
        enum Type
        {
            CBV,
            UAV,
            SRV,
        } type;
        UINT index;
        D3D12_GPU_VIRTUAL_ADDRESS bufferLocation;
    };

    struct RootDescriptorTable
    {
        UINT index;
        D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor;
    };

    short_vector<BufferInfo> buffers;
    short_vector<TextureInfo> textures;
    short_vector<RootDescriptor> rootDescriptors;
    short_vector<RootDescriptorTable> rootDescriptorTables;

    short_vector<DescriptorSet> descriptorSets;
};

struct BindingCache : public RefObject
{
    std::vector<RefPtr<BindingDataImpl>> bindingData;

    void reset() { bindingData.clear(); }
};

struct BindingContext
{
    DeviceImpl* device;

    CommandList* commandList;

    BindingCache* bindingCache;

    BufferPool<DeviceImpl, BufferImpl>* constantBufferPool;
    BufferPool<DeviceImpl, BufferImpl>* uploadBufferPool;

    D3D12DescriptorHeap* viewHeap;
    D3D12DescriptorHeap* samplerHeap;
    D3D12_DESCRIPTOR_HEAP_TYPE outOfMemoryHeap;

#if 0
    short_vector<PendingDescriptorTableBinding>* pendingTableBindings;
#endif

    BindingDataImpl* currentBindingData;

    BindingContext(
        DeviceImpl* device,
        CommandList* commandList,
        BindingCache* bindingCache,
        BufferPool<DeviceImpl, BufferImpl>* constantBufferPool,
        BufferPool<DeviceImpl, BufferImpl>* uploadBufferPool,
        D3D12DescriptorHeap* viewHeap,
        D3D12DescriptorHeap* samplerHeap
    )
        : device(device)
        , commandList(commandList)
        , bindingCache(bindingCache)
        , constantBufferPool(constantBufferPool)
        , uploadBufferPool(uploadBufferPool)
        , viewHeap(viewHeap)
        , samplerHeap(samplerHeap)
    {
    }

    Result allocateConstantBuffer(size_t size, BufferImpl*& outBufferWeakPtr, size_t& outOffset)
    {
        auto allocation = constantBufferPool->allocate(size);
        outBufferWeakPtr = allocation.resource;
        outOffset = allocation.offset;
        return SLANG_OK;
    }

    Result writeBuffer(BufferImpl* buffer, size_t offset, size_t size, const void* data)
    {
        if (size <= 0)
            return SLANG_OK;

        auto allocation = uploadBufferPool->allocate(size);

        ID3D12Resource* resource = allocation.resource->m_resource.getResource();
        D3D12_RANGE readRange = {};
        void* mappedData = nullptr;
        SLANG_RETURN_ON_FAIL(resource->Map(0, &readRange, reinterpret_cast<void**>(&mappedData)));
        memcpy((uint8_t*)mappedData + allocation.offset, data, size);
        D3D12_RANGE writtenRange = {};
        writtenRange.Begin = allocation.offset;
        writtenRange.End = allocation.offset + size;
        resource->Unmap(0, &writtenRange);

        // Write a command to copy the data from the staging buffer to the real buffer
        commands::CopyBuffer cmd;
        cmd.dst = buffer;
        cmd.dstOffset = offset;
        cmd.src = allocation.resource;
        cmd.srcOffset = allocation.offset;
        cmd.size = size;
        commandList->write(std::move(cmd));

        return SLANG_OK;
    }

#if 0
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
#endif

    void setBufferState(BufferImpl* buffer, ResourceState state)
    {
        currentBindingData->buffers.push_back({buffer, state});
    }

    void setTextureState(TextureViewImpl* textureView, ResourceState state)
    {
        currentBindingData->textures.push_back({textureView, state});
    }

    void setRootDescriptor(
        UINT index,
        D3D12_GPU_VIRTUAL_ADDRESS bufferLocation,
        BindingDataImpl::RootDescriptor::Type type
    )
    {
        auto& rootDescriptors = currentBindingData->rootDescriptors;
        if (index >= rootDescriptors.size())
            rootDescriptors.resize(index + 1);
        rootDescriptors[index] = {type, index, bufferLocation};
    }

    void setRootUAV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
    {
        setRootDescriptor(index, bufferLocation, BindingDataImpl::RootDescriptor::UAV);
    }

    void setRootSRV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
    {
        setRootDescriptor(index, bufferLocation, BindingDataImpl::RootDescriptor::SRV);
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
