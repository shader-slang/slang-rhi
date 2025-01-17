#pragma once

#include "cuda-base.h"
#include "cuda-buffer.h"
#include "cuda-texture.h"

namespace rhi::cuda {

class ShaderObjectData
{
public:
    Device* device = nullptr;
    bool isHostOnly = false;
    RefPtr<BufferImpl> m_buffer;
    std::vector<uint8_t> m_cpuBuffer;

    Result setCount(Index count);
    Index getCount();
    void* getBuffer();

    /// Returns a resource view for GPU access into the buffer content.
    Buffer* getBufferResource(
        Device* device,
        slang::TypeLayoutReflection* elementLayout,
        slang::BindingType bindingType
    );
};

class ShaderObjectImpl : public ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, ShaderObjectData>
{
    typedef ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, ShaderObjectData> Super;

public:
    std::vector<RefPtr<Resource>> m_resources;

    virtual SLANG_NO_THROW Result SLANG_MCALL init(DeviceImpl* device, ShaderObjectLayoutImpl* typeLayout);

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;

    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setData(const ShaderOffset& offset, const void* data, Size size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(const ShaderOffset& offset, Binding binding) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(const ShaderOffset& offset, IShaderObject* object) override;
};

class EntryPointShaderObjectImpl : public ShaderObjectImpl
{
public:
    EntryPointShaderObjectImpl();
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
public:
    std::vector<RefPtr<EntryPointShaderObjectImpl>> entryPointObjects;
    virtual SLANG_NO_THROW Result SLANG_MCALL init(DeviceImpl* device, ShaderObjectLayoutImpl* typeLayout) override;
    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;
    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;
};

} // namespace rhi::cuda
