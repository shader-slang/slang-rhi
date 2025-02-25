#pragma once

#include "d3d12-base.h"
#include "d3d12-shader-object-layout.h"
#include "d3d12-constant-buffer-pool.h"

#include "../buffer-pool.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::d3d12 {

/// A reprsentation of an allocated descriptor set, consisting of an option resource table and
/// an optional sampler table
struct DescriptorSet
{
    GPUDescriptorRange resources;
    GPUDescriptorRange samplers;
};


struct BindingDataBuilder
{
    DeviceImpl* m_device;
    ArenaAllocator* m_allocator;
    BindingCache* m_bindingCache;
    BindingDataImpl* m_bindingData;
    ConstantBufferPool* m_constantBufferPool;
    GPUDescriptorArena* m_cbvSrvUavArena;
    GPUDescriptorArena* m_samplerArena;

    Result bindAsRoot(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );

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
        ShaderObject* shaderObject,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout,
        DescriptorSet& outDescriptorSet
    );

    /// Bind this object as a `ConstantBuffer<X>`
    Result bindAsConstantBuffer(
        ShaderObject* shaderObject,
        const DescriptorSet& descriptorSet,
        const BindingOffset& inOffset,
        uint32_t& rootParamIndex,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a `ParameterBlock<X>`
    Result bindAsParameterBlock(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        uint32_t& rootParamIndex,
        ShaderObjectLayoutImpl* specializedLayout
    );

    Result bindAsValue(
        ShaderObject* shaderObject,
        const DescriptorSet& descriptorSet,
        const BindingOffset& offset,
        uint32_t& rootParamIndex,
        ShaderObjectLayoutImpl* specializedLayout
    );

    Result bindOrdinaryDataBufferIfNeeded(
        ShaderObject* shaderObject,
        const DescriptorSet& descriptorSet,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );
};

struct BindingDataImpl : BindingData
{
public:
    struct BufferState
    {
        BufferImpl* buffer;
        ResourceState state;
    };
    struct TextureState
    {
        TextureViewImpl* textureView;
        ResourceState state;
    };

    struct RootParameter
    {
        enum Type
        {
            CBV,
            UAV,
            SRV,
            DescriptorTable,
        };
        Type type;
        UINT index;
        union
        {
            D3D12_GPU_VIRTUAL_ADDRESS bufferLocation;
            D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor;
        };
    };

    /// Required buffer states.
    BufferState* bufferStates;
    uint32_t bufferStateCount;
    /// Required texture states.
    TextureState* textureStates;
    uint32_t textureStateCount;
    /// Root parameters.
    RootParameter* rootParameters;
    uint32_t rootParameterCount;
};

struct BindingCache
{
    void reset() {}
};

} // namespace rhi::d3d12
