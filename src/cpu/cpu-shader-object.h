#pragma once

#include "cpu-base.h"
#include "cpu-shader-object-layout.h"
#include "cpu-buffer.h"

namespace rhi::cpu {

class CPUShaderObjectData
{
public:
    /// Any "ordinary" / uniform data for this object
    std::vector<uint8_t> m_ordinaryData;
    RefPtr<BufferImpl> m_buffer;

    Index getCount();
    void setCount(Index count);
    uint8_t* getBuffer();

    ~CPUShaderObjectData();

    /// Returns a StructuredBuffer resource view for GPU access into the buffer content.
    /// Creates a StructuredBuffer resource if it has not been created.
    Buffer* getBufferResource(
        Device* device,
        slang::TypeLayoutReflection* elementLayout,
        slang::BindingType bindingType
    );
};

class ShaderObjectImpl : public ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, CPUShaderObjectData>
{
    typedef ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, CPUShaderObjectData> Super;

public:
    std::vector<RefPtr<Resource>> m_resources;

    virtual SLANG_NO_THROW Result SLANG_MCALL init(DeviceImpl* device, ShaderObjectLayoutImpl* typeLayout);

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;

    virtual SLANG_NO_THROW size_t SLANG_MCALL getSize() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    setData(ShaderOffset const& offset, void const* data, size_t size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(ShaderOffset const& offset, IShaderObject* object) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) override;

    uint8_t* getDataBuffer();
};

class EntryPointShaderObjectImpl : public ShaderObjectImpl
{
public:
    EntryPointLayoutImpl* getLayout();
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
public:
    // An overload for the `init` virtual function, with a more specific type
    Result init(DeviceImpl* device, RootShaderObjectLayoutImpl* programLayout);
    using ShaderObjectImpl::init;

    RootShaderObjectLayoutImpl* getLayout();

    EntryPointShaderObjectImpl* getEntryPoint(Index index);
    std::vector<RefPtr<EntryPointShaderObjectImpl>> m_entryPoints;

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;
    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;
};

} // namespace rhi::cpu
