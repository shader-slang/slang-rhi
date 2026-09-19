#pragma once

#include "../binding-data-storage.h"

#include "metal-base.h"
#include "metal-shader-object-layout.h"

#include <vector>

namespace rhi::metal {

struct PreparedBindingData;

struct BufferData
{
    BufferImpl* buffer = nullptr;
    Offset offset = 0;
    void* mappedData = nullptr;
};

/// Borrows allocations and resource lifetimes from a command buffer or prepared record.
class BindingDataStorage : public rhi::BindingDataStorage
{
public:
    explicit BindingDataStorage(CommandBufferImpl& commandBuffer);
    BindingDataStorage(DeviceImpl* device, PreparedBindingData& prepared);
    DeviceImpl* getDevice() const { return m_device; }
    Result writeOrdinaryData(ShaderObject* object, ShaderObjectLayout* layout, Size size, BufferData& outData);
    Result allocateBuffer(Size size, BufferData& outData);
    void retainBuffer(BufferImpl* buffer);

private:
    DeviceImpl* m_device;
    BindingCache& m_bindingCache;
};

struct BindingDataBuilder
{
    explicit BindingDataBuilder(BindingDataStorage& storage)
        : m_storage(storage)
        , m_device(storage.getDevice())
    {
    }
    BindingDataBuilder(const BindingDataBuilder&) = delete;
    BindingDataBuilder& operator=(const BindingDataBuilder&) = delete;

    BindingDataStorage& m_storage;
    DeviceImpl* const m_device;
    BindingDataImpl* m_bindingData = nullptr;

    /// Bind this object as a root shader object
    Result bindAsRoot(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );

    /// Bind this object as if it was declared as a `ConstantBuffer<T>` in Slang
    Result bindAsConstantBuffer(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as if it was declared as a `ParameterBlock<T>` in Slang
    Result bindAsParameterBlock(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a value that appears in the body of another object.
    ///
    /// This case is directly used when binding an object for an interface-type
    /// sub-object range when static specialization is used. It is also used
    /// indirectly when binding sub-objects to constant buffer or parameter
    /// block ranges.
    ///
    Result bindAsValue(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind the buffer for ordinary/uniform data, if needed
    ///
    /// The `ioOffset` parameter will be updated to reflect the constant buffer
    /// register consumed by the ordinary data buffer, if one was bound.
    ///
    Result bindOrdinaryDataBufferIfNeeded(
        ShaderObject* shaderObject,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    Result writeArgumentBuffer(
        ShaderObject* shaderObject,
        ShaderObjectLayoutImpl* specializedLayout,
        BufferData& outArgumentBuffer
    );
    Result writeArgumentBufferImpl(
        ShaderObject* shaderObject,
        ShaderObjectLayoutImpl* specializedLayout,
        BufferData& outArgumentBuffer
    );

    Result writeOrdinaryDataIntoArgumentBuffer(
        slang::TypeLayoutReflection* argumentBufferTypeLayout,
        slang::TypeLayoutReflection* defaultTypeLayout,
        uint8_t* argumentBuffer,
        uint8_t* srcData
    );

    Result resolvePointerFieldResidency(ShaderObject* shaderObject, ShaderObjectLayoutImpl* specializedLayout);

private:
    Result bindAsRootImpl(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );
};

struct BindingDataImpl : BindingData
{
    MTL::Buffer** buffers;
    NS::UInteger* bufferOffsets;
    uint32_t bufferCount;
    uint32_t bufferCapacity;

    MTL::Texture** textures;
    uint32_t textureCount;
    uint32_t textureCapacity;

    MTL::SamplerState** samplers;
    uint32_t samplerCount;

    MTL::Resource** usedResources;
    uint32_t usedResourceCount;
    uint32_t usedResourceCapacity;
    MTL::Resource** usedRWResources;
    uint32_t usedRWResourceCount;
    uint32_t usedRWResourceCapacity;

    // Root-level acceleration structures bound via setAccelerationStructure:atBufferIndex:
    MTL::AccelerationStructure** rootAccelerationStructures;
    NS::UInteger* rootAccelerationStructureSlots;
    uint32_t rootAccelerationStructureCount;
    uint32_t rootAccelerationStructureCapacity;
};

struct BindingCache
{
    std::vector<RefPtr<BufferImpl>> buffers;

    void reset() { buffers.clear(); }
};

/// Keeps native allocations alive through preparation failure and recorded command retirement.
struct PreparedBindingData : PreparedShaderObject
{
    BindingDataImpl* bindingData = nullptr;
    BindingCache bindingCache;
};

} // namespace rhi::metal
