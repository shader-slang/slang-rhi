#pragma once

#include "metal-base.h"
#include "metal-shader-object-layout.h"

#include <vector>

namespace rhi::metal {

struct BindingDataBuilder
{
    DeviceImpl* m_device;
    ArenaAllocator* m_allocator;
    BindingCache* m_bindingCache;
    BindingDataImpl* m_bindingData;

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
        BufferImpl*& outArgumentBuffer
    );

    Result writeOrdinaryDataIntoArgumentBuffer(
        slang::TypeLayoutReflection* argumentBufferTypeLayout,
        slang::TypeLayoutReflection* defaultTypeLayout,
        uint8_t* argumentBuffer,
        uint8_t* srcData
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
};

struct BindingCache
{
    std::vector<RefPtr<BufferImpl>> buffers;

    void reset() { buffers.clear(); }
};

} // namespace rhi::metal
