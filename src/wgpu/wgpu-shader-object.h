#pragma once

#include "../binding-data-storage.h"

#include "wgpu-base.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-constant-buffer-pool.h"

#include <vector>

namespace rhi::wgpu {

struct ParameterBlockBindingData
{
    std::span<const WGPUBindGroup> bindGroups;
};

struct PreparedBindingData;

/// Borrows allocations and resource lifetimes from a command buffer or prepared record.
class BindingDataStorage : public rhi::BindingDataStorage
{
public:
    explicit BindingDataStorage(CommandBufferImpl& commandBuffer);
    BindingDataStorage(DeviceImpl* device, PreparedBindingData& prepared);
    DeviceImpl* getDevice() const { return m_device; }
    Result writeOrdinaryData(ShaderObject* object, ShaderObjectLayout* layout, Size size, UniformData& outData);
    BindingDataImpl* allocateBindingData();
    Result createBindGroup(
        BindingDataImpl& data,
        size_t index,
        const WGPUBindGroupDescriptor& desc,
        WGPUBindGroup existing
    );

private:
    DeviceImpl* m_device;
    ConstantBufferPool* m_constantBufferPool = nullptr;
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

    /// A group is either assembled from entries or supplied by a prepared block.
    struct BindGroup
    {
        WGPUBindGroupLayout layout = nullptr;
        std::vector<WGPUBindGroupEntry> entries;
        WGPUBindGroup existing = nullptr;
    };
    std::vector<BindGroup> m_bindGroups;

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
    Result bindAsParameterBlockImpl(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    Result prepareParameterBlock(
        ShaderObject* shaderObject,
        ShaderObjectLayoutImpl* specializedLayout,
        const ParameterBlockBindingData*& outData
    );
    void composeParameterBlock(const ParameterBlockBindingData& data);

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

private:
    Result bindAsRootImpl(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
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

/// Keeps native allocations alive through preparation failure and recorded command retirement.
struct PreparedBindingData : PreparedShaderObject
{
    ~PreparedBindingData();
    DeviceImpl* device = nullptr;
    BindingDataImpl* bindingData = nullptr;
    BindingCache bindingCache;
};

} // namespace rhi::wgpu
