#pragma once

#include "wgpu-base.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-constant-buffer-pool.h"

#include <vector>

namespace rhi::wgpu {

struct BindingDataBuilder
{
    DeviceImpl* m_device;
    ArenaAllocator* m_allocator;
    BindingCache* m_bindingCache;
    BindingDataImpl* m_bindingData;
    CommandList* m_commandList;
    ConstantBufferPool* m_constantBufferPool;

    span<WGPUBindGroupLayout> m_bindGroupLayouts;

    /// The bind group entries for every descriptor set
    std::vector<std::vector<WGPUBindGroupEntry>> m_entries;

    /// Bind this object as a root shader object
    Result bindAsRoot(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );

    /// Allocate the descriptor sets needed for binding this object (but not nested parameter
    /// blocks)
    Result allocateDescriptorSets(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    Result createBindGroups();

    /// Bind this shader object as a "value"
    ///
    /// This is the mode used for binding sub-objects for existential-type
    /// fields, and is also used as part of the implementation of the
    /// parameter-block and constant-buffer cases.
    ///
    Result bindAsValue(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a `ParameterBlock<X>`.
    Result bindAsParameterBlock(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind the ordinary data buffer if needed.
    Result bindOrdinaryDataBufferIfNeeded(
        ShaderObject* shaderObject,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a `ConstantBuffer<X>`.
    Result bindAsConstantBuffer(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this shader object as an entry point
    Result bindAsEntryPoint(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        EntryPointLayout* specializedLayout
    );
};

struct BindingDataImpl : BindingData
{
    size_t bindGroupCount;
    WGPUBindGroup* bindGroups;

    void release(DeviceImpl* device);
};

struct BindingCache
{
    std::vector<BindingDataImpl*> bindingData;

    void reset(DeviceImpl* device);
};

} // namespace rhi::wgpu
