#pragma once

#include "../binding-data-storage.h"

#include "d3d11-base.h"
#include "d3d11-buffer.h"
#include "d3d11-shader-object-layout.h"
#include "d3d11-constant-buffer-pool.h"

namespace rhi::d3d11 {

struct PreparedBindingData;

/// Borrows allocations and resource lifetimes from a command buffer or prepared record.
class BindingDataStorage : public rhi::BindingDataStorage
{
public:
    explicit BindingDataStorage(CommandBufferImpl& commandBuffer);
    BindingDataStorage(DeviceImpl* device, PreparedShaderObject& prepared);
    DeviceImpl* getDevice() const { return m_device; }
    // Keep this inline: an out-of-line storage helper adds measurable binding overhead on D3D11.
    SLANG_FORCE_INLINE Result
    writeOrdinaryData(ShaderObject* object, ShaderObjectLayout* layout, Size size, UniformData& outData)
    {
        outData = {};
        if (object->isFinalized())
        {
            Buffer* buffer;
            SLANG_RETURN_ON_FAIL(
                object->getOrdinaryDataBuffer(layout, ((size + 255) / 256) * 256, buffer, MemoryType::Upload)
            );
            retain(buffer);
            outData.buffer = buffer;
            return SLANG_OK;
        }
        if (isPersistent())
            return SLANG_E_INVALID_ARG;
        ConstantBufferPool::Allocation allocation;
        SLANG_RETURN_ON_FAIL(m_constantBufferPool->allocate(size, allocation));
        SLANG_RETURN_ON_FAIL(object->writeOrdinaryData(allocation.mappedData, size, layout));
        outData = {allocation.buffer, allocation.offset};
        return SLANG_OK;
    }

private:
    DeviceImpl* m_device;
    ConstantBufferPool* m_constantBufferPool = nullptr;
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
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );
    Result bindAsConstantBufferImpl(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    Result prepareConstantBuffer(
        ShaderObject* shaderObject,
        ShaderObjectLayoutImpl* specializedLayout,
        const BindingDataImpl*& outData
    );
    void composeConstantBuffer(const BindingDataImpl& data, const BindingOffset& offset);

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

private:
    Result bindAsRootImpl(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );
};

struct BindingDataImpl : BindingData
{
public:
    UINT cbvCount;
    ID3D11Buffer* cbvsBuffer[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT cbvsFirst[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT cbvsCount[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT srvCount;
    ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    UINT uavCount;
    ID3D11UnorderedAccessView* uavs[D3D11_PS_CS_UAV_REGISTER_COUNT];
    UINT samplerCount;
    ID3D11SamplerState* samplers[D3D11_COMMONSHADER_SAMPLER_REGISTER_COUNT];
};

struct BindingCache : public RefObject
{
    void reset();
};

} // namespace rhi::d3d11
