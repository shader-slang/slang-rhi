#pragma once

#include "vk-base.h"
#include "vk-helper-functions.h"
#include "vk-buffer.h"
#include "vk-texture.h"
#include "vk-sampler.h"
#include "vk-shader-object-layout.h"
#include "vk-acceleration-structure.h"

#include "../state-tracking.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::vk {

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
public:
    static Result create(DeviceImpl* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject);

    Device* getDevice();

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;

    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() override;

    // TODO: Changed size_t to Size? inSize assigned to an Index variable inside implementation
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
    /// Bind the ordinary data buffer if needed.
    Result bindOrdinaryDataBufferIfNeeded(
        BindingContext& context,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    /// Bind this shader object as a "value"
    ///
    /// This is the mode used for binding sub-objects for existential-type
    /// fields, and is also used as part of the implementation of the
    /// parameter-block and constant-buffer cases.
    ///
    Result bindAsValue(BindingContext& context, const BindingOffset& offset, ShaderObjectLayoutImpl* specializedLayout)
        const;

    /// Allocate the descriptor sets needed for binding this object (but not nested parameter
    /// blocks)
    Result allocateDescriptorSets(
        BindingContext& context,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    /// Bind this object as a `ParameterBlock<X>`.
    Result bindAsParameterBlock(
        BindingContext& context,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    /// Bind this object as a `ConstantBuffer<X>`.
    Result bindAsConstantBuffer(
        BindingContext& context,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    void setResourceStates(BindingContext& context) const;

    std::vector<ResourceSlot> m_slots;

    /// Get the layout of this shader object with specialization arguments considered
    ///
    /// This operation should only be called after the shader object has been
    /// fully filled in and finalized.
    ///
    Result _getSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

    /// Create the layout for this shader object with specialization arguments considered
    ///
    /// This operation is virtual so that it can be customized by `ProgramVars`.
    ///
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

    RefPtr<ShaderObjectLayoutImpl> m_specializedLayout;
};

class EntryPointShaderObject : public ShaderObjectImpl
{
    typedef ShaderObjectImpl Super;

public:
    static Result create(DeviceImpl* device, EntryPointLayout* layout, EntryPointShaderObject** outShaderObject);

    EntryPointLayout* getLayout();

    /// Bind this shader object as an entry point
    Result bindAsEntryPoint(BindingContext& context, const BindingOffset& inOffset, EntryPointLayout* layout);

protected:
    Result init(DeviceImpl* device, EntryPointLayout* layout);
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
    using Super = ShaderObjectImpl;

public:
    RootShaderObjectLayout* getLayout();

    RootShaderObjectLayout* getSpecializedLayout();

    const std::vector<RefPtr<EntryPointShaderObject>>& getEntryPoints() const;

    virtual GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;
    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

    void setResourceStates(BindingContext& context) const;

    /// Bind this object as a root shader object
    Result bindAsRoot(BindingContext& context, RootShaderObjectLayout* layout, BindingDataImpl*& outBindingData);

public:
    Result init(DeviceImpl* device, RootShaderObjectLayout* layout);

protected:
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout) override;

    std::vector<RefPtr<EntryPointShaderObject>> m_entryPoints;
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

    short_vector<BufferInfo> buffers;
    short_vector<TextureInfo> textures;

    /// Layout associated with the root object.
    RefPtr<RootShaderObjectLayout> layout;
    /// Descriptor sets that represent all the bindings the root object.
    short_vector<VkDescriptorSet> descriptorSets;
    struct PushConstant
    {
        VkPushConstantRange range;
        const void* data;
    };
    /// Push constants that represent all the push constants the root object.
    short_vector<PushConstant> pushConstants;
};

struct BindingCache : public RefObject
{
    std::vector<RefPtr<BindingData>> bindingData;

    PagedAllocator allocator;

    void reset()
    {
        bindingData.clear();
        allocator.reset();
    }
};

/// Context information required when binding shader objects to the pipeline
struct BindingContext
{
    DeviceImpl* device;

    CommandList* commandList;

    BindingCache* bindingCache;

    /// An allocator to use for descriptor sets during binding
    DescriptorSetAllocator* descriptorSetAllocator;

    BufferPool<DeviceImpl, BufferImpl>* constantBufferPool;
    BufferPool<DeviceImpl, BufferImpl>* uploadBufferPool;

    BindingDataImpl* currentBindingData;

    span<const VkPushConstantRange> pushConstantRanges;

    BindingContext(
        DeviceImpl* device,
        CommandList* commandList,
        BindingCache* bindingCache,
        DescriptorSetAllocator* descriptorSetAllocator,
        BufferPool<DeviceImpl, BufferImpl>* constantBufferPool,
        BufferPool<DeviceImpl, BufferImpl>* uploadBufferPool
    )
        : device(device)
        , commandList(commandList)
        , bindingCache(bindingCache)
        , descriptorSetAllocator(descriptorSetAllocator)
        , constantBufferPool(constantBufferPool)
        , uploadBufferPool(uploadBufferPool)
    {
    }

    void setBufferState(BufferImpl* buffer, ResourceState state)
    {
        currentBindingData->buffers.push_back({buffer, state});
    }

    void setTextureState(TextureViewImpl* textureView, ResourceState state)
    {
        currentBindingData->textures.push_back({textureView, state});
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

        auto& api = device->m_api;

        void* mappedData = nullptr;
        SLANG_VK_RETURN_ON_FAIL(api.vkMapMemory(
            api.m_device,
            allocation.resource->m_buffer.m_memory,
            allocation.offset,
            size,
            0,
            &mappedData
        ));
        memcpy((char*)mappedData, data, size);
        api.vkUnmapMemory(api.m_device, allocation.resource->m_buffer.m_memory);

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

    void writePushConstants(VkPushConstantRange range, const void* data)
    {
        void* dataCopy = bindingCache->allocator.allocate(range.size);
        std::memcpy(dataCopy, data, range.size);
        currentBindingData->pushConstants.push_back({range, dataCopy});
    }
};

} // namespace rhi::vk
