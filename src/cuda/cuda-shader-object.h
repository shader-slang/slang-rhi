#pragma once

#include "cuda-base.h"
#include "cuda-buffer.h"
#include "cuda-texture-view.h"

namespace rhi::cuda {

class ShaderObjectData
{
public:
    RendererBase* device = nullptr;
    bool isHostOnly = false;
    RefPtr<BufferImpl> m_buffer;
    std::vector<uint8_t> m_cpuBuffer;

    Result setCount(Index count);
    Index getCount();
    void* getBuffer();

    /// Returns a resource view for GPU access into the buffer content.
    Buffer* getBufferResource(
        RendererBase* device,
        slang::TypeLayoutReflection* elementLayout,
        slang::BindingType bindingType
    );
};

class ShaderObjectImpl : public ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, ShaderObjectData>
{
    typedef ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, ShaderObjectData> Super;

public:
    std::vector<RefPtr<Resource>> m_resources;

    virtual SLANG_NO_THROW Result SLANG_MCALL init(IDevice* device, ShaderObjectLayoutImpl* typeLayout);

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;

    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setData(ShaderOffset const& offset, void const* data, Size size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(ShaderOffset const& offset, IShaderObject* object) override;
};

class MutableShaderObjectImpl : public MutableShaderObject<MutableShaderObjectImpl, ShaderObjectLayoutImpl>
{};

class EntryPointShaderObjectImpl : public ShaderObjectImpl
{
public:
    EntryPointShaderObjectImpl();
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
public:
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override;
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override;

public:
    std::vector<RefPtr<EntryPointShaderObjectImpl>> entryPointObjects;
    virtual SLANG_NO_THROW Result SLANG_MCALL init(IDevice* device, ShaderObjectLayoutImpl* typeLayout) override;
    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;
    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;
};

} // namespace rhi::cuda
